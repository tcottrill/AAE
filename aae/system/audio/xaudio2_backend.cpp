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
#include "mono_stereo_width.h"
#include "output_tone.h"
#include "sys_log.h"
#include "wav_writer.h"
#include <xapo.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <string>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "ole32.lib")

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

// =============================================================================
// InPlaceFloatXAPO: shared raw-IXAPO boilerplate (same approach as CaptureXAPO,
// no xapobase.lib) for the two in-place float effects below. A derived class
// implements OnLock (format known) and Run (process frames in place).
//
// Process() handles XAPO_BUFFER_SILENT the way both effects need: the buffer
// is zeroed first and then processed, so a delay tail still plays out after
// the input goes silent, and the output is marked VALID. A disabled effect is
// a pure pass-through.
// =============================================================================
struct InPlaceFloatXAPO : public IXAPO {
	std::atomic<ULONG> m_refs{ 1 };
	uint32_t m_channels = 2;
	uint32_t m_rate     = 48000;

	virtual const wchar_t* Name() const = 0;
	virtual void OnLock() = 0;
	virtual void Run(float* buf, UINT32 frames) = 0;

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

	HRESULT STDMETHODCALLTYPE GetRegistrationProperties(
		XAPO_REGISTRATION_PROPERTIES** ppProps) override
	{
		if (!ppProps) return E_POINTER;
		auto* props = static_cast<XAPO_REGISTRATION_PROPERTIES*>(
			CoTaskMemAlloc(sizeof(XAPO_REGISTRATION_PROPERTIES)));
		if (!props) return E_OUTOFMEMORY;
		std::memset(props, 0, sizeof(*props));
		wcscpy_s(props->FriendlyName, Name());
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
		const WAVEFORMATEX*, const WAVEFORMATEX*, WAVEFORMATEX** ppSupported) override
	{
		if (ppSupported) *ppSupported = nullptr;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(
		const WAVEFORMATEX*, const WAVEFORMATEX*, WAVEFORMATEX** ppSupported) override
	{
		if (ppSupported) *ppSupported = nullptr;
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE Initialize(const void*, UINT32) override { return S_OK; }
	void STDMETHODCALLTYPE Reset() override {}

	HRESULT STDMETHODCALLTYPE LockForProcess(
		UINT32 InputLockedParameterCount,
		const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS* pInputLockedParameters,
		UINT32, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS*) override
	{
		if (InputLockedParameterCount != 1 || !pInputLockedParameters ||
		    !pInputLockedParameters[0].pFormat)
			return E_INVALIDARG;
		m_channels = pInputLockedParameters[0].pFormat->nChannels;
		m_rate     = pInputLockedParameters[0].pFormat->nSamplesPerSec;
		if (m_channels < 1 || m_channels > 8) return E_INVALIDARG;
		OnLock();
		return S_OK;
	}
	void STDMETHODCALLTYPE UnlockForProcess() override {}

	void STDMETHODCALLTYPE Process(
		UINT32 InputProcessParameterCount,
		const XAPO_PROCESS_BUFFER_PARAMETERS* pIn,
		UINT32 OutputProcessParameterCount,
		XAPO_PROCESS_BUFFER_PARAMETERS* pOut,
		BOOL IsEnabled) override
	{
		if (InputProcessParameterCount != 1 || !pIn) return;
		const UINT32 frames = pIn[0].ValidFrameCount;
		XAPO_BUFFER_FLAGS outFlags = pIn[0].BufferFlags;

		if (IsEnabled && pIn[0].pBuffer && frames > 0) {
			float* buf = static_cast<float*>(pIn[0].pBuffer);
			if (pIn[0].BufferFlags == XAPO_BUFFER_SILENT)
				std::memset(buf, 0, (size_t)frames * m_channels * sizeof(float));
			Run(buf, frames);
			outFlags = XAPO_BUFFER_VALID;
		}
		if (OutputProcessParameterCount == 1 && pOut) {
			pOut[0].ValidFrameCount = frames;
			pOut[0].BufferFlags = outFlags;
		}
	}

	UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 n) override  { return n; }
	UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 n) override { return n; }
};

