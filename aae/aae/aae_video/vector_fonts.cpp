//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// ====
// vector_fonts.cpp
// ====
#include "vector_fonts.h"
#include "shader_util.h"
#include "colordefs.h"
#include "vector_draw.h"   // shared coverage-AA beam line path (BeamLine/BeamJoin + draw)
#include "config.h"        // RENDERER_VULKAN
#include "../aae_video_vk/vulkan_renderer.h"   // vkchain_gui_draw_quad (VK quad path)

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cmath>           // cosf / sinf for CPU-side glyph rotation
#include <chrono>          // shared wall clock for marquee scrolling

// Disable warnings about double-to-float conversions in the font data.
#pragma warning(disable : 4305)

static constexpr float CHAR_GAP = 2.0f;     // Inter-character gap (unscaled units)
static constexpr float SPACE_WIDTH = 7.0f;   // Space character width (unscaled units)
static constexpr int EOC = 256;

// ----
// Inline Shaders for OpenGL 3.3
// ----
static const char* vfVertexShader = R"glsl(
#version 330 
in vec2 aPos;
in vec2 aOrigin;
in float aAngle;
in vec4 aColor;
out vec4 vColor;
uniform mat4 uMVP;
void main() {
    vColor = aColor;

    // Convert degrees to radians
    float rad = radians(aAngle);
    float c = cos(rad);
    float s = sin(rad);

    // Calculate position relative to the text origin
    vec2 local = aPos - aOrigin;

    // Rotate
    vec2 rotated;
    rotated.x = local.x * c - local.y * s;
    rotated.y = local.x * s + local.y * c;

    // Translate back to world space
    vec2 finalPos = rotated + aOrigin;

    gl_Position = uMVP * vec4(finalPos, 0.0, 1.0);
}
)glsl";

static const char* vfFragmentShader = R"glsl(
#version 330 
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)glsl";


// ----
// Full Embedded Font Data
// ----

