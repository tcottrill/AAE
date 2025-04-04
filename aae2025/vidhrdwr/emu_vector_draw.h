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

#include <vector>
#include "aae_mame_driver.h"
#include "colordefs.h"

#include "sys_gl.h"


typedef struct colorsarray { int r, g, b; } colors;
extern colors vec_colors[1024];


class fpoint
{
public:
	float x;
	float y;
	rgb_t color;
	
	fpoint(float x, float y, rgb_t color ) : x(x), y(y), color(color) {}
	fpoint() : x(0), y(0), color(0) {}
	~fpoint() {};
};

//This too can be replaced 
class txdata
{
public:

	float x, y;
	float tx, ty;
	rgb_t color;

	txdata() : x(0), y(0), tx(0), ty(0), color(0) { }
	txdata(float x, float y, float tx, float ty, rgb_t color) : x(x), y(y), tx(tx), ty(ty), color(color) {}
};





void add_tex(float ex, float ey, int intensity, rgb_t col);
void add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col);
void draw_all();
void set_texture_id(GLuint *id);
void set_blendmode(GLenum sfactor, GLenum dfactor);
void cache_clear();
rgb_t cache_color(int intensity, rgb_t col);
rgb_t cache_tex_color(int intensity, rgb_t col);
void cache_texpoint(float ex, float ey, float tx, float ty, int intensity, rgb_t col);


#endif