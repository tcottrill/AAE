
// -----------------------------------------------------------------------------
// AAE (Another Arcade Emulator) - 6502 CPU Core
//
// This file is part of the AAE project and is released under The Unlicense.
// You are free to use, modify, and distribute this software without restriction.
// See <http://unlicense.org/> for details.
//
// Origins and Contributions:
//   - Original 6502 CPU core by Neil Bradley, adapted from unnamed web sources.
//   - ADC and SBC algorithms from M.A.M.E.™ (c) 1998–2000 Juergen Buchmueller.
//   - Timing tables and stack logic adapted from FakeNes.
//   - Modern C++ refactor and extensions by TC (2015–2025) for AAE compatibility.
//   - Additional documentation and cleanup assisted by ChatGPT.
//
// This version retains functional and timing accuracy, supports profiling and
// debugging, and integrates mostly with legacy MAME-style emulation systems.
// -----------------------------------------------------------------------------

// Notes
// 11/22/24 added undoucumented isb opcode, will work to add the rest later.
// 12/28/24 Rewrote the main loop to resolve an issue with the cycle counting being consistently undereported.
// 01/03/25 Discovered an edge case where clocktickstotal was not being set to zero at init, causing an immediate crash.
// 01/09/25 Changed clocktickstotal again to be set to zero at init, not reset. When a cpu was reset mid-frame, it was throwing the timing count off.
// 06/29/25 Added IRQ after CLI check, added new dissassembler, code cleanup
// 06/30/25 Added the most commonly used undocumented instructions, and hopefully adjusted the timing table to match. These are totally unverified.
// For your own usage, just undefine USING_AAE_EMU
// 07/01/25 Moved the stack operations back to not using the Memory Handlers. This was a good speed up and did not affect Major Havoc.
// I left the old code commented out, just in case. 
// cpu_6502.cpp

#include <stdio.h>
#include "cpu_6502.h"
#include "sys_log.h"
#include "timer.h"


#define bget(p,m) ((p) & (m))

// -----------------------------------------------------------------------------
// Opcode Mnemonics Table
// Provides string representations for all 256 opcodes. Used by both the
// instruction profiling system (log_instruction_usage) and debugger output
// to identify and label executed or inspected instructions.
// -----------------------------------------------------------------------------

static const char* mnemonics[256] = {
		"BRK","ORA","???","???","TSB","ORA","ASL","???","PHP","ORA","ASL","???","TSB","ORA","ASL","???",
		"BPL","ORA","ORA","???","TRB","ORA","ASL","???","CLC","ORA","INA","???","TRB","ORA","ASL","???",
		"JSR","AND","???","???","BIT","AND","ROL","???","PLP","AND","ROL","???","BIT","AND","ROL","???",
		"BMI","AND","AND","???","BIT","AND","ROL","???","SEC","AND","DEA","???","BIT","AND","ROL","???",
		"RTI","EOR","???","???","???","EOR","LSR","???","PHA","EOR","LSR","???","JMP","EOR","LSR","???",
		"BVC","EOR","EOR","???","???","EOR","LSR","???","CLI","EOR","PHY","???","???","EOR","LSR","???",
		"RTS","ADC","???","???","STZ","ADC","ROR","???","PLA","ADC","ROR","???","JMP","ADC","ROR","???",
		"BVS","ADC","ADC","???","STZ","ADC","ROR","???","SEI","ADC","PLY","???","JMP","ADC","ROR","???",
		"BRA","STA","???","???","STY","STA","STX","???","DEY","BIT","TXA","???","STY","STA","STX","???",
		"BCC","STA","STA","???","STY","STA","STX","???","TYA","STA","TXS","???","STZ","STA","STZ","???",
		"LDY","LDA","LDX","???","LDY","LDA","LDX","???","TAY","LDA","TAX","???","LDY","LDA","LDX","???",
		"BCS","LDA","???","???","LDY","LDA","LDX","???","CLV","LDA","TSX","???","LDY","LDA","LDX","???",
		"CPY","CMP","???","???","CPY","CMP","DEC","???","INY","CMP","DEX","???","CPY","CMP","DEC","???",
		"BNE","CMP","CMP","???","???","CMP","DEC","???","CLD","CMP","PHX","???","???","CMP","DEC","???",
		"CPX","SBC","???","ISB","CPX","SBC","INC","ISB","INX","SBC","NOP","ISB","CPX","SBC","INC","ISB",
		"BEQ","SBC","SBC","ISB","???","SBC","INC","ISB","SED","SBC","PLX","ISB","NOP","SBC","INC","ISB"
};

// -----------------------------------------------------------------------------
// Cycle count table for all 256 opcodes, including common undocumented opcodes
// -----------------------------------------------------------------------------
static const uint32_t ticks[256] = {
	/*        |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |  8  |  9  |  A  |  B  |  C  |  D  |  E  |  F  |     */
	/* 0 */      7,    6,    2,    8,    3,    3,    5,    5,    3,    2,    2,    2,    4,    4,    6,    6,  /* 0 */
	/* 1 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 1 */
	/* 2 */      6,    6,    2,    8,    3,    3,    5,    5,    4,    2,    2,    2,    4,    4,    6,    6,  /* 2 */
	/* 3 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 3 */
	/* 4 */      6,    6,    2,    8,    3,    3,    5,    5,    3,    2,    2,    2,    3,    4,    6,    6,  /* 4 */
	/* 5 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 5 */
	/* 6 */      6,    6,    2,    8,    3,    3,    5,    5,    4,    2,    2,    2,    5,    4,    6,    6,  /* 6 */
	/* 7 */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* 7 */
	/* 8 */      2,    6,    2,    6,    3,    3,    3,    3,    2,    2,    2,    2,    4,    4,    4,    4,  /* 8 */
	/* 9 */      2,    6,    2,    6,    4,    4,    4,    4,    2,    5,    2,    5,    5,    5,    5,    5,  /* 9 */
	/* A */      2,    6,    2,    6,    3,    3,    3,    3,    2,    2,    2,    2,    4,    4,    4,    4,  /* A */
	/* B */      2,    5,    2,    5,    4,    4,    4,    4,    2,    4,    2,    4,    4,    4,    4,    4,  /* B */
	/* C */      2,    6,    2,    8,    3,    3,    5,    5,    2,    2,    2,    2,    4,    4,    6,    6,  /* C */
	/* D */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7,  /* D */
	/* E */      2,    6,    2,    8,    3,    3,    5,    5,    2,    2,    2,    2,    4,    4,    6,    6,  /* E */
	/* F */      2,    5,    2,    8,    4,    4,    6,    6,    2,    4,    2,    7,    4,    4,    7,    7   /* F */
};

// -----------------------------------------------------------------------------
// Opcode Table (Automatically Populated by init6502)
//

