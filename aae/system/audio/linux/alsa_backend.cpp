//==============================================================================
// alsa_backend.cpp -- see alsa_backend.h.
//==============================================================================
#include "alsa_backend.h"
#include "mixer.h"     // WaveFormat
#include "sys_log.h"

#include <algorithm>
#include <cstring>

//------------------------------------------------------------------------------
// The opaque handle audio_backend.h forward-declares. On this backend it is
// simply a software voice owned by VoiceMixer - there is no ALSA object per
// voice, because ALSA has no concept of one.
//------------------------------------------------------------------------------
struct VoiceHandle {
	SoftVoice* soft = nullptr;
};

static inline SoftVoice* soft_of(VoiceHandle* v)
{
	return v ? v->soft : nullptr;
}

//------------------------------------------------------------------------------
// Device setup / teardown
//------------------------------------------------------------------------------
bool AlsaBackend::Init(int rateHz, int fps)
{
	unsigned int rate = (unsigned int)(rateHz > 0 ? rateHz : 44100);
	const int    f    = (fps > 0 ? fps : 60);

	int err = snd_pcm_open(&m_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		LOG_ERROR("ALSA: cannot open default playback device: %s", snd_strerror(err));
		m_pcm = nullptr;
		return false;
	}

	// One period per emulated frame, matching how the XAudio2 backend sizes
	// its ring buffer - latency then tracks the frame rate rather than being
	// an independent knob that can drift out of step with it.
	m_period = (snd_pcm_uframes_t)(rate / (unsigned int)f);

	snd_pcm_hw_params_t* hw = nullptr;
	snd_pcm_hw_params_alloca(&hw);
	snd_pcm_hw_params_any(m_pcm, hw);
	snd_pcm_hw_params_set_access(m_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(m_pcm, hw, SND_PCM_FORMAT_S16_LE);
	snd_pcm_hw_params_set_channels(m_pcm, hw, m_channels);
	snd_pcm_hw_params_set_rate_near(m_pcm, hw, &rate, nullptr);
	snd_pcm_hw_params_set_period_size_near(m_pcm, hw, &m_period, nullptr);

	err = snd_pcm_hw_params(m_pcm, hw);
	if (err < 0) {
		LOG_ERROR("ALSA: hw params failed: %s", snd_strerror(err));
		snd_pcm_close(m_pcm);
		m_pcm = nullptr;
		return false;
	}

	// set_rate_near and set_period_size_near may both have adjusted what we
	// asked for. Publish what the DEVICE actually gave us, not what we
	// requested - the mixer's resampling ratio depends on it, and a silent
	// mismatch here detunes every sample.
	m_rate               = (int)rate;
	m_fps                = f;
	m_frames_per_update  = (int)m_period;

	m_mixer.Configure(m_channels, rate);
	m_scratch.assign((size_t)m_period * m_channels, 0);
	m_appBuffer.assign((size_t)m_period * m_channels * sizeof(int16_t), 0);

	LOG_INFO("ALSA: %u Hz, %u channels, period %lu frames (~%.1f ms)",
	         rate, m_channels, (unsigned long)m_period,
	         1000.0 * (double)m_period / (double)rate);

	m_running = true;
	m_thread  = std::thread(&AlsaBackend::FeedThread, this);
	return true;
}

void AlsaBackend::Shutdown()
{
	m_running = false;
	if (m_thread.joinable())
		m_thread.join();

	if (m_pcm) {
		snd_pcm_drain(m_pcm);
		snd_pcm_close(m_pcm);
		m_pcm = nullptr;
	}
}

AlsaBackend::~AlsaBackend()
{
	Shutdown();
}

//------------------------------------------------------------------------------
// The feed thread: mix one period, hand it to the device, repeat.
//------------------------------------------------------------------------------
void AlsaBackend::FeedThread()
{
	while (m_running) {
		m_mixer.MixInto(m_scratch.data(), (uint32_t)m_period);

		snd_pcm_sframes_t written = snd_pcm_writei(m_pcm, m_scratch.data(), m_period);
		if (written < 0) {
			// -EPIPE is an underrun: the device ran dry because we were late.
			// Recovering and carrying on is correct - treating it as fatal
			// would kill audio on the first scheduling hiccup, which on a
			// desktop kernel is a matter of when, not if.
			written = snd_pcm_recover(m_pcm, (int)written, /*silent=*/1);
			if (written < 0) {
				LOG_ERROR("ALSA: write failed unrecoverably: %s",
				          snd_strerror((int)written));
				break;
			}
		}
	}
}

//------------------------------------------------------------------------------
// Streaming path.
//
// mixer.cpp's streaming path is a second, independent way of producing audio
// (alongside the voice path). This backend runs its own feed thread, so
// GetNextBuffer/Submit hand back a scratch buffer that is not wired into the
// output. The voice path - which is what every game actually uses - is fully
// implemented below.
//------------------------------------------------------------------------------
uint8_t* AlsaBackend::GetNextBuffer()
{
	return m_appBuffer.data();
}

bool AlsaBackend::Submit(uint8_t* /*buffer*/, uint32_t /*bytes*/)
{
	return true;
}

//------------------------------------------------------------------------------
// Master volume and output geometry
//------------------------------------------------------------------------------
void  AlsaBackend::SetMasterVolume(float linear) { m_mixer.SetMasterVolume(linear); }
float AlsaBackend::GetMasterVolume() const       { return m_mixer.GetMasterVolume(); }

uint32_t AlsaBackend::OutputChannelCount() const { return m_channels; }

uint32_t AlsaBackend::OutputChannelMask() const
{
	// SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT, the values audio_3d.cpp and
	// mixer.cpp expect. Spelled out numerically because those constants come
	// from a Windows header this file must not include.
	return 0x1 | 0x2;
}

//------------------------------------------------------------------------------
// Voice path - all of it delegated to VoiceMixer.
//------------------------------------------------------------------------------
VoiceHandle* AlsaBackend::VoiceCreate(const WaveFormat& fmt)
{
	SoftVoice* s = m_mixer.Create();
	if (!s) return nullptr;

	s->channels      = fmt.channels ? fmt.channels : 1;
	s->sampleRate    = fmt.rate ? fmt.rate : 44100;
	s->bitsPerSample = fmt.bits ? fmt.bits : 16;

	VoiceHandle* v = new VoiceHandle();
	v->soft = s;
	return v;
}

void AlsaBackend::VoiceDestroy(VoiceHandle* v)
{
	if (!v) return;
	m_mixer.Destroy(v->soft);
	delete v;
}

bool AlsaBackend::VoiceSubmit(VoiceHandle* v, const uint8_t* data,
                              uint32_t bytes, bool loop)
{
	SoftVoice* s = soft_of(v);
	if (!s || !data || bytes == 0) return false;

	s->data.assign(data, data + bytes);
	s->looping     = loop;
	s->exitLoopReq = false;
	s->position    = 0.0;
	++s->buffersQueued;
	return true;
}

bool AlsaBackend::VoiceStart(VoiceHandle* v)
{
	SoftVoice* s = soft_of(v);
	if (!s) return false;
	s->playing = true;
	return true;
}

void AlsaBackend::VoiceStop(VoiceHandle* v)
{
	if (SoftVoice* s = soft_of(v)) s->playing = false;
}

void AlsaBackend::VoiceFlush(VoiceHandle* v)
{
	if (SoftVoice* s = soft_of(v)) {
		s->playing       = false;
		s->position      = 0.0;
		s->buffersQueued = 0;
		s->data.clear();
	}
}

void AlsaBackend::VoiceExitLoop(VoiceHandle* v)
{
	// Stop looping at the END of the current pass; the tail still plays. The
	// mixer honours this by not rewinding once the cursor runs off the end.
	if (SoftVoice* s = soft_of(v)) s->exitLoopReq = true;
}

void AlsaBackend::VoiceSetVolume(VoiceHandle* v, float gain)
{
	if (SoftVoice* s = soft_of(v)) s->gain = std::clamp(gain, 0.0f, 4.0f);
}

void AlsaBackend::VoiceSetFrequencyRatio(VoiceHandle* v, float ratio)
{
	// Clamped to XAudio2's own documented range so a driver that pushes an
	// extreme pitch bend behaves the same on both backends.
	if (SoftVoice* s = soft_of(v)) s->freqRatio = std::clamp(ratio, 0.0005f, 1024.0f);
}

uint32_t AlsaBackend::VoiceBuffersQueued(VoiceHandle* v)
{
	SoftVoice* s = soft_of(v);
	return s ? s->buffersQueued : 0;
}

uint32_t AlsaBackend::VoiceInputChannels(VoiceHandle* v)
{
	SoftVoice* s = soft_of(v);
	return s ? s->channels : 0;
}

bool AlsaBackend::VoiceSetOutputMatrix(VoiceHandle* /*v*/, uint32_t /*srcChannels*/,
                                       uint32_t /*dstChannels*/,
                                       const float* /*matrix*/)
{
	// Positional audio on Linux is Phase 3d (see the Phase 3b spec, 3.6/5).
	// Returning false is honest rather than pretending: mixer.cpp reads it as
	// "this backend cannot pan" and falls back to its own gain path, instead
	// of believing a matrix was applied when nothing happened.
	return false;
}

//------------------------------------------------------------------------------
// The Linux half of audio_backend.h's platform factory.
//------------------------------------------------------------------------------
std::unique_ptr<IAudioBackend> create_audio_backend()
{
	return std::make_unique<AlsaBackend>();
}
