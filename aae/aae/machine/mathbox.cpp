//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// mathbox.cpp - the Atari Math Box (Battlezone, Red Baron, Tempest): four
// Am2901 bit slices driven by 256 x 24-bit microcode PROMs.
//
// This is a microcode interpreter, not a transcription of the functions.
// It executes Mike Albaugh's real microcode from the PROMs the driver loads
// (Tempest 136002-126..132, Battlezone / Red Baron 036174-01..036180-01 -
// identical contents), so its results are whatever the board computes, bit
// for bit, as far as the documented 2901 behaviour goes (MBUDOC.DOC).
// Ported from the Tempest C port's mathbox.c
// (C:\Source2026\Tempest-Full-Disassembly-and-C-Port\c_src).
//
// A 6502 write to the GO range (offset 0-31) loads the uPC from the mapping
// PROM, presents the byte on the D bus and clocks the ALU until an
// instruction with STALL=1. The model runs that to completion inside the
// write, so the status busy bit always reads 0.
//
// Microcode word (24 bits; MBUCOD.V05 $OUT macro):
//   23-20 A    register A / jump target high nibble
//   19-16 B    register B / jump target low nibble
//   15    I2HI source-select I2 for the high byte slices
//   14    I2LO source-select I2 for the low byte slices
//   13-12 I1,I0
//   11    STALL stop the clock after this instruction
//   10-8  I5-I3 ALU function
//   7     LDAB  jump latch := A,B fields
//   6-4   I8-I6 destination
//   3     SIGN  MSB* = OVR xor F15 (else 0)
//   2     JMP   jump to the latch if MSB* = 0
//   1     MULT  invert I1 if the latched Q0 was 0 (CADD)
//   0     CARIN
//
// PROM to word-bit mapping, verified against the MBUCOD source by the C
// port's tools/gen_roms.py (uPC $20 = $1073B4, uPC $10+n = $0n3B10):
//   127 (036180) bits 3-0     128 (036179) bits 7-4
//   129 (036178) bits 11-8    130 (036177) bits 15-12
//   131 (036176) bits 19-16   132 (036175) bits 23-20
// MAME's ROM tables pair these the other way round; MAME never reads them.
//
// Am2901 semantics (standard datasheet tables):
//   source I2I1I0: 0 A,Q  1 A,B  2 0,Q  3 0,B  4 0,A  5 D,A  6 D,Q  7 D,0
//   function I5-3: 0 R+S  1 S-R  2 R-S  3 R|S  4 R&S  5 ~R&S  6 R^S  7 ~(R^S)
//   dest     I8-6: 0 QREG 1 NOP 2 RAMA 3 RAMF 4 RAMQD 5 RAMD 6 RAMQU 7 RAMU
// D bus = the 6502 data byte on both halves (D7 tied to D15, MBUDOC).
// Carry ripples slice to slice (four 4-bit slices); OVR is the top slice's.
//
// Documentation gaps, chosen and flagged rather than guessed silently:
//   - C4/OVR of EXOR/EXNOR: the datasheet's expressions are not in MBUDOC;
//     modelled as 0 and every SIGN use after them is counted (xor_sign).
//   - Q0 latch when Q0 is not driven (dest 0-3): holds its previous value.
//     The microcode only CADDs after RAMQD, where Q0 is driven.
//   - RAMU shifts in 0 ("garbage from floating input", MBUDOC).
//==========================================================================
#include "mathbox.h"
#include "sys_log.h"

#define MB_STEP_CAP 4096u

static uint8_t       mb_map[32];
static uint32_t      mb_ucode[256];
static bool          mb_loaded = false;
static bool          mb_runaway_logged = false;
static mathbox_state m;

void mathbox_reset()
{
	for (int i = 0; i < 16; i++) m.r[i] = 0;
	m.q = 0; m.y = 0; m.jt = 0; m.q0 = 1;
	m.starts = m.steps = m.runaway = m.xor_sign = 0;
}

bool mathbox_init(const uint8_t* map, const uint8_t* planes)
{
	mb_loaded = false;
	mb_runaway_logged = false;
	if (!map || !planes) {
		LOG_ERROR("Mathbox: PROM regions missing (map %p, microcode %p) - the mapping PROM and the six microcode PROMs must be in the romset", (const void*)map, (const void*)planes);
		return false;
	}
	for (int i = 0; i < 32; i++) mb_map[i] = map[i];
	for (int i = 0; i < 256; i++)
		mb_ucode[i] = ((uint32_t)planes[0x200 + i] << 16) | ((uint32_t)planes[0x100 + i] << 8) | planes[i];
	mb_loaded = true;
	mathbox_reset();
	LOG_INFO("Mathbox: microcode PROMs loaded (uPC $20 = $%06X)", mb_ucode[0x20]);
	return true;
}

const mathbox_state* mathbox_get_state() { return &m; }
uint32_t mathbox_ucode_word(int upc) { return mb_ucode[upc & 0xFF]; }

static void pick(int src, uint16_t a, uint16_t b, uint16_t q, uint16_t d,
                 uint16_t* r, uint16_t* s)
{
	switch (src & 7) {
	case 0: *r = a; *s = q; break;
	case 1: *r = a; *s = b; break;
	case 2: *r = 0; *s = q; break;
	case 3: *r = 0; *s = b; break;
	case 4: *r = 0; *s = a; break;
	case 5: *r = d; *s = a; break;
	case 6: *r = d; *s = q; break;
	default: *r = d; *s = 0; break;
	}
}

