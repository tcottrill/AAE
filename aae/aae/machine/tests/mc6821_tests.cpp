//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// Standalone assert-runner for mc6821. Build & run:
//
//   WSL / Linux (from repo root):
//     g++ -std=c++17 -I aae/aae -I aae/aae/machine -I aae/system/util \
//         aae/aae/machine/tests/mc6821_tests.cpp aae/aae/machine/mc6821.cpp \
//         -o /tmp/mc6821_tests && /tmp/mc6821_tests
//
//   Windows (from a VS developer prompt, at repo root):
//     cl /std:c++17 /EHsc /nologo /I aae\aae /I aae\aae\machine /I aae\system\util ^
//        aae\aae\machine\tests\mc6821_tests.cpp aae\aae\machine\mc6821.cpp ^
//        /Fe:mc6821_tests.exe && mc6821_tests.exe
#include "mc6821.h"
#include "sys_log.h"

#include <cstdio>
#include <cstring>

// --- Stub: the PIA only logs unattached-slot misuse -------------------------
namespace Log {
    void write(Level, const char*, const char*, int, const char*, ...) {}
}

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
} while (0)

#define CHECK_EQ(got, want, ...) do { ++g_checks; \
    long long g_ = (long long)(got), w_ = (long long)(want); \
    if (g_ != w_) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        std::printf(__VA_ARGS__); \
        std::printf("  got %lld (0x%llX), want %lld (0x%llX)\n", g_, g_, w_, w_); \
        ++g_failures; } \
} while (0)

// Register indices (standard ordering).
enum { R_PORTA = 0, R_CTLA = 1, R_PORTB = 2, R_CTLB = 3 };

// Control-register bit 2 selects the port rather than the DDR.
static const uint8_t SEL_PORT = 0x04;

// ---------------------------------------------------------------------------
// Capture globals for the interface callbacks. The mc6821_interface takes plain
// function pointers, so the probes route through file-scope state.
// ---------------------------------------------------------------------------
static uint8_t g_in_a, g_in_b;
static int     g_in_ca1, g_in_ca2, g_in_cb1, g_in_cb2;
static uint8_t g_out_a, g_out_b;
static int     g_out_ca2, g_out_cb2;
static int     g_irq_a, g_irq_b;
static int     g_out_a_calls, g_out_b_calls, g_out_ca2_calls, g_out_cb2_calls;
static int     g_irq_a_calls, g_irq_b_calls;

static void probes_reset()
{
    g_in_a = 0xFF; g_in_b = 0x00;
    g_in_ca1 = g_in_ca2 = 1;
    g_in_cb1 = g_in_cb2 = 0;
    g_out_a = g_out_b = 0;
    g_out_ca2 = g_out_cb2 = -1;
    g_irq_a = g_irq_b = -1;
    g_out_a_calls = g_out_b_calls = g_out_ca2_calls = g_out_cb2_calls = 0;
    g_irq_a_calls = g_irq_b_calls = 0;
}

static uint8_t cb_in_a()   { return g_in_a; }
static uint8_t cb_in_b()   { return g_in_b; }
static int     cb_in_ca1() { return g_in_ca1; }
static int     cb_in_ca2() { return g_in_ca2; }
static int     cb_in_cb1() { return g_in_cb1; }
static int     cb_in_cb2() { return g_in_cb2; }
static void    cb_out_a(uint8_t d)   { g_out_a = d;    ++g_out_a_calls; }
static void    cb_out_b(uint8_t d)   { g_out_b = d;    ++g_out_b_calls; }
static void    cb_out_ca2(int s)     { g_out_ca2 = s;  ++g_out_ca2_calls; }
static void    cb_out_cb2(int s)     { g_out_cb2 = s;  ++g_out_cb2_calls; }
static void    cb_irq_a(int s)       { g_irq_a = s;    ++g_irq_a_calls; }
static void    cb_irq_b(int s)       { g_irq_b = s;    ++g_irq_b_calls; }

