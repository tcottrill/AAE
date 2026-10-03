//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// 93Cxx Microwire serial EEPROM (93C46 / 93C56 / 93C66 and friends), written
// from the datasheet behaviour (Microchip / Atmel 93C46 family). Four pins:
// CS, CLK, DI (into the chip) and DO (out of it). Everything is clocked in on
// a rising CLK edge while CS is high:
//
//   start bit (1), 2 opcode bits, then addr_bits address bits, MSB first
//     10  READ   - DO goes 0 (dummy bit), then each further rising edge puts
//                  the next data bit on DO, MSB first; reading on past the
//                  end of a word continues with the next address
//     01  WRITE  - word_bits data bits follow; programmed if writes enabled
//     11  ERASE  - the word becomes all ones, if writes enabled
//     00  by the top two address bits:
//           11 EWEN  enable writes      00 EWDS  disable writes
//           10 ERAL  erase all          01 WRAL  write all (data follows)
//   Writes are disabled at power-up. Programming is taken as instant, so DO
//   reads 1 (ready) whenever no data is being shifted out.
//   CS low aborts any command in progress.
//
// Organisation is set at init: word_bits 8 or 16, and addr_bits (93C46: 7
// for x8, 6 for x16; 93C56/66: 9 for x8, 8 for x16). Words are stored in
// rom[] as bytes, little-endian for x16; rom is the persisted image.
// Board-specific decoding (which address and data bit is which pin) belongs
// in the driver. This module does no file I/O and has no AAE includes, so
// tests/eeprom_93cxx_tests.cpp builds it standalone.
#ifndef EEPROM_93CXX_H
#define EEPROM_93CXX_H

#include <stdint.h>

struct eeprom_93cxx
{
	uint8_t  rom[2048];   // the persisted image (only size bytes are used)
	int      size;        // bytes: (1 << addr_bits) * word_bits / 8
	int      addr_bits;
	int      word_bits;   // 8 or 16

	// pins
	int      cs, clk, di, dout;

	// command state
	int      state;       // EE93_IDLE ...
	int      nbits;
	uint32_t sr;
	int      op;
	uint32_t addr;
	uint32_t out;
	int      outbits;
	int      write_enabled;
};

// rom filled with 0xFF (an erased chip), writes disabled, pins low.
void eeprom_93cxx_init(eeprom_93cxx* e, int addr_bits, int word_bits);

void eeprom_93cxx_set_cs(eeprom_93cxx* e, int state);
void eeprom_93cxx_set_clk(eeprom_93cxx* e, int state);    // acts on a rising edge
void eeprom_93cxx_set_di(eeprom_93cxx* e, int state);
int  eeprom_93cxx_do(const eeprom_93cxx* e);              // 0 or 1

#endif // EEPROM_93CXX_H
