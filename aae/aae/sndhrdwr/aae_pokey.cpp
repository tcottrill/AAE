#include "aae_pokey.h"
#include "timer.h"
#include "mixer.h"
#include "sys_log.h"
#include "cpu_control.h"
#include <cstdlib>
#include <cstring>

// Gain
static constexpr int32_t POKEY_GAIN = 32767 / 11;

// Sizes follow the table periods (2^n - 1).
static uint8_t g_poly4[15];
static uint8_t g_poly5[31];
static uint8_t g_poly9[511];
static uint8_t g_poly17[131071];
static uint8_t g_rand9[511];
static uint8_t g_rand17[131071];
static bool    g_built = false;

// polynomial/RNG generation.
static void poly_init(uint8_t* poly, int size, int left, int right, int add) {
	int mask = (1 << size) - 1, x = 0;
	for (int i = 0; i < mask; ++i) { *poly++ = x & 1; x = ((x << left) + (x >> right) + add) & mask; }
}
static void rand_init(uint8_t* rng, int size, int left, int right, int add) {
	int mask = (1 << size) - 1, x = 0;
	for (int i = 0; i < mask; ++i) {
		*rng++ = (size == 17) ? (uint8_t)(x >> 6) : (uint8_t)x;
		x = ((x << left) + (x >> right) + add) & mask;
	}
}

void pokey_init_tables() {
	if (g_built) return;
	poly_init(g_poly4, 4, 3, 1, 0x04);
	poly_init(g_poly5, 5, 3, 2, 0x08);
	poly_init(g_poly9, 9, 8, 1, 0x180);
	poly_init(g_poly17, 17, 16, 1, 0x1C000);
	rand_init(g_rand9, 9, 8, 1, 0x180);
	rand_init(g_rand17, 17, 16, 1, 0x1C000);
	g_built = true;
}

const uint8_t* poly4_table() { pokey_init_tables(); return g_poly4; }
const uint8_t* poly5_table() { pokey_init_tables(); return g_poly5; }
const uint8_t* poly9_table() { pokey_init_tables(); return g_poly9; }
const uint8_t* poly17_table() { pokey_init_tables(); return g_poly17; }
const uint8_t* rand9_table() { pokey_init_tables(); return g_rand9; }
const uint8_t* rand17_table() { pokey_init_tables(); return g_rand17; }

Pokey::Pokey(PokeyHost* host) : host_(host) {}

void Pokey::reset() {
	// Div_n_max(render)=0x7FFFFFFF, Div_n_cnt=0, Outvol=0, AUDF/AUDC/AUDV=0. divisor_ is the timer-facing TRUE
	// period; seed it to base_clock_ so a timer scheduled before any write is sane.
	for (int i = 0; i < 4; ++i) {
		AUDF_[i] = AUDC_[i] = 0;
		divisor_[i] = base_clock_;
		rmax_[i] = 0x7FFFFFFF;
		cnt_[i] = 0; out_[i] = 0; vol_[i] = 0;
	}
	AUDCTL_ = 0; base_mult_ = DIV_64;
	p4_ = p5_ = p9_ = p17_ = poly_adjust_ = 0;
	samp_cnt_ = 0; samp_max_ = sys_freq_ ? ((base_clock_ << 8) / sys_freq_) : 0;
	IRQEN_ = IRQST_ = SKCTL_ = SKSTAT_ = 0;
	KBCODE_ = SERIN_ = SEROUT_ = 0; kbd_pending_ = false;
	pot_scanning_ = false;
	pot_scan_start_ = 0;
	rng_enabled_ = 0; pokey_random_ = 0;
	rand_pos9_ = rand_pos17_ = 0;
	// Seed to 0.
	last_rng_cycle_ = 0;
	rng_remainder_ = 0;
	for (int i = 0; i < 3; ++i) if (host_) host_->timer_cancel(i);
}

void    Pokey::set_clock(uint32_t hz) { base_clock_ = hz ? hz : 1; }
void    Pokey::set_sample_rate(uint32_t f) { sys_freq_ = f ? f : 1; }