const cpu_6502::OpEntry cpu_6502::opcode_table[256] = {
	{ &cpu_6502::brk6502, &cpu_6502::implied6502 },   // 0x00
	{ &cpu_6502::ora6502, &cpu_6502::indx6502    },   // 0x01
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x02
	{ &cpu_6502::slo6502, &cpu_6502::indx6502    },   // 0x03 (UNDOC)
	{ &cpu_6502::tsb6502, &cpu_6502::zp6502      },   // 0x04
	{ &cpu_6502::ora6502, &cpu_6502::zp6502      },   // 0x05
	{ &cpu_6502::asl6502, &cpu_6502::zp6502      },   // 0x06
	{ &cpu_6502::slo6502, &cpu_6502::zp6502      },   // 0x07 (UNDOC)
	{ &cpu_6502::php6502, &cpu_6502::implied6502 },   // 0x08
	{ &cpu_6502::ora6502, &cpu_6502::immediate6502 }, // 0x09
	{ &cpu_6502::asla6502, &cpu_6502::implied6502 },  // 0x0A
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x0B
	{ &cpu_6502::tsb6502, &cpu_6502::abs6502     },   // 0x0C
	{ &cpu_6502::ora6502, &cpu_6502::abs6502     },   // 0x0D
	{ &cpu_6502::asl6502, &cpu_6502::abs6502     },   // 0x0E
	{ &cpu_6502::slo6502, &cpu_6502::abs6502     },   // 0x0F (UNDOC)
	{ &cpu_6502::bpl6502, &cpu_6502::relative6502 },  // 0x10
	{ &cpu_6502::ora6502, &cpu_6502::indy6502    },   // 0x11
	{ &cpu_6502::ora6502, &cpu_6502::indzp6502   },   // 0x12
	{ &cpu_6502::slo6502, &cpu_6502::indzp6502   },   // 0x13 (UNDOC)
	{ &cpu_6502::trb6502, &cpu_6502::zp6502      },   // 0x14
	{ &cpu_6502::ora6502, &cpu_6502::zpx6502     },   // 0x15
	{ &cpu_6502::asl6502, &cpu_6502::zpx6502     },   // 0x16
	{ &cpu_6502::slo6502, &cpu_6502::zpx6502     },   // 0x17 (UNDOC)
	{ &cpu_6502::clc6502, &cpu_6502::implied6502 },   // 0x18
	{ &cpu_6502::ora6502, &cpu_6502::absy6502    },   // 0x19
	{ &cpu_6502::ina6502, &cpu_6502::implied6502 },   // 0x1A
	{ &cpu_6502::slo6502, &cpu_6502::absy6502    },   // 0x1B (UNDOC)
	{ &cpu_6502::trb6502, &cpu_6502::abs6502     },   // 0x1C
	{ &cpu_6502::ora6502, &cpu_6502::absx6502    },   // 0x1D
	{ &cpu_6502::asl6502, &cpu_6502::absx6502    },   // 0x1E
	{ &cpu_6502::slo6502, &cpu_6502::absx6502    },   // 0x1F (UNDOC)
	{ &cpu_6502::jsr6502, &cpu_6502::abs6502     },   // 0x20
	{ &cpu_6502::and6502, &cpu_6502::indx6502    },   // 0x21
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x22
	{ &cpu_6502::rla6502, &cpu_6502::indx6502    },   // 0x23 (UNDOC, optional)
	{ &cpu_6502::bit6502, &cpu_6502::zp6502      },   // 0x24
	{ &cpu_6502::and6502, &cpu_6502::zp6502      },   // 0x25
	{ &cpu_6502::rol6502, &cpu_6502::zp6502      },   // 0x26
	{ &cpu_6502::rla6502, &cpu_6502::zp6502      },   // 0x27 (UNDOC, optional)
	{ &cpu_6502::plp6502, &cpu_6502::implied6502 },   // 0x28
	{ &cpu_6502::and6502, &cpu_6502::immediate6502 }, // 0x29
	{ &cpu_6502::rola6502, &cpu_6502::implied6502 },  // 0x2A
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x2B
	{ &cpu_6502::bit6502, &cpu_6502::abs6502     },   // 0x2C
	{ &cpu_6502::and6502, &cpu_6502::abs6502     },   // 0x2D
	{ &cpu_6502::rol6502, &cpu_6502::abs6502     },   // 0x2E
	{ &cpu_6502::rla6502, &cpu_6502::abs6502     },   // 0x2F (UNDOC, optional)
	{ &cpu_6502::bmi6502, &cpu_6502::relative6502 },  // 0x30
	{ &cpu_6502::and6502, &cpu_6502::indy6502    },   // 0x31
	{ &cpu_6502::and6502, &cpu_6502::indzp6502   },   // 0x32
	{ &cpu_6502::rla6502, &cpu_6502::indzp6502   },   // 0x33 (UNDOC, optional)
	{ &cpu_6502::bit6502, &cpu_6502::zpx6502     },   // 0x34
	{ &cpu_6502::and6502, &cpu_6502::zpx6502     },   // 0x35
	{ &cpu_6502::rol6502, &cpu_6502::zpx6502     },   // 0x36
	{ &cpu_6502::rla6502, &cpu_6502::zpx6502     },   // 0x37 (UNDOC, optional)
	{ &cpu_6502::sec6502, &cpu_6502::implied6502 },   // 0x38
	{ &cpu_6502::and6502, &cpu_6502::absy6502    },   // 0x39
	{ &cpu_6502::dea6502, &cpu_6502::implied6502 },   // 0x3A
	{ &cpu_6502::rla6502, &cpu_6502::absy6502    },   // 0x3B (UNDOC, optional)
	{ &cpu_6502::bit6502, &cpu_6502::absx6502    },   // 0x3C
	{ &cpu_6502::and6502, &cpu_6502::absx6502    },   // 0x3D
	{ &cpu_6502::rol6502, &cpu_6502::absx6502    },   // 0x3E
	{ &cpu_6502::rla6502, &cpu_6502::absx6502    },   // 0x3F (UNDOC, optional)
	{ &cpu_6502::rti6502, &cpu_6502::implied6502 },   // 0x40
	{ &cpu_6502::eor6502, &cpu_6502::indx6502    },   // 0x41
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x42
	{ &cpu_6502::sre6502, &cpu_6502::indx6502    },   // 0x43 (UNDOC)
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x44
	{ &cpu_6502::eor6502, &cpu_6502::zp6502      },   // 0x45
	{ &cpu_6502::lsr6502, &cpu_6502::zp6502      },   // 0x46
	{ &cpu_6502::sre6502, &cpu_6502::zp6502      },   // 0x47 (UNDOC)
	{ &cpu_6502::pha6502, &cpu_6502::implied6502 },   // 0x48
	{ &cpu_6502::eor6502, &cpu_6502::immediate6502 }, // 0x49
	{ &cpu_6502::lsra6502, &cpu_6502::implied6502 },  // 0x4A
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x4B
	{ &cpu_6502::jmp6502, &cpu_6502::abs6502     },   // 0x4C
	{ &cpu_6502::eor6502, &cpu_6502::abs6502     },   // 0x4D
	{ &cpu_6502::lsr6502, &cpu_6502::abs6502     },   // 0x4E
	{ &cpu_6502::sre6502, &cpu_6502::abs6502     },   // 0x4F (UNDOC)
	{ &cpu_6502::bvc6502, &cpu_6502::relative6502 },  // 0x50
	{ &cpu_6502::eor6502, &cpu_6502::indy6502    },   // 0x51
	{ &cpu_6502::eor6502, &cpu_6502::indzp6502   },   // 0x52
	{ &cpu_6502::sre6502, &cpu_6502::indzp6502   },   // 0x53 (UNDOC)
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x54
	{ &cpu_6502::eor6502, &cpu_6502::zpx6502     },   // 0x55
	{ &cpu_6502::lsr6502, &cpu_6502::zpx6502     },   // 0x56
	{ &cpu_6502::sre6502, &cpu_6502::zpx6502     },   // 0x57 (UNDOC)
	{ &cpu_6502::cli6502, &cpu_6502::implied6502 },   // 0x58
	{ &cpu_6502::eor6502, &cpu_6502::absy6502    },   // 0x59
	{ &cpu_6502::phy6502, &cpu_6502::implied6502 },   // 0x5A
	{ &cpu_6502::sre6502, &cpu_6502::absy6502    },   // 0x5B (UNDOC)
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x5C
	{ &cpu_6502::eor6502, &cpu_6502::absx6502    },   // 0x5D
	{ &cpu_6502::lsr6502, &cpu_6502::absx6502    },   // 0x5E
	{ &cpu_6502::sre6502, &cpu_6502::absx6502    },   // 0x5F (UNDOC)
	{ &cpu_6502::rts6502, &cpu_6502::implied6502 },   // 0x60
	{ &cpu_6502::adc6502, &cpu_6502::indx6502    },   // 0x61
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x62
	{ &cpu_6502::rra6502, &cpu_6502::indx6502    },   // 0x63 (UNDOC)
	{ &cpu_6502::stz6502, &cpu_6502::zp6502      },   // 0x64
	{ &cpu_6502::adc6502, &cpu_6502::zp6502      },   // 0x65
	{ &cpu_6502::ror6502, &cpu_6502::zp6502      },   // 0x66
	{ &cpu_6502::rra6502, &cpu_6502::zp6502      },   // 0x67 (UNDOC)
	{ &cpu_6502::pla6502, &cpu_6502::implied6502 },   // 0x68
	{ &cpu_6502::adc6502, &cpu_6502::immediate6502 }, // 0x69
	{ &cpu_6502::rora6502, &cpu_6502::implied6502 },  // 0x6A
	{ &cpu_6502::rra6502, &cpu_6502::implied6502 },   // 0x6B (UNDOC)
	{ &cpu_6502::jmp6502, &cpu_6502::indirect6502 },  // 0x6C
	{ &cpu_6502::adc6502, &cpu_6502::abs6502     },   // 0x6D
	{ &cpu_6502::ror6502, &cpu_6502::abs6502     },   // 0x6E
	{ &cpu_6502::rra6502, &cpu_6502::abs6502     },   // 0x6F (UNDOC)
	{ &cpu_6502::bvs6502, &cpu_6502::relative6502 },  // 0x70
	{ &cpu_6502::adc6502, &cpu_6502::indy6502    },   // 0x71
	{ &cpu_6502::adc6502, &cpu_6502::indzp6502   },   // 0x72
	{ &cpu_6502::rra6502, &cpu_6502::indy6502    },   // 0x73 (UNDOC)
	{ &cpu_6502::stz6502, &cpu_6502::zpx6502     },   // 0x74
	{ &cpu_6502::adc6502, &cpu_6502::zpx6502     },   // 0x75
	{ &cpu_6502::ror6502, &cpu_6502::zpx6502     },   // 0x76
	{ &cpu_6502::rra6502, &cpu_6502::zpx6502     },   // 0x77 (UNDOC)
	{ &cpu_6502::sei6502, &cpu_6502::implied6502 },   // 0x78
	{ &cpu_6502::adc6502, &cpu_6502::absy6502    },   // 0x79
	{ &cpu_6502::ply6502, &cpu_6502::implied6502 },   // 0x7A
	{ &cpu_6502::rra6502, &cpu_6502::absy6502    },   // 0x7B (UNDOC)
	{ &cpu_6502::jmp6502, &cpu_6502::indabsx6502 },   // 0x7C
	{ &cpu_6502::adc6502, &cpu_6502::absx6502    },   // 0x7D
	{ &cpu_6502::ror6502, &cpu_6502::absx6502    },   // 0x7E
	{ &cpu_6502::rra6502, &cpu_6502::absx6502    },   // 0x7F (UNDOC)
	{ &cpu_6502::bra6502, &cpu_6502::relative6502 },  // 0x80
	{ &cpu_6502::sta6502, &cpu_6502::indx6502 },	  // 0x81
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x82
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x83
	{ &cpu_6502::sty6502, &cpu_6502::zp6502 },		  // 0x84
	{ &cpu_6502::sta6502, &cpu_6502::zp6502 },		  // 0x85
	{ &cpu_6502::stx6502, &cpu_6502::zp6502 },		  // 0x86
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x87
	{ &cpu_6502::dey6502, &cpu_6502::implied6502 },   // 0x88
	{ &cpu_6502::bit6502, &cpu_6502::immediate6502 }, // 0x89
	{ &cpu_6502::txa6502, &cpu_6502::implied6502 },   // 0x8A
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x8B
	{ &cpu_6502::sty6502, &cpu_6502::abs6502 },       // 0x8C
	{ &cpu_6502::sta6502, &cpu_6502::abs6502 },       // 0x8D
	{ &cpu_6502::stx6502, &cpu_6502::abs6502 },       // 0x8E
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x8F
	{ &cpu_6502::bcc6502, &cpu_6502::relative6502 },  // 0x90
	{ &cpu_6502::sta6502, &cpu_6502::indy6502 },      // 0x91
	{ &cpu_6502::sta6502, &cpu_6502::indzp6502 },	  // 0x92
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x93
	{ &cpu_6502::sty6502, &cpu_6502::zpx6502 },		  // 0x94
	{ &cpu_6502::sta6502, &cpu_6502::zpx6502 },		  // 0x95
	{ &cpu_6502::stx6502, &cpu_6502::zpy6502 },		  // 0x96
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x97
	{ &cpu_6502::tya6502, &cpu_6502::implied6502 },   // 0x98
	{ &cpu_6502::sta6502, &cpu_6502::absy6502 },	  // 0x99
	{ &cpu_6502::txs6502, &cpu_6502::implied6502 },   // 0x9A
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x9B
	{ &cpu_6502::stz6502, &cpu_6502::abs6502 },		  // 0x9C
	{ &cpu_6502::sta6502, &cpu_6502::absx6502 },	  // 0x9D
	{ &cpu_6502::stz6502, &cpu_6502::absx6502 },	  // 0x9E
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x9F
	{ &cpu_6502::ldy6502, &cpu_6502::immediate6502 }, // 0xA0
	{ &cpu_6502::lda6502, &cpu_6502::indx6502 },      // 0xA1
	{ &cpu_6502::ldx6502, &cpu_6502::immediate6502 }, // 0xA2
	{ &cpu_6502::lax6502, &cpu_6502::indx6502 },      // 0xA3 (UNDOC)
	{ &cpu_6502::ldy6502, &cpu_6502::zp6502 },        // 0xA4
	{ &cpu_6502::lda6502, &cpu_6502::zp6502 },        // 0xA5
	{ &cpu_6502::ldx6502, &cpu_6502::zp6502 },        // 0xA6
	{ &cpu_6502::lax6502, &cpu_6502::zp6502 },        // 0xA7 (UNDOC)
	{ &cpu_6502::tay6502, &cpu_6502::implied6502 },   // 0xA8
	{ &cpu_6502::lda6502, &cpu_6502::immediate6502 }, // 0xA9
	{ &cpu_6502::tax6502, &cpu_6502::implied6502 },   // 0xAA
	{ &cpu_6502::lax6502, &cpu_6502::immediate6502 }, // 0xAB (UNDOC)
	{ &cpu_6502::ldy6502, &cpu_6502::abs6502 },       // 0xAC
	{ &cpu_6502::lda6502, &cpu_6502::abs6502 },       // 0xAD
	{ &cpu_6502::ldx6502, &cpu_6502::abs6502 },       // 0xAE
	{ &cpu_6502::lax6502, &cpu_6502::abs6502 },       // 0xAF (UNDOC)
	{ &cpu_6502::bcs6502, &cpu_6502::relative6502 },  // 0xB0
	{ &cpu_6502::lda6502, &cpu_6502::indy6502 },      // 0xB1
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xB2
	{ &cpu_6502::lax6502, &cpu_6502::indy6502 },      // 0xB3 (UNDOC)
	{ &cpu_6502::ldy6502, &cpu_6502::zpx6502 },       // 0xB4
	{ &cpu_6502::lda6502, &cpu_6502::zpx6502 },       // 0xB5
	{ &cpu_6502::ldx6502, &cpu_6502::zpy6502 },       // 0xB6
	{ &cpu_6502::lax6502, &cpu_6502::zpy6502 },       // 0xB7 (UNDOC)
	{ &cpu_6502::clv6502, &cpu_6502::implied6502 },   // 0xB8
	{ &cpu_6502::lda6502, &cpu_6502::absy6502 },      // 0xB9
	{ &cpu_6502::tsx6502, &cpu_6502::implied6502 },   // 0xBA
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xBB
	{ &cpu_6502::ldy6502, &cpu_6502::absx6502 },      // 0xBC
	{ &cpu_6502::lda6502, &cpu_6502::absx6502 },      // 0xBD
	{ &cpu_6502::ldx6502, &cpu_6502::absy6502 },      // 0xBE
	{ &cpu_6502::lax6502, &cpu_6502::absy6502 },      // 0xBF (UNDOC)
	{ &cpu_6502::cpy6502, &cpu_6502::immediate6502 }, // 0xC0
	{ &cpu_6502::cmp6502, &cpu_6502::indx6502 },      // 0xC1
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xC2
	{ &cpu_6502::dcp6502, &cpu_6502::indx6502 },      // 0xC3 (UNDOC)
	{ &cpu_6502::cpy6502, &cpu_6502::zp6502 },        // 0xC4
	{ &cpu_6502::cmp6502, &cpu_6502::zp6502 },        // 0xC5
	{ &cpu_6502::dec6502, &cpu_6502::zp6502 },        // 0xC6
	{ &cpu_6502::dcp6502, &cpu_6502::zp6502 },        // 0xC7 (UNDOC)
	{ &cpu_6502::iny6502, &cpu_6502::implied6502 },   // 0xC8
	{ &cpu_6502::cmp6502, &cpu_6502::immediate6502 }, // 0xC9
	{ &cpu_6502::dex6502, &cpu_6502::implied6502 },   // 0xCA
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xCB
	{ &cpu_6502::cpy6502, &cpu_6502::abs6502 },		  // 0xCC
	{ &cpu_6502::cmp6502, &cpu_6502::abs6502 },		  // 0xCD
	{ &cpu_6502::dec6502, &cpu_6502::abs6502 },		  // 0xCE
	{ &cpu_6502::dcp6502, &cpu_6502::abs6502 },		  // 0xCF (UNDOC)
	{ &cpu_6502::bne6502, &cpu_6502::relative6502 },  // 0xD0
	{ &cpu_6502::cmp6502, &cpu_6502::indy6502 },	  // 0xD1
	{ &cpu_6502::cmp6502, &cpu_6502::indzp6502 },	  // 0xD2
	{ &cpu_6502::dcp6502, &cpu_6502::indzp6502 },	  // 0xD3 (UNDOC)
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xD4
	{ &cpu_6502::cmp6502, &cpu_6502::zpx6502 },		  // 0xD5
	{ &cpu_6502::dec6502, &cpu_6502::zpx6502 },		  // 0xD6
	{ &cpu_6502::dcp6502, &cpu_6502::zpx6502 },		  // 0xD7 (UNDOC)
	{ &cpu_6502::cld6502, &cpu_6502::implied6502 },   // 0xD8
	{ &cpu_6502::cmp6502, &cpu_6502::absy6502 },	  // 0xD9
	{ &cpu_6502::phx6502, &cpu_6502::implied6502 },	  // 0xDA
	{ &cpu_6502::dcp6502, &cpu_6502::absy6502 },	  // 0xDB (UNDOC)
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xDC
	{ &cpu_6502::cmp6502, &cpu_6502::absx6502 },	  // 0xDD
	{ &cpu_6502::dec6502, &cpu_6502::absx6502 },	  // 0xDE
	{ &cpu_6502::dcp6502, &cpu_6502::absx6502 },	  // 0xDF (UNDOC)
	{ &cpu_6502::cpx6502, &cpu_6502::immediate6502 }, // 0xE0
	{ &cpu_6502::sbc6502, &cpu_6502::indx6502 },	  // 0xE1
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xE2
	{ &cpu_6502::isc6502, &cpu_6502::indx6502 },	  // 0xE3 (UNDOC)
	{ &cpu_6502::cpx6502, &cpu_6502::zp6502 },		  // 0xE4
	{ &cpu_6502::sbc6502, &cpu_6502::zp6502 },		  // 0xE5
	{ &cpu_6502::inc6502, &cpu_6502::zp6502 },		  // 0xE6
	{ &cpu_6502::isc6502, &cpu_6502::zp6502 },		  // 0xE7 (UNDOC)
	{ &cpu_6502::inx6502, &cpu_6502::implied6502 },   // 0xE8
	{ &cpu_6502::sbc6502, &cpu_6502::immediate6502 }, // 0xE9
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xEA
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xEB
	{ &cpu_6502::cpx6502, &cpu_6502::abs6502 },		  // 0xEC
	{ &cpu_6502::sbc6502, &cpu_6502::abs6502 },		  // 0xED
	{ &cpu_6502::inc6502, &cpu_6502::abs6502 },		  // 0xEE
	{ &cpu_6502::isc6502, &cpu_6502::abs6502 },		  // 0xEF (UNDOC)
	{ &cpu_6502::beq6502, &cpu_6502::relative6502 },  // 0xF0
	{ &cpu_6502::sbc6502, &cpu_6502::indy6502 },	  // 0xF1
	{ &cpu_6502::sbc6502, &cpu_6502::indzp6502 },	  // 0xF2
	{ &cpu_6502::isc6502, &cpu_6502::indzp6502 },	  // 0xF3 (UNDOC)
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xF4
	{ &cpu_6502::sbc6502, &cpu_6502::zpx6502 },		  // 0xF5
	{ &cpu_6502::inc6502, &cpu_6502::zpx6502 },       // 0xF6
	{ &cpu_6502::isc6502, &cpu_6502::zpx6502 },		  // 0xF7 (UNDOC)
	{ &cpu_6502::sed6502, &cpu_6502::implied6502 },   // 0xF8
	{ &cpu_6502::sbc6502, &cpu_6502::absy6502 },      // 0xF9
	{ &cpu_6502::plx6502, &cpu_6502::implied6502 },   // 0xFA
	{ &cpu_6502::isc6502, &cpu_6502::absy6502 },      // 0xFB (UNDOC)
	{ &cpu_6502::nop6502, &cpu_6502::absx6502 },      // 0xFC
	{ &cpu_6502::sbc6502, &cpu_6502::absx6502 },      // 0xFD
	{ &cpu_6502::inc6502, &cpu_6502::absx6502 },      // 0xFE
	{ &cpu_6502::isc6502, &cpu_6502::absx6502 }       // 0xFF (UNDOC)
};

