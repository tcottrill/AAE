// -----------------------------------------------------------------------------
// MOS/Synertek 6532 RIOT - implementation
//
// See riot6532.h for design notes and the MAME 0.159 attribution. Comments
// below call out anywhere this departs from emu/machine/6532riot.c and why.
// -----------------------------------------------------------------------------

#include "riot6532.h"
#include "timer.h"

namespace
{
    constexpr UINT8 TIMER_FLAG = 0x80;
    constexpr UINT8 PA7_FLAG   = 0x40;
}

// =============================================================================
// Setup
// =============================================================================

void riot6532_device::init(int cpu, UINT32 clock)
{
    m_cpu = cpu;
    m_clock = clock;
    // timer_alloc(callback, cpu) - AAE's MAME-style dormant timer slot, but
    // pinned to this device's own CPU rather than the CPU-0 default (see the
    // timer.h/.cpp overload added alongside this device).
    m_timer_id = timer_alloc([this](int param) { timer_end(param); }, cpu);
    reset();
}

void riot6532_device::reset()
{
    m_port[0] = riot_port();
    m_port[1] = riot_port();

    m_irqenable = 0;
    m_irqstate = 0;
    m_irq = 0;   // silent: matches MAME's m_irq starting CLEAR_LINE in the ctor

    m_pa7dir = 0;
    m_pa7prev = 0;

    m_timershift = 10;
    m_timerstate = TIMER_COUNTING;
    if (m_timer_id >= 0)
        timer_adjust(m_timer_id, TIME_IN_CYCLES(256u << m_timershift, m_cpu), 0, 0.0);
}

// =============================================================================
// Internals
// =============================================================================

void riot6532_device::update_irqstate()
{
    const int irq = (m_irqstate & m_irqenable) ? 1 : 0;

    if (irq)
    {
        // Re-assert on every evaluation while the condition holds - see the
        // header comment on why this differs from MAME's edge-only callback.
        if (m_irq_cb) m_irq_cb(1);
    }
    else if (m_irq != irq)
    {
        if (m_irq_cb) m_irq_cb(0);
    }
    m_irq = irq;
}

void riot6532_device::update_pa7_state()
{
    const UINT8 data = apply_ddr(m_port[0]) & 0x80;

    // if the state changed in the correct direction, set the PA7 flag and update IRQs
    if ((m_pa7prev ^ data) && (m_pa7dir ^ data) == 0)
    {
        m_irqstate |= PA7_FLAG;
        update_irqstate();
    }
    m_pa7prev = data;
}

// Ticks remaining on the live AAE timer slot, converted back from the seconds
// timer_timeleft() reports. Rounded rather than truncated (MAME truncates via
// attotime::as_ticks()); at RIOT-timer rates a rounding difference of at most
// one cycle is inaudible/inconsequential and avoids a chronic 1-tick low bias
// from floating point.
UINT32 riot6532_device::ticks_remaining() const
{
    if (m_timer_id < 0) return 0;
    double ticks = timer_timeleft(m_timer_id) * m_clock;
    if (ticks < 0.0) ticks = 0.0;
    return (UINT32)(ticks + 0.5);
}

UINT8 riot6532_device::get_timer() const
{
    // if idle, return 0
    if (m_timerstate == TIMER_IDLE)
        return 0;

    // if counting, return the number of ticks remaining
    if (m_timerstate == TIMER_COUNTING)
        return (UINT8)(ticks_remaining() >> m_timershift);

    // if finishing, return the number of ticks without the shift
    return (UINT8)ticks_remaining();
}

void riot6532_device::timer_end(int /*param*/)
{
    // if we finished counting, switch to the finishing state
    if (m_timerstate == TIMER_COUNTING)
    {
        m_timerstate = TIMER_FINISHING;
        timer_adjust(m_timer_id, TIME_IN_CYCLES(256, m_cpu), 0, 0.0);

        // signal timer IRQ as well
        m_irqstate |= TIMER_FLAG;
        update_irqstate();
    }
    // if we finished finishing, keep spinning (free-runs at /1 forever)
    else if (m_timerstate == TIMER_FINISHING)
    {
        timer_adjust(m_timer_id, TIME_IN_CYCLES(256, m_cpu), 0, 0.0);
    }
}

// =============================================================================
// I/O access
// =============================================================================

