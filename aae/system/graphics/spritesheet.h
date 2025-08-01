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
// spritesheet.h
//
// Description:
// Unified sprite batching and rendering system using OpenGL 2 or OpenGL 4.
// This header is shared across both backends and includes core sprite sheet
// parsing, sprite instance management, and metadata access.
//
// Usage:
// Define one of the following at compile time to enable the backend:
//   #define USE_OGL2   // Use OpenGL 2 legacy renderer
//   #define USE_OGL4   // Use OpenGL 4 instanced renderer
//
// Only one backend may be compiled per translation unit.
//
// Example:
// Sprite sprite;
// sprite.LoadSheet("atlas.json");
// sprite.InitGL(800, 600);
//
// int id = sprite.ByName("player");
//
// // Method 1: Parameter-style
// sprite.Add(400, 300, id, RGB_WHITE, 1.0f, 0.0f, 1.0f);
//
// // Method 2: Using SpriteParams
// sprite.Add({
//     .pos = { 100, 200 },
//     .color = RGB_RED,
//     .scale = 0.75f,
//     .angle = 45.0f,
//     .zIndex = 2.0f,
//     .spriteID = id
// });
//
// -----------------------------------------------------------------------------
#pragma once

#ifndef SPRITESHEET_H
#define SPRITESHEET_H

#include "sys_gl.h"
#include "sys_texture.h"
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include "MathUtils.h"
#include "colordefs.h"
#include "Vertex_Helpers.h"

// -----------------------------------------------------------------------------
// SpriteParams - user-facing structure for drawing a sprite instance
// -----------------------------------------------------------------------------
struct SpriteParams {
	Vec2     pos = { 0.0f, 0.0f };    // World position
	rgb_t    color = RGB_WHITE;      // RGBA color
	float    scale = 1.0f;           // Per-instance scale
	float    angle = 0.0f;           // Rotation angle in degrees
	float    zIndex = 1.0f;          // Depth ordering key
	int      spriteID = -1;          // Index into sprite sheet

	SpriteParams(Vec2 p, int id,
		rgb_t c = RGB_WHITE,
		float s = 1.0f,
		float a = 0.0f,
		float z = 1.0f)
		: pos(p), color(c), scale(s), angle(a), zIndex(z), spriteID(id) {
	}
};

// -----------------------------------------------------------------------------
// SpriteDescriptor - metadata for a single sprite inside the atlas
// -----------------------------------------------------------------------------
class SpriteDescriptor
{
	friend class Sprite;
private:
	short x{ 0 }, y{ 0 };
	short width{ 0 }, height{ 0 };
	std::string name;
	Vec2 offset{};
	Vec2 uvs[4]{};
	float scale{ 1.0f };
	int angle{ 0 };
	int rotation{ 0 };

	SpriteDescriptor() = default;
};

// -----------------------------------------------------------------------------
// Sprite - main interface for loading and rendering sprites
// -----------------------------------------------------------------------------
class Sprite
{
public:
	Sprite();
	~Sprite();

	// Loads a JSON sprite sheet definition (TexturePacker format)
	bool LoadSheet(std::string fontfile);

	// Initializes OpenGL resources (only in OGL4, dummy in OGL2)
	void InitGL(int screenW, int screenH);

	// Sets the global draw color (used if no per-instance override)
	void SetColor(int r, int g, int b, int a);
	void SetColor(rgb_t newColor);

	// Sets global blend mode (0 = normal alpha blend)
	void SetBlend(int b);

	// Overrides scale for a specific sprite ID
	void SetScale(int spritenum, float scale);

	// Sets rotation angle for a specific sprite
	void SetRotationAngle(int spritenum, int sangle);

	// Sets sprite pivot offset (e.g. for rotation centering)
	void SetOriginOffset(float x, float y, int spritenum);
	void SetOriginOffset(Vec2 a, int spritenum);
	Vec2 GetOriginOffset(int spritenum);

	// Enables batching across frames (keeps draw queue alive)
	void EnableBatching(bool enable);

	// Enables sorting draw queue by zIndex before rendering
	void SetZSort(bool enable);

	// Adds a sprite to the current render batch
	void Add(float x, float y, int spriteid,
		rgb_t color = RGB_WHITE,
		float scale = 1.0f,
		float angle = 0.0f,
		float zIndex = 1.0f);
	inline void Add(Vec2 pos, int spriteid,
		rgb_t color = RGB_WHITE,
		float scale = 1.0f,
		float angle = 0.0f,
		float zIndex = 1.0f)
	{
		Add(pos.x, pos.y, spriteid, color, scale, angle, zIndex);
	}
	void Add(const SpriteParams& params);

	void DumpAllSpritesToPNG();

	// Renders all sprites in the queue
	void Render(); // Must be implemented in backend source

	// Name -> ID lookup for sprites
	int ByName(const std::string name);
	std::string ByNum(int spritenum);

	// Sprite dimension helpers
	int GetSpriteWidth(int spritenum) const;
	int GetSpriteHeight(int spritenum) const;
	Vec2 GetSpriteSize(int spritenum) const;

	// Set or get sprite data path (used for JSON and PNG)
	void SetPath(const std::string& path);
	void SetPath(const char* path);
	std::string GetPath();

	// Returns GL texture ID of loaded sprite sheet
	GLuint GetTextureID() const { return texture ? texture->GetTexID() : 0; }

	// OGL4 access to submitted instances (for inspection/debug)
	const std::vector<SpriteInstance>& GetInstances() const { return instances; }

	// Current screen resolution (set in InitGL)
	int screenWidth{ 0 };
	int screenHeight{ 0 };

private:
	// OpenGL 4 rendering resources (unused in OGL2)
	GLuint vao = 0;
	GLuint vbo_quad = 0;
	GLuint vbo_instance = 0;
	GLuint shaderProgram = 0;

	// Sheet data and descriptors
	std::unique_ptr<TEX> texture;
	std::vector<SpriteDescriptor> sprites;
	std::string pngFileName;
	std::string dataPath;
	short sheetWidth{ 0 }, sheetHeight{ 0 };
	short spriteCount{ 0 };

	// OGL4 uses instances, OGL2 uses vertices
	std::vector<SpriteInstance> instances;
	std::vector<TexVertex> vertices;

	// Global modifiers
	rgb_t color = RGB_WHITE;
	float globalScale{ 1.0f };
	int blend{ 0 };
	int globalAngle{ 0 };
	bool keepInstances{ false };
	bool sortZIndex{ false };

	// Internal helpers
	bool ParseDefinition(const std::string&);
	void InternalAdd(float x, float y, int spriteid, float z);
	void InternalAdd(float x, float y, int spriteid);
};

#endif // SPRITESHEET_H
