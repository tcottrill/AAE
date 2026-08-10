// -----------------------------------------------------------------------------
// Motorola MC6800 / MC6802 / MC6808 CPU Core - implementation
//
// See cpu_m6800.h for design notes.
// -----------------------------------------------------------------------------

#include "cpu_m6800.h"
#include "sys_log.h"
#include "timer.h"

// =============================================================================
// Construction / reset / ticks
// =============================================================================

cpu_m6800::cpu_m6800(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, int num)
{
    MEM = mem;
    memory_read = read_mem;
    memory_write = write_mem;
    cpu_num = num;
}

// One-shot interrupt request, edge style, to match the AAE scheduler which
// pulses cpu_do_int_imm() once per interrupt period. We simply raise the
// matching level input; the core lowers it again when the interrupt is taken
// (see the dispatch block at the top of step()). A still-masked request stays
// pending until the mask clears, then fires exactly once.
void cpu_m6800::m6800_Cause_Interrupt(int type)
{
    if (type & M6800_INT_NMI) m_nmi_line = true;
    if (type & M6800_INT_IRQ) m_irq_line = true;
}

void cpu_m6800::m6800_Clear_Pending_Interrupts()
{
    m_nmi_line = m_irq_line = false;
}

int cpu_m6800::get_ticks(int reset)
{
    int tmp = clocktickstotal;
    if (reset) clocktickstotal = 0;
    return tmp;
}

void cpu_m6800::reset()
{
    m_CC = CC_UNUSED | CC_I;   // IRQ masked at reset
    m_wai = false;
    m_nmi_line = m_irq_line = false;
    m_PC = read16(0xFFFE);     // RESET vector
    m_PPC = m_PC;
}

// =============================================================================
// Memory access (handler-walk pattern, mirrors cpu_m6809)
// =============================================================================

uint8_t cpu_m6800::read8(uint16_t addr)
{
    uint8_t temp = 0;
    MemoryReadByte* MemRead = memory_read;

    while (MemRead && MemRead->lowAddr != 0xffffffff)
    {
        if ((addr >= MemRead->lowAddr) && (addr <= MemRead->highAddr))
        {
            if (MemRead->memoryCall)
                temp = MemRead->memoryCall(addr - MemRead->lowAddr, MemRead);
            else
                temp = *((uint8_t*)MemRead->pUserArea + (addr - MemRead->lowAddr));
            MemRead = nullptr;
            break;
        }
        ++MemRead;
    }
    if (MemRead && !mmem)
        temp = MEM[addr];
    else if (MemRead && mmem)
        if (log_debug_rw) LOG_INFO("CPU%d: Unhandled Read at %04X", cpu_num, (unsigned)addr);

    return temp;
}

void cpu_m6800::write8(uint16_t addr, uint8_t byte)
{
    MemoryWriteByte* MemWrite = memory_write;

    while (MemWrite && MemWrite->lowAddr != 0xffffffff)
    {
        if ((addr >= MemWrite->lowAddr) && (addr <= MemWrite->highAddr))
        {
            if (MemWrite->memoryCall)
                MemWrite->memoryCall(addr - MemWrite->lowAddr, byte, MemWrite);
            else
                *((uint8_t*)MemWrite->pUserArea + (addr - MemWrite->lowAddr)) = byte;
            MemWrite = nullptr;
            break;
        }
        ++MemWrite;
    }
    if (MemWrite && !mmem)
        MEM[addr] = byte;
    else if (MemWrite && mmem)
        if (log_debug_rw) LOG_INFO("CPU%d: Unhandled Write at %04X data: %02X", cpu_num, (unsigned)addr, (unsigned)byte);
}

uint16_t cpu_m6800::read16(uint16_t addr)
{
    uint16_t hi = read8(addr);
    uint16_t lo = read8((uint16_t)(addr + 1));
    return (uint16_t)((hi << 8) | lo);
}

void cpu_m6800::write16(uint16_t addr, uint16_t v)
{
    write8(addr, (uint8_t)(v >> 8));
    write8((uint16_t)(addr + 1), (uint8_t)v);
}

// =============================================================================
// 8-bit operation helpers
// =============================================================================