void Pokey::write(uint8_t offset, uint8_t data) {
	const uint8_t a = offset & 0x0F;
	switch (a) {
	// AUDF writes rearm only the timers whose period they can change:
	// AUDF1 -> TIMR1 (+TIMR2 when ch1+2 joined), AUDF2 -> TIMR2,
	// AUDF3 -> TIMR4 only when ch3+4 joined, AUDF4 -> TIMR4.
	// AUDC writes never touch a divisor, so they must not reset timer phase.
	case W_AUDF1:
		AUDF_[0] = data;
		recompute_channel(0); update_render_channel(0);
		if (AUDCTL_ & CTL_CH12_JOIN) { recompute_channel(1); update_render_channel(1); }
		rearm_timers((AUDCTL_ & CTL_CH12_JOIN) ? 0x03 : 0x01); break;
	case W_AUDF2:
		AUDF_[1] = data; recompute_channel(1); update_render_channel(1); rearm_timers(0x02); break;
	case W_AUDF3:
		AUDF_[2] = data;
		recompute_channel(2); update_render_channel(2);
		if (AUDCTL_ & CTL_CH34_JOIN) { recompute_channel(3); update_render_channel(3); rearm_timers(0x04); }
		break;
	case W_AUDF4:
		AUDF_[3] = data; recompute_channel(3); update_render_channel(3); rearm_timers(0x04); break;

	case W_AUDC1:
		AUDC_[0] = data; vol_[0] = (data & AUDC_VOLMASK) * POKEY_GAIN;
		recompute_channel(0); update_render_channel(0); break;
	case W_AUDC2:
		AUDC_[1] = data; vol_[1] = (data & AUDC_VOLMASK) * POKEY_GAIN;
		recompute_channel(1); update_render_channel(1); break;
	case W_AUDC3:
		AUDC_[2] = data; vol_[2] = (data & AUDC_VOLMASK) * POKEY_GAIN;
		recompute_channel(2); update_render_channel(2); break;
	case W_AUDC4:
		AUDC_[3] = data; vol_[3] = (data & AUDC_VOLMASK) * POKEY_GAIN;
		recompute_channel(3); update_render_channel(3); break;

	case W_AUDCTL:
		AUDCTL_ = data;
		base_mult_ = (data & CTL_CLK15) ? DIV_15 : DIV_64;
		recompute_all();
		for (int i = 0; i < 4; ++i) update_render_channel(i);  // chan_mask = 0x0F
		rearm_timers();
		break;

	case W_STIMER:
		for (int i = 0; i < 4; ++i) { cnt_[i] = 0; out_[i] = 0; }
		rearm_timers();
		return;

	case W_IRQEN: {
		if (IRQST_ & (uint8_t)~data) IRQST_ &= data;  // clear pending bits being disabled
		const uint8_t changed = IRQEN_ ^ data;
		IRQEN_ = data;
		// MAME semantics: IRQEN only gates the timers -- pause/resume on a bit
		// change, preserving phase. Re-arming happens on AUDF/AUDCTL/STIMER.
		// The IRQ_TIMR1/2/4 bits are 1<<w by definition, so `1 << w` is both
		// the IRQEN bit and the rearm mask bit for timer w.
		if (host_) {
			for (int w = 0; w < 3; ++w) {
				const uint8_t bit = (uint8_t)(1 << w);
				if (!(changed & bit)) continue;
				if (data & bit) {
					// Resume with phase intact; if the timer was cancelled
					// (e.g. never armed), schedule a fresh one.
					if (!host_->timer_set_enabled(w, true)) rearm_timers(bit);
				}
				else {
					host_->timer_set_enabled(w, false);
				}
			}
		}
		return;
	}

	case W_SKCTL: {
		rng_enabled_ = (data & SK_INIT) != 0;
		SKCTL_ = data;
		if (!rng_enabled_) {
			pokey_random_ = 0; rand_pos9_ = rand_pos17_ = 0;
			last_rng_cycle_ = host_ ? host_->now_cpu_cycles() : 0; rng_remainder_ = 0;
		}
		return;
	}

	case W_SKREST: SKSTAT_ &= (uint8_t)~(ST_FRAME | ST_OVERRUN | ST_KBERR); return;
	case W_POTGO:
		pot_scanning_ = true;
		pot_scan_start_ = host_ ? host_->now_cpu_cycles() : 0;
		return;
	case W_SEROUT: SEROUT_ = data; if (host_) host_->serial_out(data); return;
	default: return;
	}
}

