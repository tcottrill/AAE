// -----------------------------------------------------------------------------
// fast_poly_vk.h - Vulkan fast-poly quad renderer (Phase 4a Plan 3, Task 2).
//
// Imported from the Bosconian donor (Bosconian/sys_graphics/fast_poly.{h,cpp})
// with global-scope names renamed to avoid collisions with AAE's GL Fpoly
// (aae/aae/vidhrdwr/fast_poly.h): Fpoly -> FpolyVK, _fpdata -> _fpdataVK,
// include guard __FPOLY__ -> __FPOLY_VK__. The donor's header-scope
// "using namespace aae::math;" is dropped (vec2 is qualified instead) so
// this header does not pollute translation units that include it.
//
// One addition over the donor: an optional viewport/scissor rect override
// (SetViewportRect / ClearViewportRect) used by the VK chain for aspect-fit
// letterboxing. Everything else is donor-verbatim.
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

    VkBuffer              m_vbo = VK_NULL_HANDLE;
    VkDeviceMemory        m_vboMem = VK_NULL_HANDLE;
    void* m_mappedVBO = nullptr;
    uint32_t              m_vboCapacityVerts = 0;

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

    bool EnsureVBOCapacity(VkContext& ctx, uint32_t wantVerts);
    void UpdateGlobals(VkContext& ctx, uint32_t frameIndex);
};

#endif // __FPOLY_VK__
