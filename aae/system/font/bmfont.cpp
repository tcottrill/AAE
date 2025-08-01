#include "bmfont.h"
#include "shader_util.h"
#include "sys_log.h"
#include <fstream>
#include <sstream>
#include <cstdarg>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"       // Needed for glm::value_ptr
#include "debug_draw.h"

static const char* vertexShaderSrc = R"(
		#version 330 core
		layout(location = 0) in vec3 aPos;
		layout(location = 1) in vec2 aUV;
		layout(location = 2) in vec4 aColor;
		out vec2 vUV;
		out vec4 vColor;
		uniform mat4 u_proj;
		void main() {
			vUV = aUV;
			vColor = aColor;
			gl_Position = u_proj * vec4(aPos.xy, 1.0, 1.0); // <-- Force Z to 1.0 always
		}
	)";

static const char* fragmentShaderSrc = R"(
		#version 330 core
		in vec2 vUV;
		in vec4 vColor;
		uniform sampler2D tex;
		out vec4 FragColor;
		void main() {
			FragColor = texture(tex, vUV) * vColor;
		}
	)";

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
	useKerning(true),
	vao(0),
	vbo(0),
	shader(0),
	uProjLoc(-1)
{
	setColor(RGB_WHITE);
}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
BMFont::~BMFont() {
	Shutdown();
	LOG_INFO("BMFont instance freed");
}

// -----------------------------------------------------------------------------
// Load and Parse .fnt File
// -----------------------------------------------------------------------------
bool BMFont::loadFont(const std::string& fontFileName) {
	std::string fullPath = dataPath.empty() ? fontFileName : dataPath + fontFileName;

	LOG_INFO("Opening file: %s", fullPath.c_str());
	std::ifstream stream(fullPath);
	if (!stream) {
		LOG_ERROR("Cannot find font file: %s", fullPath.c_str());
		return false;
	}

	if (!parseFont(fullPath)) return false;

	kernCount = kernMap.size();

	if (!dataPath.empty()) {
		imageFileName = dataPath + imageFileName;
	}

	try {
		texture = std::make_unique<TEX>(imageFileName);
	}
	catch (const std::exception& e) {
		LOG_ERROR("Failed to load texture: %s", e.what());
		return false;
	}

	return true;
}

// -----------------------------------------------------------------------------
// InitGL - Setup OpenGL 4 shader, VAO, and VBO
// -----------------------------------------------------------------------------

bool BMFont::InitGL() {
	
	// Check if GL context is ready
	if (!wglGetCurrentContext()) {
		LOG_ERROR("BMFont::InitGL() - No OpenGL context is current");
		return false;
	}

	// Clear any previous GL errors
	glGetError();

	// Compile vertex shader
	GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexShaderSrc, "Sprite Vertex");
	if (!vs) {
		LOG_ERROR("BMFont::InitGL() - Vertex shader compilation failed");
		return false;
	}
	LOG_INFO("BMFont::InitGL() - Vertex shader compiled successfully (ID: %u)", vs);

	// Compile fragment shader
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc, "Sprite Fragment");
	if (!fs) {
		LOG_ERROR("BMFont::InitGL() - Fragment shader compilation failed");
		return false;
	}
	LOG_INFO("BMFont::InitGL() - Fragment shader compiled successfully (ID: %u)", fs);

	// Link shader program
	shader = LinkShaderProgram(vs, fs);
	if (!shader) {
		LOG_ERROR("BMFont::InitGL() - Shader program linking failed");
		return false;
	}
	LOG_INFO("BMFont::InitGL() - Shader program linked successfully (Program ID: %u)", shader);

	// Get uniform location
	uProjLoc = glGetUniformLocation(shader, "u_proj");
	if (uProjLoc == -1) {
		LOG_ERROR("BMFont::InitGL() - Failed to get uniform location for 'u_proj'");
	}
	else {
		LOG_INFO("BMFont::InitGL() - Got uniform location for 'u_proj': %d", uProjLoc);
	}

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);


	glEnableVertexAttribArray(0); // Position
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TexVertex), (void*)offsetof(TexVertex, x));

	glEnableVertexAttribArray(1); // TexCoord
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TexVertex), (void*)offsetof(TexVertex, tx));

	glEnableVertexAttribArray(2); // Color
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(TexVertex), (void*)offsetof(TexVertex, color));

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	LOG_INFO("BMFont::InitGL() - VAO and VBO setup complete");

	return true;
}