static float fontdata[] = {
	// 0x1E (30) - UP TRIANGLE (outline)
	30, 3, 7, 0, 0, 0, 0, 7, 0, 7, 0, 3, 7, EOC,
	// 0x1F (31) - DOWN TRIANGLE (outline)
	31, 0, 7, 3, 0, 3, 0, 7, 7, 7, 7, 0, 7, EOC,
	32, EOC,
	33, 3.5, 2, 3.5, 6, 3.5, 0, 3.5, 1, EOC,
	// 34 '"' - double quote
	34, 2.5, 6, 2.5, 5, 4.5, 6, 4.5, 5, EOC,
	// 35 '#'
	35, 2, 1, 2, 6, 5, 1, 5, 6, 1, 3, 6, 3, 1, 4, 6, 4, EOC,
	// 36 '$'
	36, 3.5, 0, 3.5, 6, 6, 6, 1, 6, 1, 6, 1, 3, 1, 3, 6, 3, 6, 3, 6, 0, 6, 0, 1, 0, EOC,
	37, 0, 0, 7, 6, 1, 6, 1, 5, 6, 0, 6, 1, EOC,
	// 38 '&'
	38, 6, 0, 1, 0, 1, 0, 1, 3, 1, 3, 6, 3, 6, 3, 6, 6, 6, 6, 1, 6, 3.5, 3, 6, 0, EOC,
	39, 3.5, 6, 3.5, 5, EOC,
	40, 2, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 5, 0, 5, 1, 6, 1, 6, 2, 6, EOC,
	41, 5, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 5, 7, 5, 6, 6, 6, 6, 5, 6, EOC,
	// *
	42, 3.5, 1, 3.5, 5, 1, 3, 6, 3, 1.5, 1.5, 5.5, 4.5, 1.5, 4.5, 5.5, 1.5, EOC,
	// 43 '+'
	43, 3.5, 1, 3.5, 5, 1.5, 3, 5.5, 3, EOC,
	44, 3.5, 6, 3.5, 5, EOC,
	45, 1, 3, 6, 3, EOC,
	46, 3, 0, 4, 0, EOC,
	47, 0, 7, 7, 0, EOC,
	48, 0, 0, 7, 0, 7, 0, 7, 6, 7, 6, 0, 6, 0, 6, 0, 0, EOC,
	49, 3.5, 0, 3.5, 6, EOC,
	50, 7, 0, 0, 0, 0, 0, 0, 3, 0, 3, 7, 3, 7, 3, 7, 6, 7, 6, 0, 6, EOC,
	51, 0, 0, 7, 0, 7, 0, 7, 3, 7, 3, 4, 3, 4, 3, 7, 6, 7, 6, 0, 6, EOC,
	52, 5, 0, 5, 6, 5, 6, 0, 3, 0, 3, 7, 3, EOC,
	53, 0, 3, 0, 6, 0, 6, 7, 6, 0, 3, 7, 3, 0, 0, 7, 0, 7, 0, 7, 3, EOC,
	54, 0, 0, 0, 6, 0, 6, 7, 6, 0, 3, 7, 3, 0, 0, 7, 0, 7, 0, 7, 3, EOC,
	55, 2, 0, 7, 6, 7, 6, 0, 6, EOC,
	56, 0, 0, 0, 6, 0, 6, 7, 6, 0, 3, 7, 3, 0, 0, 7, 0, 7, 0, 7, 6, EOC,
	57, 0, 0, 7, 0, 7, 0, 7, 6, 7, 6, 0, 6, 0, 6, 0, 3, 0, 3, 7, 3, EOC,
	// 58 ':'
	58, 3.5, 5, 3.5, 5, 3.5, 1, 3.5, 1, EOC,
	// 59 ';'
	59, 3.5, 5, 3.5, 5, 3.5, 1, 3.5, 0, EOC,
	60, 0, 3, 7, 0, 7, 0, 7, 7, 7, 7, 0, 3, EOC,
	// 61 '='
	61, 1, 4, 6, 4, 1, 2, 6, 2, EOC,
	62, 0, 0, 7, 3, 7, 3, 0, 7, 0, 7, 0, 0, EOC,
	63, 0, 6, 7, 6, 7, 6, 7, 3, 7, 3, 2, 3, 2, 3, 2, 2, 2, 0, 2, 1, EOC,
	// 64 '@'
	64, 0, 0, 7, 0, 7, 0, 7, 6, 7, 6, 0, 6, 0, 6, 0, 0, 2, 2, 5, 2, 5, 2, 5, 4, 5, 4, 2, 4, 6, 1, 6, 3, EOC,
	65, 0, 0, 0, 3, 0, 6, 7, 6, 0, 3, 7, 3, 0, 0, 7, 0, 7, 0, 7, 6, EOC,
	66, 0, 0, 0, 6, 0, 6, 6, 6, 6, 6, 7, 5, 7, 5, 7, 4, 7, 4, 6, 3, 6, 3, 0, 3, 0, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 2, 7, 2, 6, 3, EOC,
	67, 0, 0, 7, 0, 7, 6, 0, 6, 0, 6, 0, 0, EOC,
	68, 0, 0, 0, 6, 0, 6, 6, 6, 6, 6, 7, 5, 7, 5, 7, 1, 7, 1, 6, 0, 6, 0, 0, 0, EOC,
	69, 0, 0, 0, 6, 0, 6, 7, 6, 7, 6, 7, 3, 7, 3, 0, 3, 0, 0, 7, 0, EOC,
	70, 0, 0, 0, 6, 0, 6, 7, 6, 0, 3, 4, 3, EOC,
	71, 0, 0, 0, 6, 0, 6, 7, 6, 0, 3, 7, 3, 0, 0, 7, 0, 7, 0, 7, 3, EOC,
	72, 0, 0, 0, 6, 0, 3, 7, 3, 7, 0, 7, 6, EOC,
	73, 3.5, 0, 3.5, 6, EOC,
	74, 0, 1, 1, 0, 1, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 6, EOC,
	75, 0, 0, 0, 6, 0, 2, 7, 6, 3, 4, 7, 0, EOC,
	76, 0, 0, 7, 0, 0, 6, 0, 0, EOC,
	77, 0, 0, 0, 6, 0, 6, 3, 6, 3, 6, 4, 5, 4, 5, 4, 0, 4, 5, 6, 6, 6, 6, 7, 6, 7, 6, 7, 0, EOC,
	78, 0, 0, 0, 6, 0, 4, 2, 6, 2, 6, 7, 6, 7, 6, 7, 0, EOC,
	79, 0, 0, 7, 0, 7, 0, 7, 6, 7, 6, 0, 6, 0, 6, 0, 0, EOC,
	80, 0, 0, 0, 6, 0, 6, 7, 6, 7, 6, 7, 3, 7, 3, 0, 3, EOC,
	81, 0, 0, 0, 6, 0, 6, 7, 6, 7, 6, 7, 2, 7, 2, 6, 0, 6, 0, 0, 0, 5, 2, 7, 0, EOC,
	82, 0, 0, 0, 6, 0, 4, 2, 6, 2, 6, 7, 6, EOC,
	83, 0, 0, 7, 0, 7, 0, 7, 3, 7, 3, 0, 3, 0, 3, 0, 6, 0, 6, 7, 6, EOC,
	84, 3.5, 0, 3.5, 6, 0, 6, 7, 6, EOC,
	85, 0, 0, 7, 0, 7, 0, 7, 6, 0, 6, 0, 0, EOC,
	86, 0, 6, 3.5, 0, 3.5, 0, 7, 6, EOC,
	87, 0, 6, 0, 0, 0, 0, 3.5, 3, 3.5, 3, 7, 0, 7, 0, 7, 6, EOC,
	88, 0, 0, 7, 6, 0, 6, 7, 0, EOC,
	89, 0, 6, 3.5, 3, 3.5, 3, 3.5, 0, 3.5, 3, 7, 6, EOC,
	90, 0, 6, 7, 6, 7, 6, 0, 0, 0, 0, 7, 0, EOC,
	91, 2, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 5, 0, 5, 1, 6, 1, 6, 2, 6, EOC,
	// 92 '\'
	92, 0, 0, 7, 7, EOC,
	93, 5, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 5, 7, 5, 6, 6, 6, 6, 5, 6, EOC,
	94, 0, 0, 3.5, 6, 3.5, 6, 7, 0, EOC,
	95, 1, 0, 6, 0, EOC,
	// 96 '`'
	96, 3.5, 6, 2.5, 5.5, EOC,
	97, 0, 0, 0, 3, 0, 6, 7, 6, 0, 3, 7, 3, 0, 0, 7, 0, 7, 0, 7, 6, EOC,
	98, 0, 0, 0, 6, 0, 6, 6, 6, 6, 6, 7, 5, 7, 5, 7, 4, 7, 4, 6, 3, 6, 3, 0, 3, 0, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 2, 7, 2, 6, 3, EOC,
	99, 0, 0, 7, 0, 7, 6, 0, 6, 0, 6, 0, 0, EOC,
	100, 0, 0, 0, 6, 0, 6, 6, 6, 6, 6, 7, 5, 7, 5, 7, 1, 7, 1, 6, 0, 6, 0, 0, 0, EOC,
	101, 0, 0, 0, 6, 0, 6, 7, 6, 7, 6, 7, 3, 7, 3, 0, 3, 0, 0, 7, 0, EOC,
	102, 0, 0, 0, 6, 0, 6, 7, 6, 0, 3, 4, 3, EOC,
	103, 0, 0, 0, 6, 0, 6, 7, 6, 0, 3, 7, 3, 0, 0, 7, 0, 7, 0, 7, 3, EOC,
	104, 0, 0, 0, 6, 0, 3, 7, 3, 7, 0, 7, 6, EOC,
	105, 3.5, 0, 3.5, 6, EOC,
	106, 0, 1, 1, 0, 1, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 6, EOC,
	107, 0, 0, 0, 6, 0, 2, 7, 6, 3, 4, 7, 0, EOC,
	108, 0, 0, 7, 0, 0, 6, 0, 0, EOC,
	109, 0, 0, 0, 6, 0, 6, 3, 6, 3, 6, 4, 5, 4, 5, 4, 0, 4, 5, 6, 6, 6, 6, 7, 6, 7, 6, 7, 0, EOC,
	110, 0, 0, 0, 6, 0, 4, 2, 6, 2, 6, 7, 6, 7, 6, 7, 0, EOC,
	111, 0, 0, 7, 0, 7, 0, 7, 6, 7, 6, 0, 6, 0, 6, 0, 0, EOC,
	112, 0, 0, 0, 6, 0, 6, 7, 6, 7, 6, 7, 3, 7, 3, 0, 3, EOC,
	113, 0, 0, 0, 6, 0, 6, 7, 6, 7, 6, 7, 2, 7, 2, 6, 0, 6, 0, 0, 0, 5, 2, 7, 0, EOC,
	114, 0, 0, 0, 6, 0, 4, 2, 6, 2, 6, 7, 6, EOC,
	115, 0, 0, 7, 0, 7, 0, 7, 3, 7, 3, 0, 3, 0, 3, 0, 6, 0, 6, 7, 6, EOC,
	116, 3.5, 0, 3.5, 6, 0, 6, 7, 6, EOC,
	117, 0, 0, 7, 0, 7, 0, 7, 6, 0, 6, 0, 0, EOC,
	118, 0, 6, 3.5, 0, 3.5, 0, 7, 6, EOC,
	119, 0, 6, 0, 0, 0, 0, 3.5, 3, 3.5, 3, 7, 0, 7, 0, 7, 6, EOC,
	120, 0, 0, 7, 6, 0, 6, 7, 0, EOC,
	121, 0, 6, 3.5, 3, 3.5, 3, 3.5, 0, 3.5, 3, 7, 6, EOC,
	122, 0, 6, 7, 6, 7, 6, 0, 0, 0, 0, 7, 0, EOC,
	123, 2, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 5, 0, 5, 1, 6, 1, 6, 2, 6, EOC,
	// 124 '|'
	124, 3.5, 0, 3.5, 6, EOC,
	125, 5, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 5, 7, 5, 6, 6, 6, 6, 5, 6, EOC,
	// 126 '~'
	126, 1, 4, 3, 5, 3, 5, 5, 4, 5, 4, 6.5, 5, EOC,

	127, // // Ship without thrust
	 6, 2, 18, 6 ,   // top edge to nose
	 18, 6, 6, 10 ,  // bottom edge from nose
	 6, 10, 7, 8 ,   // rear bottom short
	 7, 8, 7, 4 ,    // rear inner vertical
	 7, 4, 6, 2 ,    // rear top short
	 EOC,

	 128, // Ship with thrust
	 6, 2, 18, 6,   // top edge to nose
	 18, 6, 6, 10,  // bottom edge from nose
	 6, 10, 7, 8,   // rear bottom short
	 7, 8, 7, 4,    // rear inner vertical
	 7, 4, 6, 2,    // rear top short

	 // Tail notch
	 7, 4, 3, 6,    // inner top to tail
	 3, 6, 7, 8, EOC, // tail to inner bottom

	 // 0x81 (129) - Explosion (8 asterisks at compass points, 1.5 diameters apart)
	 // Bounding box: x=[6, 26] y=[6.5, 25.5]  Center: (16, 16)
	 129,
	 // N
	 16.0, 21.5, 16.0, 25.5,  13.5, 23.5, 18.5, 23.5,  14.0, 22.0, 18.0, 25.0,  14.0, 25.0, 18.0, 22.0,
	 // NE
	 21.3, 19.3, 21.3, 23.3,  18.8, 21.3, 23.8, 21.3,  19.3, 19.8, 23.3, 22.8,  19.3, 22.8, 23.3, 19.8,
	 // E
	 23.5, 14.0, 23.5, 18.0,  21.0, 16.0, 26.0, 16.0,  21.5, 14.5, 25.5, 17.5,  21.5, 17.5, 25.5, 14.5,
	 // SE
	 21.3, 8.7, 21.3, 12.7,  18.8, 10.7, 23.8, 10.7,  19.3, 9.2, 23.3, 12.2,  19.3, 12.2, 23.3, 9.2,
	 // S
	 16.0, 6.5, 16.0, 10.5,  13.5, 8.5, 18.5, 8.5,  14.0, 7.0, 18.0, 10.0,  14.0, 10.0, 18.0, 7.0,
	 // SW
	 10.7, 8.7, 10.7, 12.7,  8.2, 10.7, 13.2, 10.7,  8.7, 9.2, 12.7, 12.2,  8.7, 12.2, 12.7, 9.2,
	 // W
	 8.5, 14.0, 8.5, 18.0,  6.0, 16.0, 11.0, 16.0,  6.5, 14.5, 10.5, 17.5,  6.5, 17.5, 10.5, 14.5,
	 // NW
	 10.7, 19.3, 10.7, 23.3,  8.2, 21.3, 13.2, 21.3,  8.7, 19.8, 12.7, 22.8,  8.7, 22.8, 12.7, 19.8,
	 EOC,

		 -5, -5
};

