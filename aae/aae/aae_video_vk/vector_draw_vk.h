//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// vector_draw_vk.h - Vulkan backend for the beam vector renderer.
//
// The mirror of the GL beam_draw_all(): draws the current frame's BeamLine /
// BeamJoin / BeamShot batches as instanced, coverage-AA quads
// (TRIANGLE_STRIP, 4 verts, N instances) into an ALREADY-OPEN dynamic
// rendering pass on the caller's command buffer.
//
// Buffer discipline: per-batch per-frame-slot write heads (uploads append,
// draws pass the base as firstInstance) + stale-buffer retirement on growth,
// drained once per frame in OnFrameBegin after the frame slot's fence wait
// (the same pattern as FpolyVK). Multiple Record calls per frame are safe.
//
// SPV paths are explicit CreateInfo paths ("shaders/vk/..." next to the exe),
// matching FastPolyVKCreateInfo / ScreenQuadVKCreateInfo.
//
// ASCII-only comments.
// -----------------------------------------------------------------------------
#pragma once
#ifndef VECTOR_DRAW_VK_H
#define VECTOR_DRAW_VK_H

#include "sys_vk.h"
#include "vector_draw.h"   // BeamLine / BeamJoin / BeamShot, beam_get_lines/shots, beam_build_caps

#include <vector>

struct VectorDrawVKCreateInfo
{
    const char* lineVertSpv = "shaders/vk/vector_line_vk.vert.spv";
    const char* lineFragSpv = "shaders/vk/vector_line_vk.frag.spv";
    const char* discVertSpv = "shaders/vk/vector_disc_vk.vert.spv";
    const char* discFragSpv = "shaders/vk/vector_disc_vk.frag.spv";
    const char* shotVertSpv = "shaders/vk/vector_shot_vk.vert.spv";
    const char* shotFragSpv = "shaders/vk/vector_shot_vk.frag.spv";

    // Color format of the target the beam draws into (the caller's
    // RenderTarget, or the swapchain when drawing direct).
    // VK_FORMAT_UNDEFINED falls back to ctx.swapchainFormat.
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;

    // Initial per-batch instance capacity; each buffer grows on demand.
    uint32_t initialInstanceCapacity = 4096;

    // Supersample factor of the bound render target: the AA feather pushed to
    // the shaders is config.line_smoothing / ssaa, mirroring the GL
    // beam_draw_all / beam_set_ssaa. 1 for direct-to-swapchain; the SSAA RT
    // passes its factor here.
    int ssaa = 1;

    // Flip the viewport Y (negative height) so the SAME column-major ortho
    // the GL path passes as uProj produces the same on-screen orientation on
    // Vulkan. Without this, a GL-style Y-up ortho under Vulkan's Y-down
    // raster renders every beam vertically mirrored. Matches the house
    // pattern used by every other VK subsystem (sprite, fast_poly,
    // debug_draw, bmfont, winfont). Default ON; turn off only if the caller
    // deliberately supplies a pre-flipped (Y-down) projection.
    bool flipViewportY = true;
};

class VectorDrawVK
{
public:
    VectorDrawVK() = default;
    ~VectorDrawVK() = default;
    VectorDrawVK(const VectorDrawVK&) = delete;
    VectorDrawVK& operator=(const VectorDrawVK&) = delete;

    // Idempotent: a second Init shuts the previous objects down first, so
    // per-game re-init cannot leak pipelines.
    bool Init(VkContext& ctx, const VectorDrawVKCreateInfo* ci = nullptr);
    void Shutdown(VkContext& ctx);

    // Deterministic per-frame reset. Call once per frame right
    // after VK_BeginFrame, whose fence wait has proven the GPU is done with
    // this slot's previous frame: drains the slot's retired (stale) instance
    // buffers and resets the slot's per-batch write heads. Same contract as
    // ScreenQuadVK::OnFrameBegin / FpolyVK's top-of-Render drain.
    void OnFrameBegin(VkContext& ctx, uint32_t frameIndex);

    // Record the current beam batches into an already-open dynamic rendering pass
    // on 'cmd'. 'proj' is the column-major 4x4 ortho (the same matrix the GL path
    // passes as uProj -- no pre-flip needed; see flipViewportY). 'additive'
    // selects the color path (lines additive, joins VK_BLEND_OP_MAX) vs the B/W
    // path (alpha-over).
    //
    // targetWidth/targetHeight are the dimensions of the framebuffer this pass
    // renders into (the RenderTarget's dims when inside an RT pass). Record sets
    // its own viewport/scissor from them, applying the flipViewportY compensation.
    // 0/0 falls back to ctx.swapchainExtent for direct-to-swapchain callers.
    void Record(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                const float proj[16], bool additive,
                uint32_t targetWidth = 0, uint32_t targetHeight = 0);

private:
    enum BatchType { BATCH_LINE = 0, BATCH_JOIN = 1, BATCH_SHOT = 2, BATCH_COUNT = 3 };

    // A retired instance buffer: pushed by EnsureBuffer on growth,
    // destroyed in OnFrameBegin once the slot's fence wait has proven the
    // GPU is done with the submission that referenced it.
    struct StaleBuffer
    {
        VkBuffer       buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        void*          mapped = nullptr;
    };

    bool CreatePipelines(VkContext& ctx, const VectorDrawVKCreateInfo& ci);
    bool EnsureBuffer(VkContext& ctx, int batch, uint32_t frameIndex, VkDeviceSize neededBytes);
    void DrainStaleBuffers(VkContext& ctx, uint32_t frameIndex);

    VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
    uint32_t m_initialCap  = 4096;
    int      m_ssaa        = 1;
    bool     m_flipViewportY = true;

    VkPipelineLayout m_pipeLayout   = VK_NULL_HANDLE;
    VkPipeline       m_pipeLineAdd  = VK_NULL_HANDLE;  // color lines: SRC_ALPHA, ONE
    VkPipeline       m_pipeLineOver = VK_NULL_HANDLE;  // B/W lines:   alpha-over
    VkPipeline       m_pipeDiscMax  = VK_NULL_HANDLE;  // color joins: VK_BLEND_OP_MAX
    VkPipeline       m_pipeDiscOver = VK_NULL_HANDLE;  // B/W joins:   alpha-over
    VkPipeline       m_pipeShotAdd  = VK_NULL_HANDLE;  // shots:       SRC_ALPHA, ONE

    // Per-batch, per-frame-in-flight host-visible instance buffers (auto-grow).
    VkBuffer       m_buf   [BATCH_COUNT][VkContext::kFramesInFlight]{};
    VkDeviceMemory m_mem   [BATCH_COUNT][VkContext::kFramesInFlight]{};
    void*          m_mapped[BATCH_COUNT][VkContext::kFramesInFlight]{};
    VkDeviceSize   m_cap   [BATCH_COUNT][VkContext::kFramesInFlight]{};

    // Per-batch per-slot write head (bytes). Uploads append at the
    // head; draws pass head/stride as firstInstance; OnFrameBegin resets the
    // slot's heads once the fence wait has proven the previous frame done.
    VkDeviceSize   m_head  [BATCH_COUNT][VkContext::kFramesInFlight]{};

    // Buffers retired on growth, per slot, drained in OnFrameBegin.
    std::vector<StaleBuffer> m_stale[VkContext::kFramesInFlight];
};

#endif // VECTOR_DRAW_VK_H
