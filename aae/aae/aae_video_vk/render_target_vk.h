//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// render_target_vk.h - Vulkan offscreen render target: the VK equivalent of
// the GL chain's FBO + attached texture (fbo1 / fbo4 / fbo_raster et al. in
// opengl_renderer.cpp). Named RenderTargetVK because AAE's GL side has its own
// fbo types.
//
// Methods take VkContext& and VkCommandBuffer directly, matching the vkchain
// calling style (sys_vk.cpp / fast_poly_vk.cpp). Compositing to the swapchain
// is done by the layout system / ScreenQuadVK.
//
// BEHAVIOR:
//   - Begin(): layout barrier to COLOR_ATTACHMENT_OPTIMAL, vkCmdBeginRendering
//     on the color image, publish ctx.activeColorFormat, viewport/scissor set
//     to the RT dimensions.
//   - End(): vkCmdEndRendering, barrier to SHADER_READ_ONLY_OPTIMAL, reset
//     ctx.activeColorFormat to VK_FORMAT_UNDEFINED.
//   - First-use hardening: the first Begin() on an UNDEFINED-layout image
//     forces clear=true (LOAD_OP_LOAD would read garbage).
//   - CLAMP_TO_EDGE sampler.
// OPTIONAL MIP CHAIN:
//   - CreateInfo.mipLevels: 0 or 1 = single level; -1 = full chain for the
//     dimensions; N > 1 = min(N, full chain).
//   - When mipped, the image adds TRANSFER_SRC|TRANSFER_DST usage, the sampled
//     view covers all levels, and the sampler is LINEAR mag/min with LINEAR
//     mipmap mode (GL_LINEAR_MIPMAP_LINEAR equivalent) and maxLod = mip count.
//   - GenerateMips(ctx, cmd) records a blit-cascade downsample (the same
//     cascade VK_BuildRGBA8Texture uses in sys_vk.cpp, the VK equivalent of
//     glGenerateMipmap), adapted for a render target: level 0 enters in
//     SHADER_READ_ONLY_OPTIMAL
//     (i.e. after End()), and on exit ALL levels are SHADER_READ_ONLY_OPTIMAL.
//
// GenerateMips CONTRACT:
//   - Must be called OUTSIDE any dynamic-rendering pass. The method guards on
//     ctx.activeColorFormat == VK_FORMAT_UNDEFINED and refuses to record
//     otherwise (vkCmdBlitImage is illegal inside a render pass instance).
//   - Must be called after End() (level 0 in SHADER_READ_ONLY_OPTIMAL) and
//     before anything samples mip levels > 0 this frame. The canonical frame
//     shape is: Begin -> draws -> End -> GenerateMips -> composite/sample.
//   - No-op (with a log) when the RT was created single-level.
//
// Resize() recreates the image with the ORIGINAL CreateInfo (format, filter,
// mip request) at the new dimensions, so a full-chain RT gets a full chain for
// the new size. It drains the device first: with kFramesInFlight = 2 the other
// in-flight frame may still be sampling the old image, and destroying a live
// VkImage is UB that shows up as VK_ERROR_DEVICE_LOST a frame or two later.
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

#ifndef RENDER_TARGET_VK_H
#define RENDER_TARGET_VK_H

#include "sys_vk.h"

#include <cstdint>

// -----------------------------------------------------------------------------
// rtFilterVK
// Sampler filtering for the RT's color image. (VK suffix to keep AAE's global
// namespace clear of the GL side.)
// -----------------------------------------------------------------------------
enum class rtFilterVK : int
{
    Nearest = 0,
    Linear  = 1
};

// -----------------------------------------------------------------------------
// RenderTargetVKCreateInfo
// -----------------------------------------------------------------------------
struct RenderTargetVKCreateInfo
{
    int        width  = 0;
    int        height = 0;
    rtFilterVK filter = rtFilterVK::Linear;

    // Explicit storage format for the color image. VK_FORMAT_UNDEFINED (the
    // default) uses the swapchain's format. The AAE
    // raster chain passes VK_FORMAT_R8G8B8A8_UNORM so blending math matches
    // the GL path's non-color-managed byte space.
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;

