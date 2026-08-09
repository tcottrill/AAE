#define NOMINMAX
#include "emu_vector_draw.h"
#include "vector_draw_gl.h" // txdata, modulate_color, draw_textured_shots
#include "colordefs.h"
#include "config.h"       // config.gain / shots_textured / fire_point_size
#include "sys_log.h"
#include "sys_gl.h"
#include "opengl_renderer.h"
#include "gl_texturing.h" // For game_tex[0]
#include "gl_shader.h"    // fragTexColor + bind_shader / set_uniform*
#include "vector_draw.h"  // beam_add_line / beam_add_shot / beam_clear
#include "MathUtils.h"    // aae::math::value_ptr
#include <vector>
#include <algorithm>       // std::min / std::max (clip)
#include <cstddef>         // offsetof

#ifdef _MSC_VER
#pragma warning( disable :  4244 )
#endif

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

void set_texture_id(rtex_t* id)
{
    tex = id;
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

static rgb_t cache_tex_color(int intensity, rgb_t col)
{
    rgb_t result = modulate_color(col, intensity, config.gain);

    // Scale only the alpha used for textured shots
    int a = (int)(intensity * g_texShotAlphaScale + 0.5f);
    if (a < 0) a = 0; else if (a > 255) a = 255;

    return (result & 0x00FFFFFF) | (a << 24);
}

static void cache_texpoint(float ex, float ey, float tx, float ty, int intensity, rgb_t col)
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

// VK-chain accessor (vector_draw_gl.h): the VK renderer records texlist with
// its own pipeline instead of draw_textured_shots' GL objects.
const txdata* tex_shot_verts(int* count)
{
    if (count)
        *count = (int)texlist.size();
    return texlist.empty() ? nullptr : texlist.data();
}

// Core-profile VAO/VBO for the textured shots (pos + uv + packed RGBA, matching
// the txdata layout). Lazily created on first use.
static GLuint s_shotVAO = 0, s_shotVBO = 0;
static void ensure_shot_buffers()
{
    if (s_shotVAO) return;
    glGenVertexArrays(1, &s_shotVAO);
    glGenBuffers(1, &s_shotVBO);
    glBindVertexArray(s_shotVAO);
    glBindBuffer(GL_ARRAY_BUFFER, s_shotVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, sizeof(txdata), (void*)offsetof(txdata, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT,         GL_FALSE, sizeof(txdata), (void*)offsetof(txdata, tx));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(txdata), (void*)offsetof(txdata, color));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Legacy textured-shot pass (textured quads via game_tex[0]). Used by the modern
// beam path when textured shots are selected (config.shots_textured) - e.g.
// Asteroids Deluxe with artwork. Core-profile: VAO/VBO + the texColor shader
// (texture * per-vertex color = the old GL_MODULATE), additive blend, drawn under
// the same projection as the beams (proj).
void draw_textured_shots(const aae::math::mat4& proj)
{
    if (texlist.empty()) return;

    ensure_shot_buffers();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);   // additive

    // Radial edge fade to kill the square halo. Tune these:
    //   kShotFadeInner = radius of the full-bright core (0=center .. 1=edge)
    //   kShotFadeOuter = radius where the halo fully fades out (<=1 keeps it inside
    //                    the quad; lower = tighter/rounder, higher = softer/larger)
    static const float kShotFadeInner = 0.20f;
    static const float kShotFadeOuter = 1.00f;

    bind_shader(fragTexColor);
    set_uniform1i(fragTexColor, "u_texture", 0);
    set_uniform_mat4f(fragTexColor, "uProj", aae::math::value_ptr(proj));
    set_uniform1f(fragTexColor, "uFadeInner", kShotFadeInner);
    set_uniform1f(fragTexColor, "uFadeOuter", kShotFadeOuter);

    glBindVertexArray(s_shotVAO);
    glBindBuffer(GL_ARRAY_BUFFER, s_shotVBO);
    glBufferData(GL_ARRAY_BUFFER, texlist.size() * sizeof(txdata), texlist.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)texlist.size());

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    unbind_shader();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   // restore default
}
