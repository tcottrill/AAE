// vector_shot_tex_vk.vert - textured vector shots (Plan 9): VK port of the GL
// texColorVert (shader_definitions.h). Vertex stream is the CPU txdata layout
// (pos, uv, packed RGBA) from emu_vector_draw.cpp's texlist; uProj is the SAME
// beam ortho(0,1024,0,1024) the beam renderer uses, drawn with the same
// flipped (negative-height) viewport so shots land on the exact RT rows as
// the beams they fly beside.
#version 450

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

layout(push_constant) uniform Push {
    mat4 uProj;   // offset 0
    vec4 fade;    // offset 64: x = fade inner radius, y = fade outer radius
} pc;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main()
{
    vUV = aUV;
    vColor = aColor;
    gl_Position = pc.uProj * vec4(aPos, 0.0, 1.0);
}
