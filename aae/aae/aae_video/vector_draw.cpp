// -----------------------------------------------------------------------------
// vector_draw.cpp - Modern OpenGL 4.3+ Vector Renderer
// -----------------------------------------------------------------------------
#define GLM_ENABLE_EXPERIMENTAL
#include "vector_draw.h"
#include "MathUtils.h"
#include <vector>
#include "shader_util.h" 
#include <algorithm>

using namespace aae::math;

// -----------------------------------------------------------------------------
// Shaders
// -----------------------------------------------------------------------------

// =============================================================================
// 1. INSTANCED LINE SHADER (SDF Capsules)
// =============================================================================
static const char* vs_line = R"GLSL(
#version 430 core

const vec2 kQuadVerts[4] = vec2[](
    vec2(0.0, -1.0), 
    vec2(1.0, -1.0), 
    vec2(0.0,  1.0), 
    vec2(1.0,  1.0) 
);

layout(location = 0) in vec2 inP0;
layout(location = 1) in vec2 inP1;
layout(location = 2) in float inThickness;
layout(location = 3) in vec4 inColor;

uniform mat4 uProj;

out vec2 vLocalPos;
out float vLen;
out float vWidth;
out vec4 vColor;

void main()
{
    vec2 delta = inP1 - inP0;
    float len = length(delta);
    vec2 dir = (len > 0.0001) ? (delta / len) : vec2(1.0, 0.0);
    vec2 norm = vec2(-dir.y, dir.x);

    float width = inThickness * 0.5; 
    float feather = 1.5; 
    float expansion = width + feather;

    vec2 rawUV = kQuadVerts[gl_VertexID]; 

    float u_pos = (rawUV.x * (len + 2.0 * expansion)) - expansion;
    float v_pos = rawUV.y * expansion;

    vec2 worldPos = inP0 + (dir * u_pos) + (norm * v_pos);

    gl_Position = uProj * vec4(worldPos, 0.0, 1.0);

    vLocalPos = vec2(u_pos, v_pos);
    vLen = len;
    vWidth = width;
    vColor = inColor;
}
)GLSL";

static const char* fs_line = R"GLSL(
#version 430 core

in vec2 vLocalPos;
in float vLen;
in float vWidth;
in vec4 vColor;

out vec4 fragColor;

void main()
{
    float t = clamp(vLocalPos.x, 0.0, vLen);
    float dist = distance(vLocalPos, vec2(t, 0.0));

    // Cap Trim to reduce "bulbous" joints
    const float kCapTrim = 0.90; 
    float effectiveDist = dist;
    if (vLocalPos.x < 0.0 || vLocalPos.x > vLen) {
        effectiveDist /= kCapTrim; 
    }

    float aa_size = 1.0; 
    float alpha = 1.0 - smoothstep(vWidth - (aa_size * 0.5), 
                                   vWidth + (aa_size * 0.5), 
                                   effectiveDist);

    if (alpha <= 0.0) discard;

    // Optional gamma correction for sharpness
    alpha = pow(alpha, 1.2); 

    fragColor = vec4(vColor.rgb, vColor.a * alpha);
}
)GLSL";


// =============================================================================
// 2. POINT SHADERS
// =============================================================================
static const char* vs_point = R"GLSL(
#version 430 core
layout(location = 0) in vec2 inPos;
layout(location = 1) in float inSize;
layout(location = 2) in vec4 inColor;

uniform mat4 uProj;

out vec4 vColor;

void main()
{
    gl_Position = uProj * vec4(inPos, 0.0, 1.0);
    gl_PointSize = inSize;
    vColor = inColor;
}
)GLSL";

static const char* fs_point = R"GLSL(
#version 430 core
in vec4 vColor;
out vec4 fragColor;
uniform float uEdgeSoftness = 0.15;

void main()
{
   float dist = length(gl_PointCoord - vec2(0.5));
   float edgeStart = 0.5;
   float edgeEnd = 0.5 - uEdgeSoftness;
   float alpha = smoothstep(edgeStart, edgeEnd, dist);
   fragColor = vec4(vColor.rgb, vColor.a * alpha);
}
)GLSL";

// =============================================================================
// 3. PROCEDURAL FIRE/SHOT SHADERS (TUNABLE)
// =============================================================================
static const char* vs_fire = R"GLSL(
#version 430 core
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

uniform mat4 uProj;

out vec2 vUV;
out vec4 vColor;

void main()
{
    gl_Position = uProj * vec4(inPos, 0.0, 1.0);
    vUV = inUV;
    vColor = inColor;
}
)GLSL";

static const char* fs_fire = R"GLSL(
#version 430 core
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;

// Tuning Uniforms
uniform float uCorePower;      
uniform float uBloomPower;     
uniform float uBloomIntensity; 
uniform float uOverdrive;      

