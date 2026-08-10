//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// Standalone assert-runner for cpu_m6800. Build & run:
//
//   WSL / Linux (from repo root):
//     g++ -std=c++17 -I aae/aae -I aae/aae/cpu_code -I aae/system/util \
//         aae/aae/cpu_code/tests/m6800_tests.cpp aae/aae/cpu_code/cpu_m6800.cpp \
//         -o /tmp/m6800_tests && /tmp/m6800_tests
//
//   Windows (from a VS developer prompt, at repo root):
//     cl /std:c++17 /EHsc /nologo /I aae\aae /I aae\aae\cpu_code /I aae\system\util ^
//        aae\aae\cpu_code\tests\m6800_tests.cpp aae\aae\cpu_code\cpu_m6800.cpp ^
//        /Fe:m6800_tests.exe && m6800_tests.exe
//
// The core is exercised against a flat 64K RAM: the handler tables are empty
// (terminator only), so every access falls through to the raw MEM pointer.
//
// The cycle/decode table below is written from the MC6800 datasheet rather than
// from the core, so it is an independent check on the decoder: a mis-slotted
// opcode or a wrong cycle count shows up as a mismatch instead of agreeing with
// itself.
#include "cpu_m6800.h"
#include "sys_log.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

// --- Stubs for the two core dependencies we do not want to link in ----------
// The core only logs unhandled bus accesses and illegal opcodes, and only drives
// the timer to keep interrupt latency honest; neither matters to these tests.
void timer_update(int, int) {}

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

// ---------------------------------------------------------------------------
// Test bench: one CPU over a flat 64K RAM.
// ---------------------------------------------------------------------------
static const uint16_t CODE = 0x0300;   // where test instructions are planted
static const uint16_t STACK = 0x01FF;  // initial SP (grows down, clear of CODE)

// Condition-code bits, mirrored from the core so the tests name them locally.
enum { F_C = 0x01, F_V = 0x02, F_Z = 0x04, F_N = 0x08, F_I = 0x10, F_H = 0x20 };

struct Bench {
    // The 64K RAM lives on the heap: a test function holds a dozen benches in
    // sibling scopes and MSVC reserves stack for all of them at once.
    std::vector<uint8_t> ram;
    uint8_t* mem;
    MemoryReadByte  rd[1];
    MemoryWriteByte wr[1];
    cpu_m6800 cpu;

    Bench() : ram(0x10000, 0), mem(ram.data()), cpu(mem, rd, wr, 0)
    {
        rd[0].lowAddr = 0xffffffff; rd[0].highAddr = 0xffffffff;
        rd[0].memoryCall = nullptr; rd[0].pUserArea = nullptr;
        wr[0].lowAddr = 0xffffffff; wr[0].highAddr = 0xffffffff;
        wr[0].memoryCall = nullptr; wr[0].pUserArea = nullptr;
        reset_to(CODE);
    }

    // Point the RESET vector at `pc`, reset, then give the CPU a usable stack.
    void reset_to(uint16_t pc)
    {
        mem[0xFFFE] = (uint8_t)(pc >> 8);
        mem[0xFFFF] = (uint8_t)pc;
        cpu.reset();
        cpu.SetSP(STACK);
    }

    void poke(uint16_t addr, uint8_t v) { mem[addr] = v; }
    uint8_t peek(uint16_t addr) const { return mem[addr]; }
    uint16_t peek16(uint16_t addr) const { return (uint16_t)((mem[addr] << 8) | mem[(uint16_t)(addr + 1)]); }

    // Plant an instruction at CODE and point the PC at it.
    void plant(std::initializer_list<uint8_t> bytes)
    {
        uint16_t a = CODE;
        for (uint8_t b : bytes) mem[a++] = b;
        cpu.SetPC(CODE);
    }

    int run1(std::initializer_list<uint8_t> bytes) { plant(bytes); return cpu.step(); }
};

// ---------------------------------------------------------------------------
// The MC6800 opcode map: cycles per opcode, -1 for opcodes the 6800 does not
// implement. Transcribed from the datasheet instruction-set summary.
// ---------------------------------------------------------------------------
#define XX (-1)
static const int OPCODE_CYCLES[256] = {
//        x0  x1  x2  x3  x4  x5  x6  x7  x8  x9  xA  xB  xC  xD  xE  xF
/* 0x */  XX,  2, XX, XX, XX, XX,  2,  2,  4,  4,  2,  2,  2,  2,  2,  2,
/* 1x */   2,  2, XX, XX, XX, XX,  2,  2, XX,  2, XX,  2, XX, XX, XX, XX,
/* 2x */   4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
/* 3x */   4,  4,  4,  4,  4,  4,  4,  4, XX,  5, XX, 10, XX, XX,  9, 12,
/* 4x */   2, XX, XX,  2,  2, XX,  2,  2,  2,  2,  2, XX,  2,  2, XX,  2,
/* 5x */   2, XX, XX,  2,  2, XX,  2,  2,  2,  2,  2, XX,  2,  2, XX,  2,
/* 6x */   7, XX, XX,  7,  7, XX,  7,  7,  7,  7,  7, XX,  7,  7,  4,  7,
/* 7x */   6, XX, XX,  6,  6, XX,  6,  6,  6,  6,  6, XX,  6,  6,  3,  6,
/* 8x */   2,  2,  2, XX,  2,  2,  2, XX,  2,  2,  2,  2,  3,  8,  3, XX,
/* 9x */   3,  3,  3, XX,  3,  3,  3,  4,  3,  3,  3,  3,  4, XX,  4,  5,
/* Ax */   5,  5,  5, XX,  5,  5,  5,  6,  5,  5,  5,  5,  6,  8,  6,  7,
/* Bx */   4,  4,  4, XX,  4,  4,  4,  5,  4,  4,  4,  4,  5,  9,  5,  6,
/* Cx */   2,  2,  2, XX,  2,  2,  2, XX,  2,  2,  2,  2, XX, XX,  3, XX,
/* Dx */   3,  3,  3, XX,  3,  3,  3,  4,  3,  3,  3,  3, XX, XX,  4,  5,
/* Ex */   5,  5,  5, XX,  5,  5,  5,  6,  5,  5,  5,  5, XX, XX,  6,  7,
/* Fx */   4,  4,  4, XX,  4,  4,  4,  5,  4,  4,  4,  4, XX, XX,  5,  6,
};
#undef XX

