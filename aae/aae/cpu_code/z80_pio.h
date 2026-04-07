// ---------------------------------------------------------------------------
// z80_pio.h - Z80 PIO (Parallel I/O) Emulation for AAE
//
// Adapted from the original MAME Z80 PIO code (z80fmly.c) for use in the
// AAE emulator. Converted from C struct + global arrays into a C++ class
// that inherits from the z80_daisy_device interface for proper daisy chaining.
//
// THE Z80 PIO:
//
// The Z80 PIO provides two 8-bit parallel I/O ports (A and B), each with
// its own handshaking signals. Each port can operate in one of 4 modes:
//   Mode 0 - Output: Data is written to the port and latched.
//   Mode 1 - Input:  Data is read from the port.
//   Mode 2 - Bidirectional: Port A only. Uses Port B handshake lines.
//   Mode 3 - Bit control: Each bit individually configured as input or output.
//
// Each port can generate an interrupt when data is available (mode 0/1) or
// when the input matches a mask/condition (mode 3).
//
// USAGE IN A GAME DRIVER:
//
//   #include "z80_pio.h"
//
//   static z80_pio pio;
//
//   // In init_game():
//   z80_pio_config cfg;
//   cfg.port_a_read = my_port_a_read;    // optional
//   cfg.port_b_write = my_port_b_write;  // optional
//   pio.init(cfg);
//
//   // Add to daisy chain (after CTC if CTC has higher priority):
//   daisy.add(&pio);
//
//   // Wire PIO I/O ports. The PIO uses 4 addresses:
//   //   offset bit 0 = C/D select (0=data, 1=control)
//   //   offset bit 1 = A/B select (0=port A, 1=port B)
//   //
//   //   Address 0: Port A data
//   //   Address 1: Port A control
//   //   Address 2: Port B data
//   //   Address 3: Port B control
//   //
//   // Write: pio.write(offset, data);
//   // Read:  pio.read(offset);
//
//   // To inject data from external hardware into a PIO port:
//   pio.port_write(Z80_PIO_PORT_A, data);
//   pio.port_write(Z80_PIO_PORT_B, data);
//
//   // To read the current output of a PIO port:
//   int val = pio.port_read(Z80_PIO_PORT_A);
//
// ---------------------------------------------------------------------------

#ifndef Z80_PIO_H
#define Z80_PIO_H

#pragma once

#include "z80_daisy.h"
#include "sys_log.h"
#include <functional>
#include <cstring>

// Port identifiers
#define Z80_PIO_PORT_A  0
#define Z80_PIO_PORT_B  1

// PIO operating modes
#define Z80_PIO_MODE_OUTPUT  0  // Mode 0: output
#define Z80_PIO_MODE_INPUT   1  // Mode 1: input
#define Z80_PIO_MODE_BIDIR   2  // Mode 2: bidirectional (port A only)
#define Z80_PIO_MODE_BITCTL  3  // Mode 3: bit control

// ---------------------------------------------------------------------------
// z80_pio_config - Configuration passed to z80_pio::init()
// ---------------------------------------------------------------------------
struct z80_pio_config
{
    // Optional callbacks for reading from / writing to external hardware.
    // If port_X_read is set, reading port X calls this to get the value.
    // If port_X_write is set, writing port X calls this to push the value out.
    std::function<int()> port_a_read;
    std::function<int()> port_b_read;
    std::function<void(int)> port_a_write;
    std::function<void(int)> port_b_write;

    // Optional ready callbacks (active when port is ready for new data).
    // These are rarely used in arcade hardware but included for completeness.
    std::function<void(int)> rdy_a;
    std::function<void(int)> rdy_b;
};

// ---------------------------------------------------------------------------
// z80_pio - Z80 PIO device, 2 ports, with daisy chain support
// ---------------------------------------------------------------------------
class z80_pio : public z80_daisy_device
{
public:
    z80_pio() { reset(); }
    ~z80_pio() override = default;

    // Initialize the PIO with a configuration. Call once during game init.
    void init(const z80_pio_config& cfg)
    {
        m_port_read[0] = cfg.port_a_read;
        m_port_read[1] = cfg.port_b_read;
        m_port_write[0] = cfg.port_a_write;
        m_port_write[1] = cfg.port_b_write;
        m_rdy_cb[0] = cfg.rdy_a;
        m_rdy_cb[1] = cfg.rdy_b;
        reset();
    }

