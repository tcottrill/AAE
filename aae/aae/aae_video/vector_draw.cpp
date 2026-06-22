// -----------------------------------------------------------------------------
// vector_draw.cpp - Modern shader-based vector beam renderer.
// Butt-capped, coverage-AA'd instanced beams + round joins + procedural shots.
// Draws into the currently bound FBO (fbo1/img1a) using an explicit uProj; no
// fixed-function state. See docs/superpowers/specs/2026-06-22-vector-beam-renderer-design.md
// -----------------------------------------------------------------------------
#include "vector_draw.h"
#include "shader_util.h"                 // CompileShader / LinkShaderProgram
#include "aae_mame_driver.h"             // Machine, VECTOR_USES_COLOR
#include "config.h"                      // config.linewidth / gain / fire_point_size
#include "emu_vector_draw.h"             // modulate_color
#include <vector>
#include <algorithm>
#include <cstdint>

using namespace aae::math;

static const float AA_PIXELS = 1.2f;     // edge feather in physical pixels (calibration knob)

static std::vector<BeamLine> g_lines;
static std::vector<BeamJoin> g_joins;
static std::vector<BeamShot> g_shots;

static GLuint vaoLine = 0, vboLine = 0;
static GLuint vaoJoin = 0, vboJoin = 0;
static GLuint vaoShot = 0, vboShot = 0;

static GLuint progLine = 0, progJoin = 0, progShot = 0;

static int   g_ssaa = 1;
static float g_uAA  = AA_PIXELS;         // feather in logical units (= AA_PIXELS / ssaa)

// ----------------------------- shaders --------------------------------------
static const char* vsLine = R"GLSL(
#version 330 core
layout(location=0) in vec2  inP0;
layout(location=1) in vec2  inP1;
layout(location=2) in float inHalf;
layout(location=3) in vec4  inColor;
uniform mat4  uProj;
uniform float uAA;
out vec2  vLocal;   // x = longitudinal [0..len], y = perpendicular
out float vLen;
out float vHalf;
out vec4  vColor;
const vec2 kQuad[4] = vec2[](vec2(0,-1), vec2(1,-1), vec2(0,1), vec2(1,1));
void main() {
    vec2  d   = inP1 - inP0;
    float len = length(d);
    vec2  dir = (len > 0.0001) ? d/len : vec2(1.0,0.0);
    vec2  nrm = vec2(-dir.y, dir.x);
    vec2  q   = kQuad[gl_VertexID];
    float along = q.x * (len + 2.0*uAA) - uAA;       // butt cap + feather past ends
    float perp  = q.y * (inHalf + uAA);
    vec2  pos   = inP0 + dir*along + nrm*perp;
    gl_Position = uProj * vec4(pos, 0.0, 1.0);
    vLocal = vec2(along, perp);
    vLen = len; vHalf = inHalf; vColor = inColor;
}
)GLSL";

static const char* fsLine = R"GLSL(
#version 330 core
in vec2  vLocal;
in float vLen;
in float vHalf;
in vec4  vColor;
uniform float uAA;
out vec4 frag;
void main() {
    // Half-coverage exactly at the geometric edge -> width matches GL_LINES.
    float covPerp = clamp((vHalf - abs(vLocal.y))/uAA + 0.5, 0.0, 1.0);
    float covEnd0 = clamp((vLocal.x)/uAA + 0.5, 0.0, 1.0);
    float covEnd1 = clamp((vLen - vLocal.x)/uAA + 0.5, 0.0, 1.0);
    float cov = covPerp * covEnd0 * covEnd1;
    if (cov <= 0.0) discard;
    frag = vec4(vColor.rgb, vColor.a * cov);   // no pow(): linear coverage
}
)GLSL";

// Join + shot programs (used in Phase 2 / Phase 4). Compiled now so the module is
// self-contained; their draws are stubbed until then.
static const char* vsJoin = R"GLSL(
#version 330 core
layout(location=0) in vec2  inCenter;
layout(location=1) in float inHalf;
layout(location=2) in vec4  inColor;
uniform mat4  uProj;
uniform float uAA;
out vec2  vLocal;
out float vHalf;
out vec4  vColor;
const vec2 kQuad[4] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));
void main() {
    vec2 q = kQuad[gl_VertexID];
    vec2 ext = q * (inHalf + uAA);
    gl_Position = uProj * vec4(inCenter + ext, 0.0, 1.0);
    vLocal = ext; vHalf = inHalf; vColor = inColor;
}
)GLSL";

