//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//==============================================================================
// alsa_backend.cpp -- the Linux IAudioBackend (class LinuxAudioBackend).
//
// The file keeps its old name (the Windows project lists it); what it holds is
// one backend with two device paths:
//
//   1. Native PipeWire (when built with AAE_HAVE_PIPEWIRE and a server is
//      reachable). Mirrors the sibling video player's proven negotiation: find
//      the default sink, ask it what layout it REALLY runs, open the stream in
//      exactly that layout, and let the server drive the timing.
//   2. Raw ALSA, the no-server fallback (Pi consoles, bare cabinets).
//
// Both paths share ProduceDeviceFrames(): voice mix + streaming ring + upmix,
// frame-count-agnostic, so the PipeWire callback and the ALSA feed thread run
// literally the same code. Owns device handling only - all per-voice arithmetic
// lives in voice_mixer.h, which contains no OS calls so the Teensy target can
// reuse it.
//
// Only ONE backend translation unit is ever linked into a binary (see
// audio_backend.h), so this file's private VoiceHandle definition cannot
// collide with xaudio2_backend.cpp's.
//==============================================================================
#include "audio_backend.h"
#include "voice_mixer.h"
#include "mixer.h"     // WaveFormat, mixer_get_speaker_config, mixer_get_output_filter
#include "output_tone.h"
#include "sys_log.h"

#include <alsa/asoundlib.h>

#ifdef AAE_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/buffers.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/utils/json.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>       // expf, log10 - the LFE low-pass coefficient, level logging
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

//------------------------------------------------------------------------------
// The opaque handle audio_backend.h forward-declares. On this backend it is
// simply a software voice owned by VoiceMixer - there is no device object per
// voice, because neither PipeWire nor ALSA has a concept of one.
//------------------------------------------------------------------------------
struct VoiceHandle {
	SoftVoice* soft = nullptr;
};

static inline SoftVoice* soft_of(VoiceHandle* v)
{
	return v ? v->soft : nullptr;
}

namespace {

//------------------------------------------------------------------------------
// Speaker bits and channel roles.
//
// The SPEAKER_xxx values audio_3d.cpp and mixer.cpp expect, spelled out
// numerically because they come from a Windows header this file must not
// include. Every device channel is described by ONE of these bits, whatever
// path produced it, and the upmix assigns content by the channel's ROLE.
//------------------------------------------------------------------------------
constexpr uint32_t kSpkFL  = 0x1;      constexpr uint32_t kSpkFR  = 0x2;
constexpr uint32_t kSpkFC  = 0x4;      constexpr uint32_t kSpkLFE = 0x8;
constexpr uint32_t kSpkBL  = 0x10;     constexpr uint32_t kSpkBR  = 0x20;
constexpr uint32_t kSpkFLC = 0x40;     constexpr uint32_t kSpkFRC = 0x80;
constexpr uint32_t kSpkBC  = 0x100;    constexpr uint32_t kSpkSL  = 0x200;
constexpr uint32_t kSpkSR  = 0x400;    constexpr uint32_t kSpkTC  = 0x800;
constexpr uint32_t kSpkTFL = 0x1000;   constexpr uint32_t kSpkTFC = 0x2000;
constexpr uint32_t kSpkTFR = 0x4000;   constexpr uint32_t kSpkTBL = 0x8000;
constexpr uint32_t kSpkTBC = 0x10000;  constexpr uint32_t kSpkTBR = 0x20000;

constexpr int kMaxDevChannels = 8;

// What a device channel gets out of the stereo mix.
enum Role : uint8_t {
	kRoleL,         // left front, untouched
	kRoleR,         // right front, untouched
	kRoleC,         // center = mono sum
	kRoleLFE,       // low-passed mono sum
	kRoleSurL,      // left surround/rear/height: difference + delayed mono fill
	kRoleSurR,      // right of the same, difference inverted
	kRoleSurMono    // centre rear/height: delayed mono fill only
};

// The tuning knob for the surround fill: the level of the delayed mono copy
// that feeds every non-front channel (0.5 = -6 dB). Final value, not a test.
constexpr float kSurroundFill = 0.5f;

static Role RoleOfSpeaker(uint32_t bit)
{
	switch (bit) {
	case kSpkFL:  case kSpkFLC:                       return kRoleL;
	case kSpkFR:  case kSpkFRC:                       return kRoleR;
	case kSpkFC:                                      return kRoleC;
	case kSpkLFE:                                     return kRoleLFE;
	case kSpkBL:  case kSpkSL:  case kSpkTFL: case kSpkTBL: return kRoleSurL;
	case kSpkBR:  case kSpkSR:  case kSpkTFR: case kSpkTBR: return kRoleSurR;
	case kSpkBC:  case kSpkTC:  case kSpkTFC: case kSpkTBC: return kRoleSurMono;
	default:                                          return kRoleC;   // unreachable: callers pass known bits
	}
}

// ALSA channel-map position -> speaker bit; 0 when we have no role for it.
static uint32_t SpeakerBitOfChmap(unsigned int pos)
{
	switch (pos) {
	case SND_CHMAP_FL:   return kSpkFL;
	case SND_CHMAP_FR:   return kSpkFR;
	case SND_CHMAP_FC:
	case SND_CHMAP_MONO: return kSpkFC;
	case SND_CHMAP_LFE:  return kSpkLFE;
	case SND_CHMAP_RL:   return kSpkBL;
	case SND_CHMAP_RR:   return kSpkBR;
	case SND_CHMAP_FLC:  return kSpkFLC;
	case SND_CHMAP_FRC:  return kSpkFRC;
	case SND_CHMAP_RC:   return kSpkBC;
	case SND_CHMAP_SL:   return kSpkSL;
	case SND_CHMAP_SR:   return kSpkSR;
	case SND_CHMAP_TC:   return kSpkTC;
	case SND_CHMAP_TFL:  return kSpkTFL;
	case SND_CHMAP_TFC:  return kSpkTFC;
	case SND_CHMAP_TFR:  return kSpkTFR;
	case SND_CHMAP_TRL:  return kSpkTBL;
	case SND_CHMAP_TRC:  return kSpkTBC;
	case SND_CHMAP_TRR:  return kSpkTBR;
	default:             return 0;
	}
}

#ifdef AAE_HAVE_PIPEWIRE
//------------------------------------------------------------------------------
// PipeWire layout helpers (ported from the video player's
// audio_output_pipewire_format.h, PCM half only).
//------------------------------------------------------------------------------

// SPA channel positions in SPEAKER_xxx BIT ORDER: index n is the position of
// speaker bit (1 << n). That makes the mask pcmLayout builds a ready-made
// WAVEFORMATEXTENSIBLE channel mask AND the canonical interleave order.
constexpr uint32_t kPwPositions[] = {
	SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR, SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
	SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR, SPA_AUDIO_CHANNEL_FLC, SPA_AUDIO_CHANNEL_FRC,
	SPA_AUDIO_CHANNEL_RC, SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR, SPA_AUDIO_CHANNEL_TC,
	SPA_AUDIO_CHANNEL_TFL, SPA_AUDIO_CHANNEL_TFC, SPA_AUDIO_CHANNEL_TFR,
	SPA_AUDIO_CHANNEL_TRL, SPA_AUDIO_CHANNEL_TRC, SPA_AUDIO_CHANNEL_TRR
};
constexpr const char* kPwPositionNames[] = {
	"FL", "FR", "FC", "LFE", "RL", "RR", "FLC", "FRC", "RC", "SL", "SR", "TC",
	"TFL", "TFC", "TFR", "TRL", "TRC", "TRR"
};
static_assert(std::size(kPwPositions) == std::size(kPwPositionNames),
              "position names must parallel the position table");

static uint32_t PwSpeakerBit(uint32_t position)
{
	for (size_t i = 0; i < std::size(kPwPositions); ++i)
		if (kPwPositions[i] == position) return 1u << i;
	return 0;
}

static const char* PwPositionName(uint32_t position)
{
	for (size_t i = 0; i < std::size(kPwPositions); ++i)
		if (kPwPositions[i] == position) return kPwPositionNames[i];
	return "?";
}

static std::string PwLayoutNames(const uint32_t* positions, uint32_t channels)
{
	std::string out;
	for (uint32_t c = 0; c < channels; ++c) {
		if (c) out += ' ';
		out += PwPositionName(positions[c]);
	}
	return out;
}

// Normalise a sink's reported positions into the canonical order. Rejects what
// we cannot place: no positions, more than 8 channels, an unknown position, a
// duplicate. MONO is treated as FC.
static bool PwPcmLayout(const uint32_t* reported, uint32_t channels,
                        spa_audio_info_raw& native, uint64_t& mask)
{
	if (!reported || !channels || channels > 8) return false;
	uint64_t bits = 0;
	for (uint32_t c = 0; c < channels; ++c) {
		const uint32_t p = reported[c] == SPA_AUDIO_CHANNEL_MONO ? uint32_t(SPA_AUDIO_CHANNEL_FC) : reported[c];
		const auto found = std::find(std::begin(kPwPositions), std::end(kPwPositions), p);
		if (found == std::end(kPwPositions)) return false;
		const auto bit = uint64_t{ 1 } << (found - std::begin(kPwPositions));
		if (bits & bit) return false;
		bits |= bit;
	}
	native.channels = 0;
	native.flags = SPA_AUDIO_FLAG_NONE;
	for (unsigned bit = 0; bit < std::size(kPwPositions); ++bit)
		if (bits & (uint64_t{ 1 } << bit)) native.position[native.channels++] = kPwPositions[bit];
	mask = bits;
	return true;
}

// Did the server settle on exactly the PCM format we asked for? The fields
// that matter are format, rate, channel count and every position; anything
// else means our interleave would land in the wrong speakers.
static bool PwAccepts(const spa_pod* pod, int rate, unsigned channels,
                      const spa_audio_info_raw& requested)
{
	uint32_t type = 0, subtype = 0;
	if (!pod || spa_format_parse(pod, &type, &subtype) < 0 || type != SPA_MEDIA_TYPE_audio) return false;
	spa_audio_info_raw info{};
	if (subtype != SPA_MEDIA_SUBTYPE_raw || spa_format_audio_raw_parse(pod, &info) < 0 ||
	    info.format != SPA_AUDIO_FORMAT_S16_LE || info.rate != static_cast<uint32_t>(rate) ||
	    info.channels != channels) return false;
	return std::equal(info.position, info.position + channels, requested.position);
}

// Same as the video player: take the thread-loop lock for a scope.
struct PwLock {
	pw_thread_loop* loop;
	explicit PwLock(pw_thread_loop* l) : loop(l) { if (loop) pw_thread_loop_lock(loop); }
	~PwLock() { if (loop) pw_thread_loop_unlock(loop); }
};

// Server-supplied text goes into our log: bound it and strip control bytes.
static std::string PwClean(const char* text)
{
	std::string out = text ? text : "(none)";
	if (out.size() > 256) out.resize(256);
	for (auto& c : out) if (static_cast<unsigned char>(c) < 32) c = ' ';
	return out;
}

static const char* PwField(const spa_dict* props, const char* key)
{
	auto* value = props ? spa_dict_lookup(props, key) : nullptr;
	return value ? value : "";
}
#endif // AAE_HAVE_PIPEWIRE

//------------------------------------------------------------------------------
// LinuxAudioBackend
//------------------------------------------------------------------------------
class LinuxAudioBackend final : public IAudioBackend {
public:
	LinuxAudioBackend() = default;
	~LinuxAudioBackend() override;

