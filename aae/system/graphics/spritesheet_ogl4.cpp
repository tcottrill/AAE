// -----------------------------------------------------------------------------
// Sprite Sheet Rendering Library - OpenGL 2.x and OpenGL 4.x Backends
// Unified sprite batching and rendering system supporting both legacy OpenGL 2
// and modern OpenGL 4 instanced rendering. Handles loading of TexturePacker JSON
// atlases, sprite metadata management, and efficient rendering with rotation,
// scaling, color modulation, z-index sorting, and batching.
//
// Features:
//   - Loads TexturePacker JSON definitions and associated sprite sheet textures.
//   - Supports both OpenGL 2 immediate-mode rendering and OpenGL 4 instanced VBO/VAO rendering.
//   - Per-sprite transformations: scaling, rotation, pivot offsets.
//   - Batching with optional z-index sorting for correct draw order.
//   - Debug features including optional point/overlay rendering.
//   - Ability to dump each sprite to a PNG file using framebuffer capture.
//
// Integration:
//   This library is part of the **Game Engine Alpha** project and is tightly
//   integrated with its texture management, logging, and math utility systems.
//
// Usage:
//   (Example usage is already included in this header; see below.)
//
// Dependencies:
//   - OpenGL (2.x legacy and 4.x core profile supported)
//   - GLSL shader utilities (OpenGL 4 backend)
//   - TEX texture loader from Game Engine Alpha
//   - nlohmann/json for sprite sheet parsing
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


// -----------------------------------------------------------------------------
// spritesheet_ogl4.cpp
//
// Description:
// OpenGL 4.0+ instanced sprite renderer using shared Sprite class.
// -----------------------------------------------------------------------------

#define USE_OGL4 1

#include "spritesheet.h"
#include "sys_gl.h"
#include "shader_util.h"
//#include "debug_draw.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"       // Needed for glm::value_ptr
#include <filesystem>
#include <unordered_set>

static const char* vertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec2 in_quad;
layout(location = 1) in vec4 in_color;
layout(location = 2) in float in_angle;
layout(location = 3) in float in_scale;
layout(location = 4) in vec2 in_center;
layout(location = 5) in vec2 in_offset;
layout(location = 6) in vec2 in_size;
layout(location = 7) in vec2 in_uvTL;
layout(location = 8) in vec2 in_uvBR;
layout(location = 9) in float in_zIndex;

out vec2 frag_uv;
out vec4 frag_color;

uniform mat4 u_proj;

void main() {
	float rad = radians(in_angle);
	float c = cos(rad);
	float s = sin(rad);

	vec2 pos = in_quad * in_size * in_scale - in_offset * in_scale;
	vec2 rotated = vec2(pos.x * c - pos.y * s, pos.x * s + pos.y * c);
	vec2 world = rotated + in_center;

	vec2 uv = vec2(
		mix(in_uvTL.x, in_uvBR.x, in_quad.x + 0.5),
		mix(in_uvBR.y, in_uvTL.y, in_quad.y + 0.5) // Flip Y
	);

	gl_Position = u_proj * vec4(world, in_zIndex, 1.0);
	frag_uv = uv;
	frag_color = in_color;
}
)";

static const char* fragmentShaderSrc = R"(
#version 330 core
in vec2 frag_uv;
in vec4 frag_color;
out vec4 out_color;

uniform sampler2D tex;

void main() {
	out_color = texture(tex, frag_uv) * frag_color;
}
)";

#ifdef USE_OGL4

void Sprite::InitGL(int screenW, int screenH)
{
	screenWidth = screenW;
	screenHeight = screenH;

	GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexShaderSrc, "Sprite Vertex");
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc, "Sprite Fragment");
	shaderProgram = LinkShaderProgram(vs, fs);

	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	static const float quadVerts[] = {
		-0.5f,  0.5f,
		 0.5f,  0.5f,
		-0.5f, -0.5f,
		 0.5f, -0.5f
	};
	glGenBuffers(1, &vbo_quad);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_quad);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glVertexAttribDivisor(0, 0); // Per-vertex

	glGenBuffers(1, &vbo_instance);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_instance);
	glBufferData(GL_ARRAY_BUFFER, 1000 * sizeof(SpriteInstance), nullptr, GL_DYNAMIC_DRAW);

#define ATTRIBF(loc, count, type, norm, field) \
	glEnableVertexAttribArray(loc); \
	glVertexAttribPointer(loc, count, type, norm, sizeof(SpriteInstance), (void*)offsetof(SpriteInstance, field)); \
	glVertexAttribDivisor(loc, 1)

	ATTRIBF(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, color);
	ATTRIBF(2, 1, GL_FLOAT, GL_FALSE, angle);
	ATTRIBF(3, 1, GL_FLOAT, GL_FALSE, scale);
	ATTRIBF(4, 2, GL_FLOAT, GL_FALSE, centerX);
	ATTRIBF(5, 2, GL_FLOAT, GL_FALSE, offset);
	ATTRIBF(6, 2, GL_FLOAT, GL_FALSE, size);
	ATTRIBF(7, 2, GL_FLOAT, GL_FALSE, uvTL);
	ATTRIBF(8, 2, GL_FLOAT, GL_FALSE, uvBR);
	ATTRIBF(9, 1, GL_FLOAT, GL_FALSE, zIndex);