// $21 is unused on the 6800 (it is the 6801's BRN). The core decodes it in the
// branch block as "never taken", which costs the same 4 cycles as any branch,
// so the table above lists it as legal at 4.

// ---------------------------------------------------------------------------
// Every implemented opcode consumes exactly its datasheet cycle count, and
// every unimplemented one is reported and charged the core's flat illegal cost.
// ---------------------------------------------------------------------------
static void test_opcode_table()
{
    static const int ILLEGAL_COST = 2;
    int legal = 0, illegal = 0;

    for (int op = 0; op < 256; ++op)
    {
        Bench b;
        // Operands chosen so every mode lands in scratch RAM well clear of the
        // planted code and the stack: direct $40, extended $4000, indexed
        // X($1000) + $40, immediate $4000/$40.
        b.cpu.SetX(0x1000);
        int got = b.run1({ (uint8_t)op, 0x40, 0x00 });

        if (OPCODE_CYCLES[op] < 0) {
            ++illegal;
            CHECK_EQ(got, ILLEGAL_COST, "illegal opcode $%02X cycles:", op);
        } else {
            ++legal;
            CHECK_EQ(got, OPCODE_CYCLES[op], "opcode $%02X cycles:", op);
        }
    }
    std::printf("  opcode map: %d implemented, %d illegal\n", legal, illegal);
}

// ---------------------------------------------------------------------------
// Addressing modes resolve to the right effective address.
// ---------------------------------------------------------------------------
static void test_addressing_modes()
{
    {   // LDAA immediate
        Bench b;
        b.run1({ 0x86, 0x5A });
        CHECK_EQ(b.cpu.GetA(), 0x5A, "LDAA #$5A:");
        CHECK_EQ(b.cpu.GetPC(), CODE + 2, "LDAA imm PC:");
    }
    {   // LDAA direct - always page zero, no DP register on the 6800
        Bench b;
        b.poke(0x0040, 0x77);
        b.run1({ 0x96, 0x40 });
        CHECK_EQ(b.cpu.GetA(), 0x77, "LDAA $40:");
    }
    {   // LDAA indexed - X plus an UNSIGNED 8-bit offset (never sign-extended)
        Bench b;
        b.cpu.SetX(0x1000);
        b.poke(0x10FF, 0x33);
        b.run1({ 0xA6, 0xFF });
        CHECK_EQ(b.cpu.GetA(), 0x33, "LDAA $FF,X (offset must be unsigned):");
    }
    {   // LDAA extended
        Bench b;
        b.poke(0x4321, 0x99);
        b.run1({ 0xB6, 0x43, 0x21 });
        CHECK_EQ(b.cpu.GetA(), 0x99, "LDAA $4321:");
    }
    {   // Indexed addressing wraps within 16 bits
        Bench b;
        b.cpu.SetX(0xFFF0);
        b.poke(0x000F, 0x5C);
        b.run1({ 0xA6, 0x1F });
        CHECK_EQ(b.cpu.GetA(), 0x5C, "LDAA $1F,X with X=$FFF0 wraps:");
    }
    {   // 16-bit loads are big-endian
        Bench b;
        b.poke(0x4000, 0x12); b.poke(0x4001, 0x34);
        b.run1({ 0xFE, 0x40, 0x00 });          // LDX $4000
        CHECK_EQ(b.cpu.GetX(), 0x1234, "LDX $4000 big-endian:");
    }
    {   // 16-bit stores are big-endian
        Bench b;
        b.cpu.SetX(0xBEEF);
        b.run1({ 0xFF, 0x40, 0x00 });          // STX $4000
        CHECK_EQ(b.peek(0x4000), 0xBE, "STX high byte:");
        CHECK_EQ(b.peek(0x4001), 0xEF, "STX low byte:");
    }
    {   // The B-half of the grid targets accumulator B
        Bench b;
        b.run1({ 0xC6, 0x2B });                // LDAB #$2B
        CHECK_EQ(b.cpu.GetB(), 0x2B, "LDAB #$2B:");
        CHECK_EQ(b.cpu.GetA(), 0x00, "LDAB must not touch A:");
    }
}

