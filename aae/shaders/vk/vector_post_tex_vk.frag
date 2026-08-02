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
    FragColor = texture(colorMap, vUV) * pc.tint;
}