// Fully wired: every pin has a callback.
static mc6821_interface full_intf()
{
    mc6821_interface i{};
    i.in_a = cb_in_a;     i.in_b = cb_in_b;
    i.in_ca1 = cb_in_ca1; i.in_ca2 = cb_in_ca2;
    i.in_cb1 = cb_in_cb1; i.in_cb2 = cb_in_cb2;
    i.out_a = cb_out_a;   i.out_b = cb_out_b;
    i.out_ca2 = cb_out_ca2; i.out_cb2 = cb_out_cb2;
    i.irq_a = cb_irq_a;   i.irq_b = cb_irq_b;
    return i;
}

// Only the data ports and interrupts; control lines left unconnected.
static mc6821_interface ports_only_intf()
{
    mc6821_interface i{};
    i.in_a = cb_in_a;   i.in_b = cb_in_b;
    i.out_a = cb_out_a; i.out_b = cb_out_b;
    i.irq_a = cb_irq_a; i.irq_b = cb_irq_b;
    return i;
}

// ---------------------------------------------------------------------------
// Reset state and the DDR/port register split.
// ---------------------------------------------------------------------------
static void test_reset_and_register_select()
{
    probes_reset();
    mc6821 p;
    p.configure(ports_only_intf());

    // With control bit 2 clear the data address is the DDR, which resets to 0.
    CHECK_EQ(p.read(R_PORTA), 0x00, "DDR A after reset:");
    CHECK_EQ(p.read(R_PORTB), 0x00, "DDR B after reset:");
    CHECK_EQ(p.read(R_CTLA), 0x00, "control A after reset:");
    CHECK_EQ(p.read(R_CTLB), 0x00, "control B after reset:");

    // Writing the DDR while it is selected, then switching to the port.
    p.write(R_PORTA, 0xFF);                 // DDR A = all outputs
    CHECK_EQ(p.get_ddr_a(), 0xFF, "DDR A write:");
    CHECK_EQ(p.read(R_PORTA), 0xFF, "DDR A read back:");

    p.write(R_CTLA, SEL_PORT);              // now the data address is the port
    p.write(R_PORTA, 0x5A);
    CHECK_EQ(p.get_a(), 0x5A, "port A output latch:");
    CHECK_EQ(p.read(R_PORTA), 0x5A, "port A read back (all output):");

    // The DDR is still reachable by clearing the select bit again.
    p.write(R_CTLA, 0x00);
    CHECK_EQ(p.read(R_PORTA), 0xFF, "DDR A still readable:");
}

