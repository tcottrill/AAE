#pragma once
#ifndef VECTOR_DRAW_H
#define VECTOR_DRAW_H

#include "colordefs.h"     // rgb_t
#include "MathUtils.h"     // aae::math::vec2 / mat4
#include <vector>

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
void beam_set_ssaa(int ssaa);          // sets the supersample factor (affects AA feather)

// Mirrors add_line / add_tex exactly. Join connectivity is inferred internally by
// endpoint matching, so EVERY producer (vector_update, the DVG sim, cchasm) is
// covered by routing through these from add_line()/add_tex().
void beam_add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col);
void beam_add_shot(float ex, float ey, int intensity, rgb_t col);

void beam_clear();
void beam_draw_all(const aae::math::mat4& proj);

// ---- Shared AA-line path (also used by the vector-font renderer) -----------
// Draw a caller-owned batch of segments / caps with the beam's coverage-AA line
// and round-disc shaders under an explicit projection. Resources are created
// lazily, so these work even when beam_init() was never called (raster games and
// the front-end GUI, where only the fonts need the line shader). 'additive'
// selects blend: false = alpha-over (B/W text/menus), true = additive (color).
// 'aaFeather' is the edge feather in the projection's logical units.
void beam_draw_lines(const aae::math::mat4& proj, const BeamLine* lines, int count,
                     float aaFeather, bool additive);
void beam_draw_caps (const aae::math::mat4& proj, const BeamJoin* caps,  int count,
                     float aaFeather, bool additive);

// Build round end-caps / corner joins for a batch of segments via endpoint
// coincidence: a vertex touched by a single segment is a true termination
// (radius = half * endcapMul); two or more is a corner (half * cornerMul). This
// is the same connectivity logic the beam uses internally, exposed so the fonts
// get identical ties without duplicating it.
void beam_build_caps(const BeamLine* lines, int count, float endcapMul, float cornerMul,
                     std::vector<BeamJoin>& out);

// ---- Backend-agnostic batch access -----------------------------------------
// The current frame's segment / shot batches, built by beam_add_line /
// beam_add_shot. The Vulkan backend (vector_draw_vk) consumes these; the round
// joins are rebuilt from the lines via beam_build_caps(), so no other internal
// state is exposed. Pure CPU accessors - no GL, no behavior change.
const std::vector<BeamLine>& beam_get_lines();
const std::vector<BeamShot>& beam_get_shots();

#endif // VECTOR_DRAW_H
