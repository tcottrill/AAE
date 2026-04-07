#pragma once

// -----------------------------------------------------------------------------
// winfont_gl4.h
// Header-only TrueType font drawing for AAE's OpenGL 330 compatibility pipeline.
//
// Bakes ASCII glyphs (32..126) into an R8 atlas using stb_truetype.
// Renders with VAO/VBO + a GLSL 330 compatibility shader.
//
// Color handling:
//   Uses rgb_t (packed uint32 RGBA from colordefs.h) to match VectorFont and
//   the rest of the AAE codebase. Colors are passed to the shader via
//   GL_UNSIGNED_BYTE normalized vertex attributes - identical to VectorFont.
//   You can use RGB_WHITE, RGB_YELLOW, MAKE_RGBA() etc. directly.
//
// Uses shader_util.h (CompileShader / LinkShaderProgram) for shader building.
// Uses MathUtils.h (aae::math::mat4, aae::math::ortho) for projection.
//
// Integration with final_render_raster:
//   While fbo_raster is still bound (img5a is the render target), call:
//     FontOverlay_Begin(rasterW, rasterH);   // sets up ortho to img5a dims
//     FontOverlay_Print(x, y, RGB_YELLOW, "FPS: %d", fps);
//     FontOverlay_End();
//   This renders text directly into img5a at the FBO's native resolution,
//   so coordinates are in img5a pixel space (not window space).
//
// Requirements:
//   - Include sys_gl.h (GLEW) before this header.
//   - stb_truetype.h in include path.
//   - In EXACTLY one .cpp, define WINFONT_GL4_IMPLEMENTATION before including:
//       #define WINFONT_GL4_IMPLEMENTATION
//       #include "winfont_gl4.h"
//
// Notes:
//   - ASCII only (32..126). Extend as needed.
// -----------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>

#include "sys_gl.h"
#include "sys_log.h"
#include "shader_util.h"
#include "MathUtils.h"
#include "colordefs.h"

#ifndef WINFONT_GL4_ASSERT
#include <cassert>
#define WINFONT_GL4_ASSERT(x) assert(x)
#endif

// stb_truetype: define the implementation in exactly one TU
#ifdef WINFONT_GL4_IMPLEMENTATION
#ifndef STB_TRUETYPE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#endif
#endif

#ifndef STBTT_STATIC
#define STBTT_STATIC
#endif

#include "stb_truetype.h"

namespace winfont_gl4
{
	// -------------------------------------------------------------------------
	// File I/O helper
	// -------------------------------------------------------------------------
	static inline bool ReadFileBytes(const char* path, std::vector<uint8_t>& out)
	{
		out.clear();
		if (!path || !path[0])
			return false;

		std::ifstream f(path, std::ios::binary);
		if (!f)
			return false;

		f.seekg(0, std::ios::end);
		std::streamoff len = f.tellg();
		if (len <= 0)
			return false;
		f.seekg(0, std::ios::beg);

		out.resize((size_t)len);
		f.read((char*)out.data(), len);
		return f.good();
	}

	// -------------------------------------------------------------------------
	// Font struct - bakes glyphs, owns GL resources, renders text
	// -------------------------------------------------------------------------
	struct Font
	{
		static constexpr int kFirst = 32;
		static constexpr int kCount = 95; // 32..126

		int atlasW = 512;
		int atlasH = 512;
		float pixelHeight = 18.0f;

		bool yDown = true;

		stbtt_bakedchar baked[kCount]{};

		GLuint tex = 0;
		GLuint vao = 0;
		GLuint vbo = 0;
		GLuint prog = 0;

		GLint uMVP = -1;
		GLint uTex = -1;

		// -----------------------------------------------------------------
		// Vertex layout: position, texcoord, packed RGBA color
		// Color is rgb_t (uint32) unpacked by GL_UNSIGNED_BYTE normalized.
		// This matches the VectorFont vertex attribute pattern exactly.
		// -----------------------------------------------------------------
		struct Vtx
		{
			float x, y;     // position
			float u, v;     // texcoord
			rgb_t color;     // packed RGBA (colordefs.h format)
		};

		std::vector<Vtx> verts;
		bool initialized = false;

		// -----------------------------------------------------------------
		// Destroy - release all GL resources
		// -----------------------------------------------------------------
		void Destroy()
		{
			if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
			if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
			if (tex) { glDeleteTextures(1, &tex); tex = 0; }
			if (prog) { glDeleteProgram(prog); prog = 0; }
			initialized = false;
			verts.clear();
		}