static const char* fsJoin = R"GLSL(
#version 330 core
in vec2  vLocal;
in float vHalf;
in vec4  vColor;
uniform float uAA;
out vec4 frag;
void main() {
    float cov = clamp((vHalf - length(vLocal))/uAA + 0.5, 0.0, 1.0);
    if (cov <= 0.0) discard;
    frag = vec4(vColor.rgb, vColor.a * cov);
}
)GLSL";

static const char* vsShot = R"GLSL(
#version 330 core
layout(location=0) in vec2  inCenter;
layout(location=1) in float inSize;
layout(location=2) in vec4  inColor;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
const vec2 kQuad[4] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));
void main() {
    vec2 q = kQuad[gl_VertexID];
    gl_Position = uProj * vec4(inCenter + q*inSize, 0.0, 1.0);
    vUV = q*0.5 + 0.5;
    vColor = inColor;
}
)GLSL";

static const char* fsShot = R"GLSL(
#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 frag;
uniform float uCorePower;
uniform float uBloomPower;
uniform float uBloomIntensity;
uniform float uOverdrive;
void main() {
    float br = max(vColor.r, max(vColor.g, vColor.b));
    br = max(0.1, br);
    float d  = distance(vUV, vec2(0.5));
    float g  = clamp(1.0 - d*2.0, 0.0, 1.0);
    float core = pow(g, uCorePower);
    float halo = pow(g, uBloomPower) * (uBloomIntensity * (0.5 + br*0.5));
    float ti = core + halo;
    vec3 hot = vColor.rgb * (uOverdrive * br);
    frag = vec4(hot * ti, ti);
}
)GLSL";

// ----------------------------- helpers --------------------------------------
static GLuint linkProg(const char* vs, const char* fs, const char* label) {
    return LinkShaderProgram(CompileShader(GL_VERTEX_SHADER,   vs, label),
                             CompileShader(GL_FRAGMENT_SHADER, fs, label));
}

static void setAA(int ssaa) {
    g_ssaa = (ssaa < 1) ? 1 : ssaa;
    g_uAA  = AA_PIXELS / (float)g_ssaa;
}

// ----------------------------- lifecycle ------------------------------------
void beam_set_ssaa(int ssaa) { setAA(ssaa); }

