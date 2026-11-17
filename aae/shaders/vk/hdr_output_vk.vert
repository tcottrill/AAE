// hdr_output_vk.vert - fullscreen triangle for the HDR10 output pass.
// No vertex buffer: gl_VertexIndex 0,1,2 expands to a triangle covering the
// whole target. UV (0,0) is the top-left of the scene image, matching the
// Vulkan framebuffer origin, so the copy is row-preserving.
#version 450

layout(location = 0) out vec2 vUV;

void main()
{
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
