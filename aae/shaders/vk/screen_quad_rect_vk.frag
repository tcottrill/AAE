// screen_quad_rect_vk.frag
// -----------------------------------------------------------------------------
// Fragment shader for the rect-aware ScreenQuadVK path.
//
// Per-call tint is supplied via a fragment-stage push constant (vec4, 16
// bytes at offset 0) and multiplied with the sampled texel. Pass
// RGB_WHITE (1,1,1,1) for identity behavior.
//
// Compile:
//   glslc screen_quad_rect_vk.frag -o shaders/screen_quad_rect_vk.frag.spv
// -----------------------------------------------------------------------------

#version 450

layout(location = 0) in vec2 v_uv;

layout(set = 0, binding = 1) uniform sampler2D u_tex;

layout(push_constant) uniform PC
{
    vec4 tint;
} pc;

layout(location = 0) out vec4 o_color;

void main()
{
    o_color = texture(u_tex, v_uv) * pc.tint;
}
