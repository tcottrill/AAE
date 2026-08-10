// -----------------------------------------------------------------------------
// Motorola MC6821 Peripheral Interface Adapter (PIA)
//
// Behaviour ported from the M.A.M.E.(TM) 6821pia module (the v0.109 C core, with
// the port-value and output-callback refinements from the modern device). The
// register semantics, control-line strobe modes and interrupt logic follow that
// reference; only the host integration is AAE's.
//
// Portions remain copyright the original MAME authors and contributors; this
// file is distributed under the GNU General Public License v3 or later, the
// same terms as the rest of AAE. See cpu_control.h for the full notice.
//
// Design notes:
//   - This is a CLASS, not an indexed array of chips: a driver declares as many
//     mc6821 objects as its board carries. Qix needs six, Williams up to four.
//   - The four registers decode from the low two address bits:
//       0 = port A / DDR A   (selected by bit 2 of control A)
//       1 = control A
//       2 = port B / DDR B   (selected by bit 2 of control B)
//       3 = control B
//     ALTERNATE_ORDERING swaps registers 1 and 2 for boards wired that way.
//   - Port A and port B are NOT symmetrical, and the differences are real
//     hardware, not simplifications:
//       * Port A input pins have internal pull-ups and read as 1 when nothing
//         drives them; port B pins are three-state and read as 0.
//       * The value handed to the port B output callback masks the input pins
//         to 0, because they are high-impedance; port A passes them through.
//       * A CB2 write-strobe with CB1 restore is released when a read of port B
//         clears the IRQ B1 flag, not when CB1 transitions. Port A releases CA2
//         on the CA1 transition itself.
//   - Interrupt outputs are wire-ORed: several PIAs on one board commonly share
//     a single CPU interrupt line. Instances register themselves on construction
//     so that a chip lowering its IRQ does not release a line another chip is
//     still holding. Callbacks are compared by pointer, so sharing a line means
//     passing the same irq_a/irq_b function.
//
// 2026-08-09  TC  Initial implementation.
// -----------------------------------------------------------------------------

#ifndef MC6821_AAE_H
#define MC6821_AAE_H

#pragma once

#include <cstdint>
#include "deftypes.h"   // UINT8/32, MemoryReadByte / MemoryWriteByte

// Peripheral-side wiring. Every entry is optional; leave a member null and the
// PIA falls back to the pin's unconnected behaviour (port A / CA1 / CA2 read as
// 1, port B / CB1 / CB2 read as 0, and outputs simply go nowhere).
//
// The input callbacks are polled at the moment the CPU reads the corresponding
// register, so a driver can return live input-port state without pushing it.
// Alternatively push values in with set_a() / set_ca1() / etc. and leave the
// callback null.
struct mc6821_interface
{
    uint8_t (*in_a)(void);        // port A pin levels
    uint8_t (*in_b)(void);        // port B pin levels
    int     (*in_ca1)(void);      // CA1 pin level (0/1)
    int     (*in_cb1)(void);      // CB1 pin level (0/1)
    int     (*in_ca2)(void);      // CA2 pin level (0/1), input mode only
    int     (*in_cb2)(void);      // CB2 pin level (0/1), input mode only
    void    (*out_a)(uint8_t data);   // port A drove a new value
    void    (*out_b)(uint8_t data);   // port B drove a new value
    void    (*out_ca2)(int state);    // CA2 output changed
    void    (*out_cb2)(int state);    // CB2 output changed
    void    (*irq_a)(int state);      // IRQA pin (active high here)
    void    (*irq_b)(int state);      // IRQB pin (active high here)
};

class mc6821
{
public:
    enum { STANDARD_ORDERING = 0, ALTERNATE_ORDERING = 1 };

    mc6821();
    ~mc6821();

    // Wire the chip up. Safe to call again to re-configure; implies reset().
    void configure(const mc6821_interface& intf, int ordering = STANDARD_ORDERING);

    // Power-on state: all registers cleared, port A inputs pulled high, port B
    // inputs floating low. Any value already pushed in with set_a()/set_b()/etc.
    // is discarded.
    void reset();

    // ---- CPU side ----------------------------------------------------------
    // `offset` is the register index; only the low two bits are used, so a
    // mirrored address range works without masking at the call site.
    uint8_t read(uint32_t offset);
    void    write(uint32_t offset, uint8_t data);

    // ---- Peripheral side: drive a pin --------------------------------------
    void set_a(uint8_t data);
    void set_b(uint8_t data);
    void set_ca1(int state);
    void set_ca2(int state);
    void set_cb1(int state);
    void set_cb2(int state);

