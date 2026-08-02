// layout_quad_vk.frag
// -----------------------------------------------------------------------------
// Fragment shader for the Vulkan MAME .lay layout compositor (LayoutQuadVK).
//
// One shader covers BOTH GL layout programs in mame_layout.cpp's
// InitLayoutShaders():
//
//   u_params.y <  0  -> the GL "single-texture" program (fsSingleSrc):
//                         FragColor = vec4(c.rgb, c.a * uAlpha)
//                       Used for backdrops, bezels, and screens with no
//                       overlay. Note the RGB is NOT scaled by the alpha
//                       uniform - only the alpha channel is - exactly as GL.
//
//   u_params.y >= 0  -> the GL "dual-texture" program (fsDualSrc): the screen
//                       texture multiplied by the overlay color gel.
//                         mode 0 (BW)    : screen * overlay
//                         mode 1 (color) : min(screen * overlay * 2, 1)
//                       u_xform maps screen-space UVs to overlay-space UVs
//                       (the overlay may cover only part of the screen, e.g.
//                       the Clowns balloon strips); outside the overlay the
//                       gel reads white = multiply identity.
//
// The vertex stage is screen_quad_rect_vk.vert, reused verbatim (it only
// touches set 0 binding 0, the ortho UBO). u_overlay is bound to the screen
// texture itself when no overlay exists, so the descriptor is always valid;
// the branch below never samples it in that case.
//
// UNORM in, UNORM out: gamma-space byte math end to end, like the GL chain.
//
// Compile:
//   glslc layout_quad_vk.frag -o shaders/vk/layout_quad_vk.frag.spv
// -----------------------------------------------------------------------------

#version 450

layout(location = 0) in vec2 v_uv;

layout(set = 0, binding = 1) uniform sampler2D u_screen;
layout(set = 0, binding = 2) uniform sampler2D u_overlay;

layout(push_constant) uniform PC
{
    vec4 u_xform;    // (scaleU, scaleV, offsetU, offsetV) - screen UV -> overlay UV
    vec4 u_params;   // x = alpha, y = overlay mode (<0 = none, 0 = 1x, 1 = 2x)
} pc;

layout(location = 0) out vec4 o_color;

void main()
{
    vec4 c = texture(u_screen, v_uv);
    vec3 result = c.rgb;

    if (pc.u_params.y >= 0.0)
    {
        vec2 ovUV = v_uv * pc.u_xform.xy + pc.u_xform.zw;
        vec3 ovColor;
        if (ovUV.x < 0.0 || ovUV.x > 1.0 || ovUV.y < 0.0 || ovUV.y > 1.0)
            ovColor = vec3(1.0);   // outside overlay = white = multiply identity
        else
            ovColor = texture(u_overlay, ovUV).rgb;

        if (pc.u_params.y >= 0.5)
            result = min(c.rgb * ovColor * 2.0, vec3(1.0));
        else
            result = c.rgb * ovColor;
    }

    o_color = vec4(result, c.a * pc.u_params.x);
}