#undef ATTRIBF

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Sprite::InternalAdd(float x, float y, int spriteid, float z)
{
	const SpriteDescriptor& f = sprites[spriteid];
	SpriteInstance s;
	s.color = color;
	s.angle = f.angle;
	s.scale = f.scale;
	s.centerX = x;
	s.centerY = y;
	s.offset = f.offset;
	s.size = Vec2((float)f.width, (float)f.height);
	s.uvTL = f.uvs[0]; // TL
	s.uvBR = f.uvs[2]; // BR
	s.zIndex = z;

	//debugAddPoint(x, y); // <-- Add here to visualize sprite center


	instances.push_back(s);
}

void Sprite::InternalAdd(float x, float y, int spriteid)
{
	InternalAdd(x, y, spriteid, 1.0f);
}

void Sprite::Render()
{
	if (!texture || instances.empty()) return;

	glUseProgram(shaderProgram);

	glm::mat4 proj = glm::ortho(0.0f, (float)screenWidth, 0.0f, (float)screenHeight);
	GLint projLoc = glGetUniformLocation(shaderProgram, "u_proj");
	if (projLoc >= 0)
		glUniformMatrix4fv(projLoc, 1, GL_FALSE, &proj[0][0]);

	GLint texLoc = glGetUniformLocation(shaderProgram, "tex");
	glUniform1i(texLoc, 0);

	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	texture->UseTexture(1, 1);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_instance);

	if (sortZIndex) {
		std::sort(instances.begin(), instances.end(),
			[](const SpriteInstance& a, const SpriteInstance& b) {
				return a.zIndex < b.zIndex;
			});
	}

	glBufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(SpriteInstance), instances.data(), GL_DYNAMIC_DRAW);
	glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)instances.size());

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);

	//debugDrawAll(proj);  // <- insert here to draw debug overlays

	if (!keepInstances)
		instances.clear();
}

// -----------------------------------------------------------------------------
// Dumps each loaded sprite to a PNG using an offscreen framebuffer.
// -----------------------------------------------------------------------------
void Sprite::DumpAllSpritesToPNG()
{
	namespace fs = std::filesystem;
	fs::create_directory("dump");

	GLint prevFBO = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

	glm::mat4 projOriginal = glm::ortho(0.0f, (float)screenWidth, 0.0f, (float)screenHeight);

	glUseProgram(shaderProgram);
	glBindVertexArray(vao);

	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	GLuint tex = 0;

	for (int i = 0; i < static_cast<int>(sprites.size()); ++i)
	{
		const SpriteDescriptor& desc = sprites[i];
		const Vec2* uv = desc.uvs;

		// Compute trimmed sprite size from UVs
		float uSpan = std::abs(uv[2].x - uv[0].x);
		float vSpan = std::abs(uv[2].y - uv[0].y);
		int trimmedW = static_cast<int>(std::round(uSpan * sheetWidth));
		int trimmedH = static_cast<int>(std::round(vSpan * sheetHeight));

		if (trimmedW <= 0 || trimmedH <= 0) {
			LOG_INFO("Skipping sprite %s due to invalid size (%d x %d)", desc.name.c_str(), trimmedW, trimmedH);
			continue;
		}

		// Create output framebuffer
		if (tex) glDeleteTextures(1, &tex);
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, trimmedW, trimmedH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			LOG_ERROR("FBO incomplete for sprite %d", i);
			continue;
		}

		// Set up orthographic projection matching trimmed size
		glViewport(0, 0, trimmedW, trimmedH);
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);

		glm::mat4 projFBO = glm::ortho(0.0f, (float)trimmedW, 0.0f, (float)trimmedH);
		GLint loc = glGetUniformLocation(shaderProgram, "u_proj");
		if (loc != -1)
			glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(projFBO));

		// SpriteInstance adjusted for [0..1] a_pos quad layout
		SpriteInstance s;
		s.color = RGB_WHITE;
		s.angle = 0.0f;
		s.scale = 1.0f;
		s.centerX = trimmedW * 0.5f;
		s.centerY = trimmedH * 0.5f;
		s.size = Vec2((float)trimmedW * 2.0f, (float)trimmedH * 2.0f);
		s.offset = Vec2((float)-trimmedW * 0.5f, (float)-trimmedH * 0.5f);
		s.uvTL = uv[0];
		s.uvBR = uv[2];
		s.zIndex = 1.0f;

		instances.clear();
		instances.push_back(s);

		Render();

		TEX::SaveFramebufferToPNG(trimmedW, trimmedH, desc.name, "dump");
	}

	// Restore original framebuffer and projection
	glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
	glViewport(0, 0, screenWidth, screenHeight);

	GLint loc = glGetUniformLocation(shaderProgram, "u_proj");
	if (loc != -1)
		glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(projOriginal));

	if (tex) glDeleteTextures(1, &tex);
	glDeleteFramebuffers(1, &fbo);

	glBindVertexArray(0);
	glUseProgram(0);

	LOG_INFO("DumpAllSpritesToPNG complete.");
}



#endif // USE_OGL4