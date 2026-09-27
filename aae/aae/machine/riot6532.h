// -----------------------------------------------------------------------------
// MOS/Synertek 6532 RIOT (RAM-I/O-Timer)
//
// Ported from the M.A.M.E.(TM) 0.159 riot6532_device (src/emu/machine/6532riot.c
// /.h). Register decode, port I/O, PA7 edge-detect IRQ and the timer's
// count-down / underflow-latch / free-run-at-/1 behaviour all follow that
// reference exactly; only the host integration (callback shape, timer backing)
// is AAE's.
//
// Portions remain copyright the original MAME authors and contributors; this
// file is distributed under the GNU General Public License v3 or later, the
// same terms as the rest of AAE. See cpu_control.h for the full notice.
//
// Design notes:
//   - This is a CLASS, not a singleton: a driver owns one instance per chip on
//     the board (Star Wars/ESB/TomCat have exactly one). No global state lives
//     inside the class itself.
//   - The 128 bytes of RAM MAME lumps into the same address decode is NOT part
//     of this device - callers map it separately as plain RAM (AAE's Star Wars
//     driver already does: 0x1000-0x107f).
//   - The timer is backed by one of AAE's timer_alloc() slots, on whichever CPU
//     the chip is clocked from (call init() with that CPU index - Star Wars'
//     RIOT is clocked from, and read by, the audio CPU, CPU 1). init() must run
//     AFTER timer_init() has wiped the timer store for this driver load, the
//     same rule every other timer_alloc() caller follows.
//   - IRQ callback: MAME's riot6532_device only calls the IRQ line callback on
//     an EDGE (m_irq changing state), because MAME's CPU interrupt lines are
//     held levels. AAE's 6809 core's irq_line() is an edge-triggered LATCH that
//     the core itself lowers the instant it takes the interrupt, so a held-but-
//     unchanging RIOT IRQ condition would only hit the CPU once and then go
//     stale. To stay faithful to "the RIOT keeps demanding service until the
//     flag that caused it is cleared", update_irqstate() here re-invokes the
//     callback with state=1 on EVERY evaluation where the condition holds, not
//     only on the low-to-high edge; the high-to-low edge is still reported
//     exactly once, as in MAME. See update_irqstate() in the .cpp.
// -----------------------------------------------------------------------------

#ifndef RIOT6532_AAE_H
#define RIOT6532_AAE_H

#pragma once

#include "deftypes.h"   // UINT8/UINT32

class riot6532_device
{
public:
    riot6532_device() = default;

    // Wire the chip up and allocate its AAE timer slot. `cpu` is the CPU index
    // whose cycle stream clocks the chip (and drives timer_update() for it);
    // `clock` is that CPU's frequency in Hz. Must be called after timer_init()
    // has run for this driver load. Calls reset() internally.
    void init(int cpu, UINT32 clock);

    // Power-on / machine-reset state: all registers and the DDRs clear, IRQ
    // enables clear, PA7 edge-detect clears, and the timer restarts counting
    // down from 0xFF at /1024 (MAME's reset default), matching
    // riot6532_device::device_reset(). Safe to call repeatedly (e.g. from a
    // driver's machine-reset hook); does not touch the wired-up callbacks.
    void reset();

    // ---- CPU side -----------------------------------------------------------
    // `offset` is the low 5 address bits (A4-A0); mirrors work unmasked at the
    // call site same as MAME's AM_RANGE/AM_MASK.
    UINT8 reg_r(UINT8 offset);
    void  reg_w(UINT8 offset, UINT8 data);

    // ---- Peripheral side: push/read port A / B pin levels --------------------
    void porta_in_set(UINT8 data, UINT8 mask);
    void portb_in_set(UINT8 data, UINT8 mask);

    UINT8 porta_in_get()  const { return m_port[0].m_in; }
    UINT8 portb_in_get()  const { return m_port[1].m_in; }
    UINT8 porta_out_get() const { return m_port[0].m_out; }
    UINT8 portb_out_get() const { return m_port[1].m_out; }

    // ---- Wiring ---------------------------------------------------------------
    // Every callback is optional (leave null); ports simply read back whatever
    // was last pushed with porta_in_set()/portb_in_set() (or 0) when no input
    // callback is wired, matching MAME's devcb_read8 with a null resolve.
    void set_in_pa_cb(UINT8 (*cb)())      { m_in_pa_cb = cb; }
    void set_out_pa_cb(void (*cb)(UINT8)) { m_out_pa_cb = cb; }
    void set_in_pb_cb(UINT8 (*cb)())      { m_in_pb_cb = cb; }
    void set_out_pb_cb(void (*cb)(UINT8)) { m_out_pb_cb = cb; }
    void set_irq_cb(void (*cb)(int))      { m_irq_cb = cb; }

private:
    riot6532_device(const riot6532_device&) = delete;
    riot6532_device& operator=(const riot6532_device&) = delete;

    struct riot_port
    {
        UINT8 m_in = 0;
        UINT8 m_out = 0;
        UINT8 m_ddr = 0;
    };

    enum { TIMER_IDLE, TIMER_COUNTING, TIMER_FINISHING };

    void  update_irqstate();
    void  update_pa7_state();
    static UINT8 apply_ddr(const riot_port& p) { return (UINT8)((p.m_out & p.m_ddr) | (p.m_in & ~p.m_ddr)); }
    UINT8 get_timer() const;
    UINT32 ticks_remaining() const;

    // AAE timer callback target (bound in init()).
    void  timer_end(int param);

    riot_port m_port[2];

    UINT8 (*m_in_pa_cb)() = nullptr;
    void  (*m_out_pa_cb)(UINT8) = nullptr;
    UINT8 (*m_in_pb_cb)() = nullptr;
    void  (*m_out_pb_cb)(UINT8) = nullptr;
    void  (*m_irq_cb)(int) = nullptr;

    UINT8 m_irqstate = 0;
    UINT8 m_irqenable = 0;
    int   m_irq = 0;

    UINT8 m_pa7dir = 0;    // 0x80 = high-to-low, 0x00 = low-to-high
    UINT8 m_pa7prev = 0;

    UINT8 m_timershift = 10;
    int   m_timerstate = TIMER_IDLE;

    int    m_cpu = 0;
    UINT32 m_clock = 0;
    int    m_timer_id = -1;
};

#endif // RIOT6532_AAE_H