uint8_t Pokey::read_random() {
	if (!rng_enabled_) return (uint8_t)(pokey_random_ ^ 0xFF);

	const uint64_t now = host_ ? host_->now_cpu_cycles() : 0;
	const uint64_t last = last_rng_cycle_;
	last_rng_cycle_ = now;

	if (now > last) {
		const uint64_t elapsed = now - last;
		const uint64_t cpu_hz = host_ ? host_->cpu_hz() : 1;
		const uint64_t pokey_hz = base_clock_;
		const uint64_t accum = rng_remainder_ + elapsed * pokey_hz;
		const uint64_t clocks = accum / (cpu_hz ? cpu_hz : 1);
		rng_remainder_ = accum % (cpu_hz ? cpu_hz : 1);
		if (clocks > 0) {
			rand_pos9_ = (uint32_t)((rand_pos9_ + clocks) % 0x1FF);
			rand_pos17_ = (uint32_t)((rand_pos17_ + clocks) % 0x1FFFF);
			pokey_random_ = (AUDCTL_ & CTL_POLY9)
				? rand9_table()[rand_pos9_]
				: rand17_table()[rand_pos17_];
		}
	}
	return (uint8_t)(pokey_random_ ^ 0xFF);
}

uint8_t Pokey::read(uint8_t offset) {
	switch (offset & 0x0F) {
	case R_RANDOM: return read_random();
	case R_IRQST:  return (uint8_t)(IRQST_ ^ 0xFF);
	case R_SKSTAT: return SKSTAT_;
	case R_KBCODE: kbd_pending_ = false; return KBCODE_;
	case R_ALLPOT: {
		// POKEY pot scanner, time-based like the real chip: each ALLPOT bit is 1
		// while that pot line's counter is still running and 0 once its POT
		// register has latched. Arcade boards strap DIP switches to the pot pins
		// as digital levels -- an open (high) line trips on the first pot clock,
		// a grounded line never trips and only latches when the scan ends at
		// count 228. So while a scan is in progress ALLPOT reads the grounded-
		// line mask (the driver's allpot handler value); once the scan completes
		// every register is latched and ALLPOT reads 0x00.
		//
		// Asteroids Deluxe depends on the full sequence (DSTTST.MAC/DSTNMI.MAC):
		// SKCTL=7 (fast pot), POTGO, then the game-price read on the very next
		// instruction (~5us, mid-scan -> DSW), while PKYTST reads ALLPOT again
		// milliseconds later and folds it into the PERR error byte -- the source
		// comments it "S/B 0" (post-scan -> 0x00). MAME returns the handler on
		// every read, which fails that self-test; real hardware passes.
		if (pot_scanning_ && host_) {
			const uint64_t elapsed = host_->now_cpu_cycles() - pot_scan_start_;
			// Scan = 228 counts of the pot clock. Fast pot (SK_FASTPOT): pot
			// clock = base clock; slow: one count per 114-base-tick scan line.
			const uint64_t need_pokey = (SKCTL_ & SK_FASTPOT) ? 228 : 228 * (uint64_t)DIV_15;
			const uint64_t cpu_hz = host_->cpu_hz() ? host_->cpu_hz() : 1;
			const uint64_t elapsed_pokey = elapsed * base_clock_ / cpu_hz;
			if (elapsed_pokey >= need_pokey) pot_scanning_ = false;
		}
		if (!pot_scanning_) return 0x00;   // pre-POTGO or scan complete: all latched
		if (host_) {
			const int v = host_->allpot_read();
			if (v >= 0) return (uint8_t)v;   // mid-scan: grounded lines still counting
		}
		return 0xFF;   // no handler: treat all lines as still counting
	}
	case R_SERIN: SKSTAT_ &= (uint8_t)~ST_SERIN_BUSY; return SERIN_;
	default:
		if ((offset & 0x0F) <= R_POT0 + 7 && host_) return (uint8_t)host_->pot_read(offset & 0x07);
		return 0xFF;
	}
}

