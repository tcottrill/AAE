#include "vector_fonts.h"
#include "shader_util.h"
#include "colordefs.h"   // <-- required
#include <vector>
#include <cstdio>
#include <cstring>

constexpr float FONT_SPACING = 9.5;
constexpr int    EOC = 256;

// -----------------------------------------------------------------------------
// Inline Shaders for OpenGL 2.1
// -----------------------------------------------------------------------------
static const char* vfVertexShader = R"glsl(
#version 120
attribute vec2 aPos;
attribute vec4 aColor;
varying vec4 vColor;
uniform mat4 uMVP;
void main() {
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
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
// makeOrtho
// Creates an orthographic projection matrix for rendering in 2D screen space.
// The resulting matrix maps the specified left/right, bottom/top, and near/far
// clipping planes into normalized device coordinates (-1 to 1) as required by
// OpenGL. Returned as a Mat4 in column-major order.
//
// Parameters:
//   left     - Left edge of the view volume.
//   right    - Right edge of the view volume.
//   bottom   - Bottom edge of the view volume.
//   top      - Top edge of the view volume.
//   nearVal  - Distance to the near clipping plane.
//   farVal   - Distance to the far clipping plane.
//
// Returns:
//   Mat4 containing the computed orthographic projection matrix.
// -----------------------------------------------------------------------------
Mat4 makeOrtho(float left, float right, float bottom, float top, float nearVal, float farVal) {
	Mat4 mat{};
	float rl = right - left;
	float tb = top - bottom;
	float fn = farVal - nearVal;

	mat.m[0] = 2.0f / rl; mat.m[4] = 0.0f;       mat.m[8] = 0.0f;        mat.m[12] = -(right + left) / rl;
	mat.m[1] = 0.0f;      mat.m[5] = 2.0f / tb;  mat.m[9] = 0.0f;        mat.m[13] = -(top + bottom) / tb;
	mat.m[2] = 0.0f;      mat.m[6] = 0.0f;       mat.m[10] = -2.0f / fn;  mat.m[14] = -(farVal + nearVal) / fn;
	mat.m[3] = 0.0f;      mat.m[7] = 0.0f;       mat.m[11] = 0.0f;        mat.m[15] = 1.0f;

	return mat;
}

// -----------------------------------------------------------------------------
// Full Embedded Font Data
// -----------------------------------------------------------------------------

static float fontdata[] = {
	32, EOC,
	33, 3.5, 2, 3.5, 6, 3.5, 0, 3.5, 1, EOC,
	37, 0, 0, 7, 6, 1, 6, 1, 5, 6, 0, 6, 1, EOC,
	39, 3.5, 6, 3.5, 5, EOC,
	40, 2, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 5, 0, 5, 1, 6, 1, 6, 2, 6, EOC,
	41, 5, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 5, 7, 5, 6, 6, 6, 6, 5, 6, EOC,
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
	60, 0, 3, 7, 0, 7, 0, 7, 7, 7, 7, 0, 3, EOC,
	62, 0, 0, 7, 3, 7, 3, 0, 7, 0, 7, 0, 0, EOC,
	63, 0, 6, 7, 6, 7, 6, 7, 3, 7, 3, 2, 3, 2, 3, 2, 2, 2, 0, 2, 1, EOC,
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
	93, 5, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 5, 7, 5, 6, 6, 6, 6, 5, 6, EOC,
	95, 1, 0, 6, 0, EOC,
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
	125, 5, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 5, 7, 5, 6, 6, 6, 6, 5, 6, EOC,
	-5, -5
};

// -----------------------------------------------------------------------------
// Singleton Access
// -----------------------------------------------------------------------------
VectorFont& VectorFont::Instance() {
	static VectorFont instance;
	return instance;
}

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------
VectorFont::VectorFont()
	: vfProgram(0), vfVBO(0), attrPos(-1), attrColor(-1), uniMVP(-1),
	lastx(0), lastscale(1.0f) {
	for (int i = 0; i < 16; i++) proj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
	InitFontData();
}

VectorFont::~VectorFont() {
	if (vfVBO) { glDeleteBuffers(1, &vfVBO); vfVBO = 0; }
	if (vfProgram) { glDeleteProgram(vfProgram); vfProgram = 0; }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Initialize
// Sets up the VectorFont system by compiling shaders, creating buffers, and
// setting the initial orthographic projection matrix based on the given
// screen width and height. Must be called once after OpenGL context creation.
//
// Parameters:
//   width   - Screen width in pixels.
//   height  - Screen height in pixels.
// -----------------------------------------------------------------------------
void VectorFont::Initialize(int width, int height) {
	InitGL();
	screenWidth = width;
	screenHeight = height;
	Mat4 ortho = makeOrtho(0.0f, (float)width, 0.0f, (float)height, -1.0f, 1.0f);
	SetProjection(ortho);
}
// -----------------------------------------------------------------------------
// Resize
// Updates the internal orthographic projection matrix to match a new window
// size. Does not reinitialize OpenGL resources. Call this when the window is
// resized to keep text aligned with the new dimensions.
//
// Parameters:
//   width   - New screen width in pixels.
//   height  - New screen height in pixels.
// -----------------------------------------------------------------------------
void VectorFont::Resize(int width, int height) {
	screenWidth = width;
	screenHeight = height;

	// Update projection matrix
	Mat4 ortho = makeOrtho(0.0f, (float)width, 0.0f, (float)height, -1.0f, 1.0f);
	SetProjection(ortho);

	// Ensure OpenGL viewport matches the new dimensions
	glViewport(0, 0, screenWidth, screenHeight);
}

// -----------------------------------------------------------------------------
// Begin
// Prepares the VectorFont renderer for batched text drawing. Binds the font
// shader program, uploads the projection matrix, and enables blending for
// alpha transparency. Must be called before any Print() calls each frame.
// -----------------------------------------------------------------------------
void VectorFont::Begin() {
	glViewport(0, 0, screenWidth, screenHeight);   // <-- sets the viewport
	glUseProgram(vfProgram);
	glUniformMatrix4fv(uniMVP, 1, GL_FALSE, proj);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// -----------------------------------------------------------------------------
// End
// Finalizes the VectorFont rendering process by uploading accumulated vertex
// data to the GPU and issuing the draw call. All text queued with Print() is
// rendered during this call. Automatically unbinds the shader and VBO after
// drawing and clears the vertex buffer for the next frame.
// -----------------------------------------------------------------------------
void VectorFont::End() {
	if (drawVerts.empty()) {
		glUseProgram(0);
		return;
	}

	glBindBuffer(GL_ARRAY_BUFFER, vfVBO);
	glBufferData(GL_ARRAY_BUFFER, drawVerts.size() * sizeof(VFVertex), drawVerts.data(), GL_DYNAMIC_DRAW);

	// Position attribute: 2 floats at start
	glEnableVertexAttribArray(attrPos);
	glVertexAttribPointer(attrPos, 2, GL_FLOAT, GL_FALSE, sizeof(VFVertex), (void*)0);

	// Color attribute: 4 unsigned bytes, normalized, offset after x,y
	glEnableVertexAttribArray(attrColor);
	glVertexAttribPointer(attrColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VFVertex), (void*)offsetof(VFVertex, color));

	glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(drawVerts.size()));
	glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(drawVerts.size()));

	glDisableVertexAttribArray(attrPos);
	glDisableVertexAttribArray(attrColor);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);

	drawVerts.clear();
}
// -----------------------------------------------------------------------------
// DrawQuad
// Uses the active VectorFont shader (set in Begin()) to render a solid quad.
// -----------------------------------------------------------------------------
void VectorFont::DrawQuad(float x, float y, float width, float height, rgb_t color)
{
	// Calculate quad corners
	float minx = x - (width * 0.5f);
	float miny = y - (height * 0.5f);
	float maxx = x + (width * 0.5f);
	float maxy = y + (height * 0.5f);

	// Normalize color
	float r = RGB_RED(color) / 255.0f;
	float g = RGB_GREEN(color) / 255.0f;
	float b = RGB_BLUE(color) / 255.0f;
	float a = RGB_ALPHA(color) / 255.0f;

	// Interleave position(2) + color(4)
	float verts[] = {
		minx, miny, r, g, b, a,
		maxx, miny, r, g, b, a,
		maxx, maxy, r, g, b, a,

		minx, miny, r, g, b, a,
		maxx, maxy, r, g, b, a,
		minx, maxy, r, g, b, a
	};

	// Draw using the same shader attributes as text
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(attrPos);
	glVertexAttribPointer(attrPos, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), verts);

	glEnableVertexAttribArray(attrColor);
	glVertexAttribPointer(attrColor, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), verts + 2);

	glDrawArrays(GL_TRIANGLES, 0, 6);

	glDisableVertexAttribArray(attrPos);
	glDisableVertexAttribArray(attrColor);
}

