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
// spritesheet.cpp
//
// Description:
// Shared logic for Sprite class: sheet loading, sprite metadata, accessors,
// and parameter handling. Rendering is in spritesheet_ogl2.cpp or ogl4.cpp.
// -----------------------------------------------------------------------------

#include "spritesheet.h"
#include "sys_log.h"
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <unordered_set>
#include <filesystem>
#include "json.hpp"

// -----------------------------------------------------------------------------
// StripPathAndExtension
// Returns the base filename with no directory path or file extension.
//
// Examples:
//   "assets/sprites/player.png"     -> "player"
//   "enemy.idle.0.png"              -> "enemy.idle.0"
//   "C:\\game\\gfx\\boss_final.jpg" -> "boss_final"
//   "plainname"                     -> "plainname" (unchanged)
// -----------------------------------------------------------------------------
inline std::string StripPathAndExtension(const std::string& filepath)
{
	std::size_t slash = filepath.find_last_of("/\\");
	std::size_t dot = filepath.find_last_of('.');
	std::size_t start = (slash == std::string::npos) ? 0 : slash + 1;

	if (dot == std::string::npos || dot < start)
		return filepath.substr(start); // no extension
	return filepath.substr(start, dot - start);
}

using json = nlohmann::json;

Sprite::Sprite() = default;

Sprite::~Sprite()
{
	sprites.clear();
	instances.clear();
}

void Sprite::SetPath(const std::string & path) {
	dataPath = path;
}

void Sprite::SetPath(const char* path) {
	dataPath = path;
}

std::string Sprite::GetPath() {
	return dataPath;
}

bool Sprite::LoadSheet(std::string fontfile)
{
	if (!dataPath.empty()) {
		fontfile = dataPath + fontfile;
	}

	std::ifstream stream(fontfile);
	if (!stream.is_open()) {
		LOG_ERROR("Cannot open Sprite Definition File: %s", fontfile.c_str());
		return false;
	}
	stream.close();

	if (!ParseDefinition(fontfile)) {
		LOG_ERROR("Failed to parse: %s", fontfile.c_str());
		return false;
	}

	if (!dataPath.empty()) {
		pngFileName = dataPath + pngFileName;
	}

	try {
		std::string n = "";
		texture = std::make_unique<TEX>(pngFileName);
	}
	catch (const std::exception& e) {
		LOG_ERROR("Failed to create texture: %s", e.what());
		return false;
	}

	return true;
}

bool Sprite::ParseDefinition(const std::string& jsonFile)
{
	std::ifstream file(jsonFile);
	if (!file.is_open()) {
		LOG_ERROR("Unable to open JSON file: %s", jsonFile.c_str());
		return false;
	}

	json j;
	try {
		file >> j;
	}
	catch (const std::exception& e) {
		LOG_ERROR("JSON parse error in %s: %s", jsonFile.c_str(), e.what());
		return false;
	}

	try {
		pngFileName = j["meta"]["image"].get<std::string>();
		sheetWidth = j["meta"]["size"]["w"].get<int>();
		sheetHeight = j["meta"]["size"]["h"].get<int>();
	}
	catch (const std::exception& e) {
		LOG_ERROR("Failed to parse meta section in %s: %s", jsonFile.c_str(), e.what());
		return false;
	}

	auto parseFrame = [&](const std::string& name, const json& frameData) {
		const auto& frame = frameData["frame"];
		bool rotated = frameData["rotated"].get<bool>();
		bool trimmed = frameData["trimmed"].get<bool>();

		int fx = frame["x"].get<int>();
		int fy = frame["y"].get<int>();
		int fw = frame["w"].get<int>();
		int fh = frame["h"].get<int>();

		int sourceW = frameData["sourceSize"]["w"].get<int>();
		int sourceH = frameData["sourceSize"]["h"].get<int>();

		SpriteDescriptor desc;
		desc.name = StripPathAndExtension(name);
		desc.width = sourceW;
		desc.height = sourceH;
		desc.rotation = rotated ? 1 : 0;
		desc.angle = 0;

		if (trimmed) {
			int ox = frameData["spriteSourceSize"]["x"].get<int>();
			int oy = frameData["spriteSourceSize"]["y"].get<int>();
			int subW = rotated ? fh : fw;
			int subH = rotated ? fw : fh;

			float cx = sourceW * 0.5f;
			float cy = sourceH * 0.5f;
			float trimCx = ox + subW * 0.5f;
			float trimCy = oy + subH * 0.5f;

			desc.offset.x = trimCx - cx;
			desc.offset.y = cy - trimCy;
		}
		else {
			desc.offset = Vec2(0.0f, 0.0f);
		}

		int atlasW = rotated ? fh : fw;
		int atlasH = rotated ? fw : fh;

		float u0 = static_cast<float>(fx) / sheetWidth;
		float u1 = static_cast<float>(fx + atlasW) / sheetWidth;
		float vT = static_cast<float>(fy) / sheetHeight;
		float vB = static_cast<float>(fy + atlasH) / sheetHeight;

		vT = 1.0f - vT;
		vB = 1.0f - vB;

		if (!rotated) {
			desc.uvs[0] = { u0, vT };
			desc.uvs[1] = { u1, vT };
			desc.uvs[2] = { u1, vB };
			desc.uvs[3] = { u0, vB };
		}
		else {
			desc.uvs[0] = { u1, vB };
			desc.uvs[1] = { u1, vT };
			desc.uvs[2] = { u0, vT };
			desc.uvs[3] = { u0, vB };
		}

		LOG_DEBUG("Parsed sprite: %-20s | trimmed: %d | rotated: %d | frame: %d,%d %dx%d",
			desc.name.c_str(), trimmed, rotated, fx, fy, fw, fh);

		sprites.emplace_back(desc);
	};

	try {
		if (j["frames"].is_array()) {
			for (const auto& frameData : j["frames"]) {
				std::string name = frameData["filename"].get<std::string>();
				parseFrame(name, frameData);
			}
		}
		else if (j["frames"].is_object()) {
			for (const auto& [name, frameData] : j["frames"].items()) {
				parseFrame(name, frameData);
			}
		}
		else {
			LOG_ERROR("Invalid frame format in: %s", jsonFile.c_str());
			return false;
		}
	}
	catch (const std::exception& e) {
		LOG_ERROR("Frame parsing error: %s", e.what());
		return false;
	}

	spriteCount = static_cast<short>(sprites.size());
	LOG_INFO("Loaded %d sprites from %s", spriteCount, jsonFile.c_str());
	return true;
}

