#define NOMINMAX
#include "emu_vector_draw.h"
#include "colordefs.h"
#include "sys_log.h"
#include "opengl_renderer.h"
#include "gl_texturing.h" // For game_tex[0]
#include "vector_draw.h"  // beam_add_line / beam_add_shot / beam_clear
#include <vector>
#include <algorithm>       // std::min / std::max (clip)

#pragma warning( disable :  4244 )

template<typename T>
inline T clip(T val, T minval, T maxval) {
    return std::min(std::max(val, minval), maxval);
}

float xoffset;
float yoffset;
GLuint* tex;

// Scales only the textured shot alpha; 1.0 = current, lower = dimmer
static float g_texShotAlphaScale = 1.0f;
void set_tex_shot_alpha_scale(float s) { g_texShotAlphaScale = (s < 0.f) ? 0.f : (s > 1.f ? 1.f : s); }

colors vec_colors[256];

// Textured-shot geometry (Asteroids/Deluxe shots). The modern beam renderer owns
// the lines/points; this list feeds the legacy textured-shot pass selected by
// config.shots_textured.
std::vector<txdata> texlist;

void set_texture_id(GLuint* id)
{
    tex = id;
}

void set_blendmode(GLenum sfactor, GLenum dfactor)
{
    glBlendFunc(sfactor, dfactor);
}

rgb_t modulate_color(rgb_t col, int intensity, int gain)
{
    if ((col & 0x00FFFFFF) == 0) { return 0; }

    uint8_t r = (col >> 0) & 0xFF;
    uint8_t g = (col >> 8) & 0xFF;
    uint8_t b = (col >> 16) & 0xFF;
    uint8_t a = 0xff;// (col >> 24) & 0xFF;

    r = clip((r & intensity) + gain, 0, 255);
    g = clip((g & intensity) + gain, 0, 255);
    b = clip((b & intensity) + gain, 0, 255);

    return  (a << 24) | (b << 16) | (g << 8) | r;
}

rgb_t cache_tex_color(int intensity, rgb_t col)
{
    rgb_t result = modulate_color(col, intensity, config.gain);

    // Scale only the alpha used for textured shots
    int a = (int)(intensity * g_texShotAlphaScale + 0.5f);
    if (a < 0) a = 0; else if (a > 255) a = 255;

    return (result & 0x00FFFFFF) | (a << 24);
}

void cache_texpoint(float ex, float ey, float tx, float ty, int intensity, rgb_t col)
{
    texlist.emplace_back(ex - xoffset, ey - yoffset, tx, ty, cache_tex_color(intensity, col));
}

void add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col)
{
    // The modern beam renderer is the only vector engine.
    beam_add_line(sx, sy, ex, ey, intensity, col);
}

void add_tex(float ex, float ey, int intensity, rgb_t col)
{
    // Procedural shots by default; legacy textured shots when selected.
    if (!config.shots_textured) { beam_add_shot(ex, ey, intensity, col); return; }

    float xoff = config.fire_point_size;
    float yoff = config.fire_point_size;

    float x0 = ex - xoff;
    float y0 = ey - yoff;
    float x1 = ex + xoff;
    float y1 = ey + yoff;

    // First triangle
    cache_texpoint(x0, y0, 0.0f, 0.0f, intensity, col);
    cache_texpoint(x1, y0, 1.0f, 0.0f, intensity, col);
    cache_texpoint(x1, y1, 1.0f, 1.0f, intensity, col);

    // Second triangle
    cache_texpoint(x1, y1, 1.0f, 1.0f, intensity, col);
    cache_texpoint(x0, y1, 0.0f, 1.0f, intensity, col);
    cache_texpoint(x0, y0, 0.0f, 0.0f, intensity, col);
}

void cache_clear()
{
    texlist.clear();
    beam_clear();          // clear the modern beam lists on the same frame boundary
}

// Legacy textured-shot pass (fixed-function quads via game_tex[0]). Used by the
// modern beam path when textured shots are selected (config.shots_textured) -
// e.g. Asteroids Deluxe with artwork.
void draw_textured_shots()
{
    if (texlist.empty()) return;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(txdata), &texlist[0].x);

    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof(txdata), &texlist[0].tx);

    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(txdata), &texlist[0].color);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)texlist.size());

    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    glDisable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
