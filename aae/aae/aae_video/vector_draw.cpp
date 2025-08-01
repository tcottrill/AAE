// -----------------------------------------------------------------------------
// vector_draw.cpp - Modern OpenGL 4.3+ Vector Renderer
// This is test code, it's not prime time yet. 
// -----------------------------------------------------------------------------
#define GLM_ENABLE_EXPERIMENTAL
#include "vector_draw.h"
#include <GL/glew.h>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include "sys_log.h"
#include <map>
#include <unordered_map>
#include <glm/gtx/hash.hpp>  // for hashing glm::ivec2
#include "shader_util.h"

// -----------------------------------------------------------------------------
// Inline GLSL shaders
// -----------------------------------------------------------------------------
static const char* vs_line = R"GLSL(
#version 430 core
layout(location = 0) in vec2 inPos;
layout(location = 1) in float inOffset;
layout(location = 2) in vec4 inColor;

uniform mat4 uProj;

out float vOffset;
out vec4 vColor;

void main()
{
    gl_Position = uProj * vec4(inPos, 0.0, 1.0);
    vOffset = inOffset;
    vColor = inColor;
}
)GLSL";

static const char* fs_line = R"GLSL(
#version 430 core

in float vOffset;
in vec4 vColor;
out vec4 fragColor;

uniform float uLineWidth;
uniform float uEdgeSmooth;

void main()
{
    float edgeStart = uLineWidth * 0.5;
    float edgeEnd   = edgeStart - uEdgeSmooth;
   // float alpha = exp(-pow(distance / uEdgeSmooth, 2.0));

    float distance = abs(vOffset * 0.5 * uLineWidth);
    float alpha = smoothstep(edgeStart, edgeEnd, distance);

    // Fade RGB toward black for smoother edges (optional)
    //vec3 fadedRGB = mix(vec3(0.0), vColor.rgb, alpha);

   // fragColor = vec4(fadedRGB, vColor.a * alpha);
fragColor = vec4(vColor.rgb, vColor.a * alpha);
}
)GLSL";

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

uniform sampler2D uTex;

void main()
{
    float alpha = texture(uTex, vUV).r;
    fragColor = vec4(vColor.rgb, vColor.a * alpha);
}
)GLSL";


struct QuadVertex {
    glm::vec2 pos;
    float offset;
    glm::vec4 color;
};

struct FireVertex {
    glm::vec2 pos;
    glm::vec2 uv;
    glm::vec4 color;
};

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

static std::string load_file(const char* path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static GLuint compile_shader(GLenum type, const char* path) {
    std::string code = load_file(path);
    const char* src = code.c_str();

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        LOG_ERROR("compile_shader - failed to compile %s:\n%s", path, log);
    }
    else {
        LOG_INFO("compile_shader - successfully compiled %s", path);
    }

    return shader;
}

static GLuint create_program(const char* vs, const char* fs) {
    LOG_INFO("create_program - creating shader program with: %s and %s", vs, fs);

    GLuint vert = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, fs);

    if (!vert || !frag) {
        LOG_ERROR("create_program - skipping link due to failed shader compile");
        return 0;
    }

    GLuint p = glCreateProgram();
    glAttachShader(p, vert);
    glAttachShader(p, frag);
    glLinkProgram(p);

    GLint linked = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(p, 512, nullptr, log);
        LOG_ERROR("create_program - failed to link program with %s and %s:\n%s", vs, fs, log);
        glDeleteProgram(p);
        return 0;
    }

    LOG_INFO("create_program - successfully linked program with %s and %s", vs, fs);
    return p;
}

void vector_add_junction_points(float angleThresholdDegrees, float minSegmentLength)
{
    if (g_lines.size() < 2)
        return;

    float angleThresholdDot = cos(glm::radians(180.0f - angleThresholdDegrees));
    std::unordered_map<glm::ivec2, std::vector<size_t>> endpointMap;

    // Step 1: map endpoints to their integer pixel coords
    for (size_t i = 0; i < g_lines.size(); ++i) {
        const auto& l = g_lines[i];
        if (glm::length(l.p1 - l.p0) < minSegmentLength)
            continue;

        endpointMap[glm::ivec2(l.p0)].push_back(i);
        endpointMap[glm::ivec2(l.p1)].push_back(i);
    }

    // Step 2: check angle at shared junctions
    for (const auto& [pix, indices] : endpointMap)
    {
        if (indices.size() < 2)
            continue;

        glm::vec2 point = glm::vec2(pix);

        for (size_t a = 0; a < indices.size(); ++a) {
            for (size_t b = a + 1; b < indices.size(); ++b) {
                const auto& la = g_lines[indices[a]];
                const auto& lb = g_lines[indices[b]];

                glm::vec2 da = (glm::length(la.p0 - point) < 0.1f) ? (la.p1 - la.p0) : (la.p0 - la.p1);
                glm::vec2 db = (glm::length(lb.p0 - point) < 0.1f) ? (lb.p1 - lb.p0) : (lb.p0 - lb.p1);

                if (glm::length(da) < minSegmentLength || glm::length(db) < minSegmentLength)
                    continue;

                float dot = glm::dot(glm::normalize(da), glm::normalize(db));
               // LOG_INFO("Testing dot at (%.1f, %.1f): dot = %.4f", point.x, point.y, dot);
                if (dot < 0.996f){
                    float avgSize = 2.3f;// 0.5f * (la.thickness + lb.thickness);
                    glm::vec4 avgColor = 0.5f * (la.color + lb.color);
                    vector_add_point(point, avgSize, avgColor);

                    // Optional: Log junction
                    //LOG_INFO("Junction added at (%.1f, %.1f) with dot %.3f", point.x, point.y, dot);
                    goto next_point;
                }
            }
        }

    next_point:;
    }
}

