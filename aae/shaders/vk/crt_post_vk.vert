// crt_post_vk.vert - shared quad vertex shader for the RASTER CRT post chain
// (mono monitor, color monitor, tiled scanline multiply). VK port of the GL
// monoMonitorVert / scanlineMultiplyVert pair, both of which are trivial
// ortho + UV passthroughs; here the quad comes from gl_VertexIndex (no vertex
// buffer) and the rect/UVs come from push constants.
//
// Coordinate convention (same as vector_post_vk.vert): the recorder uses a
// STANDARD (positive-height) viewport and Y-DOWN target-pixel rects, so
// rect.y0 is the TOP edge and uvrect.xy is the UV at that top-left corner.
//
// Push block is 128 bytes exactly - the Vulkan guaranteed minimum for
// maxPushConstantsSize. Do not add fields without moving to a UBO.
#version 450

layout(push_constant) uniform Push {
    vec4 rect;    //   0: x0,y0,x1,y1 in target pixels, y-down
    vec4 tsize;   //  16: target width, target height, fragOffX, fragOffY
    vec4 uvrect;  //  32: u,v at rect min corner ; u,v at rect max corner
    vec4 p0;      //  48: srcW, srcH, lodBias, blurH
    vec4 p1;      //  64: blurV, converge, halation, halRadius
    vec4 p2;      //  80: scanline, contrast, bright, saturation
    vec4 p3;      //  96: maskType, maskStrength, maskScale, unused
    vec4 tint;    // 112: phosphor tint rgb, unused
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
