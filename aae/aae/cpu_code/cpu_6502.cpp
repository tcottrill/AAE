// -----------------------------------------------------------------------------
// AAE (Another Arcade Emulator) - 6502 CPU Core
//
// This file is part of the AAE project and is released under The Unlicense.
// You are free to use, modify, and distribute this software without restriction.
// See <http://unlicense.org/> for details.
// -----------------------------------------------------------------------------

#include <stdio.h>
#include <cstring> // Required for memcpy
#include "cpu_6502.h"
#include "sys_log.h"

#ifdef USING_AAE_EMU
#include "timer.h"
#endif // USING_AAE_EMU


#define bget(p,m) ((p) & (m))

// -----------------------------------------------------------------------------
// Opcode Mnemonics Table
// -----------------------------------------------------------------------------
static const char* mnemonics[256] = {
	"BRK","ORA","KIL","SLO","NOP","ORA","ASL","SLO","PHP","ORA","ASL","ANC","NOP","ORA","ASL","SLO", // 00-0F
	"BPL","ORA","KIL","SLO","NOP","ORA","ASL","SLO","CLC","ORA","NOP","SLO","NOP","ORA","ASL","SLO", // 10-1F
	"JSR","AND","KIL","RLA","BIT","AND","ROL","RLA","PLP","AND","ROL","ANC","BIT","AND","ROL","RLA", // 20-2F
	"BMI","AND","KIL","RLA","NOP","AND","ROL","RLA","SEC","AND","NOP","RLA","NOP","AND","ROL","RLA", // 30-3F
	"RTI","EOR","KIL","SRE","NOP","EOR","LSR","SRE","PHA","EOR","LSR","ALR","JMP","EOR","LSR","SRE", // 40-4F
	"BVC","EOR","KIL","SRE","NOP","EOR","LSR","SRE","CLI","EOR","NOP","SRE","NOP","EOR","LSR","SRE", // 50-5F
	"RTS","ADC","KIL","RRA","NOP","ADC","ROR","RRA","PLA","ADC","ROR","ARR","JMP","ADC","ROR","RRA", // 60-6F
	"BVS","ADC","KIL","RRA","NOP","ADC","ROR","RRA","SEI","ADC","NOP","RRA","NOP","ADC","ROR","RRA", // 70-7F
	"NOP","STA","NOP","SAX","STY","STA","STX","SAX","DEY","NOP","TXA","XAA","STY","STA","STX","SAX", // 80-8F
	"BCC","STA","KIL","AHX","STY","STA","STX","SAX","TYA","STA","TXS","TAS","SHY","STA","SHX","AHX", // 90-9F
	"LDY","LDA","LDX","LAX","LDY","LDA","LDX","LAX","TAY","LDA","TAX","LAX","LDY","LDA","LDX","LAX", // A0-AF
	"BCS","LDA","KIL","LAX","LDY","LDA","LDX","LAX","CLV","LDA","TSX","LAS","LDY","LDA","LDX","LAX", // B0-BF
	"CPY","CMP","NOP","DCP","CPY","CMP","DEC","DCP","INY","CMP","DEX","AXS","CPY","CMP","DEC","DCP", // C0-CF
	"BNE","CMP","KIL","DCP","NOP","CMP","DEC","DCP","CLD","CMP","NOP","DCP","NOP","CMP","DEC","DCP", // D0-DF
	"CPX","SBC","NOP","ISC","CPX","SBC","INC","ISC","INX","SBC","NOP","SBC","CPX","SBC","INC","ISC", // E0-EF
	"BEQ","SBC","KIL","ISC","NOP","SBC","INC","ISC","SED","SBC","NOP","ISC","NOP","SBC","INC","ISC"  // F0-FF
};

