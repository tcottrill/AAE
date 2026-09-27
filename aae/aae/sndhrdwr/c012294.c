/* c012294.c - the POKEY core; see c012294.h for the host interface,
 * and for the time model ad_pokey_advance()/ad_pokey_read() share.
 *
 * Translated from the AAE emulator's engine-free POKEY core (Pokey and
 * PokeyHost), minus the AAE adapter (pokey_sh_*, mixer/stream/timer
 * calls, Read_pokey_regs, quad-pokey, MEM callbacks) - that layer is
 * AAE's engine wiring, not chip behaviour.  The timers/IRQ/serial/
 * keyboard/pot pieces are here, driven through ad_pokey_host in place of
 * AAE's virtual PokeyHost.  The audio poly tables and the SKCTL hold on
 * the audio side follow MAME's pokey.cpp (0.286,
 * src/devices/sound/pokey.cpp) - credit to the MAME team for the LFSR
 * arithmetic.  The pot scanner follows the Altirra Hardware Reference
 * (Avery Lee), which measured the real chip.  The RANDOM shift chain -
 * its registers, what SKCTL's init bits do to it clock by clock, and
 * which timers keep counting while it is held - follows a gate-level
 * transcription of Atari's schematics (Nick Mikstas's atari_pokey,
 * poly_core.v, clock_gen_core.v, freq_control.v) - see c012294.h's
 * ad_rng_chain.
 *
 * VERIFIED ON SILICON.  On 2026-09-20 parts of this file were checked against
 * the two real POKEYs on an Atari Space Duel board, which clocks POKEY 1:1
 * with its 6502, using a replacement boot ROM that reads RANDOM or IRQST on a
 * chosen clock after a write and shows the bytes on the vector monitor.  The
 * same ROM is executed cycle by cycle against this file and the two compared
 * (rig, ROM sources and raw readings: Space Duel Dissasembly\hwtest; write-
 * ups: docs/DECISIONS.md D21 to D23).  Comments below that say "[SILICON]"
 * mark what was measured that way:
 *   - RANDOM across Init: the one-clock Init lag, the one-bit-a-clock ramp
 *     ($FE on clock 8 after SKCTL=0, $FF from clock 9), and that a short
 *     Init hold does not clear the chain                        (chain_clock)
 *   - the 9-bit select's one-clock lag, at five flip phases     (chain_clock)
 *   - the slow clocks' first pulse after Init, 21 / 80          (SLOW64_/
 *                                                        SLOW15_FIRST_DELAY)
 *   - borrow to IRQST = 4 clocks, fast-clock period AUDF + 4
 *                                                   (TIMER_IRQ_STAGE_DELAY)
 *   - an AUDCTL write never reloads a timer: same-value rewrites, 64/15 kHz,
 *     fast/slow both ways, joining and unjoining a pair (both halves), and
 *     all of it again with Init held (2026-09-21, D23)            (W_AUDCTL)
 *   - timers 2 and 4: the first 64 kHz pulse after Init, and STIMER to
 *     IRQST on a joined pair at 1.79 MHz = AUDF + 11, pairs 1+2 and 3+4
 *     (2026-09-21; reported as matching, not read out row by row)
 * All of it is CPU-visible timing on the timers' IRQs and RANDOM, read
 * through a 6502's bus at one POKEY clock per CPU cycle.  NOT checked on
 * silicon: anything analogue (the DAC curve, the high-pass instant, poly
 * phase against the audio pin - a meter on pin 37 of the Space Duel board
 * read the rail at every volume), the keyboard scanner (K0-K5/KR1 are
 * unwired on an arcade board), pots, serial, timer 3 except through the 3+4
 * pair, and a board that runs POKEY at two clocks per CPU cycle.
 *
 * The parts taken from MAME's pokey.cpp - the exact POKEY polynomial
 * formulas - are used under that file's BSD-3-Clause terms, copyright
 * the MAME team and the copyright holders it names (Brad Oliver, Eric
 * Smith, Juergen Buchmueller, and others):
 * redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain this notice, that redistributions in binary form
 * reproduce it in the documentation, and that the names of the
 * copyright holders are not used to endorse derived products without
 * permission; the software is provided "as is" without warranty.  The
 * rest of this file is under the project's licence (see LICENSE).
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
#include <string.h>
#include <math.h>

#include "c012294.h"

static void notify_pins(ad_pokey *p);
static void notify_clocks(ad_pokey *p, bool force);
static void pot_step(ad_pokey *p);
static void cycle_audio_step(ad_pokey *p, const uint32_t *borrows);
static void cycle_audio_sample(ad_pokey *p);
static int32_t cycle_audio_level(const ad_pokey *p);
static void audio_level_refresh(ad_pokey *p);
static void cycle_audio_integrate(ad_pokey *p, int32_t level, uint32_t halves);
static void audio_queue_clear(ad_pokey *p);
static void audio_playback_config(ad_pokey *p, double dc_hz, double gain);
static void cycle_audio_noise_clock(ad_pokey *p);

/* ------------------------------------------------------------------ */
/* Poly/RNG tables - built once, on first use                          */
/* ------------------------------------------------------------------ */
/* The real chip's LFSRs, all maximal-length (2^n-1 states; probe_pokey.c
 * check (1) proves it).  poly4/5 are a Fibonacci LFSR with XNOR
 * feedback on bits 2 and size-1, seeded from 0.  poly9/17 feed bit0 XOR
 * bit5 back into bit8 (9-bit), seeded from all ones; the 17-bit case is
 * that 9-bit LFSR extended by an 8-bit shift register, folding bit8 XOR
 * bit13 back into bit7 of the low byte.  The render indexes g_polyN for
 * the audio toggle bit (`& 1`).  RANDOM does not use a table: it reads
 * the shift chain itself (the "RANDOM shift chain" section below) - the
 * g_randN slices (`& 0xff` for 9-bit, `>> 8 & 0xff` for 17-bit) are
 * built only for the probe, which checks the chain against them.
 *
 * Shared tables are initialized on first use; the audio needs under
 * 1 KiB of them (poly4/poly5 and the DAC curve), the 9/17-bit tables
 * are AD_PROBE only. Initialize chips serially before starting worker
 * threads; table initialization is not synchronized. */

#ifdef AD_PROBE
/* Probe only: the audio never indexes a poly table - it reads the
 * shift registers themselves (p4, p5, rng) - so these exist solely for
 * probe_pokey.c's checks that the registers walk the maximal-length
 * sequences. */
static uint8_t g_poly4[15];
static uint8_t g_poly5[31];
static uint8_t g_poly9[511];
static uint8_t g_poly17[131071];
static uint8_t g_rand9[511];
static uint8_t g_rand17[131071];
#endif
static bool    g_tables_built = false;
/* The DAC transfer curve, indexed by the summed channel weights (0..412,
 * units of 0.02 V), PCM units, negative-going: the chip pulls its output
 * DOWN from the pull-up as volume rises.  Built once with the polys. */
static int16_t g_dac_curve[413];

/* Fibonacci LFSR: each step folds bits 2 and (size-1) of the running
 * state through XNOR into a new bit shifted in at position 0; only the
 * table entry is masked down to `size` bits; the running `lfsr` is left
 * to grow (it never affects the tap bits, which stay at fixed low
 * offsets from the shifted-in end). */
#ifdef AD_PROBE
static void poly_init_4_5(uint8_t *poly, int size)
{
    uint32_t mask = (1u << size) - 1;
    uint32_t lfsr = 0;
    int xorbit = size - 1;
    for (uint32_t i = 0; i < mask; ++i) {
        uint32_t newbit = (~((lfsr >> 2) ^ (lfsr >> xorbit))) & 1u;
        lfsr = (lfsr << 1) | newbit;
        poly[i] = (uint8_t)((lfsr & mask) & 1u);
    }
}
#endif /* AD_PROBE */

/* Writes both slices of each state in the same pass (see the table
 * comment above): g_polyN gets the toggle bit, g_randN (probe builds
 * only; NULL otherwise) gets the RANDOM byte.  Seeded from lfsr = mask
 * (all ones): in this register's polarity that is the complement of the
 * chip's all-zero 9-bit register, so the tables line up with the chain
 * (see chain_to_vec()). */
#ifdef AD_PROBE
static void poly_init_9_17(uint8_t *poly, uint8_t *rnd, int size)
{
    uint32_t mask = (size == 17) ? 0x1FFFFu : 0x1FFu;
    uint32_t lfsr = mask;
    for (uint32_t i = 0; i < mask; ++i) {
        uint8_t byte;
        if (size == 17) {
            uint32_t in8 = ((lfsr >> 8) & 1u) ^ ((lfsr >> 13) & 1u);
            uint32_t in  = lfsr & 1u;
            lfsr >>= 1;
            lfsr = (lfsr & 0xFF7Fu) | (in8 << 7);
            lfsr = (in << 16) | lfsr;
            byte = (uint8_t)((lfsr >> 8) & 0xFFu);
        } else {
            uint32_t in = (lfsr & 1u) ^ ((lfsr >> 5) & 1u);
            lfsr >>= 1;
            lfsr = (in << 8) | lfsr;
            byte = (uint8_t)(lfsr & 0xFFu);
        }
        if (rnd)
            rnd[i] = byte;
        poly[i] = (uint8_t)(lfsr & 1u);
    }
}
#endif /* AD_PROBE */

/* ------------------------------------------------------------------ */
/* RANDOM shift chain                                                  */
/* ------------------------------------------------------------------ */
/* The chip's 9/17-bit polynomial, register for register from
 * poly_core.v (see ad_rng_chain in c012294.h for the register names).
 * Hardware polarity: the 9-bit register's bits are the complement of
 * the RANDOM byte, the XNOR of its bits 5 and 0 feeds the 17-bit
 * extension, and the head of the 9-bit register takes the NOR of the
 * three registered switch outputs - or a zero while the SKCTL init
 * bits are clear (Init).  With the 17-bit poly selected the switch
 * routes bit 0 of the extension round to the head (17 stages counting
 * the switch's own flop); with the 9-bit poly it routes the XNOR
 * straight round (9 stages), and the extension keeps shifting unseen.
 * The clock never stops: Init and the select only change the feeds.
 *
 * Holding Init shifts a zero into the head every clock.  After eight
 * the 9-bit register is clear (RANDOM 0xFF), from the ninth the XNOR of
 * two zeros feeds ones into the extension, and after seventeen the
 * whole chain is at rest: it stays there for as long as the hold lasts,
 * and a release inside those seventeen clocks resumes from whatever mix
 * of old and new bits the chain holds at that moment.  The one-clock
 * blank when the select flips (nors[1]) and the clock's delay on the
 * select (swDelay) are in here too, so flipping AUDCTL's poly bit
 * mid-run does what the chip does. */

/* One clock of the chain: the negedge always block of poly_core.v. */
static inline void chain_step(ad_rng_chain *c, bool init, bool sel9)
{
    uint32_t fb917 = (((c->l9 >> 5) ^ c->l9) & 1u) ^ 1u;        /* ~(l9[5] ^ l9[0]) */
    uint32_t nors0 = ((c->l17 & 1u) | (uint32_t)sel9) ^ 1u;     /* ~(l17[0] | sel9) */
    uint32_t nors1 = ((uint32_t)c->swdelay | (uint32_t)!sel9) ^ 1u; /* ~(swDelay | ~sel9) */
    uint32_t nors2 = ((uint32_t)!sel9 | fb917) ^ 1u;            /* ~(~sel9 | fb917) */
    uint32_t swout = ((uint32_t)init | (uint32_t)(c->nd != 0)) ^ 1u; /* ~(Init | nD[0..2]) */
    c->l9      = (uint8_t)((c->l9 >> 1) | (swout << 7));
    c->l17     = (uint8_t)((c->l17 >> 1) | (fb917 << 7));
    c->swdelay = (uint8_t)sel9;
    c->nd      = (uint8_t)(nors0 | (nors1 << 1) | (nors2 << 2));
}

/* One clock of the 4- and 5-bit polys (poly_core.v's 4/5-bit structures,
 * register for register; see p4/p5 in c012294.h).  Feedback is the XOR of
 * stages 0 and 1 (4-bit) or 0 and 2 (5-bit), registered; the head takes
 * NOR(Init, feedback).  Held (Init), zeros walk in and the register is
 * all-zero after 4/5 clocks. */
static inline void poly45_step(ad_pokey *p, bool init)
{
    const uint8_t l4 = p->p4 & 7u, fb4 = (p->p4 >> 3) & 1u;
    const uint8_t l5 = p->p5 & 15u, fb5 = (p->p5 >> 4) & 1u;
    const uint8_t in4 = (uint8_t)((fb4 | (uint8_t)init) ^ 1u);
    const uint8_t in5 = (uint8_t)((fb5 | (uint8_t)init) ^ 1u);
    const uint8_t nfb4 = (uint8_t)((l4 ^ (l4 >> 1)) & 1u);
    const uint8_t nfb5 = (uint8_t)((l5 ^ (l5 >> 2)) & 1u);
    p->p4 = (uint8_t)((l4 >> 1) | (in4 << 2) | (nfb4 << 3));
    p->p5 = (uint8_t)((l5 >> 1) | (in5 << 3) | (nfb5 << 4));
}

/* The chain as ad_pokey_reset() leaves it: at rest under a long hold
 * with the 17-bit poly selected (AUDCTL = 0), the state a chip that has
 * seen SKCTL = 0 for seventeen clocks is in. */
static void chain_reset(ad_rng_chain *c)
{
    c->l9 = 0; c->l17 = 0xFF; c->swdelay = 0; c->nd = 0;
}

/* Avery Lee, Altirra Hardware Reference Appendix E.2:
 * measured bit drops .12/.26/.56/1.12V. The exponential is a
 * hand-fitted combined-channel approximation, not resistor values.
 * https://www.virtualdub.org/downloads/Altirra%20Hardware%20Reference%20Manual.pdf
 * 4*(.12+.26+.56+1.12) = 8.24V, or 412 units of .02V. */
static void dac_curve_init(void)
{
    for (int i = 0; i <= 412; ++i) {
        double x = (double)i / 412.0;
        double y = 2.171 * (x <= 0.14 ? x :
            0.14 + (1.0 - exp(-2.85 * (x - 0.14))) / 2.85);
        if (y > 1.0) y = 1.0; /* rounded fit overshoots by ~0.000026 */
        g_dac_curve[i] = (int16_t)-(int32_t)(32767.0 * y + 0.5);
    }
}

