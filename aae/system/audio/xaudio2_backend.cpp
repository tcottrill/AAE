//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// =============================================================================
// xaudio2_backend.cpp
// Moved verbatim from mixer.cpp's former xaudio2_init / xaudio2_update /
// xaudio2_stop / GetNextBuffer block. Behavior is unchanged; the file-scope
// globals (pXAudio2, pMasterVoice, pSourceVoice, audioBuffers, bufferSize,
// currentBufferIndex, g_comInitLocal) are now private members.
//
// Master volume default is NOT set here - mixer.cpp owns the volume curve and
// applies the 80% default through its own mixer_set_master_volume after Init
// returns. Init only stands up the streaming infrastructure.
// =============================================================================
#include "xaudio2_backend.h"
#include "mixer.h"
#include "sys_log.h"
#include "wav_writer.h"
#include <xapo.h>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#define HR(hr) if (FAILED(hr)) { LOG_ERROR("Error at line %d: HRESULT = 0x%08X\n", __LINE__, hr); }

// Converts the platform-neutral WaveFormat to the WAVEFORMATEX that XAudio2's
// CreateSourceVoice requires. Used internally by VoiceCreate; mixer.cpp no
// longer calls this directly (Task 2).
static WAVEFORMATEX ToWaveFormatEx(const WaveFormat& f)
{
	WAVEFORMATEX w{};
	w.wFormatTag      = f.format_tag;
	w.nChannels       = f.channels;
	w.nSamplesPerSec  = f.rate;
	w.nAvgBytesPerSec = f.avg_bytes_sec;
	w.nBlockAlign     = f.block_align;
	w.wBitsPerSample  = f.bits;
	w.cbSize          = f.cb_size;
	return w;
}

// Opaque per-channel voice. mixer.cpp only ever holds a VoiceHandle*; the
// concrete definition (an XAudio2 source voice + its submission buffer)
// lives here. A backend with no per-voice concept (ALSA) would define this
// differently, routing Submit/Start/Stop into its own software mixer.
struct VoiceHandle {
	IXAudio2SourceVoice* voice  = nullptr;
	XAUDIO2_BUFFER       buffer = {};
};

// =============================================================================
// CaptureXAPO: -wavwrite session capture.
//
// A minimal in-place IXAPO installed on the mastering voice - the one point
// where the software-mix source voice AND every per-channel voice are already
// combined, so the recording is exactly what the game outputs (pre-master-
// volume: SetVolume applies after the effect chain, so the file level is
// independent of the user's volume setting).
//
// Process() runs on XAudio2's audio processing thread: it converts the float
// frames to S16, downmixes >2ch endpoints to stereo (ITU-style: C/surrounds
// at -3 dB, LFE dropped), and pushes the chunk into a mutex-guarded queue.
// NO file I/O happens on that thread - a dedicated writer thread drains the
// queue to a WavWriter every ~100 ms and patches the RIFF header each pass.
//
// Implemented against raw IXAPO (no CXAPOBase / xapobase.lib) to keep the
// build dependency-free for both the SDK and the WIN7BUILD redist paths.
// =============================================================================
struct CaptureXAPO final : public IXAPO {
	// --- lifetime --------------------------------------------------------
	std::atomic<ULONG> m_refs{ 1 };

	// --- format (set in LockForProcess) ----------------------------------
	uint32_t m_channels = 2;
	// Per-source-channel stereo gains, [ch][0]=L [ch][1]=R.
	float m_gains[8][2] = {};

	// Master-volume scaling. The effect chain runs BEFORE the mastering
	// voice's SetVolume, so without this the capture clips where the
	// speaker output (scaled by the 80%-default master volume) does not.
	// Kept in sync by SetMasterVolume so the file tracks what is heard.
	std::atomic<float> m_gain{ 1.0f };

	// --- capture queue (audio thread -> writer thread) -------------------
	std::mutex              m_qmutex;
	std::condition_variable m_qcv;
	std::deque<std::vector<int16_t>> m_queue;
	std::atomic<bool>       m_running{ false };
	std::thread             m_writer;
	WavWriter               m_wav;

	// ---------------------------------------------------------------------
	bool StartFile(const char* path, int rateHz)
	{
		if (!m_wav.open(path, rateHz)) return false;
		m_running.store(true, std::memory_order_release);
		m_writer = std::thread([this]() { WriterLoop(); });
		return true;
	}