// ---------------------------------------------------------------------------
// Reads blend the output latch with the input pins per the DDR.
// ---------------------------------------------------------------------------
static void test_port_read_write()
{
    {   // All-input port A reads the pins
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        p.write(R_CTLA, SEL_PORT);          // DDR stays 0 = all inputs
        g_in_a = 0xA5;
        CHECK_EQ(p.read(R_PORTA), 0xA5, "all-input port A:");
    }
    {   // Mixed DDR: high nibble output, low nibble input
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        p.write(R_PORTA, 0xF0);             // DDR A
        p.write(R_CTLA, SEL_PORT);
        p.write(R_PORTA, 0xCC);             // output latch
        g_in_a = 0x0F;
        CHECK_EQ(p.read(R_PORTA), 0xCF, "mixed port A = (out & ddr) | (in & ~ddr):");
    }
    {   // The output callback sees the driven value; port A passes input pins
        // through, port B masks them to zero because they are high-impedance.
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        g_in_a = 0x0F; g_in_b = 0x0F;
        p.write(R_PORTA, 0xF0); p.write(R_PORTB, 0xF0);   // both DDRs mixed
        p.write(R_CTLA, SEL_PORT); p.write(R_CTLB, SEL_PORT);
        p.write(R_PORTA, 0xCC);
        CHECK_EQ(g_out_a, 0xCF, "port A output callback keeps input pins:");
        p.write(R_PORTB, 0xCC);
        CHECK_EQ(g_out_b, 0xC0, "port B output callback masks input pins to 0:");
    }
    {   // Widening the DDR later must expose bits an earlier write latched
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        p.write(R_CTLA, SEL_PORT);
        p.write(R_PORTA, 0x3C);             // latched while DDR is still 0
        p.write(R_CTLA, 0x00);
        p.write(R_PORTA, 0xFF);             // DDR A = all outputs
        CHECK_EQ(p.get_a(), 0x3C, "output latch survives a DDR widening:");
        CHECK_EQ(g_out_a, 0x3C, "DDR change re-drives the output callback:");
    }
    {   // Port B all-output reads back the latch, not the input pins
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        p.write(R_PORTB, 0xFF);
        p.write(R_CTLB, SEL_PORT);
        p.write(R_PORTB, 0x77);
        g_in_b = 0x00;
        CHECK_EQ(p.read(R_PORTB), 0x77, "all-output port B reads the latch:");
    }
    {   // Unconnected pins: port A pulls up, port B floats low
        probes_reset();
        mc6821 p;
        mc6821_interface i{};               // nothing wired at all
        p.configure(i);
        p.write(R_CTLA, SEL_PORT);
        p.write(R_CTLB, SEL_PORT);
        CHECK_EQ(p.read(R_PORTA), 0xFF, "unconnected port A pulls high:");
        CHECK_EQ(p.read(R_PORTB), 0x00, "unconnected port B floats low:");
    }
    {   // A pushed value is used when no input callback is wired
        probes_reset();
        mc6821 p;
        mc6821_interface i{};
        p.configure(i);
        p.write(R_CTLA, SEL_PORT);
        p.set_a(0x42);
        CHECK_EQ(p.read(R_PORTA), 0x42, "pushed port A value:");
    }
}

// ---------------------------------------------------------------------------
// CA1 / CB1 interrupts: edge selection, masking, and the read-clears rule.
// ---------------------------------------------------------------------------
static void test_c1_interrupts()
{
    {   // Disabled interrupt still sets the flag, but does not drive IRQ
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        p.write(R_CTLA, SEL_PORT | 0x02);   // C1 low-to-high, IRQ1 disabled
        p.set_ca1(0);
        p.set_ca1(1);                       // active transition
        CHECK_EQ(g_irq_a, -1, "IRQ A must not fire while masked:");
        CHECK(p.read(R_CTLA) & 0x80);       // ...but the flag is visible
    }
    {   // Enabling the interrupt with the flag already set asserts immediately
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        p.write(R_CTLA, SEL_PORT | 0x02);
        p.set_ca1(0);
        p.set_ca1(1);
        p.write(R_CTLA, SEL_PORT | 0x02 | 0x01);    // enable IRQ1 now
        CHECK_EQ(g_irq_a, 1, "unmasking an existing flag asserts IRQ A:");
    }
    {   // Enabled: the active edge asserts, the opposite edge does not
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        p.write(R_CTLA, SEL_PORT | 0x03);   // C1 low-to-high, IRQ1 enabled
        p.set_ca1(0);
        CHECK(g_irq_a != 1);
        p.set_ca1(1);
        CHECK_EQ(g_irq_a, 1, "CA1 rising edge asserts IRQ A:");
        CHECK(p.read(R_CTLA) & 0x80);       // IRQ1 flag in bit 7

        // Reading the PORT clears the flag and releases the line. Reading the
        // control register must NOT.
        CHECK_EQ(g_irq_a, 1, "reading control A must not clear the flag:");
        p.read(R_PORTA);
        CHECK_EQ(g_irq_a, 0, "reading port A releases IRQ A:");
        CHECK(!(p.read(R_CTLA) & 0x80));
    }
    {   // Falling-edge selection
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        p.write(R_CTLA, SEL_PORT | 0x01);   // C1 high-to-low, IRQ1 enabled
        p.set_ca1(1);
        CHECK(g_irq_a != 1);
        p.set_ca1(0);
        CHECK_EQ(g_irq_a, 1, "CA1 falling edge asserts IRQ A:");
    }
    {   // The B side behaves the same way through CB1
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        p.write(R_CTLB, SEL_PORT | 0x03);
        p.set_cb1(0);
        p.set_cb1(1);
        CHECK_EQ(g_irq_b, 1, "CB1 rising edge asserts IRQ B:");
        CHECK_EQ(g_irq_a, -1, "the A side is unaffected:");
        p.read(R_PORTB);
        CHECK_EQ(g_irq_b, 0, "reading port B releases IRQ B:");
    }
    {   // No spurious edge when the level is re-asserted at the same value
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        p.write(R_CTLA, SEL_PORT | 0x03);
        p.set_ca1(0);
        p.set_ca1(1);
        p.read(R_PORTA);                    // clear
        const int calls = g_irq_a_calls;
        p.set_ca1(1);                       // same level again
        CHECK_EQ(g_irq_a_calls, calls, "repeating a level is not an edge:");
    }
}

