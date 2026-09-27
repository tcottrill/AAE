//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// GI ER2055 EAROM - see er2055.h. Translated arithmetic-for-arithmetic from
// MAME 0.286's er2055_device::set_control / update_state / set_clk
// (src/devices/machine/er2055.cpp, BSD-3-Clause, copyright the MAME team
// and Aaron Giles): `this->m_member` becomes `e->member`, the int pin
// arguments become bool. Redistributions must retain that notice.
#include "er2055.h"

// MAME's bit names for the internal composite control state.
#define CK  0x01
#define C1  0x02
#define C2  0x04
#define CS1 0x08
#define CS2 0x10

void er2055_init(er2055* e)
{
	for (int i = 0; i < 64; i++)
		e->rom[i] = 0x00;          // ROMREGION_ERASE00
	e->address = 0;
	e->data = 0;
	e->control = 0;
}

void er2055_set_address(er2055* e, uint8_t addr)
{
	e->address = (uint8_t)(addr & 0x3F);
}

void er2055_set_data(er2055* e, uint8_t data)
{
	e->data = data;
}

uint8_t er2055_data(const er2055* e)
{
	return e->data;
}

// Runs after a transition on clock, control or chip select while both chip
// selects are set; called from set_control() and set_clk(), as in MAME.
static void update_state(er2055* e)
{
	switch (e->control & (C1 | C2))
	{
	case 0:                                 // write: AND against previous data;
		e->rom[e->address] &= e->data;      // a write without an erase can only
		break;                              // clear bits
	case C2:                                // erase
		e->rom[e->address] = 0xFF;
		break;
	default:                                // C1 (read - the latch is in set_clk)
		break;                              // or C1|C2: no ROM change
	}
}

void er2055_set_control(er2055* e, bool cs1, bool cs2, bool c1, bool c2)
{
	uint8_t oldstate = e->control;
	uint8_t next = (uint8_t)(oldstate & CK);   // CK belongs to set_clk()
	if (c1)  next |= C1;
	if (c2)  next |= C2;
	if (cs1) next |= CS1;
	if (cs2) next |= CS2;
	e->control = next;

	// Not selected, or no change: nothing happens.
	if ((e->control & (CS1 | CS2)) != (CS1 | CS2) || e->control == oldstate)
		return;
	update_state(e);
}

void er2055_set_clk(er2055* e, bool state)
{
	uint8_t oldstate = e->control;
	if (state)
		e->control |= CK;
	else
		e->control &= (uint8_t)~CK;

	// Falling edge while selected: a read (C1 set, C2 don't care) latches
	// rom[address] into the data register, then update_state() runs.
	if ((e->control & (CS1 | CS2)) == (CS1 | CS2) && e->control != oldstate && !state)
	{
		if ((e->control & C1) == C1)
			e->data = e->rom[e->address];
		update_state(e);
	}
}