// =============================================================================
// WidenXAPO: SOUNDBAR mode. Sits on a 2-channel submix ("widen bus") that only
// mono per-channel voices send to. mid = 0.5*(L+R) goes through MonoStereoWidth
// (12 ms late, 0.7, +dl to L, -dl to R) - the float twin of what voice_mixer.cpp
// does for mono voices on Linux. Dual mono in, correlated-but-not-identical L/R
// out, so a TV/soundbar that derives rears and sub from L-R has something to
// work with.
// =============================================================================
struct WidenXAPO final : public InPlaceFloatXAPO {
	MonoStereoWidth m_width;

	const wchar_t* Name() const override { return L"AAE Mono Widen"; }
	void OnLock() override { m_width.configure(true, m_rate); }
	void Run(float* buf, UINT32 frames) override
	{
		if (m_channels != 2) return;
		for (UINT32 i = 0; i < frames; ++i) {
			float dl, dr;
			m_width.processf(0.5f * (buf[i * 2] + buf[i * 2 + 1]), dl, dr);
			buf[i * 2]     += dl;
			buf[i * 2 + 1] += dr;
		}
	}
};

// =============================================================================
// ToneXAPO: global "cabinet tone" output filter ([main] audio_filter). First
// effect on the mastering voice, always installed; OFF is a true bypass inside
// OutputTone. The preset is read live every buffer (menu changes are heard at
// once). Sits before the upmix and the capture so -wavwrite records what is heard.
// =============================================================================
struct ToneXAPO final : public InPlaceFloatXAPO {
	OutputTone m_tone;

	const wchar_t* Name() const override { return L"AAE Output Tone"; }
	void OnLock() override { m_tone.configure(m_rate, m_channels); }
	void Run(float* buf, UINT32 frames) override
	{
		m_tone.process(buf, frames, mixer_get_output_filter());
	}
};

// =============================================================================
// UpmixXAPO: NATIVE mode on a real >2 channel endpoint. XAudio2's stock 2->5.1
// matrix leaves center/LFE/rears empty, so this fills them from the FL/FR mix.
//
// Faithful float port of LinuxAudioBackend::Upmix (linux/alsa_backend.cpp,
// the reference for every coefficient and the reasoning in its comments):
//   FL/FR untouched      C = 0.5*(L+R)      LFE = ~120 Hz one-pole low-pass of C
//   surrounds = +-0.5*(L-R) + kSurroundFill*mono, ~12 ms late (stride-2 ring)
//   centre rear/height = delayed mono fill only
// Differences from the Linux int16 code: float (no clamping, the device limiter
// is downstream), and the non-front roles are ADDED to whatever the mastering
// voice already holds in those channels rather than overwriting it.
// Roles come from the master's SPEAKER_* mask in ascending-bit (interleave) order.
// =============================================================================
struct UpmixXAPO final : public InPlaceFloatXAPO {
	enum Role : uint8_t { kRoleL, kRoleR, kRoleC, kRoleLFE, kRoleSurL, kRoleSurR, kRoleSurMono };
	static constexpr float kSurroundFill = 0.5f;   // same final value as alsa_backend.cpp

	Role  m_role[8] = {};
	std::vector<float> m_delay;   // stride 2: [0]=0.5*(L-R) [1]=kSurroundFill*mono
	size_t m_pos = 0;
	float  m_lfe = 0.0f, m_lfeK = 0.0f;
	uint32_t m_iL = 0, m_iR = 1;   // channel indices of the front pair

	// Mirrors RoleOfSpeaker() in alsa_backend.cpp.
	static Role RoleOfSpeaker(DWORD bit)
	{
		switch (bit) {
		case SPEAKER_FRONT_LEFT:  case SPEAKER_FRONT_LEFT_OF_CENTER:  return kRoleL;
		case SPEAKER_FRONT_RIGHT: case SPEAKER_FRONT_RIGHT_OF_CENTER: return kRoleR;
		case SPEAKER_FRONT_CENTER:                                    return kRoleC;
		case SPEAKER_LOW_FREQUENCY:                                   return kRoleLFE;
		case SPEAKER_BACK_LEFT: case SPEAKER_SIDE_LEFT:
		case SPEAKER_TOP_FRONT_LEFT: case SPEAKER_TOP_BACK_LEFT:      return kRoleSurL;
		case SPEAKER_BACK_RIGHT: case SPEAKER_SIDE_RIGHT:
		case SPEAKER_TOP_FRONT_RIGHT: case SPEAKER_TOP_BACK_RIGHT:    return kRoleSurR;
		case SPEAKER_BACK_CENTER: case SPEAKER_TOP_CENTER:
		case SPEAKER_TOP_FRONT_CENTER: case SPEAKER_TOP_BACK_CENTER:  return kRoleSurMono;
		default:                                                      return kRoleC;
		}
	}