// -----------------------------------------------------------------------------

void vector_draw_init(const VectorConfig& config) {
    g_config = config;

    // Enable sRGB blending (gamma correct)
  //  glEnable(GL_FRAMEBUFFER_SRGB);

   // ***************** Or manually created FBOs using formats like GL_SRGB8_ALPHA8 *********************

    // Create VAOs and VBOs
    glGenVertexArrays(1, &vaoLine);
    glGenBuffers(1, &vboLine);
    glBindVertexArray(vaoLine);
    glBindBuffer(GL_ARRAY_BUFFER, vboLine);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void*)offsetof(QuadVertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void*)offsetof(QuadVertex, offset));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void*)offsetof(QuadVertex, color));
    glEnableVertexAttribArray(2);

    glGenVertexArrays(1, &vaoPoint);
    glGenBuffers(1, &vboPoint);
    glBindVertexArray(vaoPoint);
    glBindBuffer(GL_ARRAY_BUFFER, vboPoint);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VecPoint), (void*)offsetof(VecPoint, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(VecPoint), (void*)offsetof(VecPoint, size));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(VecPoint), (void*)offsetof(VecPoint, color));
    glEnableVertexAttribArray(2);

    glGenVertexArrays(1, &vaoFire);
    glGenBuffers(1, &vboFire);
    glBindVertexArray(vaoFire);
    glBindBuffer(GL_ARRAY_BUFFER, vboFire);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(FireVertex), (void*)offsetof(FireVertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(FireVertex), (void*)offsetof(FireVertex, uv));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(FireVertex), (void*)offsetof(FireVertex, color));
    glEnableVertexAttribArray(2);

    // ------------------- SHADER COMPILATION -------------------
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
    glDeleteBuffers(1, &vboPoint);
    glDeleteBuffers(1, &vboFire);
    glDeleteVertexArrays(1, &vaoLine);
    glDeleteVertexArrays(1, &vaoPoint);
    glDeleteVertexArrays(1, &vaoFire);
    glDeleteProgram(shaderLine);
    glDeleteProgram(shaderPoint);
    glDeleteProgram(shaderFire);
}

void vector_add_line(glm::vec2 p0, glm::vec2 p1, float thickness, glm::vec4 color) {
    g_lines.push_back({ p0, p1, thickness, color });
}

void vector_add_point(glm::vec2 pos, float size, glm::vec4 color) {
    g_points.push_back({ pos, size, color });
}

void vector_add_fire(glm::vec2 pos, glm::vec4 color) {
    g_fire.push_back({ pos, g_config.fire_point_size, color });
}

void vector_clear() {
    g_lines.clear();
    g_points.clear();
    g_fire.clear();
}