// ----
// Singleton Access
// ----
VectorFont& VectorFont::Instance()
{
	static VectorFont instance;
	return instance;
}

// ----
// Constructor / Destructor
// ----
VectorFont::VectorFont()
	: vfProgram(0)
	, vfVAO(0)
	, vfVBO(0)
	, attrPos(-1)
	, attrColor(-1)
	, attrOrigin(-1)
	, attrAngle(-1)
	, uniMVP(-1)
	, proj()
	, screenWidth(0)
	, screenHeight(0)
	, lastx(0)
	, lastscale(1.0f)
{
	InitFontData();
}

VectorFont::~VectorFont()
{
	if (vfVAO) { glDeleteVertexArrays(1, &vfVAO); vfVAO = 0; }
	if (vfVBO) { glDeleteBuffers(1, &vfVBO); vfVBO = 0; }

	
	if (quadVAO) { glDeleteVertexArrays(1, &quadVAO); quadVAO = 0; }
	if (quadVBO) { glDeleteBuffers(1, &quadVBO); quadVBO = 0; }

	if (vfProgram) { glDeleteProgram(vfProgram); vfProgram = 0; }
}
// ----
// Initialize
// ----
void VectorFont::Initialize(int width, int height)
{
	// GL program/VAO creation only on the GL chain; under Vulkan the glyph
	// strokes route through beam_add_line (see End) and need no GL objects.
	// The CPU-side fields below are required by BOTH chains.
	if (active_renderer() != RENDERER_VULKAN)
		InitGL();

	screenWidth = width;
	screenHeight = height;

	// Use aae::math::ortho from MathUtils.h
	proj = aae::math::ortho(0.0f, (float)width, 0.0f, (float)height);

}