	// Fills roles[] for `channels` interleaved channels. A mask whose bit count
	// disagrees with the channel count (or 0) falls back to the standard layout
	// for 4/6/8 channels; any other count returns false (no upmix is installed).
	static bool Resolve(DWORD mask, uint32_t channels, Role* roles)
	{
		auto popcount = [](DWORD m) { uint32_t n = 0; for (; m; m &= m - 1) ++n; return n; };
		if (channels < 3 || channels > 8) return false;
		if (popcount(mask) != channels) {
			switch (channels) {
			case 4: mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT; break;
			case 6: mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY
			             | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT; break;
			case 8: mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY
			             | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT; break;
			default: return false;
			}
		}
		uint32_t c = 0;
		for (DWORD bit = 1; bit && c < channels; bit <<= 1)
			if (mask & bit) roles[c++] = RoleOfSpeaker(bit);
		return c == channels;
	}

	const wchar_t* Name() const override { return L"AAE Surround Upmix"; }

	void OnLock() override
	{
		m_delay.assign(((size_t)(m_rate * 12 / 1000) + 1) * 2, 0.0f);
		m_pos = 0;
		m_lfe = 0.0f;
		m_lfeK = 1.0f - expf(-2.0f * 3.14159265f * 120.0f / (float)m_rate);
		m_iL = 0; m_iR = 1;
		for (uint32_t c = m_channels; c-- > 0;) {
			if (m_role[c] == kRoleL) m_iL = c;
			if (m_role[c] == kRoleR) m_iR = c;
		}
	}

	void Run(float* buf, UINT32 frames) override
	{
		const size_t slots = m_delay.size() / 2;
		for (UINT32 i = 0; i < frames; ++i) {
			float* frame = buf + (size_t)i * m_channels;
			const float L = frame[m_iL], R = frame[m_iR];
			const float mono = 0.5f * (L + R);

			float* slot = &m_delay[m_pos * 2];
			const float dDelayed = slot[0];
			const float mDelayed = slot[1];
			slot[0] = 0.5f * (L - R);
			slot[1] = kSurroundFill * mono;
			m_pos = (m_pos + 1 == slots) ? 0 : m_pos + 1;

			m_lfe += m_lfeK * (mono - m_lfe);

			for (uint32_t c = 0; c < m_channels; ++c) {
				switch (m_role[c]) {
				case kRoleL: case kRoleR: break;                 // untouched
				case kRoleC:       frame[c] += mono;                   break;
				case kRoleLFE:     frame[c] += m_lfe;                  break;
				case kRoleSurL:    frame[c] +=  dDelayed + mDelayed;   break;
				case kRoleSurR:    frame[c] += -dDelayed + mDelayed;   break;
				case kRoleSurMono: frame[c] += mDelayed;               break;
				}
			}
		}
	}
};