static void build_tables(void)
{
    if (g_tables_built)
        return;
    dac_curve_init();
#ifdef AD_PROBE
    poly_init_4_5(g_poly4, 4);
    poly_init_4_5(g_poly5, 5);
    poly_init_9_17(g_poly9, g_rand9, 9);
    poly_init_9_17(g_poly17, g_rand17, 17);
#endif
    g_tables_built = true;
}

#ifdef AD_PROBE
const uint8_t *ad_pokey_dbg_poly4(void)  { build_tables(); return g_poly4;  }
const uint8_t *ad_pokey_dbg_poly5(void)  { build_tables(); return g_poly5;  }
const uint8_t *ad_pokey_dbg_poly9(void)  { build_tables(); return g_poly9;  }
const uint8_t *ad_pokey_dbg_poly17(void) { build_tables(); return g_poly17; }
const uint8_t *ad_pokey_dbg_rand9(void)  { build_tables(); return g_rand9;  }
const uint8_t *ad_pokey_dbg_rand17(void) { build_tables(); return g_rand17; }
#endif

/* ------------------------------------------------------------------ */
/* Channel period, exactly as documented in aae_pokey.cpp               */
/* ------------------------------------------------------------------ */
/* Returns the TRUE half-period in base-clock ticks. */
static uint32_t channel_period(const uint8_t *AUDF, uint8_t AUDCTL, uint32_t base_mult, int ch)
{
    bool hi1 = (AUDCTL & CTL_CH1_HICLK) != 0;
    bool hi3 = (AUDCTL & CTL_CH3_HICLK) != 0;
    uint32_t d;
    switch (ch) {
    case 0: d = hi1 ? (uint32_t)(AUDF[0] + 4) : (uint32_t)((AUDF[0] + 1) * base_mult); break;
    case 1:
        if (AUDCTL & CTL_CH12_JOIN)
            d = hi1 ? (uint32_t)(AUDF[1] * 256 + AUDF[0] + 7)
                    : (uint32_t)((AUDF[1] * 256 + AUDF[0] + 1) * base_mult);
        else
            d = (uint32_t)((AUDF[1] + 1) * base_mult);
        break;
    case 2: d = hi3 ? (uint32_t)(AUDF[2] + 4) : (uint32_t)((AUDF[2] + 1) * base_mult); break;
    case 3:
        if (AUDCTL & CTL_CH34_JOIN)
            d = hi3 ? (uint32_t)(AUDF[3] * 256 + AUDF[2] + 7)
                    : (uint32_t)((AUDF[3] * 256 + AUDF[2] + 1) * base_mult);
        else
            d = (uint32_t)((AUDF[3] + 1) * base_mult);
        break;
    default: return 1;
    }
    return d ? d : 1;
}

static void recompute_channel(ad_pokey *p, int ch)
{
    if (ch < 0 || ch >= 4)
        return;
    p->divisor[ch] = channel_period(p->AUDF, p->AUDCTL, p->base_mult, ch);
}

static void recompute_all(ad_pokey *p)
{
    for (int i = 0; i < 4; ++i)
        recompute_channel(p, i);
}

/* Hardware timer index (0,1,2 = TIMR1/TIMR2/TIMR4) -> the AUDF channel
 * that drives its period, and its IRQEN/IRQST bit.  As AAE's
 * timer_channel()/timer_irq_bit(); the IRQ_TIMR1/2/4 bits are 1<<w by
 * definition. */
static int timer_channel(int which)
{
    return which == 2 ? 3 : which == 3 ? 2 : which;
}

static uint8_t timer_irq_bit(int which)
{
    return (uint8_t)(1u << which); /* bit 3 is internal only, never an IRQ */
}

/* Shared slow-clock phase -------------------------------------------------
 *
 * The 64KHz and 15KHz clocks are common POKEY clock sources, not private
 * divide-by-N delays that restart with each timer write.  Altirra HRM 5.4:
 * their phase is set by leaving initialization mode and remains locked until
 * init is re-entered.  HRM 5.2 gives the directly observable anchor for a
 * fully reset clock with AUDF=0: IRQST asserts 24 cycles after leaving init on
 * the 64KHz clock and 83 cycles after on the 15KHz clock.  The timer IRQ
 * pipeline in this core is independently measured at four cycles. The current
 * source anchors below are 21/80, giving IRQST at 25/84 in this API. Acid800
 * 1.2 brackets reads at 83/84 (15K) and 80/81 (64K, AUDF=2). Altirra 4.40
 * independently schedules IRQST at 25/84 (22/81 source tick + 3 borrow).
 * HRM's 24/83 sentence disagrees with these observable boundaries. Preserve
 * the tested timing; see docs/POKEY-SLOW-CLOCK-EVIDENCE.md. This comparison
 * establishes API deadlines, not the physical chip's internal latch phases.
 * Subsequent pulses are exactly 28 / 114 cycles apart.
 *
 * [SILICON] 2026-09-20, D21: IRQST read on each clock after the SKCTL write
 * that leaves Init, timer 1's IRQ enabled.  64 kHz clock, AUDF1 = 2: high at
 * 80, low at 81.  15 kHz clock, AUDF1 = 0: high at 83, low at 84.  That is
 * the 21/80 anchors below (third 64 kHz pulse at 21 + 56 = 77, first 15 kHz
 * pulse at 80, each + 4 to IRQST) and the acid800 brackets quoted above, on a
 * real chip; HRM 5.2's 24/83 sentence is not what the chip does.
 *
 * Keep the timer countdown itself in machine-cycle distance (tcnt[]) because
 * the rest of this core and the serial/two-tone paths already consume it that
 * way.  The only requirement for a slow timer is that tcnt always land on a
 * shared source-clock pulse.  Rearms therefore align to slow_clock_delay(),
 * and an init hold saves the number of remaining source pulses before
 * resetting/rephasing the clocks. */
#define SLOW64_FIRST_DELAY 21u
#define SLOW15_FIRST_DELAY 80u

static bool timer_fast_clock(const ad_pokey *p, int which)
{
    switch (which) {
    case 0:  return (p->AUDCTL & CTL_CH1_HICLK) != 0;
    case 1:  return (p->AUDCTL & (CTL_CH12_JOIN | CTL_CH1_HICLK)) ==
                    (CTL_CH12_JOIN | CTL_CH1_HICLK);
    case 3: return (p->AUDCTL & CTL_CH3_HICLK) != 0;
    default: return (p->AUDCTL & (CTL_CH34_JOIN | CTL_CH3_HICLK)) ==
                    (CTL_CH34_JOIN | CTL_CH3_HICLK);
    }
}

static uint32_t slow_clock_period(const ad_pokey *p)
{
    return (p->AUDCTL & CTL_CLK15) ? DIV_15 : DIV_64;
}

static uint64_t slow_clock_next_abs(const ad_pokey *p)
{
    return (p->AUDCTL & CTL_CLK15) ? p->slow_next_15 : p->slow_next_64;
}

/* Cycles from the current machine time to the NEXT selected slow-clock
 * pulse.  slow_next_* is normally already in the future.  The normalization
 * also makes this safe inside an aggregate advance() before the stored phase
 * has been rolled forward to the end of the slice. */
static uint32_t slow_clock_delay(const ad_pokey *p)
{
    const uint32_t period = slow_clock_period(p);
    uint64_t next = slow_clock_next_abs(p);

    if (!next)
        return (p->AUDCTL & CTL_CLK15) ? SLOW15_FIRST_DELAY : SLOW64_FIRST_DELAY;
    if (next > p->cycles)
        return (uint32_t)(next - p->cycles);

    uint32_t mod = (uint32_t)((p->cycles - next) % period);
    return mod ? (period - mod) : period;
}

static uint32_t slow_divisor_ticks(const ad_pokey *p, int w)
{
    const uint32_t period = slow_clock_period(p);
    const uint32_t div = p->divisor[timer_channel(w)];
    uint32_t ticks = (div + period - 1) / period;
    return ticks ? ticks : 1;
}

/* Convert an aligned machine-cycle countdown back into the number of shared
 * source pulses still required.  This is what init must preserve: HRM 5.2
 * explicitly says the 15/64K clocks reset but the timer counters do not. */
static uint32_t slow_remaining_ticks(const ad_pokey *p, int w)
{
    const uint32_t period = slow_clock_period(p);
    const uint32_t delay = slow_clock_delay(p);
    const uint32_t count = p->tcnt[w];
    if (count <= delay)
        return 1;
    return 1 + (count - delay + period - 1) / period;
}

/* Recover the high counter in LOW-UNDERFLOW units, not source pulses.
 * The raw countdown alone includes the low counter and the carry pipeline;
 * ceil(tcnt/256) overcounts when a fast low counter is near $FF. */
static uint32_t joined_remaining_ticks(const ad_pokey *p, int hi)
{
    const int lo = hi == 1 ? 0 : 3;
    if (!timer_fast_clock(p, lo)) {
        const uint32_t ticks = p->rng_enabled ? slow_remaining_ticks(p, hi)
                                             : p->slow_hold_ticks[hi];
        return ticks ? (ticks + 255u) / 256u : 1u;
    }
    if (p->timer_reload_delay[hi]) {
        /* STIMER/high underflow has staged a full joined period. Both
         * counters sample AUDF at retirement; use that staged count here,
         * not a possibly newer AUDF write. */
        return p->tcnt[hi] >= 7 ? 1u + (p->tcnt[hi] - 7u) / 256u : 1u;
    }
    if (p->timer_reload_delay[lo]) {
        /* D23: the high counter still owes the carry until low borrow+3.
         * A gap formula here would subtract it three clocks too soon. */
        const uint32_t carry = p->timer_reload_delay[lo];
        return p->tcnt[hi] >= carry ? 1u + (p->tcnt[hi] - carry) / 256u : 1u;
    }
    const int64_t gap = (int64_t)p->tcnt[hi] - p->tcnt[lo] - 3;
    return gap >= 0 ? 1u + (uint32_t)gap / 256u : 1u;
}

static void slow_clocks_enter_init(ad_pokey *p)
{
    for (int w = 0; w < 4; ++w)
        if (!timer_fast_clock(p, w))
            p->slow_hold_ticks[w] = slow_remaining_ticks(p, w);

    /* Both polynomial source clocks are held in their reset states in init. */
    p->slow_next_64 = 0;
    p->slow_next_15 = 0;
}

static void slow_clocks_leave_init(ad_pokey *p)
{
    p->slow_next_64 = p->cycles + SLOW64_FIRST_DELAY;
    p->slow_next_15 = p->cycles + SLOW15_FIRST_DELAY;

    const uint32_t period = slow_clock_period(p);
    const uint32_t first = (p->AUDCTL & CTL_CLK15) ?
        SLOW15_FIRST_DELAY : SLOW64_FIRST_DELAY;

    for (int w = 0; w < 4; ++w) {
        if (timer_fast_clock(p, w))
            continue;
        uint32_t ticks = p->slow_hold_ticks[w];
        if (!ticks)
            ticks = slow_divisor_ticks(p, w);
        p->tcnt[w] = first + (ticks - 1) * period;
    }
}

static void slow_clocks_advance_phase(ad_pokey *p)
{
    if (!p->rng_enabled)
        return;

    if (p->slow_next_64 && p->slow_next_64 <= p->cycles) {
        uint64_t n = (p->cycles - p->slow_next_64) / DIV_64 + 1;
        p->slow_next_64 += n * DIV_64;
    }
    if (p->slow_next_15 && p->slow_next_15 <= p->cycles) {
        uint64_t n = (p->cycles - p->slow_next_15) / DIV_15 + 1;
        p->slow_next_15 += n * DIV_15;
    }
}

/* Does timer w count this slice?  Always while the chip runs.  Held
 * (SKCTL init bits clear), the 15 kHz and 64 kHz clocks stop but the
 * 1.79 MHz one does not (clock_gen_core.v holds its two clock LFSRs on
 * Init; freq_control.v's carry for channels 1 and 3 is the fast-clock
 * enable OR the slow clock), so a channel on the fast clock - and the
 * joined partner it clocks - keeps counting. */
/* Which timers count this clock, bit w = timer w (0,1,2 = TIMR1/2/4, 3 =
 * the audio-only channel 3 slot).  Evaluated once per clock.
 *
 * Running (SKCTL init bits set): all four - except that asynchronous
 * receive (SK_ASYNC, SKCTL bit 4) holds timers 3+4 (tcnt[2], tcnt[3]) in
 * reset while the receiver is idle, independent of AUDCTL's clock/join
 * bits.  Altirra HRM 5.6, "Asynchronous receive mode": "timers 3 and 4
 * are held in reset state while POKEY is waiting for a start bit,
 * allowing the timers to run only once a start bit is detected."
 * sdi_busy is that "waiting for a start bit" flag: false until a frame
 * starts, so this only applies at idle, not mid-reception.
 *
 * Held (init bits clear): the 15 kHz and 64 kHz clocks stop but the
 * 1.79 MHz one does not (clock_gen_core.v holds its two clock LFSRs on
 * Init; freq_control.v's carry for channels 1 and 3 is the fast-clock
 * enable OR the slow clock), so a channel on the fast clock - and the
 * joined partner it clocks - keeps counting. */
static uint8_t timers_running(const ad_pokey *p)
{
    if (p->rng_enabled) {
        if ((p->SKCTL & SK_ASYNC) && !p->sdi_busy)
            return 0x3;                 /* timers 3+4 held */
        return 0xF;
    }
    uint8_t m = 0;
    if (p->AUDCTL & CTL_CH1_HICLK) {
        m |= 0x1;
        if (p->AUDCTL & CTL_CH12_JOIN) m |= 0x2;
    }
    if (p->AUDCTL & CTL_CH3_HICLK) {
        m |= 0x8;
        if (p->AUDCTL & CTL_CH34_JOIN) m |= 0x4;
    }
    return m;
}


/* Latch a fired IRQ into IRQST and tell the host, if either is wired up
 * to hear about it.  IRQST only ever gets bits ORed in here; a write to
 * IRQEN or SKREST is what clears them (see ad_pokey_write()). */