void main()
{
    float brightness = max(vColor.r, max(vColor.g, vColor.b));
    brightness = max(0.1, brightness); // Safety floor

    // --- DYNAMICS ---
    // 1. Core Power: Static (No shrinking for dim shots)
    float dynCorePower = uCorePower; 

    // 2. Bloom Intensity: Dampened drop-off (Floor of 50%)
    float dynBloomInt = uBloomIntensity * (0.5 + (brightness * 0.5));

    // 3. Overdrive: Proportional
    float dynOverdrive = uOverdrive * brightness;

    // --- RENDER ---
    float d = distance(vUV, vec2(0.5));
    float r = clamp(d * 2.0, 0.0, 1.0);
    float glowBase = 1.0 - r;

    float core = pow(glowBase, dynCorePower);
    float halo = pow(glowBase, uBloomPower) * dynBloomInt;

    float totalIntensity = core + halo;
    vec3 hotColor = vColor.rgb * dynOverdrive;

    fragColor = vec4(hotColor * totalIntensity, totalIntensity);
}
)GLSL";

// -----------------------------------------------------------------------------
// Data Management
// -----------------------------------------------------------------------------

static std::vector<VecLine> g_lines;
static std::vector<VecPoint> g_points;
static std::vector<VecPoint> g_fire;

static GLuint vaoLine = 0, vboLine = 0;
static GLuint vaoPoint = 0, vboPoint = 0;
static GLuint vaoFire = 0, vboFire = 0;

static GLuint shaderLine = 0;
static GLuint shaderPoint = 0;
static GLuint shaderFire = 0;
static VectorConfig g_config;

// -----------------------------------------------------------------------------
// Implementation
// -----------------------------------------------------------------------------

void vector_draw_init(const VectorConfig& config) {
    g_config = config;

    // --- LINE INIT ---
    glGenVertexArrays(1, &vaoLine);
    glGenBuffers(1, &vboLine);
    glBindVertexArray(vaoLine);
    glBindBuffer(GL_ARRAY_BUFFER, vboLine);

    GLsizei stride = sizeof(VecLine);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(VecLine, p0));
    glEnableVertexAttribArray(0); glVertexAttribDivisor(0, 1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(VecLine, p1));
    glEnableVertexAttribArray(1); glVertexAttribDivisor(1, 1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(VecLine, thickness));
    glEnableVertexAttribArray(2); glVertexAttribDivisor(2, 1);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)offsetof(VecLine, color));
    glEnableVertexAttribArray(3); glVertexAttribDivisor(3, 1);

    // --- POINT INIT ---
    glGenVertexArrays(1, &vaoPoint);
    glGenBuffers(1, &vboPoint);
    glBindVertexArray(vaoPoint);
    glBindBuffer(GL_ARRAY_BUFFER, vboPoint);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VecPoint), (void*)offsetof(VecPoint, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(VecPoint), (void*)offsetof(VecPoint, size));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VecPoint), (void*)offsetof(VecPoint, color));
    glEnableVertexAttribArray(2);

    // --- FIRE INIT ---
    glGenVertexArrays(1, &vaoFire);
    glGenBuffers(1, &vboFire);
    glBindVertexArray(vaoFire);
    glBindBuffer(GL_ARRAY_BUFFER, vboFire);

    struct FireVertex { vec2 pos; vec2 uv; rgb_t color; };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(FireVertex), (void*)offsetof(FireVertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(FireVertex), (void*)offsetof(FireVertex, uv));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(FireVertex), (void*)offsetof(FireVertex, color));
    glEnableVertexAttribArray(2);

    // --- COMPILE SHADERS ---
    shaderLine = LinkShaderProgram(
        CompileShader(GL_VERTEX_SHADER, vs_line, "vector_line.vert"),
        CompileShader(GL_FRAGMENT_SHADER, fs_line, "vector_line.frag"));

    shaderPoint = LinkShaderProgram(
        CompileShader(GL_VERTEX_SHADER, vs_point, "vector_point.vert"),
        CompileShader(GL_FRAGMENT_SHADER, fs_point, "vector_point.frag"));

    shaderFire = LinkShaderProgram(
        CompileShader(GL_VERTEX_SHADER, vs_fire, "vector_fire.vert"),
        CompileShader(GL_FRAGMENT_SHADER, fs_fire, "vector_fire.frag"));
}

void vector_draw_shutdown() {
    glDeleteBuffers(1, &vboLine);
    glDeleteVertexArrays(1, &vaoLine);
    glDeleteProgram(shaderLine);
    glDeleteBuffers(1, &vboPoint);
    glDeleteVertexArrays(1, &vaoPoint);
    glDeleteProgram(shaderPoint);
    glDeleteBuffers(1, &vboFire);
    glDeleteVertexArrays(1, &vaoFire);
    glDeleteProgram(shaderFire);
}