// ---------------------------------------------------------------------------
// Arithmetic flags, including the half carry DAA depends on.
// ---------------------------------------------------------------------------
static void test_arithmetic_flags()
{
    {   // ADDA: half carry out of bit 3
        Bench b;
        b.cpu.SetA(0x0F);
        b.run1({ 0x8B, 0x01 });                // ADDA #$01
        CHECK_EQ(b.cpu.GetA(), 0x10, "ADDA $0F+$01:");
        CHECK(b.cpu.GetCC() & F_H);
        CHECK(!(b.cpu.GetCC() & F_C));
    }
    {   // ADDA: signed overflow and carry
        Bench b;
        b.cpu.SetA(0x7F);
        b.run1({ 0x8B, 0x01 });
        CHECK_EQ(b.cpu.GetA(), 0x80, "ADDA $7F+$01:");
        CHECK(b.cpu.GetCC() & F_V);            // +127 + 1 overflows
        CHECK(b.cpu.GetCC() & F_N);
        CHECK(!(b.cpu.GetCC() & F_C));
    }
    {
        Bench b;
        b.cpu.SetA(0xFF);
        b.run1({ 0x8B, 0x01 });
        CHECK_EQ(b.cpu.GetA(), 0x00, "ADDA $FF+$01:");
        CHECK(b.cpu.GetCC() & F_C);
        CHECK(b.cpu.GetCC() & F_Z);
        CHECK(!(b.cpu.GetCC() & F_V));         // -1 + 1 does not overflow
    }
    {   // ADCA picks up the incoming carry
        Bench b;
        b.cpu.SetA(0x10);
        b.cpu.SetCC(F_C);
        b.run1({ 0x89, 0x20 });                // ADCA #$20
        CHECK_EQ(b.cpu.GetA(), 0x31, "ADCA $10+$20+C:");
    }
    {   // SUBA sets carry as a BORROW
        Bench b;
        b.cpu.SetA(0x10);
        b.run1({ 0x80, 0x20 });                // SUBA #$20
        CHECK_EQ(b.cpu.GetA(), 0xF0, "SUBA $10-$20:");
        CHECK(b.cpu.GetCC() & F_C);            // borrow
        CHECK(b.cpu.GetCC() & F_N);
    }
    {   // Subtraction overflow: -128 - 1
        Bench b;
        b.cpu.SetA(0x80);
        b.run1({ 0x80, 0x01 });
        CHECK_EQ(b.cpu.GetA(), 0x7F, "SUBA $80-$01:");
        CHECK(b.cpu.GetCC() & F_V);
        CHECK(!(b.cpu.GetCC() & F_C));
    }
    {   // SBCA borrows the incoming carry
        Bench b;
        b.cpu.SetA(0x10);
        b.cpu.SetCC(F_C);
        b.run1({ 0x82, 0x05 });                // SBCA #$05
        CHECK_EQ(b.cpu.GetA(), 0x0A, "SBCA $10-$05-C:");
    }
    {   // CMPA compares without writing back
        Bench b;
        b.cpu.SetA(0x42);
        b.run1({ 0x81, 0x42 });                // CMPA #$42
        CHECK_EQ(b.cpu.GetA(), 0x42, "CMPA must not modify A:");
        CHECK(b.cpu.GetCC() & F_Z);
        CHECK(!(b.cpu.GetCC() & F_C));
    }
    {   // ABA / SBA / CBA operate between the two accumulators
        Bench b;
        b.cpu.SetA(0x30); b.cpu.SetB(0x12);
        b.run1({ 0x1B });                      // ABA
        CHECK_EQ(b.cpu.GetA(), 0x42, "ABA:");
    }
    {
        Bench b;
        b.cpu.SetA(0x30); b.cpu.SetB(0x12);
        b.run1({ 0x10 });                      // SBA
        CHECK_EQ(b.cpu.GetA(), 0x1E, "SBA:");
    }
    {
        Bench b;
        b.cpu.SetA(0x30); b.cpu.SetB(0x30);
        b.run1({ 0x11 });                      // CBA
        CHECK_EQ(b.cpu.GetA(), 0x30, "CBA must not modify A:");
        CHECK(b.cpu.GetCC() & F_Z);
    }
    {   // Subtraction leaves the half carry alone (only additions compute it)
        Bench b;
        b.cpu.SetA(0x10);
        b.cpu.SetCC(F_H);
        b.run1({ 0x80, 0x01 });                // SUBA #$01
        CHECK(b.cpu.GetCC() & F_H);
    }
    {   // INC/DEC set V at the signed boundary and leave carry alone
        Bench b;
        b.cpu.SetA(0x7F);
        b.cpu.SetCC(F_C);
        b.run1({ 0x4C });                      // INCA
        CHECK_EQ(b.cpu.GetA(), 0x80, "INCA $7F:");
        CHECK(b.cpu.GetCC() & F_V);
        CHECK(b.cpu.GetCC() & F_C);            // carry untouched by INC
    }
    {
        Bench b;
        b.cpu.SetA(0x80);
        b.run1({ 0x4A });                      // DECA
        CHECK_EQ(b.cpu.GetA(), 0x7F, "DECA $80:");
        CHECK(b.cpu.GetCC() & F_V);
    }
    {   // NEG: V only at $80, C set for any non-zero result
        Bench b;
        b.cpu.SetA(0x80);
        b.run1({ 0x40 });                      // NEGA
        CHECK_EQ(b.cpu.GetA(), 0x80, "NEGA $80:");
        CHECK(b.cpu.GetCC() & F_V);
        CHECK(b.cpu.GetCC() & F_C);
    }
    {
        Bench b;
        b.cpu.SetA(0x00);
        b.run1({ 0x40 });
        CHECK_EQ(b.cpu.GetA(), 0x00, "NEGA $00:");
        CHECK(!(b.cpu.GetCC() & F_C));         // zero result clears carry
        CHECK(b.cpu.GetCC() & F_Z);
    }
    {   // COM always sets carry
        Bench b;
        b.cpu.SetA(0x0F);
        b.run1({ 0x43 });                      // COMA
        CHECK_EQ(b.cpu.GetA(), 0xF0, "COMA $0F:");
        CHECK(b.cpu.GetCC() & F_C);
        CHECK(!(b.cpu.GetCC() & F_V));
    }
    {   // TST and CLR both clear V and C
        Bench b;
        b.cpu.SetA(0x00);
        b.cpu.SetCC(F_C | F_V);
        b.run1({ 0x4D });                      // TSTA
        CHECK(b.cpu.GetCC() & F_Z);
        CHECK(!(b.cpu.GetCC() & (F_C | F_V)));
    }
    {
        Bench b;
        b.cpu.SetA(0xFF);
        b.cpu.SetCC(F_C | F_V | F_N);
        b.run1({ 0x4F });                      // CLRA
        CHECK_EQ(b.cpu.GetA(), 0x00, "CLRA:");
        CHECK(b.cpu.GetCC() & F_Z);
        CHECK(!(b.cpu.GetCC() & (F_C | F_V | F_N)));
    }
    {   // Logic ops clear V and leave carry alone
        Bench b;
        b.cpu.SetA(0xF0);
        b.cpu.SetCC(F_C | F_V);
        b.run1({ 0x84, 0x3C });                // ANDA #$3C
        CHECK_EQ(b.cpu.GetA(), 0x30, "ANDA:");
        CHECK(!(b.cpu.GetCC() & F_V));
        CHECK(b.cpu.GetCC() & F_C);
    }
    {   // BIT tests without writing back
        Bench b;
        b.cpu.SetA(0xF0);
        b.run1({ 0x85, 0x0F });                // BITA #$0F
        CHECK_EQ(b.cpu.GetA(), 0xF0, "BITA must not modify A:");
        CHECK(b.cpu.GetCC() & F_Z);
    }
}