	// Idempotent. Joins the writer thread and finalizes the file.
	void StopFile()
	{
		if (m_running.exchange(false, std::memory_order_acq_rel)) {
			m_qcv.notify_one();
			if (m_writer.joinable()) m_writer.join();
		}
		// Drain anything the writer missed between its last pass and join.
		std::deque<std::vector<int16_t>> rest;
		{
			std::lock_guard<std::mutex> lock(m_qmutex);
			rest.swap(m_queue);
		}
		for (const auto& chunk : rest)
			m_wav.write(chunk.data(), chunk.size() / 2);
		m_wav.close();
	}

	void WriterLoop()
	{
		std::unique_lock<std::mutex> lock(m_qmutex);
		while (m_running.load(std::memory_order_acquire)) {
			m_qcv.wait_for(lock, std::chrono::milliseconds(100));
			std::deque<std::vector<int16_t>> batch;
			batch.swap(m_queue);
			lock.unlock();
			for (const auto& chunk : batch)
				m_wav.write(chunk.data(), chunk.size() / 2);
			m_wav.flush();
			lock.lock();
		}
	}

	// --- IUnknown --------------------------------------------------------
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
	{
		if (!ppv) return E_POINTER;
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IXAPO)) {
			*ppv = static_cast<IXAPO*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return m_refs.fetch_add(1, std::memory_order_relaxed) + 1;
	}
	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG n = m_refs.fetch_sub(1, std::memory_order_acq_rel) - 1;
		if (n == 0) delete this;
		return n;
	}

	// --- IXAPO -----------------------------------------------------------
	HRESULT STDMETHODCALLTYPE GetRegistrationProperties(
		XAPO_REGISTRATION_PROPERTIES** ppProps) override
	{
		if (!ppProps) return E_POINTER;
		auto* props = static_cast<XAPO_REGISTRATION_PROPERTIES*>(
			CoTaskMemAlloc(sizeof(XAPO_REGISTRATION_PROPERTIES)));
		if (!props) return E_OUTOFMEMORY;
		std::memset(props, 0, sizeof(*props));
		wcscpy_s(props->FriendlyName, L"AAE WAV Capture");
		wcscpy_s(props->CopyrightInfo, L"AAE");
		props->MajorVersion = 1;
		props->MinorVersion = 0;
		props->Flags = XAPO_FLAG_CHANNELS_MUST_MATCH
		             | XAPO_FLAG_FRAMERATE_MUST_MATCH
		             | XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH
		             | XAPO_FLAG_BUFFERCOUNT_MUST_MATCH
		             | XAPO_FLAG_INPLACE_SUPPORTED
		             | XAPO_FLAG_INPLACE_REQUIRED;
		props->MinInputBufferCount = 1;
		props->MaxInputBufferCount = 1;
		props->MinOutputBufferCount = 1;
		props->MaxOutputBufferCount = 1;
		*ppProps = props;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE IsInputFormatSupported(
		const WAVEFORMATEX* pOutputFormat,
		const WAVEFORMATEX* pRequestedInputFormat,
		WAVEFORMATEX** ppSupportedInputFormat) override
	{
		(void)pOutputFormat; (void)pRequestedInputFormat;
		if (ppSupportedInputFormat) *ppSupportedInputFormat = nullptr;
		return S_OK; // mastering voice hands us float32 at its own layout
	}

	HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(
		const WAVEFORMATEX* pInputFormat,
		const WAVEFORMATEX* pRequestedOutputFormat,
		WAVEFORMATEX** ppSupportedOutputFormat) override
	{
		(void)pInputFormat; (void)pRequestedOutputFormat;
		if (ppSupportedOutputFormat) *ppSupportedOutputFormat = nullptr;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Initialize(const void*, UINT32) override
	{
		return S_OK;
	}

	void STDMETHODCALLTYPE Reset() override {}

	HRESULT STDMETHODCALLTYPE LockForProcess(
		UINT32 InputLockedParameterCount,
		const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* pInputLockedParameters,
		UINT32 OutputLockedParameterCount,
		const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* pOutputLockedParameters) override
	{
		(void)OutputLockedParameterCount; (void)pOutputLockedParameters;
		if (InputLockedParameterCount != 1 || !pInputLockedParameters ||
		    !pInputLockedParameters[0].pFormat)
			return E_INVALIDARG;

		m_channels = pInputLockedParameters[0].pFormat->nChannels;
		if (m_channels < 1) m_channels = 1;
		if (m_channels > 8) m_channels = 8;

		// Stereo gain table per source channel. Standard WAVEFORMATEXTENSIBLE
		// order for the common masks: FL FR C LFE BL BR SL SR. Center and
		// surrounds fold in at -3 dB (0.7071), LFE is dropped.
		constexpr float kM3dB = 0.70710678f;
		std::memset(m_gains, 0, sizeof(m_gains));
		if (m_channels == 1) {
			m_gains[0][0] = 1.0f; m_gains[0][1] = 1.0f;
		} else {
			static const float table[8][2] = {
				{ 1.0f,  0.0f  },   // FL
				{ 0.0f,  1.0f  },   // FR
				{ kM3dB, kM3dB },   // C
				{ 0.0f,  0.0f  },   // LFE
				{ kM3dB, 0.0f  },   // BL
				{ 0.0f,  kM3dB },   // BR
				{ kM3dB, 0.0f  },   // SL
				{ 0.0f,  kM3dB },   // SR
			};
			std::memcpy(m_gains, table, sizeof(table));
		}
		return S_OK;
	}

	void STDMETHODCALLTYPE UnlockForProcess() override {}

	void STDMETHODCALLTYPE Process(
		UINT32 InputProcessParameterCount,
		const XAPO_PROCESS_BUFFER_PARAMETERS* pInputProcessParameters,
		UINT32 OutputProcessParameterCount,
		XAPO_PROCESS_BUFFER_PARAMETERS* pOutputProcessParameters,
		BOOL IsEnabled) override
	{
		// In-place required: input buffer IS the output buffer; audio always
		// passes through untouched. We only read.
		if (InputProcessParameterCount != 1 || !pInputProcessParameters)
			return;
		const auto& in = pInputProcessParameters[0];
		if (OutputProcessParameterCount == 1 && pOutputProcessParameters) {
			pOutputProcessParameters[0].ValidFrameCount = in.ValidFrameCount;
			pOutputProcessParameters[0].BufferFlags = in.BufferFlags;
		}

		if (!IsEnabled || !m_running.load(std::memory_order_acquire))
			return;
		const UINT32 frames = in.ValidFrameCount;
		if (frames == 0) return;

		std::vector<int16_t> chunk(static_cast<size_t>(frames) * 2);
		if (in.BufferFlags == XAPO_BUFFER_SILENT) {
			// Silence is part of the session - record it as zeros.
			std::memset(chunk.data(), 0, chunk.size() * sizeof(int16_t));
		} else {
			// Soft-knee saturation: the float twin of mixer_soft_clip
			// (mixer.cpp). Below the -1.16 dBFS knee samples pass bit-exact;
			// only genuine overmix folds smoothly (tanh) into the remaining
			// headroom - a hard clamp here put audible clipping in the file
			// that the OS endpoint limiter keeps off the speakers.
			auto soft_clip = [](float v) -> float {
				constexpr float kKnee = 0.875f;
				constexpr float kFold = 0.125f;
				const float av = std::fabs(v);
				if (av <= kKnee) return v;
				return (v < 0.0f ? -1.0f : 1.0f)
				     * (kKnee + kFold * std::tanh((av - kKnee) / kFold));
			};

			const float* src = static_cast<const float*>(in.pBuffer);
			const float gain = m_gain.load(std::memory_order_relaxed);
			for (UINT32 f = 0; f < frames; ++f) {
				float l = 0.0f, r = 0.0f;
				for (uint32_t c = 0; c < m_channels; ++c) {
					const float s = src[f * m_channels + c];
					l += s * m_gains[c][0];
					r += s * m_gains[c][1];
				}
				l = soft_clip(l * gain);
				r = soft_clip(r * gain);
				chunk[f * 2 + 0] = static_cast<int16_t>(l * 32767.0f);
				chunk[f * 2 + 1] = static_cast<int16_t>(r * 32767.0f);
			}
		}

		{
			std::lock_guard<std::mutex> lock(m_qmutex);
			m_queue.push_back(std::move(chunk));
		}
		m_qcv.notify_one();
	}

	UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 OutputFrameCount) override
	{
		return OutputFrameCount;
	}
	UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 InputFrameCount) override
	{
		return InputFrameCount;
	}
};

