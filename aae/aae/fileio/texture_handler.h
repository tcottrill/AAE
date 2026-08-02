// -----------------------------------------------------------------------------
// This file is part of the AAE (Another Arcade Emulator) project.
//
// Copyright (C) 2025-2026 Tim Cottrill
//
// This code is released under the GNU General Public License v3.0
// or (at your option) any later version. See the accompanying
// source files and license text for full details.
// -----------------------------------------------------------------------------

#ifndef LOADERS_H
#define LOADERS_H

#include "render_types.h"
#include "aae_mame_driver.h"

extern rtex_t error_tex[2];
extern rtex_t pause_tex[2];
extern rtex_t fun_tex[4];
extern rtex_t art_tex[8];
extern rtex_t game_tex[10];
extern rtex_t menu_tex[7];

// Load
rtex_t load_texture(const char* filename, const char* archname, int numcomponents, int filter, bool modernGL = true);
// Use
void set_texture(rtex_t* texture, bool linear, bool mipmapping, bool blending, bool set_color);
// Take snapshot (F12). Backend-neutral entry point; the routing to the GL or
// Vulkan pixel source lives in aae_video/renderer_dispatch.cpp.
void snapshot();
// Shared snapshot writer, used by BOTH render chains so the two backends
// produce byte-identically named files. Expects a TIGHTLY PACKED RGBA8 buffer
// (width*4 bytes per row) whose scanline 0 is the TOP row of the image.
// Creates snap/, builds snap/<gamename>_<YYYYMMDDHHMMSS>.png and writes it.
bool snapshot_write_rgba8_png(const unsigned char* rgba, int width, int height);
// Deletes all registered textures
void destroy_all_textures();
// Retrieve loaded texture size
bool get_texture_size(rtex_t tex, int* outW, int* outH);
// Texture handling:
void load_artwork(const struct artworks* p);
int make_single_bitmap(rtex_t* texture, const char* filename, const char* archname, int mtype);

#endif