// ---------------------------------------------------------------------------
// Shifts and rotates: every one sets V = N ^ C.
// ---------------------------------------------------------------------------
static void test_shifts_and_rotates()
{
    {   // LSR always clears N, so V tracks C
        Bench b;
        b.cpu.SetA(0x01);
        b.run1({ 0x44 });                      // LSRA
        CHECK_EQ(b.cpu.GetA(), 0x00, "LSRA $01:");
        CHECK(b.cpu.GetCC() & F_C);
        CHECK(b.cpu.GetCC() & F_Z);
        CHECK(b.cpu.GetCC() & F_V);            // V = N^C = 0^1
        CHECK(!(b.cpu.GetCC() & F_N));
    }
    {   // ASR preserves the sign bit
        Bench b;
        b.cpu.SetA(0x80);
        b.run1({ 0x47 });                      // ASRA
        CHECK_EQ(b.cpu.GetA(), 0xC0, "ASRA $80:");
        CHECK(!(b.cpu.GetCC() & F_C));
        CHECK(b.cpu.GetCC() & F_N);
        CHECK(b.cpu.GetCC() & F_V);            // V = N^C = 1^0
    }
    {   // ASL shifts the top bit into carry
        Bench b;
        b.cpu.SetA(0xC0);
        b.run1({ 0x48 });                      // ASLA
        CHECK_EQ(b.cpu.GetA(), 0x80, "ASLA $C0:");
        CHECK(b.cpu.GetCC() & F_C);
        CHECK(b.cpu.GetCC() & F_N);
        CHECK(!(b.cpu.GetCC() & F_V));         // V = N^C = 1^1
    }
    {   // ROL rotates the old carry into bit 0
        Bench b;
        b.cpu.SetA(0x80);
        b.cpu.SetCC(F_C);
        b.run1({ 0x49 });                      // ROLA
        CHECK_EQ(b.cpu.GetA(), 0x01, "ROLA $80 with C:");
        CHECK(b.cpu.GetCC() & F_C);
    }
    {   // ROR rotates the old carry into bit 7
        Bench b;
        b.cpu.SetA(0x01);
        b.cpu.SetCC(F_C);
        b.run1({ 0x46 });                      // RORA
        CHECK_EQ(b.cpu.GetA(), 0x80, "RORA $01 with C:");
        CHECK(b.cpu.GetCC() & F_C);
        CHECK(b.cpu.GetCC() & F_N);
    }
    {   // The memory forms read, modify and write back the effective address
        Bench b;
        b.poke(0x4000, 0x01);
        b.run1({ 0x79, 0x40, 0x00 });          // ROL $4000
        CHECK_EQ(b.peek(0x4000), 0x02, "ROL $4000:");
    }
    {   // ...including the indexed form
        Bench b;
        b.cpu.SetX(0x2000);
        b.poke(0x2010, 0x40);
        b.run1({ 0x6C, 0x10 });                // INC $10,X
        CHECK_EQ(b.peek(0x2010), 0x41, "INC $10,X:");
    }
    {   // CLR writes without reading anything meaningful back
        Bench b;
        b.poke(0x4000, 0xFF);
        b.run1({ 0x7F, 0x40, 0x00 });          // CLR $4000
        CHECK_EQ(b.peek(0x4000), 0x00, "CLR $4000:");
    }
    {   // TST is read-only
        Bench b;
        b.poke(0x4000, 0x80);
        b.run1({ 0x7D, 0x40, 0x00 });          // TST $4000
        CHECK_EQ(b.peek(0x4000), 0x80, "TST must not write:");
        CHECK(b.cpu.GetCC() & F_N);
    }
}

