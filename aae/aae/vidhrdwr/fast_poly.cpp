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

/*
ver .5 2026
This is free and unencumbered software released into the public domain.
*/

#include "fast_poly.h"
#include "sys_gl.h"
#include "gl_shader.h"
#include "opengl_renderer.h"  // g_proj
#include "MathUtils.h"        // aae::math::value_ptr
#include <cstddef>            // offsetof

Fpoly::Fpoly() = default;

Fpoly::~Fpoly() {
	LOG_INFO("Class Destructor called");
}

void Fpoly::addPoly(float x, float y, float size, uint32_t color) {
	float x0 = x * size;
	float y0 = y * size;
	float x1 = x0 + size;
	float y1 = y0 + size;

	// Two triangles forming a rectangle (quad)
	vertices.emplace_back(x0, y0, color); // V0
	vertices.emplace_back(x1, y0, color); // V1
	vertices.emplace_back(x1, y1, color); // V2

	vertices.emplace_back(x1, y1, color); // V2
	vertices.emplace_back(x0, y1, color); // V3
	vertices.emplace_back(x0, y0, color); // V0
}

void Fpoly::Render() {
	if (vertices.empty()) return;

	if (!vao) {
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(_fpdata), (void*)offsetof(_fpdata, x));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(_fpdata), (void*)offsetof(_fpdata, color));
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	bind_shader(fragBasicColor);
	set_uniform_mat4f(fragBasicColor, "uProj", aae::math::value_ptr(g_proj));

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(_fpdata), vertices.data(), GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	unbind_shader();

	vertices.clear();
}