// ADD / ADC / ABA. Half carry comes out of bit 3, which DAA then consumes.
uint8_t cpu_m6800::add8(uint8_t a, uint8_t v, uint8_t carry_in)
{
    uint16_t r = (uint16_t)a + (uint16_t)v + carry_in;
    uint8_t res = (uint8_t)r;
    set_flag(CC_H, ((a ^ v ^ res) & 0x10) != 0);
    set_flag(CC_C, (r & 0x100) != 0);
    set_flag(CC_V, ((a ^ res) & (v ^ res) & 0x80) != 0);
    set_NZ8(res);
    return res;
}

// SUB / SBC / CMP / SBA / CBA. Carry is the BORROW; H is left alone (the 6800
// only computes half carry for additions).
uint8_t cpu_m6800::sub8(uint8_t a, uint8_t v, uint8_t carry_in)
{
    uint16_t r = (uint16_t)a - (uint16_t)v - carry_in;
    uint8_t res = (uint8_t)r;
    set_flag(CC_C, (r & 0x100) != 0);
    set_flag(CC_V, ((a ^ v) & (a ^ res) & 0x80) != 0);
    set_NZ8(res);
    return res;
}

void cpu_m6800::op_and8(uint8_t& dst, uint8_t v)
{
    dst &= v;
    set_NZ8(dst);
    set_flag(CC_V, false);
}

void cpu_m6800::op_or8(uint8_t& dst, uint8_t v)
{
    dst |= v;
    set_NZ8(dst);
    set_flag(CC_V, false);
}

void cpu_m6800::op_eor8(uint8_t& dst, uint8_t v)
{
    dst ^= v;
    set_NZ8(dst);
    set_flag(CC_V, false);
}

void cpu_m6800::op_bit8(uint8_t a, uint8_t v)
{
    set_NZ8((uint8_t)(a & v));
    set_flag(CC_V, false);
}

void cpu_m6800::op_ld8(uint8_t& dst, uint8_t v)
{
    dst = v;
    set_NZ8(dst);
    set_flag(CC_V, false);
}

void cpu_m6800::op_st8(uint16_t ea, uint8_t v)
{
    write8(ea, v);
    set_NZ8(v);
    set_flag(CC_V, false);
}

uint8_t cpu_m6800::op_neg8(uint8_t v)
{
    uint8_t r = (uint8_t)(0 - v);
    set_NZ8(r);
    set_flag(CC_V, r == 0x80);
    set_flag(CC_C, r != 0);
    return r;
}

uint8_t cpu_m6800::op_com8(uint8_t v)
{
    uint8_t r = (uint8_t)~v;
    set_NZ8(r);
    set_flag(CC_V, false);
    set_flag(CC_C, true);
    return r;
}

uint8_t cpu_m6800::op_clr8()
{
    set_flag(CC_N, false);
    set_flag(CC_Z, true);
    set_flag(CC_V, false);
    set_flag(CC_C, false);
    return 0;
}

uint8_t cpu_m6800::op_inc8(uint8_t v)
{
    uint8_t r = (uint8_t)(v + 1);
    set_NZ8(r);
    set_flag(CC_V, v == 0x7F);   // carry is NOT affected by INC
    return r;
}

uint8_t cpu_m6800::op_dec8(uint8_t v)
{
    uint8_t r = (uint8_t)(v - 1);
    set_NZ8(r);
    set_flag(CC_V, v == 0x80);   // carry is NOT affected by DEC
    return r;
}

void cpu_m6800::op_tst8(uint8_t v)
{
    set_NZ8(v);
    set_flag(CC_V, false);
    set_flag(CC_C, false);
}

// The shift/rotate group all set V = N ^ C, using the carry they just produced.
uint8_t cpu_m6800::op_lsr8(uint8_t v)
{
    bool c = (v & 0x01) != 0;
    uint8_t r = (uint8_t)(v >> 1);
    set_flag(CC_N, false);
    set_flag(CC_Z, r == 0);
    set_flag(CC_C, c);
    set_flag(CC_V, c);           // N is always 0 here, so V == C
    return r;
}

