// vector_line_vk.frag
// Coverage-AA fill: half coverage exactly at the geometric edge so the rendered
// width matches GL_LINES. Port of the GL fsLine (linear coverage, no pow()).
#version 450

layout(location = 0) in vec2  vLocal;
layout(location = 1) in float vLen;
layout(location = 2) in float vHalf;
layout(location = 3) in vec4  vColor;

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
    float covPerp = clamp((vHalf - abs(vLocal.y)) / pc.uAA + 0.5, 0.0, 1.0);
    float covEnd0 = clamp((vLocal.x) / pc.uAA + 0.5, 0.0, 1.0);
    float covEnd1 = clamp((vLen - vLocal.x) / pc.uAA + 0.5, 0.0, 1.0);
    float cov = covPerp * covEnd0 * covEnd1;
    if (cov <= 0.0) discard;
    frag = vec4(vColor.rgb, vColor.a * cov);
}
