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
// spritesheet_ogl2.cpp
//
// Description:
// OpenGL 2.0 immediate-mode sprite renderer (shared header: spritesheet.h)
// -----------------------------------------------------------------------------

#include "spritesheet.h"
#include "gl_basics.h"
#include "sys_log.h"
#include "debug_draw.h"

#include <algorithm>

#ifdef USE_OGL2

void Sprite::InitGL(int screenW, int screenH)
{
	screenWidth = screenW;
	screenHeight = screenH;
	// No shader setup required for OGL2
}

void Sprite::InternalAdd(float x, float y, int spriteid, float z)
{
	const SpriteDescriptor& f = sprites[spriteid];
	Vec2 center(x, y);
	debugAddPoint(center.x, center.y);

	bool doRotate = (f.angle != 0);
	float cosTheta = 1.0f, sinTheta = 0.0f;
	if (doRotate) {
		float radians = (f.angle * k_pi) / 180.0f;
		cosTheta = cosf(radians);
		sinTheta = sinf(radians);
	}

	float hw = f.width * 0.5f * f.scale;
	float hh = f.height * 0.5f * f.scale;

	Vec2 corners[4] = {
		{ -hw,  hh },
		{  hw,  hh },
		{  hw, -hh },
		{ -hw, -hh }
	};

	if (f.offset.x != 0.0f || f.offset.y != 0.0f) {
		Vec2 pivot = f.offset * f.scale;
		for (auto& c : corners)
			c -= pivot;
	}

	if (doRotate) {
		for (auto& c : corners) {
			float px = c.x, py = c.y;
			c.x = px * cosTheta - py * sinTheta;
			c.y = px * sinTheta + py * cosTheta;
		}
	}

	for (auto& c : corners)
		c += center;

	const Vec2* uv = f.uvs;

	vertices.emplace_back(TexVertex{ corners[0].x, corners[0].y, z, uv[0].x, uv[0].y, color });
	vertices.emplace_back(TexVertex{ corners[1].x, corners[1].y, z, uv[1].x, uv[1].y, color });
	vertices.emplace_back(TexVertex{ corners[2].x, corners[2].y, z, uv[2].x, uv[2].y, color });
	vertices.emplace_back(TexVertex{ corners[2].x, corners[2].y, z, uv[2].x, uv[2].y, color });
	vertices.emplace_back(TexVertex{ corners[3].x, corners[3].y, z, uv[3].x, uv[3].y, color });
	vertices.emplace_back(TexVertex{ corners[0].x, corners[0].y, z, uv[0].x, uv[0].y, color });
}

void Sprite::InternalAdd(float x, float y, int spriteid)
{
	InternalAdd(x, y, spriteid, 1.0f);
}

void Sprite::Render()
{
	if (!texture || vertices.empty())
		return;

	if (sortZIndex) {
		std::sort(vertices.begin(), vertices.end(), [](const TexVertex& a, const TexVertex& b) {
			return a.z < b.z;
			});
	}

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	texture->UseTexture(1, 1);
	texture->SetBlendMode(blend);

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, sizeof(TexVertex), &vertices[0].x);

	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, sizeof(TexVertex), &vertices[0].tx);

	glEnableClientState(GL_COLOR_ARRAY);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(TexVertex), &vertices[0].color);

	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size());

	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);

	if (!keepInstances)
		vertices.clear();
}

void Sprite::DumpAllSpritesToPNG()
{
	if (sprites.empty()) {
		LOG_INFO("No sprites loaded to dump.");
		return;
	}

	GLint prevFbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);

	for (size_t i = 0; i < sprites.size(); ++i) {
		const SpriteDescriptor& desc = sprites[i];
		if (desc.width <= 0 || desc.height <= 0)
			continue;

		int w = static_cast<int>(desc.width);
		int h = static_cast<int>(desc.height);

		glViewport(0, 0, w, h);

		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0, w, h, 0, -1, 1);  // top-left origin

		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();

		// Clear with transparent black
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		// Enable blending and proper blend mode
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Enable texture
		glBindTexture(GL_TEXTURE_2D, texture->GetTexID());
		glEnable(GL_TEXTURE_2D);

		glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // use full texture alpha

		const Vec2* uv = desc.uvs;

		glBegin(GL_QUADS);
		glTexCoord2f(uv[0].x, uv[0].y); glVertex2f(0.0f, 0.0f);
		glTexCoord2f(uv[1].x, uv[1].y); glVertex2f((float)w, 0.0f);
		glTexCoord2f(uv[2].x, uv[2].y); glVertex2f((float)w, (float)h);
		glTexCoord2f(uv[3].x, uv[3].y); glVertex2f(0.0f, (float)h);
		glEnd();

		glDisable(GL_TEXTURE_2D);
		glDisable(GL_BLEND);

		// Clean name
		std::string baseName = desc.name;
		size_t dot = baseName.find_last_of('.');
		if (dot != std::string::npos)
			baseName = baseName.substr(0, dot);

		TEX::SaveFramebufferToPNG(w, h, baseName, "dump");

		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();

		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
	}

	glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

	LOG_INFO("DumpAllSpritesToPNG complete.");
}


#endif // USE_OGL2