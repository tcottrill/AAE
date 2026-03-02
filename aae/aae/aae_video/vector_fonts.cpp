// ============================================================================
// vector_fonts.cpp
// ============================================================================
#include "vector_fonts.h"

#include "shader_util.h"
#include "colordefs.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>

// Disable warnings about double-to-float conversions in the font data.
#pragma warning(disable : 4305)

static constexpr float CHAR_GAP = 2.0f;     // Inter-character gap (unscaled units)
static constexpr float SPACE_WIDTH = 7.0f;   // Space character width (unscaled units)
static constexpr int EOC = 256;

// -----------------------------------------------------------------------------
// Inline Shaders for OpenGL 2.1
// -----------------------------------------------------------------------------
static const char* vfVertexShader = R"glsl(
#version 120
attribute vec2 aPos;
attribute vec2 aOrigin;
attribute float aAngle;
attribute vec4 aColor;
varying vec4 vColor;
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
#version 120
varying vec4 vColor;
void main() {
    gl_FragColor = vColor;
}
)glsl";

// -----------------------------------------------------------------------------
// Full Embedded Font Data
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// Singleton Access
// -----------------------------------------------------------------------------
VectorFont& VectorFont::Instance()
{
	static VectorFont instance;
	return instance;
}

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------
VectorFont::VectorFont()
	: vfProgram(0)
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
	if (vfVBO)
	{
		glDeleteBuffers(1, &vfVBO);
		vfVBO = 0;
	}

	if (vfProgram)
	{
		glDeleteProgram(vfProgram);
		vfProgram = 0;
	}
}

// -----------------------------------------------------------------------------
// Initialize
// -----------------------------------------------------------------------------
void VectorFont::Initialize(int width, int height)
{
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

// -----------------------------------------------------------------------------
// Begin
// -----------------------------------------------------------------------------
void VectorFont::Begin()
{
	glViewport(0, 0, screenWidth, screenHeight);

	glUseProgram(vfProgram);

	// Use aae::math::value_ptr to get the raw float pointer
	glUniformMatrix4fv(uniMVP, 1, GL_FALSE, aae::math::value_ptr(proj));

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// -----------------------------------------------------------------------------
// End
// -----------------------------------------------------------------------------
void VectorFont::End()
{
	if (drawVerts.empty())
	{
		glUseProgram(0);
		return;
	}

	glBindBuffer(GL_ARRAY_BUFFER, vfVBO);
	glBufferData(GL_ARRAY_BUFFER, drawVerts.size() * sizeof(VFVertex), drawVerts.data(), GL_DYNAMIC_DRAW);

	// Position (pos)
	glEnableVertexAttribArray(attrPos);
	glVertexAttribPointer(attrPos, 2, GL_FLOAT, GL_FALSE, sizeof(VFVertex), (void*)offsetof(VFVertex, pos));

	// Origin (origin)
	glEnableVertexAttribArray(attrOrigin);
	glVertexAttribPointer(attrOrigin, 2, GL_FLOAT, GL_FALSE, sizeof(VFVertex), (void*)offsetof(VFVertex, origin));

	// Angle
	glEnableVertexAttribArray(attrAngle);
	glVertexAttribPointer(attrAngle, 1, GL_FLOAT, GL_FALSE, sizeof(VFVertex), (void*)offsetof(VFVertex, angle));

	// Color
	glEnableVertexAttribArray(attrColor);
	glVertexAttribPointer(attrColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VFVertex), (void*)offsetof(VFVertex, color));

	glDrawArrays(GL_LINES, 0, (GLsizei)drawVerts.size());
	glDrawArrays(GL_POINTS, 0, (GLsizei)drawVerts.size());

	glDisableVertexAttribArray(attrPos);
	glDisableVertexAttribArray(attrOrigin);
	glDisableVertexAttribArray(attrAngle);
	glDisableVertexAttribArray(attrColor);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);

	drawVerts.clear();
}

// -----------------------------------------------------------------------------
// DrawQuad
// -----------------------------------------------------------------------------
void VectorFont::DrawQuad(float x, float y, float width, float height, rgb_t color)
{
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

	GLuint tmpVBO = 0;
	glGenBuffers(1, &tmpVBO);
	glBindBuffer(GL_ARRAY_BUFFER, tmpVBO);
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

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDeleteBuffers(1, &tmpVBO);
}

// -----------------------------------------------------------------------------
// Print (Legacy Overload)
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// PrintCentered
// -----------------------------------------------------------------------------
void VectorFont::PrintCentered(int y, rgb_t color, float scale, const char* str)
{
	if (!str || str[0] == '\0') return;

	const float total = GetStringPitch(str, scale, 0);
	const float scrW = (screenWidth > 0) ? (float)screenWidth : 1024.0f;
	const float x = (scrW * 0.5f) - (total * 0.5f);

	Print(x, y, color, scale, 0.0f, "%s", str);
}

// -----------------------------------------------------------------------------
	// Private Internal Helper: Generates vertices with explicit rotation origin
	// -----------------------------------------------------------------------------
void VectorFont::DrawTextInternal(float x, float y, const aae::math::vec2& rotationOrigin,
	rgb_t color, float scale, float angle, const char* text)
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

		int idx = fstart[ch] + 1;
		int bidx = idx + 1;
		const float offset = fontoffset[ch];  // Shift glyph flush left

		while ((int)fontdata[idx] != EOC)
		{
			// Vertex positions shifted by -offset to remove left dead space
			const float x0 = (fontdata[idx] - offset) * scale + x;
			const float y0 = fontdata[bidx] * scale + y;
			const float x1 = (fontdata[idx + 2] - offset) * scale + x;
			const float y1 = fontdata[bidx + 2] * scale + y;

			// Apply the specific pivot point passed by the caller
			VFVertex v1 = { aae::math::vec2(x0, y0), rotationOrigin, angle, color };
			VFVertex v2 = { aae::math::vec2(x1, y1), rotationOrigin, angle, color };

			drawVerts.push_back(v1);
			drawVerts.push_back(v2);

			idx += 4;
			bidx += 4;
		}

		// Advance by this glyph's proportional width + inter-character gap
		x += (fontwidth[ch] + CHAR_GAP) * scale;
	}

	lastx = (int)x;
	lastscale = scale;
}

// -----------------------------------------------------------------------------
// Print (Standard)
// Rotates around the STARTING (X,Y) position.
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// PrintCentered
// Rotates around the GEOMETRIC CENTER of the text line.
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// DrawGlyph
// Draws a single glyph centered exactly at (x,y), rotated around that center.
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// GetStringPitch
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// InitGL
// -----------------------------------------------------------------------------
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
}

// -----------------------------------------------------------------------------
// SetProjection
// -----------------------------------------------------------------------------
void VectorFont::SetProjection(const aae::math::mat4& mvp)
{
	proj = mvp;
}

// -----------------------------------------------------------------------------
// InitFontData
// -----------------------------------------------------------------------------
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