uint8_t cpu_m6800::op_asr8(uint8_t v)
{
    bool c = (v & 0x01) != 0;
    uint8_t r = (uint8_t)((v >> 1) | (v & 0x80));
    set_NZ8(r);
    set_flag(CC_C, c);
    set_flag(CC_V, get_flag(CC_N) != c);
    return r;
}

uint8_t cpu_m6800::op_asl8(uint8_t v)
{
    bool c = (v & 0x80) != 0;
    uint8_t r = (uint8_t)(v << 1);
    set_NZ8(r);
    set_flag(CC_C, c);
    set_flag(CC_V, get_flag(CC_N) != c);
    return r;
}

uint8_t cpu_m6800::op_rol8(uint8_t v)
{
    bool c = (v & 0x80) != 0;
    uint8_t r = (uint8_t)((v << 1) | (get_flag(CC_C) ? 1 : 0));
    set_NZ8(r);
    set_flag(CC_C, c);
    set_flag(CC_V, get_flag(CC_N) != c);
    return r;
}

uint8_t cpu_m6800::op_ror8(uint8_t v)
{
    bool c = (v & 0x01) != 0;
    uint8_t r = (uint8_t)((v >> 1) | (get_flag(CC_C) ? 0x80 : 0x00));
    set_NZ8(r);
    set_flag(CC_C, c);
    set_flag(CC_V, get_flag(CC_N) != c);
    return r;
}

// Decimal adjust after an addition. The correction depends on the half carry
// and carry left by that addition; carry is STICKY here -- DAA can set it but
// never clears one the addition already produced.
void cpu_m6800::op_daa()
{
    uint8_t msn = m_A & 0xF0;
    uint8_t lsn = m_A & 0x0F;
    uint16_t cf = 0;

    if (lsn > 0x09 || get_flag(CC_H)) cf |= 0x06;
    if (msn > 0x80 && lsn > 0x09)     cf |= 0x60;
    if (msn > 0x90 || get_flag(CC_C)) cf |= 0x60;

    uint16_t t = (uint16_t)(cf + m_A);
    m_A = (uint8_t)t;
    set_NZ8(m_A);
    set_flag(CC_V, false);
    if (t & 0x100) set_flag(CC_C, true);
}

// =============================================================================
// 16-bit operation helpers
// =============================================================================

void cpu_m6800::op_ld16(uint16_t& dst, uint16_t v)
{
    dst = v;
    set_NZ16(dst);
    set_flag(CC_V, false);
}

void cpu_m6800::op_st16(uint16_t ea, uint16_t v)
{
    write16(ea, v);
    set_NZ16(v);
    set_flag(CC_V, false);
}

// CPX affects N, Z and V but NOT carry. That makes BHI/BLS/BCC/BCS unusable
// after a CPX on the 6800 -- a documented hardware quirk the 6801 later fixed.
// MAME runs the 6801/6803 version (carry moves) for the whole family, so a
// driver ported from there may assume the other behaviour.
void cpu_m6800::op_cpx(uint16_t v)
{
    uint32_t r = (uint32_t)m_X - (uint32_t)v;
    uint16_t res = (uint16_t)r;
    set_NZ16(res);
    set_flag(CC_V, ((m_X ^ v) & (m_X ^ res) & 0x8000) != 0);
}

// =============================================================================
// Branch / interrupt helpers
// =============================================================================

bool cpu_m6800::test_branch_cond(uint8_t code)
{
    const bool N = get_flag(CC_N);
    const bool Z = get_flag(CC_Z);
    const bool V = get_flag(CC_V);
    const bool C = get_flag(CC_C);

    switch (code) {
    case 0x0: return true;                 // BRA
    case 0x1: return false;                // $21: unused on the 6800, BRN on the 6801
    case 0x2: return !(C || Z);            // BHI
    case 0x3: return C || Z;               // BLS
    case 0x4: return !C;                   // BCC
    case 0x5: return C;                    // BCS
    case 0x6: return !Z;                   // BNE
    case 0x7: return Z;                    // BEQ
    case 0x8: return !V;                   // BVC
    case 0x9: return V;                    // BVS
    case 0xA: return !N;                   // BPL
    case 0xB: return N;                    // BMI
    case 0xC: return N == V;               // BGE
    case 0xD: return N != V;               // BLT
    case 0xE: return !Z && (N == V);       // BGT
    case 0xF: return Z || (N != V);        // BLE
    }
    return false;
}