void beam_init(int ssaa) {
    setAA(ssaa);

    progLine = linkProg(vsLine, fsLine, "beam_line");
    progJoin = linkProg(vsJoin, fsJoin, "beam_join");
    progShot = linkProg(vsShot, fsShot, "beam_shot");

    // Lines: one instance per segment.
    glGenVertexArrays(1, &vaoLine);
    glGenBuffers(1, &vboLine);
    glBindVertexArray(vaoLine);
    glBindBuffer(GL_ARRAY_BUFFER, vboLine);
    glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, sizeof(BeamLine), (void*)offsetof(BeamLine, p0));    glEnableVertexAttribArray(0); glVertexAttribDivisor(0,1);
    glVertexAttribPointer(1, 2, GL_FLOAT,         GL_FALSE, sizeof(BeamLine), (void*)offsetof(BeamLine, p1));    glEnableVertexAttribArray(1); glVertexAttribDivisor(1,1);
    glVertexAttribPointer(2, 1, GL_FLOAT,         GL_FALSE, sizeof(BeamLine), (void*)offsetof(BeamLine, half));  glEnableVertexAttribArray(2); glVertexAttribDivisor(2,1);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(BeamLine), (void*)offsetof(BeamLine, color)); glEnableVertexAttribArray(3); glVertexAttribDivisor(3,1);

    // Joins.
    glGenVertexArrays(1, &vaoJoin);
    glGenBuffers(1, &vboJoin);
    glBindVertexArray(vaoJoin);
    glBindBuffer(GL_ARRAY_BUFFER, vboJoin);
    glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, sizeof(BeamJoin), (void*)offsetof(BeamJoin, center)); glEnableVertexAttribArray(0); glVertexAttribDivisor(0,1);
    glVertexAttribPointer(1, 1, GL_FLOAT,         GL_FALSE, sizeof(BeamJoin), (void*)offsetof(BeamJoin, half));   glEnableVertexAttribArray(1); glVertexAttribDivisor(1,1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(BeamJoin), (void*)offsetof(BeamJoin, color));  glEnableVertexAttribArray(2); glVertexAttribDivisor(2,1);

    // Shots.
    glGenVertexArrays(1, &vaoShot);
    glGenBuffers(1, &vboShot);
    glBindVertexArray(vaoShot);
    glBindBuffer(GL_ARRAY_BUFFER, vboShot);
    glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, sizeof(BeamShot), (void*)offsetof(BeamShot, pos));   glEnableVertexAttribArray(0); glVertexAttribDivisor(0,1);
    glVertexAttribPointer(1, 1, GL_FLOAT,         GL_FALSE, sizeof(BeamShot), (void*)offsetof(BeamShot, size));  glEnableVertexAttribArray(1); glVertexAttribDivisor(1,1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(BeamShot), (void*)offsetof(BeamShot, color)); glEnableVertexAttribArray(2); glVertexAttribDivisor(2,1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void beam_shutdown() {
    GLuint vaos[] = { vaoLine, vaoJoin, vaoShot };
    GLuint vbos[] = { vboLine, vboJoin, vboShot };
    glDeleteVertexArrays(3, vaos);
    glDeleteBuffers(3, vbos);
    glDeleteProgram(progLine);
    glDeleteProgram(progJoin);
    glDeleteProgram(progShot);
    vaoLine = vaoJoin = vaoShot = vboLine = vboJoin = vboShot = 0;
    progLine = progJoin = progShot = 0;
}

// ----------------------------- producer -------------------------------------
void beam_add_line(float sx, float sy, float ex, float ey,
                   int intensity, rgb_t col, bool joinPrev) {
    rgb_t c = modulate_color(col, intensity, config.gain);
    float half = config.linewidth * 0.5f;
    g_lines.push_back({ vec2(sx, sy), vec2(ex, ey), half, c });
    if (joinPrev)
        g_joins.push_back({ vec2(sx, sy), half, c });
}

void beam_add_shot(float ex, float ey, int intensity, rgb_t col) {
    rgb_t c = modulate_color(col, intensity, config.gain);
    g_shots.push_back({ vec2(ex, ey), (float)config.fire_point_size, c });
}

void beam_clear() {
    g_lines.clear();
    g_joins.clear();
    g_shots.clear();
}

// ----------------------------- draw -----------------------------------------
void beam_draw_all(const mat4& proj) {
    if (g_lines.empty() && g_joins.empty() && g_shots.empty()) return;

    const bool additive = (Machine->drv->video_attributes & VECTOR_USES_COLOR) != 0;

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);

    if (!additive) {
        // Black-and-white: painter's sort, darkest first, so brighter beams
        // occlude darker ones under alpha-over.
        std::sort(g_lines.begin(), g_lines.end(),
            [](const BeamLine& a, const BeamLine& b) {
                return (uint32_t)a.color < (uint32_t)b.color; });
        std::sort(g_joins.begin(), g_joins.end(),
            [](const BeamJoin& a, const BeamJoin& b) {
                return (uint32_t)a.color < (uint32_t)b.color; });
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);   // color: additive
    }

    // Lines.
    if (!g_lines.empty()) {
        glUseProgram(progLine);
        glUniformMatrix4fv(glGetUniformLocation(progLine, "uProj"), 1, GL_FALSE, value_ptr(const_cast<mat4&>(proj)));
        glUniform1f(glGetUniformLocation(progLine, "uAA"), g_uAA);
        glBindBuffer(GL_ARRAY_BUFFER, vboLine);
        glBufferData(GL_ARRAY_BUFFER, g_lines.size()*sizeof(BeamLine), g_lines.data(), GL_STREAM_DRAW);
        glBindVertexArray(vaoLine);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)g_lines.size());
    }

    // Joins + shots: implemented in Phase 2 / Phase 4.

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}
