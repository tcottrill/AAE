// vector_post_dual_up_vk.frag - dual-filter pyramid upsample (Kawase/Bjorge),
// the [main] glow_filter=1 path. VK port of the GL dualUpFrag's WIDE half:
// the 8-tap tent kernel. The core/tail re-injection (GL's uAdd sampler) is a
// SEPARATE additive draw here (vector_post_tex_vk.frag + SRC_ALPHA/ONE) so
// this stays on the single-sampler descriptor layout.
//   params.xy = halfpixel (0.5/srcSize * glow2_spread)
//   params.z  = gain (glow2_gain on the final 256 pass, 1.0 in between)
#version 450

layout(set = 0, binding = 0) uniform sampler2D colorMap;

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 tsize;
    vec4 uvrect;
    vec4 tint;
    vec4 params;  // xy = halfpixel, z = gain
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec2 h = pc.params.xy;
    vec3 sum;
    sum  = texture(colorMap, vUV + vec2(-h.x * 2.0, 0.0)).rgb;
    sum += texture(colorMap, vUV + vec2(-h.x,  h.y)).rgb * 2.0;
    sum += texture(colorMap, vUV + vec2(0.0,  h.y * 2.0)).rgb;
    sum += texture(colorMap, vUV + vec2( h.x,  h.y)).rgb * 2.0;
    sum += texture(colorMap, vUV + vec2( h.x * 2.0, 0.0)).rgb;
    sum += texture(colorMap, vUV + vec2( h.x, -h.y)).rgb * 2.0;
    sum += texture(colorMap, vUV + vec2(0.0, -h.y * 2.0)).rgb;
    sum += texture(colorMap, vUV + vec2(-h.x, -h.y)).rgb * 2.0;
    // a = 1: GL RGB8 semantics, same rule as vector_post_blur_vk.frag.
    FragColor = vec4(sum * (1.0 / 12.0) * pc.params.z, 1.0);
}
