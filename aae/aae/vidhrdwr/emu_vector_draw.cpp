#define NOMINMAX
#include "emu_vector_draw.h"
#include "colordefs.h"
#include "sys_log.h"
#include "glcode.h"
#include "gl_texturing.h" // For game_tex[0]
#include <vector>

#pragma warning( disable :  4244 )

template<typename T>
inline T clip(T val, T minval, T maxval) {
    return std::min(std::max(val, minval), maxval);
}

float xoffset;
float yoffset;
GLuint* tex;

colors vec_colors[256];

std::vector<fpoint> linelist;
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
    if ((col & 0x00FFFFFF) == 0) {return 0; }

    uint16_t a;
    uint16_t r = (col >> 0) & 0xFF;
    uint16_t g = (col >> 8) & 0xFF;
    uint16_t b = (col >> 16) & 0xFF;
    
    if (Machine->gamedrv->video_attributes & VECTOR_USES_COLOR)
       a = intensity & 0xFF;
    else
       a = 0xff;

   r = clip((r & intensity) + gain, 0, 255);
   g = clip((g & intensity) + gain, 0, 255);
   b = clip((b & intensity) + gain, 0, 255);
   
   if (Machine->gamedrv->video_attributes & VECTOR_USES_COLOR)
       a = clip((a & intensity) + gain, 0, 255);
   else
       a = 0xff;
 
   
   r = (r > 255) ? 255 : r;
   g = (g > 255) ? 255 : g;
   b = (b > 255) ? 255 : b;
   a = (a > 255) ? 255 : a;

   return  (a << 24) | ((uint8_t) b << 16) | ((uint8_t) g << 8) | (uint8_t) r;
}

/*
rgb_t modulate_color(rgb_t col, int intensity, int gain)
{
    // Extract channels (assumes colordefs.h defines these)
    uint32_t r0 = RGB_RED(col);
    uint32_t g0 = RGB_GREEN(col);
    uint32_t b0 = RGB_BLUE(col);
    uint32_t a0 = RGB_ALPHA(col);



    // Clamp intensity to 0..255
    uint32_t I = (intensity < 0) ? 0u : (intensity > 255 ? 255u : (uint32_t)intensity);

    auto clamp255 = [](int v) -> uint32_t {
        return (v < 0) ? 0u : (v > 255) ? 255u : (uint32_t)v;
        };

    // Rule: if original > 0 => set to intensity, else 0; then add gain and clamp
    uint32_t r = clamp255(((r0 > 0) ? (int)I : 0) + gain);
    uint32_t g = clamp255(((g0 > 0) ? (int)I : 0) + gain);
    uint32_t b = clamp255(((b0 > 0) ? (int)I : 0) + gain);
    uint32_t a = clamp255(((a0 > 0) ? (int)I : 0) + gain);

    return MAKE_RGBA((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a);
}
*/

rgb_t cache_tex_color(int intensity, rgb_t col)
{
    rgb_t result = modulate_color(col, intensity, config.gain);
    return col;//(result & 0x00FFFFFF) | (intensity << 24);
}

void cache_texpoint(float ex, float ey, float tx, float ty, int intensity, rgb_t col)
{
    texlist.emplace_back(ex - xoffset, ey - yoffset, tx, ty, 0xff7f7f7f);
}

void add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col)
{
    rgb_t temp_col = modulate_color(col, intensity, config.gain);
    rgb_t temp_half_col = 0;

    if (Machine->drv->video_attributes & VECTOR_USES_COLOR)
    {
        temp_half_col = modulate_color(col, intensity, config.gain / 2);
        temp_half_col = (temp_half_col & 0x00FFFFFF) | (0x7F << 24);
    }

    linelist.emplace_back(sx, sy, temp_col, temp_half_col);
    linelist.emplace_back(ex, ey, temp_col, temp_half_col);
}

void add_tex(float ex, float ey, int intensity, rgb_t col)
{
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
    linelist.clear();
}

void draw_all()
{
    if (Machine->gamedrv->video_attributes & VECTOR_USES_COLOR)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDisable(GL_TEXTURE_2D);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(fpoint), &linelist[0].color);
    glVertexPointer(2, GL_FLOAT, sizeof(fpoint), &linelist[0].x);
    glDrawArrays(GL_LINES, 0, (GLsizei)linelist.size());

    if (Machine->drv->video_attributes & VECTOR_USES_COLOR)
    {
        glColorPointer(3, GL_UNSIGNED_BYTE, sizeof(fpoint), &linelist[0].colorshalf);
    }
    glDrawArrays(GL_POINTS, 0, (GLsizei)linelist.size());

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    if (!texlist.empty())
    {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, *tex);
        glBlendFunc(GL_ONE, GL_ONE);
       // glBlendFunc(GL_SRC_ALPHA, GL_ONE);
       //glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
       // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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
}