    // Reset both ports to their power-on state.
    void reset()
    {
        for (int p = 0; p < 2; p++)
        {
            m_port[p].mode = Z80_PIO_MODE_INPUT;  // Default: input mode
            m_port[p].output = 0;
            m_port[p].input = 0;
            m_port[p].io_select = 0xff;     // All bits input in mode 3
            m_port[p].int_control = 0;
            m_port[p].int_mask = 0xff;
            m_port[p].int_enabled = false;
            m_port[p].int_state = 0;
            m_port[p].mask_follows = false;
            m_port[p].strobe = false;
        }
        m_vector[0] = 0;
        m_vector[1] = 0;
    }

    // ------------------------------------------------------------------
    // CPU I/O interface (directly mapped to port addresses)
    //
    // The standard wiring uses:
    //   bit 0 of offset = C/D (0=data, 1=control)
    //   bit 1 of offset = A/B (0=port A, 1=port B)
    // ------------------------------------------------------------------

    // Write to the PIO. offset maps to port and data/control.
    void write(int offset, int data)
    {
        int port = (offset >> 1) & 1;  // bit 1 selects A or B
        int cd   = offset & 1;         // bit 0: 0=data, 1=control

        if (cd)
            control_write(port, data);
        else
            data_write(port, data);
    }

    // Read from the PIO. offset maps to port and data/control.
    int read(int offset) const
    {
        int port = (offset >> 1) & 1;
        int cd   = offset & 1;

        if (cd)
            return control_read(port);
        else
            return data_read(port);
    }

    // ------------------------------------------------------------------
    // Direct port access for writing/reading data from external hardware
    // (e.g., buttons, switches, other chips).
    // ------------------------------------------------------------------

    // Write external data into a PIO port (simulates hardware driving the pins).
    void port_write(int port, int data)
    {
        if (port < 0 || port > 1) return;
        auto& p = m_port[port];

        p.input = data & 0xff;

        // In mode 3 (bit control), check if interrupt condition is met
        if (p.mode == Z80_PIO_MODE_BITCTL && p.int_enabled)
        {
            check_mode3_interrupt(port);
        }
    }

    // Read the current value that would appear on a PIO port's pins.
    int port_read(int port) const
    {
        if (port < 0 || port > 1) return 0;
        const auto& p = m_port[port];

        switch (p.mode)
        {
        case Z80_PIO_MODE_OUTPUT:
            return p.output;

        case Z80_PIO_MODE_INPUT:
            // If a read callback is registered, use it; otherwise return latched input
            if (m_port_read[port])
                return m_port_read[port]() & 0xff;
            return p.input;

        case Z80_PIO_MODE_BIDIR:
            if (m_port_read[port])
                return m_port_read[port]() & 0xff;
            return p.input;

        case Z80_PIO_MODE_BITCTL:
            {
                // Bits set in io_select are inputs, cleared bits are outputs
                int result = 0;
                int input_val = m_port_read[port] ? m_port_read[port]() : p.input;
                result = (input_val & p.io_select) | (p.output & ~p.io_select);
                return result & 0xff;
            }
        }
        return 0;
    }

    // ------------------------------------------------------------------
    // z80_daisy_device interface implementation
    // ------------------------------------------------------------------

    int irq_state() override
    {
        int result = 0;
        for (int p = 0; p < 2; p++)
        {
            if (m_port[p].int_state & Z80_DAISY_IEO)
                result |= Z80_DAISY_IEO;
            if (m_port[p].int_state & Z80_DAISY_INT)
                result |= Z80_DAISY_INT;
        }
        return result;
    }

    int irq_ack() override
    {
        // Port A has priority over Port B
        for (int p = 0; p < 2; p++)
        {
            if (m_port[p].int_state & Z80_DAISY_IEO)
                return -1;  // This port is under service, block further

            if (m_port[p].int_state & Z80_DAISY_INT)
            {
                m_port[p].int_state = Z80_DAISY_IEO;
                int vec = m_vector[p];

                if (0) {
                    LOG_INFO("PIO: IRQ ack port %c, vector 0x%02x",
                        (p == 0) ? 'A' : 'B', vec);
                }
                return vec;
            }
        }
        return -1;
    }

