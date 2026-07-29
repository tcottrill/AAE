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

	// Bound the buffer EXPLICITLY. Setting only the period size leaves the
	// buffer to snd_pcm_hw_params, which picks the largest the device allows -
	// and through PipeWire's or PulseAudio's ALSA compatibility layer that can
	// be the better part of a second. Two things then go wrong, and both were
	// observed on the Steam Machine:
	//
	//   * snd_pcm_writei only blocks once the buffer is FULL, so the feed
	//     thread mixes the entire buffer as fast as it can at startup and then
	//     stays that far ahead forever. Every sound a game triggers is mixed
	//     into a position seconds in the future - which is precisely what
	//     "the sounds happen seconds after they are called" is.
	//   * That burst takes VoiceMixer's lock once per period, hundreds of
	//     times back to back, so the game thread blocks whenever it creates,
	//     destroys or submits a voice. The result is a visible video hitch,
	//     repeating every time an underrun makes the buffer refill.
	//
	// Four periods is the usual safe floor: long enough that an ordinary
	// scheduling hiccup does not underrun, short enough that latency stays
	// near one video frame.
	snd_pcm_uframes_t buffer = m_period * 4;
	snd_pcm_hw_params_set_buffer_size_near(m_pcm, hw, &buffer);

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
	// Read back what the device ACTUALLY gave us for period AND buffer, not
	// what was asked for. Both are _near requests and either can be clamped;
	// the buffer in particular is what determines output latency, and a silent
	// clamp there is the difference between 67ms and most of a second.
	snd_pcm_hw_params_get_period_size(hw, &m_period, nullptr);
	snd_pcm_hw_params_get_buffer_size(hw, &buffer);

	m_rate               = (int)rate;
	m_fps                = f;
	m_frames_per_update  = (int)m_period;

	m_mixer.Configure(m_channels, rate);
	m_scratch.assign((size_t)m_period * m_channels, 0);

	// ONE FRAME OF HEADROOM, matching XAudio2Backend::Init. When rate % fps is
	// non-zero the mixer pays back the truncation by emitting N+1 samples on
	// some frames (see mixer_update_internal's g_spf_rem accumulator), and it
	// writes them into whatever GetNextBuffer() returned. Sized exactly to
	// m_period, that last frame lands past the end of the heap block.
	m_appBuffer.assign(((size_t)m_period + 1) * m_channels * sizeof(int16_t), 0);

	// Eight periods of streaming queue: deep enough to ride out a late video
	// frame, shallow enough that it cannot become a second hidden latency
	// buffer on top of the device's own.
	m_streamRing.assign((size_t)m_period * m_channels * 8, 0);
	m_streamHead  = 0;
	m_streamCount = 0;

	// Latency is logged because it is the number that matters and the only one
	// that is not obvious from the source: it is negotiated with whatever
	// PipeWire, PulseAudio or the hardware decides to grant.
	LOG_INFO("ALSA: %u Hz, %u channels, period %lu frames (~%.1f ms), "
	         "buffer %lu frames (~%.1f ms output latency)",
	         rate, m_channels,
	         (unsigned long)m_period, 1000.0 * (double)m_period / (double)rate,
	         (unsigned long)buffer,   1000.0 * (double)buffer  / (double)rate);
	if (buffer > m_period * 8) {
		LOG_WARN("ALSA: the device granted a %.0f ms buffer despite a %.0f ms "
		         "request - expect audio to lag video by about that much",
		         1000.0 * (double)buffer / (double)rate,
		         1000.0 * (double)(m_period * 4) / (double)rate);
	}

	m_running = true;
	m_thread  = std::thread(&AlsaBackend::FeedThread, this);
	return true;
}

