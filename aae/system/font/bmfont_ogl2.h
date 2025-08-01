#pragma once
// -----------------------------------------------------------------------------
// BMFont Library - Bitmap Font Rendering System
// High-performance bitmap font rendering system supporting both OpenGL 2.x
// (immediate mode) and modern OpenGL 3+/4.x (VAO/VBO with shaders). This library
// loads AngelCode .fnt definitions, parses glyph metrics, and renders text with
// kerning, scaling, rotation, alignment, and batching support.
//
// Features:
//   - Parses AngelCode BMFont (.fnt) files for glyph layout and kerning.
//   - Supports both legacy OpenGL 2.x and modern OpenGL 4.x rendering paths.
//   - Provides real-time text rendering with batching and optional caching.
//   - Adjustable font scale, rotation, blending, alignment, and origin modes.
//   - Kerning support for precise character spacing.
//   - Optional text caching to improve performance in static UIs.
//
// Integration:
//   This library is an integral part of the **Game Engine Alpha** project.
//   It is designed to work seamlessly with the engine’s rendering pipeline and
//   texture management systems (`sys_texture`, `Vertex_Helpers`, etc.).
//
// Dependencies:
//   - OpenGL (2.x legacy or 4.x core profile)
//   - TEX texture system for image loading
//   - AngelCode .fnt files with corresponding texture atlas
//   - Logging and math utilities from Game Engine Alpha
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


#ifndef __BMFONT__
#define __BMFONT__

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include "colordefs.h"
#include "MathUtils.h"
#include "gl_basics.h"
#include "sys_texture.h"
#include "Vertex_Helpers.h"

// -----------------------------------------------------------------------------
// BMFont - Bitmap Font Renderer
// Loads AngelCode .fnt file and renders bitmap glyphs with kerning, scaling, 
// alignment, rotation, and caching support. Optimized for OpenGL 2.x-3.x.
// -----------------------------------------------------------------------------
class BMFont
{
public:
	// -------------------------------------------------------------------------
	// Font Alignment and Origin Modes
	// -------------------------------------------------------------------------
	enum FontAlign : int { FONT_ALIGN_NONE, FONT_ALIGN_NEAR, FONT_ALIGN_CENTER, FONT_ALIGN_FAR };
	enum FontOrigin : int { FONT_ORIGIN_TOP, FONT_ORIGIN_BOTTOM };

	// -------------------------------------------------------------------------
	// Constructor / Destructor
	// -------------------------------------------------------------------------
	BMFont(int width, int height);
	~BMFont();

	// -------------------------------------------------------------------------
	// Load & Configuration
	// -------------------------------------------------------------------------
	bool  loadFont(const std::string& fontFile);
	void  setPath(const std::string& path);
	void  setScale(float scale);
	void  setAngle(int degrees);
	void  setBlend(int mode);
	void  setAlign(FontAlign align);
	void  setOrigin(FontOrigin origin);
	void  setCaching(bool enable);
	void  useKerning(bool enable);
	void  setColor(rgb_t color);
	void  setColor(int r, int g, int b, int a);
	void  clearCache();
	float setHeight() const;

	std::string GetPath() const;

	// -------------------------------------------------------------------------
	// Text Rendering
	// -------------------------------------------------------------------------
	void  fPrint(float x, float y, const char* fmt, ...);
	void  fPrint(float scale, float x, float y, const char* fmt, ...);
	void  fPrint(float x, float y, rgb_t color, float scale, const char* fmt, ...);
	void  fPrint(float y, rgb_t color, float scale, int angle, const char* fmt, ...);
	void  fPrintCenter(float y, char* text);

	// -------------------------------------------------------------------------
	// Measurements
	// -------------------------------------------------------------------------
	float getStrWidth(const char* text);
	float getStrWidth(const std::string& text);

	// -------------------------------------------------------------------------
	// Render
	// -------------------------------------------------------------------------
	void  Render();

private:
	// -------------------------------------------------------------------------
	// Character Definition
	// -------------------------------------------------------------------------
	struct CharDescriptor {
		short x = 0, y = 0;
		short width = 0, height = 0;
		short xOffset = 0, yOffset = 0;
		short xAdvance = 0;
		short page = 0;
		STCoords st;
	};

	// -------------------------------------------------------------------------
	// Kerning Info
	// -------------------------------------------------------------------------
	struct KerningInfo {
		int first = 0;
		int second = 0;
		int amount = 0;
	};

	// -------------------------------------------------------------------------
	// Internal State
	// -------------------------------------------------------------------------
	short lineHeight = 0;
	short base = 0;
	short sheetWidth = 0, sheetHeight = 0;
	short pages = 1;
	short outline = 0;

	std::string face;
	short size = 0;
	short bold = 0;
	short italic = 0;
	short charset = 0;

	std::string imageFileName;
	std::string dataPath;

	std::map<int, CharDescriptor> chars;
	std::unordered_map<std::size_t, int> kernMap;

	std::unique_ptr<TEX> texture;
	std::vector<TexVertex> vertices;

	int surfaceWidth = 0;
	int surfaceHeight = 0;

	rgb_t fontColor = RGB_WHITE;
	float fontScale = 1.0f;
	int fontBlend = 0;
	int fontAngle = 0;
	int fontAlign = FONT_ALIGN_NONE;
	bool fontOriginBottom = true;
	bool useKerning = true;
	bool cachingEnabled = false;

	float originalX = 0.0f;
	std::size_t kernCount = 0;

	// -------------------------------------------------------------------------
	// Internal Helpers
	// -------------------------------------------------------------------------
	bool  parseFont(const std::string& file);
	void  calculateTextureCoords(CharDescriptor& c);
	void  printString(float x, float y, char* text);
	int   getKerningPair(int first, int second);
	Vec2  rotateAroundPoint(float x, float y, float cx, float cy, float cosTheta, float sinTheta);
	void  printKerningPairs(); // debug only
};

#endif
