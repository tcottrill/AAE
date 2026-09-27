/* c012294.h - portable Atari C012294 (POKEY) core, plain C11.
 *
 * Provides audio, RANDOM, timers/IRQs, serial, the keyboard matrix scanner
 * (ad_pokey_set_key_matrix / ad_pokey_set_key_lines) and POT counters, with
 * IRQ, pot and serial-byte inputs through an optional ad_pokey_host
 * callback interface. Derived
 * from AAE's POKEY core; see c012294.c for attribution.
 *
 * Time model
 * ----------
 * ad_pokey_advance() advances machine time in a complete instance. Pass
 * elapsed POKEY master-clock cycles before register accesses. Reads and
 * writes consume no cycles; repeated RANDOM reads without an advance
 * return the same byte. Convert CPU cycles in the host when clocks differ.
 * Advance every chip on a board from the same machine timeline.
 *
 * Machine events are processed one clock at a time, preserving callback
 * deadlines across different advance batch sizes (runs of clocks with no
 * event are stepped together with identical results; see quiet_span()). SKCTL init changes
 * RANDOM feedback rather than stopping its clock. It resets and freezes
 * the shared slow clocks; fast timers continue to count. AUDF writes
 * affect reload values, not the active count. IRQEN gates interrupt
 * latching without restarting timers or their source clocks.
 *
 * Audio comes from the same timeline: advance generates samples from the
 * same counters (the chip's DAC, see ad_pokey_set_measured_audio) and
 * render drains completed samples (silence on underrun); audio_read returns
 * only available samples. There is no separate audio clock.
 *
 * Keep board addresses, CPU scheduling, audio devices and DIP mapping in
 * the host adapter. See docs/POKEY-PORTING.md for integration guidance
 * and current model limitations. The ad_ API prefix is retained for
 * compatibility with existing hosts.
 *
 * Original code: Ron Fries, PokeySound V2.3 (August 8, 1997).
 *
 * MAME pokey.cpp (0.286, src/devices/sound/pokey.cpp):
 *
 * license:BSD-3-Clause
 * copyright-holders:Brad Oliver, Eric Smith, Juergen Buchmueller
 *
 * POKEY chip emulator 4.9
 *
 * Based on original info found in Ron Fries' Pokey emulator,
 * with additions by Brad Oliver, Eric Smith and Juergen Buchmueller,
 * paddle (a/d conversion) details from the Atari 400/800 Hardware Manual.
 * Polynomial algorithms according to info supplied by Perry McFarlane.
 * Additional improvements from Mike Saarna's A7800 MAME fork.
 *
 * Altirra Hardware Reference Manual, 2026-01-02 Edition:
 *
 * Created by Avery Lee
 * Copyright (c) 2009-2024 Avery Lee, All Rights Reserved.
 *
 * Reconstructed POKEY schematics (PokeyReSchem-13.pdf):
 *
 * by Jorge Cwik vapi.fxatari.com
 * Version 1.3
 * The logic of these schematics was reverse engineered from the actual
 * chip die layout.
 *
 * Nick Mikstas's atari_pokey, HDL/Verilog/Pokey_Cores (poly_core.v,
 * clock_gen_core.v, freq_control.v).
 */
#ifndef AD_POKEY_H
#define AD_POKEY_H

#include <stdint.h>
#include <stdbool.h>