/* Cycles between a timer's borrow and its interrupt appearing in IRQST - the
 * underflow logic's pipeline stages.  See timer_irq_pending in c012294.h.
 *
 * [SILICON] 2026-09-20, D21: timer 1 on the 1.79 MHz clock, IRQST read on each
 * clock after STIMER.  AUDF1 = $80: high at 135, low at 136.  AUDF1 = $86:
 * high at 141, low at 142.  So STIMER to IRQST is AUDF + 8 - the AUDF + 4
 * fast period plus these four.  The measurement fixes the sum, not the split
 * between period and pipeline; the split is acid800's and Altirra's.  D22's
 * 64 kHz -> 1.79 MHz result (96 pulses owed -> IRQST at write + 100) is a
 * second reading of the same four. */
#define TIMER_IRQ_STAGE_DELAY 4

static void fire_irq(ad_pokey *p, uint8_t mask)
{
    /* A disabled interrupt's status bit is HELD reset - it is not merely
     * cleared once by the IRQEN write.  Altirra HRM 5.7: "With the exception
     * of bit 3, the status bit for a disabled interrupt is always locked to a
     * 1.  There is no interrupt queuing for a disabled interrupt - any
     * interrupts that would have triggered while an interrupt is disabled are
     * lost."  (IRQST reads active-low, so "locked to a 1" is this latch held
     * at 0.)  The IRQST register reference says the same: "Most bits in IRQST
     * are reset and stay low when the corresponding interrupt is cleared via
     * IRQEN."  atari800 7.1.2 pokey.c:631/642/653 gates each timer the same
     * way, testing POKEY_IRQEN at the moment the bit is asserted.
     *
     * This matters because a timer's borrow reaches IRQST four cycles later
     * (TIMER_IRQ_STAGE_DELAY).  Gating only at the borrow lets an in-flight
     * interrupt land in IRQST after IRQEN has already disabled it, so IRQST
     * comes back set a few cycles after the write that cleared it.  That is
     * exactly what acid800 pokey_addrmirror measures, and whether it hit
     * depended on where the write happened to fall in the timer period.
     * Bit 3 (SEROC) is the documented exception, but it is never latched here
     * at all - ad_pokey_read() derives it live from sdo_idle(). */
    /* Do not apply the CPU interrupt-entry latency to the IRQST latch.
     * HRM 5.7's enable-delay example says "the IRQ handler would trigger";
     * acid800 distinguishes IRQST timing from the CPU's later IRQ entry.
     * pokey_timertiming enables at STIMER+42 and requires the +44 latch to
     * survive (AUDF1=8, two-tone, late AUDF write at +31). A four-cycle gate
     * here loses that event even though the AUDF reload correctly missed the
     * write. CPU recognition latency belongs to the host's sync_irq_line(). */
    mask &= (uint8_t)(p->IRQEN | IRQ_SEROC);
    if (!mask)
        return;

    p->IRQST |= mask;
    if (p->host && p->host->raise_irq)
        p->host->raise_irq(p->host->ctx, mask);
    notify_pins(p);
}

/* The serial port and SKSTAT, defined in their own section below. */
static bool    sdo_idle(const ad_pokey *p);
static bool    sdo_line(const ad_pokey *p);
static bool    sdo_shift_level(const ad_pokey *p);
static uint8_t skstat_read(const ad_pokey *p);
static void    serial_step(ad_pokey *p, const uint32_t *borrows, uint8_t timer_completed);

/* Reload timer w.  Fast timers restart a machine-cycle countdown.  Slow
 * timers reload their counter too, but the shared 64/15KHz clock phase is
 * independent of this write (Altirra HRM 5.3, STIMER): align the resulting
 * underflow to the next source-clock pulse instead of starting DIV_64/DIV_15
 * from the write cycle. */
