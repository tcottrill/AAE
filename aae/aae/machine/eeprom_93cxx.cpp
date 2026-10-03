//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// 93Cxx Microwire serial EEPROM - see eeprom_93cxx.h.
#include "eeprom_93cxx.h"

#include <string.h>

enum { EE93_IDLE, EE93_COMMAND, EE93_READ, EE93_DATA };
enum { EE93_OP_EXT = 0, EE93_OP_WRITE = 1, EE93_OP_READ = 2, EE93_OP_ERASE = 3 };

static uint32_t word_get(const eeprom_93cxx* e, uint32_t a)
{
	if (e->word_bits == 16)
		return e->rom[2 * a] | (e->rom[2 * a + 1] << 8);
	return e->rom[a];
}

static void word_set(eeprom_93cxx* e, uint32_t a, uint32_t v)
{
	if (e->word_bits == 16) {
		e->rom[2 * a] = (uint8_t)v;
		e->rom[2 * a + 1] = (uint8_t)(v >> 8);
	}
	else
		e->rom[a] = (uint8_t)v;
}

static uint32_t words(const eeprom_93cxx* e)
{
	return 1u << e->addr_bits;
}

void eeprom_93cxx_init(eeprom_93cxx* e, int addr_bits, int word_bits)
{
	memset(e, 0, sizeof *e);
	e->addr_bits = addr_bits;
	e->word_bits = word_bits == 16 ? 16 : 8;
	e->size = (int)(words(e) * (uint32_t)(e->word_bits / 8));
	if (e->size > (int)sizeof e->rom)
		e->size = (int)sizeof e->rom;
	memset(e->rom, 0xff, sizeof e->rom);
	e->dout = 1;
	e->state = EE93_IDLE;
}

static void load_word(eeprom_93cxx* e)
{
	e->out = word_get(e, e->addr);
	e->outbits = e->word_bits;
}

static void command_done(eeprom_93cxx* e)
{
	uint32_t mask = words(e) - 1;
	uint32_t top = (e->addr >> (e->addr_bits - 2)) & 3;

	e->nbits = 0;
	e->sr = 0;
	switch (e->op) {
	case EE93_OP_READ:
		load_word(e);
		e->dout = 0;                        // the dummy bit
		e->state = EE93_READ;
		break;
	case EE93_OP_WRITE:
		e->state = EE93_DATA;
		break;
	case EE93_OP_ERASE:
		if (e->write_enabled)
			word_set(e, e->addr & mask, e->word_bits == 16 ? 0xffff : 0xff);
		e->dout = 1;
		e->state = EE93_IDLE;
		break;
	default:                                // EE93_OP_EXT
		e->dout = 1;
		e->state = EE93_IDLE;
		if (top == 3)
			e->write_enabled = 1;           // EWEN
		else if (top == 0)
			e->write_enabled = 0;           // EWDS
		else if (top == 2) {                // ERAL
			if (e->write_enabled)
				memset(e->rom, 0xff, (size_t)e->size);
		}
		else
			e->state = EE93_DATA;           // WRAL: data follows
		break;
	}
}

static void clock_rise(eeprom_93cxx* e)
{
	switch (e->state) {
	case EE93_IDLE:
		if (e->di) {                        // the start bit
			e->state = EE93_COMMAND;
			e->nbits = 0;
			e->sr = 0;
		}
		break;

	case EE93_COMMAND:
		e->sr = (e->sr << 1) | (uint32_t)e->di;
		if (++e->nbits == 2 + e->addr_bits) {
			e->op = (int)(e->sr >> e->addr_bits) & 3;
			e->addr = e->sr & (words(e) - 1);
			command_done(e);
		}
		break;

	case EE93_READ:
		if (e->outbits == 0) {              // sequential read: the next word
			e->addr = (e->addr + 1) & (words(e) - 1);
			load_word(e);
		}
		e->outbits--;
		e->dout = (int)((e->out >> e->outbits) & 1);
		break;

	case EE93_DATA:
		e->sr = (e->sr << 1) | (uint32_t)e->di;
		if (++e->nbits == e->word_bits) {
			if (e->write_enabled) {
				if (e->op == EE93_OP_WRITE)
					word_set(e, e->addr, e->sr);
				else
					for (uint32_t a = 0; a < words(e); a++)
						word_set(e, a, e->sr);   // WRAL
			}
			e->dout = 1;
			e->state = EE93_IDLE;
			e->nbits = 0;
			e->sr = 0;
		}
		break;
	}
}

void eeprom_93cxx_set_cs(eeprom_93cxx* e, int state)
{
	state = state ? 1 : 0;
	if (!state) {                           // deselect: abort, back to idle
		e->state = EE93_IDLE;
		e->nbits = 0;
		e->sr = 0;
		e->dout = 1;
	}
	e->cs = state;
}

void eeprom_93cxx_set_clk(eeprom_93cxx* e, int state)
{
	state = state ? 1 : 0;
	if (e->cs && state && !e->clk)
		clock_rise(e);
	e->clk = state;
}

void eeprom_93cxx_set_di(eeprom_93cxx* e, int state)
{
	e->di = state ? 1 : 0;
}

int eeprom_93cxx_do(const eeprom_93cxx* e)
{
	return e->dout;
}
