//-----------------------------------------------------------------------------
// Copyright (c) 2011-2012
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------
#pragma once

#ifndef EMU_VECTOR_DRAW_H
#define EMU_VECTOR_DRAW_H

// ===========================================================================
// emu_vector_draw.h - the emulation-side vector seam.
//
// Drivers and vector generators (DVG/AVG/CCPU) call add_line/add_tex. The
// backend decides what that means: the GL/VK backend queues vertices; a
// Teensy backend drives X/Y DACs directly. Nothing render-specific may be
// declared here.
// ===========================================================================

#include "colordefs.h"      // rgb_t
#include "render_types.h"   // rtex_t - already backend-neutral (plain uint32_t)

// Boundary guard: the emu-side vector header must not drag in render math.
// Only colordefs.h and render_types.h belong here.
#ifdef MATHUTILS_H
#error "MathUtils.h reached the emu-side vector header"
#endif

typedef struct colorsarray { int r, g, b; } colors;
extern colors vec_colors[256];

void add_tex(float ex, float ey, int intensity, rgb_t col);
void add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col);
void set_texture_id(rtex_t* id);
void cache_clear();

#endif
