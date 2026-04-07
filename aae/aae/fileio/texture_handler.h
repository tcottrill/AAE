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

#include "aae_mame_driver.h"

extern GLuint error_tex[2];
extern GLuint pause_tex[2];
extern GLuint fun_tex[4];
extern GLuint art_tex[8];
extern GLuint game_tex[10];
extern GLuint menu_tex[7];

// Load
GLuint load_texture(const char* filename, const char* archname, int numcomponents, int filter, bool modernGL = true);
// Use
void set_texture(GLuint* texture, GLboolean linear, GLboolean mipmapping, GLboolean blending, GLboolean set_color);
// Take snapshot
void snapshot();
// Deletes all registered textures
void destroy_all_textures();
// Retrieve loaded texture size
bool get_texture_size(GLuint tex, int* outW, int* outH);
// Texture handling:
void load_artwork(const struct artworks* p);
int make_single_bitmap(GLuint* texture, const char* filename, const char* archname, int mtype);

#endif