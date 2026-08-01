// vector_disc_vk.vert
// Round join / end-cap disc centred on a shared vertex. radius = inHalf * uStrength
// (radius is normally baked per-disc, so uStrength = 1.0). Port of the GL vsJoin.
#version 450

layout(location = 0) in vec2  inCenter;
layout(location = 1) in float inHalf;
layout(location = 2) in vec4  inColor;

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

layout(location = 0) out vec2  vLocal;
layout(location = 1) out float vRad;
layout(location = 2) out vec4  vColor;

const vec2 kQuad[4] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));

void main() {
    float r = inHalf * pc.uStrength;
    vec2 q   = kQuad[gl_VertexIndex];
    vec2 ext = q * (r + pc.uAA);
    gl_Position = pc.uProj * vec4(inCenter + ext, 0.0, 1.0);
    vLocal = ext; vRad = r; vColor = inColor;
}
