// -----------------------------------------------------------------------------
// Motorola MC6821 Peripheral Interface Adapter (PIA) - implementation
//
// See mc6821.h for design notes and the MAME attribution.
// -----------------------------------------------------------------------------

#include "mc6821.h"
#include "sys_log.h"

#include <vector>

// Control-register bit meanings. Both halves use the same layout, so these are
// written once and applied to either ctl_a or ctl_b.
//
//   bit 0    interrupt on C1 enabled
//   bit 1    C1 active transition: 1 = low-to-high, 0 = high-to-low
//   bit 2    0 = the data register selects DDR, 1 = it selects the port
//   bit 3    C2 input mode: interrupt enabled
//            C2 output set mode: the value to drive
//            C2 output strobe mode: 0 = released by C1, 1 = released by E
//   bit 4    C2 input mode: active transition, as bit 1
//            C2 output mode: 1 = set/reset, 0 = strobe
//   bit 5    0 = C2 is an input, 1 = C2 is an output
//   bit 6    IRQ2 flag (read only)
//   bit 7    IRQ1 flag (read only)
static inline bool irq1_enabled(uint8_t c)   { return (c & 0x01) != 0; }
static inline bool c1_low_to_high(uint8_t c) { return (c & 0x02) != 0; }
static inline bool c1_high_to_low(uint8_t c) { return (c & 0x02) == 0; }
static inline bool output_selected(uint8_t c){ return (c & 0x04) != 0; }
static inline bool irq2_enabled(uint8_t c)   { return (c & 0x08) != 0; }
static inline bool strobe_e_reset(uint8_t c) { return (c & 0x08) != 0; }
static inline bool strobe_c1_reset(uint8_t c){ return (c & 0x08) == 0; }
static inline bool set_c2(uint8_t c)         { return (c & 0x08) != 0; }
static inline bool c2_low_to_high(uint8_t c) { return (c & 0x10) != 0; }
static inline bool c2_high_to_low(uint8_t c) { return (c & 0x10) == 0; }
static inline bool c2_set_mode(uint8_t c)    { return (c & 0x10) != 0; }
static inline bool c2_strobe_mode(uint8_t c) { return (c & 0x10) == 0; }
static inline bool c2_output(uint8_t c)      { return (c & 0x20) != 0; }
static inline bool c2_input(uint8_t c)       { return (c & 0x20) == 0; }

static const uint8_t PIA_IRQ1 = 0x80;
static const uint8_t PIA_IRQ2 = 0x40;

// Boards wired for alternate ordering swap the control-A and port-B registers.
static const uint8_t swizzle_address[4] = { 0, 2, 1, 3 };

// Every live instance, for the wire-OR in drive_shared_irq().
static std::vector<mc6821*>& live_pias()
{
    static std::vector<mc6821*> v;
    return v;
}

// =============================================================================
// Construction / configuration / reset
// =============================================================================

mc6821::mc6821()
{
    live_pias().push_back(this);
    reset();
}

mc6821::~mc6821()
{
    std::vector<mc6821*>& v = live_pias();
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == this) { v.erase(v.begin() + i); break; }
    }
}

void mc6821::configure(const mc6821_interface& intf, int ordering)
{
    m_intf = intf;
    m_ordering = ordering;
    m_configured = true;
    reset();
}

void mc6821::reset()
{
    m_out_a = m_out_b = 0;
    m_ddr_a = m_ddr_b = 0;
    m_ctl_a = m_ctl_b = 0;
    m_out_ca2 = m_out_cb2 = 0;
    m_irq_a1 = m_irq_a2 = m_irq_b1 = m_irq_b2 = false;
    m_irq_a_state = m_irq_b_state = 0;

    // Port A pins have internal pull-ups and read high when undriven; port B
    // pins are three-state and read low.
    m_in_a = 0xFF;
    m_in_ca1 = m_in_ca2 = 1;
    m_in_b = 0;
    m_in_cb1 = m_in_cb2 = 0;
}

// =============================================================================
// Pin-value resolution
// =============================================================================

uint8_t mc6821::in_a_value()
{
    if (m_intf.in_a) m_in_a = m_intf.in_a();
    return m_in_a;
}