void cpu_m6800::branch(bool taken)
{
    int8_t off = (int8_t)fetch8();
    if (taken) m_PC = (uint16_t)(m_PC + off);
}

// Stacks PCL,PCH,XL,XH,A,B,CC (high address down to low), masks IRQ and takes
// the vector. When the interrupt terminates a WAI the frame was already stacked
// -- and paid for -- by the WAI instruction, so only the short wake-up cost is
// charged here instead of re-counting the (skipped) stacking.
int cpu_m6800::service_interrupt(uint16_t vector)
{
    int cycles;

    if (m_wai) {
        m_wai = false;
        cycles = 4;
    } else {
        push16(m_PC);
        push16(m_X);
        push8(m_A);
        push8(m_B);
        push8(m_CC);
        cycles = 12;
    }
    set_flag(CC_I, true);
    m_PC = read16(vector);
    return cycles;
}

// =============================================================================
// Main execution loop
// =============================================================================

// -----------------------------------------------------------------------------
// charge_cycles : add `c` cycles to the running total AND drive the AAE timer
// subsystem for exactly that many cycles, then return `c`. Updating the timer
// once per step (instruction or interrupt) -- instead of once per exec() batch
// -- keeps interrupt timing cycle-accurate, matching cpu_m6809 / cpu_6502.
// -----------------------------------------------------------------------------
int cpu_m6800::charge_cycles(int c)
{
    clocktickstotal += c;
    timer_update(c, cpu_num);
    if (clocktickstotal > 0xfffffff) clocktickstotal = 0;
    return c;
}