void VectorFont::Resize(int width, int height)
{
	screenWidth = width;
	screenHeight = height;

	proj = aae::math::ortho(0.0f, (float)width, 0.0f, (float)height);
	glViewport(0, 0, screenWidth, screenHeight);
}

void VectorFont::SetOverrideViewport(bool enable)
{
	overrideViewport = enable;
}

// ----
// Begin
// ----
void VectorFont::Begin()
{
	// Under Vulkan there is no GL context: every gl* below is a null GLEW
	// pointer (first GUI frame crashed here before this guard). Vertex
	// accumulation is CPU-side, so Begin has nothing to do on the VK chain.
	if (active_renderer() == RENDERER_VULKAN)
		return;

	if (overrideViewport) {
		glViewport(0, 0, screenWidth, screenHeight);
	}

	glUseProgram(vfProgram);

	// Use aae::math::value_ptr to get the raw float pointer
	glUniformMatrix4fv(uniMVP, 1, GL_FALSE, aae::math::value_ptr(proj));

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// ----
// Modern AA stroke parameters (shared coverage-AA beam line path)
// ----
// Stroke half-width and feather are in the font's logical units (the active
// projection, e.g. the 1024x768 overlay space). Because the strokes are real
// geometry in glyph space, they scale WITH the text -- so they keep the same
// proportions at 1280x1024 and 4K, unlike the old constant-pixel glLineWidth.
static constexpr float kFontHalf   = 0.70f;  // stroke half-width  (logical units)
static constexpr float kFontAA     = 0.80f;  // edge feather       (logical units)
static constexpr float kFontEndcap = 1.0f;   // round cap at true terminations (x half)
static constexpr float kFontCorner = 1.0f;   // round join at stroke corners   (x half)

// Reused across frames to avoid per-call allocation.
static std::vector<BeamLine> s_fontSegs;
static std::vector<BeamJoin> s_fontCaps;

// Rotate point p about origin o by 'deg' degrees (matches the old VF vertex
// shader, moved to the CPU so the beam line shader can be reused unchanged).
static inline aae::math::vec2 vf_rotate(aae::math::vec2 p, aae::math::vec2 o, float deg)
{
	if (deg == 0.0f) return p;
	const float rad = deg * 0.01745329252f;
	const float c = cosf(rad), s = sinf(rad);
	const float lx = p.x - o.x, ly = p.y - o.y;
	return aae::math::vec2(lx * c - ly * s + o.x, lx * s + ly * c + o.y);
}

// ----
// End
// ----
void VectorFont::End()
{
	if (drawVerts.empty())
	{
		if (active_renderer() != RENDERER_VULKAN)
			glUseProgram(0);
		return;
	}

	if (active_renderer() == RENDERER_VULKAN)
	{
		// beam_draw_lines/beam_draw_caps
		// below issue real GL draw calls (glUseProgram, glDrawArraysInstanced,
		// ...), so VF text would be invisible under Vulkan. Each glyph
		// stroke goes through beam_add_line instead -- the SAME CPU-side beam queue
		// vkchain_render's vector branch (vulkan_renderer.cpp) already
		// consumes every frame for the GUI driver (VIDEO_TYPE_VECTOR), so text
		// rides the VK vector path with no separate draw code.
		// No GL calls are made on this branch.
		//
		// Two accepted deviations from the GL look (least-code
		// path):
		//  - stroke half-width follows config.linewidth (beam_add_line's
		//    fixed half-width), not the font-tuned kFontHalf/kFontAA below.
		//  - text blends with the frame's single additive/alpha-over choice
		//    (the GUI driver is VECTOR_USES_COLOR -> additive) instead of
		//    always alpha-over; on white/colored text over black this reads
		//    the same, at most a touch brighter where strokes cross.
		//
		// Y is rescaled 768->1024: this ortho is Initialize(1024,768), but the
		// beam queue (and vkchain_render's GUI mapping, see GuiBeamToWindowPx
		// in vulkan_renderer.cpp) assumes the same shared 0..1024 box GL's
		// fbo1 canvas uses for both VF text and beam content.
		// GUI text is authored Y-down (menu rows grow downward) while the beam
		// ortho is Y-up, so SOMETHING must flip it. Which thing depends on
		// which coordinate regime this draw lands in - see
		// vkchain_ui_overlay_active():
		//
		//   in-game overlay pass -> mapped through the DEFAULT 0..1024 box,
		//     which carries no per-game flip, so mirror here.
		//   GUI front-end        -> mapped through the [gui] rect from
		//     video.ini, whose INVERTED bottom/top (1083 -> 6) already IS the
		//     flip. GL has always relied on exactly that. Mirroring here too
		//     flips it twice and the menu comes out upside down.
		//
		// The mirror MUST stay conditional: the VK game_rect readers
		// honor an inverted bottom/top range instead of treating it as
		// degenerate, so the video.ini flip reaches this draw. Mirroring
		// unconditionally on top of it stands the menu on its head (and the
		// same inverted range is what keeps Cinematronics/SegaG80 upright).
		const bool mirrorY = vkchain_ui_overlay_active();
		static constexpr float kGuiToBeamY = 1024.0f / 768.0f;
		for (size_t i = 0; i + 1 < drawVerts.size(); i += 2)
		{
			const VFVertex& a = drawVerts[i];
			const VFVertex& b = drawVerts[i + 1];
			const aae::math::vec2 p0 = vf_rotate(a.pos, a.origin, a.angle);
			const aae::math::vec2 p1 = vf_rotate(b.pos, b.origin, b.angle);
			const float y0 = mirrorY ? (768.0f - p0.y) : p0.y;
			const float y1 = mirrorY ? (768.0f - p1.y) : p1.y;
			// kFontHalf, NOT the game beam width: these are glyph strokes and
			// the GL path draws them at the font's own tuned half-width (see
			// the s_fontSegs push below). Inheriting config.linewidth made the
			// menu visibly fatter and brighter than GL.
			beam_add_line(p0.x, y0 * kGuiToBeamY,
			              p1.x, y1 * kGuiToBeamY, 255, a.color, kFontHalf);
		}
		drawVerts.clear();
		return;
	}

	// The font's glyph strokes ARE line segments, so render them with the beam's
	// coverage-AA line shader instead of GL_LINES + GL_LINE_SMOOTH / GL_POINTS.
	// The beam shader has no rotation of its own; apply each string's rotation here.
	s_fontSegs.clear();
	s_fontSegs.reserve(drawVerts.size() / 2);
	for (size_t i = 0; i + 1 < drawVerts.size(); i += 2)
	{
		const VFVertex& a = drawVerts[i];
		const VFVertex& b = drawVerts[i + 1];
		const aae::math::vec2 p0 = vf_rotate(a.pos, a.origin, a.angle);
		const aae::math::vec2 p1 = vf_rotate(b.pos, b.origin, b.angle);
		s_fontSegs.push_back({ p0, p1, kFontHalf, a.color });
	}

	// Round caps/joins tie the strokes together (replaces the per-vertex GL_POINTS).
	s_fontCaps.clear();
	beam_build_caps(s_fontSegs.data(), (int)s_fontSegs.size(), kFontEndcap, kFontCorner, s_fontCaps);

	// Alpha-over (additive = false): text composites like the B/W beam path.
	beam_draw_lines(proj, s_fontSegs.data(), (int)s_fontSegs.size(), kFontAA, false);
	beam_draw_caps (proj, s_fontCaps.data(), (int)s_fontCaps.size(), kFontAA, false);

	glUseProgram(0);
	drawVerts.clear();
}

// ----
// DrawQuad
// ----
void VectorFont::DrawQuad(float x, float y, float width, float height, rgb_t color)
{
	if (active_renderer() == RENDERER_VULKAN)
	{
		// GL path below draws through this class's own GL program/VAO
		// (GL-direct, invisible under Vulkan). The VK path reuses
		// ScreenQuadVK::RecordRect (already online for the raster composite)
		// with a 1x1 white texture tinted by 'color'. No GL calls are made
		// on this branch.
		vkchain_gui_draw_quad(x, y, width, height, color);
		return;
	}

	const float minx = x - (width * 0.5f);
	const float miny = y - (height * 0.5f);
	const float maxx = x + (width * 0.5f);
	const float maxy = y + (height * 0.5f);

	// Temporary immediate-mode draw using the new attributes
	std::vector<VFVertex> quadV;
	quadV.reserve(6);

	VFVertex qv;
	qv.origin = aae::math::vec2(0.0f, 0.0f);
	qv.angle = 0.0f;
	qv.color = color;

	qv.pos = aae::math::vec2(minx, miny); quadV.push_back(qv);
	qv.pos = aae::math::vec2(maxx, miny); quadV.push_back(qv);
	qv.pos = aae::math::vec2(maxx, maxy); quadV.push_back(qv);

	qv.pos = aae::math::vec2(minx, miny); quadV.push_back(qv);
	qv.pos = aae::math::vec2(maxx, maxy); quadV.push_back(qv);
	qv.pos = aae::math::vec2(minx, maxy); quadV.push_back(qv);

	glUseProgram(vfProgram);
	glUniformMatrix4fv(uniMVP, 1, GL_FALSE, aae::math::value_ptr(proj));

	glBindVertexArray(quadVAO);

	glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
	glBufferData(GL_ARRAY_BUFFER, quadV.size() * sizeof(VFVertex), quadV.data(), GL_STREAM_DRAW);

	glEnableVertexAttribArray(attrPos);
	glVertexAttribPointer(attrPos, 2, GL_FLOAT, GL_FALSE, sizeof(VFVertex), (void*)offsetof(VFVertex, pos));

	glEnableVertexAttribArray(attrOrigin);
	glVertexAttribPointer(attrOrigin, 2, GL_FLOAT, GL_FALSE, sizeof(VFVertex), (void*)offsetof(VFVertex, origin));

	glEnableVertexAttribArray(attrAngle);
	glVertexAttribPointer(attrAngle, 1, GL_FLOAT, GL_FALSE, sizeof(VFVertex), (void*)offsetof(VFVertex, angle));

	glEnableVertexAttribArray(attrColor);
	glVertexAttribPointer(attrColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VFVertex), (void*)offsetof(VFVertex, color));

	glDrawArrays(GL_TRIANGLES, 0, 6);

	glDisableVertexAttribArray(attrPos);
	glDisableVertexAttribArray(attrOrigin);
	glDisableVertexAttribArray(attrAngle);
	glDisableVertexAttribArray(attrColor);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ----
// DrawLine
// ----
void VectorFont::DrawLine(float x0, float y0, float x1, float y1, rgb_t color)
{
	const aae::math::vec2 origin(0.0f, 0.0f);
	drawVerts.push_back({ aae::math::vec2(x0, y0), origin, 0.0f, color });
	drawVerts.push_back({ aae::math::vec2(x1, y1), origin, 0.0f, color });
}

// ----
// Print (Legacy Overload)
// ----
void VectorFont::Print(float x, int y, rgb_t color, float scale, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	char text[EOC];
	vsnprintf(text, sizeof(text), fmt, ap);

	va_end(ap);

	// Delegate to the main Print with angle = 0.0f
	Print(x, y, color, scale, 0.0f, "%s", text);
}

// ----
// PrintCentered
// ----
void VectorFont::PrintCentered(int y, rgb_t color, float scale, const char* str)
{
	if (!str || str[0] == '\0') return;

	const float total = GetStringPitch(str, scale, 0);
	const float scrW = (screenWidth > 0) ? (float)screenWidth : 1024.0f;
	const float x = (scrW * 0.5f) - (total * 0.5f);

	Print(x, y, color, scale, 0.0f, "%s", str);
}

// ----
// Marquee support
// ----
// Constant-speed scroll with a dwell at each end, then snap back and repeat.
// Phase is derived from a shared steady clock and the string's overflow, so
// marquees need no per-string state; strings with the same overflow scroll in
// step, longer strings simply take longer per cycle.
static constexpr float  kMarqueeSpeed   = 60.0f;   // logical units per second
static constexpr double kMarqueeDwellMs = 1000.0;  // hold at each end

static double vf_now_ms()
{
	using namespace std::chrono;
	static const steady_clock::time_point t0 = steady_clock::now();
	return duration<double, std::milli>(steady_clock::now() - t0).count();
}

static float vf_marquee_offset(float overflow)
{
	const double scrollMs = (overflow / kMarqueeSpeed) * 1000.0;
	const double cycleMs  = kMarqueeDwellMs + scrollMs + kMarqueeDwellMs;
	double phase = fmod(vf_now_ms(), cycleMs);
	if (phase <= kMarqueeDwellMs) return 0.0f;             // dwell at start
	phase -= kMarqueeDwellMs;
	if (phase < scrollMs)
		return (float)(phase * (kMarqueeSpeed / 1000.0));  // constant-speed scroll
	return overflow;                                       // dwell at end
}

// Clip a stroke segment to the horizontal band [L, R] in unrotated local
// space. Returns false when the segment lies entirely outside. Endpoints are
// interpolated along the segment, so glyphs cut cleanly mid-stroke at the
// field edges -- the classic marquee look.
static inline bool vf_clip_seg_x(float& x0, float& y0, float& x1, float& y1, float L, float R)
{
	if (x0 <= x1) { if (x1 < L || x0 > R) return false; }
	else          { if (x0 < L || x1 > R) return false; }

	const float dx = x1 - x0;
	if (dx != 0.0f)
	{
		const float slope = (y1 - y0) / dx;
		if (x0 < L) { y0 += slope * (L - x0); x0 = L; }
		else if (x0 > R) { y0 += slope * (R - x0); x0 = R; }
		if (x1 < L) { y1 += slope * (L - x1); x1 = L; }
		else if (x1 > R) { y1 += slope * (R - x1); x1 = R; }
	}
	return true;
}

// ----
	// Private Internal Helper: Generates vertices with explicit rotation origin
	// ----
void VectorFont::DrawTextInternal(float x, float y, const aae::math::vec2& rotationOrigin,
	rgb_t color, float scale, float angle, const char* text,
	float clipL, float clipR)
{
	for (int i = 0; text[i]; ++i)
	{
		const unsigned char ch = (unsigned char)text[i];

		// Space character: advance by space width, no geometry
		if (ch == 32)
		{
			x += fontwidth[32] * scale;
			continue;
		}

		// Glyph geometry spans [x, x + width*scale]; once the pen passes the
		// right clip edge nothing further can be visible.
		if (x > clipR)
			break;

		int idx = fstart[ch] + 1;
		int bidx = idx + 1;
		const float offset = fontoffset[ch];  // Shift glyph flush left

		// Entirely left of the clip window: advance only, no geometry.
		if (x + fontwidth[ch] * scale < clipL)
		{
			x += (fontwidth[ch] + CHAR_GAP) * scale;
			continue;
		}

		while ((int)fontdata[idx] != EOC)
		{
			// Vertex positions shifted by -offset to remove left dead space
			float x0 = (fontdata[idx] - offset) * scale + x;
			float y0 = fontdata[bidx] * scale + y;
			float x1 = (fontdata[idx + 2] - offset) * scale + x;
			float y1 = fontdata[bidx + 2] * scale + y;

			// Clip against the field edges in local (pre-rotation) space so a
			// clipped window rotates with the string.
			if (vf_clip_seg_x(x0, y0, x1, y1, clipL, clipR))
			{
				// Apply the specific pivot point passed by the caller
				VFVertex v1 = { aae::math::vec2(x0, y0), rotationOrigin, angle, color };
				VFVertex v2 = { aae::math::vec2(x1, y1), rotationOrigin, angle, color };

				drawVerts.push_back(v1);
				drawVerts.push_back(v2);
			}

			idx += 4;
			bidx += 4;
		}

		// Advance by this glyph's proportional width + inter-character gap
		x += (fontwidth[ch] + CHAR_GAP) * scale;
	}

	lastx = (int)x;
	lastscale = scale;
}

// ----
// Print (Standard)
// Rotates around the STARTING (X,Y) position.
// ----
void VectorFont::Print(float x, int y, rgb_t color, float scale, float angle, const char* fmt, ...)
{
	if (!fmt) return;

	char text[EOC];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);

	if (text[0] == '\0') return;

	// Pivot is the exact starting coordinate passed by the user.
	aae::math::vec2 origin(x, (float)y);

	DrawTextInternal(x, (float)y, origin, color, scale, angle, text);
}