uint8_t mc6821::in_b_value()
{
    if (m_intf.in_b) m_in_b = m_intf.in_b();
    return m_in_b;
}

// What port A is presenting: the output register on pins the DDR drives, the
// pin level on the rest.
uint8_t mc6821::out_a_value()
{
    if (m_ddr_a == 0xFF) return m_out_a;
    return (uint8_t)((m_out_a & m_ddr_a) | (in_a_value() & (uint8_t)~m_ddr_a));
}

uint8_t mc6821::get_a() const
{
    return (uint8_t)((m_out_a & m_ddr_a) | (m_in_a & (uint8_t)~m_ddr_a));
}

uint8_t mc6821::get_b() const
{
    return (uint8_t)((m_out_b & m_ddr_b) | (m_in_b & (uint8_t)~m_ddr_b));
}

void mc6821::send_out_a()
{
    if (m_intf.out_a) m_intf.out_a(out_a_value());
}

void mc6821::send_out_b()
{
    if (m_intf.out_b) m_intf.out_b(out_b_value());
}

void mc6821::set_out_ca2(int state)
{
    state = state ? 1 : 0;
    if (m_out_ca2 == state) return;
    m_out_ca2 = state;
    if (m_intf.out_ca2) m_intf.out_ca2(state);
}

void mc6821::set_out_cb2(int state)
{
    state = state ? 1 : 0;
    if (m_out_cb2 == state) return;
    m_out_cb2 = state;
    if (m_intf.out_cb2) m_intf.out_cb2(state);
}

// =============================================================================
// Interrupts
// =============================================================================

// Drive `irq_func` from the OR of every live PIA that shares it. Without this a
// chip releasing its interrupt would clear a line another chip still holds.
void mc6821::drive_shared_irq(void (*irq_func)(int state))
{
    const std::vector<mc6821*>& v = live_pias();
    for (size_t i = 0; i < v.size(); ++i) {
        const mc6821* p = v[i];
        if (p->m_intf.irq_a == irq_func && p->m_irq_a_state) { irq_func(1); return; }
        if (p->m_intf.irq_b == irq_func && p->m_irq_b_state) { irq_func(1); return; }
    }
    irq_func(0);
}

void mc6821::update_interrupts()
{
    int new_state = ((m_irq_a1 && irq1_enabled(m_ctl_a)) ||
                     (m_irq_a2 && irq2_enabled(m_ctl_a))) ? 1 : 0;
    if (new_state != m_irq_a_state) {
        m_irq_a_state = new_state;
        if (m_intf.irq_a) drive_shared_irq(m_intf.irq_a);
    }

    new_state = ((m_irq_b1 && irq1_enabled(m_ctl_b)) ||
                 (m_irq_b2 && irq2_enabled(m_ctl_b))) ? 1 : 0;
    if (new_state != m_irq_b_state) {
        m_irq_b_state = new_state;
        if (m_intf.irq_b) drive_shared_irq(m_intf.irq_b);
    }
}

// =============================================================================
// CPU-side register access
// =============================================================================

