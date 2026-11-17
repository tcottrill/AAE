// hdr_output_vk.frag - final HDR10 encode pass.
//
// Reads the float scene image (gamma-encoded values, 1.0 = paper white, values
// above 1.0 allowed) and writes PQ-encoded BT.2020 to the 10-bit swapchain.
//
//   lin  = sign(v) * pow(abs(v), 2.2)
//   nits = lin * paperWhite, clamped 0..maxNits
//   BT.709 -> BT.2020 primaries, then ST.2084 (PQ) inverse EOTF.
//
// The PQ constants mirror hdr_math.h (hdr::PqEncode); keep them in sync.
#version 450

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(push_constant) uniform Push {
    float paperWhite;   // nits that scene value 1.0 maps to
    float maxNits;      // upper clamp, nits (display max, or beam peak)
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 PqEncode(vec3 nits)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;

    vec3 y  = clamp(nits, vec3(0.0), vec3(10000.0)) / 10000.0;
    vec3 ym = pow(y, vec3(m1));
    return pow((c1 + c2 * ym) / (1.0 + c3 * ym), vec3(m2));
}

void main()
{
    vec3 v = texture(uScene, vUV).rgb;

    vec3 lin  = sign(v) * pow(abs(v), vec3(2.2));
    vec3 nits = clamp(lin * pc.paperWhite, vec3(0.0), vec3(pc.maxNits));

    // BT.709 -> BT.2020 (rows are output R, G, B). GLSL mat3 is column-major,
    // so the matrix is written transposed and applied as M * v.
    const mat3 kBt709ToBt2020 = mat3(
        0.6274, 0.0691, 0.0164,
        0.3293, 0.9195, 0.0880,
        0.0433, 0.0114, 0.8956);
    vec3 rec2020 = kBt709ToBt2020 * nits;

    outColor = vec4(PqEncode(rec2020), 1.0);
}