		// -----------------------------------------------------------------
		// InitFromTTF - bake font atlas and create GL resources
		// -----------------------------------------------------------------
		bool InitFromTTF(const uint8_t* ttfData, size_t ttfSize, float sizePixels,
			int inAtlasW = 512, int inAtlasH = 512)
		{
			Destroy();

			if (!ttfData || ttfSize == 0)
				return false;

			atlasW = inAtlasW;
			atlasH = inAtlasH;
			pixelHeight = sizePixels;

			std::vector<uint8_t> bitmap;
			bitmap.resize((size_t)atlasW * (size_t)atlasH, 0);

			int res = stbtt_BakeFontBitmap(
				ttfData, 0,
				pixelHeight,
				bitmap.data(), atlasW, atlasH,
				kFirst, kCount,
				baked
			);
			if (res <= 0)
			{
				LOG_ERROR("winfont_gl4: stbtt_BakeFontBitmap failed (res=%d)", res);
				return false;
			}

			// -- Atlas texture (R8) --
			glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D, tex);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW, atlasH, 0,
				GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			// -- Shader (330 compatibility) --
			// Color comes in as a normalized vec4 from GL_UNSIGNED_BYTE.
			// The fragment shader multiplies the atlas alpha by the vertex
			// color alpha, so text blends correctly over the game image.
			const char* vsSrc = R"GLSL(
			#version 330 compatibility
			layout(location=0) in vec2 aPos;
			layout(location=1) in vec2 aUV;
			layout(location=2) in vec4 aColor;

			uniform mat4 uMVP;

			out vec2 vUV;
			out vec4 vColor;

			void main()
			{
				vUV    = aUV;
				vColor = aColor;
				gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
			}
			)GLSL";

 			const char* fsSrc = R"GLSL(
			#version 330 compatibility
			in vec2 vUV;
			in vec4 vColor;

			uniform sampler2D uTex;

			out vec4 FragColor;

			void main()
			{
				float a = texture(uTex, vUV).r;
				FragColor = vec4(vColor.rgb, vColor.a * a);
			}
			)GLSL";

			GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc, "FontOverlay_VS");
			GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc, "FontOverlay_FS");
			prog = LinkShaderProgram(vs, fs);

			if (!prog)
			{
				LOG_ERROR("winfont_gl4: shader program creation failed");
				Destroy();
				return false;
			}

			uMVP = glGetUniformLocation(prog, "uMVP");
			uTex = glGetUniformLocation(prog, "uTex");

			// -- VAO/VBO --
			glGenVertexArrays(1, &vao);
			glGenBuffers(1, &vbo);

			glBindVertexArray(vao);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

			// Attribute 0: position (2 floats)
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx),
				(void*)offsetof(Vtx, x));

			// Attribute 1: texcoord (2 floats)
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx),
				(void*)offsetof(Vtx, u));

			// Attribute 2: color (4 x GL_UNSIGNED_BYTE, normalized to 0..1)
			// Matches VectorFont's pattern: rgb_t stored as uint32,
			// GPU auto-unpacks RGBA bytes into a vec4.
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx),
				(void*)offsetof(Vtx, color));

			glBindVertexArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			initialized = true;
			LOG_INFO("winfont_gl4: font initialized (%.0fpx, atlas %dx%d)", sizePixels, atlasW, atlasH);
			return true;
		}

		// -----------------------------------------------------------------
		// InitFromFile - load TTF from disk, then bake
		// -----------------------------------------------------------------
		bool InitFromFile(float sizePixels, const char* ttfPath,
			int inAtlasW = 512, int inAtlasH = 512)
		{
#ifdef _WIN32
			const char* defaultPath = "C:/Windows/Fonts/arial.ttf";
#else
			const char* defaultPath = nullptr;
#endif
			const char* path = (ttfPath && ttfPath[0]) ? ttfPath : defaultPath;
			if (!path)
			{
				LOG_ERROR("winfont_gl4: no TTF path and no default available");
				return false;
			}

			std::vector<uint8_t> bytes;
			if (!ReadFileBytes(path, bytes))
			{
				LOG_ERROR("winfont_gl4: failed to read '%s'", path);
				return false;
			}

			return InitFromTTF(bytes.data(), bytes.size(), sizePixels, inAtlasW, inAtlasH);
		}

		// -----------------------------------------------------------------
		// Begin - set up GL state for text rendering into img5a.
		// orthoW/orthoH are the FBO dimensions (rw x rh from fbo_init_raster).
		// yDownOrtho: true = origin top-left (raster), false = origin bottom-left.
		// -----------------------------------------------------------------
		void Begin(int orthoW, int orthoH, bool yDownOrtho = true)
		{
			if (!initialized)
				return;

			verts.clear();

			// Build ortho projection matching the FBO's pixel space.
			aae::math::mat4 mvp;
			if (yDownOrtho)
				mvp = aae::math::ortho(0.0f, (float)orthoW, (float)orthoH, 0.0f);
			else
				mvp = aae::math::ortho(0.0f, (float)orthoW, 0.0f, (float)orthoH);

			glUseProgram(prog);
			glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, tex);
			glUniform1i(uTex, 0);

			glBindVertexArray(vao);

			glDisable(GL_DEPTH_TEST);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}

		// -----------------------------------------------------------------
		// End - flush remaining verts and restore state
		// -----------------------------------------------------------------
		void End()
		{
			if (!initialized)
				return;

			Flush();
			glBindVertexArray(0);
			glUseProgram(0);
		}

		// -----------------------------------------------------------------
		// Flush - upload and draw accumulated vertices
		// -----------------------------------------------------------------
		void Flush()
		{
			if (!initialized || verts.empty())
				return;

			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER,
				(GLsizeiptr)(verts.size() * sizeof(Vtx)),
				verts.data(), GL_DYNAMIC_DRAW);
			glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			verts.clear();
		}

		// -----------------------------------------------------------------
		// DrawText - queue glyphs for a string
		// color is rgb_t (use RGB_WHITE, RGB_YELLOW, MAKE_RGBA() etc.)
		// -----------------------------------------------------------------
		void DrawText(float x, float y, rgb_t color, const char* s)
		{
			if (!initialized || !s)
				return;

			float cx = x;
			float cy = y;

			for (const unsigned char* p = (const unsigned char*)s; *p; ++p)
			{
				int c = (int)(*p);
				if (c == '\n')
				{
					cx = x;
					cy += (yDown ? pixelHeight : -pixelHeight);
					continue;
				}

				if (c < kFirst || c >= (kFirst + kCount))
					continue;

				const stbtt_bakedchar& bc = baked[c - kFirst];

				float x0 = cx + bc.xoff;
				float y0 = cy + bc.yoff;
				float x1 = x0 + (bc.x1 - bc.x0);
				float y1 = y0 + (bc.y1 - bc.y0);

				float u0 = (float)bc.x0 / (float)atlasW;
				float v0 = (float)bc.y0 / (float)atlasH;
				float u1 = (float)bc.x1 / (float)atlasW;
				float v1 = (float)bc.y1 / (float)atlasH;

				Vtx a{ x0, y0, u0, v0, color };
				Vtx b{ x1, y0, u1, v0, color };
				Vtx c1{ x1, y1, u1, v1, color };
				Vtx d{ x0, y1, u0, v1, color };

				verts.push_back(a);
				verts.push_back(b);
				verts.push_back(c1);

				verts.push_back(c1);
				verts.push_back(d);
				verts.push_back(a);

				cx += bc.xadvance;
			}
		}

		// -----------------------------------------------------------------
		// Printf-style text drawing
		// -----------------------------------------------------------------
		void Printf(float x, float y, rgb_t color, const char* fmt, ...)
		{
			if (!fmt)
				return;

			char buf[2048];
			buf[0] = 0;

			va_list va;
			va_start(va, fmt);
#if defined(_MSC_VER)
			_vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, va);