uint8_t mc6821::read(uint32_t offset)
{
    int reg = (int)(offset & 3);
    if (m_ordering & ALTERNATE_ORDERING) reg = swizzle_address[reg];

    uint8_t val = 0;

    switch (reg)
    {
    case REG_PORTA:
        if (output_selected(m_ctl_a))
        {
            val = (uint8_t)((m_out_a & m_ddr_a) | (in_a_value() & (uint8_t)~m_ddr_a));

            // Reading the port clears both port A interrupt flags.
            m_irq_a1 = m_irq_a2 = false;
            update_interrupts();

            // In read-strobe mode the read itself pulses CA2 low; with E reset
            // selected it returns high again within the same cycle.
            if (c2_output(m_ctl_a) && c2_strobe_mode(m_ctl_a)) {
                set_out_ca2(0);
                if (strobe_e_reset(m_ctl_a)) set_out_ca2(1);
            }
        }
        else val = m_ddr_a;
        break;

    case REG_PORTB:
        if (output_selected(m_ctl_b))
        {
            val = (uint8_t)((m_out_b & m_ddr_b) | (in_b_value() & (uint8_t)~m_ddr_b));

            // A CB2 write-strobe with CB1 restore is released here, when the
            // read clears IRQ B1 -- not on the CB1 transition itself. This is
            // the port A / port B asymmetry noted in the header.
            if (m_irq_b1 && c2_output(m_ctl_b) && c2_strobe_mode(m_ctl_b) &&
                strobe_c1_reset(m_ctl_b))
                set_out_cb2(1);

            m_irq_b1 = m_irq_b2 = false;
            update_interrupts();
        }
        else val = m_ddr_b;
        break;

    case REG_CTLA:
        // Sampling the control register also samples CA1/CA2, which can raise
        // an interrupt flag before we read it out below.
        if (m_intf.in_ca1) set_ca1(m_intf.in_ca1());
        if (m_intf.in_ca2) set_ca2(m_intf.in_ca2());

        val = m_ctl_a;
        if (m_irq_a1) val |= PIA_IRQ1;
        if (m_irq_a2 && c2_input(m_ctl_a)) val |= PIA_IRQ2;
        break;

    case REG_CTLB:
        if (m_intf.in_cb1) set_cb1(m_intf.in_cb1());
        if (m_intf.in_cb2) set_cb2(m_intf.in_cb2());

        val = m_ctl_b;
        if (m_irq_b1) val |= PIA_IRQ1;
        if (m_irq_b2 && c2_input(m_ctl_b)) val |= PIA_IRQ2;
        break;
    }

    return val;
}

void mc6821::write(uint32_t offset, uint8_t data)
{
    int reg = (int)(offset & 3);
    if (m_ordering & ALTERNATE_ORDERING) reg = swizzle_address[reg];

    switch (reg)
    {
    case REG_PORTA:
        if (output_selected(m_ctl_a)) {
            // Store unmasked: the DDR can widen later and must then expose the
            // bits this write already latched.
            m_out_a = data;
            send_out_a();
        }
        else if (m_ddr_a != data) {
            m_ddr_a = data;
            send_out_a();       // a DDR change re-drives the port
        }
        break;

    case REG_PORTB:
        if (output_selected(m_ctl_b)) {
            m_out_b = data;
            send_out_b();

            // Writing the port pulses CB2 low in write-strobe mode; with E
            // reset selected it returns high again immediately.
            if (c2_output(m_ctl_b) && c2_strobe_mode(m_ctl_b)) {
                set_out_cb2(0);
                if (strobe_e_reset(m_ctl_b)) set_out_cb2(1);
            }
        }
        else if (m_ddr_b != data) {
            m_ddr_b = data;
            send_out_b();
        }
        break;

    case REG_CTLA:
        data &= 0x3F;           // bits 6-7 are the read-only IRQ flags
        if (c2_output(data)) {
            // Set/reset mode drives the bit directly; strobe mode idles high.
            const int level = c2_set_mode(data) ? (set_c2(data) ? 1 : 0) : 1;
            // Coming from input mode the pin was not being driven at all, so
            // announce the level even if it matches the stale output latch.
            if (c2_input(m_ctl_a)) {
                m_out_ca2 = level;
                if (m_intf.out_ca2) m_intf.out_ca2(level);
            }
            else set_out_ca2(level);
        }
        m_ctl_a = data;
        update_interrupts();
        break;

    case REG_CTLB:
        data &= 0x3F;
        if (c2_output(data)) {
            const int level = c2_set_mode(data) ? (set_c2(data) ? 1 : 0) : 1;
            if (c2_input(m_ctl_b)) {
                m_out_cb2 = level;
                if (m_intf.out_cb2) m_intf.out_cb2(level);
            }
            else set_out_cb2(level);
        }
        m_ctl_b = data;
        update_interrupts();
        break;
    }
}

// =============================================================================
// Peripheral-side inputs
// =============================================================================

void mc6821::set_a(uint8_t data) { m_in_a = data; }
void mc6821::set_b(uint8_t data) { m_in_b = data; }