bool XAudio2Backend::Init(int rateHz, int fps)
{
	HRESULT hr;

	m_rate = rateHz;
	m_fps = fps;
	m_frames_per_update = rateHz / fps;             // fixed integer frames per update

	const int remainder = rateHz % fps;
	if (remainder != 0) {
		LOG_INFO("XAudio2Backend::Init: %d Hz / %d FPS leaves remainder %d (using %d frames per update)",
			rateHz, fps, remainder, m_frames_per_update);
	}

	HRESULT hrCI = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (hrCI == S_OK || hrCI == S_FALSE) m_com_init_local = true;

	HR(XAudio2Create(&m_xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR));
	HR(m_xaudio2->CreateMasteringVoice(&m_master, XAUDIO2_DEFAULT_CHANNELS, rateHz, 0, 0));

	// Capture the actual output layout. With XAUDIO2_DEFAULT_CHANNELS the
	// channel count comes from the OS-configured endpoint (a stereo endpoint
	// reports 2; a 5.1 endpoint reports 6; a stereo endpoint with Windows
	// Sonic for Headphones enabled reports 8 because Sonic exposes itself
	// as 7.1 to applications and renders to stereo downstream).
	{
		XAUDIO2_VOICE_DETAILS details{};
		m_master->GetVoiceDetails(&details);
		m_output_channels = details.InputChannels;
		DWORD mask = 0;
		if (SUCCEEDED(m_master->GetChannelMask(&mask))) {
			m_output_channel_mask = mask;
		}
		LOG_INFO("XAudio2Backend::Init: master %u channels, mask=0x%08X",
			m_output_channels, m_output_channel_mask);
	}

	// Stereo 16-bit PCM source voice
	WAVEFORMATEX wf = {};
	wf.wFormatTag = WAVE_FORMAT_PCM;
	wf.nChannels = 2;
	wf.nSamplesPerSec = rateHz;
	wf.wBitsPerSample = 16;
	wf.nBlockAlign = wf.nChannels * wf.wBitsPerSample / 8; // 4 bytes per stereo frame
	wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
	wf.cbSize = 0;

	hr = m_xaudio2->CreateSourceVoice(&m_source, &wf, XAUDIO2_VOICE_NOPITCH,
		XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, nullptr, nullptr);
	if (FAILED(hr)) {
		LOG_ERROR("Failed to create source voice: hr=0x%08X", (unsigned)hr);
		Shutdown();
		return false;
	}

	const int buffer_duration_ms = 1000 / fps;
	LOG_INFO("XAudio2Backend::Init: FramesPerUpdate=%d (~%d ms per update)",
		m_frames_per_update, buffer_duration_ms);

	// Allocate ring buffers: frames * 4 bytes, plus one frame of headroom --
	// when rateHz % fps != 0 the mixer pays back the truncation by mixing
	// N+1 samples on some frames (see mixer_update_internal), and those
	// frames must not overrun the buffer.
	m_buffer_size = (m_frames_per_update + 1) * wf.nBlockAlign;
	for (int i = 0; i < kNumBuffers; ++i) {
		m_buffers[i] = new BYTE[m_buffer_size];
		std::memset(m_buffers[i], 0, m_buffer_size);
	}

	HR(m_source->Start());
	m_current = 0;
	return true;
}

