// vector_line_vk.vert
// Instanced, butt-capped, coverage-AA beam segment. One instance per BeamLine;
// the 4-vertex strip quad comes from gl_VertexIndex. Port of the GL vsLine.
#version 450

// Per-instance segment (VK_VERTEX_INPUT_RATE_INSTANCE).
layout(location = 0) in vec2  inP0;
layout(location = 1) in vec2  inP1;
layout(location = 2) in float inHalf;
layout(location = 3) in vec4  inColor;   // R8G8B8A8_UNORM -> [0,1]

// Shared push block (identical in every vector_*_vk shader so they can share one
// pipeline layout). Each shader reads only the fields it needs.
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

layout(location = 0) out vec2  vLocal;   // x = longitudinal, y = perpendicular
layout(location = 1) out float vLen;
layout(location = 2) out float vHalf;
layout(location = 3) out vec4  vColor;

const vec2 kQuad[4] = vec2[](vec2(0,-1), vec2(1,-1), vec2(0,1), vec2(1,1));

void main() {
    vec2  d   = inP1 - inP0;
    float len = length(d);
    vec2  dir = (len > 0.0001) ? d / len : vec2(1.0, 0.0);
    vec2  nrm = vec2(-dir.y, dir.x);
    vec2  q   = kQuad[gl_VertexIndex];
    float along = q.x * (len + 2.0 * pc.uAA) - pc.uAA;   // butt cap + feather past ends
    float perp  = q.y * (inHalf + pc.uAA);
    vec2  pos   = inP0 + dir * along + nrm * perp;
    gl_Position = pc.uProj * vec4(pos, 0.0, 1.0);
    vLocal = vec2(along, perp);
    vLen = len; vHalf = inHalf; vColor = inColor;
}
