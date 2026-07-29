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
#include <cstring>

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
}

float XAudio2Backend::GetMasterVolume() const
{
	float v = 1.0f;
	if (m_master) m_master->GetVolume(&v);
	return v;
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