uint8_t* XAudio2Backend::GetNextBuffer()
{
	return m_buffers[m_current];
}

bool XAudio2Backend::Submit(uint8_t* buffer, uint32_t bufferLength)
{
	if (!m_source) {
		LOG_ERROR("XAudio2Backend::Submit: no source voice");
		return false;
	}
	if (bufferLength == 0) return true;

	// Check voice state to prevent overwriting data currently being played
	XAUDIO2_VOICE_STATE state;
	m_source->GetState(&state);

	// Underrun diagnostic: an empty queue at submit time (after the first
	// few frames) means the voice ran dry and played silence -- an audible
	// gap. Rate-limited so a struggling system doesn't flood the log.
	{
		static int s_submits = 0;
		static int s_underruns = 0;
		++s_submits;
		if (state.BuffersQueued == 0 && s_submits > 3) {
			++s_underruns;
			if (s_underruns <= 10 || (s_underruns % 100) == 0) {
				LOG_INFO("XAudio2: output ran dry before submit #%d (underrun #%d)",
					s_submits, s_underruns);
			}
		}
	}

	// If we have too many buffers queued, the game loop is running too fast.
	// We should drop this frame or wait. For a game engine, dropping/skipping
	// update is usually better than stalling the main thread.
	if (state.BuffersQueued >= kNumBuffers - 1) {
		LOG_INFO("Audio warning: Ring buffer full, skipping update to prevent overwrite.");
		return true;
	}

	BYTE* payload = buffer ? buffer : m_buffers[m_current];

	// Safety clamp
	if (!buffer && bufferLength > m_buffer_size) {
		bufferLength = (uint32_t)m_buffer_size;
	}

	XAUDIO2_BUFFER xb = {};
	xb.AudioBytes = bufferLength;
	xb.pAudioData = payload;

	HRESULT hr = m_source->SubmitSourceBuffer(&xb);
	if (FAILED(hr)) {
		LOG_ERROR("XAudio2Backend::Submit: SubmitSourceBuffer failed, hr=0x%08X", (unsigned)hr);
		return false;
	}

	// Only advance index if submission succeeded
	m_current = (m_current + 1) % kNumBuffers;
	return true;
}

