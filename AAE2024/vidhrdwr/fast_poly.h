/*


This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/
#ifndef __FPOLY__
#define __FPOLY__

#pragma once

#include <vector>
#include "aae_mame_driver.h"
#include "colordefs.h"
#include "MathUtils.h"
#include "log.h"

//Rectangle block class, any size. "pixels" Nothing fancy supported here. 


class _fpdata
{
public:

	float x, y;
	uint32_t color;

	_fpdata() : x(0), y(0), color(0) { }
	_fpdata(float x, float y, uint32_t _color) : x(x), y(y), color(_color) {}
	_fpdata(const Vec2 &p, const uint32_t &_color) : x(p.x), y(p.y), color(_color) {}
};



class Fpoly
{

public:
	//Constructor	
	Fpoly();
	//Destructor
	~Fpoly();
	//Public Variables

	//Public Functions
	
	void addPoly(float x, float y, float size, uint32_t color);
	void Render();
	

private:
	//Private Variables
	std::vector<_fpdata> vertices;
	uint32_t color;
	int angle;

	

};


#endif