// -----------------------------------------------------------------------------
// Name: init6502
// Purpose: Initializes the 6502 CPU instance and sets up opcode dispatch tables.
//
// Description:
//   - Clears internal CPU state and registers.
//   - Resets the program counter, status flags, and clock counters.
//   - Populates the opcode table for all 256 opcodes with function pointers
//     to their instruction and addressing mode handlers.
//   - Sets the address mask used for address wrapping in emulated memory space.
//
// Parameters:
//   uint16_t addrmaskval - Bitmask used to constrain all memory addresses
//                          (e.g., 0xFFFF for 16-bit addressing).
//
// Returns:
//   void
//
// -----------------------------------------------------------------------------

void cpu_6502::init6502(uint16_t addrmaskval)
{
	PC = PPC = 0;
	addrmask = addrmaskval;
	_irqPending = 0;
	clocktickstotal = 0;

	for (int i = 0; i < 256; ++i)
	{
		instruction[i] = opcode_table[i].instruction;     // Assign opcode handlers
		adrmode[i] = opcode_table[i].addressing_mode;     // Assign addressing mode handlers
	}
}

// -----------------------------------------------------------------------------
// 6502 CPU Constructor.
// Initializes memory pointers and configuration, sets CPU ID, enables debug
// memory logging, disables execution debugging, and calls init6502().
// Parameters:
//   mem        - Pointer to backing memory array (used when no handler matches).
//   read_mem   - Pointer to memory read handler table.
//   write_mem  - Pointer to memory write handler table.
//   addr       - Address mask used for wrapping memory accesses.
//   num        - CPU identifier used for per-CPU operations/logging.
// -----------------------------------------------------------------------------
cpu_6502::cpu_6502(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, uint16_t addr, int num)
{
	MEM = mem;
	memory_write = write_mem;
	memory_read = read_mem;
	cpu_num = num;
	log_debug_rw = 1;
	mmem = false;
	debug = 0;
	init6502(addr);
}