void vector_draw_all(const glm::mat4& projection)
{
    // Step 1: Count how many lines share each endpoint
    std::unordered_map<glm::ivec2, int> endpointCount;
    auto toPix = [](const glm::vec2& p) { return glm::ivec2((int)roundf(p.x), (int)roundf(p.y)); };

    for (const auto& l : g_lines) {
        endpointCount[toPix(l.p0)]++;
        endpointCount[toPix(l.p1)]++;
    }

    // -------------------- Render Lines --------------------
    std::vector<QuadVertex> lineVerts;
    /*
    for (const auto& l : g_lines)
    {
        glm::vec2 dir = glm::normalize(l.p1 - l.p0);
        glm::vec2 normal = glm::vec2(-dir.y, dir.x) * (l.thickness * 0.5f);

        glm::vec2 v0 = l.p0 + normal;
        glm::vec2 v1 = l.p0 - normal;
        glm::vec2 v2 = l.p1 + normal;
        glm::vec2 v3 = l.p1 - normal;

        lineVerts.push_back({ v0, +1.0f, l.color });
        lineVerts.push_back({ v1, -1.0f, l.color });
        lineVerts.push_back({ v2, +1.0f, l.color });
        lineVerts.push_back({ v2, +1.0f, l.color });
        lineVerts.push_back({ v1, -1.0f, l.color });
        lineVerts.push_back({ v3, -1.0f, l.color });
    }
    */
    for (const auto& l : g_lines)
    {
        glm::vec2 dir = glm::normalize(l.p1 - l.p0);
        glm::vec2 normal = glm::vec2(-dir.y, dir.x) * (l.thickness * 0.5f);

        float extend = l.thickness * 0.25f;

        // Step 2: Extend only endpoints that are free (count == 1)
        glm::vec2 p0_ext = (endpointCount[toPix(l.p0)] == 1) ? (l.p0 - dir * extend) : l.p0;
        glm::vec2 p1_ext = (endpointCount[toPix(l.p1)] == 1) ? (l.p1 + dir * extend) : l.p1;

        glm::vec2 v0 = p0_ext + normal;
        glm::vec2 v1 = p0_ext - normal;
        glm::vec2 v2 = p1_ext + normal;
        glm::vec2 v3 = p1_ext - normal;

        lineVerts.push_back({ v0, +1.0f, l.color });
        lineVerts.push_back({ v1, -1.0f, l.color });
        lineVerts.push_back({ v2, +1.0f, l.color });
        lineVerts.push_back({ v2, +1.0f, l.color });
        lineVerts.push_back({ v1, -1.0f, l.color });
        lineVerts.push_back({ v3, -1.0f, l.color });
    }
    if (!lineVerts.empty())
    {
        glUseProgram(shaderLine);
        glUniformMatrix4fv(glGetUniformLocation(shaderLine, "uProj"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform1f(glGetUniformLocation(shaderLine, "uLineWidth"), 6.0f);
        glUniform1f(glGetUniformLocation(shaderLine, "uEdgeSmooth"), 2.0f);

        glBindBuffer(GL_ARRAY_BUFFER, vboLine);
        glBufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(QuadVertex), lineVerts.data(), GL_DYNAMIC_DRAW);

        glBindVertexArray(vaoLine);
        glBlendFunc(GL_SRC_ALPHA, g_config.blend_mode == BLEND_ADDITIVE ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)lineVerts.size());
    }

    // -------------------- Render Points --------------------
    if (!g_points.empty())
    {
        glUseProgram(shaderPoint);
        glUniformMatrix4fv(glGetUniformLocation(shaderPoint, "uProj"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform1f(glGetUniformLocation(shaderPoint, "uEdgeSoftness"), 0.15f); //AA Falloff setting

        glBindBuffer(GL_ARRAY_BUFFER, vboPoint);
        glBufferData(GL_ARRAY_BUFFER, g_points.size() * sizeof(VecPoint), g_points.data(), GL_DYNAMIC_DRAW);

        glBindVertexArray(vaoPoint);
        glBlendFunc(GL_SRC_ALPHA, g_config.blend_mode == BLEND_ADDITIVE ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_POINTS, 0, (GLsizei)g_points.size());
    }

    // -------------------- Render Fire Quads --------------------
    if (!g_fire.empty() && g_config.fire_texture)
    {
        std::vector<FireVertex> fireVerts;
        for (const auto& p : g_fire)
        {
            float x0 = p.pos.x - p.size;
            float y0 = p.pos.y - p.size;
            float x1 = p.pos.x + p.size;
            float y1 = p.pos.y + p.size;
            glm::vec4 c = p.color;

            fireVerts.push_back({ {x0, y0}, {0, 0}, c });
            fireVerts.push_back({ {x1, y0}, {1, 0}, c });
            fireVerts.push_back({ {x1, y1}, {1, 1}, c });
            fireVerts.push_back({ {x1, y1}, {1, 1}, c });
            fireVerts.push_back({ {x0, y1}, {0, 1}, c });
            fireVerts.push_back({ {x0, y0}, {0, 0}, c });
        }
        glEnable(GL_TEXTURE_2D);
      //  glDisable(GL_ALPHA_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
       // glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glUseProgram(shaderFire);
        glUniformMatrix4fv(glGetUniformLocation(shaderFire, "uProj"), 1, GL_FALSE, glm::value_ptr(projection));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_config.fire_texture);
        glUniform1i(glGetUniformLocation(shaderFire, "uTex"), 0);

        glBindBuffer(GL_ARRAY_BUFFER, vboFire);
        glBufferData(GL_ARRAY_BUFFER, fireVerts.size() * sizeof(FireVertex), fireVerts.data(), GL_DYNAMIC_DRAW);

        glBindVertexArray(vaoFire);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); // always additive like original
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)fireVerts.size());
    }
}
