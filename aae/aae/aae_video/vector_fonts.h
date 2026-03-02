// -----------------------------------------------------------------------------
// This file is part of the AAE (Another Arcade Emulator) project.
// This Code is copyright (C) 2025/2026 Tim Cottrill and released
// under the GNU GPL v3 or later. See the accompanying source files for full
// license details.
// -----------------------------------------------------------------------------

// ============================================================================
// vector_fonts.h
// ============================================================================
#ifndef VECTOR_FONTS_H
#define VECTOR_FONTS_H

#include <cstdint>
#include <cstddef>   // offsetof
#include <vector>

#include "glcode.h"
#include "colordefs.h"
#include "MathUtils.h" // aae::math::{vec2, mat4, ortho, value_ptr}

// -----------------------------------------------------------------------------
// Description
// VectorFont is a singleton class for rendering vector-based bitmap fonts
// using OpenGL 2.1 shaders. It supports batched text rendering with per-vertex
// color, deferred drawing, and 360-degree rotation.
//
// Integration:
// - Uses aae::math for vector/matrix operations.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// VFVertex
// Vertex structure for VectorFont.
// Includes position (vec2), rotation origin (vec2), angle, and packed color.
// -----------------------------------------------------------------------------
struct VFVertex
{
    aae::math::vec2 pos;     // Current vertex position (unrotated world coords)
    aae::math::vec2 origin;  // Origin of rotation for this string
    float angle;             // Rotation angle in degrees
    rgb_t color;             // Packed RGBA
};

// -----------------------------------------------------------------------------
// VectorFont Singleton Class
// -----------------------------------------------------------------------------
class VectorFont
{
public:
    // Singleton Access
    static VectorFont& Instance();

    // Initialization / Cleanup
    void Initialize(int width, int height);
    void Resize(int width, int height);

    // Rendering control
    void Begin();
    void End();
    void DrawQuad(float x, float y, float width, float height, rgb_t color);

    // Text rendering
    void DrawTextInternal(float x, float y, const aae::math::vec2& rotationOrigin, rgb_t color, float scale, float angle, const char* text);

    // 'angle' parameter (in degrees)
    void Print(float x, int y, rgb_t color, float scale, float angle, const char* fmt, ...);

    // Helper for legacy/simple calls (defaults angle to 0.0f)
    void Print(float x, int y, rgb_t color, float scale, const char* fmt, ...);

    void PrintCentered(int y, rgb_t color, float scale, const char* str);
    // Overload with angle support
    void PrintCentered(int y, rgb_t color, float scale, float angle, const char* str);
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
    GLuint vfProgram = 0;
    GLuint vfVBO = 0;

    GLint attrPos = -1;
    GLint attrColor = -1;
    GLint attrOrigin = -1;
    GLint attrAngle = -1;
    GLint uniMVP = -1;

    aae::math::mat4 proj; // Using aae::math::mat4
    int screenWidth = 0;
    int screenHeight = 0;

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
