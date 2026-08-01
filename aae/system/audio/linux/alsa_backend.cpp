//==============================================================================
// alsa_backend.cpp -- see alsa_backend.h.
//==============================================================================
#include "alsa_backend.h"
#include "mixer.h"     // WaveFormat
#include "sys_log.h"

#include <algorithm>
#include <cmath>       // expf - the LFE low-pass coefficient in Init
#include <cstring>

#ifdef AAE_HAVE_PULSE
#include <pulse/simple.h>
#include <pulse/error.h>
#endif

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
// SetupBuffersAndUpmix - the device-independent tail of Init, shared by the
// Pulse-native and raw-ALSA paths. Requires m_period, m_devChannels and the
// rate to be final.
//------------------------------------------------------------------------------
void AlsaBackend::SetupBuffersAndUpmix(unsigned int rate)
{
	m_mixer.Configure(m_channels, rate);
	m_scratch.assign((size_t)m_period * m_channels, 0);

	// Default interleave order: FL FR RL RR FC LFE - the convention shared by
	// raw ALSA's surround51 device and the PulseAudio/PipeWire default 5.1
	// map. The Pulse-native path declares exactly this order explicitly; the
	// raw-ALSA path overrides it from the device's chmap where one exists.
	m_chIndex[kFL] = 0; m_chIndex[kFR] = 1; m_chIndex[kRL] = 2;
	m_chIndex[kRR] = 3; m_chIndex[kFC] = 4; m_chIndex[kLFE] = 5;

	if (m_devChannels == 6) {
		m_devBuf.assign((size_t)m_period * m_devChannels, 0);

		// ~12ms rear delay: decorrelates the surround signal from the fronts,
		// which is what makes it read as "behind you" rather than "louder".
		// Stride 2: each slot holds the difference component AND the mono fill
		// (see UpmixPeriod for why the rears need both).
		m_rearDelay.assign(((size_t)(rate * 12 / 1000) + 1) * 2, 0.0f);
		m_rearPos  = 0;
		m_lfeState = 0.0f;
		// One-pole low-pass at ~120 Hz for the LFE feed.
		m_lfeK = 1.0f - expf(-2.0f * 3.14159265f * 120.0f / (float)rate);
	}

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

	// Stereo path: matrix surround encode ring (see EncodeStereoPeriod).
	// Same ~12ms decorrelation delay the discrete path uses for its rears.
	m_encDelay.clear();
	m_encPos = 0;
	if (m_devChannels == 2 && mixer_get_surround_encode())
		m_encDelay.assign((size_t)(rate * 12 / 1000) + 1, 0.0f);
}

//------------------------------------------------------------------------------
// EncodeStereoPeriod - matrix surround ENCODE, in place on the stereo mix.
//
// The measured problem this solves: every difference-driven upmixer between
// us and the rear speakers (PipeWire's psd upmix - confirmed via pw-dump to
// give our stream the identical 6-port treatment Dolphin's gets - Pro Logic
// AVRs, soundbar processing) derives rear content from L-R. Arcade audio is
// near-mono, L-R is ~zero, so those decoders produce silence no matter how
// healthy the chain is. Dolphin's games escape only because their stereo is
// wide.
//
// The classic Dolby Surround encode fixes it from inside a 2-channel pipe:
// inject the surround signal S ANTIPHASE (L-S, R+S). Decoders recover S into
// the rears; on plain stereo gear it reads as mild widening. S here is the
// same ~12ms-delayed mono ambience the discrete path feeds its rears, at
// ~-10dB - and the delay keeps it from smearing the direct sound.
//------------------------------------------------------------------------------
void AlsaBackend::EncodeStereoPeriod()
{
	constexpr float kEnc = 0.32f;   // ~-10dB ambience level
	const size_t n = m_encDelay.size();

	for (size_t i = 0; i < (size_t)m_period; ++i) {
		const float L = (float)m_scratch[i * 2];
		const float R = (float)m_scratch[i * 2 + 1];

		const float d = m_encDelay[m_encPos];
		m_encDelay[m_encPos] = kEnc * 0.5f * (L + R);
		m_encPos = (m_encPos + 1 == n) ? 0 : m_encPos + 1;

		const float l = L - d;
		const float r = R + d;
		m_scratch[i * 2]     = (int16_t)(l < -32768.0f ? -32768.0f : (l > 32767.0f ? 32767.0f : l));
		m_scratch[i * 2 + 1] = (int16_t)(r < -32768.0f ? -32768.0f : (r > 32767.0f ? 32767.0f : r));
	}
}

