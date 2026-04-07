// =========================================================================
// tiled_effect.cpp
// =========================================================================

#define NOMINMAX
#include "tiled_effect.h"
#include <algorithm>

static GLuint s_prog = 0;
static GLuint s_vbo = 0;

// Uniforms
static GLint s_loc_uTex = -1;
static GLint s_loc_uUseTex = -1;
static GLint s_loc_uGameRes = -1;
static GLint s_loc_uOpacity = -1;
static GLint s_loc_uScale = -1;

// Attributes
static GLint s_loc_aPos = -1;
static GLint s_loc_aUV = -1;

// GL 3.3 Compatibility Profile - Vertex Shader
static const char* VS_SRC = R"glsl(
#version 330 compatibility
in vec2 aPos;
in vec2 aUV;
out vec2 vUV;
void main(){
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

// GL 3.3 Compatibility Profile - Fragment Shader
static const char* FS_SRC = R"glsl(
#version 330 compatibility
in vec2 vUV;
uniform sampler2D uTex;
uniform int uUseTex;
uniform vec2 uGameRes;
uniform float uOpacity;
uniform float uScale;
out vec4 FragColor;

void main(){
    vec3 color = vec3(1.0);

    // uScale makes the scanlines thicker/chunkier by dividing the target resolution.
    // e.g., uScale = 2.0 makes the scanlines twice as thick!
    vec2 scaledRes = uGameRes / uScale;

    if (uUseTex == 1) {
        // Map the texture perfectly to the scaled game pixels
        vec2 texUV = vUV * scaledRes;
        vec4 c = texture(uTex, texUV);
        
        // Mix with white based on texture alpha
        vec3 texColor = mix(vec3(1.0), c.rgb, c.a);
        color = mix(vec3(1.0), texColor, uOpacity);
    } else {
        // Procedural scanline fallback
        float row = vUV.y * scaledRes.y;
        float wave = sin(row * 3.14159265);
        wave = wave * wave;
        
        // Remap so it doesn't go completely pitch black
        float lineIntensity = mix(0.4, 1.0, wave);
        color = mix(vec3(1.0), vec3(lineIntensity), uOpacity);
    }

    // Multiply result due to GL_ZERO, GL_SRC_COLOR blending
    FragColor = vec4(color, 1.0);
}
)glsl";

// Interleaved fullscreen quad in clip space (-1..1), with base UV 0..1
struct Vert { float x, y, u, v; };

static bool create_vbo_once() {
    if (s_vbo) return true;

    const Vert quad[4] = {
        { -1.0f, -1.0f, 0.0f, 0.0f },
        {  1.0f, -1.0f, 1.0f, 0.0f },
        { -1.0f,  1.0f, 0.0f, 1.0f },
        {  1.0f,  1.0f, 1.0f, 1.0f },
    };

    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    check_gl_error_named("TiledEffect create_vbo_once");
    return s_vbo != 0;
}

bool TiledEffect_Init(void)
{
    if (s_prog && s_vbo) return true;

    GLuint vs = CompileShader(GL_VERTEX_SHADER, VS_SRC, "TiledEffect VS");
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, FS_SRC, "TiledEffect FS");
    if (!vs || !fs) return false;

    s_prog = glCreateProgram();
    glAttachShader(s_prog, vs);
    glAttachShader(s_prog, fs);
    glLinkProgram(s_prog);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = GL_FALSE;
    glGetProgramiv(s_prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024] = {};
        GLsizei n = 0;
        glGetProgramInfoLog(s_prog, sizeof(log), &n, log);
        LOG_ERROR("TiledEffect link failed:\n%s", log);
        glDeleteProgram(s_prog);
        s_prog = 0;
        return false;
    }

    s_loc_uTex = glGetUniformLocation(s_prog, "uTex");
    s_loc_uUseTex = glGetUniformLocation(s_prog, "uUseTex");
    s_loc_uGameRes = glGetUniformLocation(s_prog, "uGameRes");
    s_loc_uOpacity = glGetUniformLocation(s_prog, "uOpacity");
    s_loc_uScale = glGetUniformLocation(s_prog, "uScale");
    s_loc_aPos = glGetAttribLocation(s_prog, "aPos");
    s_loc_aUV = glGetAttribLocation(s_prog, "aUV");

    if (s_loc_aPos < 0 || s_loc_aUV < 0) {
        LOG_ERROR("TiledEffect: missing attribute locations (aPos=%d aUV=%d)", s_loc_aPos, s_loc_aUV);
        TiledEffect_Shutdown();
        return false;
    }

    if (!create_vbo_once()) {
        TiledEffect_Shutdown();
        return false;
    }

    check_gl_error_named("TiledEffect_Init");
    return true;
}

void TiledEffect_Shutdown(void)
{
    if (s_vbo) {
        glDeleteBuffers(1, &s_vbo);
        s_vbo = 0;
    }
    if (s_prog) {
        glDeleteProgram(s_prog);
        s_prog = 0;
    }

    s_loc_uTex = -1;
    s_loc_uUseTex = -1;
    s_loc_uGameRes = -1;
    s_loc_uOpacity = -1;
    s_loc_uScale = -1;
    s_loc_aPos = -1;
    s_loc_aUV = -1;
}

void TiledEffect_Draw(GLuint tex, int gameW, int gameH, float opacity, float scale)
{
    if (!s_prog || !s_vbo || gameW <= 0 || gameH <= 0) return;

    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);

    glUseProgram(s_prog);

    if (tex != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        // GL_LINEAR smoothly blends the texture preventing moiré banding when stretching!
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glUniform1i(s_loc_uTex, 0);
        glUniform1i(s_loc_uUseTex, 1);
    }
    else {
        glUniform1i(s_loc_uUseTex, 0);
    }

    glUniform2f(s_loc_uGameRes, (float)gameW, (float)gameH);
    glUniform1f(s_loc_uOpacity, std::max(0.0f, std::min(1.0f, opacity)));
    glUniform1f(s_loc_uScale, std::max(0.1f, scale)); // Pass the chunkiness scale!

    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glEnableVertexAttribArray((GLuint)s_loc_aPos);
    glEnableVertexAttribArray((GLuint)s_loc_aUV);
    glVertexAttribPointer((GLuint)s_loc_aPos, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (const GLvoid*)(0));
    glVertexAttribPointer((GLuint)s_loc_aUV, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (const GLvoid*)(sizeof(float) * 2));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray((GLuint)s_loc_aPos);
    glDisableVertexAttribArray((GLuint)s_loc_aUV);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    check_gl_error_named("TiledEffect_Draw");
}