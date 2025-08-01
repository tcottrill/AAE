#ifndef VECTOR_FONTS_H
#define VECTOR_FONTS_H

#include <cstdint>
#include <vector>
#include "glcode.h" 
#include "colordefs.h"

// -----------------------------------------------------------------------------
// Description
// VectorFont is a singleton class for rendering vector-based bitmap fonts
// using OpenGL 2.1 shaders. It supports batched text rendering with per-vertex
// color and deferred drawing. Text data is built up using Print() calls and
// rendered in a single draw call at End(). Supports dynamic projection updates
// via Resize() without reinitializing OpenGL resources.
//
// Initialization
// - Call VF.Initialize(width, height) once after an OpenGL context is ready.
// - Use VF.Resize(newWidth, newHeight) when window size changes.
//
// Rendering
// - Call VF.Begin() before issuing any Print() calls.
// - Call VF.Print(x, y, color, scale, "Text") multiple times to queue text.
// - Call VF.End() once to draw all queued text.
//
// -----------------------------------------------------------------------------
// Minimal Usage Example
// -----------------------------------------------------------------------------
//
// // Create and initialize the singleton
// VF.Initialize(1024, 768);
//
// // In your render loop
// VF.Begin();
// VF.Print(100, 200, 0xFF0000FF, 1.0f, "Hello World!");
// VF.PrintCentered(300, MAKE_RGBA(128,0,255,255), 1.5f, "Centered Text");
// VF.End();
//
// // On window resize
// VF.Resize(newWidth, newHeight);
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// VFVertex
// Vertex structure for VectorFont, using 2 floats for position and a packed
// rgb_t for color (4 bytes). Matches OpenGL vertex attribute usage with
// GL_UNSIGNED_BYTE for colors.
// -----------------------------------------------------------------------------
struct VFVertex {
    float x, y;     // position
    rgb_t color;    // packed RGBA
};

// -----------------------------------------------------------------------------
// Simple 4x4 Matrix Wrapper
// -----------------------------------------------------------------------------
struct Mat4 {
    float m[16];
    operator const float* () const { return m; }
};

// Orthographic matrix generator
Mat4 makeOrtho(float left, float right, float bottom, float top, float nearVal, float farVal);

// -----------------------------------------------------------------------------
// VectorFont Singleton Class
// -----------------------------------------------------------------------------
class VectorFont {
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
    void Print(float x, int y, rgb_t color, float scale, const char* fmt, ...);
    void PrintCentered(int y, rgb_t color, float scale, const char* str);
    float GetStringPitch(const char* str, float scale, int set);
    int GetLastStringLength() const { return lastx + static_cast<int>(3.5f * lastscale); }

private:
    // Singleton enforcement
    VectorFont();
    ~VectorFont();
    VectorFont(const VectorFont&) = delete;
    VectorFont& operator=(const VectorFont&) = delete;

    // Internal helpers
    void InitGL();
    void SetProjection(const Mat4& mvp);
    void InitFontData();

    // OpenGL state
    GLuint vfProgram = 0;
    GLuint vfVBO = 0;
    GLint attrPos = -1;
    GLint attrColor = -1;
    GLint uniMVP = -1;
    float proj[16]{};
    int screenWidth = 0;
    int screenHeight = 0;
 
    // Accumulated vertex data (x, y, r, g, b, a)
    std::vector<VFVertex> drawVerts;

    // State tracking
    int lastx = 0;
    float lastscale = 1.0f;
    int fstart[257]{};
};

// Short macro alias
#define VF VectorFont::Instance()

#endif // VECTOR_FONTS_H
