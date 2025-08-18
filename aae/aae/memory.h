#ifndef MEMORY_H
#define MEMORY_H

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



extern const char* rom_regions[];



enum {
	//REGION_INVALID = 0x80,
	REGION_CPU1,
	REGION_CPU2,
	REGION_CPU3,
	REGION_CPU4,
	REGION_GFX1,
	REGION_GFX2,
	REGION_GFX3,
	REGION_GFX4,
	REGION_PROMS,
	REGION_SOUND1,
	REGION_SOUND2,
	REGION_SOUND3,
	REGION_SOUND4,
	REGION_USER1,
	REGION_USER2,
	REGION_USER3,
	REGION_MAX
};

#define REGIONFLAG_MASK			0xf8000000
#define REGIONFLAG_DISPOSE		0x80000000           /* Dispose of this region when done */
#define REGIONFLAG_SOUNDONLY	0x40000000           /* load only if sound emulation is turned on */

#define PROM_MEMORY_REGION(region) ((const unsigned char *)((-(region))-1))

unsigned char *memory_region(int num);
void new_memory_region(int num, int size);
void free_memory_region(int num);
void byteswap(unsigned char* mem, int length);
void free_all_memory_regions();
void reset_memory_tracking();





#endif