// ---------------------------------------------------------------------------
// DAA, the BCD fixup that consumes H and C.
// ---------------------------------------------------------------------------
static void test_daa()
{
    struct Case { uint8_t a, b_in; const char* label; uint8_t want; bool want_c; };
    // Each case adds two BCD values with ADDA, then corrects with DAA.
    static const Case cases[] = {
        { 0x19, 0x01, "19 + 01", 0x20, false },
        { 0x25, 0x48, "25 + 48", 0x73, false },
        { 0x39, 0x47, "39 + 47", 0x86, false },
        { 0x52, 0x59, "52 + 59", 0x11, true  },
        { 0x99, 0x01, "99 + 01", 0x00, true  },
        { 0x99, 0x99, "99 + 99", 0x98, true  },
        { 0x00, 0x00, "00 + 00", 0x00, false },
    };

    for (const Case& c : cases) {
        Bench b;
        b.cpu.SetA(c.a);
        b.run1({ 0x8B, c.b_in });              // ADDA #b
        b.run1({ 0x19 });                      // DAA
        CHECK_EQ(b.cpu.GetA(), c.want, "DAA %s:", c.label);
        CHECK_EQ((b.cpu.GetCC() & F_C) != 0, c.want_c, "DAA %s carry:", c.label);
    }
}

// ---------------------------------------------------------------------------
// CPX is the 6800's compare-index quirk: N, Z and V move, carry does not.
// ---------------------------------------------------------------------------
static void test_cpx()
{
    {
        Bench b;
        b.cpu.SetX(0x1234);
        b.run1({ 0x8C, 0x12, 0x34 });          // CPX #$1234
        CHECK(b.cpu.GetCC() & F_Z);
        CHECK_EQ(b.cpu.GetX(), 0x1234, "CPX must not modify X:");
    }
    {   // X < operand would set carry on a 6801; on a 6800 carry must not move.
        Bench b;
        b.cpu.SetX(0x0001);
        b.cpu.SetCC(0);                        // carry clear going in
        b.run1({ 0x8C, 0xFF, 0xFF });          // CPX #$FFFF
        CHECK(!(b.cpu.GetCC() & F_Z));
        CHECK(!(b.cpu.GetCC() & F_C));         // still clear: CPX never sets C
    }
    {   // ...and it does not clear a carry that was already set either.
        Bench b;
        b.cpu.SetX(0x8000);
        b.cpu.SetCC(F_C);
        b.run1({ 0x8C, 0x00, 0x01 });          // CPX #$0001
        CHECK(b.cpu.GetCC() & F_C);
    }
    {   // Signed overflow in the 16-bit compare
        Bench b;
        b.cpu.SetX(0x8000);
        b.run1({ 0x8C, 0x00, 0x01 });          // $8000 - $0001 overflows
        CHECK(b.cpu.GetCC() & F_V);
    }
}

// ---------------------------------------------------------------------------
// Branches, and the index/stack-pointer transfer group.
// ---------------------------------------------------------------------------
static void test_branches_and_index()
{
    {   // BRA takes a signed offset relative to the byte after the operand
        Bench b;
        b.run1({ 0x20, 0x10 });                // BRA +$10
        CHECK_EQ(b.cpu.GetPC(), CODE + 2 + 0x10, "BRA forward:");
    }
    {   // Backward branch
        Bench b;
        b.run1({ 0x20, 0xFE });                // BRA -2 (branch to itself)
        CHECK_EQ(b.cpu.GetPC(), CODE, "BRA -2:");
    }
    {   // Not-taken branches still consume the offset byte
        Bench b;
        b.cpu.SetCC(0);                        // Z clear
        b.run1({ 0x27, 0x10 });                // BEQ +$10
        CHECK_EQ(b.cpu.GetPC(), CODE + 2, "BEQ not taken:");
    }
    {   // BHI is the unsigned "greater than": not C and not Z
        Bench b;
        b.cpu.SetCC(0);
        b.run1({ 0x22, 0x10 });                // BHI
        CHECK_EQ(b.cpu.GetPC(), CODE + 2 + 0x10, "BHI taken:");
    }
    {
        Bench b;
        b.cpu.SetCC(F_Z);
        b.run1({ 0x22, 0x10 });
        CHECK_EQ(b.cpu.GetPC(), CODE + 2, "BHI not taken when Z:");
    }
    {   // BGT is the signed form: not Z and N == V
        Bench b;
        b.cpu.SetCC(F_N | F_V);
        b.run1({ 0x2E, 0x10 });                // BGT
        CHECK_EQ(b.cpu.GetPC(), CODE + 2 + 0x10, "BGT with N==V:");
    }
    {
        Bench b;
        b.cpu.SetCC(F_N);
        b.run1({ 0x2E, 0x10 });
        CHECK_EQ(b.cpu.GetPC(), CODE + 2, "BGT not taken with N!=V:");
    }
    {   // INX/DEX touch only Z
        Bench b;
        b.cpu.SetX(0xFFFF);
        b.cpu.SetCC(F_C | F_N | F_V);
        b.run1({ 0x08 });                      // INX
        CHECK_EQ(b.cpu.GetX(), 0x0000, "INX wrap:");
        CHECK(b.cpu.GetCC() & F_Z);
        CHECK(b.cpu.GetCC() & (F_C | F_N | F_V));   // untouched
    }
    {
        Bench b;
        b.cpu.SetX(0x0001);
        b.run1({ 0x09 });                      // DEX
        CHECK_EQ(b.cpu.GetX(), 0x0000, "DEX:");
        CHECK(b.cpu.GetCC() & F_Z);
    }
    {   // TSX loads X with SP+1; TXS sets SP to X-1. They are inverses.
        Bench b;
        b.cpu.SetSP(0x0100);
        b.run1({ 0x30 });                      // TSX
        CHECK_EQ(b.cpu.GetX(), 0x0101, "TSX = SP+1:");
        b.run1({ 0x35 });                      // TXS
        CHECK_EQ(b.cpu.GetSP(), 0x0100, "TXS = X-1 round-trips:");
    }
    {   // TAP / TPA move the whole condition-code register
        Bench b;
        b.cpu.SetA(F_C | F_Z);
        b.run1({ 0x06 });                      // TAP
        CHECK_EQ(b.cpu.GetCC(), 0xC0 | F_C | F_Z, "TAP (bits 6-7 read as 1):");
        b.run1({ 0x07 });                      // TPA
        CHECK_EQ(b.cpu.GetA(), 0xC0 | F_C | F_Z, "TPA:");
    }
    {   // The flag set/clear group
        Bench b;
        b.run1({ 0x0D }); CHECK(b.cpu.GetCC() & F_C);   // SEC
        b.run1({ 0x0C }); CHECK(!(b.cpu.GetCC() & F_C));// CLC
        b.run1({ 0x0B }); CHECK(b.cpu.GetCC() & F_V);   // SEV
        b.run1({ 0x0A }); CHECK(!(b.cpu.GetCC() & F_V));// CLV
        b.run1({ 0x0F }); CHECK(b.cpu.GetCC() & F_I);   // SEI
        b.run1({ 0x0E }); CHECK(!(b.cpu.GetCC() & F_I));// CLI
    }
    {   // JMP
        Bench b;
        b.run1({ 0x7E, 0x12, 0x34 });          // JMP $1234
        CHECK_EQ(b.cpu.GetPC(), 0x1234, "JMP extended:");
    }
    {
        Bench b;
        b.cpu.SetX(0x2000);
        b.run1({ 0x6E, 0x34 });                // JMP $34,X
        CHECK_EQ(b.cpu.GetPC(), 0x2034, "JMP indexed:");
    }
}

