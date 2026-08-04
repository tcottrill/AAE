//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// screen_quad_vk.h - Textured-rect quad renderer, Vulkan 1.3 dynamic rendering.
//
// The single entry point is RecordRect(...): it draws a textured rect at
// caller screen-pixel coords into whatever dynamic-rendering pass is currently
// open, building the pipeline variant lazily against VK_ActiveColorFormat(ctx).
// Y-flipped viewport (house convention); rect coords are y-up (bottomPx <
// topPx). It is self-contained - it owns its pipeline variants (one per active
// color format), descriptor pool, and per-frame VBO/UBO ring - alongside
// OnFrameBegin (per-frame slot-cursor reset) and Shutdown.
//
// This is what the VK chain composites with wherever the GL chain would blit a
// textured quad: the game RT to the swapchain letterbox rect, GUI solid quads,
// UI dim rects. SPV paths come from ScreenQuadVKCreateInfo (explicit
// exe-relative paths, "shaders/vk/...", same convention as
// FastPolyVKCreateInfo).
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// ASCII-only comments.
// -----------------------------------------------------------------------------

#pragma once
#ifndef SCREEN_QUAD_VK_H
#define SCREEN_QUAD_VK_H

#include "sys_vk.h"
#include "colordefs.h"
#include <stdint.h>
#include <string>

// -----------------------------------------------------------------------------
// SQBlendVK
// Blend state for a RecordRect draw. The pipeline variant cache is keyed by
// (active color format, blend mode), so a new mode costs one extra pipeline
// the first time it is used and nothing after that.
//
//   Alpha      - SRC_ALPHA / ONE_MINUS_SRC_ALPHA, the default.
//   None       - blending disabled, a straight RGBA copy. This is GL's
//                end_render_fbo4 blit (glDisable(GL_BLEND) + screen_rect),
//                used by the rotated vector output-RT blit.
//   PremulOver - ONE / ONE_MINUS_SRC_ALPHA. GL's rotated raster overlay blit
//                (opengl_renderer.cpp final_render_raster): the overlay was
//                composited into a transparent canvas with premultiplied
//                alpha, so its RGB must not be re-multiplied by SRC_ALPHA.
// -----------------------------------------------------------------------------
enum class SQBlendVK : int
{
    Alpha      = 0,
    None       = 1,
    PremulOver = 2,
    Count      = 3
};

// -----------------------------------------------------------------------------
// ScreenQuadVKCreateInfo
// Explicit SPV paths, matching the FastPolyVKCreateInfo convention. The
// defaults point at the CustomBuild output location next to the exe.
// -----------------------------------------------------------------------------
struct ScreenQuadVKCreateInfo
{
    const char* vertSpvPath = "shaders/vk/screen_quad_rect_vk.vert.spv";
    const char* fragSpvPath = "shaders/vk/screen_quad_rect_vk.frag.spv";
};

// -----------------------------------------------------------------------------
// ScreenQuadVK
// Rect-aware textured quad recorder: per-format pipeline variants, a 64-slot
// per-frame VBO + descriptor ring, and an ortho UBO per frame slot.
// -----------------------------------------------------------------------------
class ScreenQuadVK
{
public:
    // Vertex format for the rect path: screen-pixel position + UV.
    struct QuadVertex
    {
        float x, y; // screen-pixel space (caller's coord frame)
        float u, v; // UV
    };

    ScreenQuadVK() = default;
    ~ScreenQuadVK() = default;

    bool Init(VkContext& ctx, const ScreenQuadVKCreateInfo* ci = nullptr);
    void Shutdown(VkContext& ctx);

    // -------------------------------------------------------------------------
    // RecordRect
    //
    // Records a textured-rect draw into the currently-active render pass.
    // Builds the necessary state (viewport sized for the active framebuffer,
    // ortho UBO matching the caller's coord frame, sampler binding pointing
    // at the caller's texture) and emits 6 verts.
    //
    // Parameters:
    //   ctx          - Vulkan context (provides device + active color format)
    //   cmd          - command buffer in recording state, inside dynamic rendering
    //   frameIndex   - current frame slot (ctx.frameIndex at record time)
    //   srcView      - VkImageView of the source texture (must be in
    //                  SHADER_READ_ONLY_OPTIMAL layout when sampled)
    //   srcSampler   - VkSampler to sample srcView with
    //   leftPx,
    //   bottomPx,
    //   rightPx,
    //   topPx        - destination rect in caller's screen-pixel coord frame.
    //                  y-up convention (bottomPx < topPx).
    //   targetWidth,
    //   targetHeight - dimensions of the active framebuffer in pixels. Used
    //                  for the viewport AND the ortho. Caller provides since
    //                  we may be drawing into an offscreen RT at a different
    //                  size than the swapchain.
    //   flipUV_Y     - if true, swaps source V coords so the sampled image
    //                  appears vertically flipped relative to the default
    //                  orientation. Independent of the always-y-flipped
    //                  viewport.
    //   tint         - per-call RGBA tint (default RGB_WHITE = identity).
    //                  Multiplied with the sampled texel in the fragment
    //                  shader via a fragment-stage push constant.
    //   uvRotation   - display-time rotation of the SOURCE image inside the
    //                  destination rect, using the GL chain's Rect2 index
    //                  (texrect.cpp UpdateScreenRect's indices[32] table):
    //                    0 = none, 1 = rotate right (CW 90),
    //                    2 = rotate left (CCW 90), 3 = 180.
    //                  Implemented as a per-corner UV permutation on the CPU
    //                  vertices - no shader involvement - so 0 emits the
    //                  unrotated UVs. The caller is responsible for giving the
    //                  rect the ROTATED aspect; this only turns the sampled
    //                  image.
    //   blend        - pipeline blend state (see SQBlendVK), default Alpha.
    //
    // The pipeline used by RecordRect is owned by ScreenQuadVK; a variant is
    // created lazily per (VK_ActiveColorFormat(ctx), blend).
    // -------------------------------------------------------------------------
    void RecordRect(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                    VkImageView srcView, VkSampler srcSampler,
                    float leftPx, float bottomPx, float rightPx, float topPx,
                    uint32_t targetWidth, uint32_t targetHeight,
                    bool flipUV_Y = false,
                    rgb_t tint = RGB_WHITE,
                    int uvRotation = 0,
                    SQBlendVK blend = SQBlendVK::Alpha);