    // Mip chain request:
    //   0 or 1  -> single level
    //   -1      -> full chain for width x height
    //   N > 1   -> min(N, full chain)
    // When the resolved count is > 1 the image gains TRANSFER_SRC/DST usage
    // and GenerateMips() becomes available. If the runtime format does not
    // support linear blit (never the case for RGBA8 on desktop), Init falls
    // back to a single level with a log.
    int mipLevels = 1;
};

// =============================================================================
// RenderTargetVK
// =============================================================================
class RenderTargetVK
{
public:
    RenderTargetVK()  = default;
    ~RenderTargetVK() = default;

    RenderTargetVK(const RenderTargetVK&)            = delete;
    RenderTargetVK& operator=(const RenderTargetVK&) = delete;

    bool Init(VkContext& ctx, const RenderTargetVKCreateInfo& ci);
    void Shutdown(VkContext& ctx);

    // Recreates the target at the new size with the original CreateInfo.
    // Drains the device first (see header comment). Safe to call mid-frame
    // only in the sense that it is correct; it is NOT cheap - callers should
    // resize on real size changes, not every frame.
    void Resize(VkContext& ctx, int newWidth, int newHeight);

    // Opens a dynamic-rendering pass targeting mip level 0. Publishes
    // ctx.activeColorFormat and sets viewport/scissor to the RT dims.
    void Begin(VkContext& ctx, VkCommandBuffer cmd,
               bool clear = true,
               float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f);

    // Ends the pass and transitions level 0 to SHADER_READ_ONLY_OPTIMAL.
    // Resets ctx.activeColorFormat to VK_FORMAT_UNDEFINED.
    void End(VkContext& ctx, VkCommandBuffer cmd);

    // Records the blit-cascade mip downsample. See the CONTRACT block in the
    // file header: call between End() and the next pass, never inside one.
    void GenerateMips(VkContext& ctx, VkCommandBuffer cmd);

    int GetWidth()  const { return width_; }
    int GetHeight() const { return height_; }

    uint32_t GetMipLevels() const { return mipLevels_; }
    VkFormat GetFormat()    const { return vk_colorFormat_; }

    VkImage     VK_GetColorImage() const { return vk_colorImage_; }
    VkImageView VK_GetColorView()  const { return vk_colorView_;  }
    VkSampler   VK_GetSampler()    const { return vk_sampler_;    }

    bool IsValid() const { return initialized_; }

private:
    bool initialized_ = false;

    int        width_  = 0;
    int        height_ = 0;
    rtFilterVK filter_ = rtFilterVK::Linear;

    // Full creation config, kept so Resize preserves colorFormat/mip request.
    RenderTargetVKCreateInfo createInfo_{};

    bool passOpen_ = false;

    // Resolved mip level count (1 = no chain).
    uint32_t mipLevels_ = 1;

    VkImage        vk_colorImage_  = VK_NULL_HANDLE;
    VkDeviceMemory vk_colorMemory_ = VK_NULL_HANDLE;
    VkImageView    vk_colorView_   = VK_NULL_HANDLE;
    VkSampler      vk_sampler_     = VK_NULL_HANDLE;

    // Attachment view for Begin(). A dynamic-rendering color attachment view
    // must cover exactly ONE mip level, so a mipped target gets a dedicated
    // level-0 view here while vk_colorView_ (the sampled view) spans the
    // whole chain. For single-level targets this aliases vk_colorView_ and
    // is not separately destroyed.
    VkImageView vk_attachView_ = VK_NULL_HANDLE;

    // Layout tracking. The render pass only ever touches level 0, so level 0
    // and the mip tail (levels 1..N-1) are tracked separately: the tail stays
    // UNDEFINED until the first GenerateMips, then SHADER_READ_ONLY_OPTIMAL.
    VkImageLayout vk_currentLayout_ = VK_IMAGE_LAYOUT_UNDEFINED; // level 0
    VkImageLayout vk_mipTailLayout_ = VK_IMAGE_LAYOUT_UNDEFINED; // levels 1..N-1

    // Resolved color format (createInfo_.colorFormat or the swapchain
    // format). Published to ctx.activeColorFormat by Begin so subsystems can
    // match their pipelines to this pass.
    VkFormat vk_colorFormat_ = VK_FORMAT_UNDEFINED;
};

#endif // RENDER_TARGET_VK_H