#else
			vsnprintf(buf, sizeof(buf), fmt, va);
#endif
			va_end(va);

			DrawText(x, y, color, buf);
		}
	};

	// -------------------------------------------------------------------------
	// Global font instance
	// -------------------------------------------------------------------------
	static inline Font g_font;
} // namespace winfont_gl4

// =============================================================================
// Public C-style API (matches the calling conventions used elsewhere in AAE)
// =============================================================================

// Initialize the font renderer. Call once from init_gl() or similar.
// sizePixels: glyph height in pixels (e.g. 16, 18, 24).
// ttfPath: path to a .ttf file, or nullptr to use C:/Windows/Fonts/arial.ttf.
static inline int FontOverlay_Init(int sizePixels, const char* ttfPath = nullptr)
{
	return winfont_gl4::g_font.InitFromFile((float)sizePixels, ttfPath, 512, 512) ? 1 : 0;
}

// Shut down and release all GL resources.
static inline void FontOverlay_Shutdown()
{
	winfont_gl4::g_font.Destroy();
}

// Set Y-axis direction. true = Y-down (raster default), false = Y-up.
static inline void FontOverlay_SetYDown(bool yDown)
{
	winfont_gl4::g_font.yDown = yDown;
}

// Begin text rendering into the currently bound FBO.
// orthoW/orthoH should be the FBO dimensions (img5a's rw x rh).
// Uses Y-down ortho by default to match set_ortho_raster().
static inline void FontOverlay_Begin(int orthoW, int orthoH, bool yDown = true)
{
	winfont_gl4::g_font.Begin(orthoW, orthoH, yDown);
}

// Flush and restore GL state.
static inline void FontOverlay_End()
{
	winfont_gl4::g_font.End();
}

// Print colored text at (x, y) in FBO pixel coordinates.
// color is rgb_t - use RGB_WHITE, RGB_YELLOW, MAKE_RGBA() etc.
static inline void FontOverlay_Print(int x, int y, rgb_t color,
	const char* fmt, ...)
{
	if (!fmt) return;

	char buf[2048];
	buf[0] = 0;

	va_list va;
	va_start(va, fmt);
#if defined(_MSC_VER)
	_vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, va);
#else
	vsnprintf(buf, sizeof(buf), fmt, va);
#endif
	va_end(va);

	winfont_gl4::g_font.DrawText((float)x, (float)y, color, buf);
}

// Print white text (convenience overload).
static inline void FontOverlay_Print(int x, int y, const char* fmt, ...)
{
	if (!fmt) return;

	char buf[2048];
	buf[0] = 0;

	va_list va;
	va_start(va, fmt);
#if defined(_MSC_VER)
	_vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, va);
#else
	vsnprintf(buf, sizeof(buf), fmt, va);
#endif
	va_end(va);

	winfont_gl4::g_font.DrawText((float)x, (float)y, RGB_WHITE, buf);
}