    // Deterministic per-frame slot-cursor reset. Call once per frame after
    // VK_BeginFrame. The lazy reset keyed on "frameIndex changed since last
    // call" is kept as a fallback but breaks for callers that only draw on
    // same-parity frames (cursor never reset -> slot exhaustion), so prefer
    // the explicit call.
    void OnFrameBegin(uint32_t frameIndex);

private:
    bool RectInit_(VkContext& ctx);
    void RectShutdown_(VkContext& ctx);
    bool RectBuildPipelineForFormat_(VkContext& ctx, VkFormat colorFormat,
                                     SQBlendVK blend, VkPipeline& outPipeline);
    VkPipeline RectGetOrCreatePipeline_(VkContext& ctx, SQBlendVK blend);

    bool CreateBuffer(VkContext& ctx,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags memProps,
        VkBuffer& outBuf,
        VkDeviceMemory& outMem);

    uint32_t FindMemoryType(VkContext& ctx, uint32_t typeBits, VkMemoryPropertyFlags props);

private:
    // SPV paths captured from CreateInfo at Init (pipeline variants are
    // built lazily, so the paths must outlive Init).
    std::string rect_vertSpvPath_ = "shaders/vk/screen_quad_rect_vk.vert.spv";
    std::string rect_fragSpvPath_ = "shaders/vk/screen_quad_rect_vk.frag.spv";

    // Max RecordRect calls per frame. Per-slot cost is small (6 verts + 1
    // descriptor set per frame-in-flight), so 64 is cheap and leaves plenty of
    // slack over the ~15 the heaviest layout composite needs.
    static constexpr uint32_t kRectSlotsPerFrame = 64;

    VkDescriptorSetLayout rect_setLayout_  = VK_NULL_HANDLE;
    VkPipelineLayout      rect_pipeLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      rect_descPool_   = VK_NULL_HANDLE;

    // Per-(format, blend) pipeline variants. RecordRect draws into whatever
    // pass is open -- the swapchain pass for composites, an offscreen RT pass
    // for post-processing. Dynamic rendering demands a pipeline whose declared
    // format matches the pass, so variants are created lazily per
    // (VK_ActiveColorFormat(ctx), blend) and only destroyed at shutdown (no
    // in-flight-destroy hazard). Sized for the observed formats (swapchain +
    // the RGBA8_UNORM game RT) times the three blend modes.
    static constexpr uint32_t kRectMaxPipelineVariants = 8;
    VkFormat   rect_pipeFormat_[kRectMaxPipelineVariants]{};
    SQBlendVK  rect_pipeBlend_[kRectMaxPipelineVariants]{};
    VkPipeline rect_pipeVariant_[kRectMaxPipelineVariants]{};
    uint32_t   rect_pipeVariantCount_ = 0;

    // Per-frame VBO holds (kRectSlotsPerFrame * 6) verts. Each RecordRect
    // call writes 6 verts at slot N's offset and binds the VBO at that
    // offset. Avoids races on still-in-flight reads from earlier slots.
    VkBuffer       rect_vbo_[VkContext::kFramesInFlight]{};
    VkDeviceMemory rect_vboMem_[VkContext::kFramesInFlight]{};
    void*          rect_vboMapped_[VkContext::kFramesInFlight]{};

    // Per-frame UBO ring -- one mat4 per RECT SLOT (kRectSlotsPerFrame of
    // them), each at a device-aligned offset. Written by the RecordRect call
    // that claims the slot, so draws with different target dims can coexist
    // in one frame (the rotated overlay RT + the final swapchain blit).
    VkBuffer       rect_ubo_[VkContext::kFramesInFlight]{};
    VkDeviceMemory rect_uboMem_[VkContext::kFramesInFlight]{};
    void*          rect_uboMapped_[VkContext::kFramesInFlight]{};
    VkDeviceSize   rect_uboStride_ = 0;

    // Pre-allocated descriptor sets. UBO binding pre-written at init;
    // sampler binding written per-call.
    VkDescriptorSet rect_descSets_[VkContext::kFramesInFlight][kRectSlotsPerFrame]{};
    uint32_t        rect_slotCursor_[VkContext::kFramesInFlight]{};

    // Tracks the most recent frameIndex passed to RecordRect, so slot
    // cursors can be lazily reset on frame boundary detection (fi changes
    // means a new frame is starting on that ring slot). 0xFFFFFFFFu = uninitialized.
    uint32_t        rect_lastFrameIndexSeen_ = 0xFFFFFFFFu;
};

#endif // SCREEN_QUAD_VK_H
