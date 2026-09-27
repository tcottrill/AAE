// c012294_interface.cpp -- AAE engine adapter for the c012294 POKEY core.
//
// Time model. The core charges nothing on its own: ad_pokey_advance() is
// the only thing that clocks the RANDOM chain, the hardware timers, the
// pot scan window and the serial port. This adapter feeds it by catch-up:
// before every register read and write, and once per frame from
// pokey_sh_update(), it converts the CPU cycles elapsed since the chip's
// last catch-up into POKEY clocks (pokey_timebase.h carries the fraction)
// and advances the chip by that many. Two RANDOM reads therefore see a
// delta equal to the CPU cycle distance between them -- the property the
// Tempest protection and the Asteroids Deluxe self-test depend on. See
// docs/superpowers/specs/2026-09-10-pokey-c012294-adapter-design.md.
//
// Audio is produced by the same cycle advances as the hardware counters.
// Register accesses never render ahead or pad partial samples. At frame end
// pokey_sh_update() drains completed audio into the existing mixer stream.
#include "c012294_interface.h"
#include "pokey_timebase.h"
#include "mixer.h"
#include "sys_log.h"
#include "cpu_control.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

#ifdef _MSC_VER
#pragma warning(disable : 4996)   // getenv/fopen in the diagnostic trace below
#endif