// ---------------------------------------------------------------------------
// Stack: push/pull, and the call/return pair.
// ---------------------------------------------------------------------------
static void test_stack_and_calls()
{
    {   // PSHA writes at SP then post-decrements; PULA pre-increments then reads
        Bench b;
        b.cpu.SetA(0x5A);
        b.run1({ 0x36 });                      // PSHA
        CHECK_EQ(b.cpu.GetSP(), STACK - 1, "PSHA post-decrements SP:");
        CHECK_EQ(b.peek(STACK), 0x5A, "PSHA writes at the old SP:");
        b.cpu.SetA(0x00);
        b.run1({ 0x32 });                      // PULA
        CHECK_EQ(b.cpu.GetA(), 0x5A, "PULA round-trips:");
        CHECK_EQ(b.cpu.GetSP(), STACK, "PULA restores SP:");
    }
    {   // INS / DES move SP without touching memory
        Bench b;
        b.run1({ 0x31 });                      // INS
        CHECK_EQ(b.cpu.GetSP(), STACK + 1, "INS:");
        b.run1({ 0x34 });                      // DES
        CHECK_EQ(b.cpu.GetSP(), STACK, "DES:");
    }
    {   // JSR pushes the return address low byte first (PCL at the higher slot)
        Bench b;
        b.run1({ 0xBD, 0x12, 0x34 });          // JSR $1234
        uint16_t ret = CODE + 3;
        CHECK_EQ(b.cpu.GetPC(), 0x1234, "JSR extended target:");
        CHECK_EQ(b.peek(STACK), (uint8_t)ret, "JSR pushes PCL at the top slot:");
        CHECK_EQ(b.peek(STACK - 1), (uint8_t)(ret >> 8), "JSR pushes PCH below it:");
        CHECK_EQ(b.cpu.GetSP(), STACK - 2, "JSR consumes two stack bytes:");

        b.plant({ 0x39 });                     // RTS - planted, but PC is at $1234
        b.cpu.SetPC(0x1234);
        b.poke(0x1234, 0x39);
        b.cpu.step();
        CHECK_EQ(b.cpu.GetPC(), ret, "RTS returns to the byte after JSR:");
        CHECK_EQ(b.cpu.GetSP(), STACK, "RTS restores SP:");
    }
    {   // JSR indexed
        Bench b;
        b.cpu.SetX(0x2000);
        b.run1({ 0xAD, 0x40 });                // JSR $40,X
        CHECK_EQ(b.cpu.GetPC(), 0x2040, "JSR indexed target:");
        CHECK_EQ(b.cpu.GetSP(), STACK - 2, "JSR indexed pushes a return address:");
    }
    {   // BSR is relative, like a branch, but stacks a return address
        Bench b;
        b.run1({ 0x8D, 0x10 });                // BSR +$10
        CHECK_EQ(b.cpu.GetPC(), CODE + 2 + 0x10, "BSR target:");
        CHECK_EQ(b.peek(STACK), (uint8_t)((CODE + 2) & 0xFF), "BSR return PCL:");
        CHECK_EQ(b.peek(STACK - 1), (uint8_t)((CODE + 2) >> 8), "BSR return PCH:");
    }
    {   // LDS / STS reach the stack pointer
        Bench b;
        b.run1({ 0x8E, 0x0A, 0xBC });          // LDS #$0ABC
        CHECK_EQ(b.cpu.GetSP(), 0x0ABC, "LDS immediate:");
        b.run1({ 0xBF, 0x40, 0x00 });          // STS $4000
        CHECK_EQ(b.peek16(0x4000), 0x0ABC, "STS extended:");
    }
}

// ---------------------------------------------------------------------------
// Interrupts: masking, priority, the stack frame, RTI and WAI.
// ---------------------------------------------------------------------------

