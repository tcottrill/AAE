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