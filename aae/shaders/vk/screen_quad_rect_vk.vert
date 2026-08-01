// screen_quad_rect_vk.vert
// -----------------------------------------------------------------------------
// Vertex shader for the rect-aware ScreenQuadVK path. Used by BG and any other
// "draw a textured rect at screen-pixel coords" caller.
//
// Coord convention: vertex (x, y) is in CALLER-PROVIDED screen-pixel space
// (e.g. "(0..gameWidth, 0..gameHeight)"). The mat4 in the UBO is an ortho
// that maps that range to NDC. The viewport is set y-flipped on VK by the
// caller, which matches the engine convention.
//
// Compile:
//   glslc screen_quad_rect_vk.vert -o shaders/screen_quad_rect_vk.vert.spv
// -----------------------------------------------------------------------------

#version 450

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;

layout(set = 0, binding = 0) uniform UBO
{
    mat4 u_mvp;
} ubo;

layout(location = 0) out vec2 v_uv;

void main()
{
    gl_Position = ubo.u_mvp * vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}
