#include "cpu_m68000.h"
#include <cstring>

// Logging: in the AAE build use the engine logger (aae\system\util\sys_log.h).
// In the standalone test build define CPU_M68000_NO_AAE_LOG to stub it out.
#if defined(CPU_M68000_NO_AAE_LOG)
#define LOG_INFO(...)  ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define timer_update(c, n) ((void)0)   // standalone test build: no AAE timer subsystem
#else
#include "sys_log.h"    // Log::write / LOG_INFO / LOG_DEBUG
#include "timer.h"      // timer_update -- the core drives the AAE timer itself
#endif

// Unhandled-memory traces go to the DEBUG level so they don't flood the Info log.
// (call Log::setLevel(Log::Level::Debug) to see them.)
#define M68K_LOG(...) LOG_DEBUG(__VA_ARGS__)

#define AAE_READ_BYTE(B,A)  ((B)[(A)^1])
#define AAE_READ_WORD(B,A)  ((((B)[(A)+1])<<8) | (B)[(A)])
#define AAE_WRITE_BYTE(B,A,V) (B)[(A)^1]=(uint8_t)((V)&0xff)
#define AAE_WRITE_WORD(B,A,V) do{ (B)[(A)+1]=(uint8_t)(((V)>>8)&0xff); (B)[(A)]=(uint8_t)((V)&0xff);}while(0)

// Forward declarations for opcode handlers referenced by the generated table.
static void op_illegal(cpu_m68000*);
static void op_trap(cpu_m68000*);
static void op_rte(cpu_m68000*);
static void op_move(cpu_m68000*);
static void op_movea(cpu_m68000*);
static void op_lea(cpu_m68000*);
static void op_add(cpu_m68000*);
static void op_sub(cpu_m68000*);
static void op_bcc(cpu_m68000*);
static void op_nop(cpu_m68000*);
static void op_rts(cpu_m68000*);
static void op_neg(cpu_m68000*);
static void op_not(cpu_m68000*);
static void op_clr(cpu_m68000*);
static void op_tst(cpu_m68000*);
static void op_or(cpu_m68000*);
static void op_and(cpu_m68000*);
static void op_cmpeor(cpu_m68000*);
static void op_imm0(cpu_m68000*);
static void op_5(cpu_m68000*);
static void op_moveq(cpu_m68000*);
static void op_E(cpu_m68000*);
static void op_4(cpu_m68000*);

#include "cpu_m68000_ops.inc"   // defines s_optable[65536] and s_basecycles[65536]

cpu_m68000::cpu_m68000(MemoryReadByte* r8, MemoryWriteByte* w8,
	MemoryReadWord* r16, MemoryWriteWord* w16, int cpu_num)
	: m_read8(r8), m_write8(w8), m_read16(r16), m_write16(w16), m_cpu_num(cpu_num) {
	std::memset(dar, 0, sizeof(dar));
	sp_other = pc = ppc = 0; ir = 0;
	flag_n = flag_z = flag_v = flag_c = flag_x = 0; int_mask = 7;
	s_flag = true; t_flag = stopped = halted = false;
	cycles_left = 0; odometer = 0; irq_pending = 0; m_tick_base = 0;
	reset();
	LOG_INFO("cpu_m68000[%d] init (from-scratch core): PC=%08X SP=%08X SR=%04X",
		m_cpu_num, pc, dar[15], make_sr());
}
cpu_m68000::~cpu_m68000() {}

// The MC68000 has a 24-bit address bus; mask all addresses before bus access.
uint32_t cpu_m68000::read8(uint32_t address) {
	address &= 0x00FFFFFF;
	for (MemoryReadByte* m = m_read8; m->lowAddr != 0xffffffff; ++m)
		if (address >= m->lowAddr && address <= m->highAddr)
			return m->memoryCall ? (uint8_t)m->memoryCall(address - m->lowAddr, m)
			: (uint8_t)AAE_READ_BYTE((uint8_t*)m->pUserArea, address - m->lowAddr);
	M68K_LOG("unhandled r8 %x", address); return 0;
}
void cpu_m68000::write8(uint32_t address, uint32_t value) {
	address &= 0x00FFFFFF;
	for (MemoryWriteByte* m = m_write8; m->lowAddr != 0xffffffff; ++m)
		if (address >= m->lowAddr && address <= m->highAddr) {
			if (m->memoryCall) m->memoryCall(address - m->lowAddr, (uint8_t)value, m);
			else AAE_WRITE_BYTE((uint8_t*)m->pUserArea, address - m->lowAddr, (uint8_t)value);
			return;
		}
}
uint32_t cpu_m68000::read16(uint32_t address) {
	address &= 0x00FFFFFF;
	for (MemoryReadWord* m = m_read16; m->lowAddr != 0xffffffff; ++m)
		if (address >= m->lowAddr && address <= m->highAddr)
			return m->memoryCall ? (uint16_t)m->memoryCall(address - m->lowAddr, m)
			: (uint16_t)AAE_READ_WORD((uint8_t*)m->pUserArea, address - m->lowAddr);
	M68K_LOG("unhandled r16 %x", address); return 0;
}
void cpu_m68000::write16(uint32_t address, uint32_t value) {
	address &= 0x00FFFFFF;
	for (MemoryWriteWord* m = m_write16; m->lowAddr != 0xffffffff; ++m)
		if (address >= m->lowAddr && address <= m->highAddr) {
			if (m->memoryCall) m->memoryCall(address - m->lowAddr, (uint16_t)value, m);
			else AAE_WRITE_WORD((uint8_t*)m->pUserArea, address - m->lowAddr, (uint16_t)value);
			return;
		}
}
uint32_t cpu_m68000::read32(uint32_t a) { return (read16(a) << 16) | read16(a + 2); }
void cpu_m68000::write32(uint32_t a, uint32_t v) { write16(a, (v >> 16) & 0xffff); write16(a + 2, v & 0xffff); }
uint16_t cpu_m68000::make_sr() const {
	return (uint16_t)((t_flag << 15) | (s_flag << 13) | ((int_mask & 7) << 8)
		| (flag_x ? 0x10 : 0) | (flag_n ? 0x08 : 0) | (flag_z ? 0x04 : 0) | (flag_v ? 0x02 : 0) | (flag_c ? 0x01 : 0));
}
uint16_t cpu_m68000::GetSR() const { return make_sr(); }
void cpu_m68000::set_ccr(uint8_t c) {
	flag_x = (c >> 4) & 1; flag_n = (c >> 3) & 1; flag_z = (c >> 2) & 1; flag_v = (c >> 1) & 1; flag_c = c & 1;
}
void cpu_m68000::set_sr(uint16_t sr) {
	bool new_s = (sr >> 13) & 1;
	if (new_s != s_flag) {                 // swap active SP
		uint32_t tmp = dar[15]; dar[15] = sp_other; sp_other = tmp;
	}
	t_flag = (sr >> 15) & 1; s_flag = new_s; int_mask = (sr >> 8) & 7; set_ccr((uint8_t)(sr & 0x1F));
}
void cpu_m68000::reset() {
	s_flag = true; t_flag = false; int_mask = 7; stopped = halted = false;
	sp_other = 0;
	dar[15] = read32(0x000000);
	pc = read32(0x000004); ppc = pc;
	flag_n = flag_z = flag_v = flag_c = flag_x = 0;
	irq_pending = 0;
}
void cpu_m68000::take_exception(int vector) {
	uint16_t sr = make_sr();
	if (!s_flag) { uint32_t t = dar[15]; dar[15] = sp_other; sp_other = t; s_flag = true; }
	t_flag = false;
	dar[15] -= 4; write32(dar[15], pc);   // push PC
	dar[15] -= 2; write16(dar[15], sr);   // push SR
	pc = read32(vector * 4);
	cycles_left -= 34;                  // group-2 nominal; tuned vs Musashi later
}
int cpu_m68000::exec(int cycles) {
	cycles_left = cycles;
	m_slice_cycles = cycles;            // baseline for cycles_run_this_slice()
	do {
		if (irq_pending) { check_interrupts(); if (cycles_left <= 0) break; }
		if (stopped || halted) { odometer += cycles_left; cycles_left = 0; m_slice_cycles = 0; timer_update(cycles, m_cpu_num); return cycles; }
		ppc = pc;
		ir = (uint16_t)fetch16();
		s_optable[ir](this);
	} while (cycles_left > 0);
	int ran = cycles - cycles_left;
	odometer += ran;
	// Collapse the intra-slice delta to 0 *before* timer_update so any timer
	// callback it fires (e.g. the PIT OUT pin -> IRQ) sees a clock that already
	// absorbed this slice rather than counting 'ran' a second time.
	m_slice_cycles = cycles_left;
	// Drive the AAE timer subsystem from inside the core (like cpu_z80 / cpu_6502
	// / cpu_m6809). The scheduler must NOT also call timer_update for the 68000,
	// or timers advance twice as fast.
	timer_update(ran, m_cpu_num);
	return ran;
}
int cpu_m68000::get_ticks(int reset) {
	int t = (int)odometer - m_tick_base;
	if (reset) m_tick_base = (int)odometer;
	return t;
}
void cpu_m68000::end_timeslice() { cycles_left = 0; }
// irq_pending is a BITMASK of pending interrupt levels (bit n = level n asserted),
// NOT a single latched level. The 68000's IPL pins reflect the HIGHEST pending
// device; lower-priority requests stay pending while a higher one is serviced.
// A single-level latch (last-writer-wins) silently dropped a pending IRQ when two
// sources (e.g. cchasm's timer IRQ4 + refresh IRQ2) asserted in the same window.
void cpu_m68000::irq_line(int level) {
	if (level <= 0) { irq_pending = 0; return; }   // level 0 / negative = clear all pending
	if (level > 7) level = 7;
	irq_pending |= (uint8_t)(1u << level);
}
void cpu_m68000::check_interrupts() {
	if (irq_pending == 0) return;
	int level = 7;                                 // find the highest pending level
	while (level > 0 && !(irq_pending & (1u << level))) --level;
	if (level == 0) return;
	if (level == 7 || level > int_mask) {
		int vec;
		if (m_int_ack_cb) {
			int r = m_int_ack_cb(level);
			vec = (r == CPU_M68000_INT_ACK_AUTOVECTOR) ? (0x18 + level) : (r & 0xff);
		}
		else vec = 0x18 + level;
		stopped = false;
		uint16_t sr = make_sr();
		if (!s_flag) { uint32_t t = dar[15]; dar[15] = sp_other; sp_other = t; s_flag = true; }
		t_flag = false;
		dar[15] -= 4; write32(dar[15], pc);
		dar[15] -= 2; write16(dar[15], sr);
		int_mask = level;
		pc = read32(vec * 4);
		cycles_left -= 44;
		//M68K_LOG("M68K IRQ svc L%d  ppc=%06X dst=%06X", level, (unsigned)ppc, (unsigned)pc);  // DEBUG (vertigo)
		irq_pending &= ~(uint8_t)(1u << level);    // clear ONLY the serviced level; others stay pending
	}
}
static inline uint32_t sext8(uint32_t v) { return (uint32_t)(int32_t)(int8_t)v; }
static inline uint32_t sext16(uint32_t v) { return (uint32_t)(int32_t)(int16_t)v; }