    void irq_reti() override
    {
        for (int p = 0; p < 2; p++)
        {
            if (m_port[p].int_state & Z80_DAISY_IEO)
            {
                m_port[p].int_state = 0;

                if (0) {
                    LOG_INFO("PIO: RETI processed for port %c",
                        (p == 0) ? 'A' : 'B');
                }
                return;
            }
        }
    }

    // ------------------------------------------------------------------
    // Convenience: set a callback for when interrupt status changes.
    // ------------------------------------------------------------------
    void set_interrupt_callback(std::function<void(int)> cb)
    {
        m_intr_cb = std::move(cb);
    }

private:
    // Per-port state
    struct Port
    {
        int mode = Z80_PIO_MODE_INPUT;
        int output = 0;          // Output latch (what CPU has written)
        int input = 0;           // Input latch (what external hardware has written)
        int io_select = 0xff;    // Mode 3: bit direction (1=input, 0=output)
        int int_control = 0;     // Interrupt control word
        int int_mask = 0xff;     // Mode 3: interrupt mask
        bool int_enabled = false; // Interrupt enable flag
        int int_state = 0;       // Daisy chain interrupt state flags
        bool mask_follows = false; // True if next control write is a mask byte
        bool strobe = false;     // Strobe state for handshaking
    };

    Port m_port[2];
    int m_vector[2] = {0, 0};   // Interrupt vectors for each port

    // External callbacks
    std::function<int()> m_port_read[2];
    std::function<void(int)> m_port_write[2];
    std::function<void(int)> m_rdy_cb[2];
    std::function<void(int)> m_intr_cb;

    // ------------------------------------------------------------------
    // Internal: handle control register writes
    // ------------------------------------------------------------------
    void control_write(int port, int data)
    {
        auto& p = m_port[port];

        // If a mask byte is expected (mode 3), this write is the mask
        if (p.mask_follows)
        {
            p.mask_follows = false;
            p.int_mask = data;

            if (0) {
                LOG_INFO("PIO port %c: interrupt mask = 0x%02x",
                    (port == 0) ? 'A' : 'B', data);
            }

            // Check if interrupt condition is now met
            if (p.mode == Z80_PIO_MODE_BITCTL && p.int_enabled)
            {
                check_mode3_interrupt(port);
            }
            return;
        }

        // Determine what kind of control word this is based on bit patterns.
        // The Z80 PIO uses different bit patterns to distinguish control word types.

        // Interrupt vector (bit 0 = 0)
        if ((data & 0x01) == 0)
        {
            m_vector[port] = data & 0xfe;

            if (0) {
                LOG_INFO("PIO port %c: vector = 0x%02x",
                    (port == 0) ? 'A' : 'B', m_vector[port]);
            }
            return;
        }

        // Mode select word: bits 7-6 = mode, bits 3-0 = 0x0f
        if ((data & 0x0f) == 0x0f)
        {
            p.mode = (data >> 6) & 0x03;

            if (0) {
                LOG_INFO("PIO port %c: mode %d", (port == 0) ? 'A' : 'B', p.mode);
            }

            // If mode 3, the next control byte is the I/O select mask
            if (p.mode == Z80_PIO_MODE_BITCTL)
            {
                // The next byte written to control will be the I/O direction mask
                // Actually, per Z80 PIO spec, the I/O select register is written
                // immediately after the mode 3 control word.
                // We'll handle it in the next write by checking mode.
            }
            return;
        }

        // Interrupt control word: bits 3-0 = 0x07
        if ((data & 0x0f) == 0x07)
        {
            p.int_control = data;
            p.int_enabled = (data & 0x80) != 0;

            // Bit 4: mask follows
            if (data & 0x10)
            {
                p.mask_follows = true;
            }

            // If interrupts were disabled, clear any pending state
            if (!p.int_enabled)
            {
                p.int_state &= ~Z80_DAISY_INT;
            }

            if (0) {
                LOG_INFO("PIO port %c: int control = 0x%02x, enabled = %d",
                    (port == 0) ? 'A' : 'B', data, p.int_enabled ? 1 : 0);
            }
            return;
        }

        // I/O select mask (mode 3) - written after mode control word.
        // Detected when in mode 3 and the byte does not match other patterns.
        if (p.mode == Z80_PIO_MODE_BITCTL)
        {
            p.io_select = data;

            if (0) {
                LOG_INFO("PIO port %c: I/O select = 0x%02x",
                    (port == 0) ? 'A' : 'B', data);
            }
            return;
        }
    }