static void rearm_timer(ad_pokey *p, int w)
{
    p->timer_reload_delay[w] = 3;
    if (timer_fast_clock(p, w)) {
        p->tcnt[w] = p->divisor[timer_channel(w)];
        return;
    }

    const uint32_t period = slow_clock_period(p);
    const uint32_t ticks = slow_divisor_ticks(p, w);
    p->slow_hold_ticks[w] = ticks;

    if (!p->rng_enabled) {
        /* The source clock is frozen in init.  Keep a conventional raw value
         * for diagnostics; slow_clocks_leave_init() will re-anchor it. */
        p->tcnt[w] = p->divisor[timer_channel(w)];
        return;
    }

    p->tcnt[w] = slow_clock_delay(p) + (ticks - 1) * period;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void ad_pokey_reset(ad_pokey *p)
{
    for (int i = 0; i < 4; ++i) {
        p->AUDF[i] = p->AUDC[i] = 0;
        p->out[i] = 0;
    }
    p->AUDCTL = 0;
    p->base_mult = DIV_64;
    /* The divisors follow the cleared registers: AUDF 0 on the 64 kHz clock
     * is a one-pulse period (28 clocks), which is what the counters start
     * from below.  Seeding them with the whole base clock instead only
     * worked while an AUDCTL write re-armed every timer. */
    recompute_all(p);
    p->p4 = p->p5 = 0;
    p->SKCTL = 0;
    p->pot_scanning = false;
    p->pot_scan_ever = false;
    p->pot_scan_start = 0;
    p->pot_count = p->pot_previous = 0;
    p->pot_clear_delay = p->pot_finish_delay = 0;
    p->pot_transition = false;
    p->pot_next_tick = 0;
    memset(p->pot_latch, 0, sizeof p->pot_latch);
    p->rng_enabled = 0;
    p->rng_init_prev = 1;   /* reset leaves the chip held, so Init is asserted */
    p->rng_sel9_prev = 0;   /* AUDCTL is 0 after reset */
    chain_reset(&p->rng);
    /* Each timer starts a full period away - one source pulse, from the
     * cleared registers above - and the held chip remembers that count
     * for the release. */
    p->slow_next_64 = 0;
    p->slow_next_15 = 0;
    for (int i = 0; i < 4; ++i) {
        p->timer_reload_delay[i] = 0;
        p->tcnt[i] = p->divisor[timer_channel(i)];
        p->slow_hold_ticks[i] = (p->tcnt[i] + DIV_64 - 1) / DIV_64;
        if (!p->slow_hold_ticks[i]) p->slow_hold_ticks[i] = 1;
    }
    p->IRQEN = p->IRQST = 0;
    p->timer_irq_pending = 0;
    memset(p->timer_irq_delay, 0, sizeof p->timer_irq_delay);
    p->twotone_delay = p->twotone_delay2 = 0;
    p->kbd_scan_due = 0;
    p->sdo_idle_level = true;
    p->KBCODE = 0;
    p->kb_down = p->kb_shift = false;
    p->kbd_code = p->kbd_state = p->kbd_latch = 0;
    p->kbd_ctrl = p->kbd_break = false;
    /* key_kr1/key_kr2/key_lines are host wiring, untouched by reset */
    p->SERIN = p->SEROUT = 0;
    p->sdo_pending = p->sdo_busy = false;
    p->sdo_byte = 0; p->sdo_left = 0;
    p->sdi_busy = false;
    p->sdi_byte = 0; p->sdi_left = 0;
    p->sdi_line_in = 1;              /* the line idles at mark */
    p->sdi_host_line = 0;
    p->sdi_src_busy = false;
    p->sdi_src_byte = 0; p->sdi_src_left = 0;
    p->sdi_shift = 0; p->sdi_stop_ok = true;
    p->st_latch = 0;
    p->external_clock = 0;
    p->clock_out_phase = p->clock_bi_phase = 0;
    p->serial_output_delay = 0;
    p->cassette_level = 0;      /* two-tone is off after reset: the divider's reset state */
    audio_queue_clear(p);
    p->highpass_latch[0] = p->highpass_latch[1] = 1;
    memset(p->highpass_delay, 0, sizeof p->highpass_delay);
    p->audio_pipe = 0;
    p->audio_reset_delay = 0;
    p->noise4_history = p->noise5_history = p->noise917_history = 0;
    build_tables();
    audio_level_refresh(p);
    notify_pins(p);
    notify_clocks(p, false);
    /* p->cycles, p->allpot and p->host are this port's stand-ins for
     * host-owned state (the machine clock, the DIP bank, the callback
     * wiring) - a chip reset doesn't touch any of them, same as
     * Pokey::reset() never touches its host. */
}

void ad_pokey_init(ad_pokey *p, uint32_t clock_hz, uint32_t sample_rate)
{
    memset(p, 0, sizeof *p);
    p->base_clock = clock_hz ? clock_hz : 1;
    p->sys_freq = sample_rate ? sample_rate : 1;
    p->quiet_skip = true;
    build_tables();
    /* The measured DAC is the chip; 20 Hz DC removal at unity gain is the
     * default playback stage (ad_pokey_set_measured_audio() changes it). */
    audio_playback_config(p, 20.0, 1.0);
    ad_pokey_reset(p);
}

void ad_pokey_set_quiet_skip(ad_pokey *p, bool enabled)
{
    p->quiet_skip = enabled;
}

void ad_pokey_set_allpot(ad_pokey *p, uint8_t v)
{
    if (p->pot_counter_mode && p->pot_scanning) {
        for (int i = 0; i < 8; ++i)
            if ((p->allpot & (1u << i)) && !(v & (1u << i)))
                p->pot_latch[i] = p->pot_count;
    }
    p->allpot = v;
}

void ad_pokey_set_pot_scan(ad_pokey *p, bool enabled)
{
    p->pot_counter_mode = enabled;
}

static void pot_step(ad_pokey *p)
{
    if (!p->pot_counter_mode || !p->pot_scanning) return;
    p->pot_transition = false;
    if (p->pot_finish_delay && --p->pot_finish_delay == 0) {
        p->pot_scanning = false;
        return;
    }
    if (p->pot_clear_delay) {
        --p->pot_clear_delay;
        p->pot_count = 0;
    } else {
        bool tick = (p->SKCTL & SK_FASTPOT) != 0;
        if (!tick && p->rng_enabled && p->pot_next_tick == p->cycles) tick = true;
        if (tick) {
            p->pot_previous = p->pot_count;
            ++p->pot_count;
            p->pot_transition = true;
            if (p->pot_count == 228) p->pot_finish_delay = 2;
            if (p->pot_count == 229) p->pot_transition = false;
        }
    }
    /* The pot increment follows the shared 15K source pulse by one clock. */
    if (p->rng_enabled && p->slow_next_15 == p->cycles)
        p->pot_next_tick = p->cycles + 1;
    for (int i = 0; i < 8; ++i)
        if (p->allpot & (1u << i)) p->pot_latch[i] = p->pot_count;
}

void ad_pokey_set_host(ad_pokey *p, const ad_pokey_host *h)
{
    p->host = h;
}

void ad_pokey_set_key_matrix(ad_pokey *p, uint64_t kr1, uint64_t kr2)
{
    p->key_kr1 = kr1;
    p->key_kr2 = kr2;
}

void ad_pokey_set_key_lines(ad_pokey *p, int (*fn)(void *ctx, uint8_t code), void *ctx)
{
    p->key_lines = fn;
    p->key_lines_ctx = ctx;
}

/* One 15 kHz keyboard scan tick (KEY_core.v; Altirra pokey.cpp's
 * kATPokeyEventKeyboardScan, whose comment documents the state machine:
 *   0 waiting for key:     down -> 1, load compare latch
 *   1 waiting for bounce:  down & same -> 2 (take the code); down & other
 *                          -> 0; up & same -> 0
 *   2 waiting for release: up & same -> 3
 *   3 release debounce:    down & same -> 2; up & same -> 0
 * "same" is always true with debounce (SKCTL bit 0) off.  Key-down in
 * SKSTAT reads active in states 2 and 3.  CTRL and SHIFT are latched
 * from KR2 when the counter passes $00 and $10; BREAK raises its IRQ on
 * KR2 going active at $30.)  With SKCTL bit 1 clear the counter and the
 * state machine are held reset (KEY_core.v's ~SKCTLS[1] resets). */
/* KEY_core.v cell2p/cell3/cell6 reset from SKCTL bit 1 on the
 * master clock, independently of the (possibly stopped) scan clock, so
 * the clear happens the moment bit 1 is written low (ad_pokey_write's
 * W_SKCTL case), not on the next 15 kHz pulse.  KBCODE and pending IRQs
 * are separate latches and survive this reset.  AltirraSDL
 * src/ATAudio/source/pokey.cpp, SKCTL keyboard-disable branch, also
 * clears key-down and the scanner while preserving Shift status. */
static void keyboard_scan_disable(ad_pokey *p)
{
    p->kbd_code = 0;
    p->kbd_state = 0;
    p->kb_down = false;
}

static void keyboard_scan_step(ad_pokey *p)
{
    if (!(p->SKCTL & SK_KEYSCAN)) {
        p->kbd_code = 0;
        p->kbd_state = 0;
        return;
    }
    const uint8_t kc = p->kbd_code;
    p->kbd_code = (uint8_t)((kc + 1) & 0x3F);

    int lines;
    if (p->key_lines) lines = p->key_lines(p->key_lines_ctx, kc);
    else lines = (int)((p->key_kr1 >> kc) & 1u) | (int)((p->key_kr2 >> kc) & 1u) << 1;
    const bool kr1 = (lines & 1) != 0, kr2 = (lines & 2) != 0;

    switch (kc) {
    case 0x00: p->kbd_ctrl = kr2; break;
    case 0x10: p->kb_shift = kr2; break;
    case 0x30:
        if (kr2 && !p->kbd_break) fire_irq(p, IRQ_BREAK);
        p->kbd_break = kr2;
        break;
    default: break;
    }

    const bool same = kc == p->kbd_latch || !(p->SKCTL & SK_DEBOUNCE);
    switch (p->kbd_state) {
    case 0:
        if (kr1) { p->kbd_latch = kc; p->kbd_state = 1; }
        break;
    case 1:
        if (kr1) {
            if (same) {
                p->kbd_state = 2;
                if (p->IRQST & IRQ_KEYBD) p->st_latch |= ST_KBERR;
                p->KBCODE = (uint8_t)(kc | (p->kb_shift ? 0x40 : 0) | (p->kbd_ctrl ? 0x80 : 0));
                p->kb_down = true;
                fire_irq(p, IRQ_KEYBD);
            } else {
                p->kbd_state = 0;
            }
        } else if (same) {
            p->kbd_state = 0;
        }
        break;
    case 2:
        if (same && !kr1) p->kbd_state = 3;
        break;
    default:
        if (same) {
            if (kr1) p->kbd_state = 2;
            else { p->kb_down = false; p->kbd_state = 0; }
        }
        break;
    }
}

bool ad_pokey_irq_asserted(const ad_pokey *p)
{
    uint8_t pending = p->IRQST & (uint8_t)~IRQ_SEROC;
    if (!p->sdo_busy) pending |= IRQ_SEROC;
    return (pending & p->IRQEN) != 0;
}

static void notify_pins(ad_pokey *p)
{
    uint8_t irq = ad_pokey_irq_asserted(p) ? 1 : 0;
    uint8_t level = (p->SKCTL & SK_TWOTONE) ? p->cassette_level : (uint8_t)sdo_line(p);
    if (irq != p->irq_level) {
        p->irq_level = irq;
        if (p->io && p->io->irq_line) p->io->irq_line(p->io->ctx, irq, p->cycles);
    }
    if (level != p->serial_level) {
        p->serial_level = level;
        if (p->io && p->io->serial_output) p->io->serial_output(p->io->ctx, level, p->cycles);
    }
}

void ad_pokey_set_io(ad_pokey *p, const ad_pokey_io *io)
{
    p->io = io;
    p->irq_level = (uint8_t)ad_pokey_irq_asserted(p);
    p->serial_level = (p->SKCTL & SK_TWOTONE) ? p->cassette_level : (uint8_t)sdo_line(p);
    if (io && io->irq_line) io->irq_line(io->ctx, p->irq_level, p->cycles);
    if (io && io->serial_output) io->serial_output(io->ctx, p->serial_level, p->cycles);
}

static void notify_clocks(ad_pokey *p, bool force)
{
    const uint8_t driven = (p->SKCTL & 0x30) == 0x20;
    const uint8_t out = (p->SKCTL & 0x60) ? p->clock_out_phase : p->external_clock;
    const uint8_t bi = driven ? p->clock_bi_phase : p->external_clock;
    if (force || out != p->clock_out_level) {
        p->clock_out_level = out;
        if (p->clocks && p->clocks->output)
            p->clocks->output(p->clocks->ctx, out, p->cycles);
    }
    if (force || bi != p->clock_bi_level || driven != p->clock_bi_driven) {
        p->clock_bi_level = bi;
        p->clock_bi_driven = driven;
        if (p->clocks && p->clocks->bidirectional)
            p->clocks->bidirectional(p->clocks->ctx, bi, driven, p->cycles);
    }
}

void ad_pokey_set_clocks(ad_pokey *p, const ad_pokey_clocks *clocks)
{
    p->clocks = clocks;
    notify_clocks(p, true);
}

/* The audio integrator's two half clocks of one machine clock.  The level
 * only moves when a high-pass delay retires between the halves, so with
 * none pending both halves integrate in one call (same spans, same sample
 * boundaries, same sums as two calls of one half each). */
static inline void audio_clock_integrate(ad_pokey *p)
{
    if (p->highpass_delay[0] | p->highpass_delay[1]) {
        cycle_audio_sample(p);
        cycle_audio_sample(p);
    } else {
        cycle_audio_integrate(p, p->audio_level, 2);
    }
}

/* One clock of the RANDOM chain.  The chain sees Init one clock late:
 * SKCTL's register and the chain both move on the same edge, so the edge
 * on which a write lands still uses the pre-write Init; only the following
 * clock sees the new value.  See rng_init_prev in c012294.h for the
 * measured datapoint this is pinned to ($1F four clocks after leaving
 * init).
 *
 * [SILICON] 2026-09-20, D21.  Both lags, and the chain's behaviour under
 * Init, were read off real chips clock by clock:
 *   - Init: after SKCTL=7 / 18 clocks / SKCTL=0 / 10 clocks / SKCTL=7, thirty-
 *     two RANDOM reads 25 clocks apart on two chips equal this model byte for
 *     byte, repeatably across power cycles.  The same model started from a
 *     FULL Init hold does not match, so ten clocks of Init do not clear the
 *     chain - zeros walk in one a clock, as chain_step() has it.
 *   - the ramp: SKCTL=0 written to a running chip, RANDOM read on each clock
 *     4..19 after it, two prior states, both chips: $FE on clock 8 and $FF
 *     from clock 9, as here.  An Init-hold sweep of 4..19 clocks matches too.
 *     (Warlords' POKEY PROTECT reads at clock 8 and needs $FF; the chip does
 *     not give it, so that game's pass comes from its board, not from here.)
 *   - the 9-bit select: AUDCTL bit 7 flipped on a running chip, RANDOM read
 *     on each clock 4..19 after the write beside a no-flip control, at five
 *     flip phases.  The chip matches the select applied ONE CLOCK LATE and
 *     does not match the same model with the select applied immediately; the
 *     new source appears one bit per clock, not as a jump to the other
 *     sequence.  rng_sel9_prev was inferred before this (D15); it is now
 *     measured. */
static inline void chain_clock(ad_pokey *p)
{
    chain_step(&p->rng, p->rng_init_prev != 0, p->rng_sel9_prev != 0);
    poly45_step(p, p->rng_init_prev != 0);
    p->rng_init_prev = (uint8_t)!p->rng_enabled;
    p->rng_sel9_prev = (uint8_t)((p->AUDCTL & CTL_POLY9) != 0);
}

#ifdef AD_PROBE
/* Test hook: every clock the full core steps, with that clock's borrow
 * mask (bit w = timer w), or a quiet span of n clocks with no borrows.
 * tests/test_split_audio.c records these from one instance and replays
 * them into ad_pokey_audio_clock()/ad_pokey_audio_quiet() on another. */
void (*ad_pokey_probe_trace)(void *ctx, uint8_t borrows, uint32_t nclocks) = NULL;
void *ad_pokey_probe_trace_ctx = NULL;
#define TRACE(mask, n) do { if (ad_pokey_probe_trace) ad_pokey_probe_trace(ad_pokey_probe_trace_ctx, (mask), (n)); } while (0)
#else
#define TRACE(mask, n) do { } while (0)
#endif

/* One machine clock. The RANDOM chain clocks every
 * cycle, held or not (what it takes in differs); the four hardware
 * timers count while the chip runs, and while held only on the fast
 * clock (timers_running()).  See c012294.h's time-model note for who calls
 * this and with what. */
static uint8_t advance_clock(ad_pokey *p, bool with_audio)
{
    const uint32_t cycles = 1;
    if (with_audio) audio_clock_integrate(p);
    p->cycles += cycles;
    chain_clock(p);

    /* Retire each timer's IRQ before admitting this clock's new borrows.
     * A later timer event cannot postpone an earlier timer's deadline. */
    uint8_t due = 0;
    for (int w = 0; w < 3; ++w) {
        const uint8_t bit = timer_irq_bit(w);
        if ((p->timer_irq_pending & bit) && --p->timer_irq_delay[w] == 0) {
            p->timer_irq_pending &= (uint8_t)~bit;
            due |= bit;
        }
    }
    if (due) fire_irq(p, due);

    uint32_t borrows[4] = { 0, 0, 0, 0 };
    uint8_t reload_due = 0;
    const bool twotone_reset_due = (p->SKCTL & SK_TWOTONE) &&
                                   (p->twotone_delay == 1 || p->twotone_delay2 == 1);
    const uint8_t running = timers_running(p);
    for (int w = 0; w < 4; ++w) {
        if (p->timer_reload_delay[w] && --p->timer_reload_delay[w] == 0)
            reload_due |= timer_irq_bit(w);
        if (!(running & (1u << w))) {
            /* Held for async receive (see timers_running()): pin tcnt[2] at a
             * full period every slice it is held, rather than merely
             * freezing wherever it happened to be, so release - a start
             * bit (sdi_start()'s rearm_timer()) or SKCTL clearing
             * SK_ASYNC - always begins a fresh period.  Confirmed against
             * acid800 pokey_asyncrecv's timer 3+4 skip-cycles check: a
             * ~two-line async hold started right after one expiry pushes
             * the next one out by a full extra period (four lines), not
             * by the two lines actually held, which only "held in reset
             * state" (not "paused at its current count") explains. */
            if ((w == 2 || w == 3) && p->rng_enabled && (p->SKCTL & SK_ASYNC) && !p->sdi_busy)
                rearm_timer(p, w);
            continue;
        }
        /* A reset reload can straddle a slow source pulse. Do not let
         * the pre-reload counter borrow and overwrite its reload deadline.
         * Normal borrow reloads also wait here; their next period cannot
         * expire within three clocks (the minimum fast period is four). */
        if (p->timer_reload_delay[w] || (reload_due & timer_irq_bit(w)))
            continue;
        /* HRM 5.6: the other timer can fire "up to one cycle later".
         * The reset at borrow+2 wins over a new borrow on this clock.
         * Keep the rearm below, after reload retirement, so its full reload
         * delay and the documented +2 period extension are preserved. */
        if (w < 2 && twotone_reset_due)
            continue;
        if (p->tcnt[w] > 1) {
            --p->tcnt[w];
        } else {
            p->tcnt[w] = p->divisor[timer_channel(w)];
            borrows[w] = 1;
            p->timer_reload_delay[w] = 3;
            /* Every borrow enters the pipeline; IRQEN gates it when it
             * reaches the latch four cycles later. See fire_irq() and the
             * late-enable regressions in test_twotone_resync.c. */
            if (w < 3) {
                p->timer_irq_pending |= timer_irq_bit(w);
                p->timer_irq_delay[w] = TIMER_IRQ_STAGE_DELAY;
            }
        }
    }

    /* Sample AUDF on the actual reload edge, not at its register write or
     * at the earlier borrow stage. For fast timers three clocks of the
     * next period have already elapsed. Slow timers stay on the shared
     * source phase, including when a reload takes place during init. */
    /* Linking changes what a reload MEANS for the low timer of the pair.
     * Altirra HRM 5.3, "16-bit timers": "The automatic reload on underflow is
     * suppressed on the low timer ... When the high timer underflows, both the
     * low and high timer counters are reloaded together."  And "Linked timer
     * fire timing": the low timer "first counts down and underflows from its
     * initial period and then continues to count down and underflow every 256
     * ticks after that until the high timer also underflows and resets both
     * timers ... the low timer in a linked pair will fire AUDF2+1 or AUDF4+1
     * times for each time the high timer fires."
     *
     * Treating timer 1 as an independent countdown of its own period made it
     * fire every 20 cycles for AUDF1=$10 instead of once per 23-cycle linked
     * period, which acid800 pokey_timertiming reads as "1.79MHz 16-bit lo
     * timer triggered too early (loop #2)".  Only the 1+2 pair needs this:
     * timer 3 has no IRQ but uses slot 3 for cycle audio. Slot 2 is TIMR4,
     * the high timer of the 3+4 pair; both pairs use coordinated reloads. */
    const bool pair12_reset = (reload_due & timer_irq_bit(1)) != 0 &&
                              (p->AUDCTL & CTL_CH12_JOIN) != 0;
    const bool pair34_reset = (reload_due & timer_irq_bit(2)) != 0 &&
                              (p->AUDCTL & CTL_CH34_JOIN) != 0;
    for (int w = 0; w < 4; ++w) {
        bool reload = (reload_due & timer_irq_bit(w)) != 0;
        if ((w == 0 && pair12_reset) || (w == 3 && pair34_reset)) reload = true;
        if (!reload) continue;

        if ((w == 0 && (p->AUDCTL & CTL_CH12_JOIN) && !pair12_reset) ||
            (w == 3 && (p->AUDCTL & CTL_CH34_JOIN) && !pair34_reset)) {
            /* Linked low timer with no high-timer underflow: it wraps rather
             * than reloading, so the next underflow is a whole 256 ticks away
             * (the -3 is the borrow-to-reload stage the fast path also pays). */
            if (timer_fast_clock(p, w)) {
                p->tcnt[w] = 256u - 3u;
            } else {
                /* The three pipeline clocks have already passed. The next
                 * wrap is 256 SOURCE pulses after underflow, not another
                 * 256*period clocks after pipeline retirement. */
                p->slow_hold_ticks[w] = 256;
                p->tcnt[w] = p->rng_enabled ? slow_clock_delay(p) + 255u * p->base_mult
                                             : 256u * p->base_mult;
            }
            p->timer_reload_delay[w] = 0;
            continue;
        }

        rearm_timer(p, w);
        if (timer_fast_clock(p, w)) p->tcnt[w] -= 3;
        p->timer_reload_delay[w] = 0;
    }

    /* Roll the two shared slow-clock phase markers past this slice before
     * any post-timer logic (two-tone/serial) can rearm a timer at the
     * current machine time. */
    pot_step(p);
    /* The keyboard scan and the pot counter are ONE clock: "the polling
     * counter is driven by the same clock that is used by the keyboard scan"
     * (Altirra HRM 5.9), and Altirra schedules both on the same tick, 81 +
     * 114 N clocks after the Init release.  That is the clock after the
     * 15 kHz pulse the timers count (pot_step() above). */
    if (p->kbd_scan_due) {
        p->kbd_scan_due = 0;
        if (p->rng_enabled) keyboard_scan_step(p);
    }
    if (p->rng_enabled && p->slow_next_15 == p->cycles) p->kbd_scan_due = 1;
    slow_clocks_advance_phase(p);

    /* Two-tone mode (SKCTL bit 3): the serial output's FSK tone shares
     * timers 1 and 2 with ordinary audio/IRQ duty, switching between them
     * per output data bit (timer 1 = mark/1, timer 2 = space/0), and
     * resyncs both whenever either contributes a pulse to the tone.
     * Altirra HRM 5.6, "Two-tone resync": "whenever the serial output
     * toggles due to one of the timers, both timers are reset ... Timer 1
     * pulses are only used by the serial output for a 1 bit, but timer 2
     * pulses are always used, causing a resync ... regardless of the
     * current data bit."  So timer 2's borrow always resyncs both; timer
     * 1's borrow resyncs both only while the output line is at mark.
     * This is why acid800 pokey_twotone measures far fewer timer 2 IRQs
     * during a continuous mark than during a continuous space: with
     * AUDF1 << AUDF2 (1 hblank vs. 2), timer 1's frequent resyncs starve
     * timer 2 of a full period almost every time, so its IRQ - the same
     * IRQ_TIMR2/tcnt[1] the ordinary audio hardware uses - fires far less
     * often than its free-running rate would give. Resync sends no audio
     * clock pulses: cycle audio retains its flip-flops. The legacy renderer
     * keeps its historical divider/output reset approximation. */
    if (p->SKCTL & SK_TWOTONE) {
        /* The reset is TWO CYCLES LATE.  HRM 5.6, "Two-tone resync timing":
         * "The timer 1+2 reset in two-tone mode occurs two cycles after the
         * timer that triggered the resync reloads ... if timer 1 at 1.79MHz
         * drives the resync, it will have a period of two cycles longer than
         * usual, due to being re-reloaded two cycles after the normal reload.
         * Note that this only affects the second and subsequent periods after
         * an STIMER reset, as there is no timer 1+2 resync at the start."
         * Resyncing on the borrow itself made every period after the first come
         * out two cycles short, which acid800 pokey_timertiming brackets
         * exactly: with AUDF1=8 (a 12-cycle period) the first IRQ is at 16 and
         * the second at 30, not 28. */
        bool reset = false;
        if (p->twotone_delay  && --p->twotone_delay  == 0) reset = true;
        if (p->twotone_delay2 && --p->twotone_delay2 == 0) reset = true;
        if (reset) {
            rearm_timer(p, 0);
            rearm_timer(p, 1);
        }
        bool mark = sdo_line(p);
        if (borrows[1] || (borrows[0] && mark)) {
            p->cassette_level ^= 1;
            /* a second resync started while the first is still pending does
             * not replace it: both take place (see twotone_delay2) */
            if (p->twotone_delay) p->twotone_delay2 = 2;
            else                  p->twotone_delay  = 2;
        }
    }
    else {
        p->twotone_delay = p->twotone_delay2 = 0;
    }

    serial_step(p, borrows, due);
    const uint8_t mask = (uint8_t)(borrows[0] | borrows[1] << 1 | borrows[2] << 2 | borrows[3] << 3);
    TRACE(mask, 1);
    if (with_audio) cycle_audio_step(p, borrows);
    notify_pins(p);
    return mask;
}

/* Quiet clocks ---------------------------------------------------------
 *
 * Most clocks change nothing but the RANDOM chain, the poly counters, the
 * timer countdowns and the audio integrator: no timer reaches its borrow,
 * nothing is in the reload or IRQ pipelines, no two-tone resync or
 * high-pass latch is pending, the pot counter is idle and the serial port
 * is unclocked (it polls the host and watches the input line every clock
 * once a receive clock is selected).  quiet_span() says how many such
 * clocks lie ahead, at most `limit`, and advance_quiet() takes them in one
 * step with the same arithmetic advance_clock() would have applied clock
 * by clock - the chain and the noise histories still shift once per
 * clock, the countdowns lose exactly k, the slow-clock markers roll past
 * the same point, and the audio integrator adds the same area at the same
 * sample boundaries, since the level cannot change without a borrow or a
 * latch update.  Everything else stays on the one-clock path, so callback
 * timestamps and register state are unchanged (tests/probe_c012294_golden.c
 * hashes both paths against the one-clock core). */
static uint32_t quiet_span(const ad_pokey *p, uint32_t limit)
{
    if (!p->quiet_skip)
        return 0;
    if (p->timer_irq_pending || p->twotone_delay || p->twotone_delay2 ||
        p->kbd_scan_due || p->serial_output_delay ||
        p->highpass_delay[0] || p->highpass_delay[1] ||
        p->audio_pipe || p->audio_reset_delay)
        return 0;
    /* With a filter bit clear, the one-clock path forces that latch to 1
     * at the end of every clock; a latch still 0 from a filtered stretch
     * makes the next clock an event. */
    if ((!(p->AUDCTL & CTL_CH1_FILTER) && !p->highpass_latch[0]) ||
        (!(p->AUDCTL & CTL_CH2_FILTER) && !p->highpass_latch[1]))
        return 0;
    if (p->timer_reload_delay[0] | p->timer_reload_delay[1] |
        p->timer_reload_delay[2] | p->timer_reload_delay[3])
        return 0;
    if (p->pot_counter_mode && p->pot_scanning)
        return 0;
    if (p->SKCTL & 0x30)            /* receive clock selected, or async hold */
        return 0;
    uint32_t k = limit;
    /* The keyboard scanner ticks on every 15 kHz pulse: stop the span
     * there (the pulse clock itself is quiet). */
    if ((p->SKCTL & SK_KEYSCAN) && p->rng_enabled && p->slow_next_15 > p->cycles) {
        uint64_t to_pulse = p->slow_next_15 - p->cycles;
        if (to_pulse <= 1) return 0;
        if (to_pulse - 1 < k) k = (uint32_t)(to_pulse - 1);
    }
    const uint8_t running = timers_running(p);
    for (int w = 0; w < 4; ++w) {
        if (!(running & (1u << w)))
            continue;
        if (p->tcnt[w] <= 1)        /* borrows on this clock */
            return 0;
        if (p->tcnt[w] - 1 < k)
            k = p->tcnt[w] - 1;     /* the clock it reaches 1 is still quiet */
    }
    return k;
}

/* The audio side of a quiet span: k clocks of integration at the held
 * level, and k steps of the chain and the noise histories. */
static void audio_quiet(ad_pokey *p, uint32_t k)
{
    cycle_audio_integrate(p, p->audio_level, 2u * k);
    for (uint32_t i = 0; i < k; ++i) {
        chain_step(&p->rng, p->rng_init_prev != 0, p->rng_sel9_prev != 0);
        poly45_step(p, p->rng_init_prev != 0);
        p->rng_init_prev = (uint8_t)!p->rng_enabled;
        p->rng_sel9_prev = (uint8_t)((p->AUDCTL & CTL_POLY9) != 0);
        cycle_audio_noise_clock(p);
    }
}

static void advance_quiet(ad_pokey *p, uint32_t k)
{
    TRACE(0, k);
    audio_quiet(p, k);
    p->cycles += k;

    const uint8_t run_mask = timers_running(p);
    for (int w = 0; w < 4; ++w)
        if (run_mask & (1u << w))
            p->tcnt[w] -= k;

    slow_clocks_advance_phase(p);
}

/* Split-core API: the audio half of a clock on its own.  A host that runs
 * the timing engine elsewhere (the hardware replacement's bus core) feeds
 * this instance the same register writes and, per clock, the borrow mask
 * that engine produced; this instance never runs its timers.  Same order
 * of operations as advance_clock(): integrate, chain, then the borrows
 * drive the channel outputs.  tests/test_split_audio.c proves the PCM
 * is byte-identical to the monolithic core. */
void ad_pokey_audio_clock(ad_pokey *p, uint8_t borrow_mask)
{
    audio_clock_integrate(p);
    p->cycles += 1;
    chain_clock(p);
    uint32_t borrows[4] = { borrow_mask & 1u, (borrow_mask >> 1) & 1u,
                            (borrow_mask >> 2) & 1u, (borrow_mask >> 3) & 1u };
    cycle_audio_step(p, borrows);
}

void ad_pokey_audio_quiet(ad_pokey *p, uint32_t k)
{
    audio_quiet(p, k);
    p->cycles += k;
}

uint8_t ad_pokey_timing_clock(ad_pokey *p)
{
    return advance_clock(p, false);
}

void ad_pokey_advance(ad_pokey *p, uint32_t cycles)
{
    /* Walk events in machine-clock order, including IRQ
     * pipeline retirement, serial edges and two-tone timer resyncs. This
     * makes callback timestamps and register state independent of batching.
     * A zero-length advance has no side effects. */
    while (cycles) {
        uint32_t k = quiet_span(p, cycles);
        if (k) {
            advance_quiet(p, k);
            cycles -= k;
        } else {
            advance_clock(p, true);
            cycles--;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Writes                                                              */
/* ------------------------------------------------------------------ */

void ad_pokey_write(ad_pokey *p, uint8_t reg, uint8_t v)
{
    const uint8_t a = reg & 0x0F;
    switch (a) {
    /* AUDF writes update the reload value, not the running counter (HRM 5.3):
     * AUDF1 -> TIMR1 (+TIMR2 when ch1+2 joined), AUDF2 -> TIMR2, AUDF3 ->
     * TIMR4 only when ch3+4 joined, AUDF4 -> TIMR4.  AUDC writes never
     * touch a divisor, so they must not reset a timer's phase. */
    case W_AUDF1:
        p->AUDF[0] = v;
        recompute_channel(p, 0);
        if (p->AUDCTL & CTL_CH12_JOIN) recompute_channel(p, 1);
        break;
    case W_AUDF2:
        p->AUDF[1] = v; recompute_channel(p, 1);
        break;
    case W_AUDF3:
        p->AUDF[2] = v;
        recompute_channel(p, 2);
        if (p->AUDCTL & CTL_CH34_JOIN) recompute_channel(p, 3);
        break;
    case W_AUDF4:
        p->AUDF[3] = v; recompute_channel(p, 3);
        break;

    case W_AUDC1: p->AUDC[0] = v; recompute_channel(p, 0); audio_level_refresh(p); break;
    case W_AUDC2: p->AUDC[1] = v; recompute_channel(p, 1); audio_level_refresh(p); break;
    case W_AUDC3: p->AUDC[2] = v; recompute_channel(p, 2); audio_level_refresh(p); break;
    case W_AUDC4: p->AUDC[3] = v; recompute_channel(p, 3); audio_level_refresh(p); break;

    case W_AUDCTL: {
        /* [SILICON] D21: AUDCTL $00 rewritten as $00 in the middle of timer
         * 1's period leaves IRQST on exactly the clock that a write to an
         * unrelated register does.
         *
         * A rewrite of the current value is a no-op (MAME pokey.cpp returns
         * early on it), and a timer whose clocking the write leaves alone
         * keeps counting: AUDCTL never reloads a counter on the chip, only
         * STIMER and a borrow do (Altirra HRM 5.3, "Reload timing").  Major
         * Havoc rewrites $78 twice a frame and Battlezone $00 every frame;
         * re-arming every timer on each write restarted every tone once the
         * audio came from the hardware counters.
         *
         * [SILICON] 2026-09-20, D21 and D22.  A timer's counter holds the
         * pulses still owed before its next underflow, and an AUDCTL write
         * changes only which clock those pulses are counted on; nothing is
         * reloaded.  Measured on a real chip, timer 1, each as the clock on
         * which IRQST fell after a mid-period write:
         *   64 -> 15 kHz   (D21) three pulses owed, taken at 15 kHz: 246;
         *                  a reload gives 474.
         *   64 kHz -> 1.79 MHz  (D22) 96 pulses owed, taken as clocks: 100;
         *                  a reload gives 104.
         *   1.79 MHz -> 64 kHz  (D22) 9 clocks from underflow, taken as nine
         *                  64 kHz pulses; a reload owes 29.
         *   channels 1+2 joined (D22) timer 1's period undisturbed: 110, as
         *                  the control; a reload gives 138.
         * tcnt is machine cycles to the underflow, aligned to the selected
         * clock, so a carry onto a slow clock goes through pulses: the new
         * clock's own next pulse, then one period for each pulse after it.
         *
         * [SILICON] 2026-09-21, D23 (third ROM; timer 2's IRQ, and a loop one
         * 15 kHz pulse long that counts the pulses a timer still owed):
         *   JOIN $40 -> $50  AUDF1 $60, AUDF2 2 with one of three 64 kHz
         *                  pulses spent.  The HIGH half carries the two it
         *                  owes, now paid by the low half's underflows - its
         *                  next one, then one every 256 - and borrows three
         *                  clocks after the low half, as in every joined
         *                  period: IRQST at write + 339 (336 without the
         *                  three; 619 for a reload).
         *   UNJOIN $51 -> $41  AUDF2 3.  The high half carries the low-half
         *                  underflows it still owed, as 15 kHz pulses: 4 for a
         *                  write up to STIMER + 92, 3 from STIMER + 93 (the low
         *                  half borrows at + 90; the high half counts it three
         *                  clocks on).  A reload owes 4 every time.  The low
         *                  half, caught in its 256-clock free run, runs on to
         *                  its next underflow (write + 230); it is not
         *                  reloaded either.
         *   UNDER INIT     the same rule holds with Init asserted.  A
         *                  1.79 MHz timer runs under Init (STIMER + AUDF + 8,
         *                  as when running).  64 kHz -> 1.79 MHz: the 97
         *                  pulses owed, as clocks, IRQST at write + 101 (a
         *                  reload: 104).  1.79 MHz -> 15 kHz, then released:
         *                  the pulse count matched the running chip's on all
         *                  sixteen write clocks (a reload owes 113).
         *   THE RELOAD INSTANT  1.79 MHz -> 15 kHz on each of 32 clocks around
         *                  timer 1's first underflow: reported as matching
         *                  this code, including the four clocks that owe a
         *                  full AUDF + 1 (tcnt less the three-clock delay).
         * (The unjoined low half and the reload instant were reported as
         * "matched the prediction", not read out row by row.)
         * Nothing here re-arms any more.  Not measured: a join or unjoin on a
         * slow clock's high half, and a join or unjoin made under Init. */
        if (v == p->AUDCTL)
            break;
        const uint8_t old = p->AUDCTL;
        bool fast_before[4];
        bool carry_in_flight[4] = { false, false, false, false };
        uint32_t pulses_before[4];
        for (int w = 0; w < 4; ++w) {
            const bool high_half = (w == 1 || w == 2);   /* channels 2 and 4 */
            const uint8_t join_bit = (w < 2) ? CTL_CH12_JOIN : CTL_CH34_JOIN;
            fast_before[w] = timer_fast_clock(p, w);
            if (high_half && (old & join_bit)) {
                /* A joined high half owes low-half underflows, not clocks.
                 * tcnt is the clocks to its own borrow, and it takes each
                 * low-half underflow as that lap ends, so the underflows
                 * still to come are the 256-pulse laps tcnt spans. */
                const int lo = (w == 1) ? 0 : 3;
                pulses_before[w] = joined_remaining_ticks(p, w);
                /* The count includes a low-half carry still in its three
                 * pipeline clocks.  An unjoin loses that carry (D23: the
                 * high half owes it on its own clock); a pair that stays
                 * joined still takes it, see the joined branch below. */
                carry_in_flight[w] = fast_before[lo] && p->timer_reload_delay[lo] &&
                                     !p->timer_reload_delay[w];
            } else if (!fast_before[w]) {
                /* Held in Init the slow clocks are stopped and tcnt is not
                 * kept up; the pulses owed are what the hold remembered. */
                if (p->rng_enabled)
                    pulses_before[w] = slow_remaining_ticks(p, w);
                else
                    pulses_before[w] = p->slow_hold_ticks[w] ? p->slow_hold_ticks[w]
                                                             : slow_divisor_ticks(p, w);
            } else if (!high_half && (old & join_bit) && (v & join_bit) &&
                       p->timer_reload_delay[w] &&
                       !p->timer_reload_delay[w == 0 ? 1 : 2]) {
                /* A joined low half in its borrow pipeline, with no pair
                 * reset behind it and the pair staying joined, does not
                 * reload: it wraps (advance_clock()'s 256 - 3), so it owes
                 * the rest of a 256-clock lap that began at the borrow,
                 * not the AUDF reload tcnt holds while staged. */
                pulses_before[w] = 253u + p->timer_reload_delay[w];
            } else if (p->timer_reload_delay[w] && p->tcnt[w] > 3)
                pulses_before[w] = p->tcnt[w] - 3;
            else
                pulses_before[w] = p->tcnt[w] ? p->tcnt[w] : 1;
        }
        p->AUDCTL = v;
        p->base_mult = (v & CTL_CLK15) ? DIV_15 : DIV_64;
        recompute_all(p);
        const uint8_t changed = (uint8_t)(old ^ v);
        static const int order[4] = { 0, 3, 1, 2 };   /* low halves first */
        for (int i = 0; i < 4; ++i) {
            const int w = order[i];
            const uint8_t join_bit = (w < 2) ? CTL_CH12_JOIN : CTL_CH34_JOIN;
            const bool link_changed = (changed & join_bit) != 0;
            const bool high_half = (w == 1 || w == 2);   /* channels 2 and 4 */
            const bool fast_now = timer_fast_clock(p, w);
            const bool clock_changed = fast_now != fast_before[w] ||
                                       (!fast_now && (changed & CTL_CLK15));
            if ((link_changed || clock_changed) && high_half && (v & join_bit)) {
                /* joined: the pulses owed now come from the low half - its
                 * next underflow, then one every 256 of its pulses - and on
                 * the 1.79 MHz clock the high half borrows three clocks after
                 * the low half does, as it does in every joined period */
                const int lo = (w == 1) ? 0 : 3;
                /* A pair that was already joined keeps the carry in flight:
                 * it lands on its own clock whatever the new source, so it
                 * is not one of the underflows still to come.  When it is
                 * the high half's last, the high half borrows as it lands
                 * and tcnt already says when. */
                const bool keep_carry = (old & join_bit) && carry_in_flight[w];
                if (keep_carry && pulses_before[w] == 1)
                    continue;
                const uint32_t owed = pulses_before[w] - (keep_carry ? 1u : 0u);
                const bool lo_fast = timer_fast_clock(p, lo);
                const uint32_t unit = lo_fast ? 1u : slow_clock_period(p);
                p->tcnt[w] = p->tcnt[lo] + (owed - 1) * 256u * unit +
                             (lo_fast ? 3u : 0u);
                if (!lo_fast) {
                    /* slow_hold_ticks is always in SOURCE-pulse units,
                     * even for a high half whose input is low underflows. */
                    const uint32_t low_ticks = p->rng_enabled ? slow_remaining_ticks(p, lo)
                                                               : p->slow_hold_ticks[lo];
                    p->slow_hold_ticks[w] = low_ticks + (owed - 1) * 256u;
                }
                p->timer_reload_delay[w] = 0;
            } else if (!clock_changed && !(link_changed && high_half)) {
                /* same clock, and at most the low half of a re-linked pair:
                 * the period in progress is left alone */
            } else if (fast_now) {
                p->tcnt[w] = pulses_before[w];
                p->timer_reload_delay[w] = 0;
            } else {
                p->slow_hold_ticks[w] = pulses_before[w];
                p->tcnt[w] = slow_clock_delay(p) +
                             (pulses_before[w] - 1) * slow_clock_period(p);
                p->timer_reload_delay[w] = 0;
            }
        }
        break;
    }

    case W_STIMER:
        /* All four output flip-flops go to 1 (Altirra HRM 5.3, STIMER:
         * "sets the output flip-flops to 1. When high-pass filters are
         * disabled, this turns off the output of channels 1 and 2 and
         * turns on the output of channels 3 and 4").  Channels 1 and 2
         * read through the XOR with their high-pass latch, which is held
         * at 1 while the filter is off, hence off; 3 and 4 are on.
         * Verified on the schematic (PokeyReSchem p.5): each channel's
         * output is a NOR/inverter dynamic latch; rstAudPhase is an async
         * input to that NOR, forcing its node to 1.  Channels 3/4 feed
         * that node straight into the Cell 11 DAC's IN (on); channels 1/2
         * go through an inverter and an XNOR with the high-pass latch,
         * whose preset is the filter-disable bit (off).  MAME sets
         * m_output = 0 with filter_sample 1/1/0/0, which is the inverse. */
        p->audio_reset_delay = 2;   /* applied at the end of the second clock after the write; see c012294.h */
        /* HRM 5.3: T4 is the last STIMER strobe that preempts the T8
         * IRQ for fast AUDF=0. Only the just-entered borrow stage is
         * cancelable; preserve interrupts further down the pipeline. */
        for (int w = 0; w < 3; ++w) {
            if (p->timer_irq_delay[w] == TIMER_IRQ_STAGE_DELAY) {
                p->timer_irq_pending &= (uint8_t)~timer_irq_bit(w);
                p->timer_irq_delay[w] = 0;
            }
        }
        rearm_timer(p, 0); rearm_timer(p, 1); rearm_timer(p, 2); rearm_timer(p, 3);
        break;

    case W_SKCTL:
        /* A rewrite of the current value is a no-op.  The init bits
         * change what RANDOM takes from the next clock on, and reset the
         * shared 15/64KHz source-clock phase.  Crucially, init does NOT
         * reset the timer counters themselves (Altirra HRM 5.2): entering
         * init saves how many source pulses each slow timer still needs,
         * and leaving init re-anchors that count to the freshly reset clock
         * phase. The polynomial registers instead shift zeros in gradually;
         * they are not reseeded by this write. */
        if (v == p->SKCTL)
            break;
        {
            const bool was_running = p->rng_enabled != 0;
            const bool now_running = (v & SK_INIT) != 0;

            if (was_running && !now_running)
                slow_clocks_enter_init(p);

            p->SKCTL = v;
            if (!(v & SK_KEYSCAN))
                keyboard_scan_disable(p);
            /* HRM 5.6: clock select 000 resets both serial clock dividers,
             * independently of the SKCTL initialization bits. */
            if (!(v & 0x70)) {
                p->clock_out_phase = p->clock_bi_phase = 0;
                p->serial_output_delay = 0;
            }
            /* HRM 5.6: the two-tone divide-by-two "is reset to 0 when
             * two-tone mode is disabled" */
            if (!(v & SK_TWOTONE)) p->cassette_level = 0;
            p->rng_enabled = now_running ? 1 : 0;

            if (!was_running && now_running)
                slow_clocks_leave_init(p);
        }
        if (!p->rng_enabled) {
            /* The 4/5-bit polys are not zeroed here: Init walks zeros into
             * them over the next 4/5 clocks (poly45_step()).
             * Init also resets both serial state machines (SER_core.v's
             * istate/ostate): a frame in flight is abandoned and the
             * data register counts as taken.  The output shift register is
             * stopped, NOT cleared: DATA OUT stays at the last bit shifted
             * out, which can leave the line stuck low and decides which
             * timer two-tone mode then listens to (Altirra HRM 5.6, "Serial
             * port reset"; Altirra pokey.cpp's Init branch: "does NOT reset
             * the output shift register ... the output state is preserved,
             * which is important for two-tone mode"). */
            if (p->sdo_busy)
                p->sdo_idle_level = sdo_shift_level(p);
            p->sdi_busy = false;
            p->sdo_busy = false;
            p->sdo_pending = false;
            /* The byte-only source has no independent external clock.
             * Abandon its partial frame rather than replay it on release.
             * A host-driven physical line is never changed here. */
            p->sdi_src_busy = false;
            p->sdi_src_left = 0;
            if (!p->sdi_host_line) p->sdi_line_in = 1;
            p->serial_output_delay = 0;
        }
        break;

    case W_POTGO:
        p->pot_scanning = true;
        p->pot_scan_ever = true;
        p->pot_scan_start = p->cycles;
        p->pot_clear_delay = 1;
        p->pot_finish_delay = 0;
        p->pot_transition = false;
        break;

    case W_SEROUT:
        /* Into the output data register; the shifter takes it at its
         * next bit clock (serial_step()).  A write over a byte the
         * shifter has not taken yet replaces it, as on the chip. */
        p->SEROUT = v;
        /* Init holds the valid flip-flop reset, not the data register. */
        p->sdo_pending = p->rng_enabled != 0;
        break;

    case W_IRQEN: {
        /* Clear any pending IRQST bits being disabled, then set the
         * mask.  No timer scheduling here - the countdowns in
         * ad_pokey_advance() run unconditionally; IRQEN only gates
         * whether a borrow reaches fire_irq().  Bit 3 is a level, not a
         * latch: enabling it while the transmitter is idle asserts the
         * IRQ at once. */
        const uint8_t was = p->IRQEN;
        if (p->IRQST & (uint8_t)~v)
            p->IRQST &= v;
        p->IRQEN = v;
        if ((v & ~was & IRQ_SEROC) && sdo_idle(p) && p->host && p->host->raise_irq)
            p->host->raise_irq(p->host->ctx, IRQ_SEROC);
        break;
    }

    case W_SKREST:
        p->st_latch = 0;
        break;

    default:
        break;
    }
    notify_pins(p);
    notify_clocks(p, false);
}

/* ------------------------------------------------------------------ */
/* Reads                                                               */
/* ------------------------------------------------------------------ */

/* RANDOM: the complement of the chain's 9-bit register (poly_core.v's
 * rndNum = ~lfsr9bit), which machine clock advancement moves (see
 * c012294.h's time-model note).  The read charges nothing, so a caller
 * that knows the 6502 cycle distance to its previous read advances by
 * exactly that first, and back-to-back reads with no machine time
 * between them return the same byte.  Whichever poly AUDCTL selects,
 * the byte comes from the same eight flops; the select only changes
 * what feeds them.  A chip held in reset reads 0xFF once eight clocks
 * of zeros have shifted in, and before that the tail of what it was
 * doing. */
static uint8_t read_random(const ad_pokey *p)
{
    return (uint8_t)~p->rng.l9;
}

/* Digital POT model: ALLPOT follows the host-supplied comparator mask
 * until the scan's terminal count, then returns zero until the next POTGO.
 * With no scan active it reads $00 - before the first POTGO as well as after
 * a finished scan - as Altirra has it (pokey.cpp: mALLPOT = 0 at cold reset
 * and whenever no scan is active).  D25, 2026-09-21: until then the core
 * returned the mask before the first POTGO, a tuning for arcade DIP banks
 * (Asteroids Deluxe test mode).  The arcade code read so far does not need
 * it: Space Duel writes POTGO and reads ALLPOT four cycles later every time
 * ($76DC, $4218, $8492, $84A5), with SKCTL = $07 - fast pot scan, which also
 * turns the dump transistors off so the pins are plain switch inputs.  NOT
 * measured; the Space Duel board can measure it (its DIPs are on these pins).
 * POT0-7 read host callbacks; independent analog paddle
 * counters and per-input completion are not implemented.
 *
 * Terminal counts follow Altirra HRM 5.9: 228 slow counts or 229 fast
 * counts. The host owns DIP polarity and calls ad_pokey_set_allpot(). */

static uint8_t allpot_read(ad_pokey *p)
{
    if (p->pot_counter_mode)
        return p->pot_scanning ? p->allpot : 0;
    if (p->pot_scanning) {
        const uint64_t elapsed = p->cycles - p->pot_scan_start;
        /* The scan ends at the counter's terminal count: 228 in slow
         * mode, one count per 114-cycle scan line; 229 in fast mode, one
         * count per machine cycle (the counter stops one value higher in
         * fast mode - Altirra HRM 5.9). All durations are POKEY cycles. */
        const uint64_t need = (p->SKCTL & SK_FASTPOT) ? 229u : (228u * DIV_15);
        if (elapsed >= need)
            p->pot_scanning = false;
    }
    /* ALLPOT is not a latch during a scan: it follows the pins live, so
     * a line that trips reads 0 and one that drops back below threshold
     * reads 1 again (POT0-7 counters are not modelled).  The host keeps `allpot` at the pins' current
     * grounded-line mask with ad_pokey_set_allpot().  Only the finished
     * scan is a latch, forced to 0 until the next POTGO. */
    if (!p->pot_scanning)
        return 0x00;           /* no scan active - none yet, or complete: 0 */
    return p->allpot;          /* mid-scan: the comparators' mask, live */
}

uint8_t ad_pokey_read(ad_pokey *p, uint8_t reg)
{
    const uint8_t a = reg & 0x0F;
    switch (a) {
    case R_RANDOM: return read_random(p);
    case R_ALLPOT: return allpot_read(p);
    case R_IRQST: {
        /* Pending IRQs read as 0.  Bit 3 is not a latch: it is low
         * whenever the transmitter is idle, whatever IRQEN says. */
        uint8_t v = (uint8_t)(p->IRQST ^ 0xFF);
        if (sdo_idle(p)) v &= (uint8_t)~IRQ_SEROC; else v |= IRQ_SEROC;
        return v;
    }
    case R_SKSTAT: return skstat_read(p);
    case R_KBCODE: return p->KBCODE;
    case R_SERIN:  return p->SERIN;
    default:
        /* POT0-7 share offsets 0x00-0x07 with AUDF1-4/AUDC1-4's write
         * side; on the read side they are the only registers there. */
        if (a <= R_POT0 + 7) {
            if (p->pot_counter_mode) {
                if (p->pot_scanning && (p->allpot & (1u << a)) && p->pot_transition)
                    return p->pot_count & p->pot_previous;
                return p->pot_latch[a];
            }
            if (p->host && p->host->pot_read)
                return (uint8_t)p->host->pot_read(p->host->ctx, a);
            return 0xFF;   /* AAE's default with no pot handler wired up */
        }
        return 0xFF;
    }
}

/* Keyboard: see keyboard_scan_step() - every key reaches KBCODE through
 * the chip's own matrix scanner (D19 removed the host event shortcut). */

/* ------------------------------------------------------------------ */
/* Serial port                                                         */
/* ------------------------------------------------------------------ */
/* SER_core.v, a byte at a time.  Each direction is a ten-stage shift
 * register (start bit, eight data bits LSB first, stop bit) clocked by
 * a flop that toggles on a timer's borrow, so a bit is two borrows and
 * a frame twenty.  SKCTL bits 4..6 pick the timers: the receiver clocks
 * from timer 4 unless bits 5 and 4 are both clear (the external bit
 * clock), the transmitter from timer 2 with bits 6 and 5 set, timer 4
 * with either alone, the external clock with both clear. External edges
 * are supplied by ad_pokey_serial_clock().
 *
 * Transmit: the shifter takes the data register at its next bit clock
 * and that load is the "output data needed" event (IRQ bit 4); twenty
 * borrows later the stop bit has left the pin and the byte goes to the
 * host.  A second SEROUT during a frame waits in the data register and
 * loads straight after, so a stream is gapless.  "Transmission
 * finished" (IRQ bit 3) is the idle level, see sdo_idle().
 *
 * Receive: a start bit is offered whenever the receiver is idle - the
 * host's serial_in() from ad_pokey_advance(), or ad_pokey_serial_
 * receive() directly.  Outside of that, asynchronous mode (SKCTL bit 4)
 * holds timers 3+4 (tcnt[2]) in reset - see timers_running() and
 * ad_pokey_advance() - so they only start counting once a start bit
 * arrives, which is when sdi_start() resyncs them (Altirra HRM 5.6,
 * "Asynchronous receive mode"). Nineteen borrows later the stop bit is
 * sampled: SERIN takes the byte, the input IRQ (bit 5) fires, and if
 * that IRQ was still pending from the previous byte the overrun latch
 * sets (IRQ_core.v: sdiOvrun = setSdiCompl & the pending latch).
 * SKSTAT's busy bit is low from the start bit to the stop bit, and its
 * serial-data bit shows the raw line. The raw-line interface can supply a
 * zero stop bit to exercise framing errors. Two-tone resync and force break
 * are handled in advance and sdo_line. Optional IO and clock callbacks
 * expose DATA OUT and logical clock-pin transitions to the host. */

enum { SER_FRAME_BORROWS = 20 };
/* The RECEIVER is done one borrow sooner: it samples the stop bit in the
 * middle of its cell and finishes there, without waiting the cell out.
 * Altirra HRM 5.6: "19 timer periods or 9 1/2 bits later, the stop bit is
 * sampled, SERIN is updated with the new data byte, the serial input ready
 * IRQ is asserted, the overrun and framing error bits in SKSTAT are updated,
 * and timers 3+4 are stopped."  Altirra pokey.cpp: mSerialInputCounter = 19.
 * It is also why asynchronous receive gives the audio circuit an ODD number
 * of pulses a byte - the tone heard during a disk read (HRM 5.6).  The 20
 * above is the transmitter's, pinned by acid800 pokey_serclock. */
enum { SER_RECV_BORROWS = 19 };

static int sdi_timer(const ad_pokey *p)
{
    return (p->SKCTL & 0x30) ? 2 : -1;
}

static int sdo_timer(const ad_pokey *p)
{
    switch (p->SKCTL & 0x60) {
    case 0x00: return -1;
    case 0x60: return 1;
    default:   return 2;
    }
}

/* IRQST bit 3 (SEROC) follows the SHIFT REGISTER only - a byte merely waiting
 * in SEROUT does not clear it.  HRM 5.6: "The serial output complete IRQ
 * (IRQEN/ST bit 3) is asserted whenever the output shift register is idle",
 * and its warning: "there is a delay from the first write to SEROUT until the
 * serial output ready/complete IRQs update".  HRM 5.7 repeats it: the complete
 * IRQ "deasserts automatically once a new byte is loaded into the output shift
 * register and there is a delay from when SEROUT is written to when this
 * occurs."  Counting sdo_pending here made acid800 pokey_serclock's last check
 * fail - with the external clock selected and nothing driving it, the byte can
 * never load, so IRQST must read $F7 and not $FF. */
static bool sdo_idle(const ad_pokey *p)
{
    return !p->sdo_busy;
}

/* Where a frame's line sits with `left` borrows still to go: two borrows to a
 * bit cell, cell 0 the start bit, 1..8 the data bits LSB first, 9 the stop. */
static bool frame_line_level(uint8_t byte, uint32_t left)
{
    const uint32_t bit = (SER_FRAME_BORROWS - left) / 2;
    if (bit == 0) return false;                      /* start bit */
    if (bit >= 9) return true;                       /* stop bit */
    return ((byte >> (bit - 1)) & 1u) != 0;
}

/* The output data bit currently on the wire - same 20-borrow/10-bit
 * framing as frame_line_level(), for the SEROUT shifter instead of the input
 * one.  Used only by two-tone mode's timer select: mark (true) when idle
 * or on the stop bit, space (false) on the start bit, else the shifting
 * byte's bit, LSB first. */
/* The bit the output shifter has on the pin, mid-frame. */
static bool sdo_shift_level(const ad_pokey *p)
{
    const uint32_t bit = (SER_FRAME_BORROWS - p->sdo_left) / 2;
    if (bit == 0) return false;                       /* start bit */
    if (bit >= 9) return true;                        /* stop bit */
    return ((p->sdo_byte >> (bit - 1)) & 1u) != 0;
}

static bool sdo_line(const ad_pokey *p)
{
    /* HRM 5.6: force break selects the zero-bit tone in two-tone mode.
     * In ordinary serial mode it forces DATA OUT low. The shifter itself
     * keeps running; only its output is overridden. */
    if (p->SKCTL & SK_BREAKEN)
        return false;
    if (!p->sdo_busy)
        return p->sdo_idle_level;
    return sdo_shift_level(p);
}

static uint8_t skstat_read(const ad_pokey *p)
{
    uint8_t v = ST_ALWAYS_ONE;
    if (!(p->st_latch & ST_FRAME))   v |= ST_FRAME;
    if (!(p->st_latch & ST_OVERRUN)) v |= ST_OVERRUN;
    if (!(p->st_latch & ST_KBERR))   v |= ST_KBERR;
    /* Bit 4 is the RAW line, not something derived from the shift register.
     * HRM 5.6, "Direct input": it "bypasses all of the shifting and clocking
     * logic and ignores all serial input settings, working even if all clocks
     * are stopped." */
    if (p->sdi_line_in)              v |= ST_SERIN_DATA;
    if (!p->kb_shift)                v |= ST_SHIFT;
    if (!p->kb_down)                 v |= ST_KEYBD;
    if (!p->sdi_busy)                v |= ST_SERIN_BUSY;
    return v;
}

/* The internal byte -> line source.  A host that only has whole bytes hands
 * one over (serial_in / ad_pokey_serial_receive) and POKEY walks it onto the
 * line itself at the receive clock, so the line always tells the same story as
 * the shift register that is reading it.  A host that shifts its own bits uses
 * ad_pokey_serial_line() instead and this stands aside. */
static void sdi_source_offer(ad_pokey *p, uint8_t data)
{
    p->sdi_src_busy = true;
    p->sdi_src_byte = data;
    p->sdi_src_left = SER_FRAME_BORROWS;
    p->sdi_line_in  = 0;                 /* the start bit, from this cycle on */
}

static void sdi_source_step(ad_pokey *p, uint32_t borrows)
{
    if (p->sdi_host_line) return;        /* the host is driving the line */
    if (!p->sdi_src_busy) { p->sdi_line_in = 1; return; }

    while (borrows && p->sdi_src_left) { --p->sdi_src_left; --borrows; }

    if (p->sdi_src_left == 0) { p->sdi_src_busy = false; p->sdi_line_in = 1; }
    else p->sdi_line_in = frame_line_level(p->sdi_src_byte, p->sdi_src_left) ? 1 : 0;
}

static void sdi_complete(ad_pokey *p)
{
    p->sdi_busy = false;
    p->sdi_byte = p->sdi_shift;
    p->SERIN    = p->sdi_shift;
    /* "A framing error is detected if the stop bit is not high" - the latch
     * a byte interface could never set. */
    if (!p->sdi_stop_ok)
        p->st_latch |= ST_FRAME;
    if (p->IRQST & IRQ_SERIN)
        p->st_latch |= ST_OVERRUN;
    if (p->IRQEN & IRQ_SERIN)
        fire_irq(p, IRQ_SERIN);
    /* The byte source walks a whole 20-borrow frame onto the line, and the
     * receiver has just finished on the 19th with the line in its stop bit.
     * In asynchronous mode that stops timers 3+4, so the source would wait
     * for ever for a twentieth borrow: the line is already at mark, end it. */
    if (!p->sdi_host_line && p->sdi_src_busy && p->sdi_src_left <= 2) {
        p->sdi_src_busy = false;
        p->sdi_src_left = 0;
        p->sdi_line_in  = 1;
    }
}

/* One slice of the input shift register.  The start bit is an EDGE ON THE
 * LINE, not a byte handed over: the receiver watches for the line going low
 * while it is idle and clocked, then samples the middle of each bit cell -
 * borrow 3 for data bit 0, 5 for bit 1 ... 17 for bit 7, 19 for the stop bit,
 * where the frame also ends (SER_RECV_BORROWS).  Asynchronous mode holds
 * timers 3+4 in reset until that edge (HRM 5.6), which is what re-arms them
 * here. */
static void sdi_recv_step(ad_pokey *p, uint32_t borrows)
{
    if (!p->sdi_busy) {
        if (p->sdi_line_in)
            return;                      /* line at mark: no frame starting */
        p->sdi_busy    = true;
        p->sdi_left    = SER_RECV_BORROWS;
        p->sdi_shift   = 0;
        p->sdi_stop_ok = true;
        if (p->SKCTL & SK_ASYNC)
            rearm_timer(p, 2);
    }

    while (borrows--) {
        const uint32_t n = SER_RECV_BORROWS - p->sdi_left + 1;    /* 1-based */
        if ((n & 1u) && n >= 3 && n <= 17) {
            if (p->sdi_line_in)
                p->sdi_shift |= (uint8_t)(1u << ((n - 3) / 2));
        }
        else if (n == 19) {
            p->sdi_stop_ok = p->sdi_line_in != 0;
        }
        if (--p->sdi_left == 0) { sdi_complete(p); break; }
    }
}

/* One slice of serial time: borrows[w] is how many times timer w
 * borrowed in the slice ad_pokey_advance() just walked. */
static void serial_edges(ad_pokey *p, int ti, int to, const uint32_t *borrows, bool external)
{
    /* HRM 5.2: Init continuously holds both serial state machines reset.
     * Fast timers and clock dividers can still run; only their serial
     * consumer is held. Do not poll/consume a byte-only host while held. */
    if (!p->rng_enabled) return;
    if (ti >= 0) {
        /* Ask a byte-only host for the next frame before moving the line, so
         * the start bit is on the wire in the same cycle it is offered. */
        if (!p->sdi_host_line && !p->sdi_src_busy && !p->sdi_busy &&
            p->host && p->host->serial_in) {
            int b = p->host->serial_in(p->host->ctx);
            if (b >= 0)
                sdi_source_offer(p, (uint8_t)b);
        }
        if (external) {
            sdi_recv_step(p, borrows[ti]);
            sdi_source_step(p, borrows[ti]);
        } else {
            sdi_source_step(p, borrows[ti]);
            sdi_recv_step(p, borrows[ti]);
        }
    }

    if (to >= 0) {
        /* One bit-cell edge at a time.  The borrow that empties the shifter
         * is the SAME edge on which a waiting byte loads - the shifter "only
         * attempts to load once every bit cell time on the rising edge of the
         * serial clock" (HRM 5.6), and it is a 10-bit register, so a frame is
         * SER_FRAME_BORROWS edges and back-to-back loads are exactly that far
         * apart.  Giving the load an edge of its own on top of the frame made
         * a byte cost 21 edges, which acid800 pokey_serclock measures as
         * VCOUNT 42 where hardware gives 40.  Sharing the edge is also what
         * keeps bit 3 "inactive continuously while sending back-to-back
         * bytes": sdo_busy never drops between them. */
        for (uint32_t b = borrows[to]; b; --b) {
            bool went_idle = false;

            if (p->sdo_busy && --p->sdo_left == 0) {
                p->sdo_busy = false;
                p->sdo_idle_level = true;    /* it ended on its stop bit */
                went_idle = true;
                if (p->host && p->host->serial_out)
                    p->host->serial_out(p->host->ctx, p->sdo_byte);
            }

            /* Internal borrows are half-bit edges. serial_step has already
             * advanced the divider phase; a falling edge cannot load SEROUT.
             * External calls contain only rising bit-cell edges. */
            if (!p->sdo_busy && p->sdo_pending && (external || p->clock_out_phase)) {
                p->sdo_byte = p->SEROUT;
                p->sdo_pending = false;
                p->sdo_busy = true;
                p->sdo_left = SER_FRAME_BORROWS;
                went_idle = false;
                if (p->IRQEN & IRQ_SEROR)
                    fire_irq(p, IRQ_SEROR);
            }

            if (went_idle && (p->IRQEN & IRQ_SEROC) && p->host && p->host->raise_irq)
                p->host->raise_irq(p->host->ctx, IRQ_SEROC);

            if (!p->sdo_busy)
                break;                      /* nothing left to clock this slice */
        }
    }
}

static void serial_step(ad_pokey *p, const uint32_t *borrows, uint8_t timer_completed)
{
    /* HRM table 10: the output divider selects timer 2 or 4. The other
     * divider uses timer 4 and drives the bidirectional pin in modes 010
     * and 110. Clocking is independent of AUDC and of queued serial data. */
    /* No serial clock selected (SKCTL bits 6..4 clear): both dividers are
     * unassigned, nothing can shift, and the pins only follow the
     * external clock.  This is every arcade board and most of an Atari's
     * life, so take it in a few instructions.  Same effects as the full
     * path below with to = ti = -1: the output-action countdown still
     * retires (it is already zeroed by the SKCTL write that cleared the
     * bits, kept here for exact equivalence) and the pins are re-notified. */
    if (!(p->SKCTL & 0x70)) {
        if (p->serial_output_delay) --p->serial_output_delay;
        notify_clocks(p, false);
        return;
    }
    int to = sdo_timer(p);
    /* Our borrows[] are the early counter event, four clocks before the
     * timer IRQ stage. Output clocking uses the completed timer stage,
     * independent of IRQEN, followed by a two-clock serial action pipeline.
     * Acid800 sertiming brackets the resulting first load at STIMER+234
     * for period 228. Altirra FireTimer schedules SerialOutput two clocks
     * after its timer IRQ stage; the schematic transcription likewise has
     * separate divider, edge-detector and output-state stages.
     * Receive timing and external clock calls have separate paths. */
    if (p->serial_output_delay && --p->serial_output_delay == 0 && to >= 0) {
        uint32_t output_edge[4] = {0,0,0,0};
        output_edge[to] = 1;
        serial_edges(p, -1, to, output_edge, false);
    }
    if (to >= 0 && (timer_completed & timer_irq_bit(to))) {
        p->clock_out_phase ^= 1;
        p->serial_output_delay = 2;
    }
    if (p->SKCTL & 0x30) p->clock_bi_phase ^= (uint8_t)(borrows[2] & 1);
    serial_edges(p, sdi_timer(p), -1, borrows, false);
    notify_clocks(p, false);
}

void ad_pokey_serial_clock(ad_pokey *p, int high)
{
    uint8_t level = high ? 1 : 0;
    if (level == p->external_clock) return;
    p->external_clock = level;
    notify_clocks(p, false);
    if (!level || !p->rng_enabled) return;
    /* One rising external edge is a complete bit cell. The internal
     * timer interface uses two half-cell borrows per bit. */
    uint32_t edges[3] = { 2, p->sdo_busy ? 2u : 1u, 0 };
    serial_edges(p, sdi_timer(p) < 0 ? 0 : -1,
                    sdo_timer(p) < 0 ? 1 : -1, edges, true);
    notify_pins(p);
}

/* A byte from the host, outside the serial_in() poll: POKEY shifts it onto
 * the input line itself.  Dropped if a frame is already on the line, if the
 * host has taken the line over with ad_pokey_serial_line(), or if there is no
 * receive clock to shift it with. */
void ad_pokey_serial_receive(ad_pokey *p, uint8_t data)
{
    if (!p->rng_enabled || p->sdi_host_line || p->sdi_src_busy || p->sdi_busy || sdi_timer(p) < 0)
        return;
    sdi_source_offer(p, data);
}

/* Drive the serial input line directly, for a host that shifts its own bits.
 * From the first call POKEY stops synthesising the line from whole bytes; the
 * receiver reads what the host puts here, and so does SKSTAT bit 4 - "even if
 * all clocks are stopped" (HRM 5.6, "Direct input"), which is the case acid800
 * pokey_serdirect measures. */
void ad_pokey_serial_line(ad_pokey *p, int mark)
{
    p->sdi_host_line = 1;
    p->sdi_line_in   = mark ? 1u : 0u;
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */
/* Cycle audio: ad_pokey_advance() integrates the DAC level over every
 * half clock into a sample queue at sys_freq (cycle_audio_sample() and
 * friends below); ad_pokey_audio_read()/ad_pokey_render() drain it. */

/* Drop queued PCM and the integrator/DC-tracker state; the oscillators,
 * latches and registers are untouched. */
static void audio_queue_clear(ad_pokey *p)
{
    p->audio_phase = p->audio_dropped = 0;
    p->audio_area = 0;
    p->audio_head = p->audio_count = 0;
    p->audio_dc = 0;
}

/* The playback stage after the DAC curve: a one-pole DC block at dc_hz
 * (0 = none, the unipolar DAC signal) and a gain, both at sys_freq. */
/* Configuration time only: the two doubles become fixed-point constants
 * here, and nothing downstream touches floating point again.  gain is
 * capped at 4096x so the Q16 product in cycle_audio_integrate() cannot
 * overflow 64 bits. */
static void audio_playback_config(ad_pokey *p, double dc_hz, double gain)
{
    if (!(isfinite(gain) && gain >= 0)) gain = 1.0;
    if (gain > 4096.0) gain = 4096.0;
    p->audio_gain = (uint32_t)(gain * 65536.0 + 0.5);

    double decay = isfinite(dc_hz) && dc_hz > 0 ?
        exp(-6.283185307179586 * dc_hz / p->sys_freq) : 1.0;
    p->audio_dc_alpha = (uint32_t)((1.0 - decay) * 1073741824.0 + 0.5); /* Q30 */
}

void ad_pokey_audio_clear(ad_pokey *p)
{
    audio_queue_clear(p);
}

void ad_pokey_set_measured_audio(ad_pokey *p, double dc_hz, double gain)
{
    audio_playback_config(p, dc_hz, gain);
    audio_queue_clear(p);
}

uint32_t ad_pokey_audio_available(const ad_pokey *p) { return p->audio_count; }
uint64_t ad_pokey_audio_overruns(const ad_pokey *p) { return p->audio_dropped; }

int ad_pokey_audio_read(ad_pokey *p, int16_t *dst, int n)
{
    if (!dst || n <= 0) return 0;
    if ((uint32_t)n > p->audio_count) n = (int)p->audio_count;
    for (int i = 0; i < n; ++i) {
        dst[i] = p->audio_queue[p->audio_head];
        p->audio_head = (p->audio_head + 1) & (AD_POKEY_AUDIO_CAPACITY - 1);
    }
    p->audio_count -= n;
    return n;
}

/* The DAC level for the current channel outputs, latches and volumes:
 * each channel whose output is high adds its AUDC volume's weight (the
 * four measured bit drops .12/.26/.56/1.12 V in .02 V units, summed per
 * nibble), and the shared curve turns the total into PCM. */
static int32_t cycle_audio_level(const ad_pokey *p)
{
    static const uint8_t weights[16] = {
        0,6,13,19,28,34,41,47,56,62,69,75,84,90,97,103
    };
    unsigned sum = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t bit = p->out[i];
        if (i < 2) bit ^= p->highpass_latch[i];
        if (p->AUDC[i] & AUDC_VOLONLY) bit = 1;
        if (bit) sum += weights[p->AUDC[i] & AUDC_VOLMASK];
    }
    return g_dac_curve[sum];
}

static void audio_level_refresh(ad_pokey *p)
{
    p->audio_level = cycle_audio_level(p);
}

/* Integrate `level` over `halves` half clocks into the sample queue.
 * Half-cycle integration retains the 1.5-cycle high-pass delay. Each
 * half clock contributes sample_rate units to a 2*clock_hz sample. */
static void cycle_audio_integrate(ad_pokey *p, int32_t level, uint32_t halves)
{
    const uint64_t sample_span = (uint64_t)p->base_clock * 2;
    uint64_t remaining = (uint64_t)halves * p->sys_freq;
    while (remaining) {
        uint64_t span = sample_span - p->audio_phase;
        if (span > remaining) span = remaining;
        p->audio_area += (int64_t)level * (int64_t)span;
        p->audio_phase += span;
        remaining -= span;
        if (p->audio_phase == sample_span) {
            /* The bias rides through the nonlinear DAC and the integration;
             * only the playback stage removes DC and scales gain.  All
             * fixed point: raw is the sample mean in Q16.16 (rounded,
             * |area| <= 32767 * span so area * 65536 fits int64), the DC
             * tracker is a one-pole with its coefficient in Q30, and the
             * gain is Q16.16.  The result rounds to the nearest PCM step. */
            int64_t raw = p->audio_area * 65536;
            raw = raw >= 0 ? (raw + (int64_t)(sample_span >> 1)) / (int64_t)sample_span
                           : -((-raw + (int64_t)(sample_span >> 1)) / (int64_t)sample_span);
            int64_t diff = raw - p->audio_dc;
            int64_t output = (diff * (int64_t)p->audio_gain) >> 16;   /* Q16.16 */
            p->audio_dc += (diff * (int64_t)p->audio_dc_alpha) >> 30;
            output = (output + 32768) >> 16;                           /* to PCM */
            if (output > 32767) output = 32767;
            if (output < -32768) output = -32768;
            int64_t value = output;
            if (p->audio_count == AD_POKEY_AUDIO_CAPACITY) {
                p->audio_head = (p->audio_head + 1) & (AD_POKEY_AUDIO_CAPACITY - 1);
                --p->audio_count;
                ++p->audio_dropped;
            }
            p->audio_queue[(p->audio_head + p->audio_count++) & (AD_POKEY_AUDIO_CAPACITY - 1)] = (int16_t)value;
            p->audio_area = 0;
            p->audio_phase = 0;
        }
    }
}

/* One half clock of audio: integrate the current level, then retire the
 * high-pass delays (three half clocks from the clocking borrow). */
static void cycle_audio_sample(ad_pokey *p)
{
    cycle_audio_integrate(p, p->audio_level, 1);
    bool changed = false;
    for (int i = 0; i < 2; ++i)
        if (p->highpass_delay[i] && --p->highpass_delay[i] == 0) {
            changed |= p->highpass_latch[i] != p->out[i];
            p->highpass_latch[i] = p->out[i];
        }
    if (changed) audio_level_refresh(p);
}

/* One clock of the poly counters and the noise histories the channel
 * outputs sample at their borrows.  Bit N of each history is what
 * channel N+1 sees: the die delays all three poly outputs one clock per
 * channel. */
static void cycle_audio_noise_clock(ad_pokey *p)
{
    p->noise4_history = (uint8_t)((p->noise4_history << 1) | (p->p4 & 1u));
    p->noise5_history = (uint8_t)((p->noise5_history << 1) | (p->p5 & 1u));
    p->noise917_history = (uint8_t)((p->noise917_history << 1) | (p->rng.l9 & 1));
}

static inline void cycle_audio_step(ad_pokey *p, const uint32_t *borrows)
{
    cycle_audio_noise_clock(p);
    bool changed = false;

    /* Four-clock borrow-to-toggle pipeline (see audio_pipe in c012294.h). */
    const uint32_t raw = (borrows[0] & 1u) | (borrows[1] & 1u) << 1 | (borrows[2] & 1u) << 2 | (borrows[3] & 1u) << 3;
    p->audio_pipe = ((p->audio_pipe << 4) | raw) & 0xFFFFFu;
    const uint32_t due = (p->audio_pipe >> 16) & 0xFu;

    /* STIMER's output reset, three clocks after the write. */
    if (p->audio_reset_delay && --p->audio_reset_delay == 0) {
        for (int i = 0; i < 4; ++i) { changed |= p->out[i] != 1; p->out[i] = 1; }
    }

    for (int w = 0; w < 4; ++w) {
        if (!((due >> w) & 1u)) continue;
        int ch = timer_channel(w);
        uint8_t control = p->AUDC[ch];
        /* All three polys reach channel N through N one-clock delay
         * stages (PokeyReSchem p.5: a "2"/"1" pair per channel boundary on
         * each of poly4Out, poly5Out and poly17Out; the three unused tail
         * flops after channel 4 are their ends), hence `>> ch` on every
         * history.  tests/test_poly_delay.c pins this per channel. */
        /* The poly5 gate reaches each channel one clock later than the
         * poly4/poly17 bits do (hence `>> (ch + 1)`): Altirra's measured
         * init offsets (pokeyrenderer.cpp, "specifically set so that the
         * audio output patterns are correctly timed") place poly4 and the
         * 9/17 bit exactly where the die's register structure puts them,
         * and the poly5 gate one clock later.  The schematic shows the
         * extra stage: poly5Out leaves its register through an output
         * inverter (a dynamic stage) that poly4Out/poly17Out do not have. */
        if ((control & AUDC_NOTPOLY5) || ((p->noise5_history >> (ch + 1)) & 1)) {
            uint8_t was = p->out[ch];
            if (control & AUDC_PURE) p->out[ch] ^= 1;
            else if (control & AUDC_POLY4) p->out[ch] = (p->noise4_history >> ch) & 1;
            else p->out[ch] = (p->noise917_history >> ch) & 1;
            changed |= p->out[ch] != was;
        }
    }
    /* High-pass latches.  The latch samples and applies the channel's
     * output 1.5 clocks BEFORE the filter clock's own output toggles
     * (Altirra pokeyrenderer.cpp: the high-pass update runs two clocks
     * ahead of the audio output plus half a clock, "so it's a half cycle
     * earlier than the output flip/flop ... on real hardware, HP never
     * updates at the same time; it's either one half clock earlier or
     * late", and "if 1+3 or 2+4 fire at the same time, the high pass is
     * updated with the output state from the last cycle").  With the
     * filter clock's raw borrow at clock t and its toggle at t+4, the
     * latch takes the channel's output as it stands at t+2.5: five half
     * clocks from here, sampled at expiry in cycle_audio_sample(). */
    for (int i = 0; i < 2; ++i) {
        if (!(p->AUDCTL & (i ? CTL_CH2_FILTER : CTL_CH1_FILTER))) {
            changed |= p->highpass_latch[i] != 1;
            p->highpass_latch[i] = 1;
            p->highpass_delay[i] = 0;
        } else if (borrows[i ? 2 : 3]) {
            p->highpass_delay[i] = 5;
        }
    }
    if (changed) audio_level_refresh(p);
}

void ad_pokey_render(ad_pokey *p, int16_t *dst, int n)
{
    if (!dst || n <= 0)
        return;
    int got = ad_pokey_audio_read(p, dst, n);
    memset(dst + got, 0, (size_t)(n - got) * sizeof *dst);
}
