// vector_post_tex_vk.frag - plain textured quad * tint. Used by the trail
// (phosphor persistence) pipeline: the GL chain draws img1b into img1c with
// glColor(tr,tg,tb,ta) and blend ONE_MINUS_DST_COLOR/SRC_ALPHA; the tint here
// is that glColor, the blend lives in the pipeline state.
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
    // GL RGB8 semantics (gl_fbo.cpp: img1b has NO alpha channel, reads a=1):
    // force the sample's alpha to 1 so the trail blend's SRC_ALPHA dst
    // factor is exactly tint.a (the decay constant) EVERYWHERE. Using the
    // VK RT's real beam alpha instead multiplied the old trail by ~0
    // wherever no beam drew this frame - erasing the phosphor instantly.
    FragColor = vec4(texture(colorMap, vUV).rgb, 1.0) * pc.tint;
}
