#pragma once

// -----------------------------------------------------------------------------
// This file is part of the AAE (Another Arcade Emulator) project.
// This Code is copyright (C) 2025/2026 Tim Cottrill and released
// under the GNU GPL v3 or later. See the accompanying source files for full
// license details.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// TiledEffect - CRT Scanline Effect (GL 3.3).
// Replaces old texture tiling with mathematically perfect native scanlines.
// Automatically maps a texture 1:1 to native game pixels or uses a stunning 
// procedural math fallback if no texture is provided.
// -----------------------------------------------------------------------------

#include "sys_gl.h"       // GLEW setup, GL types
#include "shader_util.h"  // CompileShader, logging

// Initialize the shader program and VBO. Call once during startup.
bool TiledEffect_Init(void);

// Clean up shader program and VBO. Call on application exit.
void TiledEffect_Shutdown(void);

// Draws a scanline/CRT effect perfectly aligned to the game's native resolution.
// 'tex'     - The GL texture handle (pass 0 for a high-quality procedural scanline).
// 'gameW'   - The native horizontal resolution of the game (e.g., 320).
// 'gameH'   - The native vertical resolution of the game (e.g., 240).
// 'opacity' - Controls the strength/darkness of the scanlines (0.0 to 1.0).
void TiledEffect_Draw(GLuint tex, int gameW, int gameH, float opacity, float scale);