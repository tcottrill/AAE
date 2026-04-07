// ---------------------------------------------------------------------------
// z80_daisy_example.cpp - Example: Wiring Z80 Daisy Chain in an AAE Driver
//
// This file is NOT meant to be compiled directly. It shows the patterns
// and wiring needed to integrate z80_ctc, z80_pio, and z80_daisy_chain
// into a real AAE game driver. Copy/paste the relevant pieces into your
// actual driver code.
//
// The example shows a hypothetical Scramble-like game with:
//   - A main Z80 CPU
//   - A Z80 CTC for timer interrupts
//   - A Z80 PIO for I/O (optional, many games only use CTC)
//   - The CTC and PIO daisy-chained for IM2 interrupts
//
// ---------------------------------------------------------------------------

#include "aae_mame_driver.h"
#include "z80_daisy.h"
#include "z80_ctc.h"
#include "z80_pio.h"

// ---------------------------------------------------------------------------
// Step 1: Declare your device instances and daisy chain as statics
//         in the driver file (same as you do with other driver state).
// ---------------------------------------------------------------------------
static z80_ctc         ctc;           // The CTC device
static z80_pio         pio;           // The PIO device (if needed)
static z80_daisy_chain daisy;         // The daisy chain manager

// ---------------------------------------------------------------------------
// Step 2: CTC I/O port handlers
//
// Wire these into your Z80 port read/write tables just like any other
// port handler. The CTC typically occupies 4 consecutive port addresses.
// ---------------------------------------------------------------------------

// CTC write handler for the Z80 port write table
PORT_WRITE_HANDLER(ctc_w)
{
    ctc.write(port & 0x03, data);
}

// CTC read handler for the Z80 port read table
PORT_READ_HANDLER(ctc_r)
{
    return ctc.read(port & 0x03);
}

// ---------------------------------------------------------------------------
// Step 2b: PIO I/O port handlers (if your game uses a PIO)
// ---------------------------------------------------------------------------

// PIO write handler
PORT_WRITE_HANDLER(pio_w)
{
    pio.write(port & 0x03, data);
}

// PIO read handler
PORT_READ_HANDLER(pio_r)
{
    return pio.read(port & 0x03);
}

// ---------------------------------------------------------------------------
// Step 3: Optional ZC (zero count) callbacks
//
// The CTC's zero-count outputs can drive other hardware. For example,
// CTC channel 0's ZC might be wired to the trigger input of channel 1,
// creating a cascaded timer. Or ZC might drive a sound chip's clock.
// ---------------------------------------------------------------------------

// Example: CTC channel 0 ZC drives channel 1's trigger input (cascading)
static void ctc_zc0_callback(int channel, int data)
{
    ctc.trg_write(1, 1);  // rising edge
    ctc.trg_write(1, 0);  // then falling edge (pulse)
}

// Example: CTC channel 1 ZC drives a baud rate generator or sound chip
static void ctc_zc1_callback(int channel, int data)
{
    // Your hardware-specific code here, e.g.:
    // sn76496_clock_update(ctc.get_period(1));
}

// ---------------------------------------------------------------------------
// Step 4: The interrupt handler
//
// This is the function called by the AAE scheduler at interrupt time.
// Instead of directly calling mz80int, we ask the daisy chain for the
// vector. The daisy chain walks its device list to find the highest-
// priority pending interrupt.
// ---------------------------------------------------------------------------
static void my_interrupt_handler()
{
    int vec = daisy.interrupt();
    if (vec >= 0)
    {
        m_cpu_z80[0]->mz80int(vec);
    }
}

// ---------------------------------------------------------------------------
// Step 5: PIO port callbacks (if using PIO)
//
// These connect the PIO ports to actual hardware (buttons, dipswitches, etc.)
// ---------------------------------------------------------------------------

// Called when the CPU reads PIO port A (e.g., player controls)
static int pio_port_a_read()
{
    // Read your input port here. Example:
    // return input_port_0_r(0);
    return 0xff;
}

// Called when the CPU writes PIO port B (e.g., LEDs, coin counter)
static void pio_port_b_write(int data)
{
    // Drive your output hardware here. Example:
    // coin_counter_w(data & 0x01);
    // led_w(data & 0x02);
}

// ---------------------------------------------------------------------------
// Step 6: Port tables showing where the CTC and PIO sit in the I/O map
// ---------------------------------------------------------------------------

// Example port write table
PORT_WRITE(example_port_write)
    PORT_ADDR(0x00, 0x03, ctc_w)      // CTC at ports 0x00-0x03
    PORT_ADDR(0x04, 0x07, pio_w)      // PIO at ports 0x04-0x07
    // ... other port handlers ...
PORT_END

// Example port read table
PORT_READ(example_port_read)
    PORT_ADDR(0x00, 0x03, ctc_r)      // CTC at ports 0x00-0x03
    PORT_ADDR(0x04, 0x07, pio_r)      // PIO at ports 0x04-0x07
    // ... other port handlers ...
PORT_END

