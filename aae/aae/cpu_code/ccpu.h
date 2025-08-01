// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E.™ Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain © the M.A.M.E.™ Project and its
//     respective contributors under their original terms of distribution.
//   - Modifications, enhancements, and new code are © 2025 Tim Cottrill and
//     released under the GNU General Public License v3 (GPLv3) or later.
//   - Redistribution must preserve both this notice and the original MAME
//     copyright acknowledgement.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// Original Copyright:
//   This file is originally part of and copyright the M.A.M.E.™ Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------

/***************************************************************************

	ccpu.h
	Core implementation for the portable Cinematronics CPU emulator.

	Written by Aaron Giles
	Special thanks to Zonn Moore for his detailed documentation.

***************************************************************************/

#ifndef _CCPU_H_
#define	_CCPU_H_

#include "basetsd.h"

/***************************************************************************
	REGISTER ENUMERATION
***************************************************************************/

enum
{
	CCPU_PC = 1,
	CCPU_FLAGS,
	CCPU_A,
	CCPU_B,
	CCPU_I,
	CCPU_J,
	CCPU_P,
	CCPU_X,
	CCPU_Y,
	CCPU_T
};

/***************************************************************************
	CONFIG STRUCTURE
***************************************************************************/

struct CCPUConfig
{
	UINT8(*external_input)(void);		/* if NULL, assume JMI jumper is present */
	void		(*vector_callback)(INT16 sx, INT16 sy, INT16 ex, INT16 ey, UINT8 shift);
};
/**************************************************************************
	PUBLIC VARS
**************************************************************************/
extern UINT8 MUX_VAL;
//extern UINT8 SOUNDBITS;
//extern UINT8 CCPUROMSIZE;

/***************************************************************************
	PUBLIC FUNCTIONS
***************************************************************************/

//void ccpu_get_info(UINT32 state, cpuinfo *info);
void ccpu_wdt_timer_trigger(void);
int run_ccpu(int cycles);
int cpunum_get_reg(int cpunum, int reg);
int cpunum_set_reg(int cpunum, int reg, INT16 val);
void vec_control_write(int data);
void coin_handler(int data);
void init_ccpu(int val, int romsize);
void ccpu_reset();
UINT8 joystick_read(void);
UINT16 get_ccpu_inputs(int offset);
UINT16 get_ccpu_switches(int offset);
int get_ccpu_ticks();

#endif