// -----------------------------------------------------------------------------
// Get the Total Number of Clock Ticks Executed.
// Optionally resets the internal tick counter after returning the value.
// Parameters:
//   reset - If non-zero, resets the tick counter to 0 after retrieval.
// Returns:
//   The total number of ticks accumulated since the last reset (or start).
// -----------------------------------------------------------------------------
int cpu_6502::get6502ticks(int reset)
{
	int tmp = clocktickstotal;
	if (reset)
		clocktickstotal = 0;
	return tmp;
}

// -----------------------------------------------------------------------------
// Read a Byte from 6502 Memory.
// Checks all registered memory read handlers for a matching address range.
// If a handler is found, uses it to read from the mapped area.
// If no handler exists and `mmem` is false, returns from default `MEM` array.
// If no handler exists and `mmem` is true, optionally logs unhandled access.
// Parameters:
//   addr - 16-bit address to read.
// Returns:
//   8-bit value at the specified address.
// -----------------------------------------------------------------------------

uint8_t cpu_6502::get6502memory(uint16_t addr)
{
	MemoryReadByte* reader = memory_read;
	while (reader->lowAddr != -1)
	{
		if (addr >= reader->lowAddr && addr <= reader->highAddr)
		{
			if (reader->memoryCall)
				return reader->memoryCall(addr - reader->lowAddr, reader);
			else
				// Please note this is MAME style memory addressing, where the address returned is relative to the calling address. 
				return ((const uint8_t*)reader->pUserArea)[addr - reader->lowAddr];
		}
		++reader;
	}

	if (!mmem)	
		return MEM[addr];

	if (log_debug_rw)
		LOG_INFO("Warning! Unhandled Read at %x", addr);

	return 0;
}// -----------------------------------------------------------------------------
// Write a Byte to 6502 Memory.
// Checks all registered memory write handlers for a matching address range.
// If a handler is found, uses it to write to the mapped area.
// If no handler exists and `mmem` is false, writes to default `MEM` array.
// If no handler exists and `mmem` is true, optionally logs unhandled access.
// Parameters:
//   addr - 16-bit address to write to.
//   byte - 8-bit value to write.
// -----------------------------------------------------------------------------

void cpu_6502::put6502memory(uint16_t addr, uint8_t byte)
{
	MemoryWriteByte* writer = memory_write;
	while (writer->lowAddr != -1)
	{
		if (addr >= writer->lowAddr && addr <= writer->highAddr)
		{
			if (writer->memoryCall)
				writer->memoryCall(addr - writer->lowAddr, byte, writer);
			else
				((uint8_t*)writer->pUserArea)[addr - writer->lowAddr] = byte; 
			// Please note this is MAME style memory addressing, where the address written to is relative to the calling address. 
			return;
		}
		++writer;
	}

	if (!mmem) {
		MEM[addr] = byte; return;
	}
	
	 if (log_debug_rw)
		LOG_INFO("Warning! Unhandled Write %02X at %x", byte, addr);
}


// -----------------------------------------------------------------------------
// Callback Hook After CLI Instruction.
// Called immediately after clearing the interrupt disable flag (CLI).
// Delegates to maybe_take_irq() to decide if IRQ should be taken now.
// -----------------------------------------------------------------------------
void cpu_6502::check_interrupts_after_cli()
{
	maybe_take_irq();
}

// -----------------------------------------------------------------------------
// Check Whether a Pending IRQ Should Be Taken Now.
// Takes the IRQ if `_irqPending` is set and the interrupt disable flag is clear.
// -----------------------------------------------------------------------------
void cpu_6502::maybe_take_irq()
{
	if (_irqPending && !(P & F_I))
		irq6502();
}

// -----------------------------------------------------------------------------
// Clear the Pending IRQ Flag.
// Resets `_irqPending` to 0.
// -----------------------------------------------------------------------------
void cpu_6502::m6502clearpendingint()
{
	_irqPending = 0;
}

// -----------------------------------------------------------------------------
// Get the value of a CPU register by register index.
// Valid register IDs: M6502_S, M6502_P, M6502_A, M6502_X, M6502_Y
// Returns: 8-bit register value; returns 0 if regnum is invalid.
// -----------------------------------------------------------------------------
uint8_t cpu_6502::m6502_get_reg(int regnum)
{
	switch (regnum)
	{
	case M6502_S: return S;
	case M6502_P: return P;
	case M6502_A: return A;
	case M6502_X: return X;
	case M6502_Y: return Y;
	default: return 0;
	}
}

// -----------------------------------------------------------------------------
// Set the value of a CPU register by register index.
// Valid register IDs: M6502_S, M6502_P, M6502_A, M6502_X, M6502_Y
// If regnum is invalid, function does nothing.
// -----------------------------------------------------------------------------
void cpu_6502::m6502_set_reg(int regnum, uint8_t val)
{
	switch (regnum)
	{
	case M6502_S: S = val; break;
	case M6502_P: P = val; break;
	case M6502_A: A = val; break;
	case M6502_X: X = val; break;
	case M6502_Y: Y = val; break;
	default: break;
	}
}

// -----------------------------------------------------------------------------
// Get the current value of the program counter (PC).
// Returns: 16-bit address of the current instruction.
// -----------------------------------------------------------------------------
uint16_t cpu_6502::get_pc()
{
	return PC;
}

// -----------------------------------------------------------------------------
// Get the previous value of the program counter (PPC).
// PPC is set to the PC value before the current instruction executes.
// Returns: 16-bit address of the last instruction.
// -----------------------------------------------------------------------------
uint16_t cpu_6502::get_ppc()
{
	return PPC;
}

// -----------------------------------------------------------------------------
// Set the program counter (PC) to a new address.
// pc: New 16-bit address to assign to the PC.
// -----------------------------------------------------------------------------
void cpu_6502::set_pc(uint16_t pc)
{
	PC = pc;
}

// -----------------------------------------------------------------------------
// Reset the CPU to its initial state.
// Clears A, X, Y. Sets P with F_T | F_I | F_Z. Stack pointer to 0xFF.
// Loads PC from the reset vector at $FFFC/$FFFD.
// Adds 6 cycles to clockticks6502.
// -----------------------------------------------------------------------------
void cpu_6502::reset6502()
{
	A = X = Y = 0;
	P = F_T | F_I | F_Z;
	_irqPending = 0;
	S = 0xFF;
	PC = get6502memory(0xFFFC & addrmask);
	PC |= get6502memory(0xFFFD & addrmask) << 8;
	if (debug)
		LOG_INFO("reset: PC is %X", PC);
	clockticks6502 += 6;
}

// -----------------------------------------------------------------------------
// Execute a standard IRQ if pending and interrupts are enabled.
// Pushes PC and P (without F_B), sets F_I, loads PC from $FFFE/$FFFF.
// Adds 7 cycles to clockticks6502 and clocktickstotal.
// -----------------------------------------------------------------------------
void cpu_6502::irq6502()
{
	_irqPending = 1;
	if (!(P & F_I))
	{
		push16(PC);
		push8(P & ~F_B);
		P |= F_I;
		PC = get6502memory(0xFFFE & addrmask);
		PC |= get6502memory(0xFFFF & addrmask) << 8;
		_irqPending = 0;
		clockticks6502 += 7;
		clocktickstotal += 7;
	}
}

// -----------------------------------------------------------------------------
// Execute a non-maskable interrupt (NMI).
// Always pushes PC and P (without F_B), sets F_I, loads PC from $FFFA/$FFFB.
// Adds 7 cycles to clockticks6502 and clocktickstotal.
// -----------------------------------------------------------------------------
void cpu_6502::nmi6502()
{
	push16(PC);
	push8(P & ~F_B);
	P |= F_I;
	PC = get6502memory(0xFFFA & addrmask);
	PC |= get6502memory(0xFFFB & addrmask) << 8;
	clockticks6502 += 7;
	clocktickstotal += 7;
}

// -----------------------------------------------------------------------------
// Push a 16-bit value to the 6502 stack (high byte first).
// Stack is located at $0100. Stack pointer S is post-decremented.
// val: 16-bit value to push.
// -----------------------------------------------------------------------------
void cpu_6502::push16(uint16_t val)
{
	//put6502memory(BASE_STACK + S, (val >> 8) & 0xFF);
	//put6502memory(BASE_STACK + ((S - 1) & 0xFF), val & 0xFF);
	MEM[BASE_STACK + S] = (uint8_t)((val >> 8) & 0xFF);
	MEM[BASE_STACK + ((S - 1) & 0xFF)] = (uint8_t)(val & 0xFF);
	S -= 2;
}

// -----------------------------------------------------------------------------
// Push an 8-bit value to the 6502 stack.
// Stack pointer S is post-decremented.
// val: 8-bit value to push.
// -----------------------------------------------------------------------------
void cpu_6502::push8(uint8_t val)
{
	//put6502memory(BASE_STACK + S--, val);
	MEM[BASE_STACK + S--] = val;
}

// -----------------------------------------------------------------------------
// Pull a 16-bit value from the 6502 stack (low byte first).
// Stack pointer S is pre-incremented twice.
// Returns: 16-bit value popped from the stack.
// -----------------------------------------------------------------------------
uint16_t cpu_6502::pull16()
{
	//uint16_t val = get6502memory(BASE_STACK + ((S + 1) & 0xFF)) |
		//(static_cast<uint16_t>(get6502memory(BASE_STACK + ((S + 2) & 0xFF))) << 8);
	uint16_t val = MEM[BASE_STACK + ((S + 1) & 0xFF)] |
		((uint16_t)MEM[BASE_STACK + ((S + 2) & 0xFF)] << 8);
	S += 2;
	return val;
}

