#pragma once

// Old Style Blur Shader
// VS
const char* vertText = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uProj;

out vec2 TexCoord;

void main()
{
    TexCoord = aUV;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

// FS
const char* fragText = R"glsl(
#version 330 core

uniform sampler2D colorMap;
uniform float width;
uniform float height;

in vec2 TexCoord;
out vec4 FragColor; // Modern GLSL uses custom output variables instead of gl_FragColor

void main()
{
    float step_w = 1.0 / width;
    float step_h = 1.0 / height;

    vec2 offset[9] = vec2[](
        vec2(-step_w, -step_h), vec2(0.0, -step_h), vec2(step_w, -step_h),
        vec2(-step_w, 0.0),     vec2(0.0, 0.0),     vec2(step_w, 0.0),
        vec2(-step_w, step_h),  vec2(0.0, step_h),  vec2(step_w, step_h)
    );

    float kernel[9] = float[](
        1.0/17.0, 2.0/17.0, 1.0/17.0,
        2.0/17.0, 4.0/17.0, 2.0/17.0,
        1.0/17.0, 2.0/17.0, 1.0/17.0
    );

    vec4 sum = vec4(0.0);
    for (int i = 0; i < 9; i++)
    {
        // texture2D is deprecated in modern GLSL, replaced by texture()
        vec4 tmp = texture(colorMap, TexCoord + offset[i]);
        sum += tmp * kernel[i];
    }

    FragColor = sum * 1.12;
}
)glsl";

// Multitexturing Combining Shaders
// VS
const char* texvertText = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uProj;

out vec2 TexCoord0;

void main(void)
{
    TexCoord0 = aUV;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

// FS
const char* texfragText = R"glsl(
#version 330 core

uniform int usefb;
uniform int useglow;
uniform float glowamt;
uniform int brighten; // Note: Maybe can be removed

uniform sampler2D mytex2; // Vectors
uniform sampler2D mytex3; // Glow
uniform sampler2D mytex4; // Feedback

in vec2 TexCoord0;
out vec4 FragColor;

void main(void)
{
    float bval = 1.0;
    vec2 uv = TexCoord0;

    vec4 texval2 = texture(mytex2, uv); // Vectors
    vec4 texval3 = texture(mytex3, uv); // Glow
    vec4 texval4 = texture(mytex4, uv); // Feedback
   
    vec4 result = texval2 * bval;

    if (useglow > 0) result += texval3 * glowamt;
    if (usefb > 0)   result += texval4 * 0.25;

    FragColor = result;
}
)glsl";

// ---------------------------------------------------------
// Basic Texture Shader (Replaces fixed-function texturing)
// ---------------------------------------------------------
const char* basicTexVert = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uProj;

out vec2 TexCoord;

void main()
{
    TexCoord = aUV;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* basicTexFrag = R"glsl(
#version 330 core

uniform sampler2D u_texture;
uniform vec4 uColor;
uniform float uAlphaTest;   // 0 = no test; else discard fragments below this alpha

in vec2 TexCoord;
out vec4 FragColor;

void main()
{
    vec4 c = texture(u_texture, TexCoord) * uColor;
    if (c.a < uAlphaTest) discard;
    FragColor = c;
}
)glsl";

// ---------------------------------------------------------
// Basic Color Shader (Replaces fixed-function colored quads)
// ---------------------------------------------------------
const char* basicColorVert = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uProj;

out vec4 VertColor;

void main()
{
    VertColor = aColor;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* basicColorFrag = R"glsl(
#version 330 core

in vec4 VertColor;
out vec4 FragColor;

void main()
{
    FragColor = VertColor;
}
)glsl";


// ---------------------------------------------------------
// Scanline Multiply Shader
// Tiles a scanline texture over a fullscreen quad using
// GL_REPEAT-style UV math, then multiplies it against the
// existing framebuffer contents via blending (DST_COLOR, ZERO).
// Replaces the fixed-function glBegin/glEnd scanline overlay.
// ---------------------------------------------------------
const char* scanlineMultiplyVert = R"glsl(
#version 330 core

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inTex;

uniform mat4 u_projection;

out vec2 TexCoord;

void main()
{
    gl_Position = u_projection * vec4(inPos, 0.0, 1.0);
    TexCoord    = inTex;
}
)glsl";

const char* scanlineMultiplyFrag = R"glsl(
#version 330 core

uniform sampler2D u_scanTex;

in vec2 TexCoord;
out vec4 FragColor;

void main()
{
    FragColor = texture(u_scanTex, TexCoord);
}
)glsl";


// ---------------------------------------------------------
// Star Point Shader (VBO/VAO point rendering for GUI stars)
// ---------------------------------------------------------
const char* starPointVert = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uProj;

out vec4 VertColor;

void main()
{
    VertColor = aColor;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* starPointFrag = R"glsl(
#version 330 core

in vec4 VertColor;
out vec4 FragColor;

void main()
{
    FragColor = VertColor;
}
)glsl";


// ---------------------------------------------------------
// Textured + per-vertex-color shader
// Replaces the fixed-function GL_MODULATE client-array path used by the legacy
// textured shots (draw_textured_shots): FragColor = texture(uv) * vertexColor.
// ---------------------------------------------------------
const char* texColorVert = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform mat4 uProj;

out vec2 TexCoord;
out vec4 VertColor;

void main()
{
    TexCoord  = aUV;
    VertColor = aColor;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* texColorFrag = R"glsl(
#version 330 core

uniform sampler2D u_texture;
uniform float uFadeInner;   // radius (0=center .. 1=edge) of the full-bright core
uniform float uFadeOuter;   // radius where the edge fade reaches zero

in vec2 TexCoord;
in vec4 VertColor;
out vec4 FragColor;

void main()
{
    vec4 c = texture(u_texture, TexCoord) * VertColor;
    // Radial edge fade: full brightness inside uFadeInner, ramping to 0 by
    // uFadeOuter so the square texture/quad boundary disappears on the additive
    // halo. The bright center is untouched (fade = 1 there).
    float d = length(TexCoord - vec2(0.5)) * 2.0;   // 0 center, 1 edge, ~1.41 corner
    float fade = 1.0 - smoothstep(uFadeInner, uFadeOuter, d);
    FragColor = vec4(c.rgb * fade, c.a);
}
)glsl";






// New 330 Blur Shaders
/*
const char* vertText = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 modelViewProjection;

void main()
{
    TexCoord = aTexCoord;
    gl_Position = modelViewProjection * vec4(aPos, 1.0);
}
)glsl";

const char* fragText = R"glsl(
#version 330 core

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D colorMap;
uniform float width;
uniform float height;

void main()
{
    float step_w = 1.0 / width;
    float step_h = 1.0 / height;

    vec2 offset[9] = vec2[](
        vec2(-step_w, -step_h), vec2(0.0, -step_h), vec2(step_w, -step_h),
        vec2(-step_w, 0.0),     vec2(0.0, 0.0),     vec2(step_w, 0.0),
        vec2(-step_w, step_h),  vec2(0.0, step_h),  vec2(step_w, step_h)
    );

    float kernel[9] = float[](
        1.0/17.0, 2.0/17.0, 1.0/17.0,
        2.0/17.0, 4.0/17.0, 2.0/17.0,
        1.0/17.0, 2.0/17.0, 1.0/17.0
    );

    vec4 sum = vec4(0.0);
    for (int i = 0; i < 9; i++)
    {
        vec4 tmp = texture(colorMap, TexCoord + offset[i]);
        sum += tmp * kernel[i];
    }

    FragColor = sum * 1.12;
}
)glsl";

*/