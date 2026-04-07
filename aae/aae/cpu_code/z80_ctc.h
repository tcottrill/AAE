// ---------------------------------------------------------------------------
// z80_ctc.h - Z80 CTC (Counter/Timer Circuit) Emulation for AAE
//
// Adapted from the original MAME Z80 CTC code (z80fmly.c) for use in the
// AAE emulator. Converted from C struct + global arrays into a C++ class
// that inherits from the z80_daisy_device interface for proper daisy chaining.
//
// THE Z80 CTC:
//
// The Z80 CTC is a 4-channel counter/timer that connects to the Z80 CPU via
// the daisy chain interrupt system. Each channel can operate in either:
//   - Timer mode: counts down from a time constant using the system clock
//     (optionally prescaled by 16 or 256).
//   - Counter mode: counts external trigger (TRG) pulses.
//
// When a channel's counter reaches zero, it fires a Zero Count (ZC/TO) output.
// Channels can be chained: ZC/TO of one channel can feed the TRG of the next.
// Each channel can independently request an interrupt via the daisy chain.
//
// Games that use the Z80 CTC include many Konami and Sega boards:
//   Scramble, Super Cobra, Frogger (sound CPU), Gyruss, etc.
//
// USAGE IN A GAME DRIVER:
//
//   // In your driver file:
//   #include "z80_ctc.h"
//
//   static z80_ctc ctc;  // the CTC instance
//
//   // In init_game():
//   z80_ctc_config cfg;
//   cfg.baseclock = 4000000;  // 4 MHz system clock
//   cfg.notimer = 0;          // all timers enabled (use NOTIMER_0..3 to disable)
//   cfg.zc_cb[0] = my_zc0_callback;  // optional: wire ZC0 to something
//   cfg.zc_cb[1] = my_zc1_callback;  // optional: wire ZC1 to something
//   ctc.init(cfg);
//
//   // Add to daisy chain:
//   daisy.add(&ctc);
//
//   // Wire CTC I/O ports in your Z80 port read/write tables:
//   // Write: ctc.write(offset & 3, data);
//   // Read:  ctc.read(offset & 3);
//
//   // If channels use external triggers:
//   ctc.trg_write(channel, data);  // call when trigger pulse arrives
//
// ---------------------------------------------------------------------------

#ifndef Z80_CTC_H
#define Z80_CTC_H

#pragma once

#include "z80_daisy.h"
#include "timer.h"
#include "sys_log.h"
#include <functional>
#include <cstring>

// Bitmasks for disabling individual timer channels in the config.
// OR these together in z80_ctc_config::notimer to prevent specific
// channels from running in timer mode. Counter mode still works.
#define CTC_NOTIMER_0 (1 << 0)
#define CTC_NOTIMER_1 (1 << 1)
#define CTC_NOTIMER_2 (1 << 2)
#define CTC_NOTIMER_3 (1 << 3)

// ---------------------------------------------------------------------------
// z80_ctc_config - Configuration passed to z80_ctc::init()
// ---------------------------------------------------------------------------
struct z80_ctc_config
{
    int baseclock = 0;          // System clock in Hz fed to the CTC
    int notimer = 0;            // Bitmask of channels to disable timer mode
    int cpunum = 0;             // Which CPU number (for AAE timer system)

    // Zero-count callbacks for channels 0-2. Channel 3 has no ZC output.
    // Signature: void callback(int channel, int data)
    // 'data' is currently always 0; 'channel' is the channel number.
    std::function<void(int, int)> zc_cb[3];
};

// ---------------------------------------------------------------------------
// z80_ctc - Z80 CTC device, 4 channels, with daisy chain support
// ---------------------------------------------------------------------------
class z80_ctc : public z80_daisy_device
{
public:
    z80_ctc() { reset(); }
    ~z80_ctc() override = default;

    // Initialize the CTC with a configuration. Call once during game init.
    void init(const z80_ctc_config& cfg)
    {
        m_baseclock = cfg.baseclock;
        m_notimer = cfg.notimer;
        m_cpunum = cfg.cpunum;
        for (int i = 0; i < 3; i++)
            m_zc_cb[i] = cfg.zc_cb[i];

        reset();
    }