    // ------------------------------------------------------------------
    // Internal: handle data register writes
    // ------------------------------------------------------------------
    void data_write(int port, int data)
    {
        auto& p = m_port[port];
        p.output = data & 0xff;

        // Push data to external hardware via callback
        if (m_port_write[port])
        {
            switch (p.mode)
            {
            case Z80_PIO_MODE_OUTPUT:
                m_port_write[port](p.output);
                break;

            case Z80_PIO_MODE_BITCTL:
                // Only output the bits that are configured as outputs
                m_port_write[port](p.output & ~p.io_select);
                break;

            case Z80_PIO_MODE_BIDIR:
                m_port_write[port](p.output);
                break;

            default:
                // Mode 1 (input) - writing to data reg just stores it
                break;
            }
        }
    }

    // ------------------------------------------------------------------
    // Internal: handle data register reads
    // ------------------------------------------------------------------
    int data_read(int port) const
    {
        const auto& p = m_port[port];

        switch (p.mode)
        {
        case Z80_PIO_MODE_OUTPUT:
            return p.output;

        case Z80_PIO_MODE_INPUT:
            if (m_port_read[port])
                return m_port_read[port]() & 0xff;
            return p.input;

        case Z80_PIO_MODE_BIDIR:
            if (m_port_read[port])
                return m_port_read[port]() & 0xff;
            return p.input;

        case Z80_PIO_MODE_BITCTL:
            {
                int input_val = m_port_read[port] ? m_port_read[port]() : p.input;
                return (input_val & p.io_select) | (p.output & ~p.io_select);
            }
        }
        return 0;
    }

    // ------------------------------------------------------------------
    // Internal: handle control register reads
    // ------------------------------------------------------------------
    int control_read(int port) const
    {
        // The Z80 PIO does not have a readable control register per se.
        // Some implementations return the last written control byte or
        // the interrupt state. We return 0 for compatibility.
        return 0;
    }

    // ------------------------------------------------------------------
    // Internal: check mode 3 interrupt condition
    // ------------------------------------------------------------------
    void check_mode3_interrupt(int port)
    {
        auto& p = m_port[port];

        if (!p.int_enabled) return;
        if (p.mode != Z80_PIO_MODE_BITCTL) return;

        // Read the current port value
        int val = m_port_read[port] ? m_port_read[port]() : p.input;

        // Apply the mask: only monitored bits matter
        // int_mask: 0 = monitored, 1 = masked (ignored)
        int monitored = val & ~p.int_mask;

        // int_control bit 5: AND/OR logic
        //   AND (bit 5 = 1): interrupt when ALL monitored bits match
        //   OR  (bit 5 = 0): interrupt when ANY monitored bit matches
        // int_control bit 6: High/Low
        //   High (bit 6 = 1): match on 1
        //   Low  (bit 6 = 0): match on 0
        bool match = false;
        bool high = (p.int_control & 0x40) != 0;
        bool and_logic = (p.int_control & 0x20) != 0;

        if (!high)
        {
            // Match on low: invert so we check for zeros
            monitored = ~val & ~p.int_mask;
        }

        if (and_logic)
        {
            // AND: all monitored bits must be in the desired state
            match = (monitored == (0xff & ~p.int_mask));
        }
        else
        {
            // OR: any monitored bit in the desired state triggers
            match = (monitored != 0);
        }

        if (match && !(p.int_state & Z80_DAISY_INT))
        {
            p.int_state |= Z80_DAISY_INT;

            if (m_intr_cb)
                m_intr_cb(0);
        }
    }
};

#endif // Z80_PIO_H