UINT8 riot6532_device::reg_r(UINT8 offset)
{
    UINT8 val = 0;

    // if A2 == 1 and A0 == 1, we are reading interrupt flags
    if ((offset & 0x05) == 0x05)
    {
        val = m_irqstate;

        // implicitly clears the PA7 flag
        m_irqstate &= ~PA7_FLAG;
        update_irqstate();
    }
    // if A2 == 1 and A0 == 0, we are reading the timer
    else if ((offset & 0x05) == 0x04)
    {
        val = get_timer();

        // A3 contains the timer IRQ enable
        if (offset & 8)
            m_irqenable |= TIMER_FLAG;
        else
            m_irqenable &= ~TIMER_FLAG;

        // implicitly clears the timer flag
        if (m_timerstate != TIMER_FINISHING || val != 0xff)
            m_irqstate &= ~TIMER_FLAG;
        update_irqstate();
    }
    // if A2 == 0 and A0 == anything, we are reading from ports
    else
    {
        // A1 selects the port
        riot_port& port = m_port[(offset >> 1) & 1];
        const bool is_porta = (&port == &m_port[0]);

        // if A0 == 1, we are reading the port's DDR
        if (offset & 1)
        {
            val = port.m_ddr;
        }
        // if A0 == 0, we are reading the port as an input
        else
        {
            if (is_porta)
            {
                if (m_in_pa_cb)
                {
                    port.m_in = m_in_pa_cb();
                    update_pa7_state();
                }
            }
            else
            {
                if (m_in_pb_cb)
                    port.m_in = m_in_pb_cb();
            }

            // apply the DDR to the result
            val = apply_ddr(port);
        }
    }
    return val;
}

void riot6532_device::reg_w(UINT8 offset, UINT8 data)
{
    // if A4 == 1 and A2 == 1, we are writing to the timer
    if ((offset & 0x14) == 0x14)
    {
        static const UINT8 timershift[4] = { 0, 3, 6, 10 };

        // A0-A1 contain the timer divisor
        m_timershift = timershift[offset & 3];

        // A3 contains the timer IRQ enable
        if (offset & 8)
            m_irqenable |= TIMER_FLAG;
        else
            m_irqenable &= ~TIMER_FLAG;

        // writes here clear the timer flag
        if (m_timerstate != TIMER_FINISHING || get_timer() != 0xff)
            m_irqstate &= ~TIMER_FLAG;
        update_irqstate();

        // update the timer
        m_timerstate = TIMER_COUNTING;
        const UINT32 target_ticks = 1u + ((UINT32)data << m_timershift);
        if (m_timer_id >= 0)
            timer_adjust(m_timer_id, TIME_IN_CYCLES(target_ticks, m_cpu), 0, 0.0);
    }
    // if A4 == 0 and A2 == 1, we are writing to the edge detect control
    else if ((offset & 0x14) == 0x04)
    {
        // A1 contains the PA7 IRQ enable
        if (offset & 2)
            m_irqenable |= PA7_FLAG;
        else
            m_irqenable &= ~PA7_FLAG;

        // A0 specifies the edge detect direction: 0=negative, 1=positive
        m_pa7dir = (offset & 1) << 7;
    }
    // if A4 == anything and A2 == 0, we are writing to the I/O section
    else
    {
        // A1 selects the port
        riot_port& port = m_port[(offset >> 1) & 1];
        const bool is_porta = (&port == &m_port[0]);

        // if A0 == 1, we are writing to the port's DDR
        if (offset & 1)
        {
            port.m_ddr = data;
        }
        // if A0 == 0, we are writing to the port's output
        else
        {
            port.m_out = data;
            if (is_porta) { if (m_out_pa_cb) m_out_pa_cb(data); }
            else          { if (m_out_pb_cb) m_out_pb_cb(data); }
        }

        // writes to port A need to update the PA7 state
        if (is_porta)
            update_pa7_state();
    }
}

void riot6532_device::porta_in_set(UINT8 data, UINT8 mask)
{
    m_port[0].m_in = (UINT8)((m_port[0].m_in & ~mask) | (data & mask));
    update_pa7_state();
}

void riot6532_device::portb_in_set(UINT8 data, UINT8 mask)
{
    m_port[1].m_in = (UINT8)((m_port[1].m_in & ~mask) | (data & mask));
}