//------------------------------------------------------------------------------
// InitPulseNative - open the Pulse/PipeWire server DIRECTLY, the way cubeb
// does (and therefore the way Dolphin's surround works).
//
// Read from the cubeb and Dolphin sources rather than guessed: cubeb's pulse
// backend opens pa_stream_new with an EXPLICIT pa_channel_map, and Dolphin
// opens 6 channels with an explicit 3F2_LFE layout. Neither ships multichannel
// audio through the ALSA compat shim - and that shim is exactly where our
// 6-channel stream's positions became unverifiable (its chmap query returns
// nothing, so how it labels our rears inside the server is anyone's guess).
//
// This path declares every position explicitly. The raw-ALSA path below stays
// intact as the fallback for systems with no server at all (Pi console
// builds, bare cabinets).
//------------------------------------------------------------------------------
bool AlsaBackend::InitPulseNative(unsigned int rate, int f)
{
#ifndef AAE_HAVE_PULSE
	(void)rate; (void)f;
	return false;
#else
	// Honour [main] speakers. Default is STEREO, and that is a measured
	// decision, not a retreat: on SteamOS a discrete 6ch pulse stream loses
	// its rears inside the loopback graph (100% channel volume, correct map,
	// still silent - while speaker-test via PipeWire's native protocol plays
	// them fine), and Dolphin's admired "surround" is a plain 2ch stream
	// room-filled downstream. Stereo is what actually sounds surround there.
	// speakers=6 requests discrete 5.1 for hardware that takes it.
	const int  speakerCfg = mixer_get_speaker_config();
	const bool want6      = (speakerCfg == 6 || speakerCfg == 0);

	pa_sample_spec ss{};
	ss.format   = PA_SAMPLE_S16LE;
	ss.rate     = rate;
	ss.channels = want6 ? 6 : 2;

	// Explicit positions, matching UpmixPeriod's interleave order exactly.
	pa_channel_map cm{};
	cm.channels = ss.channels;
	cm.map[0] = PA_CHANNEL_POSITION_FRONT_LEFT;
	cm.map[1] = PA_CHANNEL_POSITION_FRONT_RIGHT;
	if (want6) {
		cm.map[2] = PA_CHANNEL_POSITION_REAR_LEFT;
		cm.map[3] = PA_CHANNEL_POSITION_REAR_RIGHT;
		cm.map[4] = PA_CHANNEL_POSITION_FRONT_CENTER;
		cm.map[5] = PA_CHANNEL_POSITION_LFE;
	}

	// One period per video frame, same sizing logic as the ALSA path; the
	// server accepts any rate and resamples well, so no negotiation dance.
	m_period = (snd_pcm_uframes_t)(rate / (unsigned int)f);

	// tlength bounds server-side latency at four periods, the same target the
	// ALSA path requests via set_buffer_size_near.
	pa_buffer_attr ba{};
	ba.maxlength = (uint32_t)-1;
	ba.tlength   = (uint32_t)((size_t)m_period * 4 * ss.channels * sizeof(int16_t));
	ba.prebuf    = (uint32_t)-1;
	ba.minreq    = (uint32_t)-1;
	ba.fragsize  = (uint32_t)-1;

	// "AAE Emulator", NOT "AAE", and the difference is load-bearing. The server
	// keeps a per-application volume memory keyed by application name
	// (module-stream-restore, "sink-input-by-application-name:<name>"). A
	// stereo-era entry saved under "AAE" was being applied to the 6-channel
	// stream, which left front L/R at 100% and REARS, CENTER and LFE at 0%
	// (-inf dB) - photographed in pactl output on the Steam Machine. Muted at
	// the stream level, upstream of the sink: no code on our side of the
	// server could ever have made the rears audible. A fresh name gets a
	// fresh entry with default (full) volume on every channel.
	static const char* kAppName = "AAE Emulator";
	int perr = 0;
	m_pulse = pa_simple_new(nullptr, kAppName, PA_STREAM_PLAYBACK, nullptr,
	                        "emulation", &ss, &cm, &ba, &perr);
	if (!m_pulse) {
		// 5.1 refused - try plain stereo before giving up on the server.
		ss.channels = 2;
		cm.channels = 2;
		cm.map[0] = PA_CHANNEL_POSITION_FRONT_LEFT;
		cm.map[1] = PA_CHANNEL_POSITION_FRONT_RIGHT;
		ba.tlength = (uint32_t)((size_t)m_period * 4 * 2 * sizeof(int16_t));
		m_pulse = pa_simple_new(nullptr, kAppName, PA_STREAM_PLAYBACK, nullptr,
		                        "emulation", &ss, &cm, &ba, &perr);
	}
	if (!m_pulse) {
		LOG_INFO("Pulse native: no usable server (%s) - using the raw ALSA path",
		         pa_strerror(perr));
		return false;
	}

	m_devChannels       = ss.channels;
	m_rate              = (int)rate;
	m_fps               = f;
	m_frames_per_update = (int)m_period;

	SetupBuffersAndUpmix(rate);

	if (m_devChannels == 6)
		LOG_INFO("Pulse NATIVE output ACTIVE: 6-channel stream with EXPLICIT "
		         "positions (FL FR RL RR FC LFE), no ALSA compat shim; rears = "
		         "difference + -3dB delayed mono fill (TEST LEVEL)");
	else
		LOG_INFO("Pulse NATIVE output ACTIVE: STEREO out (speakers=%d), matrix "
		         "surround encode %s - downstream upmixers (PipeWire/Pro Logic/"
		         "soundbar) derive the rears from the encoded L-R content; "
		         "surround_encode=0 for plain stereo, speakers=6 for discrete 5.1",
		         speakerCfg,
		         mixer_get_surround_encode() ? "ON (-10dB, 12ms)" : "OFF");
	LOG_INFO("Pulse native: %u Hz, %u device channels (%u app-side), period %lu "
	         "frames (~%.1f ms), server buffer target ~%.1f ms",
	         rate, m_devChannels, m_channels, (unsigned long)m_period,
	         1000.0 * (double)m_period / (double)rate,
	         1000.0 * (double)(m_period * 4) / (double)rate);

	m_running = true;
	m_thread  = std::thread(&AlsaBackend::FeedThread, this);
	return true;
#endif
}

