//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// fast_poly_vk.h - Vulkan fast-poly quad renderer.
//
// The Vulkan twin of AAE's GL Fpoly (aae/aae/vidhrdwr/fast_poly.h): batches
// axis-aligned colored quads into one instanced draw. Global-scope names are
// renamed so both can coexist in one build: Fpoly -> FpolyVK, _fpdata ->
// _fpdataVK, include guard __FPOLY__ -> __FPOLY_VK__.
//
// Beyond the GL version it carries an optional viewport/scissor rect override
// (SetViewportRect / ClearViewportRect) used by the VK chain for aspect-fit
// letterboxing.
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
// -----------------------------------------------------------------------------

#pragma once

#ifndef __FPOLY_VK__
#define __FPOLY_VK__

#include <vector>
#include <cstdint>

#include "colordefs.h"
#include "MathUtils.h"
#include "sys_vk.h"

// Represents a colored vertex (matches the legacy layout)
class _fpdataVK {
public:
    float x = 0.0f;
    float y = 0.0f;
    uint32_t color = 0;

    _fpdataVK() = default;
    _fpdataVK(float ix, float iy, uint32_t icolor) : x(ix), y(iy), color(icolor) {}
    _fpdataVK(const aae::math::vec2& p, const uint32_t& icolor) : x(p.x), y(p.y), color(icolor) {}
};

struct FastPolyVKCreateInfo
{
    const char* vertSpvPath = "shaders/fast_poly_vk.vert.spv";
    const char* fragSpvPath = "shaders/fast_poly_vk.frag.spv";

    // If true, we flip viewport height negative (OpenGL-style bottom-left).
    // If false, we keep normal Vulkan viewport and still use bottom-left ortho math.
    bool flipViewportY = true;

    // Initial VBO capacity in vertices (grows automatically).
    uint32_t initialCapacityVerts = 8192;

    // Color attachment format to build the pipeline against. VK_FORMAT_UNDEFINED
    // (default) means "use ctx.swapchainFormat", which is what the current
    // caller (vulkan_renderer.cpp) wants - it composites straight to the
    // swapchain. A caller rendering into an offscreen RenderTargetVK passes
    // that target's format here instead.
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
};

class FpolyVK {
public:
    FpolyVK();
    ~FpolyVK();

    // -------------------------------------------------------------------------
    // Vulkan lifecycle
    // -------------------------------------------------------------------------
    bool Init(VkContext& ctx, int surfaceW, int surfaceH, const FastPolyVKCreateInfo* ci = nullptr);
    void Shutdown(VkContext& ctx);

    void SetSurfaceSize(int surfaceW, int surfaceH);

    // -------------------------------------------------------------------------
    // API compatible with your old usage
    // -------------------------------------------------------------------------
    void addPoly(float x, float y, float size, uint32_t color);

    // Optional viewport/scissor override (letterboxed aspect fit). When set,
    // Render uses this rect instead of the full swapchain. Coordinates are
    // swapchain pixels, y-down (Vulkan viewport space).
    void SetViewportRect(int x, int y, int w, int h)
    {
        m_vpX = x; m_vpY = y; m_vpW = w; m_vpH = h; m_vpOverride = true;
    }
    void ClearViewportRect() { m_vpOverride = false; }

    // Records an overlay pass into the SWAPCHAIN image.
    // Call this AFTER your fullscreen pass (and before VK_EndFrame).
    void Render(VkContext& ctx,
        VkCommandBuffer cmd,
        uint32_t imageIndex,
        uint32_t frameIndex,
        bool clear,
        float clearR, float clearG, float clearB, float clearA);

private:
    // CPU vertices
    std::vector<_fpdataVK> vertices;

private:
    // -------------------------------------------------------------------------
    // GPU objects
    // -------------------------------------------------------------------------
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipeLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline = VK_NULL_HANDLE;

    VkDescriptorPool      m_descPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descSets[VkContext::kFramesInFlight]{};

    VkBuffer              m_ubo[VkContext::kFramesInFlight]{};
    VkDeviceMemory        m_uboMem[VkContext::kFramesInFlight]{};
    void* m_mappedUBO[VkContext::kFramesInFlight]{};

    // Per-frame-slot VBOs: a single persistently-mapped
    // VBO memcpy'd every frame races the previous frame's in-flight GPU read
    // (kFramesInFlight=2; VK_BeginFrame's fence wait only proves frame N-2 is
    // done, not N-1, for the OTHER slot). Each slot owns its own buffer, so
    // frame N only ever touches m_vbo[frameIndex], never a slot the GPU may
    // still be reading. When a slot's buffer must grow, the old buffer is
    // pushed onto that slot's stale list instead of being destroyed
    // immediately (the GPU may still be reading it from the in-flight
    // submission that used it); the stale list is drained at the top of
    // Render() for that slot, which is only reached after VK_BeginFrame's
    // fence wait has proven the GPU is done with this slot's previous frame.
    //
    // The append-discipline / per-slot write-head pattern VectorDrawVK uses is
    // NOT needed here: Render() is called at most once per frame per FpolyVK
    // instance (see vulkan_renderer.cpp's single vkchain_render call site), so
    // there is no intra-frame second flush to race against the first. If a
    // change ever calls Render() more than once per frame for the same
    // instance, adopt that write-head append pattern (track a per-slot vertex
    // write offset, bias firstVertex in the draw call) instead of the current
    // "upload at offset 0, draw, clear" behavior.
    VkBuffer       m_vbo[VkContext::kFramesInFlight]{};
    VkDeviceMemory m_vboMem[VkContext::kFramesInFlight]{};
    void*          m_mappedVBO[VkContext::kFramesInFlight]{};
    uint32_t       m_vboCapacityVerts[VkContext::kFramesInFlight]{};

    struct StaleBuffer { VkBuffer buf; VkDeviceMemory mem; void* mapped; };
    std::vector<StaleBuffer> m_staleBuffers[VkContext::kFramesInFlight];

    // Pipeline color attachment format: VK_FORMAT_UNDEFINED resolves to
    // ctx.swapchainFormat at Init time.
    VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;

    // State
    int  m_surfaceW = 0;
    int  m_surfaceH = 0;
    bool m_flipViewportY = true;

    // Viewport/scissor override (letterbox rect), swapchain pixels, y-down.
    int  m_vpX = 0;
    int  m_vpY = 0;
    int  m_vpW = 0;
    int  m_vpH = 0;
    bool m_vpOverride = false;

private:
    // Helpers
    static void MakeOrtho(float l, float r, float b, float t, float* out16_colMajor);
    static bool ReadFileBytes(const char* path, std::vector<uint8_t>& outBytes);
    static VkShaderModule CreateShaderModuleFromFile(VkContext& ctx, const char* path);

    static uint32_t FindMemoryTypeIdx(VkContext& ctx, uint32_t typeBits, VkMemoryPropertyFlags flags);

    static bool CreateBuffer(VkContext& ctx,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags memFlags,
        VkBuffer& outBuf,
        VkDeviceMemory& outMem,
        void** outMapped);

    static void DestroyBuffer(VkContext& ctx, VkBuffer& buf, VkDeviceMemory& mem, void** mapped);

    static void CmdSwapchainBarrier(VkContext& ctx,
        VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout);

    bool EnsureVBOCapacity(VkContext& ctx, uint32_t frameIndex, uint32_t wantVerts);
    void DrainStaleBuffers(VkContext& ctx, uint32_t frameIndex);
    void UpdateGlobals(VkContext& ctx, uint32_t frameIndex);
};

#endif // __FPOLY_VK__