uint32_t cpu_m68000::ea_addr(int mode, int reg, int size) {
	switch (mode) {
	case 2: return dar[8 + reg];                                   // (An)
	case 3: {
		uint32_t a = dar[8 + reg]; int inc = (size == 2 ? 4 : size == 1 ? 2 : (reg == 7 ? 2 : 1));
		dar[8 + reg] += inc; return a;
	}                        // (An)+
	case 4: {
		int dec = (size == 2 ? 4 : size == 1 ? 2 : (reg == 7 ? 2 : 1));
		dar[8 + reg] -= dec; return dar[8 + reg];
	}               // -(An)
	case 5: { uint32_t d = sext16(fetch16()); return dar[8 + reg] + d; }// d16(An)
	case 6: {
		uint16_t ext = (uint16_t)fetch16();
		int xreg = (ext >> 12) & 0xF; uint32_t xv = dar[xreg];
		if (!(ext & 0x0800)) xv = sext16(xv);
		return dar[8 + reg] + sext8(ext) + xv;
	}                  // d8(An,Xn)
	case 7:
		switch (reg) {
		case 0: return sext16(fetch16());                         // abs.W
		case 1: return fetch32();                                 // abs.L
		case 2: { uint32_t base = pc; uint32_t d = sext16(fetch16()); return base + d; } // d16(PC)
		case 3: {
			uint32_t base = pc; uint16_t ext = (uint16_t)fetch16();
			int xreg = (ext >> 12) & 0xF; uint32_t xv = dar[xreg];
			if (!(ext & 0x0800)) xv = sext16(xv);
			return base + sext8(ext) + xv;
		}                    // d8(PC,Xn)
		}
	}
	return 0; // Dn/An/imm have no address
}
uint32_t cpu_m68000::ea_read(int mode, int reg, int size) {
	if (mode == 0) { uint32_t v = dar[reg]; return size == 0 ? (v & 0xff) : size == 1 ? (v & 0xffff) : v; }
	if (mode == 1) { uint32_t v = dar[8 + reg]; return size == 1 ? sext16(v) : v; }
	if (mode == 7 && reg == 4) { // immediate
		if (size == 0) return fetch16() & 0xff;
		if (size == 1) return fetch16() & 0xffff;
		return fetch32();
	}
	uint32_t a = ea_addr(mode, reg, size);
	return ea_read_addr(mode, reg, size, a);
}
uint32_t cpu_m68000::ea_read_addr(int, int, int size, uint32_t a) {
	return size == 0 ? read8(a) : size == 1 ? read16(a) : read32(a);
}
void cpu_m68000::ea_write(int mode, int reg, int size, uint32_t v) {
	if (mode == 0) {
		if (size == 0) dar[reg] = (dar[reg] & ~0xffu) | (v & 0xff);
		else if (size == 1) dar[reg] = (dar[reg] & ~0xffffu) | (v & 0xffff);
		else dar[reg] = v; return;
	}
	if (mode == 1) { dar[8 + reg] = size == 1 ? sext16(v) : v; return; }
	uint32_t a = ea_addr(mode, reg, size);
	if (size == 0) write8(a, v); else if (size == 1) write16(a, v); else write32(a, v);
}
int cpu_m68000::ea_cycles(int mode, int reg, int size) {
	int base = 0;
	switch (mode) {
	case 0: case 1: return 0;
	case 2: case 3: base = 4; break;
	case 4: base = 6; break;
	case 5: base = 8; break;
	case 6: base = 10; break;
	case 7:
		if (reg == 0) base = 8; else if (reg == 1) base = 12; else if (reg == 2) base = 8;
		else if (reg == 3) base = 10; else if (reg == 4) return size == 2 ? 8 : 4; // imm
		break;
	}
	if (size == 2 && mode != 0 && mode != 1) base += 4;
	return base;
}
static inline void setNZ(cpu_m68000* c, uint32_t v, int size) {
	uint32_t m = size == 0 ? 0x80 : size == 1 ? 0x8000 : 0x80000000;
	uint32_t mask = size == 0 ? 0xff : size == 1 ? 0xffff : 0xffffffff;
	c->flag_n = (v & m) ? 1 : 0; c->flag_z = (v & mask) ? 0 : 1; c->flag_v = 0; c->flag_c = 0;
}
static void op_move(cpu_m68000* c) {
	uint16_t w = c->ir; int ssz = (w >> 12) & 3; int size = ssz == 1 ? 0 : ssz == 3 ? 1 : 2; // 01B 11W 10L
	int smode = (w >> 3) & 7, sreg = w & 7;
	int dmode = (w >> 6) & 7, dreg = (w >> 9) & 7;
	uint32_t v = c->ea_read(smode, sreg, size);
	c->ea_write(dmode, dreg, size, v);
	setNZ(c, v, size);
	c->cycles_left -= 4 + c->ea_cycles(smode, sreg, size) + c->ea_cycles(dmode, dreg, size);
}
static void op_movea(cpu_m68000* c) {
	uint16_t w = c->ir; int ssz = (w >> 12) & 3; int size = ssz == 3 ? 1 : 2; // word or long
	int smode = (w >> 3) & 7, sreg = w & 7; int dreg = (w >> 9) & 7;
	uint32_t v = c->ea_read(smode, sreg, size);
	c->dar[8 + dreg] = size == 1 ? sext16(v) : v;       // word source sign-extends to 32
	c->cycles_left -= 4 + c->ea_cycles(smode, sreg, size);
}
static inline uint32_t szmask(int s) { return s == 0 ? 0xff : s == 1 ? 0xffff : 0xffffffff; }
static inline uint32_t szbit(int s) { return s == 0 ? 0x80 : s == 1 ? 0x8000 : 0x80000000; }