// The interrupt frame, from the pushed SP upward: CC, B, A, XH, XL, PCH, PCL.
static void check_frame(Bench& b, uint16_t pc, uint16_t x, uint8_t a, uint8_t bb, uint8_t cc,
                        const char* what)
{
    CHECK_EQ(b.peek(STACK),     (uint8_t)pc,        "%s frame PCL:", what);
    CHECK_EQ(b.peek(STACK - 1), (uint8_t)(pc >> 8), "%s frame PCH:", what);
    CHECK_EQ(b.peek(STACK - 2), (uint8_t)x,         "%s frame XL:",  what);
    CHECK_EQ(b.peek(STACK - 3), (uint8_t)(x >> 8),  "%s frame XH:",  what);
    CHECK_EQ(b.peek(STACK - 4), a,                  "%s frame A:",   what);
    CHECK_EQ(b.peek(STACK - 5), bb,                 "%s frame B:",   what);
    CHECK_EQ(b.peek(STACK - 6), cc,                 "%s frame CC:",  what);
    CHECK_EQ(b.cpu.GetSP(), STACK - 7,              "%s frame size:", what);
}

static void test_interrupts()
{
    {   // Reset takes the vector from $FFFE and masks IRQ
        Bench b;
        b.poke(0xFFFE, 0xE0); b.poke(0xFFFF, 0x00);
        b.cpu.reset();
        CHECK_EQ(b.cpu.GetPC(), 0xE000, "reset vector:");
        CHECK(b.cpu.GetCC() & F_I);
    }
    {   // A masked IRQ does not fire; the CPU just runs the next instruction
        Bench b;
        b.cpu.SetCC(F_I);
        b.cpu.m6800_Cause_Interrupt(M6800_INT_IRQ);
        int c = b.run1({ 0x01 });              // NOP
        CHECK_EQ(c, 2, "masked IRQ runs the instruction instead:");
        CHECK_EQ(b.cpu.GetPC(), CODE + 1, "masked IRQ leaves PC alone:");
    }
    {   // Unmasking lets the still-pending request through
        Bench b;
        b.cpu.SetCC(F_I);
        b.cpu.m6800_Cause_Interrupt(M6800_INT_IRQ);
        b.run1({ 0x0E });                      // CLI
        b.poke(0xFFF8, 0xC0); b.poke(0xFFF9, 0x00);
        b.cpu.SetPC(CODE + 1);
        int c = b.cpu.step();
        CHECK_EQ(c, 12, "IRQ entry cost:");
        CHECK_EQ(b.cpu.GetPC(), 0xC000, "IRQ vector $FFF8:");
        CHECK(b.cpu.GetCC() & F_I);            // entry masks further IRQs
    }
    {   // The pushed frame, and that the latch is one-shot
        Bench b;
        b.cpu.SetCC(0);
        b.cpu.SetA(0xAA); b.cpu.SetB(0xBB); b.cpu.SetX(0x1234);
        b.cpu.SetPC(CODE);
        b.poke(0xFFF8, 0xC0); b.poke(0xFFF9, 0x00);
        b.cpu.m6800_Cause_Interrupt(M6800_INT_IRQ);
        b.cpu.step();
        check_frame(b, CODE, 0x1234, 0xAA, 0xBB, 0xC0, "IRQ");

        // One assertion = one interrupt: the next step must execute code.
        b.poke(0xC000, 0x01);                  // NOP at the handler
        int c = b.cpu.step();
        CHECK_EQ(c, 2, "IRQ latch is one-shot:");
    }
    {   // NMI is never masked and outranks IRQ
        Bench b;
        b.cpu.SetCC(F_I);
        b.poke(0xFFFC, 0xD0); b.poke(0xFFFD, 0x00);
        b.cpu.m6800_Cause_Interrupt(M6800_INT_NMI);
        int c = b.cpu.step();
        CHECK_EQ(c, 12, "NMI entry cost:");
        CHECK_EQ(b.cpu.GetPC(), 0xD000, "NMI vector $FFFC:");
    }
    {
        Bench b;
        b.cpu.SetCC(0);
        b.poke(0xFFFC, 0xD0); b.poke(0xFFFD, 0x00);
        b.poke(0xFFF8, 0xC0); b.poke(0xFFF9, 0x00);
        b.cpu.m6800_Cause_Interrupt(M6800_INT_NMI | M6800_INT_IRQ);
        b.cpu.step();
        CHECK_EQ(b.cpu.GetPC(), 0xD000, "NMI outranks IRQ:");
        b.poke(0xD000, 0x0E);                  // CLI, so the pending IRQ can run
        b.cpu.step();
        b.cpu.step();
        CHECK_EQ(b.cpu.GetPC(), 0xC000, "the IRQ is still pending afterwards:");
    }
    {   // SWI vectors through $FFFA and stacks the same frame
        Bench b;
        b.cpu.SetCC(0);
        b.cpu.SetA(0xAA); b.cpu.SetB(0xBB); b.cpu.SetX(0x1234);
        b.poke(0xFFFA, 0xB0); b.poke(0xFFFB, 0x00);
        b.run1({ 0x3F });                      // SWI
        CHECK_EQ(b.cpu.GetPC(), 0xB000, "SWI vector $FFFA:");
        check_frame(b, CODE + 1, 0x1234, 0xAA, 0xBB, 0xC0, "SWI");
        CHECK(b.cpu.GetCC() & F_I);
    }
    {   // RTI is the exact inverse of the interrupt push
        Bench b;
        b.cpu.SetCC(F_C);
        b.cpu.SetA(0x11); b.cpu.SetB(0x22); b.cpu.SetX(0x3344);
        b.poke(0xFFFA, 0xB0); b.poke(0xFFFB, 0x00);
        b.run1({ 0x3F });                      // SWI stacks everything
        b.cpu.SetA(0); b.cpu.SetB(0); b.cpu.SetX(0); b.cpu.SetCC(0);
        b.poke(0xB000, 0x3B);                  // RTI
        int c = b.cpu.step();
        CHECK_EQ(c, 10, "RTI cost:");
        CHECK_EQ(b.cpu.GetA(), 0x11, "RTI restores A:");
        CHECK_EQ(b.cpu.GetB(), 0x22, "RTI restores B:");
        CHECK_EQ(b.cpu.GetX(), 0x3344, "RTI restores X:");
        CHECK_EQ(b.cpu.GetCC(), 0xC0 | F_C, "RTI restores CC:");
        CHECK_EQ(b.cpu.GetPC(), CODE + 1, "RTI restores PC:");
        CHECK_EQ(b.cpu.GetSP(), STACK, "RTI restores SP:");
    }
    {   // WAI stacks up front, then idles until an interrupt is actually taken
        Bench b;
        b.cpu.SetCC(0);
        b.cpu.SetA(0xAA); b.cpu.SetB(0xBB); b.cpu.SetX(0x1234);
        b.poke(0xFFF8, 0xC0); b.poke(0xFFF9, 0x00);
        int c = b.run1({ 0x3E });              // WAI
        CHECK_EQ(c, 9, "WAI cost:");
        check_frame(b, CODE + 1, 0x1234, 0xAA, 0xBB, 0xC0, "WAI");

        // Idle: one cycle per step, PC parked, nothing else stacked.
        CHECK_EQ(b.cpu.step(), 1, "WAI idles one cycle per step:");
        CHECK_EQ(b.cpu.step(), 1, "WAI keeps idling:");
        CHECK_EQ(b.cpu.GetSP(), STACK - 7, "WAI does not restack while idling:");

        // The wake-up is cheap because the frame is already on the stack.
        b.cpu.m6800_Cause_Interrupt(M6800_INT_IRQ);
        CHECK_EQ(b.cpu.step(), 4, "WAI wake-up cost:");
        CHECK_EQ(b.cpu.GetPC(), 0xC000, "WAI wakes into the handler:");
        CHECK_EQ(b.cpu.GetSP(), STACK - 7, "WAI wake-up must not restack:");
    }
    {   // A masked IRQ does NOT end a WAI; an NMI always does
        Bench b;
        b.cpu.SetCC(F_I);
        b.poke(0xFFFC, 0xD0); b.poke(0xFFFD, 0x00);
        b.run1({ 0x3E });                      // WAI
        b.cpu.m6800_Cause_Interrupt(M6800_INT_IRQ);
        CHECK_EQ(b.cpu.step(), 1, "masked IRQ does not wake WAI:");
        b.cpu.m6800_Cause_Interrupt(M6800_INT_NMI);
        CHECK_EQ(b.cpu.step(), 4, "NMI wakes WAI:");
        CHECK_EQ(b.cpu.GetPC(), 0xD000, "WAI woken by NMI takes the NMI vector:");
    }
    {   // exec() must leave a WAI promptly rather than burning the whole slice
        Bench b;
        b.cpu.SetCC(0);
        b.poke(0xFFF8, 0xC0); b.poke(0xFFF9, 0x00);
        b.poke(0xC000, 0x20); b.poke(0xC001, 0xFE);   // BRA -2 (spin in handler)
        b.run1({ 0x3E });                      // WAI
        b.cpu.m6800_Cause_Interrupt(M6800_INT_IRQ);
        b.cpu.exec(64);
        CHECK(b.cpu.GetPC() == 0xC000);        // reached and spinning in the handler
    }
    {   // Clearing pending interrupts drops the latches
        Bench b;
        b.cpu.SetCC(0);
        b.cpu.m6800_Cause_Interrupt(M6800_INT_IRQ | M6800_INT_NMI);
        b.cpu.m6800_Clear_Pending_Interrupts();
        int c = b.run1({ 0x01 });              // NOP
        CHECK_EQ(c, 2, "cleared interrupts do not fire:");
    }
}

