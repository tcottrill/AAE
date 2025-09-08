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
//= ========================================================================== =
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME
// code, 0.29 through .90 mixed with code of my own. This emulator was
// created solely for my amusement and learning and is provided only
// as an archival experience.
//
// All MAME code used and abused in this emulator remains the copyright
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
//
// SOME CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//============================================================================

#include <stdio.h>
#include <stdlib.h>
#include "aae_mame_driver.h"
#include "sys_log.h"
#include "config.h"
#include "mame_fileio.h"
#include "iniFile.h"
#include "path_helper.h"

/* file handling routines from mame (TM) */

/* gamename holds the driver name, filename is only used for ROMs and samples. */
/* if 'write' is not 0, the file is opened for write. Otherwise it is opened */
/* for read. */
/*
*/

void* osd_fopen(const char* gamename,
	const char* filename,
	int         filetype,
	int         write)         // 0 = read, non-zero = write
{
	char        name[MAX_PATH];
	FILE* fp = nullptr;
	const char* dirname = nullptr;
	int         nchars = -1;              // snprintf result

	SetIniFile("aae.ini");

	/* -------- build the pathname ---------------------------------- */
	switch (filetype)
	{
	case OSD_FILETYPE_ROM:
	case OSD_FILETYPE_SAMPLE:
		LOG_ERROR("MAME OSD_FOPEN UNSUPPORTED FILE TYPE");
		return nullptr;

	case OSD_FILETYPE_HIGHSCORE:
		dirname = get_config_string("directory", "hi", "HI");
		nchars = snprintf(name, sizeof(name), "%s\\%s.hi", dirname, gamename);
		break;

	case OSD_FILETYPE_NVRAM:
		dirname = get_config_string("directory", "hi", "HI");
		nchars = snprintf(name, sizeof(name), "%s\\%s.nv", dirname, gamename);
		break;

	case OSD_FILETYPE_CONFIG:
		dirname = get_config_string("directory", "cfg", "cfg");
		nchars = snprintf(name, sizeof(name), "%s\\%s.cfg", dirname, gamename);
		LOG_INFO("Trying to open game config file: %s", name);
		break;

	case OSD_FILETYPE_INPUTLOG:
		nchars = snprintf(name, sizeof(name), "%s.inp", filename);
		break;

	default:
		LOG_ERROR("MAME OSD_FOPEN CALLED WITH UNSUPPORTED FILE TYPE");
		return nullptr;
	}

	/* -------- validate path length -------------------------------- */
	if (nchars < 0 || static_cast<size_t>(nchars) >= sizeof(name)) {
		LOG_ERROR("MAME OSD_FOPEN: path too long or formatting error");
		return nullptr;
	}

	if (fopen_s(&fp, name, write ? "wb" : "rb") != 0) {
		char errbuf[128] = {};
		strerror_s(errbuf, sizeof(errbuf), errno);
		LOG_INFO("Failed to open file '%s' (%s)", name, errbuf);
		return nullptr;
	}

	return static_cast<void*>(fp);         // keep legacy signature
}

int osd_fread_scatter(void* file, void* buffer, int length, int increment)
{
	return 0;
}

int osd_fread(void* file, void* buffer, int length)
{
	return (int) fread(buffer, 1, length, (FILE*)file);
}

int osd_fread_swap(void* file, void* buffer, int length)
{
	int i;
	unsigned char* buf;
	unsigned char temp;
	int res;

	res = osd_fread(file, buffer, length);

	buf = (unsigned char*)buffer;
	for (i = 0; i < length; i += 2)
	{
		temp = buf[i];
		buf[i] = buf[i + 1];
		buf[i + 1] = temp;
	}

	return res;
}

int osd_fwrite(void* file, const void* buffer, int length)
{
	return (int) fwrite(buffer, 1, length, (FILE*)file);
}

int osd_fseek(void* file, int offset, int whence)
{
	return fseek((FILE*)file, offset, whence);
}

void osd_fclose(void* file)
{
	if (file)
		fclose(static_cast<FILE*>(file));
}

unsigned int osd_fcrc(void* file)
{
	return 0;
}

int readint(void* f, UINT32* num)
{
	int i;

	*num = 0;
	for (i = 0; i < sizeof(UINT32); i++)
	{
		unsigned char c;

		*num <<= 8;
		if (osd_fread(f, &c, 1) != 1)
			return -1;
		*num |= c;
	}

	return 0;
}

void writeint(void* f, UINT32 num)
{
	int i;

	for (i = 0; i < sizeof(UINT32); i++)
	{
		unsigned char c;

		c = (num >> 8 * (sizeof(UINT32) - 1)) & 0xff;
		osd_fwrite(f, &c, 1);
		num <<= 8;
	}
}

int readword(void* f, UINT16* num)
{
	int i, res;

	res = 0;
	for (i = 0; i < sizeof(UINT16); i++)
	{
		unsigned char c;

		res <<= 8;
		if (osd_fread(f, &c, 1) != 1)
			return -1;
		res |= c;
	}

	*num = res;
	return 0;
}

void writeword(void* f, UINT16 num)
{
	int i;

	for (i = 0; i < sizeof(UINT16); i++)
	{
		unsigned char c;

		c = (num >> 8 * (sizeof(UINT16) - 1)) & 0xff;
		osd_fwrite(f, &c, 1);
		num <<= 8;
	}
}