    // Reset all 4 channels to their power-on state.
    void reset()
    {
        for (int i = 0; i < 4; i++)
        {
            m_channel[i].control = 0x23;  // interrupt disabled, timer mode, prescaler=16, no trigger edge, no time constant follows, reset, control word
            m_channel[i].tconst = 0x100;  // time constant = 256 (default when 0 is written)
            m_channel[i].down = 0;
            m_channel[i].extclk = 0;
            m_channel[i].timer_id = -1;
            m_channel[i].int_state = 0;
            m_channel[i].zc = 0;
            m_channel[i].tconst_loaded = false;
        }
        m_vector = 0;
    }

    // ------------------------------------------------------------------
    // CPU I/O interface
    // ------------------------------------------------------------------

    // Write to the CTC. offset selects the channel (0-3).
    // The CTC uses a single register per channel; the meaning of the
    // written byte depends on whether a time constant is expected.
    void write(int offset, int data)
    {
        int ch = offset & 3;
        auto& c = m_channel[ch];

        // If the previous write was a control word with "time constant follows" bit set,
        // this byte is the time constant value.
        if (c.tconst_loaded)
        {
            c.tconst_loaded = false;
            // Time constant of 0 means 256
            c.tconst = (data == 0) ? 0x100 : data;
            c.down = c.tconst;

            // If in timer mode and timer is not disabled, start the timer
            if ((c.control & 0x40) == 0)  // bit 6 = 0 means timer mode
            {
                if (!(m_notimer & (1 << ch)))
                {
                    // Calculate the period based on prescaler
                    int prescaler = (c.control & 0x20) ? 256 : 16;
                    double period = TIME_IN_HZ(m_baseclock) * prescaler * c.tconst;
                    start_timer(ch, period);
                }
            }
            // In counter mode, the counter is loaded but waits for trigger edges.
            // (The trigger write handler takes care of counting.)

            // Clear the channel reset bit now that a time constant is loaded
            c.control &= ~0x02;

            if (0) {
                LOG_INFO("CTC ch%d: time constant = %d, prescaler = %d, mode = %s",
                    ch, c.tconst,
                    (c.control & 0x20) ? 256 : 16,
                    (c.control & 0x40) ? "counter" : "timer");
            }
            return;
        }

        // Check bit 0: if 0, this is a vector write (only channel 0 accepts vectors)
        if ((data & 0x01) == 0)
        {
            // Interrupt vector. Bits 7-3 come from this write.
            // Bits 2-1 are the channel number (filled in at acknowledge time).
            // Bit 0 is always 0.
            m_vector = data & 0xf8;

            if (0) {
                LOG_INFO("CTC: vector base set to 0x%02x", m_vector);
            }
            return;
        }

        // This is a channel control word (bit 0 = 1).
        c.control = data;

        // Bit 1: Reset. If set, the channel stops operating.
        if (data & 0x02)
        {
            stop_timer(ch);
            c.down = 0;
            c.zc = 0;
            // If interrupt was pending, remove it
            if (c.int_state & Z80_DAISY_INT)
            {
                c.int_state = 0;
            }
        }

        // Bit 2: Time constant follows
        if (data & 0x04)
        {
            c.tconst_loaded = true;
        }

        // Bit 7: Interrupt enable
        // If interrupts are now disabled, remove any pending interrupt state
        if (!(data & 0x80))
        {
            c.int_state &= ~Z80_DAISY_INT;
        }
    }

    // Read from the CTC. offset selects the channel (0-3).
    // Reading returns the current down counter value.
    int read(int offset) const
    {
        int ch = offset & 3;
        return m_channel[ch].down & 0xff;
    }

