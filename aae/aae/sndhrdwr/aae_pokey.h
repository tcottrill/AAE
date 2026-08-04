// aae_pokey.h  -- clean-room POKEY adapter (replaces Ron-Fries aae_pokey)
// Presents the same public API as the original aae_pokey so every existing
// driver compiles unchanged.  The implementation lives in aae_pokey.cpp.
//
// Layout:
//   1. Engine-free core (PokeyHost, Pokey, poly/RNG table accessors)
//      -- compiled in ALL builds, including POKEY_TESTS.
//   2. Engine-facing adapter (includes, constants, POKEYinterface, C API)
//      -- compiled only when POKEY_TESTS is NOT defined.
#ifndef AAE_POKEY_H
#define AAE_POKEY_H

#include <cstdint>

// ---- write-register offsets (addr & 0x0F) ----
constexpr uint8_t W_AUDF1 = 0x00, W_AUDC1 = 0x01, W_AUDF2 = 0x02, W_AUDC2 = 0x03;
constexpr uint8_t W_AUDF3 = 0x04, W_AUDC3 = 0x05, W_AUDF4 = 0x06, W_AUDC4 = 0x07;
constexpr uint8_t W_AUDCTL = 0x08, W_STIMER = 0x09, W_SKREST = 0x0A, W_POTGO = 0x0B;
constexpr uint8_t W_SEROUT = 0x0D, W_IRQEN = 0x0E, W_SKCTL = 0x0F;

// ---- read-register offsets (addr & 0x0F) ----
constexpr uint8_t R_POT0 = 0x00, R_ALLPOT = 0x08, R_KBCODE = 0x09, R_RANDOM = 0x0A;
constexpr uint8_t R_SERIN = 0x0D, R_IRQST = 0x0E, R_SKSTAT = 0x0F;

// ---- AUDC bits ----
constexpr uint8_t AUDC_NOTPOLY5 = 0x80, AUDC_POLY4 = 0x40, AUDC_PURE = 0x20;
constexpr uint8_t AUDC_VOLONLY = 0x10, AUDC_VOLMASK = 0x0F;

// ---- AUDCTL bits ----
constexpr uint8_t CTL_POLY9 = 0x80, CTL_CH1_HICLK = 0x40, CTL_CH3_HICLK = 0x20;
constexpr uint8_t CTL_CH12_JOIN = 0x10, CTL_CH34_JOIN = 0x08, CTL_CH1_FILTER = 0x04;
constexpr uint8_t CTL_CH2_FILTER = 0x02, CTL_CLK15 = 0x01;

// ---- IRQEN / IRQST bits ----
constexpr uint8_t IRQ_BREAK = 0x80, IRQ_KEYBD = 0x40, IRQ_SERIN = 0x20, IRQ_SEROR = 0x10;
constexpr uint8_t IRQ_SEROC = 0x08, IRQ_TIMR4 = 0x04, IRQ_TIMR2 = 0x02, IRQ_TIMR1 = 0x01;

// ---- SKCTL bits ----
constexpr uint8_t SK_BREAKEN = 0x80, SK_BPS = 0x70, SK_TWOTONE = 0x08;
constexpr uint8_t SK_FASTPOT = 0x04, SK_INIT = 0x03;

// ---- SKSTAT bits ----
constexpr uint8_t ST_FRAME = 0x80, ST_OVERRUN = 0x40, ST_KBERR = 0x20, ST_SERIN_BUSY = 0x10;
constexpr uint8_t ST_SHIFT = 0x08, ST_KEYBD = 0x04, ST_SEROUT_ACT = 0x02;

// ---- timing divisors ----
constexpr uint32_t DIV_64 = 28, DIV_15 = 114;

// Host seam: the entire contract between the core and the world.
struct PokeyHost {
	virtual ~PokeyHost() = default;

	virtual uint32_t cpu_hz()         const = 0;   // CPU clock for cycle<->time
	virtual uint64_t now_cpu_cycles() const = 0;   // monotonic CPU cycle count

	// Arm a PERIODIC callback firing Pokey::on_timer_fire(which) every period_s
	// seconds, replacing any prior schedule for this `which`. which: 0,1,2 = TIMR1/2/4.
	virtual void timer_schedule(int which, double period_s) = 0;
	virtual void timer_cancel(int which) = 0;
	// Pause/resume the timer for `which` WITHOUT resetting its phase (MAME's
	// timer_enable on IRQEN writes). Returns false if no timer is scheduled,
	// letting the core fall back to a fresh timer_schedule.
	virtual bool timer_set_enabled(int /*which*/, bool /*on*/) { return false; }

	// IRQ event bits that just triggered (1 = fired); matches mame_pokey's
	// interrupt_cb(mask). The IRQST latch stays inside the core.
	virtual void raise_irq(uint8_t mask) = 0;