// -----------------------------------------------------------------------------
// Default render endpoint probe (the device XAudio2's default mastering voice
// opens). Non-fatal: any failure leaves an empty name / unknown form factor.
// Property keys are declared locally so no INITGUID / extra lib is needed.
// -----------------------------------------------------------------------------
namespace {
const PROPERTYKEY kPkeyDeviceFriendlyName =
	{ { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };
const PROPERTYKEY kPkeyEndpointFormFactor =
	{ { 0x1da5d803, 0xd492, 0x4edd, { 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e } }, 0 };

struct EndpointInfo {
	std::string name;
	int formFactor = -1;   // EndpointFormFactor, -1 unknown
};

EndpointInfo QueryDefaultEndpoint()
{
	EndpointInfo info;
	IMMDeviceEnumerator* en = nullptr;
	IMMDevice* dev = nullptr;
	IPropertyStore* props = nullptr;
	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
	                               __uuidof(IMMDeviceEnumerator), (void**)&en)) &&
	    SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) &&
	    SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
		PROPVARIANT pv;
		PropVariantInit(&pv);
		if (SUCCEEDED(props->GetValue(kPkeyDeviceFriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) {
			char utf8[512] = {};
			if (WideCharToMultiByte(CP_UTF8, 0, pv.pwszVal, -1, utf8, (int)sizeof(utf8) - 1, nullptr, nullptr) > 0)
				info.name = utf8;
		}
		PropVariantClear(&pv);
		PropVariantInit(&pv);
		if (SUCCEEDED(props->GetValue(kPkeyEndpointFormFactor, &pv)) && pv.vt == VT_UI4)
			info.formFactor = (int)pv.ulVal;
		PropVariantClear(&pv);
	}
	if (props) props->Release();
	if (dev) dev->Release();
	if (en) en->Release();
	return info;
}

