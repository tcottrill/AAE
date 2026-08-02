// crt_scanline_vk.frag - VK port of the GL scanlineMultiplyFrag
// (shader_definitions.h:212). Body is verbatim: the tiled scanline texture is
// sampled and returned unchanged; the MULTIPLY is pipeline blend state
// (GL glBlendFunc(GL_DST_COLOR, GL_ZERO) -> VK DST_COLOR/ZERO).
//
// Tiling comes from the vertex shader's uvrect (GL used the same u = rw/texW,
// v = rh/texH trick on its quad) plus a REPEAT + NEAREST sampler, matching
// GL's per-draw glTexParameteri(GL_REPEAT / GL_NEAREST).
#version 450

layout(set = 0, binding = 0) uniform sampler2D u_scanTex;

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 tsize;
    vec4 uvrect;
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 tint;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = texture(u_scanTex, vUV);
}