// ----
// PrintCentered
// Rotates around the GEOMETRIC CENTER of the text line.
// ----
void VectorFont::PrintCentered(int y, rgb_t color, float scale, float angle, const char* str)
{
	if (!str || str[0] == '\0') return;

	// 1. Calculate dimensions using centralized pitch calculation
	const float totalWidth = GetStringPitch(str, scale, 0);

	// 2. Determine screen width (fallback to 1024 if 0)
	const float scrW = (screenWidth > 0) ? (float)screenWidth : 1024.0f;

	// 3. Calculate Start X (to center horizontally)
	const float startX = (scrW * 0.5f) - (totalWidth * 0.5f);

	// 4. STRATEGY: Pivot is the center of the bounding box.
	//    X = Center of the line
	//    Y = Middle of the font height (range 0..7 -> middle is 3.5)
	const float centerX = startX + (totalWidth * 0.5f);
	const float centerY = (float)y + (3.5f * scale);

	aae::math::vec2 center(centerX, centerY);

	DrawTextInternal(startX, (float)y, center, color, scale, angle, str);
}

// ----
// PrintMarquee
// Draws like Print when the text fits in fieldWidth; otherwise scrolls the
// text at constant speed within [x, x + fieldWidth], clipped to the field.
// Rotation pivots on (x, y) -- the field origin -- matching Print semantics.
// ----
void VectorFont::PrintMarquee(float x, int y, float fieldWidth, rgb_t color, float scale, float angle, const char* fmt, ...)
{
	if (!fmt) return;

	char text[EOC];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);

	if (text[0] == '\0') return;

	const aae::math::vec2 origin(x, (float)y);
	const float w = GetStringPitch(text, scale, 0);

	if (w <= fieldWidth)
	{
		DrawTextInternal(x, (float)y, origin, color, scale, angle, text);
		return;
	}

	const float off = vf_marquee_offset(w - fieldWidth);
	DrawTextInternal(x - off, (float)y, origin, color, scale, angle, text,
		x, x + fieldWidth);
}