const char* FormFactorName(int ff)
{
	switch (ff) {
	case Speakers:                   return "Speakers";
	case LineLevel:                  return "LineLevel";
	case Headphones:                 return "Headphones";
	case Headset:                    return "Headset";
	case SPDIF:                      return "S/PDIF";
	case DigitalAudioDisplayDevice:  return "HDMI/DisplayPort";
	case RemoteNetworkDevice:        return "RemoteNetwork";
	case UnknownDigitalPassthrough:  return "UnknownDigital";
	default:                         return "unknown";
	}
}
} // namespace

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

	// ---- Output mode (decided once) --------------------------------------
	// audio_output has already been resolved by the emulator into speakers /
	// hdmi_passthrough (aae_emulator.cpp); the raw value is only for the log.
	const EndpointInfo ep = QueryDefaultEndpoint();
	const int hdmiKey  = mixer_get_hdmi_passthrough();   // -1 auto, 0 off, 1 on
	const int speakers = mixer_get_speaker_config();     // 0 follow device, 2, 6
	const int audioOut = mixer_get_audio_output();       // 0 auto 1 soundbar 2 surround 3 stereo
	static const char* const kOutNames[4] = { "auto", "soundbar", "surround", "stereo" };
	const bool digitalEndpoint = (ep.formFactor == DigitalAudioDisplayDevice || ep.formFactor == SPDIF);
	const bool headphones      = (ep.formFactor == Headphones || ep.formFactor == Headset);

	enum class Mode { Soundbar, Stereo, Native } mode;
	std::string why;
	if (hdmiKey == 1) {
		mode = Mode::Soundbar;
		why = audioOut == 1 ? "forced by audio_output=soundbar" : "forced by hdmi_passthrough=1";
	} else if (hdmiKey == -1 && digitalEndpoint) {
		mode = Mode::Soundbar;
		why = "endpoint is a digital HDMI/S-PDIF output";
	} else if (speakers == 2) {
		mode = Mode::Stereo;
		why = audioOut == 3 ? "forced by audio_output=stereo" : "forced by speakers=2";
	} else {
		mode = Mode::Native;
		why = audioOut == 2 ? "audio_output=surround, device layout" : "device layout";
	}

	const UINT32 masterChannels = (mode == Mode::Native) ? XAUDIO2_DEFAULT_CHANNELS : 2;
	HR(m_xaudio2->CreateMasteringVoice(&m_master, masterChannels, rateHz, 0, 0));

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
		// A 2-channel master is FL/FR whatever the endpoint's own mask says.
		if (mode != Mode::Native || m_output_channels == 2)
			m_output_channel_mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
		LOG_INFO("XAudio2Backend::Init: master %u channels, mask=0x%08X",
			m_output_channels, m_output_channel_mask);
	}

	const char* rawName = kOutNames[(audioOut >= 0 && audioOut <= 3) ? audioOut : 0];
	const char* ffName  = FormFactorName(ep.formFactor);
	const char* epName  = ep.name.empty() ? "unknown" : ep.name.c_str();

	// Master chain at Init: [tone, upmix?]. Tone is always first and always
	// installed (OFF is a pass-through inside OutputTone). If the chain with
	// tone is refused, fall back to the chain without it.
	m_tone = new ToneXAPO();
	auto installInitChain = [this]() -> HRESULT {
		HRESULT chr = ApplyMasterChain();
		if (FAILED(chr) && m_tone) {
			LOG_ERROR("XAudio2: installing the output tone filter failed (hr=0x%08X) - continuing without it", (unsigned)chr);
			m_tone->Release();
			m_tone = nullptr;
			chr = ApplyMasterChain();
		}
		return chr;
	};
	if (mode != Mode::Native) installInitChain();

	if (mode == Mode::Soundbar) {
		m_hdmiStereo = true;
		m_modeDesc = digitalEndpoint ? "SOUNDBAR (HDMI)" : "SOUNDBAR";

		// 2-channel widen bus: only mono per-channel voices send here (see
		// VoiceCreate). WidenXAPO adds the delayed antiphase copy in place.
		auto* widen = new WidenXAPO();
		XAUDIO2_EFFECT_DESCRIPTOR desc{};
		desc.pEffect = static_cast<IXAPO*>(widen);
		desc.InitialState = TRUE;
		desc.OutputChannels = 2;
		XAUDIO2_EFFECT_CHAIN chain{ 1, &desc };
		hr = m_xaudio2->CreateSubmixVoice(&m_widenBus, 2, rateHz, 0, 1, nullptr, &chain);
		widen->Release();   // the chain holds its own reference
		if (FAILED(hr)) {
			LOG_ERROR("XAudio2: widen bus creation failed (hr=0x%08X) - mono voices will not be widened", (unsigned)hr);
			m_widenBus = nullptr;
		}
		LOG_INFO("XAudio2: audio_output=%s -> SOUNDBAR (%s; endpoint '%s', form factor %s): "
		         "mono sources widened for the TV/soundbar's own upmix, stereo PCM on FL/FR%s",
		         rawName, why.c_str(), epName, ffName,
		         m_widenBus ? "" : " (voice widener unavailable)");
	} else if (mode == Mode::Stereo) {
		m_modeDesc = "STEREO";
		LOG_INFO("XAudio2: audio_output=%s -> STEREO (%s; endpoint '%s', form factor %s): "
		         "plain 2-channel output, no processing",
		         rawName, why.c_str(), epName, ffName);
	} else {
		// NATIVE: keep the OS layout. On a real >2 channel endpoint (not the
		// 8-channel Windows Sonic for Headphones view of a headphone device)
		// fill center/LFE/surrounds ourselves - the stock matrix leaves them empty.
		if (m_output_channels > 2 && !headphones) {
			auto* up = new UpmixXAPO();
			if (UpmixXAPO::Resolve(m_output_channel_mask, m_output_channels, up->m_role)) {
				m_upmix = up;   // keep our ref for ApplyMasterChain rebuilds
				if (FAILED(installInitChain())) {
					LOG_ERROR("XAudio2: installing the surround upmix failed - fronts only");
					m_upmix->Release();
					m_upmix = nullptr;
					ApplyMasterChain();
				}
			} else {
				LOG_WARN("XAudio2: %u-channel layout (mask 0x%08X) not supported by the upmix - fronts only",
					m_output_channels, m_output_channel_mask);
				up->Release();
			}
			if (!m_upmix) installInitChain();   // tone only
		} else {
			installInitChain();                  // tone only
		}

		if (headphones && m_output_channels > 2)
			m_modeDesc = "STEREO (HEADPHONES)";
		else if (m_upmix)
			m_modeDesc = m_output_channels == 6 ? "5.1 SURROUND (6 CH)"
			           : m_output_channels == 8 ? "7.1 SURROUND (8 CH)"
			           : "SURROUND";
		else if (headphones)
			m_modeDesc = "STEREO (HEADPHONES)";
		else if (m_output_channels == 2)
			m_modeDesc = "NATIVE 2 CH";
		else
			m_modeDesc = "NATIVE";

		if (speakers == 6 && m_output_channels == 2)
			LOG_WARN("XAudio2: speakers=6/audio_output=surround but the output device is stereo - set it to 5.1 in "
			         "Windows Sound settings > device > Speaker setup / Configure");

		LOG_INFO("XAudio2: audio_output=%s -> NATIVE (%s; endpoint '%s', form factor %s): %u channels, mask 0x%08X, "
		         "surround upmix %s",
		         rawName, why.c_str(), epName, ffName, m_output_channels, m_output_channel_mask,
		         m_upmix ? "ON (fronts untouched, center/LFE/surrounds added)"
		                 : (headphones && m_output_channels > 2 ? "OFF (headphone endpoint)" : "OFF"));
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

	// Order: source voices (the per-channel voices are destroyed by the mixer
	// before this runs), then the widen bus they send to, then the master.
	if (m_source) { m_source->DestroyVoice(); m_source = nullptr; }
	if (m_widenBus) { m_widenBus->DestroyVoice(); m_widenBus = nullptr; }
	if (m_master) { m_master->DestroyVoice(); m_master = nullptr; }
	if (m_upmix)  { m_upmix->Release(); m_upmix = nullptr; }
	if (m_tone)   { m_tone->Release();  m_tone = nullptr; }
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
	m_hdmiStereo = false;
	m_modeDesc = "";

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

	// Chain = [tone, upmix if active, capture]: the file records the final
	// (filtered) output, and tone/upmix stay installed across start/stop.
	m_capture_xapo = xapo;  // keep our ref; the chain holds its own
	const HRESULT hr = ApplyMasterChain();
	if (FAILED(hr)) {
		LOG_ERROR("CaptureStart: SetEffectChain failed, hr=0x%08X", (unsigned)hr);
		m_capture_xapo = nullptr;
		ApplyMasterChain();   // restore the chain without capture
		xapo->StopFile();
		xapo->Release();
		return false;
	}

	LOG_INFO("CaptureStart: recording session audio to '%s'", path);
	return true;
}