	bool Init(int rateHz, int fps) override;
	void Shutdown() override;

	uint8_t* GetNextBuffer() override;
	bool     Submit(uint8_t* buffer, uint32_t bytes) override;

	void  SetMasterVolume(float linear) override;
	float GetMasterVolume() const override;

	uint32_t OutputChannelCount() const override;
	uint32_t OutputChannelMask() const override;

	VoiceHandle* VoiceCreate(const WaveFormat& fmt) override;
	void         VoiceDestroy(VoiceHandle* v) override;

	bool VoiceSubmit(VoiceHandle* v, const uint8_t* data,
	                 uint32_t bytes, bool loop) override;
	bool VoiceStart(VoiceHandle* v) override;
	void VoiceStop(VoiceHandle* v) override;
	void VoiceFlush(VoiceHandle* v) override;
	void VoiceExitLoop(VoiceHandle* v) override;

	void VoiceSetVolume(VoiceHandle* v, float gain) override;
	void VoiceSetFrequencyRatio(VoiceHandle* v, float ratio) override;

	uint32_t VoiceBuffersQueued(VoiceHandle* v) override;
	uint32_t VoiceInputChannels(VoiceHandle* v) override;

	bool VoiceSetOutputMatrix(VoiceHandle* v, uint32_t srcChannels,
	                          uint32_t dstChannels,
	                          const float* matrix) override;

private:
	// ---- shared by both device paths -----------------------------------------
	bool InitAlsa(unsigned int rate, int f);
	void FeedThread();                                   // ALSA only
	void SetLayout(const uint32_t* speakerBits, uint32_t count);
	void SetupBuffers(unsigned int rate, uint32_t maxFrames);
	void LogLayout();
	void ProduceDeviceFrames(int16_t* dst, uint32_t frames);
	void Upmix(const int16_t* stereo, int16_t* dst, uint32_t frames);
	void EncodeStereo(int16_t* buf, uint32_t frames);

	const char*       m_backendName = "audio";   // "PipeWire" or "ALSA", for log prefixes
	bool              m_active      = false;     // an Init succeeded and its totals are not yet logged

	snd_pcm_t*        m_pcm      = nullptr;
	VoiceMixer        m_mixer;
	std::thread       m_thread;
	std::atomic<bool> m_running{ false };

	uint32_t          m_channels = 2;            // APP side: voice mixer, ring, Submit contract
	snd_pcm_uframes_t m_period   = 0;            // frames per emulated video frame (rate / fps)
	uint32_t          m_maxFrames = 0;           // largest chunk ProduceDeviceFrames is given
	uint32_t          m_maxRequested = 0;        // largest pull seen; consumer thread only

	OutputTone m_tone;                 // global audio_filter, applied to the stereo mix
	std::vector<int16_t> m_scratch;    // stereo mix when the device is not plain FL,FR
	std::vector<int16_t> m_devBuf;     // ALSA only: one period, device-interleaved
	std::vector<uint8_t> m_appBuffer;  // Returned by GetNextBuffer()

	// ---- device geometry ------------------------------------------------------
	//
	// EVERYTHING app-side stays stereo: the voice mixer, the app buffer, the
	// streaming ring and every game-facing contract. A device with any other
	// layout gets the finished stereo mix expanded per channel by ROLE (see
	// Upmix) as the very last step before the hardware:
	//
	//   FL/FR = L/R untouched      C = 0.5*(L+R)       LFE = low-passed mono
	//   surrounds = +-0.5*(L-R) + kSurroundFill*mono, ~12 ms late
	//
	// FL/FR carrying the original stereo untouched is the graceful-degradation
	// guarantee: if anything downstream drops the extra channels, the result is
	// exactly the plain stereo mix, never less.
	uint32_t m_devChannels  = 2;
	uint32_t m_devMask      = kSpkFL | kSpkFR;
	Role     m_role[kMaxDevChannels] = { kRoleL, kRoleR };
	bool     m_stereoDirect = true;      // device is exactly FL,FR: mix straight into it

	// [main] hdmi_passthrough (mixer.h): stereo PCM processing for a TV or
	// soundbar that does its own surround. NOT bitstream passthrough. Decided
	// once per Init (DecideHdmiStereo), before the first sample is made, and
	// constant afterwards, so no atomic is needed. mixer.cpp reads it to
	// enable the chip-side widener.
	bool m_hdmiStereo = false;
	bool HdmiStereoActive() const override { return m_hdmiStereo; }
	const char* OutputModeDescription() const override
	{
		if (m_hdmiStereo)   return "SOUNDBAR (HDMI)";
		if (m_stereoDirect) return "STEREO";
		snprintf(m_modeDesc, sizeof(m_modeDesc), "SURROUND (%u CH)", (unsigned)m_devChannels);
		return m_modeDesc;
	}
	mutable char m_modeDesc[32] = {};
	void DecideHdmiStereo(const char* sinkName, const char* sinkDesc, bool isPipeWire);

	std::vector<float> m_surDelay;       // ~12 ms decorrelation ring, stride 2 (see Upmix)
	size_t             m_surPos   = 0;
	float              m_lfeState = 0.0f;   // one-pole low-pass state
	float              m_lfeK     = 0.0f;   // its coefficient, set from rate

	// Matrix surround ENCODE state for a plain-stereo device (EncodeStereo): a
	// delayed mono ambience injected antiphase into L/R so downstream
	// difference-driven upmixers (PipeWire psd, Pro Logic, soundbars) derive
	// rears from our near-mono content. Empty when disabled or when the device
	// has its own surround channels (the two are mutually exclusive).
	std::vector<float> m_encDelay;
	size_t             m_encPos = 0;

	// ---- streaming path -----------------------------------------------------
	//
	// mixer_update_internal() mixes EVERY emulated sound chip - POKEY, AY-8910,
	// TMS5220, the DACs - into GetNextBuffer() and hands it to Submit() once per
	// video frame. That is the bulk of a game's audio; the voice path carries
	// only WAV samples. Both must reach the same device, so Submit() queues
	// here and the device side sums this with the voice mix, which is what
	// XAudio2 does internally with two source voices.
	//
	// A ring rather than a single buffer because the two sides run on different
	// threads at nominally the same rate: the game thread submits one video
	// frame's worth, the device side consumes whatever the server or the period
	// asks for. They match on average and jitter either side of it.
	std::mutex           m_streamLock;
	std::vector<int16_t> m_streamRing;         // interleaved, m_channels-wide
	size_t               m_streamHead  = 0;    // read cursor, in samples
	size_t               m_streamCount = 0;    // samples currently queued
	uint64_t             m_streamStarved = 0;  // consumer found it short
	uint64_t             m_streamDropped = 0;  // submit overflowed the ring
	bool                 m_streamSeen  = false;// anything ever submitted

	// Diagnostics that distinguish the three ways "no streaming audio" can
	// happen, which are otherwise indistinguishable from outside:
	//   submits == 0            -> nothing is calling Submit at all
	//   submits > 0, peak == 0  -> it is being called with SILENCE, so the
	//                              fault is upstream in the mixer, not here
	//   submits > 0, peak > 0   -> real audio is arriving and the fault is in
	//                              this backend or below it
	// Draining the ring the moment anything arrives leaves no slack: the
	// consumer pulls on its own schedule and the game submits one block per
	// video frame, so the slightest jitter empties the ring and plays a gap.
	// Priming waits for a small cushion before consuming, and re-primes if the
	// ring is ever emptied completely - the standard fix for a producer and
	// consumer that are rate-matched on average but not instantaneously.
	std::chrono::steady_clock::time_point m_streamFirstAt{};
	bool     m_streamPriming = true;
	uint64_t m_streamReprimes = 0;

	uint64_t m_streamSubmits = 0;
	uint64_t m_streamSamples = 0;
	int32_t  m_streamPeak    = 0;   // max |sample| ever submitted
	bool     m_streamReported = false;

#ifdef AAE_HAVE_PIPEWIRE
	// ---- native PipeWire path -------------------------------------------------
	//
	// Threading: the thread loop owns every callback. `process` runs on it with
	// its lock held, so it may take VoiceMixer's and m_streamLock - nothing on
	// the game thread calls pw_* except Init and Shutdown. All the pw state
	// below is touched only under the loop lock (or before the loop starts /
	// after it stops).
	bool InitPipeWire(int f);
	void ShutdownPipeWire();
	bool PwConnectServer();
	bool PwSelectSink();
	bool PwStartStream();
	void PwCloseStream();
	void PwFail(const char* action, int result);
	bool PwSync();

	// Deadline wait, ported from the video player: every handshake in Init is
	// "wait until a callback flips this condition", bounded so a wedged server
	// cannot hang startup.
	template<class Ready> bool PwWait(Ready ready) {
		timespec deadline{};
		if (pw_thread_loop_get_time(m_pwLoop, &deadline, 2000 * SPA_NSEC_PER_MSEC) < 0) return false;
		while (!ready() && !m_pwFailed) {
			int result = pw_thread_loop_timed_wait_full(m_pwLoop, &deadline);
			if (result < 0) { PwFail("operation timeout", result); break; }
		}
		return !m_pwFailed && ready();
	}

