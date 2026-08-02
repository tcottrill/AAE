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

    // GL parity clamp + sRGB store-encode cancellation. GL stores this sum
    // into the RGBA8 fbo4 (clamping at 1.0) and blits those bytes to a
    // non-sRGB window unchanged. The VK swapchain is hard-required *_SRGB
    // (sys_vk.cpp CreateSwapchain), so the hardware applies linear->sRGB on
    // store; emitting srgb_to_linear(sum) makes the stored/displayed bytes
    // equal GL's. Without this every mid/low tone is lifted (a 5% feather
    // fringe displays at ~25%) - beams read blown-out, wide, washed.
    result = clamp(result, 0.0, 1.0);
    vec3 lin = mix(result / 12.92,
                   pow((result + 0.055) / 1.055, vec3(2.4)),
                   step(0.04045, result));
    FragColor = vec4(lin, 1.0);
}