// -----------------------------------------------------------------------------
// step : run exactly ONE 6800 step and return the cycles it consumed:
//   - service the highest-priority pending/unmasked interrupt, OR
//   - fetch & execute one instruction, OR
//   - idle one cycle while WAI-waiting for an interrupt.
// charge_cycles() advances the timer for that step; exec() just loops it.
// -----------------------------------------------------------------------------
int cpu_m6800::step()
{
    // ---- WAI wait-state: idle until an interrupt will actually be taken ----
    // A masked IRQ does not end the wait; NMI always does.
    if (m_wai) {
        bool serviceable = m_nmi_line || (m_irq_line && !get_flag(CC_I));
        if (!serviceable)
            return charge_cycles(1);    // still waiting: burn one idle cycle
        // m_wai remains true so service_interrupt() doesn't restack.
    }

    // ---- Pending interrupts (priority: NMI, then IRQ) ----------------------
    if (m_nmi_line) {
        int c = service_interrupt(0xFFFC);
        m_nmi_line = false;             // latch lowered when taken
        return charge_cycles(c);
    }
    if (m_irq_line && !get_flag(CC_I)) {
        int c = service_interrupt(0xFFF8);
        m_irq_line = false;             // latch lowered when taken
        return charge_cycles(c);
    }

    // The opcode switch below subtracts this instruction's cost from `cycles`,
    // which starts at 0 and ends negative; consumed = -cycles (charged at the
    // end of this function).
    int cycles = 0;

    m_PPC = m_PC;
    uint8_t op = fetch8();
    m_last_opcode = op;

    switch (op)
    {
    // ----- Inherent / control $00-$1F -----
    case 0x01: cycles -= 2; break;                                          // NOP
    case 0x06: m_CC = (uint8_t)(m_A | CC_UNUSED); cycles -= 2; break;       // TAP
    case 0x07: m_A = (uint8_t)(m_CC | CC_UNUSED); cycles -= 2; break;       // TPA
    case 0x08: m_X++; set_flag(CC_Z, m_X == 0); cycles -= 4; break;         // INX
    case 0x09: m_X--; set_flag(CC_Z, m_X == 0); cycles -= 4; break;         // DEX
    case 0x0A: set_flag(CC_V, false); cycles -= 2; break;                   // CLV
    case 0x0B: set_flag(CC_V, true);  cycles -= 2; break;                   // SEV
    case 0x0C: set_flag(CC_C, false); cycles -= 2; break;                   // CLC
    case 0x0D: set_flag(CC_C, true);  cycles -= 2; break;                   // SEC
    case 0x0E: set_flag(CC_I, false); cycles -= 2; break;                   // CLI
    case 0x0F: set_flag(CC_I, true);  cycles -= 2; break;                   // SEI
    case 0x10: m_A = sub8(m_A, m_B, 0); cycles -= 2; break;                 // SBA
    case 0x11: sub8(m_A, m_B, 0); cycles -= 2; break;                       // CBA
    case 0x16: m_B = m_A; set_NZ8(m_B); set_flag(CC_V, false); cycles -= 2; break; // TAB
    case 0x17: m_A = m_B; set_NZ8(m_A); set_flag(CC_V, false); cycles -= 2; break; // TBA
    case 0x19: op_daa(); cycles -= 2; break;                                // DAA
    case 0x1B: m_A = add8(m_A, m_B, 0); cycles -= 2; break;                 // ABA

    // ----- Relative branches $20-$2F -----
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        branch(test_branch_cond(op & 0x0F));
        cycles -= 4; break;

    // ----- Stack / index / return $30-$3F -----
    case 0x30: m_X = (uint16_t)(m_SP + 1); cycles -= 4; break;              // TSX
    case 0x31: m_SP++; cycles -= 4; break;                                  // INS
    case 0x32: m_A = pull8(); cycles -= 4; break;                           // PULA
    case 0x33: m_B = pull8(); cycles -= 4; break;                           // PULB
    case 0x34: m_SP--; cycles -= 4; break;                                  // DES
    case 0x35: m_SP = (uint16_t)(m_X - 1); cycles -= 4; break;              // TXS
    case 0x36: push8(m_A); cycles -= 4; break;                              // PSHA
    case 0x37: push8(m_B); cycles -= 4; break;                              // PSHB
    case 0x39: m_PC = pull16(); cycles -= 5; break;                         // RTS

    case 0x3B:  // RTI
        m_CC = (uint8_t)(pull8() | CC_UNUSED);
        m_B  = pull8();
        m_A  = pull8();
        m_X  = pull16();
        m_PC = pull16();
        cycles -= 10; break;

    case 0x3E:  // WAI - stack the frame now, then idle until an interrupt is taken
        push16(m_PC);
        push16(m_X);
        push8(m_A);
        push8(m_B);
        push8(m_CC);
        m_wai = true;
        cycles -= 9; break;

    case 0x3F:  // SWI
        push16(m_PC);
        push16(m_X);
        push8(m_A);
        push8(m_B);
        push8(m_CC);
        set_flag(CC_I, true);
        m_PC = read16(0xFFFA);
        cycles -= 12; break;

    // ----- Accumulator read-modify-write $40-$5F (A = $4x, B = $5x) -----
    case 0x40: case 0x43: case 0x44: case 0x46: case 0x47: case 0x48:
    case 0x49: case 0x4A: case 0x4C: case 0x4D: case 0x4F:
    case 0x50: case 0x53: case 0x54: case 0x56: case 0x57: case 0x58:
    case 0x59: case 0x5A: case 0x5C: case 0x5D: case 0x5F:
    {
        uint8_t& acc = (op & 0x10) ? m_B : m_A;
        switch (op & 0x0F) {
        case 0x0: acc = op_neg8(acc); break;
        case 0x3: acc = op_com8(acc); break;
        case 0x4: acc = op_lsr8(acc); break;
        case 0x6: acc = op_ror8(acc); break;
        case 0x7: acc = op_asr8(acc); break;
        case 0x8: acc = op_asl8(acc); break;
        case 0x9: acc = op_rol8(acc); break;
        case 0xA: acc = op_dec8(acc); break;
        case 0xC: acc = op_inc8(acc); break;
        case 0xD: op_tst8(acc);       break;
        case 0xF: acc = op_clr8();    break;
        }
        cycles -= 2;
    } break;

    // ----- Memory read-modify-write + JMP $60-$7F (indexed = $6x, extended = $7x) -----
    case 0x60: case 0x63: case 0x64: case 0x66: case 0x67: case 0x68:
    case 0x69: case 0x6A: case 0x6C: case 0x6D: case 0x6E: case 0x6F:
    case 0x70: case 0x73: case 0x74: case 0x76: case 0x77: case 0x78:
    case 0x79: case 0x7A: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
    {
        const bool indexed = (op & 0x10) == 0;
        const uint8_t fn = op & 0x0F;
        const uint16_t ea = indexed ? ea_indexed() : ea_extended();

        if (fn == 0x0E) {                       // JMP
            m_PC = ea;
            cycles -= indexed ? 4 : 3;
            break;
        }

        const int base = indexed ? 7 : 6;
        if (fn == 0x0D) {                       // TST - read only
            op_tst8(read8(ea));
            cycles -= base;
            break;
        }
        if (fn == 0x0F) {                       // CLR - write only
            write8(ea, op_clr8());
            cycles -= base;
            break;
        }

        uint8_t v = read8(ea), r = v;
        switch (fn) {
        case 0x0: r = op_neg8(v); break;
        case 0x3: r = op_com8(v); break;
        case 0x4: r = op_lsr8(v); break;
        case 0x6: r = op_ror8(v); break;
        case 0x7: r = op_asr8(v); break;
        case 0x8: r = op_asl8(v); break;
        case 0x9: r = op_rol8(v); break;
        case 0xA: r = op_dec8(v); break;
        case 0xC: r = op_inc8(v); break;
        }
        write8(ea, r);
        cycles -= base;
    } break;

    default:
        if (op >= 0x80)
            exec_accumulator(op, cycles);
        else {
            LOG_INFO("CPU%d: Unrecognized opcode @%04X: %02X", cpu_num, (unsigned)m_PPC, (unsigned)op);
            cycles -= 2;
        }
        break;
    }

    // One instruction done: charge its cost (consumed = -cycles, since `cycles`
    // started at 0 and the opcode switch subtracted from it) and drive the timer.
    return charge_cycles(-cycles);
}

