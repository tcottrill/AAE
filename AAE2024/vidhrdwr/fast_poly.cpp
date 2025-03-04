/*
ver .1 2019
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
--------------------------------------------------------------------------------
*/

#include "fast_poly.h"
#include "allegro.h"
#include "alleggl.h"


Fpoly::~Fpoly()
{
	wrlog("Class Destructor called");
}


Fpoly::Fpoly()
{
	//Init Stuff Here
	color = RGB_WHITE;
	angle = 0;

};


void Fpoly::addPoly(float x, float y, float size, uint32_t color)
{
	// Adjust for scaling, may need to flip vertices depending on drawing.!

	x = x * size;
	y = y * size;

	//V0
	vertices.emplace_back(x, y, color);
	//V1
	vertices.emplace_back(x - size, y, color);
	//V2
	vertices.emplace_back(x - size, y + size, color);
	//V2
	vertices.emplace_back(x - size, y + size, color);
	//V3
	vertices.emplace_back(x, y + size, color);
	//V0
	vertices.emplace_back(x, y, color);
	
}


void Fpoly::Render()
{
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, sizeof(_fpdata), &vertices[0].x);
	glEnableClientState(GL_COLOR_ARRAY);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(_fpdata), &vertices[0].color);

	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size());

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	vertices.clear();
}