    // ---- Peripheral side: sample a pin -------------------------------------
    // get_a() / get_b() return what the chip is presenting on the port pins,
    // combining the output register with the input pins per the DDR.
    uint8_t get_a() const;
    uint8_t get_b() const;
    int     get_ca2() const { return m_out_ca2; }
    int     get_cb2() const { return m_out_cb2; }

    uint8_t get_ddr_a() const { return m_ddr_a; }
    uint8_t get_ddr_b() const { return m_ddr_b; }
    int     get_irq_a() const { return m_irq_a_state; }
    int     get_irq_b() const { return m_irq_b_state; }

private:
    mc6821(const mc6821&) = delete;
    mc6821& operator=(const mc6821&) = delete;

    // Register indices after any ordering swizzle.
    enum { REG_PORTA = 0, REG_CTLA = 1, REG_PORTB = 2, REG_CTLB = 3 };

    mc6821_interface m_intf{};
    bool    m_configured = false;
    int     m_ordering = STANDARD_ORDERING;

    uint8_t m_in_a = 0xFF,  m_in_b = 0;
    uint8_t m_out_a = 0,    m_out_b = 0;
    uint8_t m_ddr_a = 0,    m_ddr_b = 0;
    uint8_t m_ctl_a = 0,    m_ctl_b = 0;
    int     m_in_ca1 = 1,   m_in_cb1 = 0;
    int     m_in_ca2 = 1,   m_in_cb2 = 0;
    int     m_out_ca2 = 0,  m_out_cb2 = 0;
    bool    m_irq_a1 = false, m_irq_a2 = false;
    bool    m_irq_b1 = false, m_irq_b2 = false;
    int     m_irq_a_state = 0, m_irq_b_state = 0;

    // Resolved pin values, polling the input callback when one is wired.
    uint8_t in_a_value();
    uint8_t in_b_value();

    // Values handed to the output callbacks (port B masks its input pins to 0).
    uint8_t out_a_value();
    uint8_t out_b_value() const { return (uint8_t)(m_out_b & m_ddr_b); }

    void send_out_a();
    void send_out_b();
    void set_out_ca2(int state);
    void set_out_cb2(int state);

    void update_interrupts();

    // Wire-OR bookkeeping across every live instance sharing a callback.
    static void drive_shared_irq(void (*irq_func)(int state));
};

// ---------------------------------------------------------------------------
// Memory-map convenience layer
//
// AAE's handler tables take plain function pointers, so a driver that wants to
// drop a PIA straight into a MEM_ADDR row attaches the instance to a slot and
// uses the matching thunk. Slots are independent of the objects themselves --
// a driver is free to ignore this entirely and call read()/write() from its own
// handlers instead. Eight slots because Qix carries six PIAs.
// ---------------------------------------------------------------------------
#define MC6821_MAX_SLOTS 8

void    pia_attach(int slot, mc6821* dev);
mc6821* pia_slot(int slot);
void    pia_detach_all();
void    pia_reset_all();

UINT8 pia_0_r(UINT32 offset, struct MemoryReadByte* mem);
UINT8 pia_1_r(UINT32 offset, struct MemoryReadByte* mem);
UINT8 pia_2_r(UINT32 offset, struct MemoryReadByte* mem);
UINT8 pia_3_r(UINT32 offset, struct MemoryReadByte* mem);
UINT8 pia_4_r(UINT32 offset, struct MemoryReadByte* mem);
UINT8 pia_5_r(UINT32 offset, struct MemoryReadByte* mem);
UINT8 pia_6_r(UINT32 offset, struct MemoryReadByte* mem);
UINT8 pia_7_r(UINT32 offset, struct MemoryReadByte* mem);

void pia_0_w(UINT32 offset, UINT8 data, struct MemoryWriteByte* mem);
void pia_1_w(UINT32 offset, UINT8 data, struct MemoryWriteByte* mem);
void pia_2_w(UINT32 offset, UINT8 data, struct MemoryWriteByte* mem);
void pia_3_w(UINT32 offset, UINT8 data, struct MemoryWriteByte* mem);
void pia_4_w(UINT32 offset, UINT8 data, struct MemoryWriteByte* mem);
void pia_5_w(UINT32 offset, UINT8 data, struct MemoryWriteByte* mem);
void pia_6_w(UINT32 offset, UINT8 data, struct MemoryWriteByte* mem);
void pia_7_w(UINT32 offset, UINT8 data, struct MemoryWriteByte* mem);

#endif // MC6821_AAE_H