void XAudio2Backend::Shutdown()
{
	CaptureStop();

	if (m_source) { m_source->DestroyVoice(); m_source = nullptr; }
	if (m_master) { m_master->DestroyVoice(); m_master = nullptr; }
	if (m_xaudio2) { m_xaudio2->Release();    m_xaudio2 = nullptr; }

	for (int i = 0; i < kNumBuffers; ++i) {
		delete[] m_buffers[i];
		m_buffers[i] = nullptr;
	}
	m_buffer_size = 0;
	m_current = 0;
	m_rate = 0;
	m_fps = 0;
	m_frames_per_update = 0;
	m_output_channels = 0;
	m_output_channel_mask = 0;

	if (m_com_init_local) { CoUninitialize(); m_com_init_local = false; }
}

void XAudio2Backend::SetMasterVolume(float linear)
{
	if (m_master) m_master->SetVolume(linear);
	// Keep any -wavwrite capture at the audible level (see CaptureXAPO::m_gain).
	if (m_capture_xapo) m_capture_xapo->m_gain.store(linear, std::memory_order_relaxed);
}

float XAudio2Backend::GetMasterVolume() const
{
	float v = 1.0f;
	if (m_master) m_master->GetVolume(&v);
	return v;
}

// -----------------------------------------------------------------------------
// -wavwrite session capture (see CaptureXAPO above). Start installs the
// capture effect on the mastering voice; Stop removes it and finalizes the
// WAV. Stop is idempotent and also runs from Shutdown().
// -----------------------------------------------------------------------------
bool XAudio2Backend::CaptureStart(const char* path)
{
	if (!m_master) {
		LOG_ERROR("CaptureStart: no mastering voice");
		return false;
	}
	if (m_capture_xapo) {
		LOG_ERROR("CaptureStart: capture already running");
		return false;
	}

	auto* xapo = new CaptureXAPO();
	xapo->m_gain.store(GetMasterVolume(), std::memory_order_relaxed);
	if (!xapo->StartFile(path, m_rate)) {
		xapo->Release();
		return false;
	}

	XAUDIO2_EFFECT_DESCRIPTOR desc{};
	desc.pEffect = static_cast<IXAPO*>(xapo);
	desc.InitialState = TRUE;
	desc.OutputChannels = m_output_channels;

	XAUDIO2_EFFECT_CHAIN chain{ 1, &desc };
	const HRESULT hr = m_master->SetEffectChain(&chain);
	if (FAILED(hr)) {
		LOG_ERROR("CaptureStart: SetEffectChain failed, hr=0x%08X", (unsigned)hr);
		xapo->StopFile();
		xapo->Release();
		return false;
	}

	m_capture_xapo = xapo;  // keep our ref; the chain holds its own
	LOG_INFO("CaptureStart: recording session audio to '%s'", path);
	return true;
}

void XAudio2Backend::CaptureStop()
{
	if (!m_capture_xapo) return;

	// Detach the effect first so Process() stops being called, then stop
	// the writer and finalize the file.
	if (m_master) m_master->SetEffectChain(nullptr);
	m_capture_xapo->StopFile();
	m_capture_xapo->Release();
	m_capture_xapo = nullptr;
	LOG_INFO("CaptureStop: recording finalized");
}

