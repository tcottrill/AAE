// vector_post_vk.vert - shared quad vertex shader for the vector post chain
// (Phase 4a Plan 7). No vertex buffer: gl_VertexIndex expands a 4-vertex
// TRIANGLE_STRIP; rect/UVs come from push constants.
//
// Coordinate convention (differs from the other VK subsystems on purpose):
// VectorPostVK records with a STANDARD (positive-height) viewport and Y-DOWN
// target-pixel rects, so a copy with identity UVs is image-row-preserving
// (source row r lands in dest row r). That keeps every intermediate pass
// (trail accumulate, glow downsample/ping-pong) orientation-neutral; the ONE
// vertical flip of the chain (GL's flip_v=true fbo1->fbo4 quad) is expressed
// as a V-swap in the composite draw's uvrect.
#version 450

layout(push_constant) uniform Push {
    vec4 rect;    // x0,y0,x1,y1 in target pixels, y-down
    vec4 tsize;   // target width, height, unused, unused
    vec4 uvrect;  // u,v at rect min corner; u,v at rect max corner
    vec4 tint;
    vec4 params;
} pc;

layout(location = 0) out vec2 vUV;

void main()
{
    // 0:(0,0) 1:(1,0) 2:(0,1) 3:(1,1) - TRIANGLE_STRIP
    vec2 c = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    vec2 pos = mix(pc.rect.xy, pc.rect.zw, c);
    vUV = mix(pc.uvrect.xy, pc.uvrect.zw, c);
    vec2 ndc = pos / pc.tsize.xy * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