// -----------------------------------------------------------------------------
// Print
// Queues a formatted text string for rendering at the specified (x, y)
// position using the provided color and scaling factor. Supports printf-style
// formatting for dynamic text content. The text is built into the vertex
// buffer and rendered when End() is called.
//
// Parameters:
//   x      - Horizontal position (in screen coordinates).
//   y      - Vertical position (in screen coordinates).
//   color  - Text color (rgb_t with RGBA support).
//   scale  - Scaling factor for text size.
//   fmt    - printf-style format string followed by optional arguments.
//
// Notes:
//   - Multiple Print() calls may be issued between Begin() and End().
//   - Alpha from color controls blending if enabled.
// -----------------------------------------------------------------------------
void VectorFont::Print(float x, int y, rgb_t color, float scale, const char* fmt, ...) {
	if (!fmt) {
		LOG_ERROR("ERROR: NULL String passed to VectorFont::Print");
		return;
	}

	char text[EOC] = "";
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);

	// If the resulting string is empty, skip rendering
	if (text[0] == '\0') {
		return;
	}

	for (int i = 0; text[i]; i++) {
		int idx = fstart[(unsigned char)text[i]] + 1;
		int bidx = idx + 1;
		while (fontdata[idx] != EOC) {
			float x0 = fontdata[idx] * scale + x;
			float y0 = fontdata[bidx] * scale + y;
			float x1 = fontdata[idx + 2] * scale + x;
			float y1 = fontdata[bidx + 2] * scale + y;

			VFVertex v1 = { x0, y0, color };
			VFVertex v2 = { x1, y1, color };

			drawVerts.push_back(v1);
			drawVerts.push_back(v2);

			idx += 4;
			bidx += 4;
		}
		x += FONT_SPACING * scale;
	}

	lastx = static_cast<int>(x);
	lastscale = scale;
}