// -----------------------------------------------------------------------------
// Shutdown - Clean up OpenGL resources
// -----------------------------------------------------------------------------
void BMFont::Shutdown() {
	if (vbo) glDeleteBuffers(1, &vbo);
	if (vao) glDeleteVertexArrays(1, &vao);
	if (shader) glDeleteProgram(shader);
	vbo = vao = shader = 0;
}

// -----------------------------------------------------------------------------
// Render - Draw all queued glyphs using VAO/VBO and shader
// -----------------------------------------------------------------------------
void BMFont::Render() {
	if (!texture || vertices.empty() || !shader) return;

	//LOG_INFO("BMFont Render: %zu vertices queued", vertices.size());

	glm::mat4 proj = glm::ortho(0.0f, (float)surfaceWidth, 0.0f, (float)surfaceHeight);

	glUseProgram(shader);
	glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(proj));
	GLint texLoc = glGetUniformLocation(shader, "tex");
	glUniform1i(texLoc, 0);
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	texture->UseTexture(0, 0);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(TexVertex), vertices.data(), GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size());
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
//	debugDrawAll(proj);  // <- insert here to draw debug overlays
	
	if (!cachingEnabled)
		vertices.clear();
}

// -----------------------------------------------------------------------------
// Parsing and Text Layout Helpers
// -----------------------------------------------------------------------------
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
	LOG_INFO("Font Parsed");

	int count = 0;
	for (const auto& [id, ch] : chars) {
		if (ch.width > 0 && ch.height > 0) {
			LOG_INFO("Glyph %d: ST(%f,%f) WH(%f,%f) Px(%d,%d)", id, ch.st.s, ch.st.t, ch.st.w, ch.st.h, ch.width, ch.height);
			if (++count > 5) break;
		}
	}

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

		auto it = chars.find(ch);
		if (it == chars.end()) continue;
		const auto& glyph = it->second;

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
		vertices.emplace_back(p0.x, p0.y, 0.0f, st.s, st.t, fontColor);
		vertices.emplace_back(p1.x, p1.y, 0.0f, st.s + st.w, st.t, fontColor);
		vertices.emplace_back(p2.x, p2.y, 0.0f, st.s + st.w, st.t - st.h, fontColor);
		vertices.emplace_back(p2.x, p2.y, 0.0f, st.s + st.w, st.t - st.h, fontColor);
		vertices.emplace_back(p3.x, p3.y, 0.0f, st.s, st.t - st.h, fontColor);
		vertices.emplace_back(p0.x, p0.y, 0.0f, st.s, st.t, fontColor);

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
void BMFont::setPath(const std::string& path) {
	dataPath = path;
}

void BMFont::setScale(float scale) {
	if (scale > 0.1f && scale < 10.0f) {
		fontScale = scale;
	}
}

void BMFont::setAngle(int degrees) {
	// Normalize angle to range [0, 360)
	fontAngle = (degrees % 360 + 360) % 360;
}

void BMFont::setBlend(int mode) {
	fontBlend = mode;
}

void BMFont::setAlign(FontAlign align) {
	if (align <= FONT_ALIGN_FAR) {
		fontAlign = align;
	}
}

void BMFont::setOrigin(FontOrigin origin) {
	fontOriginBottom = (origin == FONT_ORIGIN_BOTTOM);
}

void BMFont::setCaching(bool enable) {
	cachingEnabled = enable;
}

//void BMFont::setKerning(bool enable) {
//	useKerning = enable;
//}

void BMFont::setColor(rgb_t color) {
	fontColor = color;
}

void BMFont::setColor(int r, int g, int b, int a) {
	fontColor = MAKE_RGBA(r, g, b, a);
}

void BMFont::clearCache() {
	vertices.clear();
}

float BMFont::setHeight() const {
	return lineHeight * fontScale;
}

std::string BMFont::GetPath() const {
	return dataPath;
}
