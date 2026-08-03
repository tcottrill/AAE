// vector_post_dual_down_vk.frag - dual-filter pyramid downsample (Kawase/
// Bjorge), the [main] glow_filter=1 path. VK port of the GL dualDownFrag in
// shader_definitions.h: 4-weighted centre tap + 4 diagonal taps at half-pixel
// offsets, where bilinear at the pyramid's exact 2:1 ratio averages 4 source
// texels per tap for free.
//   params.xy = halfpixel (0.5/dstSize * glow2_spread)
#version 450

layout(set = 0, binding = 0) uniform sampler2D colorMap;

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 tsize;
    vec4 uvrect;
    vec4 tint;
    vec4 params;  // xy = halfpixel
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec2 h = pc.params.xy;
    vec3 sum = texture(colorMap, vUV).rgb * 4.0;
    sum += texture(colorMap, vUV + h).rgb;
    sum += texture(colorMap, vUV - h).rgb;
    sum += texture(colorMap, vUV + vec2(h.x, -h.y)).rgb;
    sum += texture(colorMap, vUV - vec2(h.x, -h.y)).rgb;
    // a = 1: GL RGB8 semantics, same rule as vector_post_blur_vk.frag.
    FragColor = vec4(sum * (1.0 / 8.0), 1.0);
}
