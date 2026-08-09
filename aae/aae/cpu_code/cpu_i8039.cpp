
// -----------------------------------------------------------------------------
// Intel 8039 / MCS-48 Emulator for AAE (Another Arcade Emulator)
//
// Clean-room implementation from the published Intel MCS-48 instruction set.
// See cpu_i8039.h for the architecture / memory-model overview. Structured
// after the AAE cpu_i8085 template so it integrates with cpu_control the same
// way (constructor signature, exec/reset/get_ticks/interrupt/In/Out, reg_PC).
//
// Cycle accounting is in MCS-48 *machine cycles* (1 or 2 per instruction).
// One machine cycle is 15 oscillator periods on real silicon; the driver's
// cpu_freq should therefore be expressed in machine cycles/second so the
// scheduler scales correctly.
// -----------------------------------------------------------------------------

#include "cpu_i8039.h"
#include "sys_log.h"
#include <cstdlib>



// ============================================================================
// Construction / Reset / Tick Counting
// ============================================================================

cpu_i8039::cpu_i8039(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem,
                     z80PortRead* port_read, z80PortWrite* port_write, uint16_t addr)
{
	MEM = mem;
	memory_read  = read_mem;
	memory_write = write_mem;
	z80IoRead    = port_read;
	z80IoWrite   = port_write;
	(void)addr;            // unused - kept for cpu_control signature parity
	reset();
}

void cpu_i8039::reset()
{
	reg_PC = 0x0000;
	A      = 0x00;
	PSW    = PSW_X3;        // SP=0, bank0, flags clear, bit3=1
	reg_T  = 0x00;
	F1     = false;
	DBF    = false;

	for (int i = 0; i < 128; i++) intRAM[i] = 0;
	for (int i = 0; i < 8;   i++) port_latch[i] = 0xFF; // ports float high

	irq_ext_enabled   = false;
	irq_timer_enabled = false;
	irq_in_progress   = false;
	irq_ext_pending   = false;
	irq_timer_pending = false;

	int_line = 0;
	t0_line  = 0;
	t1_line  = 0;

	timer_running   = mame_compat;  // MAME starts the timer at reset (Mario Bros)
	counter_running = false;
	timer_flag      = false;
	timer_prescaler = 0;
	old_t1_sample   = 0;

	clocktickstotal = 0;
}

int cpu_i8039::get_ticks(int reset)
{
	int tmp = clocktickstotal;
	if (reset) {
		clocktickstotal = 0;
	}
	return tmp;
}


// ============================================================================
// Program memory / external data memory / IO
// ============================================================================

uint8_t cpu_i8039::prog_read(uint16_t addr)
{
	addr &= PROG_MASK;
	MemoryReadByte* MemRead = memory_read;
	while (MemRead && MemRead->lowAddr != 0xffffffff)
	{
		if ((addr >= MemRead->lowAddr) && (addr <= MemRead->highAddr))
		{
			if (MemRead->memoryCall)
				return MemRead->memoryCall(addr - MemRead->lowAddr, MemRead);
			if (MemRead->pUserArea)
				return *((uint8_t*)MemRead->pUserArea + (addr - MemRead->lowAddr));
		}
		++MemRead;
	}
	return MEM ? MEM[addr] : 0xFF;
}

uint8_t cpu_i8039::fetch()
{
	uint8_t op = prog_read(reg_PC);
	reg_PC = (reg_PC + 1) & PROG_MASK;
	return op;
}

uint8_t cpu_i8039::ext_read(uint8_t addr)
{
	if (ext_read_cb) return ext_read_cb(addr);

	// Fall back to the write-handler-less data path: try the MemoryRead list,
	// otherwise the private extRAM buffer.
	MemoryReadByte* MemRead = memory_read;
	while (MemRead && MemRead->lowAddr != 0xffffffff)
	{
		if ((addr >= MemRead->lowAddr) && (addr <= MemRead->highAddr) && MemRead->memoryCall)
			return MemRead->memoryCall(addr - MemRead->lowAddr, MemRead);
		++MemRead;
	}
	return extRAM[addr];
}

