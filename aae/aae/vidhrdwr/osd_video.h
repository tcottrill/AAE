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
//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
// 
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.  
//==========================================================================

#pragma once

// -----------------------------------------------------------------------------
// osd_video.h
// OSD-facing video entry points and palette helpers extracted for AAE.
//
// This header preserves the original API used throughout the codebase so that
// callers (glcode, drivers, etc.) do not need any changes.
// -----------------------------------------------------------------------------

#include "framework.h"
#include "aae_mame_driver.h"   // RunningMachine, Machine, MAX_GFX_ELEMENTS, MAX_PENS, etc.
#include "old_mame_raster.h"   // GfxElement, GfxLayout, gfxdecodeinfo, decode/free helpers

// -----------------------------------------------------------------------------
// From MAME .30 for VH Hardware 
// -----------------------------------------------------------------------------
#ifndef MAX_COLOR_TUPLE
#define MAX_COLOR_TUPLE 16      /* no more than 4 bits per pixel, for now */
#endif

#ifndef MAX_COLOR_CODES
#define MAX_COLOR_CODES 256     /* no more than 256 color codes, for now */
#endif


// Palette Functions cribbed from MAME

// If you already define these elsewhere, you can remove the guards.
#ifndef PALETTE_COLOR_UNUSED
#define PALETTE_COLOR_UNUSED        0
#define PALETTE_COLOR_VISIBLE       1
#define PALETTE_COLOR_CACHED        2
#define PALETTE_COLOR_TRANSPARENT_FLAG 4
// Backwards compat aliases used in older code:
#define PALETTE_COLOR_USED (PALETTE_COLOR_VISIBLE | PALETTE_COLOR_CACHED)
#endif


// -----------------------------------------------------------------------------
// Global palette store used by raster paths (public so glcode & others can read)
// -----------------------------------------------------------------------------
extern unsigned char current_palette[640][3];

// -----------------------------------------------------------------------------
// Palette helpers (used by raster code & conversions)
// -----------------------------------------------------------------------------
void osd_modify_pen(int pen, unsigned char r, unsigned char g, unsigned char b);
void osd_get_pen(int pen, unsigned char* r, unsigned char* g, unsigned char* b);

// -----------------------------------------------------------------------------
// Display creation 
//   - Returns an osd_bitmap* or nullptr on failure.
// -----------------------------------------------------------------------------
struct osd_bitmap* osd_create_display(int width, int height,
	unsigned int totalcolors,
	const unsigned char* palette,
	unsigned char* pens,
	int attributes);

// -----------------------------------------------------------------------------
// Old MAME-style video hardware open/close
//   - vh_open() returns 0 on success, non-zero on failure.
// -----------------------------------------------------------------------------
int  vh_open(void);
void vh_close(void);

bool aae_palette_start(int total_colors);
void aae_palette_stop();

void palette_change_color(int color, unsigned char red, unsigned char green, unsigned char blue);
const unsigned char* palette_recalc(void);