// -----------------------------------------------------------------------------
// exec_accumulator : the $80-$FF grid.
//   bit 6      selects the accumulator      (0 = A, 1 = B)
//   bits 5..4  select the addressing mode   (0 = immediate, 1 = direct,
//                                            2 = indexed,   3 = extended)
//   bits 3..0  select the operation
// The low nibbles $C-$F break the pure grid: they mean CPX / BSR-JSR / LDS /
// STS in the A half and (mostly) unimplemented-on-the-6800 opcodes plus
// LDX / STX in the B half, so they are handled ahead of the 8-bit group.
// -----------------------------------------------------------------------------
void cpu_m6800::exec_accumulator(uint8_t op, int& cycles)
{
    static const int c8[4]    = { 2, 3, 5, 4 };  // 8-bit read  (imm/dir/idx/ext)
    static const int c8st[4]  = { 0, 4, 6, 5 };  // 8-bit store (no immediate form)
    static const int c16[4]   = { 3, 4, 6, 5 };  // 16-bit read
    static const int c16st[4] = { 0, 5, 7, 6 };  // 16-bit store (no immediate form)

    const int  mode = (op >> 4) & 0x03;
    const int  fn   = op & 0x0F;
    const bool useB = (op & 0x40) != 0;

    // Opcodes the 6800 does not implement. Most became 6801 instructions
    // (SUBD/ADDD at $x3, JSR-direct at $9D, the LDD/STD column at $xC/$xD in
    // the B half); $87/$C7 and $8F/$CF are the meaningless "store to an
    // immediate operand" forms; $9D and $DD are the 6800's HCF test opcodes,
    // which lock up real silicon and are simply reported here.
    const bool illegal =
        (fn == 0x3) ||
        (mode == 0 && (fn == 0x7 || fn == 0xF)) ||
        (useB && (fn == 0xC || fn == 0xD)) ||
        (op == 0x9D);

    if (illegal) {
        LOG_INFO("CPU%d: Unrecognized opcode @%04X: %02X", cpu_num, (unsigned)m_PPC, (unsigned)op);
        cycles -= 2;
        return;
    }

    // Effective address for every non-immediate mode. Computed here because it
    // consumes the operand byte(s); immediate forms read their operand straight
    // out of the instruction stream instead.
    uint16_t ea = 0;
    if (mode != 0) {
        switch (mode) {
        case 1:  ea = ea_direct();   break;
        case 2:  ea = ea_indexed();  break;
        default: ea = ea_extended(); break;
        }
    }

    // ---- 16-bit and control operations ($C-$F) ----
    switch (fn) {
    case 0xC:   // CPX (A half only; the B half was filtered out as illegal)
        op_cpx(mode == 0 ? fetch16() : read16(ea));
        cycles -= c16[mode];
        return;

    case 0xD:   // $8D BSR, $AD/$BD JSR (the B half was filtered out as illegal)
        if (mode == 0) {                        // BSR: relative, like a branch
            int8_t off = (int8_t)fetch8();
            push16(m_PC);
            m_PC = (uint16_t)(m_PC + off);
            cycles -= 8;
        } else {                                // JSR indexed / extended
            push16(m_PC);
            m_PC = ea;
            cycles -= (mode == 2) ? 8 : 9;
        }
        return;

    case 0xE:   // LDS (A half) / LDX (B half)
        op_ld16(useB ? m_X : m_SP, mode == 0 ? fetch16() : read16(ea));
        cycles -= c16[mode];
        return;

    case 0xF:   // STS (A half) / STX (B half)
        op_st16(ea, useB ? m_X : m_SP);
        cycles -= c16st[mode];
        return;
    }

    // ---- 8-bit operations ($0-$B) ----
    uint8_t& acc = useB ? m_B : m_A;

    if (fn == 0x7) {                            // STA
        op_st8(ea, acc);
        cycles -= c8st[mode];
        return;
    }

    const uint8_t v = (mode == 0) ? fetch8() : read8(ea);

    switch (fn) {
    case 0x0: acc = sub8(acc, v, 0); break;                             // SUB
    case 0x1: sub8(acc, v, 0); break;                                   // CMP
    case 0x2: acc = sub8(acc, v, get_flag(CC_C) ? 1 : 0); break;        // SBC
    case 0x4: op_and8(acc, v); break;                                   // AND
    case 0x5: op_bit8(acc, v); break;                                   // BIT
    case 0x6: op_ld8(acc, v); break;                                    // LDA
    case 0x8: op_eor8(acc, v); break;                                   // EOR
    case 0x9: acc = add8(acc, v, get_flag(CC_C) ? 1 : 0); break;        // ADC
    case 0xA: op_or8(acc, v); break;                                    // ORA
    case 0xB: acc = add8(acc, v, 0); break;                             // ADD
    }
    cycles -= c8[mode];
}