void vector_add_line(vec2 p0, vec2 p1, float thickness, rgb_t color) {
    float th = thickness * g_config.line_width_scale;
    if (th < 1.0f) th = 1.0f;
    g_lines.push_back({ p0, p1, th, color });
}

void vector_add_point(vec2 pos, float size, rgb_t color) {
    g_points.push_back({ pos, size, color });
}

void vector_add_fire(vec2 pos, rgb_t color) {
    g_fire.push_back({ pos, g_config.fire_point_size, color });
}

void vector_clear() {
    g_lines.clear();
    g_points.clear();
    g_fire.clear();
}

void vector_draw_all(const mat4& projection)
{
    glEnable(GL_BLEND);
    GLenum srcFactor = GL_SRC_ALPHA;
    GLenum dstFactor = (g_config.blend_mode == BLEND_ADDITIVE) ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA;

    // 1. Lines (Instanced)
    if (!g_lines.empty())
    {
        glUseProgram(shaderLine);
        glUniformMatrix4fv(glGetUniformLocation(shaderLine, "uProj"), 1, GL_FALSE, value_ptr(projection));
        glBlendFunc(srcFactor, dstFactor);

        glBindBuffer(GL_ARRAY_BUFFER, vboLine);
        glBufferData(GL_ARRAY_BUFFER, g_lines.size() * sizeof(VecLine), g_lines.data(), GL_STREAM_DRAW);
        glBindVertexArray(vaoLine);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)g_lines.size());
    }

    // 2. Points
    if (!g_points.empty())
    {
        glUseProgram(shaderPoint);
        glUniformMatrix4fv(glGetUniformLocation(shaderPoint, "uProj"), 1, GL_FALSE, value_ptr(projection));
        glUniform1f(glGetUniformLocation(shaderPoint, "uEdgeSoftness"), 0.15f);
        glBlendFunc(srcFactor, dstFactor);

        glBindBuffer(GL_ARRAY_BUFFER, vboPoint);
        glBufferData(GL_ARRAY_BUFFER, g_points.size() * sizeof(VecPoint), g_points.data(), GL_STREAM_DRAW);
        glBindVertexArray(vaoPoint);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glDrawArrays(GL_POINTS, 0, (GLsizei)g_points.size());
    }

    // 3. Fire / Shots (Procedural & Tunable)
    if (!g_fire.empty())
    {
        struct FireVertex {
            vec2 pos;
            vec2 uv;
            rgb_t color;
        };

        // CPU Generation with Constricted Scaling (0.9 - 1.1)
        std::vector<FireVertex> fireVerts;
        fireVerts.reserve(g_fire.size() * 6);

        for (const auto& p : g_fire)
        {
            float r = RGB_RED(p.color) / 255.0f;
            float g = RGB_GREEN(p.color) / 255.0f;
            float b = RGB_BLUE(p.color) / 255.0f;
            float intensity = std::max({ r, g, b });

            // Scale geometry slightly based on intensity
            float minScale = 0.90f;
            float maxScale = 1.10f;
            float sizeScale = minScale + ((maxScale - minScale) * intensity);

            float currentSize = p.size * sizeScale;

            float x0 = p.pos.x - currentSize;
            float y0 = p.pos.y - currentSize;
            float x1 = p.pos.x + currentSize;
            float y1 = p.pos.y + currentSize;
            rgb_t c = p.color;

            fireVerts.push_back({ {x0, y0}, {0, 0}, c });
            fireVerts.push_back({ {x1, y0}, {1, 0}, c });
            fireVerts.push_back({ {x1, y1}, {1, 1}, c });
            fireVerts.push_back({ {x1, y1}, {1, 1}, c });
            fireVerts.push_back({ {x0, y1}, {0, 1}, c });
            fireVerts.push_back({ {x0, y0}, {0, 0}, c });
        }

        glUseProgram(shaderFire);
        glUniformMatrix4fv(glGetUniformLocation(shaderFire, "uProj"), 1, GL_FALSE, value_ptr(projection));

        // --- SEND TUNING UNIFORMS ---
        glUniform1f(glGetUniformLocation(shaderFire, "uCorePower"), g_config.shot_core_power);
        glUniform1f(glGetUniformLocation(shaderFire, "uBloomPower"), g_config.shot_bloom_power);
        glUniform1f(glGetUniformLocation(shaderFire, "uBloomIntensity"), g_config.shot_bloom_intensity);
        glUniform1f(glGetUniformLocation(shaderFire, "uOverdrive"), g_config.shot_overdrive);

        glBindBuffer(GL_ARRAY_BUFFER, vboFire);
        glBufferData(GL_ARRAY_BUFFER, fireVerts.size() * sizeof(FireVertex), fireVerts.data(), GL_STREAM_DRAW);
        glBindVertexArray(vaoFire);

        // Force Additive for shots
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);

        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)fireVerts.size());
    }
}