// ---------------------------------------------------------------------------
// CA2 / CB2 as interrupt inputs, and the IRQ2 flag visibility rule.
// ---------------------------------------------------------------------------
static void test_c2_input_interrupts()
{
    {
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        // C2 input (bit 5 = 0), low-to-high (bit 4 = 1), IRQ2 enabled (bit 3).
        p.write(R_CTLA, SEL_PORT | 0x18);
        p.set_ca2(0);
        CHECK(g_irq_a != 1);
        p.set_ca2(1);
        CHECK_EQ(g_irq_a, 1, "CA2 rising edge asserts IRQ A:");
        CHECK(p.read(R_CTLA) & 0x40);       // IRQ2 flag in bit 6
        p.read(R_PORTA);
        CHECK_EQ(g_irq_a, 0, "reading port A releases the CA2 interrupt:");
    }
    {   // With CA2 configured as an OUTPUT the IRQ2 flag is not reported...
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        p.write(R_CTLA, SEL_PORT | 0x18);   // input mode first
        p.set_ca2(0);
        p.set_ca2(1);                       // raise IRQ2
        CHECK(p.read(R_CTLA) & 0x40);
        p.write(R_CTLA, SEL_PORT | 0x38);   // switch CA2 to output
        CHECK(!(p.read(R_CTLA) & 0x40));    // flag no longer visible
    }
    {   // ...and a transition on the pin no longer raises one at all
        probes_reset();
        mc6821 p;
        p.configure(ports_only_intf());
        p.write(R_CTLA, SEL_PORT | 0x38);   // CA2 output, set/reset mode
        p.set_ca2(0);
        p.set_ca2(1);
        CHECK_EQ(g_irq_a, -1, "CA2 in output mode raises no interrupt:");
    }
}

