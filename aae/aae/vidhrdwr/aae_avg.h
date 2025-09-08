#include "aae_mame_driver.h"
#include "deftypes.h"

#ifndef AAE_AVG_H
#define AAE_AVG_H

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
//   
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

/*************************************************************************

	avgdvg.c: Atari DVG and AVG simulators

	Copyright 1991, 1992, 1996 Eric Smith

	Modified for the MAME project 1997 by
	Brad Oliver, Bernd Wiebelt, Aaron Giles, Andrew Caldwell

	971108 Disabled vector timing routines, introduced an ugly (but fast!)
			busy flag hack instead. BW
	980202 New anti aliasing code by Andrew Caldwell (.ac)
	980206 New (cleaner) busy flag handling.
			Moved LBO's buffered point into generic vector code. BW
	980212 Introduced timing code based on Aaron timer routines. BW
	980318 Better color handling, Bzone and MHavoc clipping. BW

	Battlezone uses a red overlay for the top of the screen and a green one
	for the rest. There is a circuit to clip color 0 lines extending to the
	red zone. This is emulated now. Thanks to Neil Bradley for the info. BW

	Frame and interrupt rates (Neil Bradley) BW
		~60 fps/4.0ms: Asteroid, Asteroid Deluxe
		~40 fps/4.0ms: Lunar Lander
		~40 fps/4.1ms: Battle Zone
		~45 fps/5.4ms: Space Duel, Red Baron
		~30 fps/5.4ms: StarWars

	Games with self adjusting framerate
		4.1ms: Black Widow, Gravitar
		4.1ms: Tempest
		Major Havoc
		Quantum

	TODO: accurate vector timing (need timing diagramm)

************************************************************************/

#define USE_DVG             1
#define USE_DVG_ASTEROID    2
#define USE_AVG_RBARON      3
#define USE_AVG_BZONE       4
#define USE_AVG             5
#define USE_AVG_TEMPEST     6
#define USE_AVG_MHAVOC      7
#define USE_AVG_SWARS       8
#define USE_AVG_QUANTUM     9
#define USE_AVG_ALPHAONE    10

#define VEC_SHIFT  16

extern unsigned char vec_ram[0x8000];
extern UINT8* tempest_colorram;
extern UINT16 quantum_colorram[0x20];
extern UINT16* quantum_vectorram;

int vector_timer(int deltax, int deltay);
//void set_new_frame();
int avg_init(int type);
void AVG_RUN();
int avg_go();
int avg_clear();
int avg_check();


void advdvg_go_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void avgdvg_go_word_w(UINT32 address, UINT16 data, struct MemoryWriteWord* psMemWrite);
void avgdvg_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void avgdvg_reset_word_w(UINT32 address, UINT16 data, struct MemoryWriteWord* pMemWrite);

int avg_start(void);
int avg_start_tempest(void);
int avg_start_mhavoc(void);
int avg_start_alphaone(void);
int avg_start_starwars(void);
int avg_start_quantum(void);
int avg_start_bzone(void);
int avg_start_redbaron(void);
void avg_stop(void);


#endif