    // Write to an external trigger input. Call this when the hardware
    // generates a trigger pulse for the given channel.
    // In counter mode, each trigger pulse decrements the counter.
    // In timer mode, triggers can start/gate the timer depending on
    // the trigger edge control bit.
    void trg_write(int ch, int data)
    {
        if (ch < 0 || ch > 3) return;
        auto& c = m_channel[ch];

        // Ignore triggers if channel is in reset state
        if (c.control & 0x02) return;

        // Edge detection: bit 4 of control = trigger edge
        //   0 = falling edge, 1 = rising edge
        bool rising = (c.control & 0x10) != 0;
        bool old_level = (c.extclk != 0);
        bool new_level = (data != 0);
        c.extclk = data;

        bool triggered = false;
        if (rising && !old_level && new_level)    triggered = true;  // rising edge
        if (!rising && old_level && !new_level)   triggered = true;  // falling edge

        if (!triggered) return;

        // In counter mode (bit 6 = 1), count the trigger
        if (c.control & 0x40)
        {
            c.down--;
            if (c.down <= 0)
            {
                // Zero count reached
                c.down = c.tconst;
                zero_count(ch);
            }
        }
        else
        {
            // In timer mode, a trigger edge can start the timer
            // (if "trigger when time constant is loaded" bit is set, etc.)
            // For most arcade uses, timer mode timers are already running
            // via the periodic timer. This path handles gated timers.
            if (!(m_notimer & (1 << ch)))
            {
                int prescaler = (c.control & 0x20) ? 256 : 16;
                double period = TIME_IN_HZ(m_baseclock) * prescaler * c.tconst;
                start_timer(ch, period);
            }
        }
    }

    // Return the period (in seconds) of a CTC channel. Useful for
    // calculating baud rates or other derived timing.
    double get_period(int ch) const
    {
        if (ch < 0 || ch > 3) return 0.0;
        const auto& c = m_channel[ch];

        if (c.control & 0x40)
        {
            // Counter mode: period depends on external trigger rate, unknown here.
            return 0.0;
        }
        else
        {
            int prescaler = (c.control & 0x20) ? 256 : 16;
            if (m_baseclock <= 0) return 0.0;
            return (double)prescaler * (double)c.tconst / (double)m_baseclock;
        }
    }

    // ------------------------------------------------------------------
    // z80_daisy_device interface implementation
    // ------------------------------------------------------------------

    // Return the combined interrupt state of all 4 channels.
    // The CTC channels form their own internal priority (ch0 = highest).
    int irq_state() override
    {
        int result = 0;

        for (int ch = 0; ch < 4; ch++)
        {
            // If any channel is under service, that blocks everything downstream.
            if (m_channel[ch].int_state & Z80_DAISY_IEO)
            {
                result |= Z80_DAISY_IEO;
                // But a higher-priority channel might also have a pending interrupt
                // that should still be visible.
            }

            if (m_channel[ch].int_state & Z80_DAISY_INT)
            {
                result |= Z80_DAISY_INT;
            }
        }
        return result;
    }

    // Acknowledge the highest-priority pending interrupt.
    // Returns the interrupt vector for IM2.
    int irq_ack() override
    {
        for (int ch = 0; ch < 4; ch++)
        {
            auto& c = m_channel[ch];

            // Skip channels under service - they block lower ones
            if (c.int_state & Z80_DAISY_IEO)
                return -1;

            // Found a pending interrupt
            if (c.int_state & Z80_DAISY_INT)
            {
                // Transition from "pending" to "under service"
                c.int_state = Z80_DAISY_IEO;

                // The vector is the base vector + (channel * 2)
                int vec = m_vector | (ch << 1);

                if (0) {
                    LOG_INFO("CTC: IRQ ack ch%d, vector 0x%02x", ch, vec);
                }
                return vec;
            }
        }
        return -1;
    }

    // Handle RETI: find the first channel under service and clear it.
    void irq_reti() override
    {
        for (int ch = 0; ch < 4; ch++)
        {
            auto& c = m_channel[ch];
            if (c.int_state & Z80_DAISY_IEO)
            {
                // Clear only the IEO (under-service) flag, NOT the INT flag.
                // If the CTC timer fired again while the ISR was running,
                // zero_count() will have set INT again. Wiping the whole
                // state (= 0) was discarding that re-entrant interrupt,
                // causing the Z80 sound sequencer to lose timing beats and
                // eventually execute from a garbage address.
                c.int_state &= ~Z80_DAISY_IEO;

                if (0) {
                    LOG_INFO("CTC: RETI processed for ch%d", ch);
                }

                // If a new interrupt arrived while we were being serviced,
                // re-fire the callback so the CPU gets the INT line asserted
                // again immediately after RETI enables it.
                if ((c.int_state & Z80_DAISY_INT) && m_intr_cb)
                {
                    m_intr_cb(0);
                }
                return;
            }
        }
    }