void cpu_i8039::ext_write(uint8_t addr, uint8_t val)
{
	if (ext_write_cb) { ext_write_cb(addr, val); return; }

	MemoryWriteByte* MemWrite = memory_write;
	while (MemWrite && MemWrite->lowAddr != 0xffffffff)
	{
		if ((addr >= MemWrite->lowAddr) && (addr <= MemWrite->highAddr) && MemWrite->memoryCall)
		{
			MemWrite->memoryCall(addr - MemWrite->lowAddr, val, MemWrite);
			return;
		}
		++MemWrite;
	}
	extRAM[addr] = val;
}

uint8_t cpu_i8039::io_in(uint16_t port)
{
	uint8_t bVal = 0xFF;
	z80PortRead* mr = z80IoRead;
	while (mr && mr->lowIoAddr != 0xffff)
	{
		if (port >= mr->lowIoAddr && port <= mr->highIoAddr) {
			bVal = (uint8_t)mr->IOCall(port, mr);
			break;
		}
		mr++;
	}
	return bVal;
}

void cpu_i8039::io_out(uint16_t port, uint8_t val)
{
	z80PortWrite* mr = z80IoWrite;
	while (mr && mr->lowIoAddr != 0xffff)
	{
		if (port >= mr->lowIoAddr && port <= mr->highIoAddr) {
			mr->IOCall(port, val, mr);
			return;
		}
		mr++;
	}
}

// Public 8-bit interface kept for cpu_i8080 / cpu_i8085 parity.
uint8_t cpu_i8039::In(uint8_t bPort)            { return io_in(bPort); }
void    cpu_i8039::Out(uint8_t bPort, uint8_t v){ io_out(bPort, v); }

// T0/T1 level used by JT0/JT1/JNT0/JNT1. In MAME mode these come from the
// port map (0x110/0x111); otherwise from the set_T0()/set_T1() pins.
int cpu_i8039::read_test(int t)
{
	if (mame_compat)
		return io_in(t ? MPORT_T1 : MPORT_T0) ? 1 : 0;
	return t ? t1_line : t0_line;
}


// ============================================================================
// Internal register file (R0..R7 live in RAM; bank chosen by PSW_BS)
// ============================================================================

int cpu_i8039::reg_base()
{
	return (PSW & PSW_BS) ? 24 : 0;
}

uint8_t cpu_i8039::get_R(int n)
{
	return intRAM[(reg_base() + n) & 0x7F];
}

void cpu_i8039::set_R(int n, uint8_t v)
{
	intRAM[(reg_base() + n) & 0x7F] = v;
}

uint8_t cpu_i8039::ram_get(uint8_t addr)
{
	return intRAM[addr & ram_mask];
}

void cpu_i8039::ram_set(uint8_t addr, uint8_t v)
{
	intRAM[addr & ram_mask] = v;
}


// ============================================================================
// Stack: 8 levels in RAM 8..23, SP in PSW bits 2..0
//
// On CALL / interrupt:  RAM[8+2*SP]   = PC low 8 bits
//                       RAM[8+2*SP+1] = (PSW & 0xF0) | (PC bits 8..11)
//                       SP = (SP + 1) & 7
// On RET / RETR:        SP = (SP - 1) & 7
//                       PC = (hi & 0x0F)<<8 | lo
//                       RETR additionally restores PSW high nibble + re-enables
// ============================================================================

void cpu_i8039::do_call(uint16_t addr)
{
	uint8_t sp = PSW & PSW_SP;
	uint8_t saddr = 8 + sp * 2;
	intRAM[saddr]     = reg_PC & 0xFF;
	intRAM[saddr + 1] = (uint8_t)(((reg_PC >> 8) & 0x0F) | (PSW & 0xF0));
	sp = (sp + 1) & 0x07;
	PSW = (PSW & 0xF8) | sp | PSW_X3;
	reg_PC = addr & PROG_MASK;
}

void cpu_i8039::do_ret(bool restore_psw)
{
	uint8_t sp = (PSW - 1) & 0x07;
	PSW = (PSW & 0xF8) | sp | PSW_X3;
	uint8_t saddr = 8 + sp * 2;
	uint8_t lo = intRAM[saddr];
	uint8_t hi = intRAM[saddr + 1];
	reg_PC = (((uint16_t)(hi & 0x0F)) << 8) | lo;
	if (restore_psw) {
		PSW = (PSW & 0x0F) | (hi & 0xF0) | PSW_X3;
		irq_in_progress = false;   // RETR re-opens the interrupt window
	}
}


