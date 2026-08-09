// ----
// This file is part of the AAE (Another Arcade Emulator) project.
// This Code is copyright (C) 2025/2026 Tim Cottrill and released
// under the GNU GPL v3 or later. See the accompanying source files for full
// license details.
// ----

// ====
// vector_fonts.h
// ====
#ifndef VECTOR_FONTS_H
#define VECTOR_FONTS_H

#include <cstdint>
#include <cstddef>   // offsetof
#include <vector>

#include "opengl_renderer.h"
#include "render_types.h"
#include "colordefs.h"
#include "MathUtils.h" // aae::math::{vec2, mat4, ortho, value_ptr}

// ----
// Description
// VectorFont is a singleton class for rendering vector-based bitmap fonts
// using OpenGL 3.3 shaders. It supports batched text rendering with per-vertex
// color, deferred drawing, and 360-degree rotation.
//
// Integration:
// - Uses aae::math for vector/matrix operations.
// ----

// ----
// VFVertex
// Vertex structure for VectorFont.
// Includes position (vec2), rotation origin (vec2), angle, and packed color.
// ----
struct VFVertex
{
    aae::math::vec2 pos;     // Current vertex position (unrotated world coords)
    aae::math::vec2 origin;  // Origin of rotation for this string
    float angle;             // Rotation angle in degrees
    rgb_t color;             // Packed RGBA
};

// ----
// VectorFont Singleton Class
// ----
class VectorFont
{
public:
    // Singleton Access
    static VectorFont& Instance();

    // Initialization / Cleanup
    void Initialize(int width, int height);
    void Resize(int width, int height);
    // When true (default), Begin() sets glViewport to screenWidth x screenHeight.
    // When false, Begin() leaves the viewport alone so the caller controls it.
    // Use SetOverrideViewport(false) when rendering overlays onto the backbuffer
    // at full window size while keeping the 1024x768 coordinate space.
    void SetOverrideViewport(bool enable);
    // Rendering control
    void Begin();
    void End();
    void DrawQuad(float x, float y, float width, float height, rgb_t color);
    // Queue one line segment in VF screen space (1024x768). Rides the same
    // drawVerts batch as glyph strokes, so it renders through Begin()/End()
    // on both the GL and Vulkan chains at the font stroke width.
    void DrawLine(float x0, float y0, float x1, float y1, rgb_t color);

    // Text rendering. clipL/clipR bound the visible x range in UNROTATED
    // string-local space (defaults = no clipping); strokes are clipped
    // geometrically before rotation, so a clipped window rotates with the text.
    void DrawTextInternal(float x, float y, const aae::math::vec2& rotationOrigin, rgb_t color, float scale, float angle, const char* text,
                          float clipL = -1.0e9f, float clipR = 1.0e9f);

    // 'angle' parameter (in degrees)
    void Print(float x, int y, rgb_t color, float scale, float angle, const char* fmt, ...);

    // Helper for legacy/simple calls (defaults angle to 0.0f)
    void Print(float x, int y, rgb_t color, float scale, const char* fmt, ...);

    void PrintCentered(int y, rgb_t color, float scale, const char* str);
    // Overload with angle support
    void PrintCentered(int y, rgb_t color, float scale, float angle, const char* str);

    // Marquee text: draws exactly like Print/PrintCentered when the string
    // fits in fieldWidth (logical units); when it doesn't, the text scrolls
    // horizontally at constant speed within the field (dwell at each end),
    // clipped to the field box. Stateless -- the scroll phase is derived from
    // a shared wall clock and the string's overflow, so no per-string state
    // is kept. Rotation is fully supported (clipping happens pre-rotation).
    void PrintMarquee(float x, int y, float fieldWidth, rgb_t color, float scale, float angle, const char* fmt, ...);
    void PrintMarquee(float x, int y, float fieldWidth, rgb_t color, float scale, const char* fmt, ...);
    void PrintMarqueeCentered(int y, float fieldWidth, rgb_t color, float scale, float angle, const char* str);
    void PrintMarqueeCentered(int y, float fieldWidth, rgb_t color, float scale, const char* str);
    // Static variant: same field clipping as the marquee but never scrolls --
    // an overlong string shows its beginning, cut at the field edge. For rows
    // that shouldn't animate (e.g. non-selected GUI list entries).
    void PrintClippedCentered(int y, float fieldWidth, rgb_t color, float scale, const char* str);
    float GetStringPitch(const char* str, float scale, int set);
    int GetLastStringLength() const { return lastx + static_cast<int>(3.5f * lastscale); }
    // Draw a specific single character/glyph centered at (x,y)
 // Useful for game objects (ships, asteroids) mapped to font slots.
    void DrawGlyph(float x, float y, int glyph, rgb_t color, float scale, float angle);


private:
    // Singleton enforcement
    VectorFont();
    ~VectorFont();

    VectorFont(const VectorFont&) = delete;
    VectorFont& operator=(const VectorFont&) = delete;

    // Internal helpers
    void InitGL();
    void SetProjection(const aae::math::mat4& mvp);
    void InitFontData();

private:
    // OpenGL state
    rprog_t vfProgram = 0;
    rvao_t vfVAO = 0;
    rbuf_t vfVBO = 0;
    rvao_t quadVAO = 0;
    rbuf_t quadVBO = 0;

    std::int32_t attrPos = -1;
    std::int32_t attrColor = -1;
    std::int32_t attrOrigin = -1;
    std::int32_t attrAngle = -1;
    std::int32_t uniMVP = -1;

    aae::math::mat4 proj; // Using aae::math::mat4
    int screenWidth = 0;
    int screenHeight = 0;
    bool overrideViewport = true;

    // Accumulated vertex data
    std::vector<VFVertex> drawVerts;

    // State tracking
    int lastx = 0;
    float lastscale = 1.0f;

    int fstart[257]{};
    float fontwidth[256]{};   // Proportional width of each glyph (maxX - minX)
    float fontoffset[256]{};  // Left bearing offset (minX) to shift glyph flush left
};

// Short macro alias
#define VF VectorFont::Instance()

#endif // VECTOR_FONTS_H