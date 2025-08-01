﻿// texrect.cpp

// -----------------------------------------------------------------------------
// Game Engine Alpha - Generic Module
// Generic component or utility file for the Game Engine Alpha project. This
// file may contain helpers, shared utilities, or subsystems that integrate
// seamlessly with the engine's rendering, audio, and gameplay frameworks.
//
// Integration:
//   This library is part of the **Game Engine Alpha** project and is tightly
//   integrated with its texture management, logging, and math utility systems.
//
// Usage:
//   Include this module where needed. It is designed to work as a building block
//   for engine subsystems such as rendering, input, audio, or game logic.
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
// -----------------------------------------------------------------------------

#include "texrect.h"

Rect2::Rect2(int screen_width, int screen_height, float aspectRatio, int rotated) {
	// --- inline GLSL 1.20 shaders ---
	static const char* vs_src =
		"#version 120\n"
		"attribute vec2 a_position;\n"
		"attribute vec2 a_texcoord;\n"
		"varying vec2 v_texcoord;\n"
		"void main() {\n"
		"    gl_Position = gl_ModelViewProjectionMatrix * vec4(a_position, 0.0, 1.0);\n"
		"    v_texcoord = a_texcoord;\n"
		"}";
	static const char* fs_src =
		"#version 120\n"
		"varying vec2 v_texcoord;\n"
		"uniform sampler2D u_texture;\n"
		"void main() {\n"
		"    gl_FragColor = texture2D(u_texture, v_texcoord);\n"
		"}";

	GLuint vs = CompileShader(GL_VERTEX_SHADER, vs_src, "Rect2 VS");
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fs_src, "Rect2 FS");
	prog_ = LinkShaderProgram(vs, fs);

	// cache attribute/uniform locations
	pos_loc_ = glGetAttribLocation(prog_, "a_position");
	texcoord_loc_ = glGetAttribLocation(prog_, "a_texcoord");
	sampler_loc_ = glGetUniformLocation(prog_, "u_texture");

	// bind sampler once to texture‐unit 0
	glUseProgram(prog_);
	glUniform1i(sampler_loc_, 0);
	glUseProgram(0);

	LOG_INFO("Rect2 shader program initialized");
	// Call the screen rect setup immediately
	UpdateScreenRect(screen_width, screen_height, aspectRatio, rotated);
}

Rect2::~Rect2() {
	if (prog_) {
		glDeleteProgram(prog_);
		LOG_INFO("Rect2 shader program deleted");
	}
}

void Rect2::SetVertex(int idx, float x, float y, float tx, float ty) {
	verts_[idx] = _Point2DA(x, y, tx, ty);
}

void Rect2::BottomLeft(float x, float y, float tx, float ty) { SetVertex(0, x, y, tx, ty); }
void Rect2::TopLeft(float x, float y, float tx, float ty) { SetVertex(1, x, y, tx, ty); }
void Rect2::TopRight(float x, float y, float tx, float ty) { SetVertex(2, x, y, tx, ty); }
void Rect2::BottomRight(float x, float y, float tx, float ty) { SetVertex(3, x, y, tx, ty); }

void Rect2::BottomLeft(float x, float y) { BottomLeft(x, y, 0.0f, 0.0f); }
void Rect2::TopLeft(float x, float y) { TopLeft(x, y, 0.0f, 1.0f); }
void Rect2::TopRight(float x, float y) { TopRight(x, y, 1.0f, 1.0f); }
void Rect2::BottomRight(float x, float y) { BottomRight(x, y, 1.0f, 0.0f); }

void Rect2::Render(float scaley) {
	// build interleaved arrays on the stack
	GLfloat positions[8] = {
		verts_[0].x, verts_[0].y,
		verts_[1].x, verts_[1].y * scaley,
		verts_[2].x, verts_[2].y * scaley,
		verts_[3].x, verts_[3].y
	};
	GLfloat texcoords[8] = {
		verts_[0].tx, verts_[0].ty,
		verts_[1].tx, verts_[1].ty,
		verts_[2].tx, verts_[2].ty,
		verts_[3].tx, verts_[3].ty
	};
	// two triangles: 0-1-2, 2-3-0
	static const GLushort idx[6] = { 0,1,2, 2,3,0 };

	// use shader
	glUseProgram(prog_);

	// assume your texture was bound to GL_TEXTURE0 already
	glActiveTexture(GL_TEXTURE0);

	// feed in positions
	glEnableVertexAttribArray(pos_loc_);
	glVertexAttribPointer(pos_loc_, 2, GL_FLOAT, GL_FALSE, 0, positions);

	// feed in texcoords
	glEnableVertexAttribArray(texcoord_loc_);
	glVertexAttribPointer(texcoord_loc_, 2, GL_FLOAT, GL_FALSE, 0, texcoords);

	// draw
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);

	// cleanup
	glDisableVertexAttribArray(pos_loc_);
	glDisableVertexAttribArray(texcoord_loc_);
	glUseProgram(0);
}

void Rect2::UpdateScreenRect(int screen_width, int screen_height, float aspectRatio, int rotated)
{
	float indices[32] =
	{
		// normal
		0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f,
		// rotated right
		1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
		// rotated left
		0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
		// flip
		1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f
	};

	LOG_INFO("Screen size: %d x %d aspect %f", screen_width, screen_height, aspectRatio);

	if (aspectRatio <= 0.0f) {
		LOG_INFO("Invalid aspect ratio: %f. Defaulting to 4:3.", aspectRatio);
		aspectRatio = 4.0f / 3.0f;
	}

	if (rotated) {
		aspectRatio = 1.0f / aspectRatio;
	}

	float used_width = (float)screen_height * aspectRatio;
	float used_height = (float)screen_height;
	float xadj = 0.0f;
	float yadj = 0.0f;

	if (used_width > screen_width) {
		// Width doesn't fit — clamp to screen width instead
		used_width = (float)screen_width;
		used_height = used_width / aspectRatio;
		yadj = ((float)screen_height - used_height) / 2.0f;
	}
	else {
		xadj = ((float)screen_width - used_width) / 2.0f;
	}

	LOG_INFO("Target width: %f", used_width);
	LOG_INFO("X adjustment: %f", xadj);
	LOG_INFO("Y adjustment: %f", yadj);

	int v = 8 * rotated;

	BottomLeft(xadj, yadj, indices[v], indices[v + 1]);
	TopLeft(xadj, yadj + used_height, indices[v + 2], indices[v + 3]);
	TopRight(xadj + used_width, yadj + used_height, indices[v + 4], indices[v + 5]);
	BottomRight(xadj + used_width, yadj, indices[v + 6], indices[v + 7]);

	LOG_INFO("BL: (%f, %f)", xadj, yadj);
	LOG_INFO("TL: (%f, %f)", xadj, yadj + used_height);
	LOG_INFO("TR: (%f, %f)", xadj + used_width, yadj + used_height);
	LOG_INFO("BR: (%f, %f)", xadj + used_width, yadj);
}