// -----------------------------------------------------------------------------
// Pull an 8-bit value from the 6502 stack.
// Stack pointer S is pre-incremented.
// Returns: 8-bit value popped from the stack.
// -----------------------------------------------------------------------------
uint8_t cpu_6502::pull8()
{
	//return get6502memory(BASE_STACK + ++S);
	return MEM[BASE_STACK + ++S];
}

// -----------------------------------------------------------------------------
// Execute Instructions Until the Specified Timer Threshold is Reached.
// Continues executing step6502() while accumulated cycles are less than timerTicks.
// Returns: Fixed value 0x80000000 after reaching the cycle threshold.
// -----------------------------------------------------------------------------
int cpu_6502::exec6502(int timerTicks)
{
	int cycles = 0;
	while (cycles < timerTicks)
		cycles += step6502();
	return 0x80000000;
}

// -----------------------------------------------------------------------------
// Execute a Single 6502 Instruction.
// Handles IRQs, fetches the opcode, performs addressing and operation logic,
// updates cycle counters, and optionally logs debugging information.
// Returns: Number of clock cycles used by the executed instruction.
// -----------------------------------------------------------------------------
int cpu_6502::step6502()
{
	clockticks6502 = 0;

	P |= F_T;

	if (_irqPending)
		irq6502();

	opcode = get6502memory(PC++);
	//++instruction_count[opcode];  // You should only enable this during instruction counting and debugging
	if (opcode > 0xFF)
	{
		LOG_INFO("Invalid Opcode called!!!: opcode %x Memory %X", opcode, PC);
		return 0;
	}

	if (debug)
	{
		int bytes = 0;
		std::string op = disassemble(PC - 1, &bytes);

		int c = (P & F_C) ? 1 : 0;
		int z = (P & F_Z) ? 1 : 0;
		int i = (P & F_I) ? 1 : 0;
		int d = (P & F_D) ? 1 : 0;
		int b = (P & F_B) ? 1 : 0;
		int t = (P & F_T) ? 1 : 0;
		int v = (P & F_V) ? 1 : 0;
		int n = (P & F_N) ? 1 : 0;

		LOG_INFO("%04X: %-20s F:C:%d Z:%d I:%d D:%d B:%d T:%d V:%d N:%d A:%02X X:%02X Y:%02X S:%02X",
			PC - 1, op.c_str(), c, z, i, d, b, t, v, n, A, X, Y, S);
	}

	PPC = PC;

	(this->*opcode_table[opcode].addressing_mode)();
	(this->*opcode_table[opcode].instruction)();

	clockticks6502 += ticks[opcode];
	clocktickstotal += clockticks6502;
    
	timer_update(clockticks6502, cpu_num);
	
	if (clocktickstotal > 0x0FFFFFFF)
		clocktickstotal = 0;

	return clockticks6502;
}

// -----------------------------------------------------------------------------
// Absolute Addressing Mode
// Operand is a 16-bit address (little-endian) following the opcode.
// -----------------------------------------------------------------------------
void cpu_6502::abs6502()
{
	savepc = get6502memory(PC) | (get6502memory(PC + 1) << 8);
	PC += 2;
}

// -----------------------------------------------------------------------------
// Immediate Addressing Mode
// Operand is the next byte. Used for instructions like LDA #$10.
// -----------------------------------------------------------------------------
void cpu_6502::immediate6502()
{
	savepc = PC++;
}

// -----------------------------------------------------------------------------
// Implied Addressing Mode
// No operand is used. Used for instructions like CLC, SEI, RTS, etc.
// -----------------------------------------------------------------------------
void cpu_6502::implied6502()
{
	// NOP (handled inside instruction)
}

// -----------------------------------------------------------------------------
// Relative Addressing Mode
// Operand is a signed 8-bit offset. Used only for branch instructions.
// -----------------------------------------------------------------------------
void cpu_6502::relative6502()
{
	savepc = get6502memory(PC++);
	if (savepc & 0x80)
		savepc |= 0xFF00;
}

// -----------------------------------------------------------------------------
// Indirect Addressing Mode (used only by JMP)
// Operand is a 16-bit pointer to a 16-bit address.
// Replicates 6502 page-boundary bug.
// -----------------------------------------------------------------------------
void cpu_6502::indirect6502()
{
	help = get6502memory(PC) | (get6502memory(PC + 1) << 8);
	uint16_t temp = (help & 0xFF00) | ((help + 1) & 0x00FF);
	savepc = get6502memory(help) | (get6502memory(temp) << 8);
	PC += 2;
}

// -----------------------------------------------------------------------------
// Absolute,X Addressing Mode
// Effective address = absolute address + X.
// Adds 1 cycle if page boundary is crossed.
// -----------------------------------------------------------------------------
void cpu_6502::absx6502()
{
	savepc = get6502memory(PC) | (get6502memory(PC + 1) << 8);
	if (ticks[opcode] == 4 && ((savepc ^ (savepc + X)) & 0xFF00))
		clockticks6502++;
	savepc += X;
	PC += 2;
}

// -----------------------------------------------------------------------------
// Absolute,Y Addressing Mode
// Effective address = absolute address + Y.
// Adds 1 cycle if page boundary is crossed.
// -----------------------------------------------------------------------------
void cpu_6502::absy6502()
{
	savepc = get6502memory(PC) | (get6502memory(PC + 1) << 8);
	if (ticks[opcode] == 4 && ((savepc ^ (savepc + Y)) & 0xFF00))
		clockticks6502++;
	savepc += Y;
	PC += 2;
}

// -----------------------------------------------------------------------------
// Zero Page Addressing Mode
// Operand is an 8-bit zero page address.
// -----------------------------------------------------------------------------
void cpu_6502::zp6502()
{
	savepc = get6502memory(PC++);
}

// -----------------------------------------------------------------------------
// Zero Page,X Addressing Mode
// Effective address = (zero page address + X) & 0xFF.
// -----------------------------------------------------------------------------
void cpu_6502::zpx6502()
{
	savepc = (get6502memory(PC++) + X) & 0xFF;
}

// -----------------------------------------------------------------------------
// Zero Page,Y Addressing Mode
// Effective address = (zero page address + Y) & 0xFF.
// -----------------------------------------------------------------------------
void cpu_6502::zpy6502()
{
	savepc = (get6502memory(PC++) + Y) & 0xFF;
}

// -----------------------------------------------------------------------------
// Indexed Indirect Addressing Mode (Indirect,X)
// Effective address = (operand + X) points to the final 16-bit address.
// -----------------------------------------------------------------------------
void cpu_6502::indx6502()
{
	value = (get6502memory(PC++) + X) & 0xFF;
	savepc = get6502memory(value) | (get6502memory((value + 1) & 0xFF) << 8);
}

// -----------------------------------------------------------------------------
// Indirect Indexed Addressing Mode (Indirect),Y
// Effective address = (operand points to base address) + Y.
// Adds 1 cycle if page boundary is crossed.
// -----------------------------------------------------------------------------
void cpu_6502::indy6502()
{
	uint16_t temp;
	value = get6502memory(PC++);
	temp = (value & 0xFF00) | ((value + 1) & 0x00FF);  //zero-page wraparound
	savepc = get6502memory(value) | (get6502memory(temp) << 8);
	if (ticks[opcode] == 5)
		if ((savepc >> 8) != ((savepc + Y) >> 8))
			clockticks6502++; //one cycle penlty for page-crossing on some opcodes
	savepc += Y;

}

// -----------------------------------------------------------------------------
// Indirect Absolute,X Addressing Mode (used by JMP ($nnnn,X))
// Effective address = address found at ($nnnn + X).
// -----------------------------------------------------------------------------
void cpu_6502::indabsx6502()
{
	help = get6502memory(PC) | (get6502memory(PC + 1) << 8);
	help += X;
	savepc = get6502memory(help) | (get6502memory(help + 1) << 8);
}

// -----------------------------------------------------------------------------
// Indirect Zero Page Addressing Mode (used by 65C02 extensions)
// Operand points to a zero page word (16-bit address).
// -----------------------------------------------------------------------------
void cpu_6502::indzp6502()
{
	value = get6502memory(PC++);
	savepc = get6502memory(value) | (get6502memory((value + 1) & 0xFF) << 8);
}

