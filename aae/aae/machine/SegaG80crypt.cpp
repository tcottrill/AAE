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

#include "SegaG80crypt.h"

 void(*sega_decrypt)(int, unsigned int*);


/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0062                      */
/****************************************************************************/
static void sega_decrypt62(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x03)
	{
	case 0x00:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x01:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x02:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x03:
		/* A */
		i = b;
		break;
	}

	*lo = i;
}

/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0063                      */
/****************************************************************************/
static void sega_decrypt63(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x09)
	{
	case 0x00:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x01:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x08:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x09:
		/* A */
		i = b;
		break;
	}

	*lo = i;
}


static void sega_decrypt64(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x03)
	{
	case 0x00:
		/* A */
		i = b;
		break;
	case 0x01:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x02:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x03:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	}

	*lo = i;
}
/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0070                      */
/****************************************************************************/
static void sega_decrypt70(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x09)
	{
	case 0x00:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x01:
		/* A */
		i = b;
		break;
	case 0x08:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x09:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	}

	*lo = i;
}

/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0076                      */
/****************************************************************************/
static void sega_decrypt76(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x09)
	{
	case 0x00:
		/* A */
		i = b;
		break;
	case 0x01:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x08:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x09:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	}

	*lo = i;
}

/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0082                      */
/****************************************************************************/
static void sega_decrypt82(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x11)
	{
	case 0x00:
		/* A */
		i = b;
		break;
	case 0x01:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x10:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x11:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	}

	*lo = i;
}

/****************************************************************************/
/* MB 971031 - Emulate no Sega G80 security chip                            */
/****************************************************************************/
static void sega_decrypt0(int pc, unsigned int* lo)
{
	return;
}


void sega_security(int chip)
{

	switch (chip)
	{
	case 62:
		sega_decrypt = sega_decrypt62;
		break;
	case 63:
		sega_decrypt = sega_decrypt63;
		break;
	case 64:
		sega_decrypt = sega_decrypt64;
		break;
	case 70:
		sega_decrypt = sega_decrypt70;
		break;
	case 76:
		sega_decrypt = sega_decrypt76;
		break;
	case 82:
		sega_decrypt = sega_decrypt82;
		break;
	default:
		sega_decrypt = sega_decrypt64;
		break;
	}
}