// One microinstruction; returns 1 when it carried STALL.
static int mb_step(uint16_t d, uint8_t* upc)
{
	uint32_t w = mb_ucode[*upc];
	int ra_i = (int)((w >> 20) & 15), rb_i = (int)((w >> 16) & 15);
	int i2hi = (int)((w >> 15) & 1), i2lo = (int)((w >> 14) & 1);
	int i10 = (int)((w >> 12) & 3);
	int stall = (int)((w >> 11) & 1), func = (int)((w >> 8) & 7);
	int ldab = (int)((w >> 7) & 1), dest = (int)((w >> 4) & 7);
	int sign = (int)((w >> 3) & 1), jmp = (int)((w >> 2) & 1);
	int mult = (int)((w >> 1) & 1);
	unsigned c = (unsigned)(w & 1);
	uint16_t av = m.r[ra_i], bv = m.r[rb_i];
	uint16_t rh, sh, rl, sl, R, S, F = 0;
	unsigned ovr = 0;

	if (mult && m.q0 == 0) i10 ^= 2;                 // CADD: ADD n,m -> ADD 0,m
	pick((i2hi << 2) | i10, av, bv, m.q, d, &rh, &sh);
	pick((i2lo << 2) | i10, av, bv, m.q, d, &rl, &sl);
	R = (uint16_t)((rh & 0xFF00) | (rl & 0x00FF));
	S = (uint16_t)((sh & 0xFF00) | (sl & 0x00FF));

	for (int k = 0; k < 4; k++) {                      // four 2901 slices
		unsigned r4 = (R >> (4 * k)) & 15u, s4 = (S >> (4 * k)) & 15u, f4, c4;
		switch (func) {
		case 0: case 1: case 2: {
			unsigned rr = (func == 1) ? (~r4 & 15u) : r4;
			unsigned ss = (func == 2) ? (~s4 & 15u) : s4;
			unsigned sum = rr + ss + c;
			unsigned c3 = ((rr & 7u) + (ss & 7u) + c) >> 3;
			f4 = sum & 15u; c4 = sum >> 4; ovr = c3 ^ c4;
			break;
		}
		case 3: f4 = r4 | s4; c4 = (f4 != 15u) | c; ovr = c4; break;
		case 4: f4 = r4 & s4; c4 = (f4 != 0u) | c; ovr = c4; break;
		case 5: f4 = ~r4 & s4 & 15u; c4 = (f4 != 0u) | c; ovr = c4; break;
		case 6: f4 = r4 ^ s4; c4 = 0; ovr = 0; break;
		default: f4 = ~(r4 ^ s4) & 15u; c4 = 0; ovr = 0; break;
		}
		F = (uint16_t)(F | (f4 << (4 * k)));
		c = c4;
	}
	unsigned f15 = (F >> 15) & 1u;
	unsigned msb = sign ? (ovr ^ f15) : 0u;
	if (sign && func >= 6 && (jmp || dest == 4 || dest == 5)) m.xor_sign++;

	m.y = F;
	switch (dest) {
	case 0: m.q = F; break;
	case 1: break;
	case 2: m.r[rb_i] = F; m.y = av; break;
	case 3: m.r[rb_i] = F; break;
	case 4:
		m.r[rb_i] = (uint16_t)((F >> 1) | (msb << 15));
		m.q0 = (uint8_t)(m.q & 1u);
		m.q = (uint16_t)((m.q >> 1) | ((F & 1u) << 15));
		break;
	case 5:
		m.r[rb_i] = (uint16_t)((F >> 1) | (msb << 15));
		m.q0 = (uint8_t)(m.q & 1u);
		break;
	case 6:
		m.r[rb_i] = (uint16_t)((F << 1) | (m.q >> 15));
		m.q = (uint16_t)(m.q << 1);
		m.q0 = 0;
		break;
	default:
		m.r[rb_i] = (uint16_t)(F << 1);
		m.q0 = 0;
		break;
	}

	if (ldab) m.jt = (uint8_t)((w >> 16) & 0xFF);
	if (jmp && msb == 0) *upc = m.jt;
	else *upc = (uint8_t)(*upc + 1);
	m.steps++;
	return stall;
}

static void mb_write(uint8_t offset, uint8_t data)
{
	uint8_t upc = mb_map[offset & 0x1F];
	uint16_t d = (uint16_t)(data | (data << 8));
	unsigned n;
	m.starts++;
	for (n = 0; n < MB_STEP_CAP; n++)
		if (mb_step(d, &upc)) break;
	if (n == MB_STEP_CAP) {
		m.runaway++;
		if (!mb_runaway_logged) {
			// The self test's signature analysis starts every mapping entry,
			// and some never STALL on the board either; not an error.
			LOG_INFO("Mathbox: start at offset $%02X (uPC $%02X) ran %u steps without STALL; further runaways not logged",
				offset & 0x1F, mb_map[offset & 0x1F], MB_STEP_CAP);
			mb_runaway_logged = true;
		}
	}
}

UINT8 MathboxStatusRead(UINT32 address, struct MemoryReadByte* psMemRead)
{
	(void)address; (void)psMemRead;
	return 0x00;   // the write ran to completion: never busy
}

UINT8 MathboxLowbitRead(UINT32 address, struct MemoryReadByte* psMemRead)
{
	(void)address; (void)psMemRead;
	return (UINT8)(m.y & 0xFF);
}

UINT8 MathboxHighbitRead(UINT32 address, struct MemoryReadByte* psMemRead)
{
	(void)address; (void)psMemRead;
	return (UINT8)(m.y >> 8);
}

void MathboxGo(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	(void)psMemWrite;
	if (!mb_loaded) return;
	mb_write((uint8_t)(address & 0x1F), data);
}