// Event-driven sound renderer. This is a clean-room reimplementation of the
// proven Ron-Fries Pokey_process event loop (the algorithm the spec carries
// over): it walks to the nearest of {a channel divider expiry, the next sample
// boundary}, advances the poly phases, toggles/samples each channel honoring
// NOTPOLY5 gating, PURE/POLY4/POLY9/POLY17 selection, VOL_ONLY, and the CH1/CH2
// high-pass filters, then emits one clipped int16 per sample boundary.
//
// Fixed-point note: samp_cnt_ is the sample-phase accumulator in Q8 (base-clock
// ticks << 8); samp_max_ = (base_clock << 8) / sys_freq. The whole-ticks
// remaining until the next sample is samp_cnt_ >> 8. Channel dividers
// (rmax_/cnt_) are in whole base-clock ticks. A frozen channel has
// rmax_ = cnt_ = 0x7FFFFFFF and never becomes the event minimum, so it never
// toggles -- it only contributes its (seeded) Outvol to the running sum.
void Pokey::render(int16_t* dst, int n) {
	if (!dst || n <= 0) return;
	pokey_init_tables();
	const uint8_t* poly4 = poly4_table();
	const uint8_t* poly5 = poly5_table();
	const uint8_t* poly9 = poly9_table();
	const uint8_t* poly17 = poly17_table();

	if (samp_max_ == 0) samp_max_ = sys_freq_ ? ((base_clock_ << 8) / sys_freq_) : 1;

	// Initial output summation: each channel contributes -AUDV/2, plus +AUDV if
	// its output latch (Outvol) is currently high.
	int32_t cur = 0;
	for (int c = 0; c < 4; ++c) { cur -= vol_[c] / 2; if (out_[c]) cur += vol_[c]; }

	static const int SAMPLE_EVENT = 127;
	int produced = 0;
	while (produced < n) {
		int next = SAMPLE_EVENT;
		uint32_t event_min = samp_cnt_ >> 8;   // whole ticks until next sample

		// Nearest channel-divider expiry; ties (<=) resolve to the channel.
		for (int c = 0; c < 4; ++c) {
			if (cnt_[c] <= event_min) { event_min = cnt_[c]; next = c; }
		}

		for (int c = 0; c < 4; ++c) cnt_[c] -= event_min;
		samp_cnt_ -= (event_min << 8);
		poly_adjust_ += event_min;

		if (next != SAMPLE_EVENT) {
			// Advance the poly phases by the elapsed ticks.
			p4_ = (p4_ + poly_adjust_) % 0x0000F;
			p5_ = (p5_ + poly_adjust_) % 0x0001F;
			p9_ = (p9_ + poly_adjust_) % 0x001FF;
			p17_ = (p17_ + poly_adjust_) % 0x1FFFF;
			poly_adjust_ = 0;

			cnt_[next] += rmax_[next];
			const uint8_t audc = AUDC_[next];
			uint8_t* outp = &out_[next];
			bool toggle = false;
			if (!(audc & AUDC_VOLONLY)) {
				if ((audc & AUDC_NOTPOLY5) || poly5[p5_]) {
					if (audc & AUDC_PURE)         toggle = true;
					else if (audc & AUDC_POLY4)   toggle = (poly4[p4_] == !(*outp));
					else if (AUDCTL_ & CTL_POLY9) toggle = (poly9[p9_] == !(*outp));
					else                              toggle = (poly17[p17_] == !(*outp));
				}
			}

			// High-pass filters: a ch3 transition clocks (latches) ch1; ch4 clocks ch2.
			auto suppress = [&](uint8_t filt, int trig, int tgt) {
				if ((AUDCTL_ & filt) && next == trig && out_[tgt]) {
					out_[tgt] = 0; cur -= vol_[tgt];
				}
				};
			suppress(CTL_CH1_FILTER, 2, 0);
			suppress(CTL_CH2_FILTER, 3, 1);

			if (toggle) {
				if (*outp) { cur -= vol_[next]; *outp = 0; }
				else { cur += vol_[next]; *outp = 1; }
			}
		}
		else {
			samp_cnt_ += samp_max_;
			int32_t v = cur;
			if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
			dst[produced++] = (int16_t)v;
		}
	}
}