// ============================================================================
// ALU helpers
// ============================================================================

void cpu_i8039::op_add(uint8_t value, bool with_carry)
{
	int c = (with_carry && carry()) ? 1 : 0;
	int sum = A + value + c;
	int half = (A & 0x0F) + (value & 0x0F) + c;

	if (sum & 0x100) PSW |= PSW_C; else PSW &= ~PSW_C;
	if (half & 0x10) PSW |= PSW_AC; else PSW &= ~PSW_AC;

	A = (uint8_t)sum;
}

void cpu_i8039::daa()
{
	int a = A;
	if ((a & 0x0F) > 9 || (PSW & PSW_AC))
		a += 0x06;
	if (((a & 0x1F0) > 0x90) || (PSW & PSW_C)) {
		a += 0x60;
		PSW |= PSW_C;
	}
	A = (uint8_t)a;
}


// ============================================================================
// Timer / Counter
//
// In timer mode the prescaler divides the machine-cycle clock by 32; each
// rollover bumps reg_T. Overflow (0xFF -> 0x00) sets TF (tested by JTF) and a
// pending timer interrupt.
//
// Counter mode: in MAME-compat mode T1 is sampled from the port once per
// machine cycle and rising edges bump reg_T (matching the Buffoni/Boris core).
// In native mode the counter is driven by set_T1() falling edges instead.
// ============================================================================

void cpu_i8039::bump_counter()
{
	reg_T++;
	if (reg_T == 0x00) {
		timer_flag = true;
		if (irq_timer_enabled) irq_timer_pending = true;
	}
}

void cpu_i8039::step_timer(int machine_cycles)
{
	if (timer_running)
	{
		timer_prescaler += machine_cycles;
		while (timer_prescaler >= 32)
		{
			timer_prescaler -= 32;
			bump_counter();
		}
	}
	else if (counter_running && mame_compat)
	{
		// Sample the T1 port each machine cycle, count low->high transitions.
		for (int i = 0; i < machine_cycles; i++)
		{
			uint8_t t1 = io_in(MPORT_T1) & 1;
			if (t1 && !old_t1_sample) bump_counter();
			old_t1_sample = t1;
		}
	}
}


// ============================================================================
// Interrupt servicing - checked at instruction boundaries.
// External interrupt has priority over the timer interrupt. Either one pushes
// PC+PSW like a CALL, vectors to its fixed address, and blocks further
// interrupts until RETR.
// ============================================================================

void cpu_i8039::service_interrupts()
{
	if (irq_in_progress) return;

	if (irq_ext_enabled && irq_ext_pending)
	{
		irq_in_progress = true;
		irq_ext_pending = false;      // edge consumed; held externally if level
		do_call(VEC_EXTERNAL);
		clocktickstotal += 2;
		return;
	}

	if (irq_timer_enabled && irq_timer_pending)
	{
		irq_in_progress = true;
		irq_timer_pending = false;
		do_call(VEC_TIMER);
		clocktickstotal += 2;
		return;
	}
}


// ============================================================================
// External signal interface
// ============================================================================

void cpu_i8039::interrupt(uint8_t n)
{
	(void)n;                          // MCS-48 external INT always vectors to 3
	set_int_line(1);
}

void cpu_i8039::cause_interrupt(int type)
{
	switch (type)
	{
	case I8039_EXTERNAL: set_int_line(1); break;
	case I8039_TIMER:    irq_timer_pending = true; timer_flag = true; break;
	default: break;
	}
}

void cpu_i8039::clear_pending_interrupts()
{
	irq_ext_pending   = false;
	irq_timer_pending = false;
}

void cpu_i8039::set_int_line(int state)
{
	int_line = state ? 1 : 0;
	if (int_line) irq_ext_pending = true;   // active-low pin asserted
}

void cpu_i8039::set_T0(int state)
{
	t0_line = state ? 1 : 0;
}

void cpu_i8039::set_T1(int state)
{
	int newstate = state ? 1 : 0;
	// Native counter mode increments reg_T on each 1 -> 0 transition of T1.
	// In MAME-compat mode the counter is sampled from the port in step_timer()
	// instead, so set_T1() only tracks the pin level here.
	if (!mame_compat && counter_running && t1_line == 1 && newstate == 0)
		bump_counter();
	t1_line = newstate;
}


