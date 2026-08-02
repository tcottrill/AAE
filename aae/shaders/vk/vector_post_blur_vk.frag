// vector_post_blur_vk.frag - port of the GL fragBlur (shader_definitions.h):
// 3x3 gaussian-ish kernel (/17 weights) with a 1.12 gain, sample offsets are
// 1/params.xy of the SOURCE's UV space (the GL chain passes the TARGET dims
// here - 512 for the first downsample, 256 for the rest - making the physical
// blur radius resolution-independent). Used by both the no-blend downsample
// pipeline and the SRC_ALPHA/ONE additive ping-pong accumulate pipeline.
#version 450

layout(set = 0, binding = 0) uniform sampler2D colorMap;

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 tsize;
    vec4 uvrect;
    vec4 tint;
    vec4 params;  // x = width, y = height (GL fragBlur uniforms)
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

void main()
{
    float step_w = 1.0 / pc.params.x;
    float step_h = 1.0 / pc.params.y;

    vec2 offset[9] = vec2[](
        vec2(-step_w, -step_h), vec2(0.0, -step_h), vec2(step_w, -step_h),
        vec2(-step_w, 0.0),     vec2(0.0, 0.0),     vec2(step_w, 0.0),
        vec2(-step_w, step_h),  vec2(0.0, step_h),  vec2(step_w, step_h)
    );

    float kernel_[9] = float[](
        1.0/17.0, 2.0/17.0, 1.0/17.0,
        2.0/17.0, 4.0/17.0, 2.0/17.0,
        1.0/17.0, 2.0/17.0, 1.0/17.0
    );

    // GL RGB8 semantics (gl_fbo.cpp: fbo1/2/3 have NO alpha channel, so GL
    // texture reads return a=1.0): force each tap's alpha to 1. This makes
    // the ping-pong SRC_ALPHA/ONE accumulate effectively full-strength
    // ONE/ONE, exactly like GL - sampling the RGBA8 VK RT's real (feathered,
    // mostly-zero) beam alpha instead had the glow accumulating a*a-weak.
    vec4 sum = vec4(0.0);
    for (int i = 0; i < 9; i++)
        sum += vec4(texture(colorMap, vUV + offset[i]).rgb, 1.0) * kernel_[i];

    FragColor = sum * 1.12;
}