// -----------------------------------------------------------------------------
// Initial Opcode Table
// Contains NMOS Official, NMOS Illegal, and CMOS Official instructions.
// -----------------------------------------------------------------------------
const cpu_6502::OpEntry cpu_6502::initial_opcode_table[256] = {
	{ &cpu_6502::brk6502, &cpu_6502::implied6502 },   // 0x00
	{ &cpu_6502::ora6502, &cpu_6502::indx6502    },   // 0x01
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x02
	{ &cpu_6502::slo6502, &cpu_6502::indx6502    },   // 0x03 (UNDOC)
	{ &cpu_6502::tsb6502, &cpu_6502::zp6502      },   // 0x04 (CMOS)
	{ &cpu_6502::ora6502, &cpu_6502::zp6502      },   // 0x05
	{ &cpu_6502::asl6502, &cpu_6502::zp6502      },   // 0x06
	{ &cpu_6502::slo6502, &cpu_6502::zp6502      },   // 0x07 (UNDOC)
	{ &cpu_6502::php6502, &cpu_6502::implied6502 },   // 0x08
	{ &cpu_6502::ora6502, &cpu_6502::immediate6502 }, // 0x09
	{ &cpu_6502::asla6502, &cpu_6502::implied6502 },  // 0x0A
	{ &cpu_6502::anc6502, &cpu_6502::immediate6502 }, // 0x0B (UNDOC)
	{ &cpu_6502::tsb6502, &cpu_6502::abs6502     },   // 0x0C (CMOS)
	{ &cpu_6502::ora6502, &cpu_6502::abs6502     },   // 0x0D
	{ &cpu_6502::asl6502, &cpu_6502::abs6502     },   // 0x0E
	{ &cpu_6502::slo6502, &cpu_6502::abs6502     },   // 0x0F (UNDOC)
	{ &cpu_6502::bpl6502, &cpu_6502::relative6502 },  // 0x10
	{ &cpu_6502::ora6502, &cpu_6502::indy6502    },   // 0x11
	{ &cpu_6502::ora6502, &cpu_6502::indzp6502   },   // 0x12 (CMOS)
	{ &cpu_6502::slo6502, &cpu_6502::indy6502    },   // 0x13 SLO (ind),Y (UNDOC)
	{ &cpu_6502::trb6502, &cpu_6502::zp6502      },   // 0x14 (CMOS)
	{ &cpu_6502::ora6502, &cpu_6502::zpx6502     },   // 0x15
	{ &cpu_6502::asl6502, &cpu_6502::zpx6502     },   // 0x16
	{ &cpu_6502::slo6502, &cpu_6502::zpx6502     },   // 0x17 (UNDOC)
	{ &cpu_6502::clc6502, &cpu_6502::implied6502 },   // 0x18
	{ &cpu_6502::ora6502, &cpu_6502::absy6502    },   // 0x19
	{ &cpu_6502::ina6502, &cpu_6502::implied6502 },   // 0x1A (CMOS)
	{ &cpu_6502::slo6502, &cpu_6502::absy6502    },   // 0x1B (UNDOC)
	{ &cpu_6502::trb6502, &cpu_6502::abs6502     },   // 0x1C (CMOS)
	{ &cpu_6502::ora6502, &cpu_6502::absx6502    },   // 0x1D
	{ &cpu_6502::asl6502, &cpu_6502::absx6502    },   // 0x1E
	{ &cpu_6502::slo6502, &cpu_6502::absx6502    },   // 0x1F (UNDOC)
	{ &cpu_6502::jsr6502, &cpu_6502::abs_jsr6502 },   // 0x20
	{ &cpu_6502::and6502, &cpu_6502::indx6502    },   // 0x21
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x22
	{ &cpu_6502::rla6502, &cpu_6502::indx6502    },   // 0x23 (UNDOC)
	{ &cpu_6502::bit6502, &cpu_6502::zp6502      },   // 0x24
	{ &cpu_6502::and6502, &cpu_6502::zp6502      },   // 0x25
	{ &cpu_6502::rol6502, &cpu_6502::zp6502      },   // 0x26
	{ &cpu_6502::rla6502, &cpu_6502::zp6502      },   // 0x27 (UNDOC)
	{ &cpu_6502::plp6502, &cpu_6502::implied6502 },   // 0x28
	{ &cpu_6502::and6502, &cpu_6502::immediate6502 }, // 0x29
	{ &cpu_6502::rola6502, &cpu_6502::implied6502 },  // 0x2A
	{ &cpu_6502::anc6502, &cpu_6502::immediate6502 }, // 0x2B (UNDOC)
	{ &cpu_6502::bit6502, &cpu_6502::abs6502     },   // 0x2C
	{ &cpu_6502::and6502, &cpu_6502::abs6502     },   // 0x2D
	{ &cpu_6502::rol6502, &cpu_6502::abs6502     },   // 0x2E
	{ &cpu_6502::rla6502, &cpu_6502::abs6502     },   // 0x2F (UNDOC)
	{ &cpu_6502::bmi6502, &cpu_6502::relative6502 },  // 0x30
	{ &cpu_6502::and6502, &cpu_6502::indy6502    },   // 0x31
	{ &cpu_6502::and6502, &cpu_6502::indzp6502   },   // 0x32 (CMOS)
	{ &cpu_6502::rla6502, &cpu_6502::indy6502    },   // 0x33 RLA (ind),Y (UNDOC)
	{ &cpu_6502::bit6502, &cpu_6502::zpx6502     },   // 0x34 (CMOS uses this, NMOS UNDOC)
	{ &cpu_6502::and6502, &cpu_6502::zpx6502     },   // 0x35
	{ &cpu_6502::rol6502, &cpu_6502::zpx6502     },   // 0x36
	{ &cpu_6502::rla6502, &cpu_6502::zpx6502     },   // 0x37 (UNDOC)
	{ &cpu_6502::sec6502, &cpu_6502::implied6502 },   // 0x38
	{ &cpu_6502::and6502, &cpu_6502::absy6502    },   // 0x39
	{ &cpu_6502::dea6502, &cpu_6502::implied6502 },   // 0x3A (CMOS)
	{ &cpu_6502::rla6502, &cpu_6502::absy6502    },   // 0x3B (UNDOC)
	{ &cpu_6502::bit6502, &cpu_6502::absx6502    },   // 0x3C (CMOS uses this, NMOS UNDOC)
	{ &cpu_6502::and6502, &cpu_6502::absx6502    },   // 0x3D
	{ &cpu_6502::rol6502, &cpu_6502::absx6502    },   // 0x3E
	{ &cpu_6502::rla6502, &cpu_6502::absx6502    },   // 0x3F (UNDOC)
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
	{ &cpu_6502::alr6502, &cpu_6502::immediate6502 }, // 0x4B (UNDOC)
	{ &cpu_6502::jmp6502, &cpu_6502::abs6502     },   // 0x4C
	{ &cpu_6502::eor6502, &cpu_6502::abs6502     },   // 0x4D
	{ &cpu_6502::lsr6502, &cpu_6502::abs6502     },   // 0x4E
	{ &cpu_6502::sre6502, &cpu_6502::abs6502     },   // 0x4F (UNDOC)
	{ &cpu_6502::bvc6502, &cpu_6502::relative6502 },  // 0x50
	{ &cpu_6502::eor6502, &cpu_6502::indy6502    },   // 0x51
	{ &cpu_6502::eor6502, &cpu_6502::indzp6502   },   // 0x52 (CMOS)
	{ &cpu_6502::sre6502, &cpu_6502::indy6502    },   // 0x53 SRE (ind),Y (UNDOC)
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x54
	{ &cpu_6502::eor6502, &cpu_6502::zpx6502     },   // 0x55
	{ &cpu_6502::lsr6502, &cpu_6502::zpx6502     },   // 0x56
	{ &cpu_6502::sre6502, &cpu_6502::zpx6502     },   // 0x57 (UNDOC)
	{ &cpu_6502::cli6502, &cpu_6502::implied6502 },   // 0x58
	{ &cpu_6502::eor6502, &cpu_6502::absy6502    },   // 0x59
	{ &cpu_6502::phy6502, &cpu_6502::implied6502 },   // 0x5A (CMOS)
	{ &cpu_6502::sre6502, &cpu_6502::absy6502    },   // 0x5B (UNDOC)
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x5C
	{ &cpu_6502::eor6502, &cpu_6502::absx6502    },   // 0x5D
	{ &cpu_6502::lsr6502, &cpu_6502::absx6502    },   // 0x5E
	{ &cpu_6502::sre6502, &cpu_6502::absx6502    },   // 0x5F (UNDOC)
	{ &cpu_6502::rts6502, &cpu_6502::implied6502 },   // 0x60
	{ &cpu_6502::adc6502, &cpu_6502::indx6502    },   // 0x61
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x62
	{ &cpu_6502::rra6502, &cpu_6502::indx6502    },   // 0x63 (UNDOC)
	{ &cpu_6502::stz6502, &cpu_6502::zp6502      },   // 0x64 (CMOS)
	{ &cpu_6502::adc6502, &cpu_6502::zp6502      },   // 0x65
	{ &cpu_6502::ror6502, &cpu_6502::zp6502      },   // 0x66
	{ &cpu_6502::rra6502, &cpu_6502::zp6502      },   // 0x67 (UNDOC)
	{ &cpu_6502::pla6502, &cpu_6502::implied6502 },   // 0x68
	{ &cpu_6502::adc6502, &cpu_6502::immediate6502 }, // 0x69
	{ &cpu_6502::rora6502, &cpu_6502::implied6502 },  // 0x6A
	{ &cpu_6502::arr6502, &cpu_6502::immediate6502 }, // 0x6B (UNDOC)
	{ &cpu_6502::jmp6502, &cpu_6502::indirect6502 },  // 0x6C
	{ &cpu_6502::adc6502, &cpu_6502::abs6502     },   // 0x6D
	{ &cpu_6502::ror6502, &cpu_6502::abs6502     },   // 0x6E
	{ &cpu_6502::rra6502, &cpu_6502::abs6502     },   // 0x6F (UNDOC)
	{ &cpu_6502::bvs6502, &cpu_6502::relative6502 },  // 0x70
	{ &cpu_6502::adc6502, &cpu_6502::indy6502    },   // 0x71
	{ &cpu_6502::adc6502, &cpu_6502::indzp6502   },   // 0x72 (CMOS)
	{ &cpu_6502::rra6502, &cpu_6502::indy6502    },   // 0x73 RRA (ind),Y (UNDOC)
	{ &cpu_6502::stz6502, &cpu_6502::zpx6502     },   // 0x74 (CMOS)
	{ &cpu_6502::adc6502, &cpu_6502::zpx6502     },   // 0x75
	{ &cpu_6502::ror6502, &cpu_6502::zpx6502     },   // 0x76
	{ &cpu_6502::rra6502, &cpu_6502::zpx6502     },   // 0x77 (UNDOC)
	{ &cpu_6502::sei6502, &cpu_6502::implied6502 },   // 0x78
	{ &cpu_6502::adc6502, &cpu_6502::absy6502    },   // 0x79
	{ &cpu_6502::ply6502, &cpu_6502::implied6502 },   // 0x7A (CMOS)
	{ &cpu_6502::rra6502, &cpu_6502::absy6502    },   // 0x7B (UNDOC)
	{ &cpu_6502::jmp6502, &cpu_6502::indabsx6502 },   // 0x7C (CMOS)
	{ &cpu_6502::adc6502, &cpu_6502::absx6502    },   // 0x7D
	{ &cpu_6502::ror6502, &cpu_6502::absx6502    },   // 0x7E
	{ &cpu_6502::rra6502, &cpu_6502::absx6502    },   // 0x7F (UNDOC)
	{ &cpu_6502::bra6502, &cpu_6502::relative6502 },  // 0x80 (CMOS)
	{ &cpu_6502::sta6502, &cpu_6502::indx6502 },	  // 0x81
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0x82
	{ &cpu_6502::sax6502, &cpu_6502::indx6502 },      // 0x83 SAX (ind,X) (UNDOC)
	{ &cpu_6502::sty6502, &cpu_6502::zp6502 },		  // 0x84
	{ &cpu_6502::sta6502, &cpu_6502::zp6502 },		  // 0x85
	{ &cpu_6502::stx6502, &cpu_6502::zp6502 },		  // 0x86
	{ &cpu_6502::sax6502, &cpu_6502::zp6502 },		  // 0x87 SAX zp (UNDOC)
	{ &cpu_6502::dey6502, &cpu_6502::implied6502 },   // 0x88
	{ &cpu_6502::bit6502, &cpu_6502::immediate6502 }, // 0x89 (CMOS uses this, NMOS UNDOC)
	{ &cpu_6502::txa6502, &cpu_6502::implied6502 },   // 0x8A
	{ &cpu_6502::ane6502, &cpu_6502::immediate6502 }, // 0x8B ANE/XAA (UNDOC)
	{ &cpu_6502::sty6502, &cpu_6502::abs6502 },       // 0x8C
	{ &cpu_6502::sta6502, &cpu_6502::abs6502 },       // 0x8D
	{ &cpu_6502::stx6502, &cpu_6502::abs6502 },       // 0x8E
	{ &cpu_6502::sax6502, &cpu_6502::abs6502 },       // 0x8F SAX abs (UNDOC)
	{ &cpu_6502::bcc6502, &cpu_6502::relative6502 },  // 0x90
	{ &cpu_6502::sta6502, &cpu_6502::indy6502 },      // 0x91
	{ &cpu_6502::sta6502, &cpu_6502::indzp6502 },	  // 0x92 (CMOS)
	{ &cpu_6502::ahx6502, &cpu_6502::indy6502 },      // 0x93 AHX (ind),Y (UNDOC)
	{ &cpu_6502::sty6502, &cpu_6502::zpx6502 },		  // 0x94
	{ &cpu_6502::sta6502, &cpu_6502::zpx6502 },		  // 0x95
	{ &cpu_6502::stx6502, &cpu_6502::zpy6502 },		  // 0x96
	{ &cpu_6502::sax6502, &cpu_6502::zpy6502 },       // 0x97 SAX zp,Y (UNDOC)
	{ &cpu_6502::tya6502, &cpu_6502::implied6502 },   // 0x98
	{ &cpu_6502::sta6502, &cpu_6502::absy6502 },	  // 0x99
	{ &cpu_6502::txs6502, &cpu_6502::implied6502 },   // 0x9A
	{ &cpu_6502::shs6502, &cpu_6502::absy6502 },      // 0x9B SHS/TAS abs,Y (UNDOC)
	{ &cpu_6502::stz6502, &cpu_6502::abs6502 },		  // 0x9C (CMOS)
	{ &cpu_6502::sta6502, &cpu_6502::absx6502 },	  // 0x9D
	{ &cpu_6502::stz6502, &cpu_6502::absx6502 },	  // 0x9E (CMOS)
	{ &cpu_6502::ahx6502, &cpu_6502::absy6502 },      // 0x9F AHX abs,Y (UNDOC)
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
	{ &cpu_6502::lxa6502, &cpu_6502::immediate6502 }, // 0xAB LXA/ATX imm (UNDOC)
	{ &cpu_6502::ldy6502, &cpu_6502::abs6502 },       // 0xAC
	{ &cpu_6502::lda6502, &cpu_6502::abs6502 },       // 0xAD
	{ &cpu_6502::ldx6502, &cpu_6502::abs6502 },       // 0xAE
	{ &cpu_6502::lax6502, &cpu_6502::abs6502 },       // 0xAF (UNDOC)
	{ &cpu_6502::bcs6502, &cpu_6502::relative6502 },  // 0xB0
	{ &cpu_6502::lda6502, &cpu_6502::indy6502 },      // 0xB1
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xB2 (CMOS Ind ZP uses this slot usually)
	{ &cpu_6502::lax6502, &cpu_6502::indy6502 },      // 0xB3 (UNDOC)
	{ &cpu_6502::ldy6502, &cpu_6502::zpx6502 },       // 0xB4
	{ &cpu_6502::lda6502, &cpu_6502::zpx6502 },       // 0xB5
	{ &cpu_6502::ldx6502, &cpu_6502::zpy6502 },       // 0xB6
	{ &cpu_6502::lax6502, &cpu_6502::zpy6502 },       // 0xB7 (UNDOC)
	{ &cpu_6502::clv6502, &cpu_6502::implied6502 },   // 0xB8
	{ &cpu_6502::lda6502, &cpu_6502::absy6502 },      // 0xB9
	{ &cpu_6502::tsx6502, &cpu_6502::implied6502 },   // 0xBA
	{ &cpu_6502::las6502, &cpu_6502::absy6502 },      // 0xBB LAS abs,Y (UNDOC)
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
	{ &cpu_6502::axs6502, &cpu_6502::immediate6502 }, // 0xCB AXS/SBX imm (UNDOC)
	{ &cpu_6502::cpy6502, &cpu_6502::abs6502 },		  // 0xCC
	{ &cpu_6502::cmp6502, &cpu_6502::abs6502 },		  // 0xCD
	{ &cpu_6502::dec6502, &cpu_6502::abs6502 },		  // 0xCE
	{ &cpu_6502::dcp6502, &cpu_6502::abs6502 },		  // 0xCF (UNDOC)
	{ &cpu_6502::bne6502, &cpu_6502::relative6502 },  // 0xD0
	{ &cpu_6502::cmp6502, &cpu_6502::indy6502 },	  // 0xD1
	{ &cpu_6502::cmp6502, &cpu_6502::indzp6502 },	  // 0xD2 (CMOS)
	{ &cpu_6502::dcp6502, &cpu_6502::indy6502 },      // 0xD3 DCP (ind),Y (UNDOC)
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xD4
	{ &cpu_6502::cmp6502, &cpu_6502::zpx6502 },		  // 0xD5
	{ &cpu_6502::dec6502, &cpu_6502::zpx6502 },		  // 0xD6
	{ &cpu_6502::dcp6502, &cpu_6502::zpx6502 },		  // 0xD7 (UNDOC)
	{ &cpu_6502::cld6502, &cpu_6502::implied6502 },   // 0xD8
	{ &cpu_6502::cmp6502, &cpu_6502::absy6502 },	  // 0xD9
	{ &cpu_6502::phx6502, &cpu_6502::implied6502 },	  // 0xDA (CMOS)
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
	{ &cpu_6502::sbc6502, &cpu_6502::indzp6502 },	  // 0xF2 (CMOS)
	{ &cpu_6502::isc6502, &cpu_6502::indy6502 },      // 0xF3 ISC (ind),Y (UNDOC)
	{ &cpu_6502::nop6502, &cpu_6502::implied6502 },   // 0xF4
	{ &cpu_6502::sbc6502, &cpu_6502::zpx6502 },		  // 0xF5
	{ &cpu_6502::inc6502, &cpu_6502::zpx6502 },       // 0xF6
	{ &cpu_6502::isc6502, &cpu_6502::zpx6502 },		  // 0xF7 (UNDOC)
	{ &cpu_6502::sed6502, &cpu_6502::implied6502 },   // 0xF8
	{ &cpu_6502::sbc6502, &cpu_6502::absy6502 },      // 0xF9
	{ &cpu_6502::plx6502, &cpu_6502::implied6502 },   // 0xFA (CMOS)
	{ &cpu_6502::isc6502, &cpu_6502::absy6502 },      // 0xFB (UNDOC)
	{ &cpu_6502::nop6502, &cpu_6502::absx6502 },      // 0xFC
	{ &cpu_6502::sbc6502, &cpu_6502::absx6502 },      // 0xFD
	{ &cpu_6502::inc6502, &cpu_6502::absx6502 },      // 0xFE
	{ &cpu_6502::isc6502, &cpu_6502::absx6502 }       // 0xFF (UNDOC)
};