void VectorFont::PrintMarquee(float x, int y, float fieldWidth, rgb_t color, float scale, const char* fmt, ...)
{
	if (!fmt) return;

	char text[EOC];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);

	PrintMarquee(x, y, fieldWidth, color, scale, 0.0f, "%s", text);
}

// ----
// PrintMarqueeCentered
// Centered when the text fits; otherwise the field itself is centered and the
// text scrolls within it. Rotation pivots on the field's center, matching
// PrintCentered semantics.
// ----
void VectorFont::PrintMarqueeCentered(int y, float fieldWidth, rgb_t color, float scale, float angle, const char* str)
{
	if (!str || str[0] == '\0') return;

	const float w = GetStringPitch(str, scale, 0);
	if (w <= fieldWidth)
	{
		PrintCentered(y, color, scale, angle, str);
		return;
	}

	const float scrW = (screenWidth > 0) ? (float)screenWidth : 1024.0f;
	const float fieldX = (scrW - fieldWidth) * 0.5f;
	const aae::math::vec2 center(fieldX + fieldWidth * 0.5f, (float)y + 3.5f * scale);

	const float off = vf_marquee_offset(w - fieldWidth);
	DrawTextInternal(fieldX - off, (float)y, center, color, scale, angle, str,
		fieldX, fieldX + fieldWidth);
}

