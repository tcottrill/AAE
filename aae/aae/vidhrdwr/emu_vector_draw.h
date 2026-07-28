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
// CONSTRAINT (deliberately not enforced by an #ifdef guard):
// this header must never include MathUtils.h, sys_gl.h, aae_mame_driver.h or
// anything else render- or platform-specific. Its include list above is the
// whole contract - keep it to two lines.
//
// An `#ifdef MATHUTILS_H / #error` guard was tried here and REMOVED: it tests
// "was MathUtils.h seen earlier in this translation unit", not "did this
// header pull it in". Render TUs include MathUtils.h legitimately, so the
// guard fired on correct code and forced unrelated files (vector_draw.cpp,
// winmain.cpp) to reorder their includes to appease it - with the error
// pointing at this header rather than the real file. Order-sensitive guards
// like that are false-positive generators; the short include list above and
// review are the real protection.

typedef struct colorsarray { int r, g, b; } colors;
extern colors vec_colors[256];

void add_tex(float ex, float ey, int intensity, rgb_t col);
void add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col);
void set_texture_id(rtex_t* id);
void cache_clear();

#endif