void AlsaBackend::Shutdown()
{
	m_running = false;

	// snd_pcm_drop() BEFORE the join, and it is what stops exit from hanging.
	//
	// The feed thread spends most of its life blocked inside snd_pcm_writei
	// waiting for buffer space. Clearing m_running does not wake it - writei
	// has no idea the flag exists - so join() waits for a thread that is
	// waiting for a device that may never drain, and the process hangs with no
	// window and no way out but a kill.
	//
	// drop() discards whatever is queued and forces the stream out of the
	// running state, which makes any in-flight writei return immediately.
	if (m_pcm)
		snd_pcm_drop(m_pcm);

	if (m_thread.joinable())
		m_thread.join();

	// ALWAYS reported, including - especially - when the counts are zero.
	// Logging only on non-zero counts made "Submit was never called" look
	// identical to "everything worked", which is precisely the question that
	// needed answering.
	LOG_INFO("ALSA streaming totals: %llu submits, %llu samples, peak %d, "
	         "starved %llu, dropped %llu",
	         (unsigned long long)m_streamSubmits,
	         (unsigned long long)m_streamSamples,
	         m_streamPeak,
	         (unsigned long long)m_streamStarved,
	         (unsigned long long)m_streamDropped);

	if (m_streamSubmits == 0) {
		LOG_ERROR("ALSA streaming: Submit() was NEVER called - the emulated "
		          "sound chips never reached the backend. The fault is upstream "
		          "of this file (mixer_update -> audio_thread_func -> "
		          "mixer_update_internal), not in the ALSA code.");
	} else if (m_streamPeak == 0) {
		LOG_ERROR("ALSA streaming: %llu submits but every sample was ZERO - the "
		          "mixer is producing silence. The fault is in the mixer's "
		          "channel accumulation, not in this backend.",
		          (unsigned long long)m_streamSubmits);
	}

	if (m_pcm) {
		// NOT snd_pcm_drain(). drain() blocks until every queued sample has
		// been played, which is unbounded if the device has stalled - a second
		// way for exit to hang, and one that only shows up on the machine
		// where the device misbehaves. The stream was already dropped above;
		// what would be thrown away is at most the ~80ms still in the buffer,
		// which nobody hears at shutdown anyway.
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
		// Voices first - MixInto REPLACES the buffer's contents.
		m_mixer.MixInto(m_scratch.data(), (uint32_t)m_period);

		// Then sum the streaming chip audio on top, saturating. XAudio2 gets
		// this for free by running two source voices into one mastering voice;
		// ALSA hands back a single stream, so the summing is ours to do.
		{
			const size_t want = (size_t)m_period * m_channels;
			std::lock_guard<std::mutex> g(m_streamLock);
			const size_t cap = m_streamRing.size();

			// Build a two-period cushion before consuming anything. Without it
			// the ring is drained as fast as it fills and every scheduling
			// wobble becomes an audible gap - measured at 203 starved periods
			// against 60 submits before this was added.
			//
			// The cost is two periods of extra latency on top of the device
			// buffer, which at a 20ms period is ~40ms. That is a deliberate
			// trade: continuous audio slightly late beats punctual audio full
			// of holes.
			constexpr size_t kPrimePeriods = 2;
			if (m_streamPriming) {
				if (m_streamCount >= want * kPrimePeriods)
					m_streamPriming = false;
			}

			if (!m_streamPriming) {
				const size_t have = (m_streamCount < want) ? m_streamCount : want;

				for (size_t i = 0; i < have; ++i) {
					const int32_t sum = (int32_t)m_scratch[i] + (int32_t)m_streamRing[m_streamHead];
					m_scratch[i] = (int16_t)(sum < -32768 ? -32768 : (sum > 32767 ? 32767 : sum));
					m_streamHead = (m_streamHead + 1 == cap) ? 0 : m_streamHead + 1;
				}
				m_streamCount -= have;

				if (m_streamSeen && have < want) ++m_streamStarved;

				// Re-prime only on a COMPLETE drain, not on any shortfall:
				// re-priming for a few missing samples would insert a fresh
				// gap every time, which is the problem it is meant to avoid.
				if (m_streamCount == 0 && m_streamSeen) {
					m_streamPriming = true;
					++m_streamReprimes;
				}
			}
		}

		snd_pcm_sframes_t written = snd_pcm_writei(m_pcm, m_scratch.data(), m_period);
		if (written < 0) {
			// -EPIPE is an underrun: the device ran dry because we were late.
			// Recovering and carrying on is correct - treating it as fatal
			// would kill audio on the first scheduling hiccup, which on a
			// desktop kernel is a matter of when, not if.
			written = snd_pcm_recover(m_pcm, (int)written, /*silent=*/1);
			if (written < 0) {
				// Expected during shutdown, not a fault: Shutdown() calls
				// snd_pcm_drop() precisely to make this call fail so the thread
				// stops waiting and can be joined. Reporting "write failed
				// unrecoverably: File descriptor in bad state" at ERROR made a
				// clean exit look like a crash.
				if (!m_running)
					LOG_INFO("ALSA: feed thread stopping (stream dropped for shutdown)");
				else
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
// This was a stub until 2026-07-29, with a comment claiming the voice path was
// "what every game actually uses". That was wrong. mixer_update_internal()
// mixes every emulated sound chip into this buffer, so discarding it left the
// emulator playing WAV samples and nothing else - no POKEY, no AY-8910, no
// speech, no DACs.
//------------------------------------------------------------------------------
uint8_t* AlsaBackend::GetNextBuffer()
{
	return m_appBuffer.data();
}

bool AlsaBackend::Submit(uint8_t* buffer, uint32_t bytes)
{
	if (!buffer || bytes == 0) return true;

	const int16_t* in      = reinterpret_cast<const int16_t*>(buffer);
	const size_t   samples = bytes / sizeof(int16_t);

	std::lock_guard<std::mutex> g(m_streamLock);
	if (m_streamRing.empty()) return false;

	const size_t cap = m_streamRing.size();
	m_streamSeen = true;

	// Peak tracking: cheap (one pass over ~1600 samples at 60Hz) and it is the
	// single measurement that says whether the mixer is handing us audio or
	// handing us zeroes.
	++m_streamSubmits;
	m_streamSamples += samples;
	for (size_t i = 0; i < samples; ++i) {
		const int32_t a = in[i] < 0 ? -(int32_t)in[i] : (int32_t)in[i];
		if (a > m_streamPeak) m_streamPeak = a;
	}

	// The FIRST submit is announced on its own. "Is anything calling Submit at
	// all" is the single most useful bit here, and waiting for a threshold
	// meant a run that was killed before reaching it reported nothing - which
	// is indistinguishable from never being called.
	if (m_streamSubmits == 1) {
		m_streamFirstAt = std::chrono::steady_clock::now();
		LOG_INFO("ALSA streaming: first Submit() received (%zu samples)", samples);
	}

	// Submits per second is THE number. The device consumes a period every
	// ~20ms, so the game must submit ~50 times a second to keep up. A rate far
	// below that means the audio cannot be continuous no matter how the ring
	// is tuned, and points upstream at the frame loop rather than at this file.
	const double elapsed = std::chrono::duration<double>(
	                           std::chrono::steady_clock::now() - m_streamFirstAt).count();
	const double perSec  = elapsed > 0.0 ? (double)m_streamSubmits / elapsed : 0.0;

	// A summary at 60 submits (~1s, so a short run still reports), then every
	// 600 (~12s) thereafter. The first one is dominated by start-up and ROM
	// loading; the later ones show steady state, which is the number that
	// says whether audio is actually healthy while playing.
	if (m_streamSubmits == 60 || (m_streamSubmits % 600) == 0) {
		m_streamReported = true;
		LOG_INFO("ALSA streaming: %llu submits in %.1fs = %.1f/sec (device needs "
		         "%.1f/sec), peak amplitude %d (0 = SILENCE), starved %llu, "
		         "dropped %llu, reprimes %llu",
		         (unsigned long long)m_streamSubmits, elapsed, perSec,
		         (double)m_rate / (double)m_period,
		         m_streamPeak,
		         (unsigned long long)m_streamStarved,
		         (unsigned long long)m_streamDropped,
		         (unsigned long long)m_streamReprimes);
	}

	// Overflow means the device is consuming slower than the game is producing
	// - a paused or fast-forwarding emulator, or a stalled feed thread. DROP
	// THE OLDEST rather than blocking: stalling the game thread here would
	// trade a moment of audio for a visible hitch, and the newest audio is the
	// audio that matches what is on screen.
	if (samples >= cap) {
		// A single submit larger than the whole ring: keep only its tail.
		in += (samples - cap);
		m_streamHead  = 0;
		m_streamCount = cap;
		std::copy(in, in + cap, m_streamRing.begin());
		m_streamDropped += samples - cap;
		return true;
	}

	if (m_streamCount + samples > cap) {
		const size_t over = m_streamCount + samples - cap;
		m_streamHead   = (m_streamHead + over) % cap;
		m_streamCount -= over;
		m_streamDropped += over;
	}

	size_t w = (m_streamHead + m_streamCount) % cap;
	for (size_t i = 0; i < samples; ++i) {
		m_streamRing[w] = in[i];
		w = (w + 1 == cap) ? 0 : w + 1;
	}
	m_streamCount += samples;
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

	// Locked separately from Create(), which takes the same non-recursive lock
	// and has already released it. The voice is in the mix list by now, so
	// these writes race the audio thread without this.
	{
		const auto guard = m_mixer.LockVoices();
		s->channels      = fmt.channels ? fmt.channels : 1;
		s->sampleRate    = fmt.rate ? fmt.rate : 44100;
		s->bitsPerSample = fmt.bits ? fmt.bits : 16;
	}

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

	// THE one that was actively dangerous: assign() reallocates, and MixInto
	// holds a raw pointer into this vector while it reads.
	const auto guard = m_mixer.LockVoices();
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
	const auto guard = m_mixer.LockVoices();
	s->playing = true;
	return true;
}

void AlsaBackend::VoiceStop(VoiceHandle* v)
{
	if (SoftVoice* s = soft_of(v)) {
		const auto guard = m_mixer.LockVoices();
		s->playing = false;
	}
}

void AlsaBackend::VoiceFlush(VoiceHandle* v)
{
	if (SoftVoice* s = soft_of(v)) {
		// clear() frees the buffer MixInto may be reading - the same hazard as
		// VoiceSubmit's assign().
		const auto guard = m_mixer.LockVoices();
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
	if (SoftVoice* s = soft_of(v)) {
		const auto guard = m_mixer.LockVoices();
		s->exitLoopReq = true;
	}
}

void AlsaBackend::VoiceSetVolume(VoiceHandle* v, float gain)
{
	if (SoftVoice* s = soft_of(v)) {
		const auto guard = m_mixer.LockVoices();
		s->gain = std::clamp(gain, 0.0f, 4.0f);
	}
}

void AlsaBackend::VoiceSetFrequencyRatio(VoiceHandle* v, float ratio)
{
	// Clamped to XAudio2's own documented range so a driver that pushes an
	// extreme pitch bend behaves the same on both backends.
	if (SoftVoice* s = soft_of(v)) {
		const auto guard = m_mixer.LockVoices();
		s->freqRatio = std::clamp(ratio, 0.0005f, 1024.0f);
	}
}

uint32_t AlsaBackend::VoiceBuffersQueued(VoiceHandle* v)
{
	SoftVoice* s = soft_of(v);
	if (!s) return 0;
	// Read under the lock too: MixInto decrements this when a one-shot drains,
	// and mixer.cpp polls it every frame to decide whether a channel is free.
	const auto guard = m_mixer.LockVoices();
	return s->buffersQueued;
}

uint32_t AlsaBackend::VoiceInputChannels(VoiceHandle* v)
{
	SoftVoice* s = soft_of(v);
	if (!s) return 0;
	const auto guard = m_mixer.LockVoices();
	return s->channels;
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
