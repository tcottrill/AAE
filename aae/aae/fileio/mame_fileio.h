#pragma once

// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code or design traits originally developed as part of 
// the M.A.M.E.(TM) Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain @ the M.A.M.E.(TM) Project and its
//     respective contributors under their original terms of distribution.
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
//   This file is originally part of and copyright the M.A.M.E.(TM) Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------

#include "deftypes.h"

#define osd_fread_msbfirst osd_fread_swap
#define osd_fwrite_msbfirst osd_fwrite_swap
#define osd_fread_lsbfirst osd_fread
#define osd_fwrite_lsbfirst osd_fwrite

/* file handling routines */
enum
{
	OSD_FILETYPE_ROM = 1,
	OSD_FILETYPE_SAMPLE,
	OSD_FILETYPE_NVRAM,
	OSD_FILETYPE_HIGHSCORE,
	OSD_FILETYPE_CONFIG,
	OSD_FILETYPE_INPUTLOG,
	OSD_FILETYPE_STATE,
	OSD_FILETYPE_ARTWORK,
	OSD_FILETYPE_MEMCARD,
	OSD_FILETYPE_SCREENSHOT

};

/* gamename holds the driver name, filename is only used for ROMs and samples. */
/* if 'write' is not 0, the file is opened for write. Otherwise it is opened */
/* for read. */
void *osd_fopen(const char *gamename, const char *filename, int filetype, int write);
int osd_fread(void *file, void *buffer, int length);
int osd_fread_swap(void *file, void *buffer, int length);
int osd_fread_scatter(void *file, void *buffer, int length, int increment);
int osd_fwrite(void *file, const void *buffer, int length);
int osd_fseek(void *file, int offset, int whence);
unsigned int osd_fcrc(void *file);
void osd_fclose(void *file);

//From inputport.cpp, all file ops should be together!
int readint(void *f, UINT32 *num);
void writeint(void *f, UINT32 num);
int readword(void *f, UINT16 *num);
void writeword(void *f, UINT16 num);

