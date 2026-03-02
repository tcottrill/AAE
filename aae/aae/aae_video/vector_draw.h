// -----------------------------------------------------------------------------
// vector_draw.h
// -----------------------------------------------------------------------------
#pragma once
#include <vector>
#include <GL/glew.h>
#include "colordefs.h"  // rgb_t, MAKE_RGBA etc.
#include "MathUtils.h"

using namespace aae::math;

enum BlendMode {
    BLEND_STANDARD, // Alpha blending (transparency)
    BLEND_ADDITIVE  // CRT style (light accumulation)
};

struct VectorConfig {
   
    BlendMode blend_mode = BLEND_ADDITIVE; // Default to additive for vectors
    GLuint fire_texture = 0;

    // Global width multiplier (default 1.0)
    float line_width_scale = 1.0f;

    // --- SHOT TUNING ---
    float fire_point_size = 3.5f;      // GEOMETRY SIZE (Smaller = 3.5, Standard = 4.0)

    // Shader Uniforms
    float shot_core_power = 6.0f;      // Higher = Sharper/Smaller hot center
    float shot_bloom_power = 2.5f;     // Higher = Smaller Halo (was 2.0)
    float shot_bloom_intensity = 0.3f; // Lower = Dimmer Halo (was 0.4)
    float shot_overdrive = 3.0f;       // Brightness Multiplier (was 4.0)
};

struct VecLine {
    vec2 p0;
    vec2 p1;
    float thickness;
    rgb_t color;      // packed RGBA
};

struct VecPoint {
    vec2 pos;
    float size;
    rgb_t color;
};

void vector_draw_init(const VectorConfig& config);
void vector_draw_shutdown();

// Core drawing functions
void vector_add_line(vec2 p0, vec2 p1, float thickness, rgb_t rgba);
void vector_add_point(vec2 pos, float size, rgb_t rgba);
void vector_add_fire(vec2 pos, rgb_t rgba);

// Render everything
void vector_draw_all(const mat4& projection);
void vector_clear();