	static void PwCoreInfo(void* data, const pw_core_info* info);
	static void PwCoreDone(void* data, uint32_t id, int seq);
	static void PwCoreError(void* data, uint32_t id, int seq, int res, const char* message);
	static int  PwMetadataProperty(void* data, uint32_t subject, const char* key, const char* type, const char* value);
	static void PwRegistryGlobal(void* data, uint32_t id, uint32_t permissions, const char* type, uint32_t version, const spa_dict* props);
	static void PwRegistryRemove(void* data, uint32_t id);
	static void PwSinkInfo(void* data, const pw_node_info* info);
	static void PwSinkParam(void* data, int seq, uint32_t id, uint32_t index, uint32_t next, const spa_pod* param);
	static void PwStateChanged(void* data, pw_stream_state old, pw_stream_state state, const char* error);
	static void PwParamChanged(void* data, uint32_t id, const spa_pod* param);
	static void PwProcess(void* data);

	pw_thread_loop* m_pwLoop     = nullptr;
	pw_context*     m_pwContext  = nullptr;
	pw_core*        m_pwCore     = nullptr;
	pw_registry*    m_pwRegistry = nullptr;
	pw_metadata*    m_pwMetadata = nullptr;
	pw_node*        m_pwSink     = nullptr;
	pw_stream*      m_pwStream   = nullptr;
	spa_hook        m_pwCoreHook{}, m_pwRegistryHook{}, m_pwMetadataHook{}, m_pwSinkHook{}, m_pwStreamHook{};

	struct PwSinkEntry { uint32_t id; std::string name, serial; int priority; std::string description; };
	std::vector<PwSinkEntry> m_pwSinks;
	std::string m_pwDefaultSink, m_pwTargetName, m_pwTargetSerial, m_pwTargetDesc;
	uint32_t    m_pwTargetId = PW_ID_ANY;

	spa_audio_info_raw m_pwPcm{};          // the sink's layout, normalised; defaults to stereo
	uint64_t           m_pwPcmMask = 3;
	const char*        m_pwLayoutSource = "none";   // "Format" or "EnumFormat": who supplied m_pwPcm
	bool               m_pwHaveLayout   = false;
	bool               m_pwRequestedActiveFormat = false;
	spa_audio_info_raw m_pwStreamFmt{};    // what we ASK the server for

	int  m_pwPendingSync = -1, m_pwDoneSync = -1;
	bool m_pwStarted = false;              // thread loop running
	bool m_pwFailed = false, m_pwNegotiated = false, m_pwLinked = false;
	bool m_pwClosing = false, m_pwUp = false;   // m_pwUp: bring-up finished, now playing
	bool m_pwFirstProcessLogged = false;
	unsigned m_pwLogs = 0;                 // bounded logging, like the video player's `logs`