void VectorFont::PrintMarqueeCentered(int y, float fieldWidth, rgb_t color, float scale, const char* str)
{
	PrintMarqueeCentered(y, fieldWidth, color, scale, 0.0f, str);
}

// ----
// PrintClippedCentered
// Field clipping without the animation: centered when the text fits,
// otherwise the beginning of the string fills the centered field and the
// overflow is cut at the edge. Used for rows that shouldn't scroll.
// ----
void VectorFont::PrintClippedCentered(int y, float fieldWidth, rgb_t color, float scale, const char* str)
{
	if (!str || str[0] == '\0') return;

	const float w = GetStringPitch(str, scale, 0);
	if (w <= fieldWidth)
	{
		PrintCentered(y, color, scale, 0.0f, str);
		return;
	}

	const float scrW = (screenWidth > 0) ? (float)screenWidth : 1024.0f;
	const float fieldX = (scrW - fieldWidth) * 0.5f;
	const aae::math::vec2 center(fieldX + fieldWidth * 0.5f, (float)y + 3.5f * scale);

	DrawTextInternal(fieldX, (float)y, center, color, scale, 0.0f, str,
		fieldX, fieldX + fieldWidth);
}

// ----
// DrawGlyph
// Draws a single glyph centered exactly at (x,y), rotated around that center.
// ----
void VectorFont::DrawGlyph(float x, float y, int glyph, rgb_t color, float scale, float angle)
{
	if (glyph < 0 || glyph > 255) return;

	// 1. Find the start index for this glyph
	// Note: fstart is initialized in InitFontData. 
	// If the glyph hasn't been defined, fstart usually defaults to the space char or 0.
	// We double check if it points to valid data.
	int idx = fstart[glyph];
	if (static_cast<int>(fontdata[idx]) != glyph) {
		// Fallback: If map is incorrect, try to find it (optional safety)
		// or just return to avoid crashing. 
		// For standard initialized data, fstart[glyph] should be correct.
		return;
	}

	// 2. Calculate the Bounding Box of the glyph to find its center
	float minX = 10000.0f, maxX = -10000.0f;
	float minY = 10000.0f, maxY = -10000.0f;

	// Skip the glyph ID
	int scanner = idx + 1;
	bool hasData = false;

	while (static_cast<int>(fontdata[scanner]) != EOC)
	{
		// Read segment (x1, y1, x2, y2)
		float vx1 = fontdata[scanner];
		float vy1 = fontdata[scanner + 1];
		float vx2 = fontdata[scanner + 2];
		float vy2 = fontdata[scanner + 3];

		if (vx1 < minX) minX = vx1;
		if (vx1 > maxX) maxX = vx1;
		if (vy1 < minY) minY = vy1;
		if (vy1 > maxY) maxY = vy1;

		if (vx2 < minX) minX = vx2;
		if (vx2 > maxX) maxX = vx2;
		if (vy2 < minY) minY = vy2;
		if (vy2 > maxY) maxY = vy2;

		hasData = true;
		scanner += 4;
	}

	if (!hasData) return;

	// 3. Determine the geometric center of the glyph data
	float cx = (minX + maxX) * 0.5f;
	float cy = (minY + maxY) * 0.5f;

	// 4. Calculate the drawing start position.
	// We want the glyph's (cx, cy) to land exactly on world coordinates (x, y).
	// DrawTextInternal shifts vertices by -fontoffset, so effective center is (cx - fontoffset).
	// We want: x = (cx - fontoffset) * scale + startPos  =>  startPos = x - (cx - fontoffset) * scale
	float drawX = x - ((cx - fontoffset[glyph]) * scale);
	float drawY = y - (cy * scale);

	// 5. Construct a temporary string containing just this character
	char str[2] = { (char)glyph, '\0' };

	// 6. Draw it using the internal helper.
	// The 'rotationOrigin' is the target (x,y) because we want to spin around the screen position.
	DrawTextInternal(drawX, drawY, aae::math::vec2(x, y), color, scale, angle, str);
}