// ============================================================================
// Main Execution Loop
// ============================================================================

int cpu_i8039::exec(int cycles)
{
	uint8_t opcode, operand, temp8, ptr;
	uint16_t page, addr;
	int bit;

	while (cycles > 0)
	{
		int last_cycles = cycles;

		// ----- Interrupt check at instruction boundary -----
		service_interrupts();

		// ----- Fetch -----
		opcode = fetch();

		switch (opcode)
		{

		// ================================================================
		// NOP
		// ================================================================
		case 0x00: // NOP
			cycles -= 1;
			break;

		// ================================================================
		// Jumps and Calls (page from opcode bits 5..7, A11 from DBF).
		// While in an ISR the A11 select is suppressed (bank 0 forced).
		// ================================================================
		case 0x04: case 0x24: case 0x44: case 0x64:   // JMP
		case 0x84: case 0xA4: case 0xC4: case 0xE4:
			operand = fetch();
			addr = (((uint16_t)(opcode >> 5) & 7) << 8) | operand;
			if (!irq_in_progress && DBF) addr |= 0x800;
			reg_PC = addr & PROG_MASK;
			cycles -= 2;
			break;

		case 0x14: case 0x34: case 0x54: case 0x74:   // CALL
		case 0x94: case 0xB4: case 0xD4: case 0xF4:
			operand = fetch();
			addr = (((uint16_t)(opcode >> 5) & 7) << 8) | operand;
			if (!irq_in_progress && DBF) addr |= 0x800;
			do_call(addr);
			cycles -= 2;
			break;

		// ================================================================
		// RET / RETR
		// ================================================================
		case 0x83: // RET
			do_ret(false);
			cycles -= 2;
			break;
		case 0x93: // RETR
			do_ret(true);
			cycles -= 2;
			break;

		// ================================================================
		// Conditional / bit-test jumps (within current 256-byte page)
		// ================================================================
		case 0x16: // JTF  - jump if timer flag (and clear it)
			page = reg_PC & 0xF00; operand = fetch();
			if (timer_flag) { reg_PC = page | operand; timer_flag = false; }
			cycles -= 2;
			break;
		case 0x26: // JNT0 - jump if T0 == 0
			page = reg_PC & 0xF00; operand = fetch();
			if (!read_test(0)) reg_PC = page | operand;
			cycles -= 2;
			break;
		case 0x36: // JT0  - jump if T0 == 1
			page = reg_PC & 0xF00; operand = fetch();
			if (read_test(0)) reg_PC = page | operand;
			cycles -= 2;
			break;
		case 0x46: // JNT1 - jump if T1 == 0
			page = reg_PC & 0xF00; operand = fetch();
			if (!read_test(1)) reg_PC = page | operand;
			cycles -= 2;
			break;
		case 0x56: // JT1  - jump if T1 == 1
			page = reg_PC & 0xF00; operand = fetch();
			if (read_test(1)) reg_PC = page | operand;
			cycles -= 2;
			break;
		case 0x76: // JF1  - jump if F1 == 1
			page = reg_PC & 0xF00; operand = fetch();
			if (F1) reg_PC = page | operand;
			cycles -= 2;
			break;
		case 0x86: // JNI  - jump if interrupt input active
			page = reg_PC & 0xF00; operand = fetch();
			if (int_line) reg_PC = page | operand;
			cycles -= 2;
			break;
		case 0x96: // JNZ  - jump if A != 0
			page = reg_PC & 0xF00; operand = fetch();
			if (A != 0) reg_PC = page | operand;
			cycles -= 2;
			break;
		case 0xB6: // JF0  - jump if F0 == 1
			page = reg_PC & 0xF00; operand = fetch();
			if (PSW & PSW_F0) reg_PC = page | operand;
			cycles -= 2;
			break;
		case 0xC6: // JZ   - jump if A == 0
			page = reg_PC & 0xF00; operand = fetch();
			if (A == 0) reg_PC = page | operand;
			cycles -= 2;
			break;
		case 0xE6: // JNC  - jump if carry == 0
			page = reg_PC & 0xF00; operand = fetch();
			if (!carry()) reg_PC = page | operand;
			cycles -= 2;
			break;
		case 0xF6: // JC   - jump if carry == 1
			page = reg_PC & 0xF00; operand = fetch();
			if (carry()) reg_PC = page | operand;
			cycles -= 2;
			break;

		case 0x12: case 0x32: case 0x52: case 0x72:   // JBb - jump if A bit b set
		case 0x92: case 0xB2: case 0xD2: case 0xF2:
			bit = (opcode >> 5) & 7;
			page = reg_PC & 0xF00; operand = fetch();
			if (A & (1 << bit)) reg_PC = page | operand;
			cycles -= 2;
			break;

		// ================================================================
		// DJNZ Rr,addr - decrement register, jump if non-zero
		// ================================================================
		case 0xE8: case 0xE9: case 0xEA: case 0xEB:
		case 0xEC: case 0xED: case 0xEE: case 0xEF:
			page = reg_PC & 0xF00; operand = fetch();
			temp8 = get_R(opcode & 7) - 1;
			set_R(opcode & 7, temp8);
			if (temp8 != 0) reg_PC = page | operand;
			cycles -= 2;
			break;

		// ================================================================
		// MOVP / MOVP3 / JMPP - program-memory table reads
		// ================================================================
		case 0xA3: // MOVP A,@A  - A = PROG[(PC & 0xF00) | A]
			A = prog_read((reg_PC & 0xF00) | A);
			cycles -= 2;
			break;
		case 0xE3: // MOVP3 A,@A - A = PROG[0x300 | A]
			A = prog_read(0x300 | A);
			cycles -= 2;
			break;
		case 0xB3: // JMPP @A    - PC = (PC & 0xF00) | PROG[(PC & 0xF00) | A]
			page = reg_PC & 0xF00;
			reg_PC = page | prog_read(page | A);
			cycles -= 2;
			break;

		// ================================================================
		// Accumulator <- immediate / register / @Rr / arithmetic
		// ================================================================
		case 0x23: // MOV A,#data
			A = fetch();
			cycles -= 2;
			break;
		case 0x03: // ADD A,#data
			operand = fetch(); op_add(operand, false); cycles -= 2; break;
		case 0x13: // ADDC A,#data
			operand = fetch(); op_add(operand, true);  cycles -= 2; break;
		case 0x43: // ORL A,#data
			A |= fetch(); cycles -= 2; break;
		case 0x53: // ANL A,#data
			A &= fetch(); cycles -= 2; break;
		case 0xD3: // XRL A,#data
			A ^= fetch(); cycles -= 2; break;

		case 0x60: case 0x61: // ADD A,@Rr
			op_add(ram_get(get_R(opcode & 1)), false); cycles -= 1; break;
		case 0x70: case 0x71: // ADDC A,@Rr
			op_add(ram_get(get_R(opcode & 1)), true);  cycles -= 1; break;
		case 0x40: case 0x41: // ORL A,@Rr
			A |= ram_get(get_R(opcode & 1)); cycles -= 1; break;
		case 0x50: case 0x51: // ANL A,@Rr
			A &= ram_get(get_R(opcode & 1)); cycles -= 1; break;
		case 0xD0: case 0xD1: // XRL A,@Rr
			A ^= ram_get(get_R(opcode & 1)); cycles -= 1; break;

		case 0x68: case 0x69: case 0x6A: case 0x6B: // ADD A,Rr
		case 0x6C: case 0x6D: case 0x6E: case 0x6F:
			op_add(get_R(opcode & 7), false); cycles -= 1; break;
		case 0x78: case 0x79: case 0x7A: case 0x7B: // ADDC A,Rr
		case 0x7C: case 0x7D: case 0x7E: case 0x7F:
			op_add(get_R(opcode & 7), true); cycles -= 1; break;
		case 0x48: case 0x49: case 0x4A: case 0x4B: // ORL A,Rr
		case 0x4C: case 0x4D: case 0x4E: case 0x4F:
			A |= get_R(opcode & 7); cycles -= 1; break;
		case 0x58: case 0x59: case 0x5A: case 0x5B: // ANL A,Rr
		case 0x5C: case 0x5D: case 0x5E: case 0x5F:
			A &= get_R(opcode & 7); cycles -= 1; break;
		case 0xD8: case 0xD9: case 0xDA: case 0xDB: // XRL A,Rr
		case 0xDC: case 0xDD: case 0xDE: case 0xDF:
			A ^= get_R(opcode & 7); cycles -= 1; break;

		case 0xF0: case 0xF1: // MOV A,@Rr
			A = ram_get(get_R(opcode & 1)); cycles -= 1; break;
		case 0xF8: case 0xF9: case 0xFA: case 0xFB: // MOV A,Rr
		case 0xFC: case 0xFD: case 0xFE: case 0xFF:
			A = get_R(opcode & 7); cycles -= 1; break;

		// ================================================================
		// Stores: A -> register / @Rr / RAM-immediate
		// ================================================================
		case 0xA0: case 0xA1: // MOV @Rr,A
			ram_set(get_R(opcode & 1), A); cycles -= 1; break;
		case 0xA8: case 0xA9: case 0xAA: case 0xAB: // MOV Rr,A
		case 0xAC: case 0xAD: case 0xAE: case 0xAF:
			set_R(opcode & 7, A); cycles -= 1; break;
		case 0xB0: case 0xB1: // MOV @Rr,#data
			operand = fetch(); ram_set(get_R(opcode & 1), operand); cycles -= 2; break;
		case 0xB8: case 0xB9: case 0xBA: case 0xBB: // MOV Rr,#data
		case 0xBC: case 0xBD: case 0xBE: case 0xBF:
			operand = fetch(); set_R(opcode & 7, operand); cycles -= 2; break;

		// ================================================================
		// Exchanges
		// ================================================================
		case 0x20: case 0x21: // XCH A,@Rr
			ptr = get_R(opcode & 1);
			temp8 = ram_get(ptr); ram_set(ptr, A); A = temp8;
			cycles -= 1;
			break;
		case 0x28: case 0x29: case 0x2A: case 0x2B: // XCH A,Rr
		case 0x2C: case 0x2D: case 0x2E: case 0x2F:
			temp8 = get_R(opcode & 7); set_R(opcode & 7, A); A = temp8;
			cycles -= 1;
			break;
		case 0x30: case 0x31: // XCHD A,@Rr - swap low nibbles only
			ptr = get_R(opcode & 1);
			temp8 = ram_get(ptr);
			ram_set(ptr, (temp8 & 0xF0) | (A & 0x0F));
			A = (A & 0xF0) | (temp8 & 0x0F);
			cycles -= 1;
			break;

		// ================================================================
		// Increment / Decrement
		// ================================================================
		case 0x17: // INC A
			A++; cycles -= 1; break;
		case 0x07: // DEC A
			A--; cycles -= 1; break;
		case 0x10: case 0x11: // INC @Rr
			ptr = get_R(opcode & 1); ram_set(ptr, ram_get(ptr) + 1); cycles -= 1; break;
		case 0x18: case 0x19: case 0x1A: case 0x1B: // INC Rr
		case 0x1C: case 0x1D: case 0x1E: case 0x1F:
			set_R(opcode & 7, get_R(opcode & 7) + 1); cycles -= 1; break;
		case 0xC8: case 0xC9: case 0xCA: case 0xCB: // DEC Rr
		case 0xCC: case 0xCD: case 0xCE: case 0xCF:
			set_R(opcode & 7, get_R(opcode & 7) - 1); cycles -= 1; break;

		// ================================================================
		// Accumulator unary ops
		// ================================================================
		case 0x27: // CLR A
			A = 0; cycles -= 1; break;
		case 0x37: // CPL A
			A = ~A; cycles -= 1; break;
		case 0x47: // SWAP A - swap nibbles
			A = (A << 4) | (A >> 4); cycles -= 1; break;
		case 0x57: // DA A
			daa(); cycles -= 1; break;
		case 0x77: // RR A
			A = (A >> 1) | (A << 7); cycles -= 1; break;
		case 0x67: // RRC A
			temp8 = (carry() ? 0x80 : 0); set_carry(A & 1); A = (A >> 1) | temp8; cycles -= 1; break;
		case 0xE7: // RL A
			A = (A << 1) | (A >> 7); cycles -= 1; break;
		case 0xF7: // RLC A
			temp8 = (carry() ? 1 : 0); set_carry(A & 0x80); A = (A << 1) | temp8; cycles -= 1; break;

		// ================================================================
		// Carry / flag manipulation
		// ================================================================
		case 0x97: // CLR C
			PSW &= ~PSW_C; cycles -= 1; break;
		case 0xA7: // CPL C
			PSW ^= PSW_C; cycles -= 1; break;
		case 0x85: // CLR F0
			PSW &= ~PSW_F0; cycles -= 1; break;
		case 0x95: // CPL F0
			PSW ^= PSW_F0; cycles -= 1; break;
		case 0xA5: // CLR F1
			F1 = false; cycles -= 1; break;
		case 0xB5: // CPL F1
			F1 = !F1; cycles -= 1; break;

		// ================================================================
		// PSW access
		// ================================================================
		case 0xC7: // MOV A,PSW
			A = PSW | PSW_X3; cycles -= 1; break;
		case 0xD7: // MOV PSW,A
			PSW = A | PSW_X3; cycles -= 1; break;

		// ================================================================
		// Register-bank / memory-bank / interrupt control
		// ================================================================
		case 0xC5: // SEL RB0
			PSW &= ~PSW_BS; cycles -= 1; break;
		case 0xD5: // SEL RB1
			PSW |= PSW_BS; cycles -= 1; break;
		case 0xE5: // SEL MB0
			DBF = false; cycles -= 1; break;
		case 0xF5: // SEL MB1
			DBF = true; cycles -= 1; break;

		case 0x05: // EN I
			irq_ext_enabled = true; cycles -= 1; break;
		case 0x15: // DIS I
			irq_ext_enabled = false; cycles -= 1; break;
		case 0x25: // EN TCNTI
			irq_timer_enabled = true; cycles -= 1; break;
		case 0x35: // DIS TCNTI
			irq_timer_enabled = false; irq_timer_pending = false; cycles -= 1; break;

		// ================================================================
		// Timer / counter control + access
		// ================================================================
		case 0x42: // MOV A,T
			A = reg_T; cycles -= 1; break;
		case 0x62: // MOV T,A
			reg_T = A; cycles -= 1; break;
		case 0x45: // STRT CNT
			counter_running = true; timer_running = false;
			old_t1_sample = mame_compat ? (io_in(MPORT_T1) & 1) : 0;
			cycles -= 1; break;
		case 0x55: // STRT T
			timer_running = true; counter_running = false;
			timer_prescaler = 0;   // MAME resets the prescaler on STRT T
			cycles -= 1; break;
		case 0x65: // STOP TCNT
			timer_running = false; counter_running = false; cycles -= 1; break;
		case 0x75: // ENT0 CLK - enable clock output on T0 (no emulated effect)
			cycles -= 1; break;

		// ================================================================
		// BUS / Port I/O
		// In MAME mode the named ports use the 16-bit map (P1=0x101 etc.),
		// IN A,Pp is gated by the output latch, and ANL/ORL BUS is a
		// read-modify-write through the bus port. In native mode the simple
		// 0/1/2 numbering and per-port output latches are used.
		// ================================================================
		case 0x08: // INS A,BUS
			A = io_in(pnum_bus()); cycles -= 2; break;
		case 0x02: // OUTL BUS,A
			port_latch[PORT_BUS] = A; io_out(pnum_bus(), A); cycles -= 2; break;
		case 0x88: // ORL BUS,#data
			operand = fetch();
			if (mame_compat) { io_out(pnum_bus(), io_in(pnum_bus()) | operand); }
			else { port_latch[PORT_BUS] |= operand; io_out(pnum_bus(), port_latch[PORT_BUS]); }
			cycles -= 2; break;
		case 0x98: // ANL BUS,#data
			operand = fetch();
			if (mame_compat) { io_out(pnum_bus(), io_in(pnum_bus()) & operand); }
			else { port_latch[PORT_BUS] &= operand; io_out(pnum_bus(), port_latch[PORT_BUS]); }
			cycles -= 2; break;

		case 0x09: // IN A,P1
			A = io_in(pnum_p1());
			if (mame_compat) A &= port_latch[PORT_P1];
			cycles -= 2; break;
		case 0x0A: // IN A,P2
			A = io_in(pnum_p2());
			if (mame_compat) A &= port_latch[PORT_P2];
			cycles -= 2; break;
		case 0x39: // OUTL P1,A
			port_latch[PORT_P1] = A; io_out(pnum_p1(), A); cycles -= 2; break;
		case 0x3A: // OUTL P2,A
			port_latch[PORT_P2] = A; io_out(pnum_p2(), A); cycles -= 2; break;
		case 0x89: // ORL P1,#data
			operand = fetch(); port_latch[PORT_P1] |= operand; io_out(pnum_p1(), port_latch[PORT_P1]); cycles -= 2; break;
		case 0x8A: // ORL P2,#data
			operand = fetch(); port_latch[PORT_P2] |= operand; io_out(pnum_p2(), port_latch[PORT_P2]); cycles -= 2; break;
		case 0x99: // ANL P1,#data
			operand = fetch(); port_latch[PORT_P1] &= operand; io_out(pnum_p1(), port_latch[PORT_P1]); cycles -= 2; break;
		case 0x9A: // ANL P2,#data
			operand = fetch(); port_latch[PORT_P2] &= operand; io_out(pnum_p2(), port_latch[PORT_P2]); cycles -= 2; break;

		// ================================================================
		// Expander ports P4..P7 via MOVD / ORLD / ANLD.
		// MAME mode: read-modify-write straight through the port (full byte).
		// Native mode: 4-bit output latches.
		// ================================================================
		case 0x0C: case 0x0D: case 0x0E: case 0x0F: // MOVD A,Pp
			ptr = opcode & 3;
			A = io_in(pnum_exp(ptr));
			if (!mame_compat) A &= 0x0F;
			cycles -= 2; break;
		case 0x3C: case 0x3D: case 0x3E: case 0x3F: // MOVD Pp,A
			ptr = opcode & 3;
			if (mame_compat) { io_out(pnum_exp(ptr), A); }
			else { port_latch[4 + ptr] = A & 0x0F; io_out(pnum_exp(ptr), A & 0x0F); }
			cycles -= 2; break;
		case 0x8C: case 0x8D: case 0x8E: case 0x8F: // ORLD Pp,A
			ptr = opcode & 3;
			if (mame_compat) { io_out(pnum_exp(ptr), io_in(pnum_exp(ptr)) | A); }
			else { port_latch[4 + ptr] = (port_latch[4 + ptr] | (A & 0x0F)) & 0x0F; io_out(pnum_exp(ptr), port_latch[4 + ptr]); }
			cycles -= 2; break;
		case 0x9C: case 0x9D: case 0x9E: case 0x9F: // ANLD Pp,A
			ptr = opcode & 3;
			if (mame_compat) { io_out(pnum_exp(ptr), io_in(pnum_exp(ptr)) & A); }
			else { port_latch[4 + ptr] = (port_latch[4 + ptr] & (A & 0x0F)) & 0x0F; io_out(pnum_exp(ptr), port_latch[4 + ptr]); }
			cycles -= 2; break;

		// ================================================================
		// External data memory (MOVX).
		// MAME mode routes through the I/O port space (port = R0/R1); native
		// mode uses the ext_read_cb/ext_write_cb data path (extRAM fallback).
		// ================================================================
		case 0x80: case 0x81: // MOVX A,@Rr
			ptr = get_R(opcode & 1);
			A = mame_compat ? io_in(ptr) : ext_read(ptr);
			cycles -= 2; break;
		case 0x90: case 0x91: // MOVX @Rr,A
			ptr = get_R(opcode & 1);
			if (mame_compat) io_out(ptr, A); else ext_write(ptr, A);
			cycles -= 2; break;

		// ================================================================
		// Undefined / unimplemented opcodes - treated as NOP (1 cycle)
		// ================================================================
		default:
			if (log_debug_rw)
				LOG_INFO("I8039 unrecognised opcode %02X at %04X", opcode, (reg_PC - 1) & PROG_MASK);
			cycles -= 1;
			break;

		} // end switch(opcode)

		// Advance the timer/counter by the cycles this instruction consumed.
		int consumed = last_cycles - cycles;
		step_timer(consumed);

		// Update clock tick total (same accounting style as cpu_i8085).
		clocktickstotal += abs(consumed);
		if (clocktickstotal > 0x0FFFFFFF) clocktickstotal = 0;

	} // end while(cycles > 0)

	return cycles;
}