void Pokey::keyboard_key(uint8_t code, uint8_t flags, bool down) {
	// Scanning requires SKCTL lower-2-bits != 0 (not in init/reset).
	if ((SKCTL_ & SK_INIT) == 0) return;
	if (!down) { SKSTAT_ &= (uint8_t)~ST_KEYBD; return; }

	if (kbd_pending_) SKSTAT_ |= ST_KBERR;   // prior code not yet read -> overrun
	KBCODE_ = code;
	SKSTAT_ |= ST_KEYBD;
	if (flags & ST_SHIFT) SKSTAT_ |= ST_SHIFT; else SKSTAT_ &= (uint8_t)~ST_SHIFT;
	kbd_pending_ = true;
	if (IRQEN_ & IRQ_KEYBD) fire_irq(IRQ_KEYBD);
}

void Pokey::serial_receive(uint8_t data) {
	SERIN_ = data;
	SKSTAT_ |= ST_SERIN_BUSY;
	if (IRQEN_ & IRQ_SERIN) fire_irq(IRQ_SERIN);
}

// Documented POKEY channel-divider formula. Returns the TRUE half-period in base-clock
// ticks; never frozen. Drives the timer path (divisor_).
static uint32_t channel_period(const uint8_t* AUDF, uint8_t AUDCTL, uint32_t base_mult, int ch) {
	const bool hi1 = (AUDCTL & CTL_CH1_HICLK) != 0;
	const bool hi3 = (AUDCTL & CTL_CH3_HICLK) != 0;
	uint32_t d;
	switch (ch) {
	case 0: d = hi1 ? (uint32_t)(AUDF[0] + 4) : (uint32_t)((AUDF[0] + 1) * base_mult); break;
	case 1:
		if (AUDCTL & CTL_CH12_JOIN)
			d = hi1 ? (uint32_t)(AUDF[1] * 256 + AUDF[0] + 7)
			: (uint32_t)((AUDF[1] * 256 + AUDF[0] + 1) * base_mult);
		else d = (uint32_t)((AUDF[1] + 1) * base_mult);
		break;
	case 2: d = hi3 ? (uint32_t)(AUDF[2] + 4) : (uint32_t)((AUDF[2] + 1) * base_mult); break;
	case 3:
		if (AUDCTL & CTL_CH34_JOIN)
			d = hi3 ? (uint32_t)(AUDF[3] * 256 + AUDF[2] + 7)
			: (uint32_t)((AUDF[3] * 256 + AUDF[2] + 1) * base_mult);
		else d = (uint32_t)((AUDF[3] + 1) * base_mult);
		break;
	default: return 1;
	}
	return d ? d : 1;
}

void Pokey::recompute_channel(int ch) {
	if (ch < 0 || ch >= 4) return;
	divisor_[ch] = channel_period(AUDF_, AUDCTL_, base_mult_, ch);
}

void Pokey::recompute_all() { for (int i = 0; i < 4; ++i) recompute_channel(i); }

void Pokey::update_render_channel(int ch) {
	if (ch < 0 || ch >= 4) return;

	// --- update_channel_freq(): set Div_n_max(render); clamp Div_n_cnt down ---
	const uint32_t new_val = channel_period(AUDF_, AUDCTL_, base_mult_, ch);
	if (new_val != rmax_[ch]) {
		rmax_[ch] = new_val;
		if (cnt_[ch] > new_val) cnt_[ch] = new_val;
	}

	// --- Outvol seeding + disable (freeze) ---
	const uint8_t audc = AUDC_[ch];
	const uint32_t samp_thresh = samp_max_ >> 8;
	if ((audc & AUDC_VOLONLY) || !(audc & AUDC_VOLMASK) || (rmax_[ch] < samp_thresh)) {
		out_[ch] = 1;  // Outvol = 1 (participates in the initial DC sum)

		const bool disable =
			(ch == 2 && !(AUDCTL_ & CTL_CH1_FILTER)) ||
			(ch == 3 && !(AUDCTL_ & CTL_CH2_FILTER)) ||
			(ch == 0 || ch == 1) ||
			(rmax_[ch] < samp_thresh);
		if (disable) { rmax_[ch] = cnt_[ch] = 0x7FFFFFFF; }
	}
}