//------------------------------------------------------------------------------
// Device setup / teardown
//------------------------------------------------------------------------------
bool AlsaBackend::Init(int rateHz, int fps)
{
	unsigned int rate = (unsigned int)(rateHz > 0 ? rateHz : 44100);
	const int    f    = (fps > 0 ? fps : 60);

	// Server first, shim never: when Pulse/PipeWire is reachable, open it
	// natively with explicit channel positions (see InitPulseNative). Raw ALSA
	// below only runs when there is no server - Pi consoles, bare cabinets.
	if (InitPulseNative(rate, f))
		return true;

	int err = snd_pcm_open(&m_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		LOG_ERROR("ALSA: cannot open default playback device: %s", snd_strerror(err));
		m_pcm = nullptr;
		return false;
	}

	snd_pcm_hw_params_t* hw = nullptr;
	snd_pcm_hw_params_alloca(&hw);
	snd_pcm_hw_params_any(m_pcm, hw);
	snd_pcm_hw_params_set_access(m_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(m_pcm, hw, SND_PCM_FORMAT_S16_LE);

	// Channel count per [main] speakers (see InitPulseNative for the default's
	// rationale): stereo unless 6/auto was requested AND the device offers it.
	// m_channels (the APP side - voice mixer, streaming ring, Submit contract)
	// stays 2 regardless; a surround device just means the feed thread upmixes
	// at the writei boundary (see UpmixPeriod).
	const int alsaSpeakerCfg = mixer_get_speaker_config();
	m_devChannels = 2;
	if (alsaSpeakerCfg != 2 &&
	    snd_pcm_hw_params_test_channels(m_pcm, hw, 6) == 0)
		m_devChannels = 6;
	if (snd_pcm_hw_params_set_channels(m_pcm, hw, m_devChannels) < 0 &&
	    m_devChannels == 6) {
		// Offered in theory, refused in practice - fall back rather than fail.
		m_devChannels = 2;
		snd_pcm_hw_params_set_channels(m_pcm, hw, m_devChannels);
	}

	// ASK the device its rate rather than TELLING it ours (the Dolphin/cubeb
	// model). With ALSA's automatic resampler left on, set_rate_near claims to
	// support any rate and the plug layer quietly converts with LINEAR
	// interpolation - the cheapest, most audible resampler there is. Disabling
	// it makes set_rate_near land on a rate the device actually runs (48000 on
	// essentially every HDMI sink), and the mixer then synthesizes at that rate
	// natively - no conversion anywhere in the chain.
	//
	// The negotiated rate is published through OutputRate(); mixer_init() reads
	// it back and re-sizes everything, so requesting 44100 from the ini and
	// being granted 48000 is a rate CHANGE, not a detune.
	//
	// Failure is ignored deliberately: the PulseAudio/PipeWire compat plugins
	// don't expose the plug layer's resample knob, and through a server the
	// server's own (good) resampler handles any mismatch anyway.
	snd_pcm_hw_params_set_rate_resample(m_pcm, hw, 0);

	const unsigned int requested = rate;
	snd_pcm_hw_params_set_rate_near(m_pcm, hw, &rate, nullptr);
	if (rate != requested)
		LOG_INFO("ALSA: device prefers %u Hz (ini asked %u) - following the device",
		         rate, requested);

	// One period per emulated frame, matching how the XAudio2 backend sizes
	// its ring buffer - latency then tracks the frame rate rather than being
	// an independent knob that can drift out of step with it. Computed from the
	// NEGOTIATED rate, not the requested one - at 44100-asked/48000-granted the
	// difference is 735 vs 800 frames, and sizing the period from the wrong
	// rate would make "one period" no longer mean "one video frame".
	m_period = (snd_pcm_uframes_t)(rate / (unsigned int)f);
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
	snd_pcm_hw_params_get_rate(hw, &rate, nullptr);

	m_rate               = (int)rate;
	m_fps                = f;
	m_frames_per_update  = (int)m_period;

	SetupBuffersAndUpmix(rate);

	// Correct the interleave order from the device's channel map where one
	// exists (raw hw devices report one; compat shims usually return nothing).
	// Getting this wrong puts dialogue in a rear speaker, so log what was used.
	if (m_devChannels == 6) {
		const char* mapSrc = "assumed (FL FR RL RR FC LFE)";
		if (snd_pcm_chmap_t* map = snd_pcm_get_chmap(m_pcm)) {
			if (map->channels == m_devChannels) {
				for (unsigned int i = 0; i < map->channels; ++i) {
					switch (map->pos[i]) {
					case SND_CHMAP_FL:  m_chIndex[kFL]  = (int)i; break;
					case SND_CHMAP_FR:  m_chIndex[kFR]  = (int)i; break;
					case SND_CHMAP_RL:
					case SND_CHMAP_SL:  m_chIndex[kRL]  = (int)i; break;
					case SND_CHMAP_RR:
					case SND_CHMAP_SR:  m_chIndex[kRR]  = (int)i; break;
					case SND_CHMAP_FC:  m_chIndex[kFC]  = (int)i; break;
					case SND_CHMAP_LFE: m_chIndex[kLFE] = (int)i; break;
					default: break;
					}
				}
				mapSrc = "queried from device";
			}
			free(map);
		}

		LOG_INFO("ALSA: pseudo-surround upmix ACTIVE: 6-channel device, "
		         "channel order %s; fronts untouched, rears = difference + "
		         "-3dB delayed mono fill (TEST LEVEL)", mapSrc);
	}

	// Latency is logged because it is the number that matters and the only one
	// that is not obvious from the source: it is negotiated with whatever
	// PipeWire, PulseAudio or the hardware decides to grant.
	LOG_INFO("ALSA: %u Hz, %u device channels (%u app-side), period %lu frames "
	         "(~%.1f ms), buffer %lu frames (~%.1f ms output latency)",
	         rate, m_devChannels, m_channels,
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
		// INFO, not ERROR: all-zero streaming is NORMAL for the GUI frontend
		// (its only audio is WAV voices, which ride the voice path). It is a
		// fault only if a GAME session shows it - chip audio would be missing.
		LOG_INFO("ALSA streaming: %llu submits, every sample zero. Normal for "
		         "the GUI; in a game session this would mean the chip-audio "
		         "path is silent upstream of the backend.",
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

#ifdef AAE_HAVE_PULSE
	// After the join, so the feed thread can no longer be inside a write.
	// No drain for the same reason as ALSA above: at most ~4 periods die.
	if (m_pulse) {
		pa_simple_free(m_pulse);
		m_pulse = nullptr;
	}
#endif
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

		// Stereo devices get the matrix surround encode (if enabled) in place;
		// surround devices get the passive-matrix expansion. Either way this
		// is the last stop before the hardware.
		const int16_t* out = m_scratch.data();
		if (m_devChannels != m_channels) {
			UpmixPeriod();
			out = m_devBuf.data();
		} else if (!m_encDelay.empty()) {
			EncodeStereoPeriod();
		}

#ifdef AAE_HAVE_PULSE
		if (m_pulse) {
			// pa_simple_write blocks only until the server's tlength has room -
			// bounded by ~4 periods - so unlike snd_pcm_writei it needs no drop()
			// trick at shutdown; the loop re-checks m_running every period.
			int perr = 0;
			if (pa_simple_write(m_pulse, out,
			                    (size_t)m_period * m_devChannels * sizeof(int16_t),
			                    &perr) < 0) {
				if (m_running)
					LOG_ERROR("Pulse native: write failed: %s", pa_strerror(perr));
				break;
			}
			continue;
		}
#endif

		snd_pcm_sframes_t written = snd_pcm_writei(m_pcm, out, m_period);
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
// UpmixPeriod - stereo scratch -> 5.1 device buffer, passive matrix.
//
// The classic Dolby Surround decode, chosen over "copy L/R to the rears"
// because it actually separates: the rears get the L-R DIFFERENCE, which is
// where stereo ambience and effects live, while anything center-panned (most
// gameplay-critical audio) cancels out of them and stays anchored up front.
//
// Every coefficient is 0.5, deliberately: 0.5*(L+R) and 0.5*(L-R) can never
// exceed int16 range, so this stage cannot clip - the textbook 0.707 gains
// buy 3dB of level at the cost of clamping distortion on loud mono content.
//
// The rears run ~12ms late via a delay line. Identical undelayed signals in
// front and rear speakers just sound louder; the delay is what makes the
// surround content read as spatially separate (precedence effect).
//------------------------------------------------------------------------------
void AlsaBackend::UpmixPeriod()
{
	// The rears carry TWO components, and the second one is the load-bearing
	// lesson of the first listening test:
	//
	//   difference (L-R): real stereo separation - ambience, panned effects.
	//     The textbook surround feed, but arcade audio is overwhelmingly MONO
	//     (single-channel chips mixed center), and L-R of mono is ZERO. A
	//     passive matrix therefore delivered six perfect channels of which two
	//     were silent - "log shows surround, ears hear stereo", on BOTH
	//     platforms (XAudio2's stock 2->5.1 matrix leaves rears empty too).
	//
	//   mono fill (-10dB, delayed): a quiet, late copy of the whole mix. This
	//     is what active upmixers (PipeWire's, the soundbar's own stereo
	//     expansion, Dolby PLII) manufacture rear content from when the source
	//     is mono-ish - and those helpers all switch OFF the moment a source
	//     hands them discrete 5.1, so we must bring our own.
	//
	// Both run through the ~12ms delay ring (stride 2), which is what makes
	// the rears read as "behind you" rather than "louder".
	// TEST LEVEL: -3dB, deliberately too loud, to make the rears unmissable.
	// This splits "the rears are folded away downstream" from "the rears play
	// but -10dB was too quiet to notice". Tune back down once heard.
	constexpr float kFill = 0.71f;

	const size_t delaySlots = m_rearDelay.size() / 2;

	auto clamp16 = [](float v) -> int16_t {
		return (int16_t)(v < -32768.0f ? -32768.0f : (v > 32767.0f ? 32767.0f : v));
	};

	for (size_t i = 0; i < (size_t)m_period; ++i) {
		const float L = (float)m_scratch[i * 2];
		const float R = (float)m_scratch[i * 2 + 1];
		const float mono = 0.5f * (L + R);

		// Push today's pair, read the ~12ms-old pair.
		float* slot = &m_rearDelay[m_rearPos * 2];
		const float dDelayed = slot[0];
		const float mDelayed = slot[1];
		slot[0] = 0.5f * (L - R);
		slot[1] = kFill * mono;
		m_rearPos = (m_rearPos + 1 == delaySlots) ? 0 : m_rearPos + 1;

		// LFE: one-pole low-pass (~120 Hz) of the mono sum.
		m_lfeState += m_lfeK * (mono - m_lfeState);

		int16_t* frame = &m_devBuf[i * m_devChannels];
		frame[m_chIndex[kFL]]  = (int16_t)L;          // untouched stereo
		frame[m_chIndex[kFR]]  = (int16_t)R;
		frame[m_chIndex[kFC]]  = (int16_t)mono;       // center = mono sum
		frame[m_chIndex[kLFE]] = (int16_t)m_lfeState;
		// Difference antiphase across the pair for width; fill IN phase in
		// both, so mono content fills the room instead of cancelling.
		frame[m_chIndex[kRL]]  = clamp16( dDelayed + mDelayed);
		frame[m_chIndex[kRR]]  = clamp16(-dDelayed + mDelayed);
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

// Report the DEVICE geometry, matching what the XAudio2 backend reports (its
// mastering voice channel count). The app-side mix stays stereo either way -
// these exist for diagnostics and audio_3d_init, which is a null stub here.
uint32_t AlsaBackend::OutputChannelCount() const { return m_devChannels; }

uint32_t AlsaBackend::OutputChannelMask() const
{
	// SPEAKER_xxx bits, the values audio_3d.cpp and mixer.cpp expect. Spelled
	// out numerically because those constants come from a Windows header this
	// file must not include. 0x3F = 5.1 (FL FR FC LFE BL BR), 0x3 = stereo.
	if (m_devChannels == 6)
		return 0x3F;
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
