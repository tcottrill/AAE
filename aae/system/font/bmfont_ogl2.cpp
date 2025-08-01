#include "bmfont.h"
#include "sys_texture.h"
#include "sys_log.h"
#include <fstream>
#include <sstream>
#include <cstdarg>

// Static hash key function for kerning lookup
static inline std::size_t key(int i, int j) {
	return ((std::size_t)i << 16) | (unsigned int)j;
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
BMFont::BMFont(int width, int height)
	: surfaceWidth(width),
	surfaceHeight(height),
	kernCount(0),
	fontBlend(0),
	fontScale(1.0f),
	fontAngle(0),
	cachingEnabled(false),
	fontAlign(FONT_ALIGN_NONE),
	fontOriginBottom(true),
	useKerning(true)
{
	setColor(RGB_WHITE);
}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
BMFont::~BMFont() {
	LOG_INFO("BMFont instance freed");
}

// -----------------------------------------------------------------------------
// Font File Loader
// -----------------------------------------------------------------------------
bool BMFont::loadFont(const std::string& fontFileName) {
	std::string fullPath = dataPath.empty() ? fontFileName : dataPath + fontFileName;

	LOG_INFO("Opening file: %s", fullPath.c_str());
	std::ifstream stream(fullPath);
	if (!stream) {
		LOG_ERROR("Cannot find font file: %s", fullPath.c_str());
		return false;
	}

	LOG_INFO("Font file opened: %s", fullPath.c_str());
	if (!parseFont(fullPath)) return false;

	kernCount = kernMap.size();
	LOG_INFO("Kerning pair count: %zu", kernCount);

	if (!dataPath.empty()) {
		imageFileName = dataPath + imageFileName;
	}

	LOG_INFO("Loading texture: %s", imageFileName.c_str());
	try {
		texture = std::make_unique<TEX>(imageFileName);
	}
	catch (const std::exception& e) {
		LOG_ERROR("Failed to load texture: %s", e.what());
		return false;
	}

	LOG_INFO("Texture loaded");
	return true;
}

bool BMFont::parseFont(const std::string& file) {
	std::ifstream stream(file);
	std::string line, read, keyStr, value;
	std::size_t i;
	CharDescriptor ch;
	KerningInfo kern;

	while (std::getline(stream, line)) {
		std::stringstream lineStream(line);
		lineStream >> read;

		if (read == "common") {
			while (!lineStream.eof()) {
				lineStream >> read;
				i = read.find('=');
				if (i == std::string::npos) continue;
				keyStr = read.substr(0, i);
				value = read.substr(i + 1);
				std::stringstream ss(value);

				if (keyStr == "lineHeight")	ss >> lineHeight;
				else if (keyStr == "base")		ss >> base;
				else if (keyStr == "scaleW")	ss >> sheetWidth;
				else if (keyStr == "scaleH")	ss >> sheetHeight;
				else if (keyStr == "pages")	ss >> pages;
				else if (keyStr == "outline")	ss >> outline;
			}
		}
		else if (read == "page") {
			while (!lineStream.eof()) {
				lineStream >> read;
				i = read.find('=');
				if (i == std::string::npos) continue;
				keyStr = read.substr(0, i);
				value = read.substr(i + 1);
				std::stringstream ss(value);

				if (keyStr == "file") {
					ss >> imageFileName;
					if (imageFileName.front() == '"')
						imageFileName = imageFileName.substr(1, imageFileName.size() - 2);
				}
			}
		}
		else if (read == "char") {
			int id = 0;
			while (!lineStream.eof()) {
				lineStream >> read;
				i = read.find('=');
				if (i == std::string::npos) continue;
				keyStr = read.substr(0, i);
				value = read.substr(i + 1);
				std::stringstream ss(value);

				if (keyStr == "id")			ss >> id;
				else if (keyStr == "x")		ss >> ch.x;
				else if (keyStr == "y")		ss >> ch.y;
				else if (keyStr == "width")	ss >> ch.width;
				else if (keyStr == "height")ss >> ch.height;
				else if (keyStr == "xoffset")ss >> ch.xOffset;
				else if (keyStr == "yoffset")ss >> ch.yOffset;
				else if (keyStr == "xadvance")ss >> ch.xAdvance;
				else if (keyStr == "page")	ss >> ch.page;
			}
			chars[id] = ch;
		}
		else if (read == "kernings") {
			while (!lineStream.eof()) {
				lineStream >> read;
				i = read.find('=');
				if (i == std::string::npos) continue;
				keyStr = read.substr(0, i);
				value = read.substr(i + 1);
				if (keyStr == "count") std::stringstream(value) >> kernCount;
			}
		}
		else if (read == "kerning") {
			while (!lineStream.eof()) {
				lineStream >> read;
				i = read.find('=');
				if (i == std::string::npos) continue;
				keyStr = read.substr(0, i);
				value = read.substr(i + 1);
				std::stringstream ss(value);

				if (keyStr == "first")		ss >> kern.first;
				else if (keyStr == "second")ss >> kern.second;
				else if (keyStr == "amount")ss >> kern.amount;
			}
			kernMap[key(kern.first, kern.second)] = kern.amount;
		}
	}
	stream.close();

	for (auto& pair : chars)
		calculateTextureCoords(pair.second);

	return true;
}

void BMFont::calculateTextureCoords(CharDescriptor& c) {
	c.st.s = (float)c.x / sheetWidth;
	c.st.t = 1.0f - (float)c.y / sheetHeight;
	c.st.w = (float)c.width / sheetWidth;
	c.st.h = (float)c.height / sheetHeight;
}

int BMFont::getKerningPair(int first, int second) {
	if (kernCount && useKerning)
		return kernMap[key(first, second)];
	return 0;
}

void BMFont::setColor(rgb_t color) { fontColor = color; }
void BMFont::setColor(int r, int g, int b, int a) { fontColor = MAKE_RGBA(r, g, b, a); }
void BMFont::setBlend(int mode) { fontBlend = mode; }
void BMFont::setCaching(bool enable) { cachingEnabled = enable; }
void BMFont::setScale(float scale) {
	if (scale > 0.1f && scale < 10.0f) fontScale = scale;
}
void BMFont::setAngle(int degrees) {
	fontAngle = (degrees % 360 + 360) % 360;
}
void BMFont::setAlign(FontAlign align) {
	if (align <= FONT_ALIGN_FAR) fontAlign = align;
}
void BMFont::setOrigin(FontOrigin origin) {
	fontOriginBottom = (origin == FONT_ORIGIN_BOTTOM);
}
void BMFont::setPath(const std::string& path) { dataPath = path; }
std::string BMFont::GetPath() const { return dataPath; }
void BMFont::clearCache() { vertices.clear(); }
float BMFont::setHeight() const { return lineHeight * fontScale; }

Vec2 BMFont::rotateAroundPoint(float x, float y, float cx, float cy, float cosTheta, float sinTheta) {
	float dx = x - cx;
	float dy = y - cy;
	return Vec2(
		dx * cosTheta - dy * sinTheta + cx,
		dx * sinTheta + dy * cosTheta + cy
	);
}

void BMFont::printKerningPairs() {
	for (const auto& entry : kernMap) {
		int first = (entry.first >> 16) & 0xFFFF;
		int second = entry.first & 0xFFFF;
		int amount = entry.second;
		LOG_INFO("%d,%d,%d,", first, second, amount);
	}
}
void BMFont::printString(float x, float y, char* text) {
	if (!text || !*text) return;

	float cursorX = x;
	float cursorY = y;

	if (fontOriginBottom)
		cursorY += lineHeight * fontScale;

	cursorX *= 1.0f / fontScale;
	cursorY *= 1.0f / fontScale;

	originalX = x;
	size_t len = strlen(text);
	if (len == 0) return;

	switch (fontAlign) {
	case FONT_ALIGN_NEAR:
		cursorX = lineHeight;
		break;
	case FONT_ALIGN_CENTER:
		cursorX = surfaceWidth / 2.0f - getStrWidth(text) / 2.0f;
		break;
	case FONT_ALIGN_FAR:
		cursorX = surfaceWidth - getStrWidth(text);
		break;
	default:
		break;
	}

	Vec2 origin(cursorX, cursorY);

	for (size_t i = 0; i < len; ++i) {
		char ch = text[i];
		if (ch == '\n') {
			cursorX = originalX;
			cursorY -= (lineHeight * fontScale);
			continue;
		}

		const auto& glyph = chars[ch];
		float cx = cursorX + glyph.xOffset;
		float cy = cursorY;

		if (fontOriginBottom)
			cy -= glyph.yOffset;
		else
			cy += glyph.yOffset;

		float dx = cx + glyph.width;
		float dy = fontOriginBottom ? (cy - glyph.height) : (cy + glyph.height);

		cx *= fontScale; cy *= fontScale;
		dx *= fontScale; dy *= fontScale;

		Vec2 p0(cx, cy);
		Vec2 p1(dx, cy);
		Vec2 p2(dx, dy);
		Vec2 p3(cx, dy);

		if (fontAngle != 0) {
			float angleRad = (fontAngle * k_pi) / 180.0f;
			float cosA = cosf(angleRad);
			float sinA = sinf(angleRad);
			p0 = rotateAroundPoint(p0.x, p0.y, origin.x, origin.y, cosA, sinA);
			p1 = rotateAroundPoint(p1.x, p1.y, origin.x, origin.y, cosA, sinA);
			p2 = rotateAroundPoint(p2.x, p2.y, origin.x, origin.y, cosA, sinA);
			p3 = rotateAroundPoint(p3.x, p3.y, origin.x, origin.y, cosA, sinA);
		}

		const auto& st = glyph.st;
		vertices.emplace_back(p0.x, p0.y, st.s, st.t, fontColor);
		vertices.emplace_back(p1.x, p1.y, st.s + st.w, st.t, fontColor);
		vertices.emplace_back(p2.x, p2.y, st.s + st.w, st.t - st.h, fontColor);
		vertices.emplace_back(p2.x, p2.y, st.s + st.w, st.t - st.h, fontColor);
		vertices.emplace_back(p3.x, p3.y, st.s, st.t - st.h, fontColor);
		vertices.emplace_back(p0.x, p0.y, st.s, st.t, fontColor);

		if (i + 1 < len)
			cursorX += getKerningPair(ch, text[i + 1]);

		cursorX += glyph.xAdvance;
	}
}

void BMFont::fPrint(float x, float y, const char* fmt, ...) {
	if (!fmt) return;
	char buffer[1024] = { 0 };
	va_list args;
	va_start(args, fmt);
	vsprintf_s(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	printString(x, y, buffer);
}

void BMFont::fPrint(float scale, float x, float y, const char* fmt, ...) {
	if (!fmt) return;
	char buffer[1024] = { 0 };
	va_list args;
	va_start(args, fmt);
	vsprintf_s(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	setScale(scale);
	printString(x, y, buffer);
}

void BMFont::fPrint(float x, float y, rgb_t color, float scale, const char* fmt, ...) {
	if (!fmt) return;
	char buffer[1024] = { 0 };
	va_list args;
	va_start(args, fmt);
	vsprintf_s(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	setColor(color);
	setScale(scale);
	printString(x, y, buffer);
}

void BMFont::fPrint(float y, rgb_t color, float scale, int angle, const char* fmt, ...) {
	if (!fmt) return;
	char buffer[1024] = { 0 };
	va_list args;
	va_start(args, fmt);
	vsprintf_s(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	setColor(color);
	setScale(scale);
	setAngle(angle);
	setAlign(FONT_ALIGN_CENTER);
	printString(0.0f, y, buffer);
}

void BMFont::fPrintCenter(float y, char* text) {
	if (!text) return;
	float width = getStrWidth(text);
	originalX = width;
	printString((surfaceWidth / 2.0f) - (width / 2.0f), y, text);
}

float BMFont::getStrWidth(const char* str) {
	if (!str || !*str) return 0.0f;
	float total = 0.0f;
	for (size_t i = 0; str[i]; ++i) {
		auto it = chars.find(str[i]);
		if (it == chars.end()) continue;
		total += fontScale * it->second.xAdvance;
		if (str[i + 1])
			total += getKerningPair(str[i], str[i + 1]);
	}
	return total;
}

float BMFont::getStrWidth(const std::string& str) {
	return getStrWidth(str.c_str());
}

void BMFont::Render() {
	if (!texture || vertices.empty()) return;

	texture->UseTexture(1, 1);
	texture->SetBlendMode(fontBlend);

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, sizeof(TexVertex), &vertices[0].x);

	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, sizeof(TexVertex), &vertices[0].tx);

	glEnableClientState(GL_COLOR_ARRAY);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(TexVertex), &vertices[0].color);

	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size());

	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);

	if (!cachingEnabled) vertices.clear();
}

