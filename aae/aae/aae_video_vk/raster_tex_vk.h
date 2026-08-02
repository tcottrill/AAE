// -----------------------------------------------------------------------------
// raster_tex_vk.h - Per-frame raster game image as a streamed texture.
//
// REPLACES the per-pixel quad emit on the VK raster path. raster_emit_polys()
// visits every source pixel; the old VK sink turned each one into an FpolyVK
// quad, so pacman (288x224 visible) rebuilt and uploaded ~64,500 quads /
// ~258,000 vertices EVERY FRAME. An immediate-mode desktop GPU shrugs that
// off; a tile-based GPU (Raspberry Pi 5 / Mesa v3d) bins every primitive up
// front, which is close to worst case - pacman ran 53 fps there at prescale 1
// with no shaders. That design was inherited from the legacy fixed-function
// GL Fpoly path; Vulkan does not need it.
//
// Instead the emit writes a linear RGBA8 buffer, this class streams it to a
// texture, and the caller draws ONE textured quad into the game RT. 4 verts
// instead of 258,000; ~258 KB of pixels instead of ~3 MB of vertex data.
//
// SHAPE AND PRESCALE
//   The texture is NATIVE-sized (raster_dst_dims, i.e. post-orientation game
//   pixels, NO prescale). The game RT stays PRESCALED, and the single quad
//   covers it with NEAREST magnification - so one source pixel still paints a
//   solid prescale x prescale block, exactly what the quad path produced.
//   See the prescale-equivalence note in vulkan_renderer.cpp.
//
// PIXEL FORMAT
//   The buffer holds MAKE_RGBA values: R in the low byte (r | g<<8 | b<<16 |
//   a<<24). On a little-endian host that is the byte order R,G,B,A, which is
//   VK_FORMAT_R8G8B8A8_UNORM verbatim - no swizzle, no conversion. UNORM
//   only, never _SRGB: the whole chain is gamma-space byte math (commit
//   09ec1bb).
//
// IN-FLIGHT SAFETY
//   Everything the GPU touches is PER FRAME-IN-FLIGHT: one staging buffer and
//   one image per slot. Frame N only ever writes slot ctx.frameIndex, whose
//   previous submission VK_BeginFrame already proved complete by waiting
//   ctx.inFlight[frameIndex]; frame N-1 lives in the other slot. Nothing is
//   created or destroyed per frame - Init/Shutdown bracket the whole game, so
//   there is no allocation churn and no destroy-while-sampled hazard.
//
// PASS DISCIPLINE
//   Upload() records vkCmdCopyBufferToImage, which is ILLEGAL inside a
//   dynamic-rendering pass. Call it with the frame pass suspended and before
//   the game RT's Begin().
//
// License: GPL-3.0-or-later (as the rest of AAE).
// ASCII-only comments.
// -----------------------------------------------------------------------------
#pragma once
#ifndef RASTER_TEX_VK_H
#define RASTER_TEX_VK_H

#include "sys_vk.h"

#include <stdint.h>
#include <vector>

class RasterTexVK
{
public:
    RasterTexVK() = default;
    ~RasterTexVK() = default;
    RasterTexVK(const RasterTexVK&) = delete;
    RasterTexVK& operator=(const RasterTexVK&) = delete;

    // Creates the CPU buffer plus one staging buffer and one image per
    // frame-in-flight, all at w x h. Idempotent only in the sense that a
    // second Init must be preceded by Shutdown.
    bool Init(VkContext& ctx, uint32_t w, uint32_t h);
    void Shutdown(VkContext& ctx);

    bool     IsValid() const { return initialized_; }
    uint32_t GetWidth()  const { return width_; }
    uint32_t GetHeight() const { return height_; }

    // CPU pixel buffer, width_ * height_ RGBA8 texels, TOP-DOWN (row 0 is the
    // game's top scanline). The emit sink writes straight into this.
    uint32_t*       Pixels()       { return pixels_.data(); }
    const uint32_t* Pixels() const { return pixels_.data(); }

    // Fills the whole buffer with one packed RGBA value. Called once per frame
    // before the emit so a source row the emit skips shows the same opaque
    // black the RT clear used to supply, rather than last frame's pixels.
    void ClearPixels(uint32_t rgba);

    // Copies the CPU buffer into slot frameIndex's staging buffer and records
    // the barriers + vkCmdCopyBufferToImage into cmd. MUST be recorded with no
    // dynamic-rendering pass open.
    void Upload(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex);

    // Sampling handles for the slot Upload() was last called on this frame.
    VkImageView View(uint32_t frameIndex) const
    {
        return (frameIndex < VkContext::kFramesInFlight) ? view_[frameIndex] : VK_NULL_HANDLE;
    }
    VkSampler Sampler() const { return sampler_; }

private:
    bool CreateSlot_(VkContext& ctx, uint32_t i);

    bool     initialized_ = false;
    uint32_t width_  = 0;
    uint32_t height_ = 0;

    std::vector<uint32_t> pixels_;

    // Per frame-in-flight staging buffers, host-visible + coherent and
    // persistently mapped. The emit never writes these directly: it fills the
    // cached CPU vector and Upload does one sequential memcpy, because the
    // rotated (SWAP_XY) emit order walks the destination with a stride and
    // scattered writes into write-combined memory are pathologically slow.
    VkBuffer       staging_[VkContext::kFramesInFlight]{};
    VkDeviceMemory stagingMem_[VkContext::kFramesInFlight]{};
    void*          stagingMapped_[VkContext::kFramesInFlight]{};

    // Per frame-in-flight images. Single mip level; NEAREST sampling is what
    // reproduces the quad path's solid prescale blocks.
    VkImage        image_[VkContext::kFramesInFlight]{};
    VkDeviceMemory imageMem_[VkContext::kFramesInFlight]{};
    VkImageView    view_[VkContext::kFramesInFlight]{};
    // False until this slot's image has been written once; the first upload
    // barrier then transitions from UNDEFINED instead of SHADER_READ_ONLY.
    bool           imageLive_[VkContext::kFramesInFlight]{};

    // One shared immutable sampler (NEAREST, clamp, no mips).
    VkSampler sampler_ = VK_NULL_HANDLE;
};

#endif // RASTER_TEX_VK_H
