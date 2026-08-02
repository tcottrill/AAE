// vector_shot_tex_vk.frag - textured vector shots (Plan 9): VK port of the GL
// texColorFrag (shader_definitions.h). texture * per-vertex color (the old
// GL_MODULATE) with the radial edge fade that kills the square halo on the
// additive blend: full brightness inside fade.x, ramping to 0 by fade.y.
#version 450

layout(set = 0, binding = 0) uniform sampler2D u_texture;

layout(push_constant) uniform Push {
    mat4 uProj;
    vec4 fade;    // x = inner radius (0=center..1=edge), y = outer radius
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 c = texture(u_texture, vUV) * vColor;
    float d = length(vUV - vec2(0.5)) * 2.0;   // 0 center, 1 edge, ~1.41 corner
    float fade = 1.0 - smoothstep(pc.fade.x, pc.fade.y, d);
    FragColor = vec4(c.rgb * fade, c.a);
}