    // ------------------------------------------------------------------
    // Convenience: set a callback to be called whenever the CTC wants
    // to signal an interrupt (e.g., to assert the CPU's INT line).
    // Not part of daisy chain protocol itself, but useful for wiring.
    // ------------------------------------------------------------------
    void set_interrupt_callback(std::function<void(int)> cb)
    {
        m_intr_cb = std::move(cb);
    }

private:
    // Per-channel state
    struct Channel
    {
        int control = 0;         // Control register (8 bits)
        int tconst = 0x100;      // Time constant (1-256, 0 means 256)
        int down = 0;            // Current down counter value
        int extclk = 0;          // External clock/trigger pin state
        int timer_id = -1;       // AAE timer ID for periodic timer, or -1
        int int_state = 0;       // Daisy chain interrupt state flags
        int zc = 0;              // Zero count output state
        bool tconst_loaded = false;  // True if next write is a time constant
    };

    Channel m_channel[4];
    int m_vector = 0;            // Base interrupt vector (bits 7-3)
    int m_baseclock = 0;         // System clock in Hz
    int m_notimer = 0;           // Bitmask of disabled timer channels
    int m_cpunum = 0;            // CPU number for AAE timer system

    // Zero-count callbacks for channels 0-2 (ch3 has no ZC output)
    std::function<void(int, int)> m_zc_cb[3];

    // Optional callback when interrupt status changes
    std::function<void(int)> m_intr_cb;

    // ------------------------------------------------------------------
    // Internal: handle a zero count event on the given channel
    // ------------------------------------------------------------------
    void zero_count(int ch)
    {
        auto& c = m_channel[ch];

        // Fire the ZC callback if one is registered (channels 0-2 only)
        if (ch < 3 && m_zc_cb[ch])
        {
            m_zc_cb[ch](ch, 0);
        }

        // Request an interrupt if interrupts are enabled for this channel
        if (c.control & 0x80)
        {
            c.int_state |= Z80_DAISY_INT;

            // Notify the external interrupt handler if registered
            if (m_intr_cb)
            {
                m_intr_cb(0);
            }
        }
    }

    // ------------------------------------------------------------------
    // Internal: start or restart the periodic AAE timer for a channel
    // ------------------------------------------------------------------
    void start_timer(int ch, double period)
    {
        stop_timer(ch);

        // Use the AAE timer system. We create a repeating timer that
        // fires zero_count when the period elapses.
        // The timer callback receives the channel number as its parameter.
        m_channel[ch].timer_id = timer_set(period, m_cpunum, ch,
            [this](int param) { timer_fired(param); });
    }

    // ------------------------------------------------------------------
    // Internal: stop the AAE timer for a channel
    // ------------------------------------------------------------------
    void stop_timer(int ch)
    {
        if (m_channel[ch].timer_id >= 0)
        {
            timer_remove(m_channel[ch].timer_id);
            m_channel[ch].timer_id = -1;
        }
    }

    // ------------------------------------------------------------------
    // Internal: called by the AAE timer when a channel's period elapses
    // ------------------------------------------------------------------
    void timer_fired(int ch)
    {
        if (ch < 0 || ch > 3) return;
        auto& c = m_channel[ch];

        // Reload the down counter
        c.down = c.tconst;

        // Fire zero count
        zero_count(ch);

        // In timer mode, the timer automatically repeats (the AAE timer
        // system handles the repeat since we use timer_set not timer_pulse).
        // We need to restart it for the next period.
        if ((c.control & 0x40) == 0)  // timer mode
        {
            if (!(m_notimer & (1 << ch)))
            {
                int prescaler = (c.control & 0x20) ? 256 : 16;
                double period = TIME_IN_HZ(m_baseclock) * prescaler * c.tconst;
                start_timer(ch, period);
            }
        }
    }
};

#endif // Z80_CTC_H