// ---------------------------------------------------------------------------
// CA2 / CB2 as outputs: set-reset mode and the two strobe modes.
// ---------------------------------------------------------------------------
static void test_c2_output_modes()
{
    {   // Set/reset mode drives the pin straight from control bit 3
        probes_reset();
        mc6821 p;
        p.configure(full_intf());
        p.write(R_CTLA, SEL_PORT | 0x30);           // output, set/reset, bit3=0
        CHECK_EQ(g_out_ca2, 0, "CA2 set/reset drives low:");
        CHECK_EQ(p.get_ca2(), 0, "CA2 state low:");
        p.write(R_CTLA, SEL_PORT | 0x38);           // bit3 = 1
        CHECK_EQ(g_out_ca2, 1, "CA2 set/reset drives high:");
        CHECK_EQ(p.get_ca2(), 1, "CA2 state high:");
    }
    {   // Entering output mode always announces the level, even if it matches
        // the stale latch left over from before.
        probes_reset();
        mc6821 p;
        p.configure(full_intf());
        p.write(R_CTLA, SEL_PORT | 0x30);           // -> 0, one call
        CHECK_EQ(g_out_ca2_calls, 1, "first CA2 output call:");
        p.write(R_CTLA, SEL_PORT | 0x00);           // back to input mode
        p.write(R_CTLA, SEL_PORT | 0x30);           // -> 0 again
        CHECK_EQ(g_out_ca2_calls, 2, "re-entering output mode re-announces:");
    }
    {   // Read-strobe mode with E reset: a port A read pulses CA2 low then high
        probes_reset();
        mc6821 p;
        p.configure(full_intf());
        p.write(R_CTLA, SEL_PORT | 0x28);           // output, strobe, E reset
        CHECK_EQ(g_out_ca2, 1, "strobe mode idles high:");
        const int before = g_out_ca2_calls;
        p.read(R_PORTA);
        CHECK_EQ(g_out_ca2_calls, before + 2, "read pulses CA2 low then high:");
        CHECK_EQ(g_out_ca2, 1, "CA2 back high after the pulse:");
    }
    {   // Read-strobe mode with CA1 reset: CA2 stays low until a CA1 edge
        probes_reset();
        mc6821 p;
        p.configure(full_intf());
        p.write(R_CTLA, SEL_PORT | 0x22);           // output, strobe, C1 reset,
                                                    // CA1 low-to-high
        CHECK_EQ(g_out_ca2, 1, "strobe mode idles high:");
        p.read(R_PORTA);
        CHECK_EQ(g_out_ca2, 0, "read drives CA2 low and holds it:");
        p.set_ca1(0);
        p.set_ca1(1);                               // active CA1 transition
        CHECK_EQ(g_out_ca2, 1, "CA1 active edge releases CA2:");
    }
    {   // CB2 write-strobe with E reset: a port B WRITE pulses it
        probes_reset();
        mc6821 p;
        p.configure(full_intf());
        p.write(R_CTLB, SEL_PORT | 0x28);
        const int before = g_out_cb2_calls;
        p.write(R_PORTB, 0x00);                     // DDR B is 0 but the strobe
                                                    // still fires on the write
        CHECK_EQ(g_out_cb2_calls, before + 2, "write pulses CB2 low then high:");
        CHECK_EQ(g_out_cb2, 1, "CB2 back high:");
    }
    {   // CB2 write-strobe with CB1 reset. The asymmetry: unlike CA2, the CB1
        // edge does NOT release it -- a port B read that clears IRQ B1 does.
        probes_reset();
        mc6821 p;
        p.configure(full_intf());
        p.write(R_CTLB, SEL_PORT | 0x22);           // output, strobe, C1 reset,
                                                    // CB1 low-to-high
        p.write(R_PORTB, 0x00);
        CHECK_EQ(g_out_cb2, 0, "port B write drives CB2 low:");
        p.set_cb1(0);
        p.set_cb1(1);                               // active CB1 transition
        CHECK_EQ(g_out_cb2, 0, "CB1 edge alone does NOT release CB2:");
        p.read(R_PORTB);                            // clears IRQ B1
        CHECK_EQ(g_out_cb2, 1, "the port B read releases CB2:");
    }
}

// ---------------------------------------------------------------------------
// Two PIAs sharing one interrupt line must wire-OR.
// ---------------------------------------------------------------------------
static void test_shared_irq()
{
    probes_reset();
    mc6821 p0, p1;
    p0.configure(ports_only_intf());
    p1.configure(ports_only_intf());        // same irq_a callback pointer

    p0.write(R_CTLA, SEL_PORT | 0x03);
    p1.write(R_CTLA, SEL_PORT | 0x03);

    p0.set_ca1(0); p0.set_ca1(1);
    CHECK_EQ(g_irq_a, 1, "first PIA asserts the shared line:");

    p1.set_ca1(0); p1.set_ca1(1);
    CHECK_EQ(g_irq_a, 1, "second PIA also asserting keeps it high:");

    // Releasing ONE must not drop a line the other still holds.
    p0.read(R_PORTA);
    CHECK_EQ(g_irq_a, 1, "releasing one PIA must not drop the shared line:");

    p1.read(R_PORTA);
    CHECK_EQ(g_irq_a, 0, "the line drops once both have released:");
}

