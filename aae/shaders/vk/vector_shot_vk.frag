// vector_shot_vk.frag
// Radial core + halo. The alpha (vColor.a = clean linear intensity z) re-multiplies
// the profile, squaring it under additive blend -> a sharp core, not a fuzzy ball.
// Always drawn additive (SRC_ALPHA, ONE). Port of the GL fsShot.
#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

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
    float d = distance(vUV, vec2(0.5));
    float g = clamp(1.0 - d * 2.0, 0.0, 1.0);
    float profile = pow(g, pc.uCorePower) + pow(g, pc.uBloomPower) * pc.uBloomIntensity;
    float z = vColor.a;
    frag = vec4(vColor.rgb * pc.uOverdrive * profile, profile * z);
}