// Maps timer index 0,1,2 -> AUDF channel 0,1,3 and IRQ bit.
static inline int timer_channel(int which) { return which == 2 ? 3 : which; }
static inline uint8_t timer_irq_bit(int which) {
	return which == 0 ? IRQ_TIMR1 : which == 1 ? IRQ_TIMR2 : IRQ_TIMR4;
}

void Pokey::rearm_timers(uint8_t which_mask) {
	if (!host_) return;
	for (int w = 0; w < 3; ++w) {
		if (!(which_mask & (1 << w))) continue;
		const uint8_t bit = timer_irq_bit(w);
		if (IRQEN_ & bit) {
			const int ch = timer_channel(w);
			const double period_s = (double)divisor_[ch] / (double)base_clock_;
			host_->timer_schedule(w, period_s);
		}
		else {
			host_->timer_cancel(w);
		}
	}
}

void Pokey::on_timer_fire(int which) {
	if (which < 0 || which >= 3) return;
	const uint8_t bit = timer_irq_bit(which);
	if (IRQEN_ & bit) fire_irq(bit);
}

void Pokey::fire_irq(uint8_t mask) {
	IRQST_ |= mask;
	if (host_) host_->raise_irq(mask);
}

void Pokey::scan_keyboard() {
	// Polled path: pull a code from the host if one is available.
	if ((SKCTL_ & SK_INIT) == 0 || !host_) return;
	uint8_t code = 0, flags = 0;
	if (host_->keyboard_scan(&code, &flags)) keyboard_key(code, flags, true);
}


// Forward declaration: accessor used by the timer lambda (defined after the
// anonymous namespace so it can see g_chip[]).
static Pokey* core_instance(int chip);

namespace {
	POKEYinterface* g_intf = nullptr;
	int             g_num = 0;
	int             g_buffer_len = 0;
	int             g_sample_pos = 0;
	int16_t* g_buffer[POKEY_MAX] = { nullptr, nullptr, nullptr, nullptr };
	int             g_mixer_ch[POKEY_MAX] = { -1, -1, -1, -1 };
	// AAE timer id per (chip, which); -1 = none. Must start at -1: 0 is a valid
	// timer id, and pokey_sh_stop walks all POKEY_MAX chips including unstarted ones.
	int             g_timer_id[POKEY_MAX][3] = {
		{ -1, -1, -1 }, { -1, -1, -1 }, { -1, -1, -1 }, { -1, -1, -1 } };

	// Host: bridges one Pokey core instance to the AAE engine.
	class AaePokeyHost : public PokeyHost {
	public:
		int chip = 0;

		uint32_t cpu_hz() const override {
			int c = get_active_cpu();
			if (c < 0) c = 0;
			return (uint32_t)Machine->gamedrv->cpu[c].cpu_freq;
		}

		uint64_t now_cpu_cycles() const override {
			int c = get_active_cpu();
			if (c < 0) c = 0;
			// get_exact_cyclecount() is a per-frame counter: cpu_clear_cyclecount_eof()
			// zeroes it every frame, so it sawtooths (0 -> cpf -> 0) instead of advancing
			// monotonically. The RANDOM timing math uses now-last deltas, so a once-per-
			// frame reader -- the Asteroids Deluxe self-test RNG check (PKYTST), which
			// reads RANDOM once per frame at a near-constant intra-frame offset -- would
			// otherwise see now == last every frame and the RNG would never advance,
			// returning a constant value and failing the test. Rebase onto an absolute
			// timeline: completed frames (framecounter) times cycles-per-frame, plus the
			// current intra-frame count. Intra-frame deltas are unchanged, so pot timing
			// and any sequential-read behaviour are unaffected.
			const int fps = (Machine && Machine->gamedrv && Machine->gamedrv->fps > 0)
				? Machine->gamedrv->fps : 1;
			const uint64_t cpf = (uint64_t)Machine->gamedrv->cpu[c].cpu_freq / (uint64_t)fps;
			return (uint64_t)cpu_getcurrentframe() * cpf + (uint64_t)get_exact_cyclecount(c);
		}

