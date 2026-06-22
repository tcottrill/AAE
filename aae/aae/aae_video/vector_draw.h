#pragma once
#ifndef VECTOR_DRAW_H
#define VECTOR_DRAW_H

#include "sys_gl.h"
#include "colordefs.h"     // rgb_t
#include "MathUtils.h"     // aae::math::vec2 / mat4

// Per-segment beam (butt-capped, coverage-AA rectangle).
struct BeamLine {
    aae::math::vec2 p0;
    aae::math::vec2 p1;
    float           half;   // half-width, logical units
    rgb_t           color;  // packed RGBA (a = 0xff); coverage supplies edge alpha
};

// Round join disc placed at an interior shared vertex (radius == beam half-width).
struct BeamJoin {
    aae::math::vec2 center;
    float           half;
    rgb_t           color;
};

// Procedural shot/fire point (radial core + halo in the shader).
struct BeamShot {
    aae::math::vec2 pos;
    float           size;
    rgb_t           color;
};

// ssaa = supersample factor of the bound render target (1 in Phase 1, 2 in Phase 6).
void beam_init(int ssaa);
void beam_shutdown();
void beam_set_ssaa(int ssaa);   // updates the AA feather if the factor changes

// Mirrors the old add_line/add_tex signatures so the producer change is minimal.
// joinPrev == true draws a round join at p0 (the shared vertex with the previous
// connected segment).
void beam_add_line(float sx, float sy, float ex, float ey,
                   int intensity, rgb_t col, bool joinPrev);
void beam_add_shot(float ex, float ey, int intensity, rgb_t col);

void beam_clear();
void beam_draw_all(const aae::math::mat4& proj);

#endif // VECTOR_DRAW_H