static uint32_t do_add(cpu_m68000* c, uint32_t a, uint32_t b, int s) {
	uint32_t m = szmask(s), sb = szbit(s);
	uint64_t full = (uint64_t)(a & m) + (uint64_t)(b & m);
	uint32_t r = (uint32_t)(full & m);
	c->flag_c = (full > (uint64_t)m) ? 1 : 0;
	c->flag_v = (((a ^ r) & (b ^ r)) & sb) ? 1 : 0;
	c->flag_x = c->flag_c; c->flag_n = (r & sb) ? 1 : 0; c->flag_z = r ? 0 : 1;
	return r;
}
static uint32_t do_sub(cpu_m68000* c, uint32_t a, uint32_t b, int s) { // a - b
	uint32_t m = szmask(s), r = (a - b) & m, sb = szbit(s);
	c->flag_c = (a & m) < (b & m);
	c->flag_v = (((a ^ b) & (a ^ r)) & sb) ? 1 : 0;
	c->flag_x = c->flag_c; c->flag_n = (r & sb) ? 1 : 0; c->flag_z = r ? 0 : 1;
	return r;
}
// Extended add/sub: include X, and Z is only ever CLEARED (multi-precision chains).
static uint32_t do_addx(cpu_m68000* c, uint32_t a, uint32_t b, int s) {
	uint32_t m = szmask(s), sb = szbit(s);
	uint64_t full = (uint64_t)(a & m) + (uint64_t)(b & m) + (uint64_t)(c->flag_x ? 1 : 0);
	uint32_t r = (uint32_t)(full & m);
	c->flag_c = (full > (uint64_t)m) ? 1 : 0;
	c->flag_v = (((a ^ r) & (b ^ r)) & sb) ? 1 : 0;
	c->flag_x = c->flag_c; c->flag_n = (r & sb) ? 1 : 0;
	if (r & m) c->flag_z = 0;
	return r;
}
static uint32_t do_subx(cpu_m68000* c, uint32_t a, uint32_t b, int s) { // a - b - X
	uint32_t m = szmask(s), sb = szbit(s);
	uint64_t full = (uint64_t)(a & m) - (uint64_t)(b & m) - (uint64_t)(c->flag_x ? 1 : 0);
	uint32_t r = (uint32_t)(full & m);
	c->flag_c = ((full >> 63) & 1) ? 1 : 0;   // borrow -> 64-bit underflow sets the top bit
	c->flag_v = (((a ^ b) & (a ^ r)) & sb) ? 1 : 0;
	c->flag_x = c->flag_c; c->flag_n = (r & sb) ? 1 : 0;
	if (r & m) c->flag_z = 0;
	return r;
}
// ADDX/SUBX shared driver: bit3 of the opcode picks Dy,Dx (0) or -(Ay),-(Ax) (1).
static void do_xop(cpu_m68000* c, int size, int rx, int ry, bool predec,
	uint32_t(*xop)(cpu_m68000*, uint32_t, uint32_t, int)) {
	if (!predec) { // Dy,Dx -> Dx
		uint32_t r = xop(c, c->dar[rx] & szmask(size), c->dar[ry] & szmask(size), size);
		c->dar[rx] = (c->dar[rx] & ~szmask(size)) | (r & szmask(size));
		c->cycles_left -= (size == 2 ? 8 : 4);
	}
	else {     // -(Ay),-(Ax) -> (Ax)
		int dy = size == 2 ? 4 : size == 1 ? 2 : (ry == 7 ? 2 : 1);
		c->dar[8 + ry] -= dy; uint32_t sv = (size == 0 ? c->read8(c->dar[8 + ry]) : size == 1 ? c->read16(c->dar[8 + ry]) : c->read32(c->dar[8 + ry]));
		int dx = size == 2 ? 4 : size == 1 ? 2 : (rx == 7 ? 2 : 1);
		c->dar[8 + rx] -= dx; uint32_t dv = (size == 0 ? c->read8(c->dar[8 + rx]) : size == 1 ? c->read16(c->dar[8 + rx]) : c->read32(c->dar[8 + rx]));
		uint32_t r = xop(c, dv, sv, size);
		if (size == 0)c->write8(c->dar[8 + rx], r); else if (size == 1)c->write16(c->dar[8 + rx], r); else c->write32(c->dar[8 + rx], r);
		c->cycles_left -= (size == 2 ? 30 : 18);
	}
}
static void do_cmp(cpu_m68000* c, uint32_t a, uint32_t b, int s) { // a - b, sets NZVC (X untouched)
	uint32_t m = szmask(s), r = (a - b) & m, sb = szbit(s);
	c->flag_c = (a & m) < (b & m);
	c->flag_v = (((a ^ b) & (a ^ r)) & sb) ? 1 : 0;
	c->flag_n = (r & sb) ? 1 : 0; c->flag_z = r ? 0 : 1;
}
// Shift/rotate a `size` value by `cnt`. type: 0=AS, 1=LS, 2=ROX, 3=RO. dir: 0=right, 1=left.
static uint32_t do_shift(cpu_m68000* c, uint32_t v, int cnt, int size, int type, int dir) {
	uint32_t m = szmask(size), sb = szbit(size); v &= m; bool carry = false;
	if (type == 3) {                               // RO (rotate, no X)
		if (cnt == 0) { c->flag_c = 0; }
		else if (dir) { for (int i = 0; i < cnt; i++) { carry = (v & sb) != 0; v = ((v << 1) | (carry ? 1 : 0)) & m; } c->flag_c = carry ? 1 : 0; }
		else { for (int i = 0; i < cnt; i++) { carry = (v & 1) != 0;  v = ((v >> 1) | (carry ? sb : 0)) & m; } c->flag_c = carry ? 1 : 0; }
		c->flag_v = 0;
	}
	else if (type == 2) {                        // ROX (rotate through X)
		if (cnt == 0) { c->flag_c = c->flag_x; }
		else {
			int x = c->flag_x ? 1 : 0;
			if (dir) { for (int i = 0; i < cnt; i++) { int nb = (v & sb) ? 1 : 0; v = ((v << 1) | x) & m; x = nb; } }
			else { for (int i = 0; i < cnt; i++) { int nb = (v & 1) ? 1 : 0;  v = ((v >> 1) | (x ? sb : 0)) & m; x = nb; } }
			c->flag_x = x; c->flag_c = x;
		}
		c->flag_v = 0;
	}
	else {                                   // AS / LS
		if (cnt == 0) { c->flag_c = 0; c->flag_v = 0; }
		else if (dir) {                          // left (ASL/LSL)
			bool ovf = false;
			for (int i = 0; i < cnt; i++) { bool om = (v & sb) != 0; carry = om; v = (v << 1) & m; if (type == 0 && om != ((v & sb) != 0)) ovf = true; }
			c->flag_c = carry ? 1 : 0; c->flag_x = carry ? 1 : 0; c->flag_v = (type == 0 && ovf) ? 1 : 0;
		}
		else {                               // right (ASR/LSR)
			int bits = (size == 0 ? 8 : size == 1 ? 16 : 32);
			bool msb = (v & sb) != 0;
			for (int i = 0; i < cnt; i++) { carry = (v & 1) != 0; v >>= 1; if (type == 0 && msb) v |= sb; v &= m; }
			if (cnt > bits) carry = false;          // shifted past operand width: hw reports C=0, not sign fill
			c->flag_c = carry ? 1 : 0; c->flag_x = carry ? 1 : 0; c->flag_v = 0;
		}
	}
	c->flag_n = (v & sb) ? 1 : 0; c->flag_z = (v & m) ? 0 : 1;
	return v & m;
}
// Generic unary op over an EA operand. Computes the EA address ONCE (avoids the
// double-advance EA caveat). `compute` sets flags and returns the result; if
// write_back, the result is stored back to the same operand.
static void do_unary(cpu_m68000* c, int size, bool write_back,
	uint32_t(*compute)(cpu_m68000*, uint32_t, int)) {
	uint16_t w = c->ir; int mode = (w >> 3) & 7, reg = w & 7;
	uint32_t v, a = 0;
	if (mode == 0) v = c->dar[reg] & szmask(size);
	else { a = c->ea_addr(mode, reg, size); v = c->ea_read_addr(mode, reg, size, a); }
	uint32_t r = compute(c, v, size);
	if (write_back) {
		if (mode == 0) c->dar[reg] = (c->dar[reg] & ~szmask(size)) | (r & szmask(size));
		else { if (size == 0) c->write8(a, r); else if (size == 1) c->write16(a, r); else c->write32(a, r); }
	}
	c->cycles_left -= (mode == 0 ? (size == 2 ? 6 : 4) : ((size == 2 ? 12 : 8) + c->ea_cycles(mode, reg, size)));
}
static uint32_t comp_neg(cpu_m68000* c, uint32_t v, int s) { return do_sub(c, 0, v, s); }
static uint32_t comp_not(cpu_m68000* c, uint32_t v, int s) { uint32_t m = szmask(s), sb = szbit(s), r = (~v) & m; c->flag_n = (r & sb) ? 1 : 0; c->flag_z = r ? 0 : 1; c->flag_v = 0; c->flag_c = 0; return r; }
static uint32_t comp_clr(cpu_m68000* c, uint32_t v, int s) { (void)v; (void)s; c->flag_n = 0; c->flag_z = 1; c->flag_v = 0; c->flag_c = 0; return 0; }
static uint32_t comp_tst(cpu_m68000* c, uint32_t v, int s) { uint32_t m = szmask(s), sb = szbit(s); c->flag_n = (v & sb) ? 1 : 0; c->flag_z = (v & m) ? 0 : 1; c->flag_v = 0; c->flag_c = 0; return v; }
static void op_neg(cpu_m68000* c) { int s = (c->ir >> 6) & 3; do_unary(c, s, true, comp_neg); }
static void op_not(cpu_m68000* c) { int s = (c->ir >> 6) & 3; do_unary(c, s, true, comp_not); }
static void op_clr(cpu_m68000* c) { int s = (c->ir >> 6) & 3; do_unary(c, s, true, comp_clr); }
static void op_tst(cpu_m68000* c) { int s = (c->ir >> 6) & 3; do_unary(c, s, false, comp_tst); }

