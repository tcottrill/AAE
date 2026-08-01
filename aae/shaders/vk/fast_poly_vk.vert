#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inColor;

layout(set = 0, binding = 0) uniform Globals
{
    mat4 uProj;
} g;

layout(location = 0) out vec4 vColor;

void main()
{
    vColor = inColor;
    gl_Position = g.uProj * vec4(inPos.xy, 0.0, 1.0);
}