// -----------------------------------------------------------------------------
// Add with Carry (ADC)
// Adds memory value and carry flag to the accumulator.
// Handles both binary and decimal (BCD) mode.
// -----------------------------------------------------------------------------
inline void cpu_6502::adc6502()
{
	uint8_t tmp = get6502memory(savepc);
	if (P & F_D) // BCD mode
	{
		int c = P & F_C;
		int lo = (A & 0x0F) + (tmp & 0x0F) + c;
		int hi = (A & 0xF0) + (tmp & 0xF0);
		P &= ~(F_V | F_C);
		if (lo > 0x09) { hi += 0x10; lo += 0x06; }
		if (~(A ^ tmp) & (A ^ hi) & F_N) P |= F_V;
		if (hi > 0x90) hi += 0x60;
		if (hi & 0xFF00) P |= F_C;
		A = (lo & 0x0F) + (hi & 0xF0);
	}
	else // binary mode
	{
		int c = P & F_C;
		int sum = A + tmp + c;
		P &= ~(F_V | F_C);
		if (~(A ^ tmp) & (A ^ sum) & F_N) P |= F_V;
		if (sum & 0xFF00) P |= F_C;
		A = (uint8_t)(sum);  // C-style cast
	}
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Subtract with Carry (SBC)
// Subtracts a value from the accumulator (A) along with the inverse of the
// carry flag. Updates the carry, zero, negative, and overflow flags.
// Handles both binary and decimal (BCD) modes.
// -----------------------------------------------------------------------------
inline void cpu_6502::sbc6502()
{
	uint8_t tmp = get6502memory(savepc);
	if (P & F_D) // BCD mode
	{
		int c = (P & F_C) ^ F_C;
		int sum = A - tmp - c;
		int lo = (A & 0x0F) - (tmp & 0x0F) - c;
		int hi = (A & 0xF0) - (tmp & 0xF0);
		if (lo & 0x10) { lo -= 6; hi--; }
		P &= ~(F_V | F_C | F_Z | F_N);
		if ((A ^ tmp) & (A ^ sum) & F_N) P |= F_V;
		if ((sum & 0xFF00) == 0) P |= F_C;
		if (!(sum & 0xFF)) P |= F_Z;
		if (sum & 0x80) P |= F_N;
		A = (lo & 0x0F) | (hi & 0xF0);
	}
	else // binary mode
	{
		int c = (P & F_C) ^ F_C;
		int sum = A - tmp - c;
		P &= ~(F_V | F_C);
		if ((A ^ tmp) & (A ^ sum) & F_N) P |= F_V;
		if ((sum & 0xFF00) == 0) P |= F_C;
		A = (uint8_t)(sum);  // C-style cast
		set_nz(A);
	}
}

// -----------------------------------------------------------------------------
// Bitwise AND
// Performs a logical AND between the accumulator (A) and memory.
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::and6502()
{
	value = get6502memory(savepc);
	A &= value;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Bitwise Exclusive OR (EOR)
// Performs a logical exclusive OR between the accumulator (A) and memory.
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::eor6502()
{
	A ^= get6502memory(savepc);
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Bitwise OR (ORA)
// Performs a logical inclusive OR between the accumulator (A) and memory.
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::ora6502()
{
	A |= get6502memory(savepc);
	set_nz(A);
}

// -----------------------------------------------------------------------------
// BIT (Bit Test)
// Tests bits in memory against the accumulator (A).
// Sets the zero flag if (A & M) == 0.
// Sets the negative and overflow flags from memory bits 7 and 6 respectively.
// -----------------------------------------------------------------------------
void cpu_6502::bit6502()
{
	value = get6502memory(savepc);

	// Clear N, V, Z flags
	P &= ~(F_N | F_V | F_Z);

	// Set N and V from bits 7 and 6 of memory
	P |= value & (F_N | F_V);

	// Set Z if (A & value) == 0
	if ((A & value) == 0)
		P |= F_Z;
}

// -----------------------------------------------------------------------------
// Compare (CMP)
// Compares the accumulator (A) with memory and sets flags as if performing
// A - M, without storing the result.
// Updates carry, zero, and negative flags.
// -----------------------------------------------------------------------------
void cpu_6502::cmp6502()
{
	value = get6502memory(savepc);
	if (A >= value)
		P |= F_C;
	else
		P &= ~F_C;
	set_nz(A - value);
}

// -----------------------------------------------------------------------------
// Compare X Register (CPX)
// Compares the X register with memory and sets flags as if performing
// X - M, without storing the result.
// Updates carry, zero, and negative flags.
// -----------------------------------------------------------------------------
void cpu_6502::cpx6502()
{
	value = get6502memory(savepc);

	// Set Carry if X >= value
	if (X >= value)
		P |= F_C;
	else
		P &= ~F_C;

	set_nz(X - value);
}

// -----------------------------------------------------------------------------
// Compare Y Register (CPY)
// Compares the Y register with memory and sets flags as if performing
// Y - M, without storing the result.
// Updates carry, zero, and negative flags.
// -----------------------------------------------------------------------------
void cpu_6502::cpy6502()
{
	value = get6502memory(savepc);

	// Set Carry if Y >= value
	if (Y >= value)
		P |= F_C;
	else
		P &= ~F_C;

	set_nz(Y - value);
}

// -----------------------------------------------------------------------------
// Load Accumulator
// Loads a value from memory into the accumulator (A) and sets the zero and
// negative flags based on the result.
// -----------------------------------------------------------------------------
inline void cpu_6502::lda6502()
{
	A = get6502memory(savepc);
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Load X Register
// Loads a value from memory into the X register and sets the zero and
// negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::ldx6502()
{
	X = get6502memory(savepc);
	set_nz(X);
}

// -----------------------------------------------------------------------------
// Load Y Register
// Loads a value from memory into the Y register and sets the zero and
// negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::ldy6502()
{
	Y = get6502memory(savepc);
	set_nz(Y);
}

// -----------------------------------------------------------------------------
// Store Accumulator
// Stores the value of the accumulator (A) into memory.
// -----------------------------------------------------------------------------
void cpu_6502::sta6502()
{
	put6502memory(savepc, A);
}

// -----------------------------------------------------------------------------
// Store X Register
// Stores the value of the X register into memory.
// -----------------------------------------------------------------------------
void cpu_6502::stx6502()
{
	put6502memory(savepc, X);
}

// -----------------------------------------------------------------------------
// Store Y Register
// Stores the value of the Y register into memory.
// -----------------------------------------------------------------------------
void cpu_6502::sty6502()
{
	put6502memory(savepc, Y);
}

// -----------------------------------------------------------------------------
// Store Zero (65C02)
// Stores a zero value into memory. This is a 65C02 extension instruction.
// -----------------------------------------------------------------------------
void cpu_6502::stz6502()
{
	put6502memory(savepc, 0);
}

// -----------------------------------------------------------------------------
// Increment Memory
// Increments the value at the specified memory location and sets the zero and
// negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::inc6502()
{
	uint8_t result = get6502memory(savepc) + 1;
	put6502memory(savepc, result);
	set_nz(result);
}

// -----------------------------------------------------------------------------
// Decrement Memory
// Decrements the value at the specified memory location and sets the zero and
// negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::dec6502()
{
	uint8_t result = get6502memory(savepc) - 1;
	put6502memory(savepc, result);
	set_nz(result);
}

// -----------------------------------------------------------------------------
// Increment X
// Increments the X register and sets the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::inx6502()
{
	X++;
	set_nz(X);
}

// -----------------------------------------------------------------------------
// Decrement X
// Decrements the X register and sets the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::dex6502()
{
	X--;
	set_nz(X);
}

// -----------------------------------------------------------------------------
// Increment Y
// Increments the Y register and sets the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::iny6502()
{
	Y++;
	set_nz(Y);
}

// -----------------------------------------------------------------------------
// Decrement Y
// Decrements the Y register and sets the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::dey6502()
{
	Y--;
	set_nz(Y);
}

// -----------------------------------------------------------------------------
// Increment A (Unofficial)
// Increments the accumulator (A) and sets the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::ina6502()
{
	A++;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Decrement A (Unofficial)
// Decrements the accumulator (A) and sets the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::dea6502()
{
	A--;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Logical Shift Left (Memory)
// Performs a logical shift left on a memory value, stores the result back, and
// sets the carry, zero, and negative flags accordingly.
// -----------------------------------------------------------------------------
void cpu_6502::asl6502()
{
	value = get6502memory(savepc);
	P = (P & ~F_C) | ((value >> 7) & F_C);
	value <<= 1;
	put6502memory(savepc, value);
	set_nz(value);
}

// -----------------------------------------------------------------------------
// Logical Shift Left (Accumulator)
// Performs a logical shift left on the accumulator (A) and sets the carry,
// zero, and negative flags accordingly.
// -----------------------------------------------------------------------------
void cpu_6502::asla6502()
{
	P = (P & ~F_C) | ((A >> 7) & F_C);
	A <<= 1;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Logical Shift Right (Memory)
// Shifts memory value right by 1. Carry flag is set to bit 0 before the shift.
// The result is stored back at the same memory location.
// Updates the Zero and Negative flags (N is always 0).
// -----------------------------------------------------------------------------
void cpu_6502::lsr6502()
{
	value = get6502memory(savepc);
	P = (P & ~F_C) | (value & F_C);
	value >>= 1;
	put6502memory(savepc, value);
	set_nz(value);
}

// -----------------------------------------------------------------------------
// Logical Shift Right (Accumulator)
// Shifts the accumulator (A) one bit to the right (logical shift).
// The low bit is moved into the carry flag. Bit 7 is set to 0.
// Updates the carry, zero, and negative flags.
// -----------------------------------------------------------------------------
void cpu_6502::lsra6502()
{
	P = (P & ~F_C) | (A & F_C);
	A >>= 1;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Rotate Left (Memory)
// Rotates a memory value one bit to the left through the carry flag.
// Bit 7 moves into carry, carry moves into bit 0.
// Updates the carry, zero, and negative flags.
// -----------------------------------------------------------------------------
void cpu_6502::rol6502()
{
	saveflags = P & F_C;
	value = get6502memory(savepc);
	P = (P & ~F_C) | ((value >> 7) & F_C);
	value = (value << 1) | saveflags;
	put6502memory(savepc, value);
	set_nz(value);
}

// -----------------------------------------------------------------------------
// Rotate Left (Accumulator)
// Rotates the accumulator (A) one bit to the left through the carry flag.
// Bit 7 moves into carry, carry moves into bit 0.
// Updates the carry, zero, and negative flags.
// -----------------------------------------------------------------------------
void cpu_6502::rola6502()
{
	saveflags = P & F_C;
	P = (P & ~F_C) | ((A >> 7) & F_C);
	A = (A << 1) | saveflags;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Rotate Right (Memory)
// Rotates a memory value one bit to the right through the carry flag.
// Bit 0 moves into carry, carry moves into bit 7.
// Updates the carry, zero, and negative flags.
// -----------------------------------------------------------------------------
void cpu_6502::ror6502()
{
	saveflags = P & F_C;
	value = get6502memory(savepc);
	P = (P & ~F_C) | (value & F_C);
	value >>= 1;
	if (saveflags) value |= F_N;
	put6502memory(savepc, value);
	set_nz(value);
}

// -----------------------------------------------------------------------------
// Rotate Right (Accumulator)
// Rotates the accumulator (A) one bit to the right through the carry flag.
// Bit 0 moves into carry, carry moves into bit 7.
// Updates the carry, zero, and negative flags.
// -----------------------------------------------------------------------------
void cpu_6502::rora6502()
{
	saveflags = P & F_C;
	P = (P & ~F_C) | (A & F_C);
	A >>= 1;
	if (saveflags) A |= F_N;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Branch if Carry Clear
// Adds signed offset from operand if Carry flag is clear.
// Increments clockticks by 1 if branch is on same page,
// or by 2 if it crosses a page boundary.
// -----------------------------------------------------------------------------
inline void cpu_6502::bcc6502()
{
	if (!(P & F_C))
	{
		oldpc = PC;
		PC += (int8_t)savepc;  // C-style cast used here
		clockticks6502 += ((oldpc ^ PC) & 0xFF00) ? 2 : 1;
	}
}

// -----------------------------------------------------------------------------
// Branch if Carry Set (BCS)
// Branches to a relative address if the carry flag is set.
// Adds 1 cycle if branch occurs on the same page, 2 if it crosses a page.
// -----------------------------------------------------------------------------
inline void cpu_6502::bcs6502()
{
	if (P & F_C)
	{
		oldpc = PC;
		PC += (int8_t)(savepc);
		clockticks6502 += ((oldpc ^ PC) & 0xFF00) ? 2 : 1;
	}
}

// -----------------------------------------------------------------------------
// Branch if Equal (BEQ)
// Branches to a relative address if the zero flag is set (A == M).
// Adds 1 cycle if branch occurs on the same page, 2 if it crosses a page.
// -----------------------------------------------------------------------------
inline void cpu_6502::beq6502()
{
	if (P & F_Z)
	{
		oldpc = PC;
		PC += (int8_t)(savepc);
		clockticks6502 += ((oldpc ^ PC) & 0xFF00) ? 2 : 1;
	}
}

// -----------------------------------------------------------------------------
// Branch if Minus (BMI)
// Branches to a relative address if the negative flag is set (result < 0).
// Adds 1 cycle if branch occurs on the same page, 2 if it crosses a page.
// -----------------------------------------------------------------------------
inline void cpu_6502::bmi6502()
{
	if (P & F_N)
	{
		oldpc = PC;
		PC += (int8_t)(savepc);
		clockticks6502 += ((oldpc ^ PC) & 0xFF00) ? 2 : 1;
	}
}

// -----------------------------------------------------------------------------
// Branch if Not Equal (BNE)
// Branches to a relative address if the zero flag is clear (A != M).
// Adds 1 cycle if branch occurs on the same page, 2 if it crosses a page.
// -----------------------------------------------------------------------------
inline void cpu_6502::bne6502()
{
	if (!(P & F_Z))
	{
		oldpc = PC;
		PC += (int8_t)(savepc);
		clockticks6502 += ((oldpc ^ PC) & 0xFF00) ? 2 : 1;
	}
}

// -----------------------------------------------------------------------------
// Branch if Positive (BPL)
// Branches to a relative address if the negative flag is clear (result >= 0).
// Adds 1 cycle if branch occurs on the same page, 2 if it crosses a page.
// -----------------------------------------------------------------------------
void cpu_6502::bpl6502()
{
	if (!(P & F_N))
	{
		oldpc = PC;
		PC += (int8_t)(savepc);
		clockticks6502 += ((oldpc ^ PC) & 0xFF00) ? 2 : 1;
	}
}

// -----------------------------------------------------------------------------
// Branch if Overflow Clear (BVC)
// Branches to a relative address if the overflow flag is clear.
// Adds 1 cycle if branch occurs on the same page, 2 if it crosses a page.
// -----------------------------------------------------------------------------
void cpu_6502::bvc6502()
{
	if (!(P & F_V))
	{
		oldpc = PC;
		PC += (int8_t)(savepc);
		clockticks6502 += ((oldpc ^ PC) & 0xFF00) ? 2 : 1;
	}
}

// -----------------------------------------------------------------------------
// Branch if Overflow Set (BVS)
// Branches to a relative address if the overflow flag is set.
// Adds 1 cycle if branch occurs on the same page, 2 if it crosses a page.
// -----------------------------------------------------------------------------
void cpu_6502::bvs6502()
{
	if (P & F_V)
	{
		oldpc = PC;
		PC += (int8_t)(savepc);
		clockticks6502 += ((oldpc ^ PC) & 0xFF00) ? 2 : 1;
	}
}

// -----------------------------------------------------------------------------
// Branch Always (BRA) - 65C02 Only
// Unconditionally branches to a relative address.
// Adds 1 clock cycle to the base instruction timing.
// -----------------------------------------------------------------------------
inline void cpu_6502::bra6502()
{
	PC += (int8_t)(savepc);
	clockticks6502++;
}
// -----------------------------------------------------------------------------
// Jump (JMP)
// Sets the program counter to the target address specified by savepc.
// -----------------------------------------------------------------------------
inline void cpu_6502::jmp6502()
{
	PC = savepc;
}

// -----------------------------------------------------------------------------
// Jump to Subroutine (JSR)
// Pushes the return address (PC - 1) onto the stack and jumps to the target
// address specified by savepc.
// -----------------------------------------------------------------------------
inline void cpu_6502::jsr6502()
{
	PC--;
	push16(PC);
	PC = savepc;
}

// -----------------------------------------------------------------------------
// Return from Subroutine (RTS)
// Pops the return address from the stack and sets the program counter to it,
// then increments the program counter by 1.
// -----------------------------------------------------------------------------
inline void cpu_6502::rts6502()
{
	PC = pull16();
	PC++;
}

// -----------------------------------------------------------------------------
// Return from Interrupt (RTI)
// Pops the processor status and program counter from the stack.
// Restores the CPU state after an interrupt handler has completed.
// -----------------------------------------------------------------------------
void cpu_6502::rti6502()
{
	P = pull8() | F_T | F_B;
	PC = pull16();
}

// -----------------------------------------------------------------------------
// Break (BRK)
// Forces an interrupt (software interrupt).
// Pushes the program counter and processor status onto the stack,
// sets the interrupt disable flag, and loads the interrupt vector from $FFFE.
// -----------------------------------------------------------------------------
void cpu_6502::brk6502()
{
	PC++;
	push16(PC);
	push8(P | F_B);
	P = (P | F_I) & ~F_D;
	PC = get6502memory(0xFFFE & addrmask) | (get6502memory(0xFFFF & addrmask) << 8);
}

// -----------------------------------------------------------------------------
// No Operation (NOP)
// Performs no operation. Some undocumented NOPs add timing delay.
// Logs a warning if the NOP opcode is unofficial.
// -----------------------------------------------------------------------------
void cpu_6502::nop6502()
{
	if (opcode != 0xEA) // Official NOP is 0xEA
		LOG_INFO("!!!!WARNING UNHANDLED NO-OP CALLED: %02X CPU: %d", opcode, cpu_num);

	// I need to review this when I get some time.
	switch (opcode)
	{
	case 0x1C: case 0x3C: case 0x5C: case 0x7C:
	case 0xDC: case 0xFC:
		clockticks6502++;
		break;
	}
}

// -----------------------------------------------------------------------------
// Clear Carry Flag (CLC)
// Clears the carry flag (C = 0).
// -----------------------------------------------------------------------------
void cpu_6502::clc6502()
{
	P &= ~F_C;
}

// -----------------------------------------------------------------------------
// Clear Decimal Mode (CLD)
// Clears the decimal mode flag (D = 0), disabling BCD arithmetic.
// -----------------------------------------------------------------------------
void cpu_6502::cld6502()
{
	P &= ~F_D;
}

// -----------------------------------------------------------------------------
// Clear Interrupt Disable (CLI)
// Clears the interrupt disable flag (I = 0), enabling maskable IRQs.
// Also checks for any pending interrupts that were deferred.
// -----------------------------------------------------------------------------
void cpu_6502::cli6502()
{
	P &= ~F_I;
	check_interrupts_after_cli(); // evaluate pending IRQs
}

// -----------------------------------------------------------------------------
// Clear Overflow Flag (CLV)
// Clears the overflow flag (V = 0).
// -----------------------------------------------------------------------------
void cpu_6502::clv6502()
{
	P &= ~F_V;
}

// -----------------------------------------------------------------------------
// Set Carry Flag (SEC)
// Sets the carry flag (C = 1).
// -----------------------------------------------------------------------------
void cpu_6502::sec6502()
{
	P |= F_C;
}

// -----------------------------------------------------------------------------
// Set Decimal Mode (SED)
// Sets the decimal mode flag (D = 1), enabling BCD arithmetic.
// -----------------------------------------------------------------------------
void cpu_6502::sed6502()
{
	P |= F_D;
}

// -----------------------------------------------------------------------------
// Set Interrupt Disable (SEI)
// Sets the interrupt disable flag (I = 1), preventing maskable IRQs.
// -----------------------------------------------------------------------------
void cpu_6502::sei6502()
{
	P |= F_I;
}

// -----------------------------------------------------------------------------
// Transfer Accumulator to X (TAX)
// Copies the accumulator (A) into the X register.
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::tax6502()
{
	X = A;
	set_nz(X);
}

// -----------------------------------------------------------------------------
// Transfer Accumulator to Y (TAY)
// Copies the accumulator (A) into the Y register.
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::tay6502()
{
	Y = A;
	set_nz(Y);
}

// -----------------------------------------------------------------------------
// Transfer X to Accumulator (TXA)
// Copies the X register into the accumulator (A).
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::txa6502()
{
	A = X;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Transfer Y to Accumulator (TYA)
// Copies the Y register into the accumulator (A).
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::tya6502()
{
	A = Y;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Transfer Stack Pointer to X (TSX)
// Copies the stack pointer (S) into the X register.
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::tsx6502()
{
	X = S;
	set_nz(X);
}

// -----------------------------------------------------------------------------
// Transfer X to Stack Pointer (TXS)
// Copies the X register into the stack pointer (S).
// Does not affect any flags.
// -----------------------------------------------------------------------------
void cpu_6502::txs6502()
{
	S = X;
}

// -----------------------------------------------------------------------------
// Push Accumulator (PHA)
// Pushes the accumulator (A) onto the stack.
// -----------------------------------------------------------------------------
void cpu_6502::pha6502()
{
	push8(A);
}

// -----------------------------------------------------------------------------
// Push Processor Status (PHP)
// Pushes the processor status register (P) onto the stack.
// The break flag is set in the value pushed.
// -----------------------------------------------------------------------------
void cpu_6502::php6502()
{
	push8(P | F_B);
}

// -----------------------------------------------------------------------------
// Pull Accumulator (PLA)
// Pulls a byte from the stack into the accumulator (A).
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::pla6502()
{
	A = pull8();
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Pull Processor Status (PLP)
// Pulls a byte from the stack into the processor status register (P).
// The unused flag (T) is forced on.
// -----------------------------------------------------------------------------
void cpu_6502::plp6502()
{
	P = pull8() | F_T;
}

// -----------------------------------------------------------------------------
// Push X Register (PHX) - 65C02/undocumented
// Pushes the X register onto the stack.
// -----------------------------------------------------------------------------
void cpu_6502::phx6502()
{
	push8(X);
}

// -----------------------------------------------------------------------------
// Pull X Register (PLX) - 65C02/undocumented
// Pulls a byte from the stack into the X register.
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::plx6502()
{
	X = pull8();
	set_nz(X);
}

// -----------------------------------------------------------------------------
// Push Y Register (PHY) - 65C02/undocumented
// Pushes the Y register onto the stack.
// -----------------------------------------------------------------------------
void cpu_6502::phy6502()
{
	push8(Y);
}

// -----------------------------------------------------------------------------
// Pull Y Register (PLY) - 65C02/undocumented
// Pulls a byte from the stack into the Y register.
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::ply6502()
{
	Y = pull8();
	set_nz(Y);
}

// -----------------------------------------------------------------------------
// LAX Instruction (Undocumented)
//
// Loads memory into both the A and X registers simultaneously.
// Affects: N, Z
// -----------------------------------------------------------------------------
inline void cpu_6502::lax6502()
{
	A = X = get6502memory(savepc);
	set_nz(A);
}

// -----------------------------------------------------------------------------
// SAX Instruction (Undocumented)
//
// Stores A & X to memory. Combines A and X registers with bitwise AND.
// Affects: None
// -----------------------------------------------------------------------------
inline void cpu_6502::sax6502()
{
	put6502memory(savepc, A & X);
}

// -----------------------------------------------------------------------------
// TRB (Test and Reset Bits)
// Tests bits in memory against the accumulator (A).
// Sets the zero flag if (A & M) == 0.
// Clears bits in memory where A has 1s (M = M & ~A).
// -----------------------------------------------------------------------------
void cpu_6502::trb6502()
{
	uint8_t v = get6502memory(savepc);
	set_z(A & v);
	put6502memory(savepc, v & ~A);
}

// -----------------------------------------------------------------------------
// TSB (Test and Set Bits)
// Tests bits in memory against the accumulator (A).
// Sets the zero flag if (A & M) == 0.
// Sets bits in memory where A has 1s (M = M | A).
// -----------------------------------------------------------------------------
void cpu_6502::tsb6502()
{
	uint8_t v = get6502memory(savepc);
	set_z(A & v);
	put6502memory(savepc, v | A);
}

// -----------------------------------------------------------------------------
// DCP Instruction (Undocumented)
//
// Decrements memory, then compares the result with A as if performing CMP.
// Affects: N, Z, C
// -----------------------------------------------------------------------------
inline void cpu_6502::dcp6502()
{
	uint8_t m = get6502memory(savepc) - 1;
	put6502memory(savepc, m);
	uint16_t result = (uint16_t)A - m;
	P = (P & ~(F_C | F_Z | F_N)) |
		((result < 0x100) ? F_C : 0) |
		((A == m) ? F_Z : 0) |
		((result & 0x80) ? F_N : 0);
}

// -----------------------------------------------------------------------------
// ISC Instruction (Undocumented)
//
// Increments memory, then subtracts it from A with carry (SBC).
// Affects: N, Z, C, V
// -----------------------------------------------------------------------------
inline void cpu_6502::isc6502()
{
	uint8_t m = get6502memory(savepc) + 1;
	put6502memory(savepc, m);

	uint16_t sum = (uint16_t)A - m - (P & F_C ? 0 : 1);
	uint8_t result = (uint8_t)(sum & 0xFF);

	P = (P & ~(F_V | F_C)) |
		((~(A ^ m) & (A ^ result) & 0x80) ? F_V : 0) |
		((sum < 0x100) ? F_C : 0);

	A = result;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// SLO Instruction (Undocumented)
//
// Shifts memory left (ASL), then ORs the result into A.
// Affects: N, Z, C
// -----------------------------------------------------------------------------
inline void cpu_6502::slo6502()
{
	value = get6502memory(savepc);
	P = (P & ~F_C) | (value >> 7);
	value <<= 1;
	put6502memory(savepc, value);
	A |= value;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// RRA Instruction (Undocumented)
//
// Rotates memory right (ROR), then adds it to A with carry (ADC).
// Affects: N, Z, C, V
// -----------------------------------------------------------------------------
inline void cpu_6502::rra6502()
{
	value = get6502memory(savepc);
	uint8_t carry_in = (P & F_C) ? 0x80 : 0;
	P = (P & ~F_C) | (value & 1);
	value = (value >> 1) | carry_in;
	put6502memory(savepc, value);

	int c = (P & F_C) ? 1 : 0;
	int sum = A + value + c;

	P &= ~(F_V | F_C);
	if (~(A ^ value) & (A ^ sum) & 0x80) P |= F_V;
	if (sum > 0xFF) P |= F_C;

	A = (uint8_t)(sum);
	set_nz(A);
}

// -----------------------------------------------------------------------------
// RLA Instruction (Undocumented)
//
// Performs a left shift on memory (ASL), then ANDs the result with A.
// Equivalent to: ASL + AND
// Affects: N, Z, C
// -----------------------------------------------------------------------------
inline void cpu_6502::rla6502()
{
	value = get6502memory(savepc);
	P = (P & ~F_C) | (value >> 7); // Set Carry from old bit 7
	value <<= 1;
	put6502memory(savepc, value);

	A &= value;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// SRE Instruction (Undocumented)
//
// Performs a logical shift right (LSR) on memory, then EORs the result with A.
// Equivalent to: LSR + EOR
// Affects: N, Z, C
// -----------------------------------------------------------------------------
inline void cpu_6502::sre6502()
{
	value = get6502memory(savepc);
	P = (P & ~F_C) | (value & 0x01); // Set Carry from bit 0
	value >>= 1;
	put6502memory(savepc, value);

	A ^= value;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Log Instruction Usage
// Logs the number of times each opcode was executed during the current frame.
// Intended for profiling or debugging purposes.
// -----------------------------------------------------------------------------
void cpu_6502::log_instruction_usage()
{
	LOG_INFO("Instruction Usage This Frame:");
	for (int i = 0; i < 256; ++i)
	{
		if (instruction_count[i] > 0)
		{
			LOG_INFO("Opcode %02X (%s): %llu", i, mnemonics[i], instruction_count[i]);
		}
	}
}

// -----------------------------------------------------------------------------
// Reset Instruction Counts
// Resets the instruction execution counters to zero.
// Typically called at the start of a new profiling frame.
// -----------------------------------------------------------------------------
void cpu_6502::reset_instruction_counts()
{
	std::fill(std::begin(instruction_count), std::end(instruction_count), 0);
}

/*
Usage:
int used = 0;
std::string disasm = cpu->disassemble(cpu->get_pc(), &used);
LOG_INFO("%04X: %-20s  A:%02X X:%02X Y:%02X P:%02X", cpu->get_pc(), disasm.c_str(), cpu->A, cpu->X, cpu->Y, cpu->P);
*/

std::string cpu_6502::disassemble(uint16_t pc, int* bytesUsed)
{
	static const uint8_t length[256] = {
		2,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1,  // 00–0F
		2,2,2,1,2,2,2,1,1,3,1,1,3,3,3,1,  // 10–1F
		3,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1,  // 20–2F
		2,2,2,1,2,2,2,1,1,3,1,1,3,3,3,1,  // 30–3F
		1,2,1,1,1,2,2,1,1,2,1,1,3,3,3,1,  // 40–4F
		2,2,2,1,1,2,2,1,1,3,1,1,1,3,3,1,  // 50–5F
		1,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1,  // 60–6F
		2,2,2,1,2,2,2,1,1,3,1,1,3,3,3,1,  // 70–7F
		2,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1,  // 80–8F
		2,2,2,1,2,2,2,1,1,3,1,1,3,3,3,1,  // 90–9F
		2,2,2,1,2,2,2,1,1,2,1,1,3,3,3,1,  // A0–AF
		2,2,1,1,2,2,2,1,1,3,1,1,3,3,3,1,  // B0–BF
		2,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1,  // C0–CF
		2,2,2,1,1,2,2,1,1,3,1,1,3,3,3,1,  // D0–DF
		2,2,1,2,2,2,2,2,1,2,1,2,3,3,3,3,  // E0–EF
		2,2,2,2,2,2,2,2,1,3,1,3,3,3,3,3   // F0–FF
	};

	char buffer[64] = {};
	uint8_t opcode = get6502memory(pc);
	uint8_t op1 = get6502memory(pc + 1);
	uint8_t op2 = get6502memory(pc + 2);

	switch (length[opcode])
	{
	case 1:
		sprintf_s(buffer, sizeof(buffer), "%02X       %-4s", opcode, mnemonics[opcode]);
		break;

	case 2:
		if ((opcode & 0x1F) == 0x10) // branch instructions
		{
			int8_t offset = static_cast<int8_t>(op1);
			uint16_t target = pc + 2 + offset;
			sprintf_s(buffer, sizeof(buffer), "%02X %02X    %-4s $%04X", opcode, op1, mnemonics[opcode], target);
		}
		else
		{
			sprintf_s(buffer, sizeof(buffer), "%02X %02X    %-4s $%02X", opcode, op1, mnemonics[opcode], op1);
		}
		break;

	case 3:
	{
		uint16_t addr = static_cast<uint16_t>(op1) | (static_cast<uint16_t>(op2) << 8);
		if (opcode == 0x4C || opcode == 0x20) // JMP abs, JSR abs
		{
			sprintf_s(buffer, sizeof(buffer), "%02X %02X %02X %-4s $%04X", opcode, op1, op2, mnemonics[opcode], addr);
		}
		else if (opcode == 0x6C) // JMP (indirect)
		{
			sprintf_s(buffer, sizeof(buffer), "%02X %02X %02X %-4s ($%04X)", opcode, op1, op2, mnemonics[opcode], addr);
		}
		else
		{
			sprintf_s(buffer, sizeof(buffer), "%02X %02X %02X %-4s $%02X%02X", opcode, op1, op2, mnemonics[opcode], op2, op1);
		}
		break;
	}

	default:
		sprintf_s(buffer, sizeof(buffer), "%02X       ???", opcode);
		break;
	}

	if (bytesUsed)
		*bytesUsed = length[opcode];

	return std::string(buffer);
}