/* C linkage for C++ hosts; compile c012294.c as C11. */
#ifdef __cplusplus
extern "C" {
#endif

/* ---- write-register offsets (addr & 0x0F) ---- */
#define W_AUDF1   0x00
#define W_AUDC1   0x01
#define W_AUDF2   0x02
#define W_AUDC2   0x03
#define W_AUDF3   0x04
#define W_AUDC3   0x05
#define W_AUDF4   0x06
#define W_AUDC4   0x07
#define W_AUDCTL  0x08
#define W_STIMER  0x09
#define W_SKREST  0x0A
#define W_POTGO   0x0B
#define W_SEROUT  0x0D
#define W_IRQEN   0x0E
#define W_SKCTL   0x0F

/* ---- read-register offsets (addr & 0x0F) ---- */
#define R_POT0    0x00
#define R_ALLPOT  0x08
#define R_KBCODE  0x09
#define R_RANDOM  0x0A
#define R_SERIN   0x0D
#define R_IRQST   0x0E
#define R_SKSTAT  0x0F

/* ---- AUDC bits ---- */
#define AUDC_NOTPOLY5  0x80
#define AUDC_POLY4     0x40
#define AUDC_PURE      0x20
#define AUDC_VOLONLY   0x10
#define AUDC_VOLMASK   0x0F

/* ---- AUDCTL bits ---- */
#define CTL_POLY9      0x80
#define CTL_CH1_HICLK  0x40
#define CTL_CH3_HICLK  0x20
#define CTL_CH12_JOIN  0x10
#define CTL_CH34_JOIN  0x08
#define CTL_CH1_FILTER 0x04
#define CTL_CH2_FILTER 0x02
#define CTL_CLK15      0x01

/* ---- IRQEN / IRQST bits ---- */
#define IRQ_BREAK  0x80
#define IRQ_KEYBD  0x40
#define IRQ_SERIN  0x20
#define IRQ_SEROR  0x10
#define IRQ_SEROC  0x08
#define IRQ_TIMR4  0x04
#define IRQ_TIMR2  0x02
#define IRQ_TIMR1  0x01

/* ---- SKCTL bits ---- */
#define SK_BREAKEN  0x80   /* force serial output low / select zero-bit two-tone */
#define SK_SERMODE  0x70   /* serial clock select, bits 6..4 - see sdi_timer()/sdo_timer() */
#define SK_ASYNC    0x10   /* hold receive timers 3+4 until a start bit */
#define SK_TWOTONE  0x08   /* timer 1/2 resync; bit 7 overrides tone select */
#define SK_FASTPOT  0x04
#define SK_INIT     0x03   /* both clear: the chip is held (Init) */
#define SK_KEYSCAN  0x02   /* the keyboard scanner runs */
#define SK_DEBOUNCE 0x01

/* ---- SKSTAT bits, as read: each condition reads as a 0 ---- */
/* Error bits are active low on reads and remain latched until SKREST. */
#define ST_FRAME       0x80   /* serial input frame error, latched until SKREST */
#define ST_KBERR       0x40   /* keyboard overrun, latched until SKREST */
#define ST_OVERRUN     0x20   /* serial input overrun, latched until SKREST */
#define ST_SERIN_DATA  0x10   /* the serial input line itself, live (1 = mark) */
#define ST_SHIFT       0x08   /* shift key down, live */
#define ST_KEYBD       0x04   /* a key down, live */
#define ST_SERIN_BUSY  0x02   /* input shift register receiving a frame, live */
#define ST_ALWAYS_ONE  0x01

/* ---- timing divisors ---- */
#define DIV_64  28
#define DIV_15  114

/* The RANDOM shift chain, register for register as the chip has it -
 * poly_core.v of Nick Mikstas's atari_pokey, a gate-level transcription
 * of Atari's POKEY schematics: the eight flip-flops of the 9-bit
 * register (RANDOM reads their complement), the eight of the 17-bit
 * extension, the flop that delays AUDCTL's 9/17 select by a clock, and
 * the three registered NOR outputs of the 9/17 switch, whose NOR is what
 * the head of the 9-bit register takes next clock (or a zero, while the
 * SKCTL init bits are clear).  Hardware polarity throughout.  The chain
 * clocks every POKEY cycle whatever SKCTL and AUDCTL say; those two only
 * change what feeds it.  See c012294.c's "RANDOM shift chain" section. */
typedef struct ad_rng_chain {
    uint8_t l9;        /* lfsr9bit[7:0]:  shifts right, head is bit 7 */
    uint8_t l17;       /* lfsr17bit[7:0]: shifts right, fed by the XNOR
                        * of l9 bits 5 and 0 */
    uint8_t swdelay;   /* swDelay: the poly-select bit, one clock late */
    uint8_t nd;        /* norsDelayed[2:0]: bit 0 the 17-bit path (l17
                        * bit 0), bit 2 the 9-bit path (the XNOR), bit 1
                        * the one-clock blank when the select flips */
} ad_rng_chain;

#define AD_POKEY_AUDIO_CAPACITY 2048   /* power of two: the queue indexes with & (CAPACITY-1) */

/* Optional host callbacks. The core stores this pointer without copying it;
 * keep the struct and ctx valid until detached or replaced. Callbacks run
 * synchronously on the caller's thread. NULL callbacks disable host input
 * or notification; chip register access and clocking remain available. */
typedef struct ad_pokey_host {
    void *ctx;
    void (*raise_irq)(void *ctx, uint8_t mask);          /* IRQST bits that just fired */
    int  (*pot_read)(void *ctx, int n);                   /* POT0-7 count, 0..228 */
    int  (*serial_in)(void *ctx);                         /* next SERIN byte, or -1 */
    void (*serial_out)(void *ctx, uint8_t data);
} ad_pokey_host;

/* Optional pin-level interface, separate from the original host struct so
 * existing hosts need not initialize new fields. Timestamp is p->cycles.
 * Attachment immediately reports current levels. Do not reenter the core
 * from callbacks. Keep this struct alive until detached. */
typedef struct ad_pokey_io {
    void *ctx;
    void (*irq_line)(void *ctx, int asserted, uint64_t cycle);
    void (*serial_output)(void *ctx, int mark, uint64_t cycle);
} ad_pokey_io;

/* Optional clock pins, separate from ad_pokey_io for existing host initializers.
 * Attach reports both levels immediately. Keep this struct alive; callbacks
 * must not reenter POKEY. Times use the existing machine-event cycle boundary.
 * bidirectional: driven=1 means POKEY drives SIO CLOCK IN, 0 means input.
 * In input mode, level reports the host's ad_pokey_serial_clock level.
 */
typedef struct ad_pokey_clocks {
    void *ctx;
    void (*output)(void *ctx, int level, uint64_t cycle);
    void (*bidirectional)(void *ctx, int level, int driven, uint64_t cycle);
} ad_pokey_clocks;

/* Per-chip state. Use the API to modify it; rebuild hosts when replacing
 * this header because the public struct layout is not a stable ABI. */
typedef struct ad_pokey {
    /* registers */
    uint8_t  AUDF[4];
    uint8_t  AUDC[4];
    uint8_t  AUDCTL;
    uint8_t  SKCTL;

    /* clocking */
    uint32_t base_clock;     /* chip master clock, Hz (e.g. 1512000) */
    uint32_t sys_freq;       /* render sample rate, Hz (e.g. 44100) */
    uint32_t base_mult;      /* DIV_64 or DIV_15, from AUDCTL bit 0 */

    /* audio channels */
    uint32_t divisor[4];     /* true half-period in base-clock ticks; also
                               * the period each hardware timer below re-
                               * arms to (channel_period(), recompute_channel()) */
    uint8_t  out[4];         /* channel output level / toggle latch (Outvol) */

    /* The 4- and 5-bit polys as the chip has them (PokeyReSchem p.5 top
     * left; nmikstas poly_core.v): a shift register of 3 (4) stages plus a
     * registered XOR feedback bit, the head fed by NOR(Init, feedback).
     * p4: bits 0..2 = lfsr4bit[0..2], bit 3 = feedback4.
     * p5: bits 0..3 = lfsr5bit[0..3], bit 4 = feedback5.
     * The audio takes poly4 = p4 bit 0 and the poly5 gate bit = p5 bit 0
     * one clock later (the pin's output inverter; see cycle_audio_step()).
     * The poly5Out line carries the complement and the channel's gate
     * re-inverts it, so the gate opens on lfsr5bit[0] = 1.  Init shifts
     * zeros in every clock, so 0 is the held state, reached 4/5 clocks
     * after Init asserts; while held the poly5 gate is closed.  Stepped
     * by poly45_step() with the same one-clock-late Init the RANDOM chain
     * sees.  The 9/17-bit noise bit is bit 0 of that chain (rng.l9). */
    uint8_t p4, p5;

    /* RNG: the RANDOM shift chain (see ad_rng_chain), clocked by
     * ad_pokey_advance() every POKEY cycle; rng_enabled is SKCTL's init
     * bits set, i.e. the chip is not held - while it is, the chain
     * takes zeros at its head instead of the switch's output */
    uint8_t  rng_enabled;
    /* Init as the CHAIN sees it, one clock behind rng_enabled. SKCTL's register
     * updates on the same clock edge the chain steps on, so at the edge where a
     * write lands the chain still sees the OLD Init; the new value only takes
     * effect from the next clock. Measured: Altirra Hardware Reference ch.5,
     * "exiting initialization mode ... STA SKCTL + LDA RANDOM back-to-back will
     * give A=$1F" - both instructions strobe on their 4th cycle, so that is 4
     * clocks after the write. Without this lag the chip reaches $1F at 3.
     * See ad_pokey_advance().
     * [SILICON] Also measured directly on two real chips, 2026-09-20 (D21):
     * RANDOM reads $FE eight clocks after an SKCTL=0 write and $FF from the
     * ninth, which is this lag plus one zero per clock.  See chain_clock(). */
    uint8_t  rng_init_prev;
    /* AUDCTL's 9-bit select as the chain sees it, one clock late for the
     * same reason as Init: the register and the chain move on the same
     * edge, so the edge on which the write lands still uses the old
     * select.  Init and this select enter the chain the same way
     * (poly_core.v), so they get the same treatment; the Init lag is the
     * measured one, this follows from it.
     * [SILICON] No longer only inferred: measured on a real chip, 2026-09-20
     * (D21), at five flip phases, against the same model without the lag.
     * See chain_clock(). */
    uint8_t  rng_sel9_prev;
    ad_rng_chain rng;

    /* hardware timers: TIMR1/TIMR2/TIMR4, driven by channels 0/1/3
     * (tcnt index w -> channel timer_channel(w) in c012294.c).  Countdowns
     * in POKEY cycles, stepped by ad_pokey_advance() while rng_enabled
     * or while the channel runs off the fast clock (see timer_runs());
     * IRQEN, IRQST are the usual latch-and-mask pair (see fire_irq() and
     * ad_pokey_write()'s W_IRQEN case) */
    uint32_t tcnt[4];       /* TIMR1, TIMR2, TIMR4, then audio-only channel 3 */
    /* HRM 5.3: reload is three clocks after STIMER / the model's borrow,
     * one clock before IRQST. AUDF is sampled on that edge. */
    uint8_t timer_reload_delay[4];

    /* Shared slow-clock phase, in absolute POKEY cycles (0 during init).
     * Reloads preserve source phase. slow_hold_ticks retains remaining
     * source pulses across init. Current IRQST anchors are +25/+84 after
     * init release; HRM reports +24/+83. See the porting guide. */
    uint64_t slow_next_64;
    uint64_t slow_next_15;
    uint32_t slow_hold_ticks[4];

    uint8_t  IRQEN, IRQST;

    /* Each borrow reaches IRQST four clocks later. Pending interrupts
     * retain independent deadlines; see tests/review_pokey_timing.c and
     * acid800 pokey_timertiming for STIMER and repeated-period checks. */
    uint8_t  timer_irq_pending;
    uint8_t  timer_irq_delay[3]; /* independent TIMR1/TIMR2/TIMR4 deadlines */

    /* Two-tone mode resets timers 1+2 TWO CYCLES after the borrow that
     * triggered it - Altirra HRM 5.6, "Two-tone resync timing".  Counts down
     * to the reset; 0 means nothing pending.  Timers 1 and 2 can each start
     * a resync within a clock of one another and BOTH resets take place
     * (HRM 5.6, the third resync effect; Altirra keeps two events for the
     * same reason), so there is a second countdown for the one that arrives
     * while the first is still pending. */
    uint8_t  twotone_delay;
    uint8_t  twotone_delay2;
    /* the keyboard scan is one clock after the 15 kHz pulse, the clock the
     * pot counter also counts on: set on the pulse, taken on the next clock */
    uint8_t  kbd_scan_due;

    /* keyboard: the last code, and the two live conditions SKSTAT
     * shows; a keyboard overrun is a new code arriving while the
     * keyboard IRQ is still pending in IRQST (see keyboard_scan_step()) */
    uint8_t  KBCODE;
    bool     kb_down, kb_shift;

    /* Keyboard matrix scanner - the chip's own (KEY_core.v, Altirra
     * pokey.cpp).  While SKCTL bit 1 is set the chip drives K0-K5 with a
     * six-bit code that advances on every 15 kHz pulse, and on that pulse
     * reads KR1 (the matrix column selected by the code: a key is down)
     * and KR2 (the CTRL / SHIFT / BREAK line, meaningful at codes $00,
     * $10 and $30).  A two-bit state machine with a compare register
     * debounces (SKCTL bit 0): a key is taken on the second consecutive
     * scan that finds the same code down, KBCODE = code | shift<<6 |
     * ctrl<<7, the keyboard IRQ fires (an overrun if the previous one is
     * still pending), SKSTAT's key-down bit holds until two consecutive
     * scans find it up.  BREAK raises its IRQ on the KR2 edge at $30.
     *
     * The host supplies the lines either as two 64-bit masks (bit c =
     * line active while K = c) with ad_pokey_set_key_matrix(), or per
     * scan tick through the key_lines callback below - the pin-reading
     * form the hardware replacement uses.  There is no other way in:
     * every key reaches KBCODE through the scanner, as on the chip
     * (the pre-D19 event shortcut is gone). */
    uint64_t key_kr1, key_kr2;
    int    (*key_lines)(void *ctx, uint8_t code);   /* returns KR1 in bit 0, KR2 in bit 1 */
    void    *key_lines_ctx;
    uint8_t  kbd_code;       /* the scan counter: what K0-K5 show now */
    uint8_t  kbd_state;      /* 0 waiting, 1 bounce, 2 held, 3 release debounce */
    uint8_t  kbd_latch;      /* compare register */
    bool     kbd_ctrl;       /* CTRL as latched at code $00 (KBCODE bit 7) */
    bool     kbd_break;      /* BREAK line as last seen at code $30 */

    /* serial port: a byte at a time, but timed in the selected timer's
     * borrows - two per bit, twenty per frame (see c012294.c's "Serial
     * port" section).  SEROUT is the output data register; sdo_pending
     * says the shifter has not taken it yet; sdo_busy/sdo_byte/sdo_left
     * are the frame leaving the pin.  sdi_* is the frame arriving;
     * SERIN takes it at the stop bit. */
    uint8_t  SERIN, SEROUT;
    bool     sdo_pending, sdo_busy;
    /* DATA OUT while the shifter is idle.  Mark after a whole frame; but Init
     * stops the shifter without clearing it, "leaving it outputting the last
     * bit shifted out" (HRM 5.6), so an abandoned send leaves its level here. */
    bool     sdo_idle_level;
    uint8_t  sdo_byte;
    uint32_t sdo_left;
    bool     sdi_busy;
    uint8_t  sdi_byte;
    uint32_t sdi_left;

    /* The serial input LINE, which is the thing SKSTAT bit 4 actually reads.
     * Altirra HRM 5.6, "Direct input": "Bit 4 of SKSTAT directly reads the raw
     * state of the serial input line. This bypasses all of the shifting and
     * clocking logic and ignores all serial input settings, working even if
     * all clocks are stopped."  acid800's pokey_serdirect depends on exactly
     * that: it stops the serial clock (SKCTL=$03) and decodes a whole byte by
     * sampling this bit every 94 cycles.
     *
     * 1 = mark (idle high). Two things can drive it: a host that shifts its
     * own bits with ad_pokey_serial_line(), or - for hosts that only have
     * whole bytes - POKEY's own frame source below, which walks a supplied
     * byte onto the line at the receive clock so the line always tells the
     * same story as the shift register. */
    uint8_t  sdi_line_in;     /* the raw line, 1 = mark */
    uint8_t  sdi_host_line;   /* set once a host drives the line itself */
    bool     sdi_src_busy;    /* the internal byte->line source is shifting */
    uint8_t  sdi_src_byte;
    uint32_t sdi_src_left;    /* borrows left in the source's frame */
    uint8_t  sdi_shift;       /* what the receiver has sampled so far */
    bool     sdi_stop_ok;     /* the stop bit sampled high */

    /* SKSTAT's three latches, set = the condition happened; SKREST
     * clears them.  ST_FRAME | ST_OVERRUN | ST_KBERR. */
    uint8_t  st_latch;

    /* running machine time in POKEY cycles, fed only by
     * ad_pokey_advance(); the ALLPOT scan window measures against it */
    uint64_t cycles;

    /* pots: ALLPOT is derived from the scan window (see ad_pokey_read's
     * R_ALLPOT case) - during a scan it reads the still-counting-line
     * mask (the host-supplied DIP byte), after it 0x00. */
    uint8_t  allpot;          /* the DIP bank on the pot pins, host-supplied */
    bool     pot_scanning;
    bool     pot_scan_ever;   /* sticky: true from this chip's first POTGO on */
    uint64_t pot_scan_start;

    /* host wiring: NULL until ad_pokey_set_host() - see ad_pokey_host
     * above.  Not touched by ad_pokey_reset(), same as allpot: it is
     * host-owned wiring, not chip state a reset clears. */
    const ad_pokey_host *host;
    const ad_pokey_io *io;
    const ad_pokey_clocks *clocks;
    uint8_t clock_out_phase, clock_bi_phase;
    uint8_t serial_output_delay; /* timer IRQ stage to serial action: two clocks */
    uint8_t clock_out_level, clock_bi_level, clock_bi_driven;
    uint8_t irq_level, serial_level, external_clock, cassette_level;
    bool pot_counter_mode;
    uint8_t pot_count, pot_previous, pot_clear_delay, pot_finish_delay;
    uint8_t pot_latch[8];
    bool pot_transition;
    uint64_t pot_next_tick;
    /* Playback stage after the DAC, fixed point so the core has no
     * floating point in any per-clock or per-sample path and produces
     * bit-identical PCM on every compiler/CPU.  audio_dc is the DC
     * tracker's state in Q16.16 PCM units; audio_dc_alpha is (1 - decay)
     * in Q30, 0 = no DC removal; audio_gain is the playback gain in
     * Q16.16 (65536 = unity).  ad_pokey_set_measured_audio() still takes
     * doubles - they are converted once, at configuration time. */
    int64_t  audio_dc;
    uint32_t audio_dc_alpha, audio_gain;
    bool quiet_skip;          /* step event-free clock runs together (default on) */
    uint8_t highpass_latch[2];
    /* Half-clocks until the high-pass latch samples and applies the
     * channel output (see cycle_audio_step()); highpass_pending is no
     * longer used (the latch samples at expiry) and stays for layout. */
    uint8_t highpass_pending[2], highpass_delay[2];
    /* Borrow-to-output pipeline, four bits per clock: a timer's borrow at
     * clock t toggles the channel output (and samples the polys) at clock
     * t+4, the clock its IRQ reaches IRQST.  Altirra's renderer samples
     * the polys at its borrow event, which is IRQST time, and its measured
     * init offsets only line up with this core's poly streams when the
     * toggle is placed there; the core used to toggle at the borrow, four
     * clocks early (D17).  Bits [3:0] = this clock's borrows, [19:16] =
     * four clocks ago = what cycle_audio_step() delivers. */
    uint32_t audio_pipe;
    /* STIMER's output reset travels the same way: applied at the end of
     * the second clock after the write, two clocks before the toggle of a
     * borrow whose IRQ lands at write+4 (Altirra: ResetTimers at write+4
     * with no audio delay, toggles at IRQ+2). */
    uint8_t audio_reset_delay;
    uint8_t noise4_history, noise5_history, noise917_history;
    uint64_t audio_phase, audio_dropped;
    int64_t audio_area;
    /* The DAC level for the current outputs, latches and volumes, in PCM
     * units (cycle_audio_level()).  Cached: it can only change on a
     * channel borrow, a high-pass latch update, an AUDC write, STIMER or
     * reset, so audio_level_refresh() runs there and the half-clock
     * integrator just reads this. */
    int32_t audio_level;
    uint32_t audio_head, audio_count;
    int16_t audio_queue[AD_POKEY_AUDIO_CAPACITY];
} ad_pokey;

void ad_pokey_set_io(ad_pokey *p, const ad_pokey_io *io);
void ad_pokey_set_clocks(ad_pokey *p, const ad_pokey_clocks *clocks);
/* Drive the external serial clock pin after advancing to the edge time.
 * Repeating a level has no effect. Internal-clock directions ignore it. */
void ad_pokey_serial_clock(ad_pokey *p, int high);
bool ad_pokey_irq_asserted(const ad_pokey *p);
/* Opt into hardware counters. set_allpot supplies the live comparator mask
 * (1 = below threshold). The host models capacitor charging and discharge;
 * changing a pin to 0 latches its counter independently. Default is legacy
 * digital scan timeout with direct host POT reads. Select before POTGO. */
void ad_pokey_set_pot_scan(ad_pokey *p, bool enabled);
/* Drop queued PCM and the integrator state so output restarts at a fresh
 * sample boundary; the oscillators, latches and registers are untouched.
 * Queue overflow drops the oldest samples and increments a count. */
void ad_pokey_audio_clear(ad_pokey *p);
/* Cycle audio's level is the chip's DAC: HRM Appendix E.2's measured bit
 * weights and the fitted shared saturation of the summed channels, then a
 * playback stage that defaults to 20 Hz DC removal at unity gain.  This
 * call changes the playback stage only: dc_hz (>=0) is host DC removal, not
 * a motherboard model; 0 exposes the unipolar DAC signal. gain (>=0) is
 * playback gain after DC removal. Select before clocking; clears PCM state;
 * survives a chip reset. */
void ad_pokey_set_measured_audio(ad_pokey *p, double dc_hz, double gain);
/* Diagnostic: false forces every clock through the one-clock path.  The
 * default (true) steps runs of event-free clocks together with identical
 * results (c012294.c's quiet_span()); this exists to prove that live. */
void ad_pokey_set_quiet_skip(ad_pokey *p, bool enabled);
uint32_t ad_pokey_audio_available(const ad_pokey *p);
uint64_t ad_pokey_audio_overruns(const ad_pokey *p);
int ad_pokey_audio_read(ad_pokey *p, int16_t *dst, int n);

/* Initialize once per instance with positive clock and sample rates.
 * init clears all state and host wiring; reset preserves cycles, allpot,
 * host wiring and clock/sample-rate configuration. */
void    ad_pokey_init(ad_pokey *p, uint32_t clock_hz, uint32_t sample_rate);
void    ad_pokey_reset(ad_pokey *p);
void    ad_pokey_write(ad_pokey *p, uint8_t reg, uint8_t v);   /* reg 0..15 */
uint8_t ad_pokey_read(ad_pokey *p, uint8_t reg);               /* RANDOM, ALLPOT, IRQST, SKSTAT,
                                                                * KBCODE, SERIN, POT0-7; else 0xFF */
void    ad_pokey_set_allpot(ad_pokey *p, uint8_t v);           /* the DIP bank on the pot pins */
void    ad_pokey_set_host(ad_pokey *p, const ad_pokey_host *h); /* NULL = no host (default) */
void    ad_pokey_advance(ad_pokey *p, uint32_t cycles);        /* full-core machine time, POKEY cycles */
/* Split-core API: choose this pair OR ad_pokey_advance for an instance's
 * lifetime (until reset). timing_clock advances one master clock, including
 * RANDOM, timers, IRQs, keyboard, pots, serial and clock-pin callbacks, but
 * does not integrate PCM or advance audio output latches. It returns that
 * clock's borrow mask: bits 0..3 = TIMR1, TIMR2, TIMR4, audio channel 3.
 * Read registers and attach physical I/O only on the timing instance.
 * Use a separate initialized instance for audio. Deliver every returned
 * mask (including zero) and all register writes in the same cycle order.
 * No shared mutable instance, queue or thread synchronization is supplied.
 * Audio quiet spans additionally require no pending audio pipeline events;
 * a zero borrow mask alone is NOT sufficient to select audio_quiet.
 */
uint8_t ad_pokey_timing_clock(ad_pokey *p);
/* The audio half of the clock alone, for a
 * host whose timing engine (timers/IRQ/RANDOM/pots) runs elsewhere and
 * hands over each clock's borrow mask (bit w = timer w: TIMR1, TIMR2,
 * TIMR4, channel 3).  Feed this instance every register write the engine
 * sees; never call ad_pokey_advance() on it.  audio_quiet is k clocks
 * with no borrows. */
void    ad_pokey_audio_clock(ad_pokey *p, uint8_t borrow_mask);
void    ad_pokey_audio_quiet(ad_pokey *p, uint32_t k);
#ifdef AD_PROBE
extern void (*ad_pokey_probe_trace)(void *ctx, uint8_t borrows, uint32_t nclocks);
extern void *ad_pokey_probe_trace_ctx;
#endif
void    ad_pokey_render(ad_pokey *p, int16_t *dst, int n);     /* drain n mono samples at sample_rate,
                                                                * zero-filled past what advance made */
/* The matrix scanner's inputs: bit c of kr1 = a key is down at scan code
 * c; bit c of kr2 = the KR2 line is active at code c ($00 CTRL, $10
 * SHIFT, $30 BREAK).  Or a per-tick pin reader (takes precedence). */
void    ad_pokey_set_key_matrix(ad_pokey *p, uint64_t kr1, uint64_t kr2);
void    ad_pokey_set_key_lines(ad_pokey *p, int (*fn)(void *ctx, uint8_t code), void *ctx);
void    ad_pokey_serial_line(ad_pokey *p, int mark);          /* drive the raw input line: 1 = mark.
                                                                 SKSTAT bit 4 reads it whether or not
                                                                 the receiver is clocked (HRM 5.6). */
void    ad_pokey_serial_receive(ad_pokey *p, uint8_t data);    /* a start bit now: the frame takes
                                                                * ten bit periods; dropped if the
                                                                * receiver is busy or unclocked */

#ifdef AD_PROBE
/* Test-only accessors for the shared static poly/RNG tables (built once,
 * on first use).  Sizes: poly4=15, poly5=31, poly9=511, poly17=131071,
 * rand9=511, rand17=131071 - the chip's maximal-length LFSRs; see
 * probe_pokey.c's check (1). */
const uint8_t *ad_pokey_dbg_poly4(void);
const uint8_t *ad_pokey_dbg_poly5(void);
const uint8_t *ad_pokey_dbg_poly9(void);
const uint8_t *ad_pokey_dbg_poly17(void);
const uint8_t *ad_pokey_dbg_rand9(void);
const uint8_t *ad_pokey_dbg_rand17(void);
#endif

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* AD_POKEY_H */