// ---------------------------------------------------------------------------
// exec() honours its cycle budget and the tick counter tracks it.
// ---------------------------------------------------------------------------
static void test_exec_and_ticks()
{
    Bench b;
    // A tight NOP loop: NOP (2) then BRA -3 (4) = 6 cycles per iteration.
    b.poke(CODE + 0, 0x01);
    b.poke(CODE + 1, 0x20);
    b.poke(CODE + 2, 0xFD);
    b.cpu.SetPC(CODE);
    b.cpu.get_ticks(1);                        // zero the counter

    int ran = b.cpu.exec(60);
    CHECK(ran >= 60);                          // exec runs AT LEAST the budget
    CHECK(ran <= 60 + 4);                      // ...overshooting by one instruction at most
    CHECK_EQ(b.cpu.get_ticks(0), ran, "get_ticks matches what exec reported:");
    CHECK_EQ(b.cpu.get_ticks(1), ran, "get_ticks(reset) returns the total:");
    CHECK_EQ(b.cpu.get_ticks(0), 0, "get_ticks(reset) then zeroes it:");
}

int main()
{
    std::printf("cpu_m6800 tests\n");
    test_opcode_table();
    test_addressing_modes();
    test_arithmetic_flags();
    test_shifts_and_rotates();
    test_daa();
    test_cpx();
    test_branches_and_index();
    test_stack_and_calls();
    test_interrupts();
    test_exec_and_ticks();

    std::printf("%s: %d checks, %d failure(s)\n",
                g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
