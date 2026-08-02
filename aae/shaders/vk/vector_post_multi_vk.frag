// vector_post_multi_vk.frag - port of the GL fragMulti composite
// (shader_definitions.h texfragText): beam frame + glow * glowamt +
// trail feedback * 0.25. The GL "brighten" uniform is dead (bval is a
// constant 1.0 there) and is not ported.
#version 450

layout(set = 0, binding = 0) uniform sampler2D beamMap;   // GL mytex2 (img1b)
layout(set = 0, binding = 1) uniform sampler2D glowMap;   // GL mytex3 (img3b)
layout(set = 0, binding = 2) uniform sampler2D trailMap;  // GL mytex4 (img1c)

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 tsize;
    vec4 uvrect;
    vec4 tint;
    vec4 params;  // x = glowamt, y = usefb (0/1), z = useglow (0/1)
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

void main()
{
    // GL RGB8 semantics: all three GL source textures read a=1.0.
    vec3 result = texture(beamMap, vUV).rgb;
    result += texture(glowMap,  vUV).rgb * pc.params.x * pc.params.z;
    result += texture(trailMap, vUV).rgb * 0.25 * pc.params.y;

    // GL parity clamp (GL stores this sum into the RGBA8 fbo4, clamping at
    // 1.0, then blits the bytes unchanged). The swapchain is UNORM (sys_vk
    // CreateSwapchain prefers it precisely so raw byte output displays like
    // GL's non-sRGB window); a briefly-shipped srgb_to_linear cancellation
    // here targeted the old forced-SRGB swapchain and was removed with it.
    FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
