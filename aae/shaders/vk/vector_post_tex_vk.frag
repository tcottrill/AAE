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
    vec4 tsize;
    vec4 uvrect;
    vec4 tint;
    vec4 params;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 t = texture(colorMap, vUV);

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

    FragColor = c;
}