// -----------------------------------------------------------------------------
// exec : run a batch of at least `cycles` cycles by repeatedly stepping, exactly
// like cpu_m6809::exec. Returns the total cycles actually run (the final step may
// overshoot). The scheduler reads the precise consumed count via get6800ticks();
// the timer is already advanced per-step inside step().
// -----------------------------------------------------------------------------
int cpu_m6800::exec(int cycles)
{
    int instr_budget = cycles;
    int total = 0;

    // WAI wait-state: idle in SMALL CHUNKS, not the whole slice at once.
    // charge_cycles() drives timer_update(), and a timer callback can assert an
    // interrupt line MID-slice. Consuming the entire slice in one charge would
    // delay the wake-up to the next slice -- up to a slice of latency/jitter
    // that real hardware (which leaves WAI within a couple of cycles of
    // assertion) does not have. Re-check the wake condition between chunks.
    while (instr_budget > 0 && m_wai) {
        if (m_nmi_line || (m_irq_line && !get_flag(CC_I)))
            break;              // step() below handles the actual wake/service
        int chunk = (instr_budget < 8) ? instr_budget : 8;
        total += charge_cycles(chunk);
        instr_budget -= chunk;
    }

    // Run the remaining budget of INSTRUCTION cycles.
    while (instr_budget > 0) {
        int c = step();
        total += c;
        instr_budget -= c;
    }
    return total;
}