		void timer_schedule(int which, double period_s) override {
			if (which < 0 || which >= 3) return;
			if (g_timer_id[chip][which] >= 0 &&
				timer_is_timer_enabled(g_timer_id[chip][which])) {
				timer_remove(g_timer_id[chip][which]);
			}
			const int ch = chip, w = which;
			// PERIODIC timer (no ONE_SHOT flag) — timer_update repeats it.
			g_timer_id[chip][which] = timer_set(period_s, 0, [ch, w](int) {
				if (Pokey* p = core_instance(ch)) p->on_timer_fire(w);
				});
		}

		void timer_cancel(int which) override {
			if (which < 0 || which >= 3) return;
			if (g_timer_id[chip][which] >= 0) {
				timer_remove(g_timer_id[chip][which]);
				g_timer_id[chip][which] = -1;
			}
		}

		bool timer_set_enabled(int which, bool on) override {
			if (which < 0 || which >= 3) return false;
			if (g_timer_id[chip][which] < 0) return false;
			return timer_enable(g_timer_id[chip][which], on ? 1 : 0) != 0;
		}

		void raise_irq(uint8_t mask) override {
			if (g_intf && g_intf->interrupt_cb[chip])
				g_intf->interrupt_cb[chip]((int)mask);
		}

		int pot_read(int n) override {
			if (!g_intf) return 228;
			int (*h)(int) = nullptr;
			switch (n) {
			case 0: h = g_intf->pot0_r[chip]; break;
			case 1: h = g_intf->pot1_r[chip]; break;
			case 2: h = g_intf->pot2_r[chip]; break;
			case 3: h = g_intf->pot3_r[chip]; break;
			case 4: h = g_intf->pot4_r[chip]; break;
			case 5: h = g_intf->pot5_r[chip]; break;
			case 6: h = g_intf->pot6_r[chip]; break;
			case 7: h = g_intf->pot7_r[chip]; break;
			}
			return h ? h(n) : 228;
		}

		int allpot_read() override {
			// Drivers wire digital inputs / pot-complete status to allpot_r; return it
			// (or -1 if none, so the core uses its internal scan-status model).
			return (g_intf && g_intf->allpot_r[chip]) ? g_intf->allpot_r[chip](0) : -1;
		}

		int keyboard_scan(uint8_t* code, uint8_t* flags) override {
			if (g_intf && g_intf->keyboard_r[chip])
				return g_intf->keyboard_r[chip](code, flags);
			return 0;
		}

		int serial_in() override {
			return (g_intf && g_intf->serin_r[chip]) ? g_intf->serin_r[chip]() : -1;
		}

		void serial_out(uint8_t d) override {
			if (g_intf && g_intf->serout_w[chip]) g_intf->serout_w[chip](d);
		}
	};

	AaePokeyHost g_host[POKEY_MAX];
	// Pokey has no default ctor (requires a PokeyHost*), so store as pointers
	// allocated in pokey_sh_start and freed in pokey_sh_stop.
	Pokey* g_chip[POKEY_MAX] = { nullptr, nullptr, nullptr, nullptr };

	// Update all chips to the current cycle position within the frame buffer.
	void update_to_now() {
		if (g_num == 0) return;
		// Pass 0 so the position is scaled by the active CPU's own clock: newpos is
		// a fraction-of-frame, and ran_this_frame[] counts CPU cycles, not POKEY
		// ticks. Passing g_intf->clock breaks games where the POKEY clock differs
		// from the CPU clock (warlords: 756 kHz CPU, 1.512 MHz POKEY).
		int newpos = cpu_scale_by_cycles(g_buffer_len, 0);
		if (newpos > g_buffer_len) newpos = g_buffer_len;
		if (newpos < 0) newpos = 0;
		const int delta = newpos - g_sample_pos;
		if (delta < 10) return;
		for (int i = 0; i < g_num; ++i)
			g_chip[i]->render(g_buffer[i] + g_sample_pos, delta);
		g_sample_pos = newpos;
	}
} // anonymous namespace

// Accessor used by timer lambdas (forward-declared above the namespace;
// must be defined after g_chip[] is in scope).
static Pokey* core_instance(int chip) {
	return (chip >= 0 && chip < POKEY_MAX) ? g_chip[chip] : nullptr;
}

// ----------------------------------------------------------------------------
// Internal read/write helpers
// ----------------------------------------------------------------------------

static int read_chip(int chip, int offset) {
	if (chip >= g_num) return 0xFF;
	update_to_now();
	int val = g_chip[chip]->read((uint8_t)offset);
	return val;
}

