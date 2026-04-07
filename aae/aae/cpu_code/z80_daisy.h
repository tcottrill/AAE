// ---------------------------------------------------------------------------
// z80_daisy.h - Z80 Daisy Chain Interface for AAE
//
// This provides the abstract interface for Z80 peripheral devices that
// participate in the Z80 interrupt daisy chain (CTC, PIO, SIO, etc.).
//
// HOW THE Z80 DAISY CHAIN WORKS:
//
// The Z80 supports a priority-based interrupt daisy chain. Multiple
// peripheral devices are connected in series. When the CPU acknowledges
// an interrupt (IM2), it reads a vector from the highest-priority device
// that has a pending interrupt. The RETI instruction signals the end of
// an interrupt service routine, and the devices pass this signal down
// the chain so lower-priority devices know they can now interrupt.
//
// In hardware, IEI (Interrupt Enable In) and IEO (Interrupt Enable Out)
// signals flow through the chain. A device blocks downstream devices
// when it is being serviced (IEO goes low).
//
// In software emulation, we model this with a simple ordered array of
// device pointers. The CPU walks the array from highest to lowest
// priority to find pending interrupts and to propagate RETI.
//
// INTEGRATION WITH AAE:
//
// 1. Each Z80 family device (z80_ctc, z80_pio) inherits from z80_daisy_device.
// 2. In your game driver's init, create a z80_daisy_chain and add devices:
//       z80_daisy_chain daisy;
//       daisy.add(&my_ctc);
//       daisy.add(&my_pio);
// 3. Hook the chain to the CPU's interrupt and RETI mechanisms:
//       // In your interrupt handler callback:
//       int vec = daisy.interrupt();
//       if (vec >= 0) m_cpu_z80[0]->mz80int(vec);
//
//       // Hook RETI so the chain gets notified:
//       m_cpu_z80[0]->reti_hook = [&daisy]() { daisy.reti(); };
//
// ---------------------------------------------------------------------------

#ifndef Z80_DAISY_H
#define Z80_DAISY_H

#pragma once

#include <vector>
#include <cstdint>

// ---------------------------------------------------------------------------
// z80_daisy_device - Abstract base class for any Z80 family peripheral
//                    that participates in daisy chaining.
//
// Each device must implement:
//   irq_state()  - Returns combined flags indicating interrupt status.
//   irq_ack()    - Called during interrupt acknowledge; returns the vector.
//   irq_reti()   - Called when a RETI instruction propagates to this device.
// ---------------------------------------------------------------------------

// Flags returned by irq_state(). A device can be in multiple states
// (e.g., pending AND servicing if it has multiple channels).
constexpr int Z80_DAISY_INT = 0x01;   // interrupt pending
constexpr int Z80_DAISY_IEO = 0x02;   // interrupt under service (blocks downstream)

class z80_daisy_device
{
public:
    virtual ~z80_daisy_device() = default;

    // Return a bitmask of Z80_DAISY_INT and/or Z80_DAISY_IEO flags.
    // Z80_DAISY_INT means this device has an interrupt waiting.
    // Z80_DAISY_IEO means this device is currently being serviced
    // (its interrupt handler is running), so downstream devices are blocked.
    virtual int irq_state() = 0;

    // Called by the daisy chain when the CPU acknowledges an interrupt
    // from this device. The device should mark itself as "under service"
    // and return the interrupt vector byte for IM2.
    virtual int irq_ack() = 0;

    // Called when a RETI instruction is detected while this device is
    // the one being serviced. The device should clear its "under service"
    // state so downstream devices become unblocked.
    virtual void irq_reti() = 0;
};

// ---------------------------------------------------------------------------
// z80_daisy_chain - Manages an ordered list of daisy-chained devices.
//
// Devices are added in priority order (highest priority first).
// The chain is queried by the CPU's interrupt handler and RETI hook.
// ---------------------------------------------------------------------------
class z80_daisy_chain
{
public:
    z80_daisy_chain() = default;
    ~z80_daisy_chain() = default;

    // Add a device to the end of the chain (lowest priority so far).
    // Call in order from highest to lowest priority.
    void add(z80_daisy_device* dev)
    {
        if (dev) m_chain.push_back(dev);
    }

    // Remove all devices (call during cleanup or game end).
    void clear()
    {
        m_chain.clear();
    }

    // Check if any device in the chain has a pending interrupt.
    // Returns true if at least one device wants to interrupt.
    bool has_pending_interrupt() const
    {
        for (auto* dev : m_chain)
        {
            int state = dev->irq_state();

            // If this device is under service (IEO blocked), nothing
            // downstream can interrupt, so stop checking.
            if (state & Z80_DAISY_IEO)
                return false;

            // Found a pending interrupt that is not blocked.
            if (state & Z80_DAISY_INT)
                return true;
        }
        return false;
    }

    // Called by the interrupt handler. Walks the chain looking for the
    // highest-priority device with a pending interrupt that is not
    // blocked by a higher-priority device being serviced.
    //
    // Returns the interrupt vector (0-255) on success, or -1 if no
    // device needs servicing.
    int interrupt()
    {
        for (auto* dev : m_chain)
        {
            int state = dev->irq_state();

            // If this device has an interrupt being serviced already,
            // nothing further down the chain can request an interrupt.
            if (state & Z80_DAISY_IEO)
                return -1;

            // This device has a pending interrupt - acknowledge it.
            if (state & Z80_DAISY_INT)
                return dev->irq_ack();
        }
        return -1;
    }

    // Called when the Z80 executes a RETI instruction. Walks the chain
    // and notifies the first device that is currently under service.
    void reti()
    {
        for (auto* dev : m_chain)
        {
            int state = dev->irq_state();

            // The first device in the chain that is under service
            // is the one that the RETI applies to.
            if (state & Z80_DAISY_IEO)
            {
                dev->irq_reti();
                return;
            }
        }
    }

    // Return true if the chain has any devices registered.
    bool empty() const { return m_chain.empty(); }

    // Return the number of devices in the chain.
    int size() const { return static_cast<int>(m_chain.size()); }

private:
    std::vector<z80_daisy_device*> m_chain;
};

#endif // Z80_DAISY_H
