// vector_disc_vk.frag
// Round coverage-AA disc. uPremult > 0.5 emits premultiplied coverage for the
// additive/color path (paired with VK_BLEND_OP_MAX, so joins fill gaps at the
// line's own brightness and never sum over the lines); otherwise straight
// alpha-over for the B/W path. Port of the GL fsJoin.
#version 450

layout(location = 0) in vec2  vLocal;
layout(location = 1) in float vRad;
layout(location = 2) in vec4  vColor;

layout(push_constant) uniform Push {
    mat4  uProj;
    float uAA;
    float uStrength;
    float uPremult;
    float uCorePower;
    float uBloomPower;
    float uBloomIntensity;
    float uOverdrive;
} pc;

layout(location = 0) out vec4 frag;

void main() {
    float cov = clamp((vRad - length(vLocal)) / pc.uAA + 0.5, 0.0, 1.0);
    if (cov <= 0.0) discard;
    if (pc.uPremult > 0.5)
        frag = vec4(vColor.rgb * cov, cov);       // GL_MAX path
    else
        frag = vec4(vColor.rgb, vColor.a * cov);  // alpha-over (B/W)
}