// ---------------------------------------------------------------------------
// Alternate register ordering swaps control A and port B.
// ---------------------------------------------------------------------------
static void test_alternate_ordering()
{
    probes_reset();
    mc6821 p;
    p.configure(ports_only_intf(), mc6821::ALTERNATE_ORDERING);

    // Under alternate ordering address 1 is port B / DDR B and address 2 is
    // control A. Prove it by writing DDR B through address 1.
    p.write(1, 0xFF);
    CHECK_EQ(p.get_ddr_b(), 0xFF, "alternate ordering: address 1 is DDR B:");
    CHECK_EQ(p.get_ddr_a(), 0x00, "...and not DDR A:");

    // Address 2 is control A. Setting its port-select bit must redirect the
    // next write to address 0 into the port latch instead of DDR A.
    p.write(2, SEL_PORT);
    p.write(0, 0x11);
    CHECK_EQ(p.get_ddr_a(), 0x00, "alternate ordering: address 2 is control A:");

    // Widen DDR A and the latch that write left behind becomes visible.
    p.write(2, 0x00);                       // control A: select the DDR again
    p.write(0, 0xFF);                       // DDR A = all outputs
    CHECK_EQ(p.get_a(), 0x11, "the port A latch held the earlier write:");
}

// ---------------------------------------------------------------------------
// The memory-map slot thunks.
// ---------------------------------------------------------------------------
static void test_slot_thunks()
{
    probes_reset();
    pia_detach_all();

    mc6821 p;
    p.configure(ports_only_intf());
    pia_attach(2, &p);
    CHECK(pia_slot(2) == &p);

    pia_2_w(R_PORTA, 0xFF, nullptr);        // DDR A
    pia_2_w(R_CTLA, SEL_PORT, nullptr);
    pia_2_w(R_PORTA, 0x5A, nullptr);
    CHECK_EQ(pia_2_r(R_PORTA, nullptr), 0x5A, "slot thunk round-trip:");

    // Offsets are masked to the low two bits, so a mirrored range still works.
    CHECK_EQ(pia_2_r(R_PORTA + 4, nullptr), 0x5A, "thunk masks the offset:");

    // An unattached slot must not crash.
    CHECK_EQ(pia_3_r(0, nullptr), 0xFF, "unattached slot reads 0xFF:");
    pia_3_w(0, 0x00, nullptr);

    // pia_reset_all only touches attached slots.
    pia_reset_all();
    CHECK_EQ(p.get_ddr_a(), 0x00, "pia_reset_all resets the attached PIA:");

    pia_detach_all();
    CHECK(pia_slot(2) == nullptr);
}

// ---------------------------------------------------------------------------
// A destructed PIA must leave the shared-IRQ registry consistent.
// ---------------------------------------------------------------------------
static void test_lifetime()
{
    probes_reset();
    mc6821 keeper;
    keeper.configure(ports_only_intf());
    keeper.write(R_CTLA, SEL_PORT | 0x03);
    keeper.set_ca1(0); keeper.set_ca1(1);
    CHECK_EQ(g_irq_a, 1, "keeper holds the line:");

    {
        mc6821 temp;
        temp.configure(ports_only_intf());
        temp.write(R_CTLA, SEL_PORT | 0x03);
        temp.set_ca1(0); temp.set_ca1(1);
        CHECK_EQ(g_irq_a, 1, "both hold the line:");
    }   // temp destructs here, deregistering itself

    // The keeper still holds it, and releasing now must still work cleanly.
    keeper.read(R_PORTA);
    CHECK_EQ(g_irq_a, 0, "line drops after the survivor releases:");
}

int main()
{
    std::printf("mc6821 tests\n");
    test_reset_and_register_select();
    test_port_read_write();
    test_c1_interrupts();
    test_c2_input_interrupts();
    test_c2_output_modes();
    test_shared_irq();
    test_alternate_ordering();
    test_slot_thunks();
    test_lifetime();

    std::printf("%s: %d checks, %d failure(s)\n",
                g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