static void write_chip(int chip, int offset, int data) {
	if (chip >= g_num) return;
	update_to_now();
	g_chip[chip]->write((uint8_t)offset, (uint8_t)data);
}

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------

int pokey_sh_start(POKEYinterface* intf) {
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
	g_sample_pos = 0;

	for (int i = 0; i < g_num; ++i) {
		for (int w = 0; w < 3; ++w) g_timer_id[i][w] = -1;

		g_buffer[i] = (int16_t*)std::malloc(sizeof(int16_t) * (size_t)g_buffer_len);
		if (!g_buffer[i]) {
			LOG_ERROR("pokey_sh_start: buffer alloc failed");
			pokey_sh_stop();
			return 1;
		}
		std::memset(g_buffer[i], 0, sizeof(int16_t) * (size_t)g_buffer_len);

		g_host[i].chip = i;
		g_chip[i] = new Pokey(&g_host[i]);
		g_chip[i]->set_clock((uint32_t)intf->clock);
		g_chip[i]->set_sample_rate((uint32_t)config.samplerate);
		g_chip[i]->reset();

		g_mixer_ch[i] = mixer_alloc_channel(MIXER_CHIP_STREAM_RANGE_LOW, MIXER_FIRST_RESERVED_CHANNEL);
		if (g_mixer_ch[i] < 0) {
			LOG_ERROR("pokey_sh_start: no free mixer channel for chip %d", i);
			pokey_sh_stop();
			return 1;
		}
		stream_start(g_mixer_ch[i], 0, 16, Machine->gamedrv->fps, false);
		sample_set_volume_mixer(g_mixer_ch[i], intf->mixing_level[i]);
	}
	return 0;
}

void pokey_sh_stop(void) {
	for (int i = 0; i < POKEY_MAX; ++i) {
		for (int w = 0; w < 3; ++w) {
			if (g_timer_id[i][w] >= 0) {
				timer_remove(g_timer_id[i][w]);
				g_timer_id[i][w] = -1;
			}
		}
		if (g_mixer_ch[i] >= 0) {
			stream_stop(g_mixer_ch[i], 0);
			g_mixer_ch[i] = -1;
		}
		if (g_chip[i]) { delete g_chip[i]; g_chip[i] = nullptr; }
		if (g_buffer[i]) { std::free(g_buffer[i]); g_buffer[i] = nullptr; }
	}
	g_intf = nullptr; g_num = 0; g_buffer_len = 0; g_sample_pos = 0;
}

void pokey_sh_update(void) {
	if (g_num == 0) return;
	const int remains = g_buffer_len - g_sample_pos;
	if (remains > 0)
		for (int i = 0; i < g_num; ++i)
			g_chip[i]->render(g_buffer[i] + g_sample_pos, remains);
	for (int i = 0; i < g_num; ++i)
		if (g_mixer_ch[i] >= 0) stream_update(g_mixer_ch[i], g_buffer[i]);
	g_sample_pos = 0;
}

// ----------------------------------------------------------------------------
// Low-level register reader 
// ----------------------------------------------------------------------------

int Read_pokey_regs(uint16_t addr, uint8_t chip) { return read_chip(chip, addr & 0x0F); }

// ----------------------------------------------------------------------------
// Per-chip accessors (used by drivers, and by the quad-pokey handlers for convenience)
// ----------------------------------------------------------------------------

int  pokey1_r(int o) { return read_chip(0, o); }
int  pokey2_r(int o) { return read_chip(1, o); }
int  pokey3_r(int o) { return read_chip(2, o); }
int  pokey4_r(int o) { return read_chip(3, o); }
void pokey1_w(int o, int d) { write_chip(0, o, d); }
void pokey2_w(int o, int d) { write_chip(1, o, d); }
void pokey3_w(int o, int d) { write_chip(2, o, d); }
void pokey4_w(int o, int d) { write_chip(3, o, d); }

int quad_pokey_r(int offset) {
	int chip = (offset >> 3) & ~0x04;
	int ctrl = (offset & 0x20) >> 2;
	int reg = (offset % 8) | ctrl;
	return read_chip(chip, reg);
}

void quad_pokey_w(int offset, int data) {
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