// -----------------------------------------------------------------------------
// Name: init6502
// Purpose: Initializes the 6502 CPU instance and patches the opcode table
//          based on whether we are emulating an NMOS 6502, CMOS 65C02, or NES.
// -----------------------------------------------------------------------------
void cpu_6502::init6502(uint16_t addrmaskval, CpuModel model)
{
	PC = PPC = 0;
	addrmask = addrmaskval;
	_irqPending = 0;
	_irqMode = 0;
	clocktickstotal = 0;
	cpu_model = model;

	// 1. Copy the master table (Contains ALL variants)
	memcpy(opcode_table, initial_opcode_table, sizeof(initial_opcode_table));

	// 2. Prune the table based on architecture type.

	// Case A: CMOS 65C02
	if (cpu_model == CPU_CMOS_65C02)
	{
		// 1. Map CMOS-Specific ALU functions (Decimal Flag fixes)
		static const uint8_t adc_ops[] = { 0x61, 0x65, 0x69, 0x6D, 0x71, 0x72, 0x75, 0x79, 0x7D };
		static const uint8_t sbc_ops[] = { 0xE1, 0xE5, 0xE9, 0xED, 0xF1, 0xF2, 0xF5, 0xF9, 0xFD };

		for (uint8_t op : adc_ops) opcode_table[op].instruction = &cpu_6502::adc65c02;
		for (uint8_t op : sbc_ops) opcode_table[op].instruction = &cpu_6502::sbc65c02;

		// 2. NOP out Undocumented NMOS Instructions (CMOS doesn't support them)
		//    We keep the addressing mode (implied behavior for multi-byte NOPs)
		static const uint8_t nmos_undoc_opcodes[] = {
			0xA3, 0xA7, 0xAB, 0xAF, 0xB3, 0xB7, 0xBF, // LAX
			0x83, 0x87, 0x8F, 0x97,                   // SAX
			0x03, 0x07, 0x0F, 0x13, 0x17, 0x1B, 0x1F, // SLO
			0x23, 0x27, 0x2F, 0x33, 0x37, 0x3B, 0x3F, // RLA
			0x43, 0x47, 0x4F, 0x53, 0x57, 0x5B, 0x5F, // SRE
			0x63, 0x67, 0x6F, 0x73, 0x77, 0x7B, 0x7F, // RRA
			0xC3, 0xC7, 0xCF, 0xD3, 0xD7, 0xDB, 0xDF, // DCP
			0xE3, 0xE7, 0xEF, 0xF3, 0xF7, 0xFB, 0xFF, // ISC
			0x0B, 0x2B, 0x4B, 0x6B, 0x8B, 0x9B, 0xBB, 0xCB // Other
		};

		for (uint8_t op : nmos_undoc_opcodes)
		{
			opcode_table[op].instruction = &cpu_6502::nop6502;
			opcode_table[op].addressing_mode = &cpu_6502::implied6502;
		}

		// Map Rockwell/WDC Bit Manipulation Instructions
		// RMB0-7, SMB0-7, BBR0-7, BBS0-7
		for (int i = 0; i < 8; i++)
		{
			// RMBx (Opcode 07, 17 ... 77)
			int rmb_op = (i << 4) | 0x07;
			opcode_table[rmb_op].instruction = &cpu_6502::rmb_smb_6502;
			opcode_table[rmb_op].addressing_mode = &cpu_6502::zp6502;

			// SMBx (Opcode 87, 97 ... F7)
			int smb_op = (i << 4) | 0x87;
			opcode_table[smb_op].instruction = &cpu_6502::rmb_smb_6502;
			opcode_table[smb_op].addressing_mode = &cpu_6502::zp6502;

			// BBRx (Opcode 0F, 1F ... 7F)
			int bbr_op = (i << 4) | 0x0F;
			opcode_table[bbr_op].instruction = &cpu_6502::bbr_bbs_6502;
			opcode_table[bbr_op].addressing_mode = &cpu_6502::zprel6502;

			// BBSx (Opcode 8F, 9F ... FF)
			int bbs_op = (i << 4) | 0x8F;
			opcode_table[bbs_op].instruction = &cpu_6502::bbr_bbs_6502;
			opcode_table[bbs_op].addressing_mode = &cpu_6502::zprel6502;
		}
		// 1. Map 2-byte NOPs (Immediate Addressing)
		// These are opcodes: 02, 22, 42, 62, 82, C2, E2
		static const uint8_t nop_2byte[] = {0x02, 0x22, 0x42, 0x62, 0x82, 0xC2, 0xE2,	0x44, 0x54, 0xD4, 0xF4 };
		for (uint8_t op : nop_2byte) {
			opcode_table[op].instruction = &cpu_6502::nop6502;
			opcode_table[op].addressing_mode = &cpu_6502::immediate6502; // Consumes 2 bytes (Op + Imm)
		}

		// 2. Map 3-byte NOPs (Absolute/AbsX Addressing)
		// 5C = NOP Abs (3 bytes, 4 cycles)
		opcode_table[0x5C].instruction = &cpu_6502::nop6502;
		opcode_table[0x5C].addressing_mode = &cpu_6502::abs6502;

		// DC = NOP Abs,X (3 bytes, 4 cycles)
		opcode_table[0xDC].instruction = &cpu_6502::nop6502;
		opcode_table[0xDC].addressing_mode = &cpu_6502::absx6502;

		// FC is usually already mapped to NOP+AbsX in your initial table, but safe to enforce:
		opcode_table[0xFC].instruction = &cpu_6502::nop6502;
		opcode_table[0xFC].addressing_mode = &cpu_6502::absx6502;

		// FIX: Map CMOS Zero Page Indirect instructions ($zp)
		// WDC reserved opcodes are one-byte, one-cycle NOPs.
		for (int op = 0; op < 256; ++op) {
			if ((op & 15) == 3 || (op & 15) == 0x0b) {
				opcode_table[op] = {&cpu_6502::nop6502, &cpu_6502::implied6502};
			}
		}
		opcode_table[0x44] = {&cpu_6502::nop6502, &cpu_6502::zp6502};
		for (int op : {0x54, 0xd4, 0xf4}) opcode_table[op] = {&cpu_6502::nop6502, &cpu_6502::zpx6502};
		for (int op : {0x5c, 0xdc, 0xfc}) opcode_table[op] = {&cpu_6502::nop6502, &cpu_6502::abs6502};
		opcode_table[0xcb] = {&cpu_6502::wai6502, &cpu_6502::implied6502};
		opcode_table[0xdb] = {&cpu_6502::stp6502, &cpu_6502::implied6502};
		// Opcodes: 12, 32, 52, 72, 92, B2, D2, F2
		// -----------------------------------------------------------
		opcode_table[0x12].instruction = &cpu_6502::ora6502; opcode_table[0x12].addressing_mode = &cpu_6502::indzp6502;
		opcode_table[0x32].instruction = &cpu_6502::and6502; opcode_table[0x32].addressing_mode = &cpu_6502::indzp6502;
		opcode_table[0x52].instruction = &cpu_6502::eor6502; opcode_table[0x52].addressing_mode = &cpu_6502::indzp6502;
		opcode_table[0x72].instruction = &cpu_6502::adc65c02; opcode_table[0x72].addressing_mode = &cpu_6502::indzp6502;
		opcode_table[0x92].instruction = &cpu_6502::sta6502; opcode_table[0x92].addressing_mode = &cpu_6502::indzp6502;
		opcode_table[0xB2].instruction = &cpu_6502::lda6502; opcode_table[0xB2].addressing_mode = &cpu_6502::indzp6502;
		opcode_table[0xD2].instruction = &cpu_6502::cmp6502; opcode_table[0xD2].addressing_mode = &cpu_6502::indzp6502;
		opcode_table[0xF2].instruction = &cpu_6502::sbc65c02; opcode_table[0xF2].addressing_mode = &cpu_6502::indzp6502;
	}
	// Case B: NMOS 6502 or NES 2A03
	else
	{
		// 1. Map 1-byte CMOS NOPs (Implied)
		// INC/DEC A (1A, 3A), PHY/PLY (5A, 7A), PHX/PLX (DA, FA)
		static const uint8_t nops_1byte[] = {
			0x1A, 0x3A, 0x5A, 0x7A, 0xDA, 0xFA
		};

		for (uint8_t op : nops_1byte)
		{
			opcode_table[op].instruction = &cpu_6502::nop6502;
			opcode_table[op].addressing_mode = &cpu_6502::implied6502;
		}

		// 2. Map 2-byte NMOS NOPs.
		// NOP zp / zp,X: 04, 14, 34, 44, 54, 64, 74, D4, F4
		// NOP #imm:      80, 82, 89, C2, E2  (82/C2/E2 MUST be 2 bytes - as
		// 1-byte NOPs the instruction stream desyncs and code runs wild)
		static const uint8_t nops_2byte[] = {
			0x04, 0x14, 0x34, 0x44, 0x54, 0x64, 0x74, 0xD4, 0xF4
		};
		static const uint8_t nops_imm[] = {
			0x80, 0x82, 0x89, 0xC2, 0xE2
		};

		for (uint8_t op : nops_2byte)
		{
			opcode_table[op].instruction = &cpu_6502::nop6502;
			// Use zp6502 to consume the operand byte (PC+2 total)
			opcode_table[op].addressing_mode = (op & 0x10) ? &cpu_6502::zpx6502 : &cpu_6502::zp6502;
		}
		for (uint8_t op : nops_imm)
		{
			opcode_table[op].instruction = &cpu_6502::nop6502;
			opcode_table[op].addressing_mode = &cpu_6502::immediate6502;
		}

		// 2b. NMOS JAM/KIL opcodes: 02,12,22,32,42,52,62,72,92,B2,D2,F2.
		// Real hardware wedges the CPU; freezing the PC keeps crashed code at
		// the jam point (instead of free-running through memory), matching
		// what a real C64 does when a program dies.
		static const uint8_t kil_ops[] = {
			0x02, 0x12, 0x22, 0x32, 0x42, 0x52,
			0x62, 0x72, 0x92, 0xB2, 0xD2, 0xF2
		};
		for (uint8_t op : kil_ops)
		{
			opcode_table[op].instruction = &cpu_6502::kil6502;
			opcode_table[op].addressing_mode = &cpu_6502::implied6502;
		}

		// 3. Map 3-byte CMOS NOPs (Absolute / AbsX)
		// 0x0C (TSB Abs), 0x1C (TRB Abs), 0x3C (BIT AbsX), 0x7C (JMP Ind,X)
		static const uint8_t nops_3byte[] = {
			0x0C, 0x1C, 0x3C, 0x5C, 0x7C, 0xDC, 0xFC
		};

		for (uint8_t op : nops_3byte)
		{
			opcode_table[op].instruction = &cpu_6502::nop6502;
			// Use abs6502 to consume 2 operand bytes (PC+3 total)
			opcode_table[op].addressing_mode = op == 0x0C ? &cpu_6502::abs6502 : &cpu_6502::absx6502;
		}
		opcode_table[0xEB].instruction = &cpu_6502::sbc6502;
		opcode_table[0xEB].addressing_mode = &cpu_6502::immediate6502;
		// 4. Handle NMOS Specific Undocumented Overwrites
		// NMOS 0x9C is SHY (Abs,X) - not STZ
		opcode_table[0x9C].instruction = &cpu_6502::shy6502;
		opcode_table[0x9C].addressing_mode = &cpu_6502::absx6502;

		// NMOS 0x9E is SHX (Abs,Y) - not STZ
		opcode_table[0x9E].instruction = &cpu_6502::shx6502;
		opcode_table[0x9E].addressing_mode = &cpu_6502::absy6502;
	}

	// 3. Apply NES 2A03 Specific Patches (BCD Disable)
	if (cpu_model == CPU_NES_2A03)
	{
		static const uint8_t adc_ops[] = { 0x61, 0x65, 0x69, 0x6D, 0x71, 0x72, 0x75, 0x79, 0x7D };
		static const uint8_t sbc_ops[] = { 0xE1, 0xE5, 0xE9, 0xED, 0xF1, 0xF2, 0xF5, 0xF9, 0xFD };
		static const uint8_t rra_ops[] = { 0x63, 0x67, 0x6F, 0x73, 0x77, 0x7B, 0x7F };
		static const uint8_t isc_ops[] = { 0xE3, 0xE7, 0xEF, 0xF3, 0xF7, 0xFB, 0xFF };

		for (uint8_t op : adc_ops) opcode_table[op].instruction = &cpu_6502::adc_2a03;
		for (uint8_t op : sbc_ops) opcode_table[op].instruction = &cpu_6502::sbc_2a03;
		for (uint8_t op : rra_ops) opcode_table[op].instruction = &cpu_6502::rra_2a03;
		for (uint8_t op : isc_ops) opcode_table[op].instruction = &cpu_6502::isc_2a03;
	}
}

// -----------------------------------------------------------------------------
// 6502 CPU Constructor.
// -----------------------------------------------------------------------------
cpu_6502::cpu_6502(uint8_t* mem, MemoryReadByte* read_mem, MemoryWriteByte* write_mem, uint16_t addr, int num, CpuModel model)
{
	MEM = mem;
	memory_write = write_mem;
	memory_read = read_mem;
	cpu_num = num;
	log_debug_rw = 1;
	mmem = false;
	debug = 0;

	// Pass the model through to the initialization logic
	init6502(addr, model);
}