// -----------------------------------------------------------------------------
// PrintCentered
// Renders a text string horizontally centered on the screen at the given Y
// position. The X position is calculated automatically based on the string
// length and scaling factor.
//
// Parameters:
//   y      - Vertical position (in screen coordinates).
//   color  - Text color (rgb_t with RGBA support).
//   scale  - Scaling factor for text size.
//   str    - Null-terminated string to render.
// -----------------------------------------------------------------------------
void VectorFont::PrintCentered(int y, rgb_t color, float scale, const char* str) {
	int len = static_cast<int>(strnlen(str, 255));
	float total = len * (FONT_SPACING * scale);
	float x = (1024.0f / 2.0f) - (total / 2.0f);
	Print(x, y, color, scale, "%s", str);
}

// -----------------------------------------------------------------------------
// GetStringPitch
// Calculates the horizontal pixel width (pitch) of a string when rendered
// at the specified scale. Useful for manual alignment or layout calculations.
//
// Parameters:
//   str    - Null-terminated string to measure.
//   scale  - Scaling factor for text size.
//   set    - Unused parameter (reserved for future font sets).
//
// Returns:
//   Width in pixels that the string would occupy.
// -----------------------------------------------------------------------------
float VectorFont::GetStringPitch(const char* str, float scale, int set) {
	(void)set;
	int len = static_cast<int>(strnlen(str, 255));
	return len * (FONT_SPACING * scale);
}

// -----------------------------------------------------------------------------
// Private Helpers
// -----------------------------------------------------------------------------
void VectorFont::InitGL() {
	GLuint vs = CompileShader(GL_VERTEX_SHADER, vfVertexShader, "Vector Font VS");
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, vfFragmentShader, "Vector Font FS");
	vfProgram = LinkShaderProgram(vs, fs);

	attrPos = glGetAttribLocation(vfProgram, "aPos");
	attrColor = glGetAttribLocation(vfProgram, "aColor");
	uniMVP = glGetUniformLocation(vfProgram, "uMVP");

	glGenBuffers(1, &vfVBO);
}

// -----------------------------------------------------------------------------
// SetProjection
// Copies the provided orthographic projection matrix into the internal storage
// used by the VectorFont shader. This matrix is sent to the GPU in Begin().
//
// Parameters:
//   mvp - Reference to a Mat4 containing the projection matrix to use.
// -----------------------------------------------------------------------------
void VectorFont::SetProjection(const Mat4& mvp) {
	memcpy(proj, mvp.m, sizeof(float) * 16);
}

// -----------------------------------------------------------------------------
// InitFontData
// Initializes the font data lookup table `fstart[]` from the embedded fontdata[]
// array. Each character code (ASCII 32–254) is mapped to its starting index
// within fontdata. Values outside this range default to 32 (space).
//
// Behavior:
//   - Sets all entries in fstart[] to 32 initially.
//   - Scans fontdata[] until the sentinel -5 is found.
//   - Assigns the start offset for every character present in the array.
// -----------------------------------------------------------------------------
void VectorFont::InitFontData() {
	int a = 0;
	for (int i = 0; i < 257; i++) fstart[i] = 32;
	while (fontdata[a] > -1) {
		int d = static_cast<int>(fontdata[a]);
		if (d > 31 && d < 255) fstart[d] = a;
		a++;
	}
}