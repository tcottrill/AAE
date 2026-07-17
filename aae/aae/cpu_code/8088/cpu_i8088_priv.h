
#ifndef _i8088_PRIV_H_
#define _i8088_PRIV_H_

#pragma once

/*
  Private implementation macros for the cpu_i8088 core.

  These are the decode/access macros from Fake86's cpu.h / modregrm.h, with the
  ModRM address-mode cache removed. They reference cpu_i8088 member variables
  (regs, segregs, ip, mode, reg, rm, ...) and member functions (read86, getmem*)
  and are therefore ONLY valid inside cpu_i8088 method bodies. This header is
  included by the cpu_i8088 .cpp files exclusively and is deliberately NOT pulled
  into cpu_control.h, so these short macro names cannot collide with other CPU
  cores.

  Original macros: Copyright (C)2010-2013 Mike Chambers (Fake86), GPL v2+.
*/

// ---- word-register indices ----
#define regax 0
#define regcx 1
#define regdx 2
#define regbx 3
#define regsp 4
#define regbp 5
#define regsi 6
#define regdi 7

// ---- segment-register indices ----
#define reges 0
#define regcs 1
#define regss 2
#define regds 3

// ---- byte-register indices (little-endian) ----
#define regal 0
#define regah 1
#define regcl 2
#define regch 3
#define regdl 4
#define regdh 5
#define regbl 6
#define regbh 7

// Enable the 80186/V20-class instructions that Fake86 emulates by default
// (PUSHA/POPA/BOUND/PUSH imm/IMUL imm/INS/OUTS/shift-by-imm8/ENTER/LEAVE).
// A strict 8088 lacks these; leaving them in preserves Fake86 compatibility.
// Set to 0 for strict 8086/8088 behaviour (they then fall through to NOP).
#define I8088_ENABLE_80186_OPS 1

#define StepIP(x)               ip += (x)
#define getmem8(x, y)           read86(segbase(x) + (y))
#define getmem16(x, y)          readw86(segbase(x) + (y))
#define putmem8(x, y, z)        write86(segbase(x) + (y), z)
#define putmem16(x, y, z)       writew86(segbase(x) + (y), z)
#define signext(value)          (int16_t)(int8_t)(value)
#define signext32(value)        (int32_t)(int16_t)(value)
#define getreg16(regid)         regs.wordregs[regid]
#define getreg8(regid)          regs.byteregs[byteregtable[regid]]
#define putreg16(regid, w)      regs.wordregs[regid] = (w)
#define putreg8(regid, w)       regs.byteregs[byteregtable[regid]] = (w)
#define getsegreg(regid)        segregs[regid]
#define putsegreg(regid, w)     segregs[regid] = (w)
#define segbase(x)              ((uint32_t)(x) << 4)

#define makeflagsword() \
	( \
	2 | (uint16_t)cf | ((uint16_t)pf << 2) | ((uint16_t)af << 4) | ((uint16_t)zf << 6) | ((uint16_t)sf << 7) | \
	((uint16_t)tf << 8) | ((uint16_t)ifl << 9) | ((uint16_t)df << 10) | ((uint16_t)of << 11) \
	)

#define decodeflagsword(x) { \
	temp16 = x; \
	cf  = temp16 & 1; \
	pf  = (temp16 >> 2) & 1; \
	af  = (temp16 >> 4) & 1; \
	zf  = (temp16 >> 6) & 1; \
	sf  = (temp16 >> 7) & 1; \
	tf  = (temp16 >> 8) & 1; \
	ifl = (temp16 >> 9) & 1; \
	df  = (temp16 >> 10) & 1; \
	of  = (temp16 >> 11) & 1; \
	}

// ModRM decode (non-cached variant from Fake86 modregrm.h).
#define modregrm() { \
	addrbyte = getmem8(segregs[regcs], ip); \
	StepIP(1); \
	mode = addrbyte >> 6; \
	reg = (addrbyte >> 3) & 7; \
	rm = addrbyte & 7; \
	switch (mode) \
	{ \
	case 0: \
		if (rm == 6) { \
			disp16 = getmem16(segregs[regcs], ip); \
			StepIP(2); \
		} \
		if (((rm == 2) || (rm == 3)) && !segoverride) { \
			useseg = segregs[regss]; \
		} \
		break; \
	case 1: \
		disp16 = signext(getmem8(segregs[regcs], ip)); \
		StepIP(1); \
		if (((rm == 2) || (rm == 3) || (rm == 6)) && !segoverride) { \
			useseg = segregs[regss]; \
		} \
		break; \
	case 2: \
		disp16 = getmem16(segregs[regcs], ip); \
		StepIP(2); \
		if (((rm == 2) || (rm == 3) || (rm == 6)) && !segoverride) { \
			useseg = segregs[regss]; \
		} \
		break; \
	default: \
		disp8 = 0; \
		disp16 = 0; \
	} \
}

#endif