// -----------------------------------------------------------------------------
// Per-channel voice path (moved from mixer.cpp as part of Task 2). mixer.cpp
// now only holds a VoiceHandle*; every XAudio2 call lives here.
// -----------------------------------------------------------------------------

VoiceHandle* XAudio2Backend::VoiceCreate(const WaveFormat& fmt)
{
	if (!m_xaudio2) return nullptr;

	auto* h = new VoiceHandle();
	const WAVEFORMATEX wfx = ToWaveFormatEx(fmt);

	// The 8.0f max frequency ratio is important here and required for the
	// StarCastle drone (extreme pitch shift).
	if (FAILED(m_xaudio2->CreateSourceVoice(&h->voice, &wfx, 0, 8.0f))) {
		delete h;
		return nullptr;
	}
	return h;
}

void XAudio2Backend::VoiceDestroy(VoiceHandle* v)
{
	if (!v) return;
	if (v->voice) v->voice->DestroyVoice();
	delete v;
}

bool XAudio2Backend::VoiceSubmit(VoiceHandle* v, const uint8_t* data,
                                 uint32_t bytes, bool loop)
{
	if (!v || !v->voice) return false;
	std::memset(&v->buffer, 0, sizeof(v->buffer));
	v->buffer.AudioBytes = bytes;
	v->buffer.pAudioData = data;
	v->buffer.LoopCount  = loop ? XAUDIO2_LOOP_INFINITE : 0;
	if (FAILED(v->voice->SubmitSourceBuffer(&v->buffer))) {
		LOG_ERROR("VoiceSubmit: SubmitSourceBuffer failed");
		return false;
	}
	return true;
}

bool XAudio2Backend::VoiceStart(VoiceHandle* v)
{
	if (!v || !v->voice) return false;
	if (FAILED(v->voice->Start())) {
		LOG_ERROR("VoiceStart: Start failed");
		return false;
	}
	return true;
}

void XAudio2Backend::VoiceStop(VoiceHandle* v)
{
	if (!v || !v->voice) return;
	v->voice->Stop();
}

void XAudio2Backend::VoiceFlush(VoiceHandle* v)
{
	if (!v || !v->voice) return;
	v->voice->FlushSourceBuffers();
}

void XAudio2Backend::VoiceExitLoop(VoiceHandle* v)
{
	if (!v || !v->voice) return;
	// Affects only buffers submitted with XAUDIO2_LOOP_INFINITE; the current
	// pass still plays to completion.
	v->voice->ExitLoop();
}

void XAudio2Backend::VoiceSetVolume(VoiceHandle* v, float gain)
{
	if (!v || !v->voice) return;
	v->voice->SetVolume(gain);
}

void XAudio2Backend::VoiceSetFrequencyRatio(VoiceHandle* v, float ratio)
{
	if (!v || !v->voice) return;
	v->voice->SetFrequencyRatio(ratio);
}

uint32_t XAudio2Backend::VoiceBuffersQueued(VoiceHandle* v)
{
	if (!v || !v->voice) return 0;
	XAUDIO2_VOICE_STATE st{};
	v->voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
	return st.BuffersQueued;
}

uint32_t XAudio2Backend::VoiceInputChannels(VoiceHandle* v)
{
	if (!v || !v->voice) return 0;
	XAUDIO2_VOICE_DETAILS details{};
	v->voice->GetVoiceDetails(&details);
	return details.InputChannels;
}

bool XAudio2Backend::VoiceSetOutputMatrix(VoiceHandle* v, uint32_t srcChannels,
                                          uint32_t dstChannels, const float* matrix)
{
	if (!v || !v->voice) return false;
	HRESULT hr = v->voice->SetOutputMatrix(nullptr, srcChannels, dstChannels, matrix);
	if (FAILED(hr)) {
		LOG_ERROR("VoiceSetOutputMatrix: SetOutputMatrix failed, hr=0x%08X", (unsigned)hr);
		return false;
	}
	return true;
}

// -----------------------------------------------------------------------------
// The Windows half of audio_backend.h's platform factory.
//
// This is the ONLY place in the Windows build that names XAudio2Backend.
// mixer.cpp used to construct it directly, which meant portable mixer code
// referred to a concrete Win32 type; it now calls create_audio_backend() and
// never learns which backend it got.
// -----------------------------------------------------------------------------
std::unique_ptr<IAudioBackend> create_audio_backend()
{
	return std::make_unique<XAudio2Backend>();
}
