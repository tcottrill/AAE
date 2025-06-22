#pragma once

// Old Style Blur Shader
// VS
const char* vertText = R"glsl(
#version 130

void main()
{
    // Pass through the texture coordinate (compatibility built-in)
    gl_TexCoord[0] = gl_MultiTexCoord0;

    // Use ftransform() for compatibility with fixed-function pipeline
    gl_Position = ftransform();
}
)glsl";

// FS
const char* fragText = R"glsl(
#version 130

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
        vec4 tmp = texture2D(colorMap, gl_TexCoord[0].st + offset[i]);
        sum += tmp * kernel[i];
    }

    gl_FragColor = sum * 1.12;
}
)glsl";

// Multitexturing Combining Shaders
// VS
const char* texvertText = R"glsl(
#version 130

void main(void)
{
    // Pass texture coordinates for multiple units
    gl_TexCoord[0] = gl_MultiTexCoord0;
    gl_TexCoord[1] = gl_MultiTexCoord1;

    // Use fixed-function transform
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)glsl";

// FS
const char* texfragText = R"glsl(
#version 130

uniform int useart;
uniform int usefb;
uniform int useglow;
uniform float glowamt;
uniform int brighten;

uniform sampler2D mytex1; // Artwork
uniform sampler2D mytex2; // Vectors
uniform sampler2D mytex3; // Glow
uniform sampler2D mytex4; // Feedback

void main(void)
{
    float bval = 1.0;

    vec2 uv = gl_TexCoord[0].xy;

    vec4 texval1 = texture2D(mytex1, uv); // Artwork
    vec4 texval2 = texture2D(mytex2, uv); // Vectors
    vec4 texval3 = texture2D(mytex3, uv); // Glow
    vec4 texval4 = texture2D(mytex4, uv); // Feedback

    if (brighten > 0 && useart > 0 && usefb == 0)
        bval = 1.6;
    else if (usefb > 0)
        bval = 1.0;

    vec4 result = texval2 * bval;

    if (useart > 0)  result += texval1 * 0.60;
    if (useglow > 0) result += texval3 * glowamt;
    if (usefb > 0)   result += texval4 * 0.25;

    gl_FragColor = result;
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