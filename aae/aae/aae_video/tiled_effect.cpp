#define NOMINMAX
#include "tiled_effect.h"
#include <algorithm>

static GLuint s_prog = 0;
static GLuint s_vbo = 0;

// Uniforms
static GLint s_loc_uTex = -1;
static GLint s_loc_uScale = -1;
static GLint s_loc_uTint = -1;
static GLint s_loc_uOpacity = -1;

// Attributes
static GLint s_loc_aPos = -1;
static GLint s_loc_aUV = -1;

// GL 2.1 / GLSL 1.20
static const char* VS_SRC =
"#version 120\n"
"attribute vec2 aPos;\n"
"attribute vec2 aUV;\n"
"varying vec2 vUV;\n"
"uniform vec2 uScale;\n"
"void main(){\n"
"  vUV = aUV * uScale;\n"
"  gl_Position = vec4(aPos, 0.0, 1.0);\n"
"}\n";

static const char* FS_SRC =
"#version 120\n"
"varying vec2 vUV;\n"
"uniform sampler2D uTex;\n"
"uniform float uOpacity;\n"
"uniform vec4  uTint;\n"
"void main(){\n"
"  vec4 c = texture2D(uTex, vUV);\n"
"  gl_FragColor = vec4(c.rgb * uTint.rgb, c.a * uOpacity);\n"
"}\n";

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

static bool get_texture_size(GLuint tex, GLint* w, GLint* h) {
    if (!tex || !w || !h) return false;
    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint tw = 0, th = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    if (tw <= 0 || th <= 0) return false;
    *w = tw; *h = th;
    return true;
}

bool TiledEffect_Init(void)
{
    if (s_prog && s_vbo) return true;

    // Compile using your shader util
    GLuint vs = CompileShader(GL_VERTEX_SHADER, VS_SRC, "TiledEffect VS");
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, FS_SRC, "TiledEffect FS");
    if (!vs || !fs) return false;

    // Link program (explicitly, so we can query attribute/uniform locations)
    s_prog = glCreateProgram();
    glAttachShader(s_prog, vs);
    glAttachShader(s_prog, fs);
    glLinkProgram(s_prog);

    // shaders can be deleted after link; your util also deletes in LinkShaderProgram,
    // but we manually linked here to keep control over attrib bindings for GLSL 120.
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

    // Query locations
    s_loc_uTex = glGetUniformLocation(s_prog, "uTex");
    s_loc_uScale = glGetUniformLocation(s_prog, "uScale");
    s_loc_uTint = glGetUniformLocation(s_prog, "uTint");
    s_loc_uOpacity = glGetUniformLocation(s_prog, "uOpacity");
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

    s_loc_uTex = s_loc_uScale = s_loc_uOpacity = -1;
    s_loc_aPos = s_loc_aUV = -1;
}

void TiledEffect_Draw(GLuint tex, int surfaceW, int surfaceH, float opacity)
{
    if (!s_prog || !s_vbo || !tex || surfaceW <= 0 || surfaceH <= 0) return;

    // Source texture size (e.g., 12x4)
    GLint texW = 0, texH = 0;
    if (!get_texture_size(tex, &texW, &texH) || texW <= 0 || texH <= 0) return;

    // How many repetitions across the target area
    const float scaleX = (float)surfaceW / (float)texW;
    const float scaleY = (float)surfaceH / (float)texH;

    // Texture state: repeat + nearest (crisp). Change to LINEAR if you prefer.
    GLint prevTex = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Pipeline bits appropriate for a full-screen overlay
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);

    glUseProgram(s_prog);
    glUniform1i(s_loc_uTex, 0);
    glUniform2f(s_loc_uScale, scaleX, scaleY);
    const float tint = 0.55f;
    glUniform4f(s_loc_uTint, tint, tint, tint, tint);
    glUniform1f(s_loc_uOpacity, std::max(0.0f, std::min(1.0f, opacity)));

    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    // Interleaved [x,y,u,v]
    glEnableVertexAttribArray((GLuint)s_loc_aPos);
    glEnableVertexAttribArray((GLuint)s_loc_aUV);
    glVertexAttribPointer((GLuint)s_loc_aPos, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (const GLvoid*)(0));
    glVertexAttribPointer((GLuint)s_loc_aUV, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, (const GLvoid*)(sizeof(float) * 2));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray((GLuint)s_loc_aPos);
    glDisableVertexAttribArray((GLuint)s_loc_aUV);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    // Optional: glDisable(GL_BLEND);

    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);

    check_gl_error_named("TiledEffect_Draw");
}
