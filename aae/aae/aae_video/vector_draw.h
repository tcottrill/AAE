// -----------------------------------------------------------------------------
// vector_draw.h - Modern OpenGL 4.3+ Vector Line/Point Rendering System
// Self-contained replacement for emu_vector_draw
// -----------------------------------------------------------------------------

#pragma once
#ifndef VECTOR_DRAW_H
#define VECTOR_DRAW_H

#include <glm/glm.hpp>
#include <vector>
#include <GL/glew.h>

enum BlendMode {
    BLEND_STANDARD,
    BLEND_ADDITIVE
};

struct VectorConfig {
    float fire_point_size = 4.0f;
    BlendMode blend_mode = BLEND_STANDARD;
    GLuint fire_texture = 0;
};

struct VecLine {
    glm::vec2 p0, p1;
    float thickness;
    glm::vec4 color;
};

struct VecPoint {
    glm::vec2 pos;
    float size;
    glm::vec4 color;
};

void vector_draw_init(const VectorConfig& config);
void vector_draw_shutdown();
//vector_add_junction_points(15.0f, 2.0f); // 15° minimum angle, 2px min segment length
void vector_add_line(glm::vec2 p0, glm::vec2 p1, float thickness, glm::vec4 color);
void vector_add_point(glm::vec2 pos, float size, glm::vec4 color);
void vector_add_fire(glm::vec2 pos, glm::vec4 color); // uses config.fire_point_size
void vector_add_junction_points(float angleThresholdDegrees = 10.0f, float minSegmentLength = 1.0f);
void vector_draw_all(const glm::mat4& projection);
void vector_clear();

#endif // VECTOR_DRAW_H