namespace {

// Diagnostic trace (AAE_POKEY_TRACE=<path> in the environment): every
// catch-up advance ("A chip clocks"), register access ("W chip reg val",
// "R chip reg val") and frame drain ("F chip got") in order, after a header
// "I chips clock samplerate buffer_len fps".  Replayed offline by the Space
// Duel tree's tests\probe_c012294_replay.c to reproduce a game's audio
// through the core without the emulator.
FILE* g_trace = nullptr;

struct ChipSlot {
	ad_pokey       chip;             // the core instance (POD, no heap)
	ad_pokey_host  host;             // callbacks below; ctx = this slot
	int            index = 0;        // chip number 0..3
	int            cpu = -1;         // CPU whose counters clock this chip; bound on first access
	bool           started = false;  // time base stamped by a first catch-up
	bool           external = false; // clocked from outside via pokey_advance_external
	bool           external_warned = false;
	uint64_t       last_cycles = 0;  // CPU cycles (monotonic) at the last catch-up
	uint64_t       remainder = 0;    // carried POKEY-clock fraction, see pokey_timebase.h
	int16_t*       buffer = nullptr; // one frame of samples for the mixer stream
	int            mixer_ch = -1;
	uint64_t       missing_samples = 0;
	int16_t        last_sample = 0;
};

POKEYinterface* g_intf = nullptr;
int             g_num = 0;
int             g_buffer_len = 0;
ChipSlot        g_slot[POKEY_MAX];

// ---------------------------------------------------------------------------
// Host seam: ad_pokey_host callbacks mapped onto the POKEYinterface handlers.
// ---------------------------------------------------------------------------
int slot_index(void* ctx) { return static_cast<ChipSlot*>(ctx)->index; }

void host_raise_irq(void* ctx, uint8_t mask)
{
	const int i = slot_index(ctx);
	if (g_intf && g_intf->interrupt_cb[i]) g_intf->interrupt_cb[i]((int)mask);
}

int host_pot_read(void* ctx, int n)
{
	const int i = slot_index(ctx);
	if (!g_intf) return 228;
	int (*h)(int) = nullptr;
	switch (n) {
	case 0: h = g_intf->pot0_r[i]; break;
	case 1: h = g_intf->pot1_r[i]; break;
	case 2: h = g_intf->pot2_r[i]; break;
	case 3: h = g_intf->pot3_r[i]; break;
	case 4: h = g_intf->pot4_r[i]; break;
	case 5: h = g_intf->pot5_r[i]; break;
	case 6: h = g_intf->pot6_r[i]; break;
	case 7: h = g_intf->pot7_r[i]; break;
	}
	return h ? h(n) : 228;
}

int host_key_lines(void* ctx, uint8_t code)
{
	const int i = slot_index(ctx);
	if (g_intf && g_intf->key_lines_r[i]) return g_intf->key_lines_r[i](code);
	return 0;
}

int host_serial_in(void* ctx)
{
	const int i = slot_index(ctx);
	return (g_intf && g_intf->serin_r[i]) ? g_intf->serin_r[i]() : -1;
}

void host_serial_out(void* ctx, uint8_t d)
{
	const int i = slot_index(ctx);
	if (g_intf && g_intf->serout_w[i]) g_intf->serout_w[i](d);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

// Monotonic CPU cycle count. get_exact_cyclecount() is a per-frame counter
// (zeroed by cpu_clear_cyclecount_eof()), so it sawtooths; rebase it onto
// completed frames times cycles-per-frame so a once-per-frame reader (the
// Asteroids Deluxe self-test reads RANDOM once per frame) still sees time
// advance. Intra-frame deltas are unchanged.
uint64_t now_cpu_cycles(int cpu, bool frame_end)
{
	const int fps = (Machine && Machine->gamedrv && Machine->gamedrv->fps > 0)
		? Machine->gamedrv->fps : 1;
	const uint64_t cpf = (uint64_t)Machine->gamedrv->cpu[cpu].cpu_freq / (uint64_t)fps;
	// cpu_run increments the frame number before run_game drains audio.
	// The per-frame cycle count is not cleared until after run_game.
	const uint64_t frame = (uint64_t)cpu_getcurrentframe();
	return (frame_end && frame ? frame - 1 : frame) * cpf +
	       (uint64_t)get_exact_cyclecount(cpu);
}

// Advance chip i to the present. Binds the chip to the active CPU on its
// first call (pokey_sh_start runs before the CPU cores exist, so the stamp
// cannot be taken there; the core is held in init mode until the first
// access, so nothing is lost by waiting). Backward time -- a machine reset
// or frame-counter rebase -- charges nothing.
void catch_up(int i, bool frame_end = false)
{
	ChipSlot& s = g_slot[i];
	if (s.external) return;
	if (s.cpu < 0) {
		s.cpu = get_active_cpu();
		if (s.cpu < 0) s.cpu = 0;
	}
	const uint64_t now = now_cpu_cycles(s.cpu, frame_end);
	if (!s.started) {
		s.started = true;
		// The chip existed (in init mode) before its first access. Account
		// for that frame's leading clocks so the first audio block is full.
		s.last_cycles = now - (uint64_t)get_exact_cyclecount(s.cpu);
		s.remainder = 0;
	}
	if (now <= s.last_cycles) {
		s.last_cycles = now;
		return;
	}
	const uint64_t elapsed = now - s.last_cycles;
	s.last_cycles = now;
	const uint64_t cpu_hz = (uint64_t)Machine->gamedrv->cpu[s.cpu].cpu_freq;
	const uint32_t clocks = pokey_clocks_elapsed(elapsed, s.chip.base_clock, cpu_hz, &s.remainder);
	if (clocks) ad_pokey_advance(&s.chip, clocks);
	if (g_trace && clocks) std::fprintf(g_trace, "A %d %u\n", i, clocks);
}

// ---------------------------------------------------------------------------
// Register access
// ---------------------------------------------------------------------------
int read_chip(int chip, int offset)
{
	if (chip >= g_num) return 0xFF;
	catch_up(chip);
	const uint8_t reg = (uint8_t)(offset & 0x0F);
	// Arcade boards strap DIP switches and buttons to the pot pins and read
	// them through ALLPOT. The driver's allpot_r handler is the live state
	// of those pins, so hand it to the core as the comparator mask and let
	// the core's scan-window model answer: the mask before any POTGO and
	// mid-scan, 0x00 once a scan has finished. Asteroids Deluxe's self-test
	// (PKYTST, "S/B 0") reads ALLPOT well after its POTGO and needs that
	// post-scan 0; returning the handler directly fails it. Chips with no
	// handler keep the 0xFF mask set in pokey_sh_start.
	if (reg == R_ALLPOT && g_intf && g_intf->allpot_r[chip])
		ad_pokey_set_allpot(&g_slot[chip].chip, (uint8_t)(g_intf->allpot_r[chip](0) & 0xFF));
	const int v = ad_pokey_read(&g_slot[chip].chip, reg);
	if (g_trace) std::fprintf(g_trace, "R %d %d %d\n", chip, reg, v);
	return v;
}

void write_chip(int chip, int offset, int data)
{
	if (chip >= g_num) return;
	catch_up(chip);
	if (g_trace) std::fprintf(g_trace, "W %d %d %d\n", chip, offset & 0x0F, data & 0xFF);
	ad_pokey_write(&g_slot[chip].chip, (uint8_t)(offset & 0x0F), (uint8_t)data);
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------

int pokey_sh_start(POKEYinterface* intf)
{
	if (!intf || intf->num < 1 || intf->num > POKEY_MAX || intf->clock <= 0) {
		LOG_ERROR("pokey_sh_start: bad interface");
		return 1;
	}
	if (!Machine || !Machine->gamedrv || Machine->gamedrv->fps <= 0 ||
		config.samplerate <= 0) {
		LOG_ERROR("pokey_sh_start: bad machine/samplerate");
		return 1;
	}

	g_intf = intf;
	g_num = intf->num;
	g_buffer_len = config.samplerate / Machine->gamedrv->fps;
	if (g_buffer_len <= 0) { g_intf = nullptr; g_num = 0; return 1; }

	if (const char* path = std::getenv("AAE_POKEY_TRACE")) {
		g_trace = std::fopen(path, "w");
		if (g_trace)
			std::fprintf(g_trace, "I %d %d %d %d %d\n", g_num, intf->clock,
			             (int)(g_buffer_len * Machine->gamedrv->fps), g_buffer_len,
			             Machine->gamedrv->fps);
		else
			LOG_ERROR("pokey_sh_start: cannot open AAE_POKEY_TRACE %s", path);
	}
	// Diagnostic: AAE_POKEY_ONECLOCK=1 forces the core through its one-clock
	// path (no quiet-clock stepping) for A/B listening runs.
	const char* oneclock = std::getenv("AAE_POKEY_ONECLOCK");
	const bool force_oneclock = oneclock && *oneclock == '1';
	if (force_oneclock) LOG_INFO("POKEY: quiet-clock stepping disabled (AAE_POKEY_ONECLOCK)");

	for (int i = 0; i < g_num; ++i) {
		ChipSlot& s = g_slot[i];
		s.index = i;
		s.cpu = -1;
		s.started = false;
		s.external = false;
		s.external_warned = false;
		s.last_cycles = 0;
		s.remainder = 0;
		s.missing_samples = 0;
		s.last_sample = 0;

		s.buffer = (int16_t*)std::malloc(sizeof(int16_t) * (size_t)g_buffer_len);
		if (!s.buffer) {
			LOG_ERROR("pokey_sh_start: buffer alloc failed");
			pokey_sh_stop();
			return 1;
		}
		std::memset(s.buffer, 0, sizeof(int16_t) * (size_t)g_buffer_len);

		// The existing mixer consumes a fixed integer block each frame.
		// Match its effective sample count rather than accumulate fractional
		// frame surplus in the chip queue (identical to config rate at 60 Hz).
		ad_pokey_init(&s.chip, (uint32_t)intf->clock,
		             (uint32_t)(g_buffer_len * Machine->gamedrv->fps));
		if (force_oneclock) ad_pokey_set_quiet_skip(&s.chip, false);
		// The chip's DAC curve is built in; this is the playback stage after it:
		// 20 Hz DC removal at unity gain. Level against samples is the user's
		// CHIP VOLUME / SAMPLE VOLUME (mixer_groups.h), not a constant here.
		ad_pokey_set_measured_audio(&s.chip, 20.0, 1.0);
		// No handler = every pot line still counting (the previous adapter's
		// answer); read_chip refreshes this from allpot_r when one exists.
		ad_pokey_set_allpot(&s.chip, 0xFF);
		s.host.ctx           = &s;
		s.host.raise_irq     = host_raise_irq;
		s.host.pot_read      = host_pot_read;
		s.host.serial_in     = host_serial_in;
		s.host.serial_out    = host_serial_out;
		ad_pokey_set_host(&s.chip, &s.host);
		// The core scans and debounces contacts; hosts supply KR1/KR2 levels.
		if (intf->key_lines_r[i]) ad_pokey_set_key_lines(&s.chip, host_key_lines, &s);

		s.mixer_ch = mixer_alloc_channel(MIXER_CHIP_STREAM_RANGE_LOW, MIXER_FIRST_RESERVED_CHANNEL);
		if (s.mixer_ch < 0) {
			LOG_ERROR("pokey_sh_start: no free mixer channel for chip %d", i);
			pokey_sh_stop();
			return 1;
		}
		stream_start(s.mixer_ch, 0, 16, Machine->gamedrv->fps, false);
		sample_set_volume_mixer(s.mixer_ch, intf->mixing_level[i]);
	}
	return 0;
}

void pokey_sh_stop(void)
{
	for (int i = 0; i < POKEY_MAX; ++i) {
		ChipSlot& s = g_slot[i];
		if (s.missing_samples || ad_pokey_audio_overruns(&s.chip))
			LOG_ERROR("POKEY %d audio: %llu missing, %llu dropped samples", i,
			          (unsigned long long)s.missing_samples,
			          (unsigned long long)ad_pokey_audio_overruns(&s.chip));
		if (s.mixer_ch >= 0) {
			stream_stop(s.mixer_ch, 0);
			s.mixer_ch = -1;
		}
		if (s.buffer) { std::free(s.buffer); s.buffer = nullptr; }
		ad_pokey_set_host(&s.chip, nullptr);
	}
	g_intf = nullptr; g_num = 0; g_buffer_len = 0;
	if (g_trace) { std::fclose(g_trace); g_trace = nullptr; }
}

void pokey_set_muted(int chip, bool muted)
{
    if (chip < 0 || chip >= g_num || !g_intf) return;
    const int channel = g_slot[chip].mixer_ch;
    if (channel >= 0)
        sample_set_volume_mixer(channel, muted ? 0 : g_intf->mixing_level[chip]);
}

void pokey_sh_update(void)
{
	if (g_num == 0) return;
	// Time passes whether or not the CPU touched the chip this frame: bring
	// every chip to the frame boundary so timers, the pot scan and the
	// serial port keep running.
	for (int i = 0; i < g_num; ++i) catch_up(i, true);
	for (int i = 0; i < g_num; ++i) {
		ChipSlot& s = g_slot[i];
		const int got = ad_pokey_audio_read(&s.chip, s.buffer, g_buffer_len);
		if (g_trace) std::fprintf(g_trace, "F %d %d\n", i, got);
		if (got) s.last_sample = s.buffer[got - 1];
		if (got < g_buffer_len) {
			if (!s.missing_samples)
				LOG_ERROR("POKEY %d audio short: %d of %d samples", i, got, g_buffer_len);
			s.missing_samples += (uint64_t)(g_buffer_len - got);
			// A reset/external-clock discontinuity must not replay stale memory.
			for (int j = got; j < g_buffer_len; ++j) s.buffer[j] = s.last_sample;
		}
		if (s.mixer_ch >= 0) stream_update(s.mixer_ch, s.buffer);
	}
}

// ----------------------------------------------------------------------------
// External clock
// ----------------------------------------------------------------------------

void pokey_set_external_clock(int chip, bool on)
{
	if (chip < 0 || chip >= POKEY_MAX) return;
	ChipSlot& s = g_slot[chip];
	s.external = on;
	s.external_warned = false;
	// Leaving external mode: re-stamp on the next catch-up rather than
	// charging the whole externally clocked span again.
	if (!on) s.started = false;
}

void pokey_advance_external(int chip, uint32_t pokey_clocks)
{
	if (chip < 0 || chip >= g_num) return;
	ChipSlot& s = g_slot[chip];
	if (!s.external) {
		if (!s.external_warned) {
			LOG_ERROR("pokey_advance_external: chip %d is not in external clock mode", chip);
			s.external_warned = true;
		}
		return;
	}
	if (pokey_clocks) ad_pokey_advance(&s.chip, pokey_clocks);
}

// ----------------------------------------------------------------------------
// Low-level register reader
// ----------------------------------------------------------------------------

int Read_pokey_regs(uint16_t addr, uint8_t chip) { return read_chip(chip, addr & 0x0F); }

// ----------------------------------------------------------------------------
// Per-chip accessors (used by drivers, and by the quad-pokey handlers)
// ----------------------------------------------------------------------------

int  pokey1_r(int o) { return read_chip(0, o); }
int  pokey2_r(int o) { return read_chip(1, o); }
int  pokey3_r(int o) { return read_chip(2, o); }
int  pokey4_r(int o) { return read_chip(3, o); }
void pokey1_w(int o, int d) { write_chip(0, o, d); }
void pokey2_w(int o, int d) { write_chip(1, o, d); }
void pokey3_w(int o, int d) { write_chip(2, o, d); }
void pokey4_w(int o, int d) { write_chip(3, o, d); }

int quad_pokey_r(int offset)
{
	int chip = (offset >> 3) & ~0x04;
	int ctrl = (offset & 0x20) >> 2;
	int reg = (offset % 8) | ctrl;
	return read_chip(chip, reg);
}

void quad_pokey_w(int offset, int data)
{
	int chip = (offset >> 3) & ~0x04;
	int ctrl = (offset & 0x20) >> 2;
	int reg = (offset % 8) | ctrl;
	write_chip(chip, reg, data);
}

// ----------------------------------------------------------------------------
// AAE MEM callbacks
// ----------------------------------------------------------------------------

uint8_t pokey_1_r(uint32_t a, struct MemoryReadByte*) { return (uint8_t)pokey1_r(a & 0x0F); }
uint8_t pokey_2_r(uint32_t a, struct MemoryReadByte*) { return (uint8_t)pokey2_r(a & 0x0F); }
uint8_t pokey_3_r(uint32_t a, struct MemoryReadByte*) { return (uint8_t)pokey3_r(a & 0x0F); }
uint8_t pokey_4_r(uint32_t a, struct MemoryReadByte*) { return (uint8_t)pokey4_r(a & 0x0F); }
uint8_t quadpokey_r(uint32_t a, struct MemoryReadByte*) { return (uint8_t)quad_pokey_r(a); }

void pokey_1_w(uint32_t a, uint8_t d, struct MemoryWriteByte*) { pokey1_w(a & 0x0F, d); }
void pokey_2_w(uint32_t a, uint8_t d, struct MemoryWriteByte*) { pokey2_w(a & 0x0F, d); }
void pokey_3_w(uint32_t a, uint8_t d, struct MemoryWriteByte*) { pokey3_w(a & 0x0F, d); }
void pokey_4_w(uint32_t a, uint8_t d, struct MemoryWriteByte*) { pokey4_w(a & 0x0F, d); }
void quadpokey_w(uint32_t a, uint8_t d, struct MemoryWriteByte*) { quad_pokey_w(a, d); }