int Sprite::ByName(const std::string name)
{
	for (size_t i = 0; i < sprites.size(); ++i) {
		if (sprites[i].name == name)
			return static_cast<int>(i);
	}
	LOG_INFO("Sprite name '%s' not found, returning 0", name.c_str());
	return 0;
}

std::string Sprite::ByNum(int spritenum)
{
	if (spritenum >= 0 && spritenum < static_cast<int>(sprites.size()))
		return sprites[spritenum].name;

	LOG_INFO("Sprite index %d out of range", spritenum);
	return "Sprite not found";
}

int Sprite::GetSpriteWidth(int spritenum) const
{
	if (spritenum >= 0 && spritenum < static_cast<int>(sprites.size()))
		return sprites[spritenum].width;
	return 0;
}

int Sprite::GetSpriteHeight(int spritenum) const
{
	if (spritenum >= 0 && spritenum < static_cast<int>(sprites.size()))
		return sprites[spritenum].height;
	return 0;
}

Vec2 Sprite::GetSpriteSize(int spritenum) const
{
	if (spritenum >= 0 && spritenum < static_cast<int>(sprites.size()))
		return Vec2((float)sprites[spritenum].width, (float)sprites[spritenum].height);
	return Vec2(0.0f, 0.0f);
}

void Sprite::SetRotationAngle(int spritenum, int sangle)
{
	if (spritenum >= 0 && spritenum < static_cast<int>(sprites.size()))
		sprites[spritenum].angle = (sangle % 360 + 360) % 360;
}

void Sprite::SetScale(int spritenum, float newscale)
{
	if (spritenum >= 0 && spritenum < static_cast<int>(sprites.size())) {
		if (newscale > 0.1f && newscale < 10.0f)
			sprites[spritenum].scale = newscale;
	}
}

void Sprite::SetOriginOffset(float x, float y, int spritenum)
{
	if (spritenum >= 0 && spritenum < static_cast<int>(sprites.size()))
		sprites[spritenum].offset = Vec2(x, y);
}

void Sprite::SetOriginOffset(Vec2 a, int spritenum)
{
	if (spritenum >= 0 && spritenum < static_cast<int>(sprites.size()))
		sprites[spritenum].offset = a;
}

Vec2 Sprite::GetOriginOffset(int spritenum)
{
	if (spritenum >= 0 && spritenum < static_cast<int>(sprites.size()))
		return sprites[spritenum].offset;
	return Vec2(0.0f, 0.0f);
}

void Sprite::EnableBatching(bool enable)
{
	keepInstances = enable;
}

void Sprite::SetZSort(bool enable)
{
	sortZIndex = enable;
}

void Sprite::SetColor(int r, int g, int b, int a)
{
	color = MAKE_RGBA(r, g, b, a);
}

void Sprite::SetColor(rgb_t newColor)
{
	color = newColor;
}

void Sprite::SetBlend(int b)
{
	blend = b;
}

void Sprite::Add(const SpriteParams& p)
{
	if (p.spriteID < 0) return;

	SetRotationAngle(p.spriteID, (int)p.angle);
	SetScale(p.spriteID, p.scale);
	SetColor(p.color);
	InternalAdd(p.pos.x, p.pos.y, p.spriteID, p.zIndex);
}

void Sprite::Add(float x, float y, int spriteid,
	rgb_t color,
	float scale,
	float angle,
	float zIndex)
{
	SetRotationAngle(spriteid, (int)angle);
	SetScale(spriteid, scale);
	SetColor(color);
	InternalAdd(x, y, spriteid, zIndex);
}