static void op_lea(cpu_m68000* c) {
	uint16_t w = c->ir; int an = (w >> 9) & 7; int mode = (w >> 3) & 7, reg = w & 7;
	c->dar[8 + an] = c->ea_addr(mode, reg, 2);
	c->cycles_left -= c->ea_cycles(mode, reg, 2);
}
// 0xD block: ADD (<ea>+Dn->Dn / Dn+<ea>-><ea>), ADDA (opmode 3/7), ADDX (opmode 4/5/6, ea-mode<=1)
static void op_add(cpu_m68000* c) {
	uint16_t w = c->ir; int rn = (w >> 9) & 7; int op = (w >> 6) & 7; int mode = (w >> 3) & 7, reg = w & 7;
	if (op == 3 || op == 7) {                                  // ADDA.W / ADDA.L (no flags)
		int size = (op == 3) ? 1 : 2;
		uint32_t v = c->ea_read(mode, reg, size); if (size == 1) v = sext16(v);
		c->dar[8 + rn] += v;
		c->cycles_left -= 8 + c->ea_cycles(mode, reg, size); return;
	}
	int size = op & 3;
	if (op < 4) {                                            // <ea> + Dn -> Dn
		uint32_t v = c->ea_read(mode, reg, size);
		uint32_t r = do_add(c, c->dar[rn], v, size);
		c->dar[rn] = (c->dar[rn] & ~szmask(size)) | (r & szmask(size));
		c->cycles_left -= (size == 2 ? 6 : 4) + c->ea_cycles(mode, reg, size);
	}
	else if (mode <= 1) {                                  // ADDX
		do_xop(c, size, rn, reg, mode == 1, do_addx);
	}
	else {                                             // Dn + <ea> -> <ea>
		uint32_t a = c->ea_addr(mode, reg, size);
		uint32_t v = c->ea_read_addr(mode, reg, size, a);
		uint32_t r = do_add(c, v, c->dar[rn], size);
		if (size == 0) c->write8(a, r); else if (size == 1) c->write16(a, r); else c->write32(a, r);
		c->cycles_left -= (size == 2 ? 12 : 8) + c->ea_cycles(mode, reg, size);
	}
}
// 0x9 block: SUB (Dn-<ea>->Dn / <ea>-Dn-><ea>), SUBA (opmode 3/7), SUBX (opmode 4/5/6, ea-mode<=1)
static void op_sub(cpu_m68000* c) {
	uint16_t w = c->ir; int rn = (w >> 9) & 7; int op = (w >> 6) & 7; int mode = (w >> 3) & 7, reg = w & 7;
	if (op == 3 || op == 7) {                                  // SUBA.W / SUBA.L (no flags)
		int size = (op == 3) ? 1 : 2;
		uint32_t v = c->ea_read(mode, reg, size); if (size == 1) v = sext16(v);
		c->dar[8 + rn] -= v;
		c->cycles_left -= 8 + c->ea_cycles(mode, reg, size); return;
	}
	int size = op & 3;
	if (op < 4) {                                            // Dn - <ea> -> Dn
		uint32_t v = c->ea_read(mode, reg, size);
		uint32_t r = do_sub(c, c->dar[rn], v, size);
		c->dar[rn] = (c->dar[rn] & ~szmask(size)) | (r & szmask(size));
		c->cycles_left -= (size == 2 ? 6 : 4) + c->ea_cycles(mode, reg, size);
	}
	else if (mode <= 1) {                                  // SUBX
		do_xop(c, size, rn, reg, mode == 1, do_subx);
	}
	else {                                             // <ea> - Dn -> <ea>
		uint32_t a = c->ea_addr(mode, reg, size);
		uint32_t v = c->ea_read_addr(mode, reg, size, a);
		uint32_t r = do_sub(c, v, c->dar[rn], size);
		if (size == 0) c->write8(a, r); else if (size == 1) c->write16(a, r); else c->write32(a, r);
		c->cycles_left -= (size == 2 ? 12 : 8) + c->ea_cycles(mode, reg, size);
	}
}
// Bit op (BTST/BCHG/BCLR/BSET). type 0..3. Dn dest -> long (bit mod 32); memory -> byte (bit mod 8).
static void do_bitop(cpu_m68000* c, int type, int bitnum, int mode, int reg) {
	if (mode == 0) {
		int b = bitnum & 31; uint32_t mask = 1u << b;
		c->flag_z = (c->dar[reg] & mask) ? 0 : 1;
		if (type == 1) c->dar[reg] ^= mask; else if (type == 2) c->dar[reg] &= ~mask; else if (type == 3) c->dar[reg] |= mask;
		c->cycles_left -= (type == 0 ? 6 : 8);
	}
	else if (type == 0) {                              // BTST: read-only; allows #imm and d(PC) sources
		uint8_t v = (uint8_t)c->ea_read(mode, reg, 0);
		c->flag_z = (v & (1u << (bitnum & 7))) ? 0 : 1;
		c->cycles_left -= 4 + c->ea_cycles(mode, reg, 0);
	}
	else {                                         // BCHG/BCLR/BSET: byte read-modify-write
		uint8_t mask = (uint8_t)(1u << (bitnum & 7));
		uint32_t a = c->ea_addr(mode, reg, 0); uint8_t v = (uint8_t)c->read8(a);
		c->flag_z = (v & mask) ? 0 : 1;
		if (type == 1) v ^= mask; else if (type == 2) v &= ~mask; else v |= mask;
		c->write8(a, v);
		c->cycles_left -= 8 + c->ea_cycles(mode, reg, 0);
	}
}
static void do_mulu(cpu_m68000* c, int dn, int mode, int reg) {
	uint32_t v = c->ea_read(mode, reg, 1) & 0xffff;
	uint32_t r = (c->dar[dn] & 0xffff) * v; c->dar[dn] = r;
	c->flag_n = (r & 0x80000000u) ? 1 : 0; c->flag_z = r ? 0 : 1; c->flag_v = 0; c->flag_c = 0;
	c->cycles_left -= 38 + c->ea_cycles(mode, reg, 1);
}
static void do_muls(cpu_m68000* c, int dn, int mode, int reg) {
	int32_t a = (int16_t)(c->dar[dn] & 0xffff), b = (int16_t)(c->ea_read(mode, reg, 1) & 0xffff);
	uint32_t r = (uint32_t)(a * b); c->dar[dn] = r;
	c->flag_n = (r & 0x80000000u) ? 1 : 0; c->flag_z = r ? 0 : 1; c->flag_v = 0; c->flag_c = 0;
	c->cycles_left -= 38 + c->ea_cycles(mode, reg, 1);
}
static void do_divu(cpu_m68000* c, int dn, int mode, int reg) {
	uint32_t divisor = c->ea_read(mode, reg, 1) & 0xffff;
	if (divisor == 0) { c->take_exception(5); return; }              // zero divide (vector 5)
	uint32_t dividend = c->dar[dn], q = dividend / divisor, rem = dividend % divisor;
	if (q > 0xffff) { c->flag_v = 1; c->flag_c = 0; c->cycles_left -= 140; return; }  // overflow: Dn unchanged
	c->dar[dn] = (rem << 16) | (q & 0xffff);
	c->flag_n = (q & 0x8000) ? 1 : 0; c->flag_z = (q & 0xffff) ? 0 : 1; c->flag_v = 0; c->flag_c = 0;
	c->cycles_left -= 140 + c->ea_cycles(mode, reg, 1);
}
static void do_divs(cpu_m68000* c, int dn, int mode, int reg) {
	int32_t divisor = (int16_t)(c->ea_read(mode, reg, 1) & 0xffff);
	if (divisor == 0) { c->take_exception(5); return; }
	int32_t dividend = (int32_t)c->dar[dn];
	if ((uint32_t)dividend == 0x80000000u && divisor == -1) { c->flag_v = 1; c->flag_c = 0; c->cycles_left -= 158; return; }
	int32_t q = dividend / divisor, rem = dividend % divisor;
	if (q > 32767 || q < -32768) { c->flag_v = 1; c->flag_c = 0; c->cycles_left -= 158; return; }
	c->dar[dn] = ((uint32_t)(rem & 0xffff) << 16) | ((uint32_t)q & 0xffff);
	c->flag_n = (q & 0x8000) ? 1 : 0; c->flag_z = (q & 0xffff) ? 0 : 1; c->flag_v = 0; c->flag_c = 0;
	c->cycles_left -= 158 + c->ea_cycles(mode, reg, 1);
}
// BCD helpers — replicate Musashi's exact algorithm (incl. its officially-undefined N).
static uint32_t do_abcd(cpu_m68000* c, uint32_t dst, uint32_t src) {
	uint32_t res = (src & 0xf) + (dst & 0xf) + (c->flag_x ? 1 : 0);
	if (res > 9) res += 6;
	res += (src & 0xf0) + (dst & 0xf0);
	int carry = (res > 0x99) ? 1 : 0; c->flag_x = carry; c->flag_c = carry;
	if (carry) res -= 0xa0;
	c->flag_n = (res & 0x80) ? 1 : 0;            // officially undefined
	res &= 0xff; if (res) c->flag_z = 0;       // sticky Z
	return res;
}
static uint32_t do_sbcd(cpu_m68000* c, uint32_t dst, uint32_t src) {
	uint32_t res = (dst & 0xf) - (src & 0xf) - (c->flag_x ? 1 : 0);
	if (res > 9) res -= 6;
	res += (dst & 0xf0) - (src & 0xf0);
	int carry = (res > 0x99) ? 1 : 0; c->flag_x = carry; c->flag_c = carry;
	if (carry) res += 0xa0;
	res &= 0xff;
	c->flag_n = (res & 0x80) ? 1 : 0;            // officially undefined
	if (res) c->flag_z = 0;                  // sticky Z
	return res;
}
// MOVEP Dn <-> alternate bytes at d(An). opmode 100=W m->r,101=L m->r,110=W r->m,111=L r->m.
static void op_movep(cpu_m68000* c) {
	uint16_t w = c->ir; int dn = (w >> 9) & 7; int opmode = (w >> 6) & 7; int an = w & 7;
	int16_t disp = (int16_t)c->fetch16(); uint32_t base = c->dar[8 + an] + disp;
	bool islong = (opmode & 1), toMem = (opmode & 2);
	if (toMem) {
		if (islong) { c->write8(base, (c->dar[dn] >> 24) & 0xff); c->write8(base + 2, (c->dar[dn] >> 16) & 0xff); c->write8(base + 4, (c->dar[dn] >> 8) & 0xff); c->write8(base + 6, c->dar[dn] & 0xff); }
		else { c->write8(base, (c->dar[dn] >> 8) & 0xff); c->write8(base + 2, c->dar[dn] & 0xff); }
		c->cycles_left -= islong ? 24 : 16;
	}
	else {
		if (islong) { uint32_t v = (c->read8(base) << 24) | (c->read8(base + 2) << 16) | (c->read8(base + 4) << 8) | c->read8(base + 6); c->dar[dn] = v; }
		else { uint32_t v = (c->read8(base) << 8) | c->read8(base + 2); c->dar[dn] = (c->dar[dn] & 0xffff0000u) | v; }
		c->cycles_left -= islong ? 24 : 16;
	}
}
// 0x8 block: OR (data dir). DIVU/DIVS (op 3/7) and SBCD (op4, ea-mode<=1) -> later batches.
static void op_or(cpu_m68000* c) {
	uint16_t w = c->ir; int dn = (w >> 9) & 7; int op = (w >> 6) & 7; int mode = (w >> 3) & 7, reg = w & 7;
	if (op == 3) { do_divu(c, dn, mode, reg); return; }                 // DIVU.W
	if (op == 7) { do_divs(c, dn, mode, reg); return; }                 // DIVS.W
	int size = op & 3;
	if (op < 4) {                                                     // <ea> | Dn -> Dn
		uint32_t v = c->ea_read(mode, reg, size);
		uint32_t r = (c->dar[dn] | v) & szmask(size); setNZ(c, r, size);
		c->dar[dn] = (c->dar[dn] & ~szmask(size)) | r;
		c->cycles_left -= (size == 2 ? 6 : 4) + c->ea_cycles(mode, reg, size);
	}
	else {
		if (op == 4 && mode <= 1) {                                     // SBCD Dy,Dx (mode0) / -(Ay),-(Ax) (mode1)
			int rx = dn, ry = reg;
			if (mode == 0) { uint32_t r = do_sbcd(c, c->dar[rx] & 0xff, c->dar[ry] & 0xff); c->dar[rx] = (c->dar[rx] & ~0xffu) | r; c->cycles_left -= 6; }
			else {
				c->dar[8 + ry] -= (ry == 7 ? 2 : 1); uint32_t s = c->read8(c->dar[8 + ry]);
				c->dar[8 + rx] -= (rx == 7 ? 2 : 1); uint32_t d = c->read8(c->dar[8 + rx]);
				uint32_t r = do_sbcd(c, d, s); c->write8(c->dar[8 + rx], (uint8_t)r); c->cycles_left -= 18;
			}
			return;
		}
		if (mode <= 1) { op_illegal(c); return; }                     // op5/6 with reg-direct ea = illegal
		uint32_t a = c->ea_addr(mode, reg, size); uint32_t v = c->ea_read_addr(mode, reg, size, a);
		uint32_t r = (v | c->dar[dn]) & szmask(size); setNZ(c, r, size);
		if (size == 0)c->write8(a, r); else if (size == 1)c->write16(a, r); else c->write32(a, r);
		c->cycles_left -= (size == 2 ? 12 : 8) + c->ea_cycles(mode, reg, size);
	}
}
// 0xC block: AND (data dir), EXG (op5 mode0/1, op6 mode1). MULU/MULS (3/7), ABCD (op4) -> later.
static void op_and(cpu_m68000* c) {
	uint16_t w = c->ir; int dn = (w >> 9) & 7; int op = (w >> 6) & 7; int mode = (w >> 3) & 7, reg = w & 7;
	if (op == 3) { do_mulu(c, dn, mode, reg); return; }                 // MULU.W
	if (op == 7) { do_muls(c, dn, mode, reg); return; }                 // MULS.W
	if (op == 5 && (mode == 0 || mode == 1)) {                              // EXG Dx,Dy / Ax,Ay
		if (mode == 0) { uint32_t t = c->dar[dn]; c->dar[dn] = c->dar[reg]; c->dar[reg] = t; }
		else { uint32_t t = c->dar[8 + dn]; c->dar[8 + dn] = c->dar[8 + reg]; c->dar[8 + reg] = t; }
		c->cycles_left -= 6; return;
	}
	if (op == 6 && mode == 1) {                                        // EXG Dx,Ay
		uint32_t t = c->dar[dn]; c->dar[dn] = c->dar[8 + reg]; c->dar[8 + reg] = t;
		c->cycles_left -= 6; return;
	}
	int size = op & 3;
	if (op < 4) {                                                     // <ea> & Dn -> Dn
		uint32_t v = c->ea_read(mode, reg, size);
		uint32_t r = (c->dar[dn] & v) & szmask(size); setNZ(c, r, size);
		c->dar[dn] = (c->dar[dn] & ~szmask(size)) | r;
		c->cycles_left -= (size == 2 ? 6 : 4) + c->ea_cycles(mode, reg, size);
	}
	else {
		if (op == 4 && mode <= 1) {                                     // ABCD Dy,Dx (mode0) / -(Ay),-(Ax) (mode1)
			int rx = dn, ry = reg;
			if (mode == 0) { uint32_t r = do_abcd(c, c->dar[rx] & 0xff, c->dar[ry] & 0xff); c->dar[rx] = (c->dar[rx] & ~0xffu) | r; c->cycles_left -= 6; }
			else {
				c->dar[8 + ry] -= (ry == 7 ? 2 : 1); uint32_t s = c->read8(c->dar[8 + ry]);
				c->dar[8 + rx] -= (rx == 7 ? 2 : 1); uint32_t d = c->read8(c->dar[8 + rx]);
				uint32_t r = do_abcd(c, d, s); c->write8(c->dar[8 + rx], (uint8_t)r); c->cycles_left -= 18;
			}
			return;
		}
		if (mode <= 1) { op_illegal(c); return; }                     // op5/6 with reg-direct ea = illegal
		uint32_t a = c->ea_addr(mode, reg, size); uint32_t v = c->ea_read_addr(mode, reg, size, a);
		uint32_t r = (v & c->dar[dn]) & szmask(size); setNZ(c, r, size);
		if (size == 0)c->write8(a, r); else if (size == 1)c->write16(a, r); else c->write32(a, r);
		c->cycles_left -= (size == 2 ? 12 : 8) + c->ea_cycles(mode, reg, size);
	}
}
// 0xB block: CMP (op0/1/2), CMPA (op3/7), CMPM (op4/5/6 & ea-mode==1), EOR (op4/5/6 else).
static void op_cmpeor(cpu_m68000* c) {
	uint16_t w = c->ir; int dn = (w >> 9) & 7; int op = (w >> 6) & 7; int mode = (w >> 3) & 7, reg = w & 7;
	if (op == 3 || op == 7) {                                           // CMPA.W/.L (32-bit compare)
		int size = (op == 3) ? 1 : 2; uint32_t v = c->ea_read(mode, reg, size); if (size == 1) v = sext16(v);
		do_cmp(c, c->dar[8 + dn], v, 2);
		c->cycles_left -= 6 + c->ea_cycles(mode, reg, size); return;
	}
	int size = op & 3;
	if (op < 4) {                                                     // CMP <ea>,Dn
		uint32_t v = c->ea_read(mode, reg, size);
		do_cmp(c, c->dar[dn], v, size);
		c->cycles_left -= (size == 2 ? 6 : 4) + c->ea_cycles(mode, reg, size);
	}
	else if (mode == 1) {                                           // CMPM (Ay)+,(Ax)+
		uint32_t sv = c->ea_read(3, reg, size);
		uint32_t dv = c->ea_read(3, dn, size);
		do_cmp(c, dv, sv, size);
		c->cycles_left -= (size == 2 ? 20 : 12);
	}
	else {                                                      // EOR Dn,<ea>
		uint32_t a = 0, v;
		if (mode == 0) v = c->dar[reg] & szmask(size);
		else { a = c->ea_addr(mode, reg, size); v = c->ea_read_addr(mode, reg, size, a); }
		uint32_t r = (v ^ c->dar[dn]) & szmask(size); setNZ(c, r, size);
		if (mode == 0) c->dar[reg] = (c->dar[reg] & ~szmask(size)) | r;
		else { if (size == 0)c->write8(a, r); else if (size == 1)c->write16(a, r); else c->write32(a, r); }
		c->cycles_left -= (mode == 0 ? (size == 2 ? 8 : 4) : ((size == 2 ? 12 : 8) + c->ea_cycles(mode, reg, size)));
	}
}
// 0x0 block: immediate ALU (ORI/ANDI/SUBI/ADDI/EORI/CMPI) + ORI/ANDI/EORI to CCR/SR.
// Static/dynamic bit ops and MOVEP route to op_illegal for now (later batch).
static void op_imm0(cpu_m68000* c) {
	uint16_t w = c->ir; int op = (w >> 9) & 7; int size = (w >> 6) & 3; int mode = (w >> 3) & 7, reg = w & 7;
	if (w & 0x0100) {                                                 // dynamic bit op (or MOVEP if mode==1)
		if (mode == 1) { op_movep(c); return; }                      // MOVEP Dn<->d(An)
		do_bitop(c, (w >> 6) & 3, (int)c->dar[(w >> 9) & 7], mode, reg); return;
	}
	if (op == 4) {                                                    // static bit op (#bit follows opcode)
		int type = (w >> 6) & 3; int bitnum = (int)(c->fetch16() & 0xff);
		do_bitop(c, type, bitnum, mode, reg); return;
	}
	if (op == 7) { op_illegal(c); return; }
	if (mode == 7 && reg == 4 && (op == 0 || op == 1 || op == 5)) {               // ORI/ANDI/EORI to CCR/SR
		uint16_t imm = (uint16_t)c->fetch16();
		if (size == 1) {                                              // to SR (privileged)
			if (!c->s_flag) { c->pc = c->ppc; c->take_exception(8); return; }
			uint16_t sr = c->make_sr();
			uint16_t r = (op == 0) ? (sr | imm) : (op == 1) ? (sr & imm) : (sr ^ imm);
			c->set_sr(r); c->cycles_left -= 20;
		}
		else {                                                  // to CCR (byte)
			uint8_t ccr = (uint8_t)(c->make_sr() & 0x1F), i8 = (uint8_t)(imm & 0xFF);
			uint8_t r = (op == 0) ? (ccr | i8) : (op == 1) ? (ccr & i8) : (ccr ^ i8);
			c->set_ccr((uint8_t)(r & 0x1F)); c->cycles_left -= 20;
		}
		return;
	}
	if (size == 3) { op_illegal(c); return; }                        // size 11 illegal for immediate ALU ops
	uint32_t imm;
	if (size == 0) imm = c->fetch16() & 0xff; else if (size == 1) imm = c->fetch16() & 0xffff; else imm = c->fetch32();
	if (op == 6) {                                                    // CMPI #,<ea>
		uint32_t v = c->ea_read(mode, reg, size);
		do_cmp(c, v, imm, size);
		c->cycles_left -= 8 + c->ea_cycles(mode, reg, size); return;
	}
	uint32_t a = 0, v;
	if (mode == 0) v = c->dar[reg] & szmask(size);
	else { a = c->ea_addr(mode, reg, size); v = c->ea_read_addr(mode, reg, size, a); }
	uint32_t r;
	switch (op) {
	case 0: r = (v | imm) & szmask(size); setNZ(c, r, size); break;     // ORI
	case 1: r = (v & imm) & szmask(size); setNZ(c, r, size); break;     // ANDI
	case 2: r = do_sub(c, v, imm, size); break;                       // SUBI
	case 3: r = do_add(c, v, imm, size); break;                       // ADDI
	case 5: r = (v ^ imm) & szmask(size); setNZ(c, r, size); break;     // EORI
	default: op_illegal(c); return;
	}
	if (mode == 0) c->dar[reg] = (c->dar[reg] & ~szmask(size)) | r;
	else { if (size == 0)c->write8(a, r); else if (size == 1)c->write16(a, r); else c->write32(a, r); }
	c->cycles_left -= 8 + c->ea_cycles(mode, reg, size);
}
static bool cond_true(cpu_m68000* c, int cc) {
	bool C = c->flag_c, Z = c->flag_z, N = c->flag_n, V = c->flag_v;
	switch (cc) {
	case 0:return true; case 1:return false;
	case 2:return !C && !Z; case 3:return C || Z; case 4:return !C; case 5:return C;
	case 6:return !Z; case 7:return Z; case 8:return !V; case 9:return V;
	case 10:return !N; case 11:return N; case 12:return N == V; case 13:return N != V;
	case 14:return !Z && (N == V); case 15:return Z || (N != V);
	} return false;
}
static void op_bcc(cpu_m68000* c) {
	uint16_t w = c->ir; int cc = (w >> 8) & 0xF; int32_t disp = (int8_t)(w & 0xff);
	uint32_t base = c->pc;                  // PC already past opcode word
	if ((w & 0xff) == 0) { disp = (int16_t)c->fetch16(); }
	if (cc == 1) { // BSR
		uint32_t ret = c->pc; c->dar[15] -= 4; c->write32(c->dar[15], ret);
		c->pc = base + disp; c->cycles_left -= 18; return;
	}
	if (cond_true(c, cc)) { c->pc = base + disp; c->cycles_left -= 10; }
	else c->cycles_left -= ((w & 0xff) == 0 ? 12 : 8);
}
// 0x5 block: ADDQ/SUBQ (#data 1-8) and (size==3) Scc / DBcc.
static void op_5(cpu_m68000* c) {
	uint16_t w = c->ir; int size = (w >> 6) & 3; int mode = (w >> 3) & 7, reg = w & 7;
	if (size == 3) {
		int cc = (w >> 8) & 0xF;
		if (mode == 1) {                                // DBcc Dn,disp
			uint32_t base = c->pc; int16_t disp = (int16_t)c->fetch16();
			if (cond_true(c, cc)) { c->cycles_left -= 12; return; }   // condition true -> terminate
			uint16_t cnt = (uint16_t)(c->dar[reg] & 0xffff); cnt--;
			c->dar[reg] = (c->dar[reg] & 0xffff0000u) | cnt;
			if (cnt != 0xffff) { c->pc = base + disp; c->cycles_left -= 10; }
			else c->cycles_left -= 14;
			return;
		}
		uint32_t r = cond_true(c, cc) ? 0xFFu : 0x00u;   // Scc <ea> (byte)
		if (mode == 0) c->dar[reg] = (c->dar[reg] & ~0xffu) | r;
		else { uint32_t a = c->ea_addr(mode, reg, 0); c->write8(a, r); }
		c->cycles_left -= (mode == 0 ? 4 : 8 + c->ea_cycles(mode, reg, 0));
		return;
	}
	int data = (w >> 9) & 7; if (data == 0) data = 8;
	bool sub = (w >> 8) & 1;
	if (mode == 1) {                                    // ADDQ/SUBQ #,An : full 32-bit, no flags
		if (sub) c->dar[8 + reg] -= data; else c->dar[8 + reg] += data;
		c->cycles_left -= 8; return;
	}
	uint32_t a = 0, v;
	if (mode == 0) v = c->dar[reg] & szmask(size);
	else { a = c->ea_addr(mode, reg, size); v = c->ea_read_addr(mode, reg, size, a); }
	uint32_t r = sub ? do_sub(c, v, data, size) : do_add(c, v, data, size);
	if (mode == 0) c->dar[reg] = (c->dar[reg] & ~szmask(size)) | (r & szmask(size));
	else { if (size == 0)c->write8(a, r); else if (size == 1)c->write16(a, r); else c->write32(a, r); }
	c->cycles_left -= (mode == 0 ? (size == 2 ? 8 : 4) : ((size == 2 ? 12 : 8) + c->ea_cycles(mode, reg, size)));
}
// 0x7 block: MOVEQ #data8,Dn (sign-extended to 32 bits; N/Z set, V=C=0, X untouched).
static void op_moveq(cpu_m68000* c) {
	uint16_t w = c->ir;
	if (w & 0x0100) { op_illegal(c); return; }          // bit 8 must be 0
	int dn = (w >> 9) & 7; uint32_t v = sext8(w & 0xff);
	c->dar[dn] = v;
	c->flag_n = (v & 0x80000000u) ? 1 : 0; c->flag_z = v ? 0 : 1; c->flag_v = 0; c->flag_c = 0;
	c->cycles_left -= 4;
}
// 0xE block: shifts/rotates. Register form (size 0/1/2) shifts a Dn by an immediate
// (1-8) or Dn-mod-64 count; memory form (size==3) shifts <ea>.w by 1.
static void op_E(cpu_m68000* c) {
	uint16_t w = c->ir; int size = (w >> 6) & 3;
	if (size == 3) {                               // memory shift by 1 (word)
		int type = (w >> 9) & 3; int dir = (w >> 8) & 1; int mode = (w >> 3) & 7, reg = w & 7;
		uint32_t a = c->ea_addr(mode, reg, 1); uint16_t v = (uint16_t)c->read16(a);
		uint16_t r = (uint16_t)do_shift(c, v, 1, 1, type, dir);
		c->write16(a, r);
		c->cycles_left -= 8 + c->ea_cycles(mode, reg, 1); return;
	}
	int dir = (w >> 8) & 1; int useReg = (w >> 5) & 1; int type = (w >> 3) & 3; int reg = w & 7;
	int cnt = useReg ? (int)(c->dar[(w >> 9) & 7] & 63) : (((w >> 9) & 7) == 0 ? 8 : ((w >> 9) & 7));
	uint32_t r = do_shift(c, c->dar[reg], cnt, size, type, dir);
	c->dar[reg] = (c->dar[reg] & ~szmask(size)) | (r & szmask(size));
	c->cycles_left -= (size == 2 ? 8 : 6) + 2 * cnt;
}
static uint32_t comp_negx(cpu_m68000* c, uint32_t v, int s) { return do_subx(c, 0, v, s); }
// MOVEM: register list <-> memory. dir 0=reg->mem, 1=mem->reg. word transfers to regs sign-extend.
static void op_movem(cpu_m68000* c) {
	uint16_t w = c->ir; int dir = (w >> 10) & 1; int size = ((w >> 6) & 1) ? 2 : 1; int mode = (w >> 3) & 7, reg = w & 7;
	uint16_t mask = (uint16_t)c->fetch16(); int inc = (size == 2 ? 4 : 2);
	if (dir == 0) {                                       // registers -> memory
		if (mode == 4) {                                  // -(An): predecrement, reversed mask (bit0=A7)
			uint32_t a = c->dar[8 + reg];
			for (int i = 0; i < 16; i++) if (mask & (1 << i)) { int rn = 15 - i; a -= inc; if (size == 2) c->write32(a, c->dar[rn]); else c->write16(a, c->dar[rn] & 0xffff); }
			c->dar[8 + reg] = a;
		}
		else {
			uint32_t a = c->ea_addr(mode, reg, size);
			for (int i = 0; i < 16; i++) if (mask & (1 << i)) { if (size == 2) c->write32(a, c->dar[i]); else c->write16(a, c->dar[i] & 0xffff); a += inc; }
		}
	}
	else {                                          // memory -> registers
		uint32_t a; bool post = (mode == 3);
		a = post ? c->dar[8 + reg] : c->ea_addr(mode, reg, size);
		for (int i = 0; i < 16; i++) if (mask & (1 << i)) { uint32_t v = (size == 2 ? c->read32(a) : sext16(c->read16(a))); c->dar[i] = v; a += inc; }
		if (post) c->dar[8 + reg] = a;
	}
	c->cycles_left -= 12;
}
static void op_chk(cpu_m68000* c) {
	uint16_t w = c->ir; int dn = (w >> 9) & 7; int mode = (w >> 3) & 7, reg = w & 7;
	int16_t bound = (int16_t)c->ea_read(mode, reg, 1);
	int16_t val = (int16_t)(c->dar[dn] & 0xffff);
	c->cycles_left -= 10 + c->ea_cycles(mode, reg, 1);
	if (val < 0) { c->flag_n = 1; c->flag_z = 0; c->flag_v = 0; c->flag_c = 0; c->take_exception(6); return; }
	if (val > bound) { c->flag_n = 0; c->flag_z = 0; c->flag_v = 0; c->flag_c = 0; c->take_exception(6); return; }
}
// 0x4 block dispatcher (everything not already claimed by NEG/NOT/CLR/TST/LEA/TRAP/RTE/NOP/RTS).
static void op_4(cpu_m68000* c) {
	uint16_t w = c->ir; int mode = (w >> 3) & 7, reg = w & 7;
	switch (w) {
	case 0x4E70: if (!c->s_flag) { c->pc = c->ppc; c->take_exception(8); return; } c->cycles_left -= 132; return; // RESET
	case 0x4E71: c->cycles_left -= 4; return;                                                              // NOP
	case 0x4E72: {
		uint16_t imm = (uint16_t)c->fetch16(); if (!c->s_flag) { c->pc = c->ppc; c->take_exception(8); return; }
		c->set_sr(imm); c->stopped = true; c->cycles_left -= 4; return;
	}                          // STOP
	case 0x4E76: if (c->flag_v) { c->take_exception(7); }
			   else c->cycles_left -= 4; return;                   // TRAPV
	case 0x4E77: {
		uint16_t cc = (uint16_t)c->read16(c->dar[15]); c->dar[15] += 2;                            // RTR
		uint32_t np = c->read32(c->dar[15]); c->dar[15] += 4; c->set_ccr((uint8_t)(cc & 0x1F)); c->pc = np; c->cycles_left -= 20; return;
	}
	case 0x4AFC: op_illegal(c); return;                                                                  // ILLEGAL
	}
	if ((w & 0xFFF8) == 0x4E50) {
		int an = w & 7; int16_t d = (int16_t)c->fetch16();                                    // LINK An,#d
		c->dar[15] -= 4; c->write32(c->dar[15], c->dar[8 + an]); c->dar[8 + an] = c->dar[15]; c->dar[15] += d; c->cycles_left -= 16; return;
	}
	if ((w & 0xFFF8) == 0x4E58) { int an = w & 7; c->dar[15] = c->dar[8 + an]; uint32_t v = c->read32(c->dar[15]); c->dar[15] += 4; c->dar[8 + an] = v; c->cycles_left -= 12; return; } // UNLK (load after SP inc -> correct for A7)
	if ((w & 0xFFF8) == 0x4E60) { if (!c->s_flag) { c->pc = c->ppc; c->take_exception(8); return; } c->sp_other = c->dar[8 + (w & 7)]; c->cycles_left -= 4; return; } // MOVE An,USP
	if ((w & 0xFFF8) == 0x4E68) { if (!c->s_flag) { c->pc = c->ppc; c->take_exception(8); return; } c->dar[8 + (w & 7)] = c->sp_other; c->cycles_left -= 4; return; } // MOVE USP,An
	if ((w & 0xFFC0) == 0x4EC0) { c->pc = c->ea_addr(mode, reg, 0); c->cycles_left -= 8 + c->ea_cycles(mode, reg, 0); return; }                 // JMP
	if ((w & 0xFFC0) == 0x4E80) { uint32_t a = c->ea_addr(mode, reg, 0); c->dar[15] -= 4; c->write32(c->dar[15], c->pc); c->pc = a; c->cycles_left -= 16; return; } // JSR
	if ((w & 0xF1C0) == 0x4180) { op_chk(c); return; }                                                            // CHK <ea>,Dn
	if ((w & 0xFFC0) == 0x40C0) {
		uint16_t sr = c->make_sr();                                                       // MOVE from SR (unprivileged on 68000)
		if (mode == 0) c->dar[reg] = (c->dar[reg] & 0xffff0000u) | sr; else { uint32_t a = c->ea_addr(mode, reg, 1); c->write16(a, sr); }
		c->cycles_left -= (mode == 0 ? 6 : 8 + c->ea_cycles(mode, reg, 1)); return;
	}
	if ((w & 0xFFC0) == 0x44C0) { uint16_t v = (uint16_t)c->ea_read(mode, reg, 1); c->set_ccr((uint8_t)(v & 0x1F)); c->cycles_left -= 12 + c->ea_cycles(mode, reg, 1); return; } // MOVE to CCR
	if ((w & 0xFFC0) == 0x46C0) { if (!c->s_flag) { c->pc = c->ppc; c->take_exception(8); return; } uint16_t v = (uint16_t)c->ea_read(mode, reg, 1); c->set_sr(v); c->cycles_left -= 12 + c->ea_cycles(mode, reg, 1); return; } // MOVE to SR
	if ((w & 0xFF00) == 0x4000 && ((w >> 6) & 3) != 3) { do_unary(c, (w >> 6) & 3, true, comp_negx); return; }                 // NEGX.b/w/l
	if ((w & 0xFFC0) == 0x4800) {                                                                                  // NBCD <ea>.b
		uint32_t a = 0, dst; if (mode == 0) dst = c->dar[reg] & 0xff; else { a = c->ea_addr(mode, reg, 0); dst = c->read8(a); }
		uint32_t res = (0x9a - dst - (c->flag_x ? 1 : 0)) & 0xff;
		if (res != 0x9a) {
			if ((res & 0x0f) == 0x0a) res = (res & 0xf0) + 0x10; res &= 0xff; if (res) c->flag_z = 0; c->flag_c = 1; c->flag_x = 1;
			if (mode == 0) c->dar[reg] = (c->dar[reg] & ~0xffu) | res; else c->write8(a, (uint8_t)res);
		}
		else { c->flag_c = 0; c->flag_x = 0; }
		c->cycles_left -= (mode == 0 ? 6 : 8 + c->ea_cycles(mode, reg, 0)); return;
	}
	if ((w & 0xFFF8) == 0x4840) {
		int r = w & 7; uint32_t v = ((c->dar[r] >> 16) & 0xffff) | ((c->dar[r] << 16) & 0xffff0000u);   // SWAP Dn
		c->dar[r] = v; c->flag_n = (v & 0x80000000u) ? 1 : 0; c->flag_z = v ? 0 : 1; c->flag_v = 0; c->flag_c = 0; c->cycles_left -= 4; return;
	}
	if ((w & 0xFFC0) == 0x4840) { uint32_t a = c->ea_addr(mode, reg, 0); c->dar[15] -= 4; c->write32(c->dar[15], a); c->cycles_left -= 12 + c->ea_cycles(mode, reg, 0); return; } // PEA
	if ((w & 0xFFF8) == 0x4880) {
		int r = w & 7; uint32_t v = sext8(c->dar[r] & 0xff);                                    // EXT.W
		c->dar[r] = (c->dar[r] & 0xffff0000u) | (v & 0xffff); c->flag_n = (v & 0x8000) ? 1 : 0; c->flag_z = (v & 0xffff) ? 0 : 1; c->flag_v = 0; c->flag_c = 0; c->cycles_left -= 4; return;
	}
	if ((w & 0xFFF8) == 0x48C0) {
		int r = w & 7; uint32_t v = sext16(c->dar[r] & 0xffff);                                 // EXT.L
		c->dar[r] = v; c->flag_n = (v & 0x80000000u) ? 1 : 0; c->flag_z = v ? 0 : 1; c->flag_v = 0; c->flag_c = 0; c->cycles_left -= 4; return;
	}
	if ((w & 0xFB80) == 0x4880) { op_movem(c); return; }                                                          // MOVEM
	if ((w & 0xFFC0) == 0x4AC0) {
		if (mode == 0) { uint8_t v = c->dar[reg] & 0xff; c->flag_n = (v & 0x80) ? 1 : 0; c->flag_z = v ? 0 : 1; c->flag_v = 0; c->flag_c = 0; c->dar[reg] = (c->dar[reg] & ~0xffu) | (v | 0x80); c->cycles_left -= 4; } // TAS
		else { uint32_t a = c->ea_addr(mode, reg, 0); uint8_t v = (uint8_t)c->read8(a); c->flag_n = (v & 0x80) ? 1 : 0; c->flag_z = v ? 0 : 1; c->flag_v = 0; c->flag_c = 0; c->write8(a, (uint8_t)(v | 0x80)); c->cycles_left -= 14 + c->ea_cycles(mode, reg, 0); } return;
	}
	op_illegal(c);
}
static void op_nop(cpu_m68000* c) { c->cycles_left -= 4; }
static void op_rts(cpu_m68000* c) { c->pc = c->read32(c->dar[15]); c->dar[15] += 4; c->cycles_left -= 16; }
static void op_illegal(cpu_m68000* c) {
	c->pc = c->ppc;                    // exception stacks the opcode address
	int v = ((c->ir & 0xF000) == 0xA000) ? 10 : ((c->ir & 0xF000) == 0xF000) ? 11 : 4;
	c->take_exception(v);
}
static void op_trap(cpu_m68000* c) { int n = c->ir & 0xF; c->take_exception(32 + n); c->cycles_left -= (38 - 34); }
static void op_rte(cpu_m68000* c) {
	uint16_t sr = (uint16_t)c->read16(c->dar[15]); c->dar[15] += 2;
	uint32_t newpc = c->read32(c->dar[15]);        c->dar[15] += 4;
	c->set_sr(sr); c->pc = newpc; c->cycles_left -= 20;
}