	virtual int  pot_read(int n) = 0;                            // 0..228
	// ALLPOT read: return the driver's allpot handler value (0..255) if it has one
	// — many arcade boards wire digital inputs (buttons/spinner) to the POKEY pot
	// pins and read them here — or -1 for "no handler", letting the core fall back
	// to its internal pot-scan-completion status.
	virtual int  allpot_read() { return -1; }
	virtual int  keyboard_scan(uint8_t* code, uint8_t* flags) = 0; // 1 if a code is available
	virtual int  serial_in() { return -1; }                     // -1 = no data
	virtual void serial_out(uint8_t /*data*/) {}
};

class Pokey {
public:
	explicit Pokey(PokeyHost* host);
	void reset();

	void    set_clock(uint32_t base_clock_hz);   // chip master clock (e.g. 1789790)
	void    set_sample_rate(uint32_t sys_freq);

	void    write(uint8_t offset, uint8_t data);
	uint8_t read(uint8_t offset);
	void    render(int16_t* dst, int n);

	void    on_timer_fire(int which);            // driven by the host timer
	void    keyboard_key(uint8_t code, uint8_t flags, bool down);
	void    serial_receive(uint8_t data);

	// Test-only: current divider (base-clock ticks) for channel ch (0..3).
	uint32_t divisor_of(int ch) const { return (ch >= 0 && ch < 4) ? divisor_[ch] : 0; }
	// Test-only: GAIN-scaled volume for channel ch.
	int32_t  volume_of(int ch)  const { return (ch >= 0 && ch < 4) ? vol_[ch] : 0; }
	// Debug-only: is the RNG out of reset (SKCTL init bits set)? and raw SKCTL.
	bool     dbg_rng_enabled() const { return rng_enabled_ != 0; }
	uint8_t  dbg_skctl()       const { return SKCTL_; }

private:
	PokeyHost* host_;
	uint32_t base_clock_ = 1789790;
	uint32_t sys_freq_ = 44100;

	// audio channels
	uint8_t  AUDF_[4]{}, AUDC_[4]{}, AUDCTL_ = 0;
	uint32_t base_mult_ = DIV_64;
	uint32_t divisor_[4]{};      // TRUE half-period in base-clock ticks (timer-facing)
	uint32_t rmax_[4]{};         // render Div_n_max (= divisor_, or frozen 0x7FFFFFFF)
	uint32_t cnt_[4]{};          // render countdown (Div_n_cnt)
	uint8_t  out_[4]{};          // render output level / toggle latch (Outvol)
	int32_t  vol_[4]{};          // AUDC volume * GAIN (AUDV)

	// poly render phases + sample clock
	uint32_t p4_ = 0, p5_ = 0, p9_ = 0, p17_ = 0, poly_adjust_ = 0;
	uint32_t samp_cnt_ = 0, samp_max_ = 0;

	// digital
	uint8_t  IRQEN_ = 0, IRQST_ = 0, SKCTL_ = 0, SKSTAT_ = 0;
	uint8_t  KBCODE_ = 0, SERIN_ = 0, SEROUT_ = 0;
	bool     kbd_pending_ = false;

	// pots: ALLPOT is derived from the scan window (see R_ALLPOT) -- during a
	// scan it reads the still-counting-line mask, after it 0x00.
	uint64_t pot_scan_start_ = 0;
	bool     pot_scanning_ = false;
	// Sticky: set by the first POTGO this machine ever issues. A board that
	// never runs the scanner is using the pot pins as a plain digital input
	// port, and the scan window must not gate its reads. See R_ALLPOT.
	bool     pot_scan_ever_ = false;

	// RNG (carried-over logic)
	uint8_t  rng_enabled_ = 0, pokey_random_ = 0;
	uint32_t rand_pos9_ = 0, rand_pos17_ = 0;
	uint64_t last_rng_cycle_ = 0, rng_remainder_ = 0;

	void recompute_channel(int ch);  // true period -> divisor_ (timer-facing)
	void recompute_all();
	void update_render_channel(int ch);  // maintain render rmax_/cnt_/out_ (Ron-Fries seeding)
	// Re-schedule the host IRQ timers named in which_mask (bit0=TIMR1, bit1=TIMR2,
	// bit2=TIMR4); default rearms all three. Timers not in the mask keep their phase.
	void rearm_timers(uint8_t which_mask = 0x07);
	void fire_irq(uint8_t mask);
	uint8_t read_random();
	void scan_keyboard();
};

// Test-only accessors for the shared static poly/RNG tables (built on first use
// or via pokey_init_tables()). Sizes: poly4=15, poly5=31, poly9=511,
// poly17=131071, rand9=511, rand17=131071.
void           pokey_init_tables();
const uint8_t* poly4_table();
const uint8_t* poly5_table();
const uint8_t* poly9_table();
const uint8_t* poly17_table();
const uint8_t* rand9_table();
const uint8_t* rand17_table();

// ---------------------------------------------------------------------------
// Engine-facing adapter -- excluded from test builds
// ---------------------------------------------------------------------------
#ifndef POKEY_TESTS

#include "aae_mame_driver.h"

// ---------------------------------------------------------------------------
// Chip count limits
// ---------------------------------------------------------------------------
constexpr int MAXPOKEYS = 4;
constexpr int POKEY_MAX = 4;   // alias used inside the adapter