	// process() counters; loop thread only, read after the loop is stopped.
	uint64_t m_pwCallbacks = 0, m_pwFrames = 0, m_pwSilent = 0, m_pwBadBuffers = 0;
	bool     m_pwWasUsed = false;
#endif
};

//------------------------------------------------------------------------------
// Shared: geometry, buffers, the per-frame production path
//------------------------------------------------------------------------------

// Install the device layout: one speaker bit per interleaved channel, in the
// order the device wants them. Derives the roles, the channel mask and whether
// the device is exactly FL,FR (mix straight into it, no upmix).
void LinuxAudioBackend::SetLayout(const uint32_t* speakerBits, uint32_t count)
{
	m_devChannels = count;
	m_devMask     = 0;
	for (uint32_t c = 0; c < count && c < (uint32_t)kMaxDevChannels; ++c) {
		m_role[c]  = RoleOfSpeaker(speakerBits[c]);
		m_devMask |= speakerBits[c];
	}
	m_stereoDirect = (count == 2 && speakerBits[0] == kSpkFL && speakerBits[1] == kSpkFR);
}

//------------------------------------------------------------------------------
// SetupBuffers - the device-independent tail of both Init paths. Requires
// m_period, the layout (SetLayout) and the rate to be final. maxFrames is the
// largest chunk ProduceDeviceFrames will ever be handed: scratch is sized from
// it once so the PipeWire callback never allocates.
//------------------------------------------------------------------------------
void LinuxAudioBackend::SetupBuffers(unsigned int rate, uint32_t maxFrames)
{
	m_mixer.Configure(m_channels, rate, m_hdmiStereo);
	m_maxFrames = maxFrames;

	if (m_stereoDirect)
		m_scratch.clear();
	else
		m_scratch.assign((size_t)m_maxFrames * m_channels, 0);

	m_surDelay.clear();
	m_surPos   = 0;
	m_lfeState = 0.0f;
	if (!m_stereoDirect) {
		// ~12ms surround delay: decorrelates the surround signal from the
		// fronts, which is what makes it read as "behind you" rather than
		// "louder". Stride 2: each slot holds the difference component AND the
		// mono fill (see Upmix for why the surrounds need both).
		m_surDelay.assign(((size_t)(rate * 12 / 1000) + 1) * 2, 0.0f);
		// One-pole low-pass at ~120 Hz for the LFE feed.
		m_lfeK = 1.0f - expf(-2.0f * 3.14159265f * 120.0f / (float)rate);
	}

	// ONE FRAME OF HEADROOM, matching XAudio2Backend::Init. When rate % fps is
	// non-zero the mixer pays back the truncation by emitting N+1 samples on
	// some frames (see mixer_update_internal's g_spf_rem accumulator), and it
	// writes them into whatever GetNextBuffer() returned. Sized exactly to
	// m_period, that last frame lands past the end of the heap block.
	m_appBuffer.assign(((size_t)m_period + 1) * m_channels * sizeof(int16_t), 0);

	// Output tone filter works on the app-side stereo mix (see ProduceDeviceFrames).
	m_tone.configure(rate, 2);

	// Eight periods of streaming queue: deep enough to ride out a late video
	// frame, shallow enough that it cannot become a second hidden latency
	// buffer on top of the device's own.
	m_streamRing.assign((size_t)m_period * m_channels * 8, 0);
	m_streamHead    = 0;
	m_streamCount   = 0;
	m_streamPriming = true;

	// Plain-stereo device: matrix surround encode ring (see EncodeStereo).
	// Same ~12ms decorrelation delay the multichannel path uses.
	m_encDelay.clear();
	m_encPos = 0;
	if (m_stereoDirect && mixer_get_surround_encode() && !m_hdmiStereo)
		m_encDelay.assign((size_t)(rate * 12 / 1000) + 1, 0.0f);
}

//------------------------------------------------------------------------------
// DecideHdmiStereo - [main] hdmi_passthrough, resolved once per Init and logged
// as the one line that says which and why. Must run after SetLayout and before
// SetupBuffers.
//
// This is stereo PCM processing for a TV or soundbar that makes its own
// surround from stereo; it is NOT Dolby/DTS bitstream passthrough. auto turns
// it on when the PipeWire sink is HDMI (ALSA names HDMI and DisplayPort ports
// "hdmi" in node.name / node.description) and never on the raw ALSA fallback.
//------------------------------------------------------------------------------
void LinuxAudioBackend::DecideHdmiStereo(const char* sinkName, const char* sinkDesc, bool isPipeWire)
{
	const int mode = mixer_get_hdmi_passthrough();   // -1 auto, 0 off, 1 on
	auto hasHdmi = [](const char* s) {
		if (!s) return false;
		std::string lower(s);
		for (char& c : lower) c = (char)tolower((unsigned char)c);
		return lower.find("hdmi") != std::string::npos;
	};

	std::string why;
	if (mode == 1) {
		m_hdmiStereo = true;
		why = "forced on by hdmi_passthrough=1";
	} else if (mode == 0) {
		m_hdmiStereo = false;
		why = "forced off by hdmi_passthrough=0";
	} else if (!isPipeWire) {
		m_hdmiStereo = false;
		why = "raw ALSA output, HDMI cannot be told apart";
	} else {
		m_hdmiStereo = hasHdmi(sinkName) || hasHdmi(sinkDesc);
		why = std::string("sink '") + (sinkName ? sinkName : "") +
		      (m_hdmiStereo ? "' is HDMI" : "' is not HDMI");
	}

	LOG_INFO("%s: hdmi_passthrough=%s -> %s (%s): %s", m_backendName,
	         mode < 0 ? "auto" : (mode ? "1" : "0"), m_hdmiStereo ? "ON" : "OFF", why.c_str(),
	         m_hdmiStereo
	             ? "mono sources widened for the TV/soundbar's own upmix, output on FL/FR"
	             : "discrete per-position upmix");
}

void LinuxAudioBackend::LogLayout()
{
	if (m_hdmiStereo)
		return;   // DecideHdmiStereo already logged the layout in use
	if (m_stereoDirect)
		LOG_INFO("%s: STEREO out (speakers=%d), matrix surround encode %s - downstream "
		         "upmixers (PipeWire/Pro Logic/soundbar) derive the rears from the "
		         "encoded L-R content; surround_encode=0 for plain stereo",
		         m_backendName, mixer_get_speaker_config(),
		         mixer_get_surround_encode() ? "ON (-10dB, 12ms)" : "OFF");
	else
		LOG_INFO("%s: %u-channel output (mask 0x%X), every channel filled by position: "
		         "fronts untouched, center = mono, LFE = low-passed mono, "
		         "surrounds = difference + %.0f dB delayed mono fill",
		         m_backendName, m_devChannels, m_devMask,
		         20.0 * log10((double)kSurroundFill));
}

//------------------------------------------------------------------------------
// EncodeStereo - matrix surround ENCODE, in place on the stereo mix.
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
// same ~12ms-delayed mono ambience the multichannel path feeds its surrounds,
// at ~-10dB - and the delay keeps it from smearing the direct sound.
//------------------------------------------------------------------------------
void LinuxAudioBackend::EncodeStereo(int16_t* buf, uint32_t frames)
{
	constexpr float kEnc = 0.32f;   // ~-10dB ambience level
	const size_t n = m_encDelay.size();

	for (size_t i = 0; i < (size_t)frames; ++i) {
		const float L = (float)buf[i * 2];
		const float R = (float)buf[i * 2 + 1];

		const float d = m_encDelay[m_encPos];
		m_encDelay[m_encPos] = kEnc * 0.5f * (L + R);
		m_encPos = (m_encPos + 1 == n) ? 0 : m_encPos + 1;

		const float l = L - d;
		const float r = R + d;
		buf[i * 2]     = (int16_t)(l < -32768.0f ? -32768.0f : (l > 32767.0f ? 32767.0f : l));
		buf[i * 2 + 1] = (int16_t)(r < -32768.0f ? -32768.0f : (r > 32767.0f ? 32767.0f : r));
	}
}

//------------------------------------------------------------------------------
// Upmix - stereo mix -> device layout, every channel filled by its ROLE.
//
// The classic Dolby Surround decode, chosen over "copy L/R to the rears"
// because it actually separates: the surrounds get the L-R DIFFERENCE, which
// is where stereo ambience and effects live, while anything center-panned
// (most gameplay-critical audio) cancels out of them and stays anchored up
// front.
//
// Every coefficient keeps 0.5 headroom, deliberately: 0.5*(L+R) and 0.5*(L-R)
// can never exceed int16 range, so these stages cannot clip - the textbook
// 0.707 gains buy 3dB of level at the cost of clamping distortion on loud mono
// content. (The surrounds sum two terms, so they are clamped explicitly.)
//
// The surrounds run ~12ms late via a delay line. Identical undelayed signals in
// front and rear speakers just sound louder; the delay is what makes the
// surround content read as spatially separate (precedence effect).
//------------------------------------------------------------------------------
void LinuxAudioBackend::Upmix(const int16_t* stereo, int16_t* dst, uint32_t frames)
{
	// The surrounds carry TWO components, and the second one is the load-bearing
	// lesson of the first listening test:
	//
	//   difference (L-R): real stereo separation - ambience, panned effects.
	//     The textbook surround feed, but arcade audio is overwhelmingly MONO
	//     (single-channel chips mixed center), and L-R of mono is ZERO. A
	//     passive matrix therefore delivered six perfect channels of which two
	//     were silent - "log shows surround, ears hear stereo", on BOTH
	//     platforms (XAudio2's stock 2->5.1 matrix leaves rears empty too).
	//
	//   mono fill (kSurroundFill, delayed): a late copy of the whole mix. This
	//     is what active upmixers (PipeWire's, the soundbar's own stereo
	//     expansion, Dolby PLII) manufacture rear content from when the source
	//     is mono-ish - and those helpers all switch OFF the moment a source
	//     hands them a discrete multichannel layout, so we must bring our own.
	//
	// Both run through the delay ring (stride 2). Height and centre-rear
	// channels reuse the same feed so that EVERY channel of whatever layout the
	// sink reports has content.
	const size_t delaySlots = m_surDelay.size() / 2;

	auto clamp16 = [](float v) -> int16_t {
		return (int16_t)(v < -32768.0f ? -32768.0f : (v > 32767.0f ? 32767.0f : v));
	};

	for (size_t i = 0; i < (size_t)frames; ++i) {
		const float L = (float)stereo[i * 2];
		const float R = (float)stereo[i * 2 + 1];
		const float mono = 0.5f * (L + R);

		// Push today's pair, read the ~12ms-old pair.
		float* slot = &m_surDelay[m_surPos * 2];
		const float dDelayed = slot[0];
		const float mDelayed = slot[1];
		slot[0] = 0.5f * (L - R);
		slot[1] = kSurroundFill * mono;
		m_surPos = (m_surPos + 1 == delaySlots) ? 0 : m_surPos + 1;

		// LFE: one-pole low-pass (~120 Hz) of the mono sum.
		m_lfeState += m_lfeK * (mono - m_lfeState);

		int16_t* frame = dst + i * m_devChannels;
		for (uint32_t c = 0; c < m_devChannels; ++c) {
			switch (m_role[c]) {
			case kRoleL:       frame[c] = (int16_t)L;                  break;   // untouched stereo
			case kRoleR:       frame[c] = (int16_t)R;                  break;
			case kRoleC:       frame[c] = (int16_t)mono;               break;   // center = mono sum
			case kRoleLFE:     frame[c] = (int16_t)m_lfeState;         break;
			// Difference antiphase across the pair for width; fill IN phase in
			// both, so mono content fills the room instead of cancelling.
			case kRoleSurL:    frame[c] = clamp16( dDelayed + mDelayed); break;
			case kRoleSurR:    frame[c] = clamp16(-dDelayed + mDelayed); break;
			case kRoleSurMono: frame[c] = clamp16(mDelayed);           break;
			}
		}
	}
}

//------------------------------------------------------------------------------
// ProduceDeviceFrames - the one place audio is made, for BOTH device paths.
// Called by the PipeWire process callback (any size the server asks for,
// chunked to m_maxFrames) and by the ALSA feed thread (one period). Never
// allocates. `dst` is device-interleaved (m_devChannels per frame).
//------------------------------------------------------------------------------
void LinuxAudioBackend::ProduceDeviceFrames(int16_t* dst, uint32_t frames)
{
	if (frames == 0) return;

	// Voices first - MixInto REPLACES the buffer's contents. A device that is
	// exactly FL,FR is mixed into directly; anything else goes via m_scratch.
	int16_t* stereo = m_stereoDirect ? dst : m_scratch.data();
	m_mixer.MixInto(stereo, frames);

	// Master volume for the streaming audio. MixInto already applied it to the
	// voices; the chip stream is summed on top, so it has to get the same gain
	// here - parity with XAudio2, whose mastering voice scales voices and
	// stream together. Without this neither the volume control nor the pause
	// mute (mixer.cpp calls SetMasterVolume(0) while paused) reached the sound
	// chips on Linux. Read BEFORE taking m_streamLock: GetMasterVolume takes
	// VoiceMixer's own lock, and the two locks are never held together.
	const float master = m_mixer.GetMasterVolume();

	// Then sum the streaming chip audio on top, saturating. XAudio2 gets this
	// for free by running two source voices into one mastering voice; here the
	// summing is ours to do. m_streamLock is NEVER held across MixInto.
	{
		const size_t want = (size_t)frames * m_channels;
		std::lock_guard<std::mutex> g(m_streamLock);
		const size_t cap = m_streamRing.size();

		// Build a cushion before consuming anything. Without it the ring is
		// drained as fast as it fills and every scheduling wobble becomes an
		// audible gap - measured at 203 starved periods against 60 submits
		// before this was added.
		//
		// The cushion must also cover the consumer's PULL size: a server that
		// asks for more frames than the cushion holds would starve the ring on
		// every single cycle. Hence max(2 periods, largest pull + 1 period),
		// capped at 6 periods so it still fits inside the 8-period ring.
		//
		// The cost is that much extra latency on top of the device buffer. That
		// is a deliberate trade: continuous audio slightly late beats punctual
		// audio full of holes.
		const size_t primeFrames = std::min<size_t>(
			std::max<size_t>(2 * (size_t)m_period, (size_t)m_maxRequested + (size_t)m_period),
			6 * (size_t)m_period);
		if (m_streamPriming && m_streamCount >= primeFrames * m_channels)
			m_streamPriming = false;

		if (!m_streamPriming) {
			const size_t have = (m_streamCount < want) ? m_streamCount : want;

			// The ring is consumed even at master 0 (pause mute): leaving it
			// to fill would replay stale chip audio the moment sound resumes.
			for (size_t i = 0; i < have; ++i) {
				const int32_t chip = (master == 1.0f)
				    ? (int32_t)m_streamRing[m_streamHead]
				    : (int32_t)lrintf((float)m_streamRing[m_streamHead] * master);
				const int32_t sum = (int32_t)stereo[i] + chip;
				stereo[i] = (int16_t)(sum < -32768 ? -32768 : (sum > 32767 ? 32767 : sum));
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

	// Global output filter ([main] audio_filter, output_tone.h): the finished
	// stereo mix (voices + streaming ring) is softened here, in place, before
	// any hdmi / encode / upmix branch. When stereo IS dst this filters dst.
	// OFF is a true bypass. The preset is read live every buffer.
	m_tone.process_i16(stereo, frames, mixer_get_output_filter());

	// hdmi_passthrough: the finished stereo mix goes to FL/FR as is and every
	// other position stays zero, so the TV/soundbar gets plain stereo PCM and
	// derives its own surround from it. (The widening was already done in the
	// mixers, on the mono sources only.) Positions come from m_role, which
	// SetLayout derived from the stream's PipeWire channel positions.
	if (m_hdmiStereo) {
		if (m_stereoDirect) return;   // stereo is already in dst, no matrix encode
		for (uint32_t i = 0; i < frames; ++i) {
			for (uint32_t c = 0; c < m_devChannels; ++c) {
				dst[i * m_devChannels + c] = m_role[c] == kRoleL ? stereo[i * 2] :
				                             m_role[c] == kRoleR ? stereo[i * 2 + 1] : 0;
			}
		}
		return;
	}
	// Plain-stereo devices get the matrix surround encode (if enabled) in
	// place; everything else gets the per-role expansion. Either way this is
	// the last stop before the hardware.
	if (m_stereoDirect) {
		if (!m_encDelay.empty())
			EncodeStereo(dst, frames);
	} else {
		Upmix(stereo, dst, frames);
	}
}

#ifdef AAE_HAVE_PIPEWIRE
//------------------------------------------------------------------------------
// Native PipeWire
//
// Ported from the sibling video player (mp4_av_player audio_output_pipewire.cpp),
// PCM half only. Its lifecycle is the lesson that cost hardware time to learn:
// a connect call returning OK - or the stream reaching PAUSED - is NOT
// negotiation. The stream is created INACTIVE, we wait for the graph to link it
// (Latency param seen, state PAUSED), activate it, and only then wait for the
// accepted Format and STREAMING. Unlike the player we never park it again:
// AAE plays continuously from here on.
//
// Left out on purpose: IEC958/encoded output, takeover and the parking sink,
// route-metadata writes, competitor tracking, the Manager media category (AAE
// only READS the default metadata) and the player's clock/queue machinery -
// timing comes entirely from the server pulling `process`.
//------------------------------------------------------------------------------
void LinuxAudioBackend::PwFail(const char* action, int result)
{
	m_pwFailed = true;
	if (m_pwLogs++ < 32)
		LOG_ERROR("PipeWire: %s failed (code %d: %s)", action, result,
		          strerror(result < 0 ? -result : result));
	if (m_pwLoop) pw_thread_loop_signal(m_pwLoop, false);
}

// A core round trip: when it returns, every event the server had queued for us
// at the time has been delivered. Registry discovery needs two (the first may
// bind the metadata object, whose properties arrive during the second).
bool LinuxAudioBackend::PwSync()
{
	m_pwPendingSync = pw_core_sync(m_pwCore, PW_ID_CORE, 0);
	if (m_pwPendingSync < 0) { PwFail("core sync", m_pwPendingSync); return false; }
	return PwWait([&] { return m_pwDoneSync == m_pwPendingSync; });
}

void LinuxAudioBackend::PwCoreInfo(void*, const pw_core_info* info)
{
	LOG_INFO("PipeWire: server version %s", PwClean(info->version).c_str());
}

void LinuxAudioBackend::PwCoreDone(void* data, uint32_t id, int seq)
{
	auto& self = *static_cast<LinuxAudioBackend*>(data);
	if (id == PW_ID_CORE) { self.m_pwDoneSync = seq; pw_thread_loop_signal(self.m_pwLoop, false); }
}

void LinuxAudioBackend::PwCoreError(void* data, uint32_t id, int, int res, const char* message)
{
	auto& self = *static_cast<LinuxAudioBackend*>(data);
	// The native protocol preserves the policy error text that Pulse drops.
	if (self.m_pwLogs++ < 32)
		LOG_ERROR("PipeWire: object=%u code=%d detail=%s", id, res, PwClean(message).c_str());
	// During bring-up any core error aborts the handshake. Once we are playing,
	// a stray error about some other object must not latch silence; a dead
	// stream reports itself through PwStateChanged.
	if (!self.m_pwUp) self.m_pwFailed = true;
	pw_thread_loop_signal(self.m_pwLoop, false);
}

// The server's session default: metadata "default", key default.audio.sink,
// value JSON {"name":"<node.name>"}.
int LinuxAudioBackend::PwMetadataProperty(void* data, uint32_t subject, const char* key,
                                          const char*, const char* value)
{
	auto& self = *static_cast<LinuxAudioBackend*>(data);
	if (subject == PW_ID_CORE && key && strcmp(key, "default.audio.sink") == 0) {
		self.m_pwDefaultSink.clear();
		if (value) {
			spa_json outer{}, object{};
			spa_json_init(&outer, value, strlen(value));
			if (spa_json_enter_object(&outer, &object) > 0) {
				char keyName[128], name[512];
				while (spa_json_get_string(&object, keyName, sizeof(keyName)) > 0 &&
				       spa_json_get_string(&object, name, sizeof(name)) > 0) {
					if (strcmp(keyName, "name") == 0) { self.m_pwDefaultSink = name; break; }
				}
			}
		}
	}
	pw_thread_loop_signal(self.m_pwLoop, false);
	return 0;
}

void LinuxAudioBackend::PwRegistryGlobal(void* data, uint32_t id, uint32_t, const char* type,
                                         uint32_t version, const spa_dict* props)
{
	auto& self = *static_cast<LinuxAudioBackend*>(data);
	if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0 &&
	    strcmp(PwField(props, PW_KEY_MEDIA_CLASS), "Audio/Sink") == 0) {
		self.m_pwSinks.push_back({ id, PwField(props, PW_KEY_NODE_NAME), PwField(props, PW_KEY_OBJECT_SERIAL),
		                           atoi(PwField(props, PW_KEY_PRIORITY_SESSION)),
		                           PwField(props, PW_KEY_NODE_DESCRIPTION) });
	} else if (!self.m_pwMetadata && strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 &&
	           strcmp(PwField(props, PW_KEY_METADATA_NAME), "default") == 0) {
		self.m_pwMetadata = static_cast<pw_metadata*>(pw_registry_bind(self.m_pwRegistry, id, type,
			std::min(version, uint32_t(PW_VERSION_METADATA)), 0));
		if (self.m_pwMetadata) {
			static const pw_metadata_events events = [] {
				pw_metadata_events e{};
				e.version = PW_VERSION_METADATA_EVENTS;
				e.property = PwMetadataProperty;
				return e;
			}();
			pw_metadata_add_listener(self.m_pwMetadata, &self.m_pwMetadataHook, &events, &self);
		}
	}
	pw_thread_loop_signal(self.m_pwLoop, false);
}

void LinuxAudioBackend::PwRegistryRemove(void* data, uint32_t id)
{
	auto& self = *static_cast<LinuxAudioBackend*>(data);
	self.m_pwSinks.erase(std::remove_if(self.m_pwSinks.begin(), self.m_pwSinks.end(),
		[id](const PwSinkEntry& s) { return s.id == id; }), self.m_pwSinks.end());
	// Logged, NOT fatal: the policy daemon may relink our stream elsewhere (an
	// HDMI sink vanishing on a TV power cycle is routine), and the stream's own
	// state callback reports it if the stream actually dies.
	if (id == self.m_pwTargetId && self.m_pwUp && self.m_pwLogs++ < 32)
		LOG_WARN("PipeWire: output sink '%s' (id %u) was removed - stream kept, "
		         "waiting for the session manager to relink it",
		         PwClean(self.m_pwTargetName.c_str()).c_str(), id);
	pw_thread_loop_signal(self.m_pwLoop, false);
}

void LinuxAudioBackend::PwSinkParam(void* data, int, uint32_t id, uint32_t, uint32_t, const spa_pod* param)
{
	auto& self = *static_cast<LinuxAudioBackend*>(data);
	if (!param || (id != SPA_PARAM_EnumFormat && id != SPA_PARAM_Format)) return;
	uint32_t type, subtype;
	if (spa_format_parse(param, &type, &subtype) < 0 || type != SPA_MEDIA_TYPE_audio) return;
	if (subtype == SPA_MEDIA_SUBTYPE_raw) {
		spa_audio_info_raw raw{};
		// Format is the active layout. EnumFormat is a fallback only; selecting
		// the largest capability can invent inactive speakers.
		if (spa_format_audio_raw_parse(param, &raw) >= 0 && (!self.m_pwHaveLayout || id == SPA_PARAM_Format) &&
		    PwPcmLayout(raw.position, raw.channels, self.m_pwPcm, self.m_pwPcmMask)) {
			if (raw.rate >= 8000 && raw.rate <= 192000) self.m_pwPcm.rate = raw.rate;
			self.m_pwHaveLayout   = true;
			self.m_pwLayoutSource = (id == SPA_PARAM_Format) ? "Format" : "EnumFormat";
		}
	}
}

void LinuxAudioBackend::PwSinkInfo(void* data, const pw_node_info* info)
{
	auto& self = *static_cast<LinuxAudioBackend*>(data);
	if (!info || self.m_pwRequestedActiveFormat) return;
	for (uint32_t n = 0; n < info->n_params; ++n) {
		if (info->params[n].id == SPA_PARAM_Format && (info->params[n].flags & SPA_PARAM_INFO_READ)) {
			self.m_pwRequestedActiveFormat = true;
			pw_node_enum_params(self.m_pwSink, 1, SPA_PARAM_Format, 0, UINT32_MAX, nullptr);
			break;
		}
	}
}

void LinuxAudioBackend::PwStateChanged(void* data, pw_stream_state, pw_stream_state state, const char* error)
{
	auto& self = *static_cast<LinuxAudioBackend*>(data);
	if (self.m_pwClosing) return;
	const bool dead = (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED);
	if (self.m_pwLogs++ < 32) {
		if (dead && self.m_pwUp)
			LOG_WARN("PipeWire: stream state=%s after startup (%s) - audio stops until restart",
			         pw_stream_state_as_string(state), PwClean(error).c_str());
		else
			LOG_INFO("PipeWire: stream state=%s detail=%s",
			         pw_stream_state_as_string(state), PwClean(error).c_str());
	}
	if (dead) self.m_pwFailed = true;
	pw_thread_loop_signal(self.m_pwLoop, false);
}

void LinuxAudioBackend::PwParamChanged(void* data, uint32_t id, const spa_pod* param)
{
	auto& self = *static_cast<LinuxAudioBackend*>(data);
	if ((id == SPA_PARAM_Format || id == SPA_PARAM_Latency) && self.m_pwLogs++ < 32)
		LOG_INFO("PipeWire: param=%s present=%d",
		         id == SPA_PARAM_Format ? "Format" : "Latency", param != nullptr);
	if (id == SPA_PARAM_Latency && param) self.m_pwLinked = true;
	if (id == SPA_PARAM_Format) {
		self.m_pwNegotiated = false;
		if (!param) { pw_thread_loop_signal(self.m_pwLoop, false); return; }
		// The server is allowed to counter-offer. Interleaving our channels into
		// a different count or order would put them in the wrong speakers, so
		// anything but exactly what we asked for is a failure, not a tolerance.
		if (!PwAccepts(param, (int)self.m_pwStreamFmt.rate, self.m_pwStreamFmt.channels, self.m_pwStreamFmt)) {
			self.PwFail("negotiated format differs from requested format", -ENOTSUP);
			return;
		}
		self.m_pwNegotiated = true;
		if (self.m_pwLogs++ < 32)
			LOG_INFO("PipeWire: accepted format S16LE rate=%u channels=%u [%s]",
			         self.m_pwStreamFmt.rate, self.m_pwStreamFmt.channels,
			         PwLayoutNames(self.m_pwStreamFmt.position, self.m_pwStreamFmt.channels).c_str());
		uint8_t bytes[512];
		spa_pod_builder builder = SPA_POD_BUILDER_INIT(bytes, sizeof(bytes));
		const int stride = (int)(self.m_devChannels * sizeof(int16_t));
		const int rate = (int)self.m_pwStreamFmt.rate;
		const spa_pod* buffers = static_cast<const spa_pod*>(spa_pod_builder_add_object(&builder,
			SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
			SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
			SPA_PARAM_BUFFERS_size, SPA_POD_CHOICE_RANGE_Int(rate / 10 * stride, stride, INT32_MAX),
			SPA_PARAM_BUFFERS_stride, SPA_POD_Int(stride)));
		int res = pw_stream_update_params(self.m_pwStream, &buffers, 1);
		if (res < 0) self.PwFail("configure stream buffers", res);
	}
	pw_thread_loop_signal(self.m_pwLoop, false);
}

//------------------------------------------------------------------------------
// process - the server asks for audio. Runs on the PipeWire thread loop with its
// lock held. NOT an RT callback (see PwStartStream), which is exactly why it may
// take VoiceMixer's mutex and m_streamLock inside ProduceDeviceFrames.
//------------------------------------------------------------------------------
void LinuxAudioBackend::PwProcess(void* data)
{
	auto& self = *static_cast<LinuxAudioBackend*>(data);
	pw_buffer* buffer = pw_stream_dequeue_buffer(self.m_pwStream);
	if (!buffer) return;

	spa_buffer* b = buffer->buffer;
	if (!b->n_datas || !b->datas[0].data || !b->datas[0].chunk) {
		// Counted, not fatal: hand it straight back and try the next cycle.
		++self.m_pwBadBuffers;
		pw_stream_queue_buffer(self.m_pwStream, buffer);
		return;
	}
	spa_data& target = b->datas[0];

	const size_t stride = (size_t)self.m_devChannels * sizeof(int16_t);
	size_t capacity = target.maxsize;
	if (buffer->requested)
		capacity = (size_t)std::min<uint64_t>(capacity, buffer->requested * stride);
	uint32_t frames = (uint32_t)(capacity / stride);

	++self.m_pwCallbacks;
	if (!self.m_pwNegotiated || self.m_pwFailed) {
		// Before the Format is accepted (or after a failure) the buffer layout
		// is not ours to fill: an empty chunk is a gap, not fabricated PCM.
		frames = 0;
		++self.m_pwSilent;
	}

	if (frames) {
		if (!self.m_pwFirstProcessLogged) {
			self.m_pwFirstProcessLogged = true;
			LOG_INFO("PipeWire: first process callback asks for %u frames (buffer holds %u)",
			         frames, (unsigned)(target.maxsize / stride));
		}
		// The largest pull seen is what the priming cushion must cover.
		if (frames > self.m_maxRequested) self.m_maxRequested = frames;

		// Chunks of at most m_maxFrames so the scratch buffer (sized once, in
		// Init) never has to grow inside the callback.
		int16_t* dst = static_cast<int16_t*>(target.data);
		uint32_t done = 0;
		while (done < frames) {
			const uint32_t n = std::min(frames - done, self.m_maxFrames);
			self.ProduceDeviceFrames(dst + (size_t)done * self.m_devChannels, n);
			done += n;
		}
		self.m_pwFrames += frames;
	}

	buffer->size = frames;
	target.chunk->offset = 0;
	target.chunk->stride = (int32_t)stride;
	target.chunk->size = (uint32_t)(frames * stride);
	target.chunk->flags = frames ? 0 : SPA_CHUNK_FLAG_EMPTY;
	pw_stream_queue_buffer(self.m_pwStream, buffer);
}

void LinuxAudioBackend::PwCloseStream()
{
	m_pwClosing = true;
	if (m_pwStream) {
		spa_hook_remove(&m_pwStreamHook);
		pw_stream_destroy(m_pwStream);
		m_pwStream = nullptr;
	}
	m_pwClosing = false;
	m_pwNegotiated = m_pwLinked = false;
}

// Steps 1-2: library, thread loop, context, core, registry, and start the loop.
// No core means no server, which is a NORMAL condition (the ALSA fallback exists
// for exactly those machines), so it is an INFO line, not an error.
bool LinuxAudioBackend::PwConnectServer()
{
	static std::once_flag once;
	std::call_once(once, [] { pw_init(nullptr, nullptr); });

	m_pwLoop = pw_thread_loop_new("AAE audio", nullptr);
	if (!m_pwLoop) { LOG_WARN("PipeWire: cannot create the thread loop"); return false; }
	m_pwContext = pw_context_new(pw_thread_loop_get_loop(m_pwLoop), nullptr, 0);
	// Plain connection, no special media category: AAE only listens to the
	// default-sink metadata and never rewrites routing, so none of the player's
	// Manager privileges are needed.
	m_pwCore = m_pwContext ? pw_context_connect(m_pwContext, nullptr, 0) : nullptr;
	if (!m_pwCore) {
		const int err = errno;
		LOG_INFO("PipeWire: no server reachable (%s) - using the raw ALSA path", strerror(err));
		return false;
	}

	static const pw_core_events coreEvents = [] {
		pw_core_events e{};
		e.version = PW_VERSION_CORE_EVENTS;
		e.info = PwCoreInfo;
		e.done = PwCoreDone;
		e.error = PwCoreError;
		return e;
	}();
	pw_core_add_listener(m_pwCore, &m_pwCoreHook, &coreEvents, this);

	m_pwRegistry = pw_core_get_registry(m_pwCore, PW_VERSION_REGISTRY, 0);
	if (!m_pwRegistry) { PwFail("get registry", -errno); return false; }
	static const pw_registry_events registryEvents = [] {
		pw_registry_events e{};
		e.version = PW_VERSION_REGISTRY_EVENTS;
		e.global = PwRegistryGlobal;
		e.global_remove = PwRegistryRemove;
		return e;
	}();
	pw_registry_add_listener(m_pwRegistry, &m_pwRegistryHook, &registryEvents, this);

	const int result = pw_thread_loop_start(m_pwLoop);
	if (result < 0) { PwFail("start mainloop", result); return false; }
	m_pwStarted = true;
	return true;
}

// Steps 3-4: pick the sink, bind it, and read the layout it advertises.
// Caller holds the loop lock.
bool LinuxAudioBackend::PwSelectSink()
{
	// Session default first; otherwise (default unset, or named a node that is
	// not an Audio/Sink) the highest priority.session sink, and say which.
	auto selected = std::find_if(m_pwSinks.begin(), m_pwSinks.end(),
		[&](const PwSinkEntry& s) { return s.name == m_pwDefaultSink; });
	const char* why = "session default (default.audio.sink)";
	if (selected == m_pwSinks.end()) {
		if (m_pwSinks.empty()) {
			LOG_WARN("PipeWire: server is up but has no Audio/Sink node - using the raw ALSA path");
			return false;
		}
		selected = std::max_element(m_pwSinks.begin(), m_pwSinks.end(),
			[](const PwSinkEntry& a, const PwSinkEntry& b) { return a.priority < b.priority; });
		why = m_pwDefaultSink.empty()
		    ? "no default.audio.sink set, using the highest priority.session sink"
		    : "default.audio.sink names no Audio/Sink node, using the highest priority.session sink";
	}
	m_pwTargetId     = selected->id;
	m_pwTargetName   = selected->name;
	m_pwTargetSerial = selected->serial;
	m_pwTargetDesc   = selected->description;

	m_pwSink = static_cast<pw_node*>(pw_registry_bind(m_pwRegistry, m_pwTargetId,
		PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
	if (!m_pwSink) { PwFail("bind selected sink", -errno); return false; }
	static const pw_node_events nodeEvents = [] {
		pw_node_events e{};
		e.version = PW_VERSION_NODE_EVENTS;
		e.info = PwSinkInfo;
		e.param = PwSinkParam;
		return e;
	}();
	pw_node_add_listener(m_pwSink, &m_pwSinkHook, &nodeEvents, this);
	pw_node_enum_params(m_pwSink, 0, SPA_PARAM_EnumFormat, 0, UINT32_MAX, nullptr);
	// Suspended sinks need not have a readable active Format. PwSinkInfo
	// queries it only when advertised; the second barrier collects that
	// optional request issued while the first barrier was in flight. As in the
	// player, an active Format overrides EnumFormat (EnumFormat alone can
	// invent inactive speakers).
	if (!PwSync() || !PwSync()) return false;

	LOG_INFO("PipeWire: output sink '%s' (id %u, serial %s) - %s",
	         PwClean(m_pwTargetName.c_str()).c_str(), m_pwTargetId,
	         PwClean(m_pwTargetSerial.c_str()).c_str(), why);
	if (m_pwHaveLayout)
		LOG_INFO("PipeWire: sink advertises %u channels [%s] at %u Hz (layout from %s)",
		         m_pwPcm.channels, PwLayoutNames(m_pwPcm.position, m_pwPcm.channels).c_str(),
		         m_pwPcm.rate, m_pwLayoutSource);
	else
		LOG_WARN("PipeWire: sink advertised no usable PCM layout - assuming stereo at %u Hz",
		         m_pwPcm.rate);
	return true;
}

// Steps 6-9: create the stream, run the connect/link/activate/negotiate
// lifecycle, neutralise stale volumes, report latency. Caller holds the lock.
bool LinuxAudioBackend::PwStartStream()
{
	const int rate = (int)m_pwStreamFmt.rate;

	uint8_t storage[1024];
	spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
	const spa_pod* format = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &m_pwStreamFmt);
	if (!format) { PwFail("build stream format", -EINVAL); return false; }

	pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
		PW_KEY_MEDIA_ROLE, "Game", PW_KEY_APP_NAME, "AAE Emulator", PW_KEY_NODE_NAME, "AAE Emulator",
		PW_KEY_NODE_DESCRIPTION, "AAE emulator audio",
		PW_KEY_TARGET_OBJECT, m_pwTargetSerial.empty() ? m_pwTargetName.c_str() : m_pwTargetSerial.c_str(), nullptr);
	if (!props) { PwFail("stream properties", -ENOMEM); return false; }
	pw_properties_setf(props, PW_KEY_NODE_RATE, "1/%d", rate);
	pw_properties_setf(props, PW_KEY_NODE_LATENCY, "%d/%d", rate / 100, rate);

	m_pwStream = pw_stream_new(m_pwCore, "AAE audio", props);
	if (!m_pwStream) { PwFail("create stream", -errno); return false; }
	static const pw_stream_events events = [] {
		pw_stream_events e{};
		e.version = PW_VERSION_STREAM_EVENTS;
		e.state_changed = PwStateChanged;
		e.param_changed = PwParamChanged;
		e.process = PwProcess;
		return e;
	}();
	pw_stream_add_listener(m_pwStream, &m_pwStreamHook, &events, this);

	// No PW_STREAM_FLAG_RT_PROCESS, on purpose: without it `process` runs on the
	// thread loop instead of the realtime data thread, so it is allowed to take
	// the VoiceMixer and streaming mutexes (a mutex in an RT callback is a
	// priority-inversion xrun waiting to happen). INACTIVE: the node is created
	// idle and activated below, once the graph has linked it.
	auto flags = static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_INACTIVE);
	int res = pw_stream_connect(m_pwStream, PW_DIRECTION_OUTPUT, PW_ID_ANY, flags, &format, 1);
	if (res < 0) { PwFail("connect playback", res); return false; }

	// An inactive node can be linked (Latency/PAUSED) without its buffers and
	// Format being negotiated, so waiting for Format first deadlocks startup on
	// real PipeWire. Wait for the link, activate, THEN wait for the accepted
	// Format and STREAMING.
	bool ok = PwWait([&] { return m_pwLinked && pw_stream_get_state(m_pwStream, nullptr) == PW_STREAM_STATE_PAUSED; });
	if (ok) {
		res = pw_stream_set_active(m_pwStream, true);
		if (res < 0) PwFail("activate format negotiation", res);
		ok = PwWait([&] { return m_pwNegotiated && pw_stream_get_state(m_pwStream, nullptr) == PW_STREAM_STATE_STREAMING; });
	}
	LOG_INFO("PipeWire: start result=%d negotiated=%d linked=%d state=%s", ok, m_pwNegotiated, m_pwLinked,
	         pw_stream_state_as_string(pw_stream_get_state(m_pwStream, nullptr)));
	if (!ok) return false;

	// Neutralise any remembered per-application volume. The server keeps a
	// volume memory keyed by application name, and a stale entry has applied a
	// stereo-era volume to a multichannel stream: front L/R at 100% and
	// rears/center/LFE at 0% (photographed in pactl output on the Steam
	// Machine) - muted at the stream level, upstream of the sink, where nothing
	// we mix could ever help. That history is why the app is named "AAE
	// Emulator" and not "AAE"; forcing unity and unmute here covers the case
	// where the new name is remembered wrongly too.
	float unity[kMaxDevChannels];
	std::fill(std::begin(unity), std::end(unity), 1.0f);
	float mute = 0.0f;
	const int gainResult = pw_stream_set_control(m_pwStream, SPA_PROP_channelVolumes, m_devChannels, unity, 0);
	const int muteResult = pw_stream_set_control(m_pwStream, SPA_PROP_mute, 1, &mute, 0);
	LOG_INFO("PipeWire: stream unity volume result=%d, unmute result=%d", gainResult, muteResult);

	// Latency as the SERVER reports it; nothing in this backend advances on
	// wall-clock time. The figure can still settle over the first few cycles.
	pw_time t{};
	if (pw_stream_get_time_n(m_pwStream, &t, sizeof(t)) >= 0 && t.rate.denom)
		LOG_INFO("PipeWire: server-reported latency %.1f ms (delay %lld ticks at %u/%u)",
		         1000.0 * (double)t.delay * (double)t.rate.num / (double)t.rate.denom,
		         (long long)t.delay, t.rate.num, t.rate.denom);
	else
		LOG_INFO("PipeWire: server latency not reported yet");
	return true;
}

//------------------------------------------------------------------------------
// InitPipeWire - sink discovery, layout choice, stream bring-up.
//------------------------------------------------------------------------------
bool LinuxAudioBackend::InitPipeWire(int f)
{
	m_pwPcm = spa_audio_info_raw{};
	m_pwPcm.format      = SPA_AUDIO_FORMAT_S16_LE;
	m_pwPcm.rate        = 48000;
	m_pwPcm.channels    = 2;
	m_pwPcm.position[0] = SPA_AUDIO_CHANNEL_FL;
	m_pwPcm.position[1] = SPA_AUDIO_CHANNEL_FR;
	m_pwPcmMask = 3;
	m_pwLayoutSource = "none";
	m_pwHaveLayout = m_pwRequestedActiveFormat = false;
	m_pwFailed = m_pwNegotiated = m_pwLinked = m_pwUp = m_pwFirstProcessLogged = false;
	m_pwPendingSync = m_pwDoneSync = -1;
	m_pwLogs = 0;
	m_pwCallbacks = m_pwFrames = m_pwSilent = m_pwBadBuffers = 0;
	m_pwWasUsed = false;

	if (!PwConnectServer()) return false;

	// Everything from here runs under the loop lock; the waits release it while
	// they sleep, which is when the callbacks run.
	PwLock guard(m_pwLoop);
	// Registry discovery may bind metadata while the first roundtrip runs.
	if (!PwSync() || !PwSync()) return false;
	if (!PwSelectSink()) return false;

	// Stream layout. speakers=2 is the explicit user override and always means
	// FL FR. Anything else (0 = default, legacy 6) opens EXACTLY the layout the
	// sink reported - the way the video player does; nothing usable parsed
	// falls back to stereo. The rate is the sink's own when sane, so the
	// server never has to resample; mixer_init refits to OutputRate().
	const int speakerCfg = mixer_get_speaker_config();
	const bool followSink = (speakerCfg != 2) && m_pwHaveLayout;
	const unsigned rate = (m_pwPcm.rate >= 8000 && m_pwPcm.rate <= 192000) ? m_pwPcm.rate : 48000u;

	m_pwStreamFmt = spa_audio_info_raw{};
	m_pwStreamFmt.format = SPA_AUDIO_FORMAT_S16_LE;
	m_pwStreamFmt.flags  = SPA_AUDIO_FLAG_NONE;
	m_pwStreamFmt.rate   = rate;
	if (followSink) {
		m_pwStreamFmt.channels = m_pwPcm.channels;
		for (uint32_t c = 0; c < m_pwPcm.channels; ++c) m_pwStreamFmt.position[c] = m_pwPcm.position[c];
	} else {
		m_pwStreamFmt.channels = 2;
		m_pwStreamFmt.position[0] = SPA_AUDIO_CHANNEL_FL;
		m_pwStreamFmt.position[1] = SPA_AUDIO_CHANNEL_FR;
	}

	uint32_t bits[kMaxDevChannels] = {};
	for (uint32_t c = 0; c < m_pwStreamFmt.channels; ++c) bits[c] = PwSpeakerBit(m_pwStreamFmt.position[c]);
	SetLayout(bits, m_pwStreamFmt.channels);

	m_backendName       = "PipeWire";
	m_rate              = (int)rate;
	m_fps               = f;
	m_period            = (snd_pcm_uframes_t)(rate / (unsigned)f);
	m_frames_per_update = (int)m_period;
	m_maxRequested      = 0;
	// 4096 frames (~85 ms at 48 kHz) per chunk: far above any quantum the server
	// asks for, and a bigger pull is simply produced in several chunks.
	DecideHdmiStereo(PwClean(m_pwTargetName.c_str()).c_str(), PwClean(m_pwTargetDesc.c_str()).c_str(), true);
	SetupBuffers(rate, 4096);

	LOG_INFO("PipeWire: opening %u-channel S16LE stream [%s] at %u Hz (speakers=%d%s), "
	         "period %lu frames (~%.1f ms), node.latency %u/%u",
	         m_pwStreamFmt.channels, PwLayoutNames(m_pwStreamFmt.position, m_pwStreamFmt.channels).c_str(),
	         rate, speakerCfg,
	         speakerCfg == 2 ? ", forced stereo" : (followSink ? ", following the sink" : ", sink layout unknown"),
	         (unsigned long)m_period, 1000.0 * (double)m_period / (double)rate,
	         rate / 100, rate);
	LogLayout();

	if (!PwStartStream()) return false;

	m_pwUp      = true;
	m_pwWasUsed = true;
	m_active    = true;
	return true;
}

// Idempotent. The teardown order is the video player's destructor order: stream
// first (under the lock, so no callback is mid-flight), stop the loop, then the
// proxies, the core, the context and finally the loop itself.
void LinuxAudioBackend::ShutdownPipeWire()
{
	if (m_pwLoop && m_pwStarted) {
		PwLock guard(m_pwLoop);
		PwCloseStream();
	}
	if (m_pwLoop && m_pwStarted) pw_thread_loop_stop(m_pwLoop);
	m_pwStarted = false;
	PwCloseStream();   // a loop that never started has no lock to take

	if (m_pwSink) {
		spa_hook_remove(&m_pwSinkHook);
		pw_proxy_destroy(reinterpret_cast<pw_proxy*>(m_pwSink));
		m_pwSink = nullptr;
	}
	if (m_pwMetadata) {
		spa_hook_remove(&m_pwMetadataHook);
		pw_proxy_destroy(reinterpret_cast<pw_proxy*>(m_pwMetadata));
		m_pwMetadata = nullptr;
	}
	if (m_pwRegistry) {
		spa_hook_remove(&m_pwRegistryHook);
		pw_proxy_destroy(reinterpret_cast<pw_proxy*>(m_pwRegistry));
		m_pwRegistry = nullptr;
	}
	if (m_pwCore) {
		spa_hook_remove(&m_pwCoreHook);
		pw_core_disconnect(m_pwCore);
		m_pwCore = nullptr;
	}
	if (m_pwContext) { pw_context_destroy(m_pwContext); m_pwContext = nullptr; }
	if (m_pwLoop)    { pw_thread_loop_destroy(m_pwLoop); m_pwLoop = nullptr; }

	m_pwSinks.clear();
	m_pwUp = m_pwFailed = false;
}
#endif // AAE_HAVE_PIPEWIRE

//------------------------------------------------------------------------------
// Device setup / teardown
//------------------------------------------------------------------------------
bool LinuxAudioBackend::Init(int rateHz, int fps)
{
	const int rate = (rateHz > 0 ? rateHz : 44100);
	const int f    = (fps > 0 ? fps : 60);

#ifdef AAE_HAVE_PIPEWIRE
	// Server first: native PipeWire when one is reachable (each failure site
	// inside InitPipeWire has already logged its own reason). Raw ALSA below
	// only runs when there is no usable server - Pi consoles, bare cabinets.
	if (InitPipeWire(f))
		return true;
	// Full teardown so ALSA starts from a clean slate, whatever was half-built.
	ShutdownPipeWire();
	LOG_INFO("Audio: PipeWire backend unavailable - using the raw ALSA path");
#endif

	return InitAlsa((unsigned int)rate, f);
}

//------------------------------------------------------------------------------
// InitAlsa - the raw ALSA path: the no-server fallback.
//------------------------------------------------------------------------------
bool LinuxAudioBackend::InitAlsa(unsigned int rate, int f)
{
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

	// Channel count per [main] speakers: stereo only when speakers=2, otherwise
	// 6 when the device offers it. m_channels (the APP side - voice mixer,
	// streaming ring, Submit contract) stays 2 regardless; a surround device
	// just means ProduceDeviceFrames upmixes at the writei boundary (see Upmix).
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

	m_backendName        = "ALSA";
	m_rate               = (int)rate;
	m_fps                = f;
	m_frames_per_update  = (int)m_period;

	// Channel layout. Default interleave order: FL FR RL RR FC LFE - the
	// convention shared by raw ALSA's surround51 device and the PulseAudio/
	// PipeWire default 5.1 map. It is corrected from the device's channel map
	// where one exists (raw hw devices report one; compat shims usually return
	// nothing). Getting this wrong puts dialogue in a rear speaker, so log what
	// was used.
	uint32_t bits[kMaxDevChannels] = { kSpkFL, kSpkFR, kSpkBL, kSpkBR, kSpkFC, kSpkLFE };
	const char* mapSrc = "assumed (FL FR RL RR FC LFE)";
	if (m_devChannels > 2) {
		if (snd_pcm_chmap_t* map = snd_pcm_get_chmap(m_pcm)) {
			if (map->channels == m_devChannels && map->channels <= (unsigned)kMaxDevChannels) {
				uint32_t fromMap[kMaxDevChannels] = {};
				bool usable = true;
				for (unsigned int i = 0; i < map->channels; ++i) {
					fromMap[i] = SpeakerBitOfChmap(map->pos[i]);
					if (!fromMap[i]) usable = false;   // a position we have no role for
				}
				if (usable) {
					std::copy(fromMap, fromMap + map->channels, bits);
					mapSrc = "queried from device";
				}
			}
			free(map);
		}
	}
	SetLayout(bits, m_devChannels);

	m_devBuf.assign((size_t)m_period * m_devChannels, 0);
	m_maxRequested = (uint32_t)m_period;   // the feed thread always pulls exactly one period
	DecideHdmiStereo(nullptr, nullptr, false);   // also resets a value a failed PipeWire attempt left
	SetupBuffers(rate, (uint32_t)m_period);

	if (!m_stereoDirect)
		LOG_INFO("ALSA: %u-channel device, channel order %s", m_devChannels, mapSrc);
	LogLayout();

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
	m_active  = true;
	m_thread  = std::thread(&LinuxAudioBackend::FeedThread, this);
	return true;
}

void LinuxAudioBackend::Shutdown()
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

#ifdef AAE_HAVE_PIPEWIRE
	// Stops the process callback for good, so the counters below are final.
	ShutdownPipeWire();
#endif

	// Once per successful Init, not once per Shutdown call (the destructor
	// calls this again). ALWAYS reported, including - especially - when the
	// counts are zero. Logging only on non-zero counts made "Submit was never
	// called" look identical to "everything worked", which is precisely the
	// question that needed answering.
	if (m_active) {
		m_active = false;

		LOG_INFO("%s streaming totals: %llu submits, %llu samples, peak %d, "
		         "starved %llu, dropped %llu, reprimes %llu",
		         m_backendName,
		         (unsigned long long)m_streamSubmits,
		         (unsigned long long)m_streamSamples,
		         m_streamPeak,
		         (unsigned long long)m_streamStarved,
		         (unsigned long long)m_streamDropped,
		         (unsigned long long)m_streamReprimes);

#ifdef AAE_HAVE_PIPEWIRE
		if (m_pwWasUsed)
			LOG_INFO("PipeWire process totals: %llu callbacks, %llu frames produced, "
			         "%llu silent (not negotiated or failed), %llu unusable buffers, "
			         "largest pull %u frames, underruns (ring short while playing) %llu",
			         (unsigned long long)m_pwCallbacks,
			         (unsigned long long)m_pwFrames,
			         (unsigned long long)m_pwSilent,
			         (unsigned long long)m_pwBadBuffers,
			         m_maxRequested,
			         (unsigned long long)m_streamStarved);
#endif

		if (m_streamSubmits == 0) {
			LOG_ERROR("%s streaming: Submit() was NEVER called - the emulated "
			          "sound chips never reached the backend. The fault is upstream "
			          "of this file (mixer_update -> audio_thread_func -> "
			          "mixer_update_internal), not in the audio backend code.",
			          m_backendName);
		} else if (m_streamPeak == 0) {
			// INFO, not ERROR: all-zero streaming is NORMAL for the GUI frontend
			// (its only audio is WAV voices, which ride the voice path). It is a
			// fault only if a GAME session shows it - chip audio would be missing.
			LOG_INFO("%s streaming: %llu submits, every sample zero. Normal for "
			         "the GUI; in a game session this would mean the chip-audio "
			         "path is silent upstream of the backend.",
			         m_backendName, (unsigned long long)m_streamSubmits);
		}
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

LinuxAudioBackend::~LinuxAudioBackend()
{
	Shutdown();
}

//------------------------------------------------------------------------------
// The ALSA feed thread: make one period, hand it to the device, repeat.
//------------------------------------------------------------------------------
void LinuxAudioBackend::FeedThread()
{
	while (m_running) {
		ProduceDeviceFrames(m_devBuf.data(), (uint32_t)m_period);

		snd_pcm_sframes_t written = snd_pcm_writei(m_pcm, m_devBuf.data(), m_period);
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
uint8_t* LinuxAudioBackend::GetNextBuffer()
{
	return m_appBuffer.data();
}

bool LinuxAudioBackend::Submit(uint8_t* buffer, uint32_t bytes)
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
		LOG_INFO("%s streaming: first Submit() received (%zu samples)", m_backendName, samples);
	}

	// Submits per second is THE number. The device consumes a period's worth
	// every ~20ms, so the game must submit ~50 times a second to keep up. A rate
	// far below that means the audio cannot be continuous no matter how the ring
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
		LOG_INFO("%s streaming: %llu submits in %.1fs = %.1f/sec (device needs "
		         "%.1f/sec), peak amplitude %d (0 = SILENCE), starved %llu, "
		         "dropped %llu, reprimes %llu",
		         m_backendName,
		         (unsigned long long)m_streamSubmits, elapsed, perSec,
		         (double)m_rate / (double)m_period,
		         m_streamPeak,
		         (unsigned long long)m_streamStarved,
		         (unsigned long long)m_streamDropped,
		         (unsigned long long)m_streamReprimes);
	}

	// Overflow means the device is consuming slower than the game is producing
	// - a paused or fast-forwarding emulator, or a stalled consumer. DROP
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
void  LinuxAudioBackend::SetMasterVolume(float linear) { m_mixer.SetMasterVolume(linear); }
float LinuxAudioBackend::GetMasterVolume() const       { return m_mixer.GetMasterVolume(); }

// Report the DEVICE geometry, matching what the XAudio2 backend reports (its
// mastering voice channel count). The app-side mix stays stereo either way -
// these exist for diagnostics and audio_3d_init, which is a null stub here.
uint32_t LinuxAudioBackend::OutputChannelCount() const { return m_devChannels; }

// SPEAKER_xxx bits built from the device's own positions, the values
// audio_3d.cpp and mixer.cpp expect (0x3 stereo, 0x3F = 5.1 FL FR FC LFE BL BR).
uint32_t LinuxAudioBackend::OutputChannelMask() const { return m_devMask; }

//------------------------------------------------------------------------------
// Voice path - all of it delegated to VoiceMixer.
//------------------------------------------------------------------------------
VoiceHandle* LinuxAudioBackend::VoiceCreate(const WaveFormat& fmt)
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

void LinuxAudioBackend::VoiceDestroy(VoiceHandle* v)
{
	if (!v) return;
	m_mixer.Destroy(v->soft);
	delete v;
}

bool LinuxAudioBackend::VoiceSubmit(VoiceHandle* v, const uint8_t* data,
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

bool LinuxAudioBackend::VoiceStart(VoiceHandle* v)
{
	SoftVoice* s = soft_of(v);
	if (!s) return false;
	const auto guard = m_mixer.LockVoices();
	s->playing = true;
	return true;
}

void LinuxAudioBackend::VoiceStop(VoiceHandle* v)
{
	if (SoftVoice* s = soft_of(v)) {
		const auto guard = m_mixer.LockVoices();
		s->playing = false;
	}
}

void LinuxAudioBackend::VoiceFlush(VoiceHandle* v)
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

void LinuxAudioBackend::VoiceExitLoop(VoiceHandle* v)
{
	// Stop looping at the END of the current pass; the tail still plays. The
	// mixer honours this by not rewinding once the cursor runs off the end.
	if (SoftVoice* s = soft_of(v)) {
		const auto guard = m_mixer.LockVoices();
		s->exitLoopReq = true;
	}
}

void LinuxAudioBackend::VoiceSetVolume(VoiceHandle* v, float gain)
{
	if (SoftVoice* s = soft_of(v)) {
		const auto guard = m_mixer.LockVoices();
		s->gain = std::clamp(gain, 0.0f, 4.0f);
	}
}

void LinuxAudioBackend::VoiceSetFrequencyRatio(VoiceHandle* v, float ratio)
{
	// Clamped to XAudio2's own documented range so a driver that pushes an
	// extreme pitch bend behaves the same on both backends.
	if (SoftVoice* s = soft_of(v)) {
		const auto guard = m_mixer.LockVoices();
		s->freqRatio = std::clamp(ratio, 0.0005f, 1024.0f);
	}
}

uint32_t LinuxAudioBackend::VoiceBuffersQueued(VoiceHandle* v)
{
	SoftVoice* s = soft_of(v);
	if (!s) return 0;
	// Read under the lock too: MixInto decrements this when a one-shot drains,
	// and mixer.cpp polls it every frame to decide whether a channel is free.
	const auto guard = m_mixer.LockVoices();
	return s->buffersQueued;
}

uint32_t LinuxAudioBackend::VoiceInputChannels(VoiceHandle* v)
{
	SoftVoice* s = soft_of(v);
	if (!s) return 0;
	const auto guard = m_mixer.LockVoices();
	return s->channels;
}

bool LinuxAudioBackend::VoiceSetOutputMatrix(VoiceHandle* /*v*/, uint32_t /*srcChannels*/,
                                             uint32_t /*dstChannels*/,
                                             const float* /*matrix*/)
{
	// Positional audio on Linux is Phase 3d (see the Phase 3b spec, 3.6/5).
	// Returning false is honest rather than pretending: mixer.cpp reads it as
	// "this backend cannot pan" and falls back to its own gain path, instead
	// of believing a matrix was applied when nothing happened.
	return false;
}

} // namespace

//------------------------------------------------------------------------------
// The Linux half of audio_backend.h's platform factory.
//------------------------------------------------------------------------------
std::unique_ptr<IAudioBackend> create_audio_backend()
{
	return std::make_unique<LinuxAudioBackend>();
}
