// vector_post_tex_vk.frag - plain textured quad * tint. Used by the trail
// (phosphor persistence) pipeline: the GL chain draws img1b into img1c with
// glColor(tr,tg,tb,ta) and blend ONE_MINUS_DST_COLOR/SRC_ALPHA; the tint here
// is that glColor, the blend lives in the pipeline state.
//
// Also serves every ARTWORK quad of the layered composite (backdrop, color
// overlay, bezel, frame-RT draws - see VectorPostVK::RecordCompositeLayered):
//   params.z = alpha test threshold (GL fragBasicTex uAlphaTest: discard when
//              the TINTED alpha is below it; 0 disables). Bezel uses 0.2.
//   params.w = 1 -> keep the texture's REAL alpha (GL samples RGBA8 art
//              through fragBasicTex with true alpha - the backdrop/overlay2
//              blends depend on it). 0 -> force alpha 1 (GL RGB8 semantics
//              for RT sources; the trail pass and frame-RT draws rely on it).
#version 450

layout(set = 0, binding = 0) uniform sampler2D colorMap;

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 tsize;   // x,y = target size (vertex stage); z = HDR k, w = HDR curve
    vec4 uvrect;
    vec4 tint;
    vec4 params;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

// HDR highlight expansion for the two frame-RT draws of the layered composite
// (CPU side: VectorPostVK::SetHdrPush). tsize.w > 0 selects it: the sample is
// clamped below at 0 only (the float frame RT may hold > 1.0) and, when
// tsize.z (k) > 1, expanded BEFORE the tint multiplies it. tsize.w <= 0 (every
// other draw, and all of SDR) keeps the original 0..1 sample clamp.
// One gain from the brightest channel is applied to all three (preserves hue).
// Keep in sync with hdr::ExpandCrtRgb (hdr_math.h) and vector_post_multi_vk.frag.
vec3 expand_crt(vec3 v, float k, float curve)
{
    float m = max(v.r, max(v.g, v.b));
    float g = 1.0 + (k - 1.0) * pow(clamp(m, 0.0, 1.0), curve);
    return v * pow(g, 1.0 / 2.2);
}

void main()
{
    // HDR: the sources this reads (beam RT for the trail, glow/pyramid RTs for
    // the core/tail add, frame RT for the layered composite) are float and can
    // hold values above 1.0 that a UNORM texel never could. Clamp the sample so
    // those passes see exactly what they saw from UNORM. A no-op for UNORM
    // sources and for the 8-bit artwork PNGs, so artwork is unchanged.
    vec4 t;
    if (pc.tsize.w > 0.0)
    {
        t = texture(colorMap, vUV);
        t = vec4(max(t.rgb, vec3(0.0)), clamp(t.a, 0.0, 1.0));
        if (pc.tsize.z > 1.0)
            t.rgb = expand_crt(t.rgb, pc.tsize.z, pc.tsize.w);
    }
    else
    {
        t = clamp(texture(colorMap, vUV), 0.0, 1.0);
    }

    // GL RGB8 semantics (gl_fbo.cpp: img1b has NO alpha channel, reads a=1):
    // force the sample's alpha to 1 so the trail blend's SRC_ALPHA dst
    // factor is exactly tint.a (the decay constant) EVERYWHERE. Using the
    // VK RT's real beam alpha instead multiplied the old trail by ~0
    // wherever no beam drew this frame - erasing the phosphor instantly.
    // Artwork quads opt out via params.w (their PNGs carry real alpha).
    if (pc.params.w < 0.5)
        t.a = 1.0;

    vec4 c = t * pc.tint;

    // GL fragBasicTex parity: `if (c.a < uAlphaTest) discard;` runs on the
    // tinted color. params.z == 0 disables (c.a < 0 is never true in GL).
    if (pc.params.z > 0.0 && c.a < pc.params.z)
        discard;

    // params.x = 1 (the dual-filter core/tail re-injection draw, whose tint
    // carries weights up to core*gain = 10): clamp the output, as a UNORM
    // target does per fragment before blending. Every other caller leaves
    // params.x = 0 on purpose: artwork draws into the swapchain pass (the
    // crt_boost frame-RT draw tints up to 1.25) must keep their unclamped
    // output, which in the HDR scene image is allowed above 1.0.
    if (pc.params.x > 0.5)
        c = clamp(c, 0.0, 1.0);

    FragColor = c;
}