// ---------------------------------------------------------------------------
// POKEY write-register address constants (used by drivers directly)
// ---------------------------------------------------------------------------
constexpr uint8_t AUDF1_C = 0x00;
constexpr uint8_t AUDC1_C = 0x01;
constexpr uint8_t AUDF2_C = 0x02;
constexpr uint8_t AUDC2_C = 0x03;
constexpr uint8_t AUDF3_C = 0x04;
constexpr uint8_t AUDC3_C = 0x05;
constexpr uint8_t AUDF4_C = 0x06;
constexpr uint8_t AUDC4_C = 0x07;
constexpr uint8_t AUDCTL_C = 0x08;
constexpr uint8_t STIMER_C = 0x09;
constexpr uint8_t SKREST_C = 0x0A;
constexpr uint8_t POTGO_C = 0x0B;
constexpr uint8_t SEROUT_C = 0x0D;
constexpr uint8_t IRQEN_C = 0x0E;
constexpr uint8_t SKCTL_C = 0x0F;

// POKEY read-register address constants
constexpr uint8_t POT0_C = 0x00;
constexpr uint8_t POT1_C = 0x01;
constexpr uint8_t POT2_C = 0x02;
constexpr uint8_t POT3_C = 0x03;
constexpr uint8_t POT4_C = 0x04;
constexpr uint8_t POT5_C = 0x05;
constexpr uint8_t POT6_C = 0x06;
constexpr uint8_t POT7_C = 0x07;
constexpr uint8_t ALLPOT_C = 0x08;
constexpr uint8_t KBCODE_C = 0x09;
constexpr uint8_t RANDOM_C = 0x0A;
constexpr uint8_t SERIN_C = 0x0D;
constexpr uint8_t IRQST_C = 0x0E;
constexpr uint8_t SKSTAT_C = 0x0F;

// ---------------------------------------------------------------------------
// POKEYinterface
// The legacy fields come first so existing positional brace-inits keep
// compiling; the new hooks are appended and default to null.
// ---------------------------------------------------------------------------
struct POKEYinterface {
	int num;
	int clock;
	int mixing_level[POKEY_MAX];
	int (*pot0_r[POKEY_MAX])(int);
	int (*pot1_r[POKEY_MAX])(int);
	int (*pot2_r[POKEY_MAX])(int);
	int (*pot3_r[POKEY_MAX])(int);
	int (*pot4_r[POKEY_MAX])(int);
	int (*pot5_r[POKEY_MAX])(int);
	int (*pot6_r[POKEY_MAX])(int);
	int (*pot7_r[POKEY_MAX])(int);
	int (*allpot_r[POKEY_MAX])(int);
	// --- extended (new, appended) ---
	void (*interrupt_cb[POKEY_MAX])(int mask);
	int  (*keyboard_r[POKEY_MAX])(uint8_t* code, uint8_t* flags);
	int  (*serin_r[POKEY_MAX])(void);
	void (*serout_w[POKEY_MAX])(uint8_t data);
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
int  pokey_sh_start(POKEYinterface* intf);
void pokey_sh_stop(void);
void pokey_sh_update(void);

// ---------------------------------------------------------------------------
// Low-level register access (used by missile.cpp and others)
// ---------------------------------------------------------------------------
int Read_pokey_regs(uint16_t addr, uint8_t chip);

// ---------------------------------------------------------------------------
// Per-chip read/write trampolines (int-indexed API)
// ---------------------------------------------------------------------------
int  pokey1_r(int offset); int  pokey2_r(int offset);
int  pokey3_r(int offset); int  pokey4_r(int offset);
void pokey1_w(int offset, int data); void pokey2_w(int offset, int data);
void pokey3_w(int offset, int data); void pokey4_w(int offset, int data);
int  quad_pokey_r(int offset); void quad_pokey_w(int offset, int data);

// ---------------------------------------------------------------------------
// AAE MEM callbacks (MemoryReadByte / MemoryWriteByte)
// ---------------------------------------------------------------------------
uint8_t pokey_1_r(uint32_t address, struct MemoryReadByte*);
uint8_t pokey_2_r(uint32_t address, struct MemoryReadByte*);
uint8_t pokey_3_r(uint32_t address, struct MemoryReadByte*);
uint8_t pokey_4_r(uint32_t address, struct MemoryReadByte*);
uint8_t quadpokey_r(uint32_t address, struct MemoryReadByte*);
void pokey_1_w(uint32_t address, uint8_t data, struct MemoryWriteByte*);
void pokey_2_w(uint32_t address, uint8_t data, struct MemoryWriteByte*);
void pokey_3_w(uint32_t address, uint8_t data, struct MemoryWriteByte*);
void pokey_4_w(uint32_t address, uint8_t data, struct MemoryWriteByte*);
void quadpokey_w(uint32_t address, uint8_t data, struct MemoryWriteByte*);

#endif // POKEY_TESTS

#endif // AAE_POKEY_H
