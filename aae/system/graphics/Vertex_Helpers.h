#pragma once

#ifndef __VERTEX_HELP__
#define __VERTEX_HELP__

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


#include "colordefs.h"
// -----------------------------------------------------------------------------
// Helper Vertex Structures
// -----------------------------------------------------------------------------
// Static quad vertex (used for instancing)
struct QuadVertex {
	float x, y;
	QuadVertex() = default;
	QuadVertex(float x, float y) : x(x), y(y) {}
};

// One instance = one sprite
struct SpriteInstance {
	rgb_t color;
	float angle;
	float scale;
	float centerX, centerY;
	Vec2 offset;     // pivot in pixels
	Vec2 size;       // width, height
	Vec2 uvTL;       // top-left UV
	Vec2 uvBR;       // bottom-right UV
	float zIndex = 1.0f; // NEW — for layer sorting or Z-depth

	SpriteInstance() = default;
};
/*
struct TexVertex {
	float x, y;       // local vertex (relative to center)
	float tx, ty;     // UV
	rgb_t color;
	float angle;
	float scale;
	float centerX, centerY; // new

	TexVertex() = default;

	TexVertex(float x, float y, float tx, float ty, rgb_t color,
		float angle, float scale, float centerX, float centerY)
		: x(x), y(y), tx(tx), ty(ty), color(color),
		angle(angle), scale(scale),
		centerX(centerX), centerY(centerY) {
	}
};
*/
struct TexVertex {
	float x{ 0.0f }, y{ 0.0f }, z{ 0.0f };
	float tx{ 0.0f }, ty{ 0.0f };
	rgb_t color{};

	TexVertex() = default;

	TexVertex(float x, float y, float z, float tx, float ty, rgb_t color)
		: x(x), y(y), z(z), tx(tx), ty(ty), color(color) {
	}
};

struct STCoords {
	float s{ 0.0f }, t{ 0.0f };
	float w{ 0.0f }, h{ 0.0f };

	STCoords() = default;
	STCoords(float s, float t, float w, float h)
		: s(s), t(t), w(w), h(h) {
	}
};

#endif