// vector_shot_vk.vert
// Procedural shot/fire point: a screen-aligned quad of half-extent inSize, with a
// [0,1] UV for the radial profile in the fragment shader. Port of the GL vsShot.
#version 450

layout(location = 0) in vec2  inCenter;
layout(location = 1) in float inSize;
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

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

const vec2 kQuad[4] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));

void main() {
    vec2 q = kQuad[gl_VertexIndex];
    gl_Position = pc.uProj * vec4(inCenter + q * inSize, 0.0, 1.0);
    vUV    = q * 0.5 + 0.5;
    vColor = inColor;
}