// ----
// GetStringPitch
// ----
float VectorFont::GetStringPitch(const char* str, float scale, int set)
{
	(void)set;
	if (!str) return 0.0f;

	float total = 0.0f;
	for (int i = 0; str[i]; ++i)
	{
		const unsigned char ch = (unsigned char)str[i];
		if (ch == 32)
			total += fontwidth[32] * scale;
		else
			total += (fontwidth[ch] + CHAR_GAP) * scale;
	}
	return total;
}

// ----
// InitGL
// ----
void VectorFont::InitGL()
{
	GLuint vs = CompileShader(GL_VERTEX_SHADER, vfVertexShader, "Vector Font VS");
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, vfFragmentShader, "Vector Font FS");
	vfProgram = LinkShaderProgram(vs, fs);

	attrPos = glGetAttribLocation(vfProgram, "aPos");
	attrColor = glGetAttribLocation(vfProgram, "aColor");
	attrOrigin = glGetAttribLocation(vfProgram, "aOrigin");
	attrAngle = glGetAttribLocation(vfProgram, "aAngle");
	uniMVP = glGetUniformLocation(vfProgram, "uMVP");

	glGenBuffers(1, &vfVBO);
	glGenVertexArrays(1, &vfVAO);

	glGenBuffers(1, &quadVBO);
	glGenVertexArrays(1, &quadVAO);
}

// ----
// SetProjection
// ----
void VectorFont::SetProjection(const aae::math::mat4& mvp)
{
	proj = mvp;
}

// ----
// InitFontData
// ----
void VectorFont::InitFontData()
{
	int a = 0;

	for (int i = 0; i < 257; ++i)
		fstart[i] = 32;

	// Initialize all widths/offsets to 0
	for (int i = 0; i < 256; ++i)
	{
		fontwidth[i] = 0.0f;
		fontoffset[i] = 0.0f;
	}

	while (fontdata[a] > -1.0f)
	{
		const int d = (int)fontdata[a];
		if (d > 29 && d < 255)
			fstart[d] = a;
		a++;
	}

	// Calculate proportional width for each glyph using actual bounding box
	for (int ch = 30; ch < 255; ++ch)
	{
		int idx = fstart[ch];
		if (static_cast<int>(fontdata[idx]) != ch)
			continue;  // Glyph not defined

		float minX = 10000.0f;
		float maxX = -10000.0f;
		int scanner = idx + 1;  // Skip glyph ID
		bool hasData = false;

		while (static_cast<int>(fontdata[scanner]) != EOC)
		{
			float vx1 = fontdata[scanner];
			float vx2 = fontdata[scanner + 2];
			if (vx1 < minX) minX = vx1;
			if (vx1 > maxX) maxX = vx1;
			if (vx2 < minX) minX = vx2;
			if (vx2 > maxX) maxX = vx2;
			hasData = true;
			scanner += 4;
		}

		if (hasData)
		{
			fontoffset[ch] = minX;                // Left bearing to subtract
			fontwidth[ch] = maxX - minX;           // Actual visible width
		}
	}

	// Space character: fixed width, no offset
	fontoffset[32] = 0.0f;
	fontwidth[32] = SPACE_WIDTH;
}