// -----------------------------------------------------------------------------
// Get the Total Number of Clock Ticks Executed.
// -----------------------------------------------------------------------------
int cpu_6502::get6502ticks(int reset)
{
	int tmp = clocktickstotal;
	if (reset)
		clocktickstotal = 0;
	return tmp;
}

// -----------------------------------------------------------------------------
// get6502memory
// -----------------------------------------------------------------------------
uint8_t cpu_6502::get6502memory(uint16_t addr)
{
	addr &= addrmask;

	if (cpu_model == CPU_6510 && addr < 2)
	{
		if (addr == 0) return ddr;
		if (addr == 1)
		{
			// Accurate Logic:
			// If Bit is Output (1): Return the Latch value (port_out)
			// If Bit is Input (0):  Return the Pin value (port_in)
			return (port_out & ddr) | (port_in & ~ddr);
		}
	}

	MemoryReadByte* reader = memory_read;
	while (reader->lowAddr != -1)
	{
		if (addr >= reader->lowAddr && addr <= reader->highAddr)
		{
			if (reader->memoryCall)
				return reader->memoryCall(addr - reader->lowAddr, reader);
			else
				return ((const uint8_t*)reader->pUserArea)[addr - reader->lowAddr];
		}
		++reader;
	}

	if (!mmem)
		return MEM[addr];

	if (log_debug_rw)
		LOG_INFO("Warning! Unhandled Read at %x", addr);

	return 0;
}