// ---------------------------------------------------------------------------
// Step 7: Game init - wire everything together
// ---------------------------------------------------------------------------
static int example_init_game()
{
    // --- Configure the CTC ---
    z80_ctc_config ctc_cfg;
    ctc_cfg.baseclock = 4000000;    // 4 MHz (match your game's CPU clock)
    ctc_cfg.notimer = 0;            // All timer channels enabled
    ctc_cfg.cpunum = 0;             // CTC is clocked by CPU 0
    ctc_cfg.zc_cb[0] = ctc_zc0_callback;  // ZC0 -> cascaded to ch1
    ctc_cfg.zc_cb[1] = ctc_zc1_callback;  // ZC1 -> sound chip clock
    // ctc_cfg.zc_cb[2] = nullptr;         // ZC2 unused (default)
    ctc.init(ctc_cfg);

    // --- Configure the PIO (if needed) ---
    z80_pio_config pio_cfg;
    pio_cfg.port_a_read = pio_port_a_read;
    pio_cfg.port_b_write = pio_port_b_write;
    pio.init(pio_cfg);

    // --- Build the daisy chain in priority order ---
    // The device added first has the highest interrupt priority.
    daisy.clear();
    daisy.add(&ctc);   // CTC has highest priority
    daisy.add(&pio);   // PIO has lower priority

    // --- Initialize the CPU ---
    init_z80(example_mem_read, example_mem_write,
             example_port_read, example_port_write, 0);

    // --- Hook the RETI instruction ---
    // This is crucial! When the Z80 executes RETI, the daisy chain
    // needs to be notified so it can update the IEO chain and allow
    // lower-priority devices to interrupt.
    m_cpu_z80[0]->reti_hook = []() { daisy.reti(); };

    // --- Optionally set an interrupt callback on the CTC ---
    // This is useful if you want the CTC to be able to assert INT
    // asynchronously (e.g., from timer expiration). The callback
    // should trigger an interrupt check.
    ctc.set_interrupt_callback([](int which) {
        // The CTC wants to interrupt. We can either:
        // a) Set the CPU's IRQ line directly:
        int vec = daisy.interrupt();
        if (vec >= 0) {
            m_cpu_z80[0]->mz80int(vec);
        }
        // b) Or just flag it and let the scheduler's normal interrupt
        //    handler pick it up at the right time.
    });

    return 0;  // success
}

// ---------------------------------------------------------------------------
// Step 8: Game end - clean up
// ---------------------------------------------------------------------------
static void example_end_game()
{
    daisy.clear();
    // CTC and PIO destructors handle their own cleanup.
    // The AAE timer system cleans up timers at end of game.
}


// ===========================================================================
// ALTERNATIVE PATTERN: CTC-only (no PIO, simpler games)
//
// Many games only use a CTC for interrupt timing. The pattern is the same
// but you skip the PIO setup and just have the CTC in the daisy chain.
//
// This is common for sound CPUs in dual-CPU games.
// ===========================================================================

/*
static z80_ctc         sound_ctc;
static z80_daisy_chain sound_daisy;

static void sound_init()
{
    z80_ctc_config cfg;
    cfg.baseclock = 3579545;     // 3.579545 MHz
    cfg.cpunum = 1;              // Sound CPU is CPU #1
    cfg.notimer = CTC_NOTIMER_3; // Channel 3 not used as timer
    sound_ctc.init(cfg);

    sound_daisy.clear();
    sound_daisy.add(&sound_ctc);

    m_cpu_z80[1]->reti_hook = []() { sound_daisy.reti(); };
}
*/


// ===========================================================================
// ALTERNATIVE PATTERN: Using CTC without daisy chain (simple IRQ generation)
//
// If a game uses the CTC just for timing and does NOT use IM2 / daisy chain
// (e.g., it uses IM1 interrupts triggered by CTC zero-count), you can skip
// the daisy chain entirely and just use the CTC's interrupt callback.
// ===========================================================================

/*
static z80_ctc simple_ctc;

static void simple_ctc_interrupt(int which)
{
    // CTC fired -- just do an IM1 interrupt
    cpu_do_int_imm(0, INT_TYPE_INT);
}

static void simple_init()
{
    z80_ctc_config cfg;
    cfg.baseclock = 3072000;
    cfg.cpunum = 0;
    simple_ctc.init(cfg);
    simple_ctc.set_interrupt_callback(simple_ctc_interrupt);
    // No daisy chain needed. No reti_hook needed.
}
*/


// ===========================================================================
// QUICK REFERENCE: What goes where
//
// z80_daisy.h     - #include in any driver that uses daisy chaining.
//                   Provides z80_daisy_chain and z80_daisy_device.
//
// z80_ctc.h       - #include in any driver that uses a Z80 CTC.
//                   Provides z80_ctc (inherits z80_daisy_device).
//
// z80_pio.h       - #include in any driver that uses a Z80 PIO.
//                   Provides z80_pio (inherits z80_daisy_device).
//
// Your driver:
//   1. Declare static instances of z80_ctc, z80_pio, z80_daisy_chain
//   2. Configure and init devices in init_game()
//   3. Build the daisy chain with daisy.add() in priority order
//   4. Hook reti: m_cpu_z80[N]->reti_hook = [&]() { daisy.reti(); };
//   5. Wire port handlers for CTC/PIO reads and writes
//   6. Use daisy.interrupt() in your interrupt handler
//   7. Call daisy.clear() in end_game()
//
// ===========================================================================