void mc6821::set_ca1(int state)
{
    state = state ? 1 : 0;

    if (m_in_ca1 != state &&
        ((state && c1_low_to_high(m_ctl_a)) || (!state && c1_high_to_low(m_ctl_a))))
    {
        m_irq_a1 = true;
        update_interrupts();

        // A CA2 read-strobe with CA1 restore is released by this transition.
        if (c2_output(m_ctl_a) && c2_strobe_mode(m_ctl_a) && strobe_c1_reset(m_ctl_a))
            set_out_ca2(1);
    }
    m_in_ca1 = state;
}

void mc6821::set_ca2(int state)
{
    state = state ? 1 : 0;

    if (c2_input(m_ctl_a) && m_in_ca2 != state &&
        ((state && c2_low_to_high(m_ctl_a)) || (!state && c2_high_to_low(m_ctl_a))))
    {
        m_irq_a2 = true;
        update_interrupts();
    }
    m_in_ca2 = state;
}

void mc6821::set_cb1(int state)
{
    state = state ? 1 : 0;

    // Unlike CA1 this does not release a CB2 strobe; a read of port B does.
    if (m_in_cb1 != state &&
        ((state && c1_low_to_high(m_ctl_b)) || (!state && c1_high_to_low(m_ctl_b))))
    {
        m_irq_b1 = true;
        update_interrupts();
    }
    m_in_cb1 = state;
}

void mc6821::set_cb2(int state)
{
    state = state ? 1 : 0;

    if (c2_input(m_ctl_b) && m_in_cb2 != state &&
        ((state && c2_low_to_high(m_ctl_b)) || (!state && c2_high_to_low(m_ctl_b))))
    {
        m_irq_b2 = true;
        update_interrupts();
    }
    m_in_cb2 = state;
}

// =============================================================================
// Memory-map convenience layer
// =============================================================================

static mc6821* s_slots[MC6821_MAX_SLOTS];

void pia_attach(int slot, mc6821* dev)
{
    if (slot < 0 || slot >= MC6821_MAX_SLOTS) {
        LOG_ERROR("pia_attach: slot %d out of range", slot);
        return;
    }
    s_slots[slot] = dev;
}

mc6821* pia_slot(int slot)
{
    if (slot < 0 || slot >= MC6821_MAX_SLOTS) return nullptr;
    return s_slots[slot];
}

void pia_detach_all()
{
    for (int i = 0; i < MC6821_MAX_SLOTS; ++i) s_slots[i] = nullptr;
}

void pia_reset_all()
{
    for (int i = 0; i < MC6821_MAX_SLOTS; ++i)
        if (s_slots[i]) s_slots[i]->reset();
}

// A read of an unattached slot returns the open-bus-ish 0xFF rather than
// silently reading 0, and says so once per call site via the log.
static uint8_t slot_read(int slot, UINT32 offset)
{
    mc6821* dev = s_slots[slot];
    if (!dev) {
        LOG_ERROR("pia_%d_r: no PIA attached to slot %d", slot, slot);
        return 0xFF;
    }
    return dev->read(offset);
}

static void slot_write(int slot, UINT32 offset, UINT8 data)
{
    mc6821* dev = s_slots[slot];
    if (!dev) {
        LOG_ERROR("pia_%d_w: no PIA attached to slot %d", slot, slot);
        return;
    }
    dev->write(offset, data);
}

#define PIA_SLOT_HANDLERS(n)                                                       \
    UINT8 pia_##n##_r(UINT32 offset, struct MemoryReadByte* /*mem*/)               \
        { return slot_read(n, offset); }                                           \
    void  pia_##n##_w(UINT32 offset, UINT8 data, struct MemoryWriteByte* /*mem*/)  \
        { slot_write(n, offset, data); }

PIA_SLOT_HANDLERS(0)
PIA_SLOT_HANDLERS(1)
PIA_SLOT_HANDLERS(2)
PIA_SLOT_HANDLERS(3)
PIA_SLOT_HANDLERS(4)
PIA_SLOT_HANDLERS(5)
PIA_SLOT_HANDLERS(6)
PIA_SLOT_HANDLERS(7)

#undef PIA_SLOT_HANDLERS