// -----------------------------------------------------------------------------
// put6502memory
// -----------------------------------------------------------------------------
void cpu_6502::put6502memory(uint16_t addr, uint8_t byte)
{
	addr &= addrmask;

	if (cpu_model == CPU_6510 && addr < 2)
	{
		uint8_t old_ddr = ddr;
		uint8_t old_port = port_out;

		if (addr == 0) ddr = byte;
		if (addr == 1) port_out = byte;

		check_and_notify_6510(old_ddr, old_port);
		return;
	}

	MemoryWriteByte* writer = memory_write;
	while (writer->lowAddr != -1)
	{
		if (addr >= writer->lowAddr && addr <= writer->highAddr)
		{
			if (writer->memoryCall)
				writer->memoryCall(addr - writer->lowAddr, byte, writer);
			else
				((uint8_t*)writer->pUserArea)[addr - writer->lowAddr] = byte;
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

void cpu_6502::check_and_notify_6510(uint8_t old_ddr, uint8_t old_port)
{
	if (port_cb)
	{
		// Notify on any change of either register: what the board sees also
		// depends on the input bits (a C64 reads them as pulled high), so a
		// DDR change with the same output latch still changes the banking.
		if (old_ddr != ddr || old_port != port_out) {
			port_cb(port_out, ddr);
		}
	}
}

// -----------------------------------------------------------------------------
// Callback Hook After CLI Instruction.
// -----------------------------------------------------------------------------
void cpu_6502::check_interrupts_after_cli()
{
	// block IRQ recognition for exactly the next instruction
	irq_inhibit_one = 2;
}

// -----------------------------------------------------------------------------
// Check Whether a Pending IRQ Should Be Taken Now.
// -----------------------------------------------------------------------------
void cpu_6502::maybe_take_irq()
{
	if (_irqPending && !(P & F_I))
		irq6502(0);
}

// -----------------------------------------------------------------------------
// Clear the Pending IRQ Flag.
// -----------------------------------------------------------------------------
void cpu_6502::m6502clearpendingint()
{
	_irqPending = 0;
}

// -----------------------------------------------------------------------------
// Get the value of a CPU register by register index.
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
// -----------------------------------------------------------------------------
uint16_t cpu_6502::get_pc()
{
	return PC;
}

// -----------------------------------------------------------------------------
// Get the previous value of the program counter (PPC).
// -----------------------------------------------------------------------------
uint16_t cpu_6502::get_ppc()
{
	return PPC;
}

// -----------------------------------------------------------------------------
// Set the program counter (PC) to a new address.
// -----------------------------------------------------------------------------
void cpu_6502::set_pc(uint16_t pc)
{
	PC = pc;
}

// -----------------------------------------------------------------------------
// Reset the CPU to its initial state.
// -----------------------------------------------------------------------------
void cpu_6502::reset6502()
{
	LOG_INFO("6502 Reset");
	run_state = RunState::Running;

	A = X = Y = 0;
	P = F_T | F_I | F_Z;
	_irqPending = 0;
	kil_logged = false;
	irq_inhibit_one = 0;
	irq_polled = nmi_polled = nmi_latched = nmi_immediate = false;
	irq_history = nmi_history = 0;
	in_instruction = false;

	PC = PPC = 0;
	_irqPending = 0;
	clocktickstotal = 0;
	clockticks6502 = 0;   // untimed host initialization; reset_bus6502() is the timed one

	S = 0xFF;

	PC = get6502memory(0xFFFC & addrmask);
	PC |= get6502memory(0xFFFD & addrmask) << 8;

	LOG_INFO("reset: PC is %X", PC);

	if (cpu_model == CPU_6510) {
		ddr = 0x00;       // Hardware resets to 0 (Input)
		port_out = 0x00;
		port_in = 0xFF;   // Inputs usually pulled high

		// Note: The C64 KERNAL ROM will write 0x2F and 0x37 shortly after boot.
		// We don't need to force it here, let the ROM do it.
	}
}

// -----------------------------------------------------------------------------
// Internal Helper: Actually take the interrupt, push stack, change PC.
// -----------------------------------------------------------------------------
void cpu_6502::execute_irq()
{
	if (run_state == RunState::Stopped || run_state == RunState::Jammed) return;
	run_state = RunState::Running;
	interrupt_entry(0xFFFE);


	if (_irqMode == IRQ_PULSE)
	{
		_irqPending = 0;
	}
}

// -----------------------------------------------------------------------------
// Assert the IRQ Line.
// -----------------------------------------------------------------------------
void cpu_6502::irq6502(int irqmode)
{
	_irqPending = 1;
	_irqMode = irqmode;
}

void cpu_6502::set_irq_line(bool asserted)
{
	pin_irq_enabled = true;
	irq_line = asserted;
	if (!in_instruction && asserted && !(P & F_I)) irq_polled = true;
}

void cpu_6502::set_nmi_line(bool asserted)
{
	if (asserted && !nmi_line) {
		nmi_latched = true;
		if (!in_instruction) nmi_polled = true;
	}
	nmi_line = asserted;
}

void cpu_6502::poll_interrupt_pins()
{
	// Poll before the last bus cycle. A taken same-page NMOS branch retains
	// its earlier polling point. Samples contain I before that cycle's effects.
	const bool branch = (opcode & 0x1f) == 0x10;
	const unsigned mask = branch && cycles_emitted == 3 ? 4 : 2;
	irq_polled = (irq_history & mask) != 0;
	nmi_polled = nmi_latched && (nmi_immediate || (nmi_history & mask) != 0);
}

// -----------------------------------------------------------------------------
// Execute a non-maskable interrupt (NMI). See the header for the boundary
// versus busy contract.
// -----------------------------------------------------------------------------
void cpu_6502::nmi6502()
{
	if (run_state == RunState::Stopped || run_state == RunState::Jammed) return;
	if (in_instruction) {
		// Latched now; nmi_immediate keeps poll_interrupt_pins() from dropping
		// it, and nmi_polled covers a request raised after the poll.
		nmi_latched = nmi_immediate = nmi_polled = true;
		return;
	}
	take_nmi();
#ifdef USING_AAE_EMU
	timer_update(clockticks6502, cpu_num);
#endif // USING_AAE_EMU
}

void cpu_6502::take_nmi()
{
	run_state = RunState::Running;
	const bool was_in_instruction = in_instruction;
	in_instruction = true;
	nmi_latched = nmi_polled = nmi_immediate = false;
	cycles_emitted = 0;
	elapsed_cycles = 0;
	interrupt_entry(0xFFFA);
	finish_cycle_report();
	in_instruction = was_in_instruction;
}

void cpu_6502::reset_bus6502()
{
	run_state = RunState::Running;
	cycles_emitted = 0;
	elapsed_cycles = 0;
	irq_inhibit_one = 0;
	irq_polled = nmi_polled = nmi_latched = false;
	kil_logged = false;
	(void)fetch_cycle(PC);
	(void)fetch_cycle(PC);
	for (int i = 0; i < 3; ++i) {
		(void)read_cycle(BASE_STACK + S, true);
		--S;
	}
	P |= F_I | F_T;
	if (cpu_model == CPU_CMOS_65C02) P &= ~F_D;
	const uint8_t lo = read_cycle(0xfffc);
	const uint8_t hi = read_cycle(0xfffd);
	PC = lo | (hi << 8);
	finish_cycle_report();
}

// IRQ/NMI discard two reads at the current PC, then push PC/status and
// fetch the vector. Call only at an instruction boundary, never from cycle_cb.
void cpu_6502::interrupt_entry(uint16_t vector)
{
	(void)fetch_cycle(PC);
	(void)fetch_cycle(PC);
	push_cycle(static_cast<uint8_t>(PC >> 8));
	push_cycle(static_cast<uint8_t>(PC));
	if (vector == 0xfffe && cpu_model != CPU_CMOS_65C02 && nmi_latched) {
		vector = 0xfffa;
		nmi_latched = nmi_polled = nmi_immediate = false;
	}
	push_cycle((P | F_T) & ~F_B);
	P |= F_I;
	if (cpu_model == CPU_CMOS_65C02) P &= ~F_D;
	const uint8_t lo = read_cycle(vector);
	const uint8_t hi = read_cycle(static_cast<uint16_t>(vector + 1));
	PC = lo | (hi << 8);
}

// Instruction-only helpers: keep the public raw stack API untimed for hosts.
void cpu_6502::push_cycle(uint8_t value)
{
	write_cycle(BASE_STACK + S, value, true);
	--S;
}

uint8_t cpu_6502::pull_cycle()
{
	++S;
	return read_cycle(BASE_STACK + S, true);
}

// -----------------------------------------------------------------------------
// Push a 16-bit value to the 6502 stack (high byte first).
// -----------------------------------------------------------------------------
void cpu_6502::push16(uint16_t val)
{
	stack_write(BASE_STACK + S, (val >> 8) & 0xFF);
	stack_write(BASE_STACK + ((S - 1) & 0xFF), val & 0xFF);
	S -= 2;
}

// -----------------------------------------------------------------------------
// Push an 8-bit value to the 6502 stack.
// -----------------------------------------------------------------------------
void cpu_6502::push8(uint8_t val)
{
	stack_write(BASE_STACK + S--, val);
}

// -----------------------------------------------------------------------------
// Pull a 16-bit value from the 6502 stack (low byte first).
// -----------------------------------------------------------------------------
uint16_t cpu_6502::pull16()
{
	uint16_t val = stack_read(BASE_STACK + ((S + 1) & 0xFF)) |
		(static_cast<uint16_t>(stack_read(BASE_STACK + ((S + 2) & 0xFF))) << 8);
	S += 2;
	return val;
}

// -----------------------------------------------------------------------------
// Pull an 8-bit value from the 6502 stack.
// -----------------------------------------------------------------------------
uint8_t cpu_6502::pull8()
{
	return stack_read(BASE_STACK + ++S);
}

// -----------------------------------------------------------------------------
// Execute Instructions Until the Specified Timer Threshold is Reached.
// Returns the Number of Cycles Executed.
// -----------------------------------------------------------------------------
int cpu_6502::exec6502(int timerTicks)
{
	int cycles = 0;
	while (cycles < timerTicks) {
		const int step = step6502();
		// A parked CPU reports zero CPU cycles but one elapsed machine cycle,
		// so a cycle-budget host (AAE's cpu_run) still makes progress.
		cycles += step ? step : 1;
	}
	return cycles;
}

// -----------------------------------------------------------------------------
// Cycle primitives always count; an optional callback advances the host machine.
// -----------------------------------------------------------------------------
inline void cpu_6502::wait_for_bus_cycle()
{
	if (cycle_cb) {
		bool available;
		do {
			++elapsed_cycles;
			available = cycle_cb(cycle_user);
		} while (!available);
	} else ++elapsed_cycles;
	++cycles_emitted;
	++clocktickstotal;
	irq_history = static_cast<uint16_t>((irq_history << 1) | (irq_line && !(P & F_I)));
	nmi_history = static_cast<uint16_t>((nmi_history << 1) | (nmi_latched ? 1 : 0));
}

// Wait states from a memory handler (see cpu_6502.h).  The held cycles are
// dead bus cycles: they are added to this instruction's emitted count, so
// finish_cycle_report() hands them to the scheduler and the AAE timers like
// any other cycle, and to clocktickstotal, which get6502ticks() exposes to
// the POKEY adapter's catch-up before the access completes.  The cycle
// callback, when a machine has one, already models its own stalls and is
// not called here.
void cpu_6502::bus_wait(int cycles)
{
	if (cycles <= 0) return;
	cycles_emitted += cycles;
	elapsed_cycles += (uint64_t)cycles;
	clocktickstotal += cycles;
}

uint8_t cpu_6502::stack_read(uint16_t addr)
{
	return direct_stack_page ? MEM[addr & addrmask] : get6502memory(addr);
}

uint8_t cpu_6502::readop(uint16_t addr)
{
	return use_handler_for_opfetch ? get6502memory(addr) : MEM[addr & addrmask];
}

uint8_t cpu_6502::fetch_cycle(uint16_t addr)
{
	wait_for_bus_cycle();
	const uint8_t result = readop(addr);
	if (bus_cb) bus_cb(result, bus_user);
	return result;
}

uint8_t cpu_6502::operand_cycle()
{
	return opcode_table[opcode].addressing_mode == &cpu_6502::immediate6502
		? fetch_cycle(savepc) : read_cycle(savepc);
}

void cpu_6502::stack_write(uint16_t addr, uint8_t value)
{
	if (direct_stack_page) MEM[addr & addrmask] = value;
	else put6502memory(addr, value);
}

inline uint8_t cpu_6502::read_cycle(uint16_t addr, bool stack)
{
	wait_for_bus_cycle();
	const uint8_t result = stack ? stack_read(addr) : get6502memory(addr);
	if(bus_cb) bus_cb(result,bus_user);
	return result;
}

inline void cpu_6502::write_cycle(uint16_t addr, uint8_t v, bool stack)
{
	// A write cycle ignores RDY - HRM 4.9: "The 6502 does not respond to RDY
	// during a write cycle and therefore always performs this write on the next
	// available cycle regardless." The machine still gets the tick, and is told
	// this one is a write so it can decide (ANTIC DMA halts the CPU on any
	// cycle; a WSYNC wait does not stop a write).
	cycle_is_write = true;
	wait_for_bus_cycle();
	cycle_is_write = false;
	if (stack) stack_write(addr, v);
	else put6502memory(addr, v);
	if(bus_cb) bus_cb(v,bus_user);
}

inline void cpu_6502::idle_cycle()
{
	// An NMOS 6502 "internal" cycle is still a (dummy) read on the bus, so
	// ANTIC's HALT and a WSYNC RDY wait stop it like any other read (HRM 2.5:
	// "these dead cycles cannot be overlapped by DMA - the CPU must still be
	// halted"). Letting it through made every refresh cycle that landed on a
	// PHA/PLA internal cycle vanish, so long cycle-counted sequences ran
	// several cycles early (acid800 gtia_pmretrigger).
	(void)fetch_cycle(PC);
}

// -----------------------------------------------------------------------------
// Execute a Single 6502 Instruction.
// -----------------------------------------------------------------------------
void cpu_6502::finish_cycle_report()
{
	CycleReport report;
	report.cpu_cycles = cycles_emitted;
	report.elapsed_cycles = elapsed_cycles;
	report.stall_cycles = elapsed_cycles - cycles_emitted;
	report.bus_timing_active = cycle_cb != nullptr;
	clockticks6502 = cycles_emitted;   // the step's cycle count is the emitted bus cycles
	last_cycle_report = report;
}

int cpu_6502::step6502()
{
	if (run_state == RunState::Waiting && (irq_line || _irqPending || nmi_latched)) {
		run_state = RunState::Running;
		nmi_polled = nmi_latched;
		if (irq_line && !(P & F_I)) irq_polled = true;
	}
	if (run_state != RunState::Running) {
		// The host must keep running while the CPU is parked. No instruction
		// or memory transfer completes, and a denied bus does not stop the host.
		// One machine cycle still passes: it is counted, and the AAE timers run
		// so the interrupt that ends a WAI can arrive.
		in_instruction = true;
		if (cycle_cb) (void)cycle_cb(cycle_user);
		++clocktickstotal;
		last_cycle_report = {};
		last_cycle_report.elapsed_cycles = 1;
		last_cycle_report.bus_timing_active = cycle_cb != nullptr;
#ifdef USING_AAE_EMU
		timer_update(1, cpu_num);
#endif // USING_AAE_EMU
		in_instruction = false;
		return 0;
	}
	clockticks6502 = 0;
	cycles_emitted = 0;
	elapsed_cycles = 0;
	irq_history = nmi_history = 0;
	in_instruction = true;

	const bool interrupts_allowed = (irq_inhibit_one == 0) && !(P & F_I);

	if (nmi_polled) {
		take_nmi();
	}
	else if ((pin_irq_enabled && irq_polled) || (_irqPending && interrupts_allowed)) {
		irq_polled = false;
		execute_irq();

		// Cycle-accurate mode: advance the world for the IRQ's cycles too.
		finish_cycle_report();

		if (irq_inhibit_one > 0) irq_inhibit_one--;
	}
	else {
		// Normal Instruction Fetch
		PPC = PC;
		opcode = fetch_cycle(PC++);
		P |= F_T;

		if (debug) {
			int bytes = 0;
			std::string op = disassemble(PC - 1, &bytes);
			LOG_INFO("%04X: %-20s A:%02X X:%02X Y:%02X S:%02X P:%02X",
				PC - 1, op.c_str(), A, X, Y, S, P);
		}

		(this->*opcode_table[opcode].addressing_mode)();
		(this->*opcode_table[opcode].instruction)();
		if (skip_poll) { skip_poll = false; irq_polled = nmi_polled = false; }
		else poll_interrupt_pins();
		finish_cycle_report();

		if (irq_inhibit_one > 0)
			irq_inhibit_one--;
	}

	// Single accounting point for every path. Copy the result first: a timer
	// callback may call nmi6502()/irq6502(), which only latch while
	// in_instruction is still set, so this step's count survives.
	const int ran = clockticks6502;
#ifdef USING_AAE_EMU
	timer_update(ran, cpu_num);
#endif // USING_AAE_EMU

	if (clocktickstotal > 0x0FFFFFFF)
		clocktickstotal = 0;

	in_instruction = false;
	return ran;
}

// -----------------------------------------------------------------------------
// Addressing Modes
// -----------------------------------------------------------------------------
void cpu_6502::abs6502()
{
	uint8_t lo = fetch_cycle(PC);
	uint8_t hi = fetch_cycle(PC + 1);
	savepc = lo | (hi << 8);
	PC += 2;
}

// JSR is the one absolute-addressed instruction that does NOT fetch both
// operand bytes up front. Its bus sequence is opcode, ADL, an internal cycle
// that reads the stack, push PCH, push PCL, ADH - the high byte arrives on
// cycle 6, after the pushes. Fetch only the low byte here and let jsr6502()
// finish in order, so each of the six cycles is the bus cycle it really is
// (which decides whether ANTIC's RDY can halt it - see read_cycle()).
void cpu_6502::abs_jsr6502()
{
	savepc = fetch_cycle(PC);        // ADL only; PC still points at it
}

void cpu_6502::immediate6502()
{
	savepc = PC++;
}

void cpu_6502::implied6502()
{
	if (cpu_model == CPU_CMOS_65C02 && opcode != 0xea &&
		opcode_table[opcode].instruction == &cpu_6502::nop6502) return;
    // NMOS implied instructions read the next opcode without incrementing
    // PC on their second cycle. This is a read bus cycle, so RDY/ANTIC
    // refresh can stall it (not an unstalled trailing idle cycle).
    (void)fetch_cycle(PC);
}

void cpu_6502::relative6502()
{
	savepc = fetch_cycle(PC++);
	if (savepc & 0x80)
		savepc |= 0xFF00;
}

void cpu_6502::indirect6502()
{
	const uint8_t ptr_lo = fetch_cycle(PC++);
	const uint8_t ptr_hi = fetch_cycle(PC++);
	uint16_t addr_ptr = ptr_lo | (ptr_hi << 8);
	uint16_t lo = addr_ptr;
	uint16_t hi = addr_ptr + 1;

	// Handle Page Boundary Bug for NMOS only (NES is also NMOS-based)
	if (cpu_model != CPU_CMOS_65C02 && (lo & 0x00FF) == 0x00FF)
	{
		hi = lo & 0xFF00; // Wrap to beginning of page
	}
	const uint8_t target_lo = read_cycle(lo);
	// CMOS first reads the NMOS wrapped high-byte address, then corrects it.
	// On a non-crossing pointer this is simply two reads of the same address.
	uint8_t target_hi = read_cycle(cpu_model == CPU_CMOS_65C02 ? ((lo & 0xff00) | (hi & 0xff)) : hi);
	if (cpu_model == CPU_CMOS_65C02) target_hi = read_cycle(hi);
	savepc = target_lo | (target_hi << 8);
}

bool cpu_6502::indexed_access_is_read() const
{
	const auto instruction = opcode_table[opcode].instruction;
	return instruction != &cpu_6502::sta6502 && instruction != &cpu_6502::stz6502 &&
		instruction != &cpu_6502::shs6502 && instruction != &cpu_6502::shx6502 &&
		instruction != &cpu_6502::shy6502 && instruction != &cpu_6502::ahx6502 &&
		instruction != &cpu_6502::asl6502 && instruction != &cpu_6502::lsr6502 &&
		instruction != &cpu_6502::rol6502 && instruction != &cpu_6502::ror6502 &&
		instruction != &cpu_6502::inc6502 && instruction != &cpu_6502::dec6502 &&
		instruction != &cpu_6502::slo6502 && instruction != &cpu_6502::sre6502 &&
		instruction != &cpu_6502::rla6502 && instruction != &cpu_6502::rra6502 &&
		instruction != &cpu_6502::dcp6502 && instruction != &cpu_6502::isc6502 &&
		instruction != &cpu_6502::rra_2a03 && instruction != &cpu_6502::isc_2a03;
}

void cpu_6502::absx6502()
{
	uint8_t lo = fetch_cycle(PC);
	uint8_t hi = fetch_cycle(PC + 1);
	uint16_t base = lo | (hi << 8);
	uint16_t addr = base + X;
	bool crossed = ((base ^ addr) & 0xFF00) != 0;
	const auto ins = opcode_table[opcode].instruction;
	const bool cmos_shift = cpu_model == CPU_CMOS_65C02 && (ins == &cpu_6502::asl6502 || ins == &cpu_6502::lsr6502 || ins == &cpu_6502::rol6502 || ins == &cpu_6502::ror6502);
	if (indexed_access_is_read() || cmos_shift) {
		if (crossed) { read_cycle(cpu_model == CPU_CMOS_65C02 ? static_cast<uint16_t>(PC + 1) : ((base & 0xFF00) | (addr & 0x00FF))); }
	} else {
		read_cycle(cpu_model == CPU_CMOS_65C02 ? static_cast<uint16_t>(PC + 1) : ((base & 0xFF00) | (addr & 0x00FF)));   // store/RMW: always a dummy read
	}
	savepc = addr;
	PC += 2;
}

void cpu_6502::absy6502()
{
	uint8_t lo = fetch_cycle(PC);
	uint8_t hi = fetch_cycle(PC + 1);
	uint16_t base = lo | (hi << 8);
	uint16_t addr = base + Y;
	bool crossed = ((base ^ addr) & 0xFF00) != 0;
	if (indexed_access_is_read()) {
		if (crossed) { read_cycle(cpu_model == CPU_CMOS_65C02 ? static_cast<uint16_t>(PC + 1) : ((base & 0xFF00) | (addr & 0x00FF))); }
	} else {
		read_cycle(cpu_model == CPU_CMOS_65C02 ? static_cast<uint16_t>(PC + 1) : ((base & 0xFF00) | (addr & 0x00FF)));   // store/RMW: always a dummy read
	}
	savepc = addr;
	PC += 2;
}

void cpu_6502::zp6502()
{
	savepc = fetch_cycle(PC++);
}

void cpu_6502::zpx6502()
{
	uint8_t zp = fetch_cycle(PC++);
	read_cycle(zp);                  // dummy read at the un-indexed zp (index-add cycle)
	savepc = (zp + X) & 0xFF;
}

void cpu_6502::zpy6502()
{
	uint8_t zp = fetch_cycle(PC++);
	read_cycle(zp);
	savepc = (zp + Y) & 0xFF;
}

void cpu_6502::indx6502()
{
	uint8_t zp = fetch_cycle(PC++);              // fetch zero-page pointer base
	read_cycle(zp);                             // dummy read during index add
	uint8_t ptr = (zp + X) & 0xFF;
	uint8_t lo = read_cycle(ptr);
	uint8_t hi = read_cycle((ptr + 1) & 0xFF);
	savepc = lo | (hi << 8);
}

void cpu_6502::indy6502()
{
	uint8_t zp = fetch_cycle(PC++);              // fetch zero-page pointer
	uint8_t lo = read_cycle(zp);                // pointer low
	uint8_t hi = read_cycle((zp + 1) & 0xFF);   // pointer high (zero-page wraparound)
	uint16_t base = lo | (hi << 8);
	uint16_t addr = base + Y;
	bool crossed = ((base ^ addr) & 0xFF00) != 0;
	if (indexed_access_is_read()) {
		if (crossed) { read_cycle(cpu_model == CPU_CMOS_65C02 ? static_cast<uint16_t>(PC - 1) : ((base & 0xFF00) | (addr & 0x00FF))); }
	} else {
		read_cycle(cpu_model == CPU_CMOS_65C02 ? static_cast<uint16_t>(PC - 1) : ((base & 0xFF00) | (addr & 0x00FF)));   // store/RMW: always a dummy read
	}
	savepc = addr;
}

void cpu_6502::indabsx6502()
{
	const uint16_t operand = PC;
	const uint8_t lo = fetch_cycle(PC++);
	const uint8_t hi = fetch_cycle(PC++);
	help = lo | (hi << 8);
	help += X;
	(void)fetch_cycle(operand);
	const uint8_t target_lo = read_cycle(help);
	const uint8_t target_hi = read_cycle(static_cast<uint16_t>(help + 1));
	savepc = target_lo | (target_hi << 8);
}

void cpu_6502::indzp6502()
{
	value = fetch_cycle(PC++);
	savepc = read_cycle(value) | (read_cycle((value + 1) & 0xFF) << 8);
}

// -----------------------------------------------------------------------------
// Zero Page Relative (65C02 Bit Branching)
// Fetches ZP Address into 'help' and Relative Offset into 'savepc'
// -----------------------------------------------------------------------------
void cpu_6502::zprel6502()
{
	help = fetch_cycle(PC++); // Zero Page Address
	value = read_cycle(help);
	(void)read_cycle(help);
	savepc = fetch_cycle(PC++); // Relative Offset
	if (savepc & 0x80)
		savepc |= 0xFF00; // Sign extend
}

// -----------------------------------------------------------------------------
// NES 2A03 Arithmetic Implementations (Binary Only)
// -----------------------------------------------------------------------------
void cpu_6502::adc_2a03()
{
	const uint8_t m = operand_cycle();
	const int     cin = (P & F_C) ? 1 : 0;
	const uint16_t sum = (uint16_t)A + m + cin;
	const uint8_t  bin = (uint8_t)sum;

	P &= ~(F_V | F_C);
	if ((~(A ^ m) & (A ^ bin) & 0x80) != 0) P |= F_V;
	if (sum & 0x0100) P |= F_C;

	A = bin;
	set_nz(A);
}

void cpu_6502::sbc_2a03()
{
	const uint8_t m = operand_cycle();
	const int     cin = (P & F_C) ? 1 : 0;
	const uint16_t diff = (uint16_t)A - m - (1 - cin);
	const uint8_t  bin = (uint8_t)diff;

	P &= ~(F_V | F_C);
	if (((A ^ m) & (A ^ bin) & 0x80) != 0) P |= F_V;
	if (!(diff & 0x0100)) P |= F_C;

	A = bin;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// CMOS 65C02 Arithmetic Implementations (Decimal Flag Fix)
// -----------------------------------------------------------------------------
void cpu_6502::adc65c02()
{
	const uint8_t m = operand_cycle();
	if (P & F_D) {
		(void)read_cycle(opcode == 0x69 ? 0x7f : savepc);
	}
	adc_nmos_value(m);
	set_nz(A); // CMOS observes the corrected result.
}

void cpu_6502::sbc65c02()
{
	const uint8_t m = operand_cycle();
	const int     cin = (P & F_C) ? 1 : 0;
	const uint16_t diff = (uint16_t)A - m - (1 - cin);
	const uint8_t  bin = (uint8_t)diff;

	P &= ~(F_V | F_C);
	if (((A ^ m) & (A ^ bin) & 0x80) != 0) P |= F_V;

	if (P & F_D)
	{
		(void)read_cycle(opcode == 0xe9 ? 0x00 : savepc);
		uint16_t dec = diff;
		const int lo_raw = (int)(A & 0x0F) - (int)(m & 0x0F) - (1 - cin);
		const int lo_borr = (lo_raw < 0) ? 1 : 0;
		if (lo_borr) dec -= 0x06;

		int hi = (int)(A >> 4) - (int)(m >> 4) - lo_borr;
		if (hi < 0) { dec -= 0x60; P &= ~F_C; }
		else { P |= F_C; }

		A = (uint8_t)dec;
		set_nz(A);
	}
	else
	{
		if (!(diff & 0x0100)) P |= F_C;
		A = bin;
		set_nz(A);
	}
}

// -----------------------------------------------------------------------------
// Standard NMOS Arithmetic Implementations
// -----------------------------------------------------------------------------
void cpu_6502::adc6502()
{
	adc_nmos_value(operand_cycle());
}

// Decimal logic reused from C:/Source2026/shared/ref6502/ref6502.c op_adc/op_sbc.
// NMOS N/V use the low-nibble-corrected ADC intermediate, Z the binary sum.
void cpu_6502::adc_nmos_value(uint8_t m)
{
	const uint8_t a = A;
	const unsigned cin = (P & F_C) ? 1 : 0;
	const unsigned binary = a + m + cin;
	unsigned result = binary;
	if (P & F_D) {
		unsigned low = (a & 15) + (m & 15) + cin;
		if (low >= 10) low = ((low + 6) & 15) + 16;
		result = (a & 0xf0) + (m & 0xf0) + low;
	}
	P &= ~(F_N | F_V | F_Z | F_C);
	if (static_cast<uint8_t>(binary) == 0) P |= F_Z;
	if (result & 0x80) P |= F_N;
	if ((~(a ^ m) & (a ^ result) & 0x80) != 0) P |= F_V;
	if ((P & F_D) && result >= 0xa0) result += 0x60;
	if (result >= 0x100) P |= F_C;
	A = static_cast<uint8_t>(result);
}

void cpu_6502::sbc_nmos_value(uint8_t m)
{
	const uint8_t a = A;
	const int borrow = (P & F_C) ? 0 : 1;
	const int binary = a - m - borrow;
	P &= ~(F_V | F_C);
	if (binary >= 0) P |= F_C;
	if (((a ^ m) & (a ^ binary) & 0x80) != 0) P |= F_V;
	set_nz(static_cast<uint8_t>(binary));
	int result = binary;
	if (P & F_D) {
		int low = (a & 15) - (m & 15) - borrow;
		if (low < 0) low = ((low - 6) & 15) - 16;
		result = (a & 0xf0) - (m & 0xf0) + low;
		if (result < 0) result -= 0x60;
	}
	A = static_cast<uint8_t>(result);
}

void cpu_6502::sbc6502()
{
	sbc_nmos_value(operand_cycle());
}

// -----------------------------------------------------------------------------
// Bitwise AND
// Performs a logical AND between the accumulator (A) and memory.
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::and6502()
{
	value = operand_cycle();
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
	A ^= operand_cycle();
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Bitwise OR (ORA)
// Performs a logical inclusive OR between the accumulator (A) and memory.
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::ora6502()
{
	A |= operand_cycle();
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
	value = operand_cycle();

	// 65C02 Specific: Immediate Mode ($89) ONLY touches the Z flag.
	// All other modes (ZeroPage, Absolute, etc) touch N, V, and Z.
	if (cpu_model == CPU_CMOS_65C02 && opcode == 0x89)
	{
		// Immediate mode: Only update Z
		set_z(A & value);
	}
	else
	{
		// Standard behavior (NMOS and CMOS non-immediate)
		// Clear N, V, Z
		P &= ~(F_N | F_V | F_Z);

		// Set N and V from bits 7 and 6 of memory
		P |= value & (F_N | F_V);

		// Set Z if (A & value) == 0
		if ((A & value) == 0)
			P |= F_Z;
	}
}
// -----------------------------------------------------------------------------
// Compare (CMP)
// Compares the accumulator (A) with memory and sets flags as if performing
// A - M, without storing the result.
// Updates carry, zero, and negative flags.
// -----------------------------------------------------------------------------
void cpu_6502::cmp6502()
{
	value = operand_cycle();
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
	value = operand_cycle();

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
	value = operand_cycle();

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
	A = operand_cycle();
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Load X Register
// Loads a value from memory into the X register and sets the zero and
// negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::ldx6502()
{
	X = operand_cycle();
	set_nz(X);
}

// -----------------------------------------------------------------------------
// Load Y Register
// Loads a value from memory into the Y register and sets the zero and
// negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::ldy6502()
{
	Y = operand_cycle();
	set_nz(Y);
}

// -----------------------------------------------------------------------------
// Store Accumulator
// Stores the value of the accumulator (A) into memory.
// -----------------------------------------------------------------------------
void cpu_6502::sta6502()
{
	write_cycle(savepc, A);
}

// -----------------------------------------------------------------------------
// Store X Register
// Stores the value of the X register into memory.
// -----------------------------------------------------------------------------
void cpu_6502::stx6502()
{
	write_cycle(savepc, X);
}

// -----------------------------------------------------------------------------
// Store Y Register
// Stores the value of the Y register into memory.
// -----------------------------------------------------------------------------
void cpu_6502::sty6502()
{
	write_cycle(savepc, Y);
}

// -----------------------------------------------------------------------------
// Store Zero (65C02)
// Stores a zero value into memory. This is a 65C02 extension instruction.
// -----------------------------------------------------------------------------
void cpu_6502::stz6502()
{
	write_cycle(savepc, 0);
}

// -----------------------------------------------------------------------------
// Increment Memory
// Increments the value at the specified memory location and sets the zero and
// negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::inc6502()
{
	uint8_t m = operand_cycle();
	if (cpu_model == CPU_CMOS_65C02) (void)operand_cycle();
	else write_cycle(savepc, m);              // RMW dummy write of the unmodified value
	uint8_t result = m + 1;
	write_cycle(savepc, result);
	set_nz(result);
}

// -----------------------------------------------------------------------------
// Decrement Memory
// Decrements the value at the specified memory location and sets the zero and
// negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::dec6502()
{
	uint8_t m = operand_cycle();
	if (cpu_model == CPU_CMOS_65C02) (void)operand_cycle();
	else write_cycle(savepc, m);
	uint8_t result = m - 1;
	write_cycle(savepc, result);
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
	value = operand_cycle();
	if (cpu_model == CPU_CMOS_65C02) (void)operand_cycle();
	else write_cycle(savepc, value);
	P = (P & ~F_C) | ((value >> 7) & F_C);
	value <<= 1;
	write_cycle(savepc, value);
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
	value = operand_cycle();
	if (cpu_model == CPU_CMOS_65C02) (void)operand_cycle();
	else write_cycle(savepc, value);
	P = (P & ~F_C) | (value & F_C);
	value >>= 1;
	write_cycle(savepc, value);
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
	value = operand_cycle();
	if (cpu_model == CPU_CMOS_65C02) (void)operand_cycle();
	else write_cycle(savepc, value);
	P = (P & ~F_C) | ((value >> 7) & F_C);
	value = (value << 1) | saveflags;
	write_cycle(savepc, value);
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
	value = operand_cycle();
	if (cpu_model == CPU_CMOS_65C02) (void)operand_cycle();
	else write_cycle(savepc, value);
	P = (P & ~F_C) | (value & F_C);
	value >>= 1;
	if (saveflags) value |= F_N;
	write_cycle(savepc, value);
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
		idle_cycle();                                   // taken: dummy opcode fetch
		oldpc = PC;
		PC += (int8_t)savepc;
		if ((oldpc ^ PC) & 0xFF00) (void)read_cycle((oldpc&0xff00)|(PC&0xff)); // page-fix dummy read
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
		idle_cycle();                                   // taken: dummy opcode fetch
		oldpc = PC;
		PC += (int8_t)(savepc);
		if ((oldpc ^ PC) & 0xFF00) (void)read_cycle((oldpc&0xff00)|(PC&0xff)); // page-fix dummy read
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
		idle_cycle();                                   // taken: dummy opcode fetch
		oldpc = PC;
		PC += (int8_t)(savepc);
		if ((oldpc ^ PC) & 0xFF00) (void)read_cycle((oldpc&0xff00)|(PC&0xff)); // page-fix dummy read
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
		idle_cycle();                                   // taken: dummy opcode fetch
		oldpc = PC;
		PC += (int8_t)(savepc);
		if ((oldpc ^ PC) & 0xFF00) (void)read_cycle((oldpc&0xff00)|(PC&0xff)); // page-fix dummy read
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
		idle_cycle();                                   // taken: dummy opcode fetch
		oldpc = PC;
		PC += (int8_t)(savepc);
		if ((oldpc ^ PC) & 0xFF00) (void)read_cycle((oldpc&0xff00)|(PC&0xff)); // page-fix dummy read
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
		idle_cycle();                                   // taken: dummy opcode fetch
		oldpc = PC;
		PC += (int8_t)(savepc);
		if ((oldpc ^ PC) & 0xFF00) (void)read_cycle((oldpc&0xff00)|(PC&0xff)); // page-fix dummy read
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
		idle_cycle();                                   // taken: dummy opcode fetch
		oldpc = PC;
		PC += (int8_t)(savepc);
		if ((oldpc ^ PC) & 0xFF00) (void)read_cycle((oldpc&0xff00)|(PC&0xff)); // page-fix dummy read
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
		idle_cycle();                                   // taken: dummy opcode fetch
		oldpc = PC;
		PC += (int8_t)(savepc);
		if ((oldpc ^ PC) & 0xFF00) (void)read_cycle((oldpc&0xff00)|(PC&0xff)); // page-fix dummy read
	}
}

// -----------------------------------------------------------------------------
// Branch Always (BRA) - 65C02 Only
// Unconditionally branches to a relative address.
// Adds 1 clock cycle to the base instruction timing.
// -----------------------------------------------------------------------------
inline void cpu_6502::bra6502()
{
	idle_cycle();
	oldpc = PC;
	PC += (int8_t)(savepc);
	if ((oldpc ^ PC) & 0xff00) { (void)read_cycle((oldpc & 0xff00) | (PC & 0xff)); }
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
	// Cycles 3-6 of JSR (1-2 are the opcode fetch and abs_jsr6502's ADL read).
	// Only the two stack pushes are write cycles; every other cycle is a bus
	// read and is therefore halted by RDY when ANTIC takes the bus.
	// PC still points at ADL, so the pushed return address - the last byte of
	// this instruction - is PC+1.
	const uint16_t ret = (uint16_t)(PC + 1);
	(void)read_cycle((uint16_t)(BASE_STACK + S), true);            // 3
	write_cycle((uint16_t)(BASE_STACK + S), (uint8_t)(ret >> 8), true);          // 4
	write_cycle((uint16_t)(BASE_STACK + ((S - 1) & 0xFF)), (uint8_t)ret, true);  // 5
	S -= 2;
	const uint8_t adh = fetch_cycle(ret);                                  // 6: instruction operand
	PC = (uint16_t)((savepc & 0x00FF) | (adh << 8));
}

// -----------------------------------------------------------------------------
// Return from Subroutine (RTS)
// Pops the return address from the stack and sets the program counter to it,
// then increments the program counter by 1.
// -----------------------------------------------------------------------------
inline void cpu_6502::rts6502()
{
	// Cycles 3-6 (1-2 are the opcode fetch and implied6502's dummy read at PC).
	// RTS has no write cycles at all: the stack-pointer increment and the final
	// PC increment both appear on the bus as reads, so all six cycles are
	// halted by RDY when ANTIC takes the bus.
	(void)read_cycle((uint16_t)(BASE_STACK + S), true);            // 3
	const uint8_t lo = read_cycle((uint16_t)(BASE_STACK + ((S + 1) & 0xFF)), true);  // 4
	const uint8_t hi = read_cycle((uint16_t)(BASE_STACK + ((S + 2) & 0xFF)), true);  // 5
	S += 2;
	const uint16_t ret = (uint16_t)(lo | (hi << 8));
	(void)fetch_cycle(ret);                                 // 6: discarded instruction read
	PC = (uint16_t)(ret + 1);
}

// -----------------------------------------------------------------------------
// Return from Interrupt (RTI)
// Pops the processor status and program counter from the stack.
// Restores the CPU state after an interrupt handler has completed.
// -----------------------------------------------------------------------------
void cpu_6502::rti6502()
{
	// Unlike CLI/PLP, RTI has NO one-instruction IRQ delay on the real 6502:
	// a pending IRQ is taken immediately after RTI restores I=0. Adding the
	// delay here made every IRQ-driven loop run one mainline instruction late.
	(void)read_cycle(BASE_STACK + S, true);
	P = pull_cycle() | F_T | F_B;
	const uint8_t lo = pull_cycle();
	const uint8_t hi = pull_cycle();
	PC = lo | (hi << 8);
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
	push_cycle(static_cast<uint8_t>(PC >> 8));
	push_cycle(static_cast<uint8_t>(PC));
	uint16_t vector = 0xfffe;
	if (cpu_model != CPU_CMOS_65C02 && nmi_latched) {
		vector = 0xfffa;
		nmi_latched = nmi_polled = false;
	}
	// Ensure both Bit 4 (B) and Bit 5 (T) are set
	push_cycle(P | F_B | F_T);
	P |= F_I;
	if (cpu_model == CPU_CMOS_65C02)
		P &= ~F_D;    // only the 65C02 clears decimal on interrupt entry
	const uint8_t lo = read_cycle(vector);
	const uint8_t hi = read_cycle(static_cast<uint16_t>(vector + 1));
	PC = lo | (hi << 8);
	// The interrupt sequence does not poll: an NMI that misses the hijack
	// window (Lorenz NMI test) is taken after the handler's first instruction.
	skip_poll = true;
}

// -----------------------------------------------------------------------------
// No Operation (NOP)
// Performs no operation. Some undocumented NOPs add timing delay.
// Logs a warning if the NOP opcode is unofficial.
// -----------------------------------------------------------------------------
void cpu_6502::nop6502()
{
	if (opcode_table[opcode].addressing_mode != &cpu_6502::implied6502)
		(void)read_cycle(cpu_model == CPU_CMOS_65C02 &&
			(opcode == 0x5c || opcode == 0xdc || opcode == 0xfc) ? static_cast<uint16_t>(PC - 1) : savepc);
	// Addressing consumes the operand bytes; the read above supplies the final
	// data/dummy cycle for multi-byte NOPs. Implied NOPs are already complete.
}

// -----------------------------------------------------------------------------
// KIL / JAM (NMOS $x2 opcodes)
// A real NMOS 6502 wedges completely. Freeze the PC on the KIL opcode so the
// machine visibly halts at the crash site (like real hardware) instead of
// free-running through memory.
// -----------------------------------------------------------------------------
void cpu_6502::kil6502()
{
	PC--;
	run_state = RunState::Jammed;
	if (!kil_logged) {
		kil_logged = true;
		LOG_INFO("6502 JAM (KIL opcode %02X) at %04X - CPU halted", opcode, PC);
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
	// --- CLI: defer IRQ for one instruction if I actually transitions 1->0 (NMOS) ---
	const bool was_I = (P & F_I) != 0;
	P &= ~F_I;
	if (was_I) check_interrupts_after_cli();
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
	push_cycle(A);
}

// -----------------------------------------------------------------------------
// Push Processor Status (PHP)
// Pushes the processor status register (P) onto the stack.
// The break flag is set in the value pushed.
// -----------------------------------------------------------------------------
void cpu_6502::php6502()
{
	// Ensure both Bit 4 (B) and Bit 5 (T) are set
	push_cycle(P | F_B | F_T);
}

// -----------------------------------------------------------------------------
// Pull Accumulator (PLA)
// Pulls a byte from the stack into the accumulator (A).
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::pla6502()
{
	(void)read_cycle(BASE_STACK + S, true);
	A = pull_cycle();
	set_nz(A);
}

// -----------------------------------------------------------------------------
// Pull Processor Status (PLP)
// Pulls a byte from the stack into the processor status register (P).
// The unused flag (T) is forced on.
// -----------------------------------------------------------------------------
void cpu_6502::plp6502()
{
	const bool was_I = (P & F_I) != 0;
	(void)read_cycle(BASE_STACK + S, true);
	P = pull_cycle() | F_T | F_B;
	if (was_I && !(P & F_I)) irq_inhibit_one = 2;
}

// -----------------------------------------------------------------------------
// Push X Register (PHX) - 65C02/undocumented
// Pushes the X register onto the stack.
// -----------------------------------------------------------------------------
void cpu_6502::phx6502()
{
	push_cycle(X);
}

// -----------------------------------------------------------------------------
// Pull X Register (PLX) - 65C02/undocumented
// Pulls a byte from the stack into the X register.
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::plx6502()
{
	(void)read_cycle(BASE_STACK + S, true);
	X = pull_cycle();
	set_nz(X);
}

// -----------------------------------------------------------------------------
// Push Y Register (PHY) - 65C02/undocumented
// Pushes the Y register onto the stack.
// -----------------------------------------------------------------------------
void cpu_6502::phy6502()
{
	push_cycle(Y);
}

// -----------------------------------------------------------------------------
// Pull Y Register (PLY) - 65C02/undocumented
// Pulls a byte from the stack into the Y register.
// Updates the zero and negative flags based on the result.
// -----------------------------------------------------------------------------
void cpu_6502::ply6502()
{
	(void)read_cycle(BASE_STACK + S, true);
	Y = pull_cycle();
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
	A = X = operand_cycle();
	set_nz(A);
}

void cpu_6502::wai6502()
{
	(void)fetch_cycle(PC);
	run_state = RunState::Waiting;
}

void cpu_6502::stp6502()
{
	(void)fetch_cycle(PC);
	run_state = RunState::Stopped;
}

// -----------------------------------------------------------------------------
// SAX Instruction (Undocumented)
//
// Stores A & X to memory. Combines A and X registers with bitwise AND.
// Affects: None
// -----------------------------------------------------------------------------
inline void cpu_6502::sax6502()
{
	write_cycle(savepc, A & X);
}

// -----------------------------------------------------------------------------
// TRB (Test and Reset Bits)
// Tests bits in memory against the accumulator (A).
// Sets the zero flag if (A & M) == 0.
// Clears bits in memory where A has 1s (M = M & ~A).
// -----------------------------------------------------------------------------
void cpu_6502::trb6502()
{
	uint8_t v = operand_cycle();
	(void)operand_cycle();
	set_z(A & v);
	write_cycle(savepc, v & ~A);
}

// -----------------------------------------------------------------------------
// TSB (Test and Set Bits)
// Tests bits in memory against the accumulator (A).
// Sets the zero flag if (A & M) == 0.
// Sets bits in memory where A has 1s (M = M | A).
// -----------------------------------------------------------------------------
void cpu_6502::tsb6502()
{
	uint8_t v = operand_cycle();
	(void)operand_cycle();
	set_z(A & v);
	write_cycle(savepc, v | A);
}

// -----------------------------------------------------------------------------
// DCP Instruction (Undocumented)
//
// Decrements memory, then compares the result with A as if performing CMP.
// Affects: N, Z, C
// -----------------------------------------------------------------------------
inline void cpu_6502::dcp6502()
{
	uint8_t m = operand_cycle();
	write_cycle(savepc, m);
	--m;
	write_cycle(savepc, m);
	uint16_t result = (uint16_t)A - m;
	P = (P & ~(F_C | F_Z | F_N)) | ((result < 0x100) ? F_C : 0) | ((A == m) ? F_Z : 0) | ((result & 0x80) ? F_N : 0);
}

// -----------------------------------------------------------------------------
// NES 2A03: RRA (Undocumented, Binary Only)
// -----------------------------------------------------------------------------
void cpu_6502::rra_2a03()
{
	uint8_t carry_in = (P & F_C) ? 0x80 : 0;
	value = operand_cycle();
	write_cycle(savepc, value);
	P = (P & ~F_C) | (value & 1);
	value = (value >> 1) | carry_in;
	write_cycle(savepc, value);

	// ADC (Binary) logic
	const uint8_t m = value;
	const int cin = (P & F_C) ? 1 : 0;
	const uint16_t sum = (uint16_t)A + m + cin;

	P &= ~(F_V | F_C);
	if ((~(A ^ m) & (A ^ (uint8_t)sum) & 0x80) != 0) P |= F_V;
	if (sum & 0x0100) P |= F_C;

	A = (uint8_t)sum;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// NES 2A03: ISC (Undocumented, Binary Only)
// -----------------------------------------------------------------------------
void cpu_6502::isc_2a03()
{
	uint8_t m = operand_cycle();
	write_cycle(savepc, m);
	++m;
	write_cycle(savepc, m);

	// SBC (Binary) logic
	const int cin = (P & F_C) ? 1 : 0;
	const uint16_t diff = (uint16_t)A - m - (1 - cin);

	P &= ~(F_V | F_C);
	if (((A ^ m) & (A ^ (uint8_t)diff) & 0x80) != 0) P |= F_V;
	if (!(diff & 0x0100)) P |= F_C;

	A = (uint8_t)diff;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// NMOS Undocumented: ISC (With BCD Support)
// -----------------------------------------------------------------------------
void cpu_6502::isc6502()
{
	uint8_t m = operand_cycle();
	write_cycle(savepc, m);
	++m;
	write_cycle(savepc, m);
	sbc_nmos_value(m);
}

inline void cpu_6502::slo6502()
{
	value = operand_cycle();
	write_cycle(savepc, value);
	P = (P & ~F_C) | (value >> 7);
	value <<= 1;
	write_cycle(savepc, value);
	A |= value;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// NMOS Undocumented: RRA (With BCD Support)
// -----------------------------------------------------------------------------
void cpu_6502::rra6502()
{
	const uint8_t carry_in = (P & F_C) ? 0x80 : 0;
	uint8_t m = operand_cycle();
	write_cycle(savepc, m);
	P = (P & ~F_C) | (m & 1);
	m = (m >> 1) | carry_in;
	write_cycle(savepc, m);
	adc_nmos_value(m);
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
	value = operand_cycle();
	write_cycle(savepc, value);

	// Save the bit that will become the new carry
	uint8_t new_carry = (value >> 7) & 1;

	// Get the old carry to rotate into bit 0
	uint8_t old_carry = (P & F_C) ? 1 : 0;

	// Perform ROL: shift left and rotate old carry into bit 0
	value = (value << 1) | old_carry;

	// Update carry flag with bit that was shifted out
	P = (P & ~F_C) | (new_carry ? F_C : 0);

	// Write back to memory
	write_cycle(savepc, value);

	// AND with accumulator
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
	value = operand_cycle();
	write_cycle(savepc, value);
	P = (P & ~F_C) | (value & 0x01); // Set Carry from bit 0
	value >>= 1;
	write_cycle(savepc, value);

	A ^= value;
	set_nz(A);
}

// -----------------------------------------------------------------------------
// RMB0-7 and SMB0-7 (65C02)
// Reset/Set Memory Bit. 
// Cycle count: 5
// -----------------------------------------------------------------------------
void cpu_6502::rmb_smb_6502()
{
	// savepc contains the Zero Page address (fetched by zp6502 mode)
	uint8_t val = operand_cycle();
	(void)operand_cycle();

	// Decode Bit (High nibble 0-7) and Action (High nibble bit 3)
	// RMB: 0x07, 17, 27... SMB: 0x87, 97, A7...
	uint8_t bit_mask = 1 << ((opcode >> 4) & 7);

	if (opcode & 0x80) {
		val |= bit_mask;  // SMB (Set)
	}
	else {
		val &= ~bit_mask; // RMB (Reset)
	}

	write_cycle(savepc, val);
}

// -----------------------------------------------------------------------------
// BBR0-7 and BBS0-7 (65C02)
// Branch on Bit Reset/Set.
// Cycle count: 5 + 1 if branch + 1 if page cross
// -----------------------------------------------------------------------------
void cpu_6502::bbr_bbs_6502()
{
	// help contains ZP address, savepc contains relative offset (fetched by zprel6502)
	uint8_t val = value;

	// Decode Bit
	uint8_t bit_mask = 1 << ((opcode >> 4) & 7);

	// Check condition
	// BBR (0x0F..7F): Branch if Clear. BBS (0x8F..FF): Branch if Set.
	bool condition = (val & bit_mask);

	// If Opcode has bit 7 set (BBS), we want condition true.
	// If Opcode has bit 7 clear (BBR), we want condition false.
	if (((opcode & 0x80) != 0) == condition)
	{
		(void)fetch_cycle(PC);
		oldpc = PC;
		PC += (int8_t)savepc;
		if ((oldpc ^ PC) & 0xFF00) {
			(void)read_cycle(oldpc);
		}
	}
}
// -----------------------------------------------------------------------------
// More undocumented NMOS Instructions
// -----------------------------------------------------------------------------

// ANC - AND with immediate, then set C = N
void cpu_6502::anc6502()
{
	value = operand_cycle();
	A &= value;
	set_nz(A);
	// Carry = bit7(A)
	if (A & 0x80) P |= F_C; else P &= ~F_C;
}

// ALR - AND immediate, then LSR
void cpu_6502::alr6502()
{
	value = operand_cycle();
	A &= value;
	P = (P & ~F_C) | (A & 0x01 ? F_C : 0);
	A >>= 1;
	set_nz(A);
}

void cpu_6502::arr6502()
{
	value = operand_cycle();
	A &= value;
	const uint8_t unrotated = A;

	// Perform ROR on A (rotate right through carry)
	uint8_t old_carry = (P & F_C) ? 0x80 : 0;
	A = (A >> 1) | old_carry;

	set_nz(A);

	// ARR has unique flag behavior in binary mode:
	// C = bit 6 of result
	// V = bit 6 XOR bit 5 of result
	P &= ~(F_C | F_V);

	if (A & 0x40)
		P |= F_C;

	if (((A >> 6) ^ (A >> 5)) & 0x01)
		P |= F_V;

	if ((P & F_D) && cpu_model != CPU_NES_2A03) {
		// Decimal corrections affect A/C, retaining the pre-correction N/Z/V.
		if ((unrotated & 15) + (unrotated & 1) > 5)
			A = (A & 0xf0) | ((A + 6) & 15);
		if ((unrotated & 0xf0) + (unrotated & 0x10) > 0x50) {
			A = static_cast<uint8_t>(A + 0x60);
			P |= F_C;
		} else P &= ~F_C;
	}
}

void cpu_6502::axs6502()
{
	// AXS: X = (A & X) - immediate, sets flags like CMP
	// Does NOT use borrow (like CMP, not SBC)
	value = operand_cycle();
	uint8_t temp = A & X;
	uint16_t result = (uint16_t)temp - value;

	X = (uint8_t)result;

	// Set carry if no borrow (like CMP)
	if (temp >= value)
		P |= F_C;
	else
		P &= ~F_C;

	set_nz(X);
}


// -----------------------------------------------------------------------------
// ANE / XAA (0x8B)
// -----------------------------------------------------------------------------
void cpu_6502::ane6502()
{
	value = operand_cycle();
	A = (A | 0xEE) & X & value;  // Magic constant 0xEE
	set_nz(A);
}

// -----------------------------------------------------------------------------
// LXA / ATX (0xAB)
// -----------------------------------------------------------------------------
void cpu_6502::lxa6502()
{
	value = operand_cycle();
	A = X = (A | 0xEE) & value;  // Magic constant 0xEE
	set_nz(A);
}

// -----------------------------------------------------------------------------
// The SHx family stores reg & (high(base)+1). The addressing mode has already
// resolved savepc = base + index. On a page cross, the high byte of the target
// address itself is corrupted to the stored value (real NMOS quirk).
// -----------------------------------------------------------------------------
inline uint16_t cpu_6502::sh_target(uint16_t base, uint8_t store_value)
{
	if ((base & 0xFF00) != (savepc & 0xFF00))
		return (uint16_t)((store_value << 8) | (savepc & 0x00FF));
	return savepc;
}

// -----------------------------------------------------------------------------
// SHS / TAS (0x9B): S = A & X, then store A & X & (H+1) at abs,Y
// -----------------------------------------------------------------------------
void cpu_6502::shs6502()
{
	S = A & X;
	uint16_t base = savepc - Y;
	uint8_t store_value = A & X & (uint8_t)((base >> 8) + 1);
	write_cycle(sh_target(base, store_value), store_value);
}

// -----------------------------------------------------------------------------
// SHY / SAY (0x9C): store Y & (H+1) at abs,X
// -----------------------------------------------------------------------------
void cpu_6502::shy6502()
{
	uint16_t base = savepc - X;
	uint8_t store_value = Y & (uint8_t)((base >> 8) + 1);
	write_cycle(sh_target(base, store_value), store_value);
}

// -----------------------------------------------------------------------------
// SHX / SXA (0x9E): store X & (H+1) at abs,Y
// -----------------------------------------------------------------------------
void cpu_6502::shx6502()
{
	uint16_t base = savepc - Y;
	uint8_t store_value = X & (uint8_t)((base >> 8) + 1);
	write_cycle(sh_target(base, store_value), store_value);
}

// -----------------------------------------------------------------------------
// AHX / SHA (0x93 (zp),Y ; 0x9F abs,Y): store A & X & (H+1).
// The addressing mode (indy/absy) already resolved savepc to base+Y - do NOT
// re-fetch operands here (an earlier version re-read a pointer from savepc as
// if it were the PC, corrupting an unrelated address on every execution).
// -----------------------------------------------------------------------------
void cpu_6502::ahx6502()
{
	uint16_t base = savepc - Y;
	uint8_t store_value = A & X & (uint8_t)((base >> 8) + 1);
	write_cycle(sh_target(base, store_value), store_value);
}

void cpu_6502::las6502()
{
	value = operand_cycle();
	S &= value;
	A = X = S;
	set_nz(A);
}

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

void cpu_6502::reset_instruction_counts()
{
	std::fill(std::begin(instruction_count), std::end(instruction_count), 0);
}

std::string cpu_6502::disassemble(uint16_t pc, int* bytesUsed)
{
	static const uint8_t length[256] = {
		2,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1,  // 00-0F
		2,2,2,1,2,2,2,1,1,3,1,1,3,3,3,1,  // 10-1F
		3,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1,  // 20-2F
		2,2,2,1,2,2,2,1,1,3,1,1,3,3,3,1,  // 30-3F
		1,2,1,1,1,2,2,1,1,2,1,1,3,3,3,1,  // 40-4F
		2,2,2,1,1,2,2,1,1,3,1,1,1,3,3,1,  // 50-5F
		1,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1,  // 60-6F
		2,2,2,1,2,2,2,1,1,3,1,1,3,3,3,1,  // 70-7F
		2,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1,  // 80-8F
		2,2,2,1,2,2,2,1,1,3,1,1,3,3,3,1,  // 90-9F
		2,2,2,1,2,2,2,1,1,2,1,1,3,3,3,1,  // A0-AF
		2,2,1,1,2,2,2,1,1,3,1,1,3,3,3,1,  // B0-BF
		2,2,1,1,2,2,2,1,1,2,1,1,3,3,3,1,  // C0-CF
		2,2,2,1,1,2,2,1,1,3,1,1,3,3,3,1,  // D0-DF
		2,2,1,2,2,2,2,2,1,2,1,2,3,3,3,3,  // E0-EF
		2,2,2,2,2,2,2,2,1,3,1,3,3,3,3,3   // F0-FF
	};

	char buffer[64] = {};
	uint8_t opcode = readop(pc);
	uint8_t op1 = readop(pc + 1);
	uint8_t op2 = readop(pc + 2);

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
