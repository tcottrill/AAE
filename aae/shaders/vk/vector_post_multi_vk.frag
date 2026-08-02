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
    vec4 result = texture(beamMap, vUV);
    result += texture(glowMap,  vUV) * pc.params.x * pc.params.z;
    result += texture(trailMap, vUV) * 0.25 * pc.params.y;
    FragColor = result;
}