void XAudio2Backend::CaptureStop()
{
	if (!m_capture_xapo) return;

	// Detach the effect first so Process() stops being called, then stop
	// the writer and finalize the file.
	CaptureXAPO* cap = m_capture_xapo;
	m_capture_xapo = nullptr;   // ApplyMasterChain builds the chain without it
	if (m_master) ApplyMasterChain();
	cap->StopFile();
	cap->Release();
	LOG_INFO("CaptureStop: recording finalized");
}

// One place that builds the mastering-voice effect chain:
// [tone, upmix if m_upmix, capture if m_capture_xapo], each with the master's
// channel count. Tone is first so the capture records what is heard.
HRESULT XAudio2Backend::ApplyMasterChain()
{
	if (!m_master) return E_FAIL;
	XAUDIO2_EFFECT_DESCRIPTOR descs[3]{};
	UINT32 n = 0;
	if (m_tone) {
		descs[n].pEffect = static_cast<IXAPO*>(m_tone);
		descs[n].InitialState = TRUE;
		descs[n].OutputChannels = m_output_channels;
		++n;
	}
	if (m_upmix) {
		descs[n].pEffect = static_cast<IXAPO*>(m_upmix);
		descs[n].InitialState = TRUE;
		descs[n].OutputChannels = m_output_channels;
		++n;
	}
	if (m_capture_xapo) {
		descs[n].pEffect = static_cast<IXAPO*>(m_capture_xapo);
		descs[n].InitialState = TRUE;
		descs[n].OutputChannels = m_output_channels;
		++n;
	}
	if (n == 0) return m_master->SetEffectChain(nullptr);
	XAUDIO2_EFFECT_CHAIN chain{ n, descs };
	return m_master->SetEffectChain(&chain);
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
	// SOUNDBAR: mono voices go through the widen bus (their only send); stereo
	// voices keep the default send to the master.
	XAUDIO2_SEND_DESCRIPTOR send{ 0, m_widenBus };
	XAUDIO2_VOICE_SENDS sends{ 1, &send };
	const bool viaBus = m_widenBus && fmt.channels == 1;
	if (FAILED(m_xaudio2->CreateSourceVoice(&h->voice, &wfx, 0, 8.0f, nullptr,
	                                        viaBus ? &sends : nullptr))) {
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
