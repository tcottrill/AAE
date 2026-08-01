// -----------------------------------------------------------------------------
// render_target_vk.cpp - Vulkan offscreen render target (Phase 4a Plan 4, Task 2).
// Imported from the SpriteTest engine donor (sys_render/render_target_vk.cpp),
// VK-only strip + mip-chain extension. See render_target_vk.h for the full
// list of what was stripped, preserved, and extended.
// ASCII-only comments.
// -----------------------------------------------------------------------------

#include "render_target_vk.h"
#include "sys_log.h"

// =============================================================================
// File-local helpers
// =============================================================================
static uint32_t RTFindMemoryTypeIdx_(VkContext& ctx, uint32_t typeBits,
                                     VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp{};
    ctx.vkGetPhysicalDeviceMemoryProperties_(ctx.phys, &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) == 0)
            continue;
        if ((mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    }
    return 0xFFFFFFFFu;
}

// Single-image layout barrier over an arbitrary mip range. The donor's helper
// hardcoded levelCount = 1; the mip extension needs baseLevel/levelCount so
// GenerateMips can transition the tail levels in one barrier.
static void RTImageLayoutBarrier_(VkContext& ctx,
                                  VkCommandBuffer cmd,
                                  VkImage img,
                                  uint32_t baseMipLevel,
                                  uint32_t levelCount,
                                  VkImageLayout oldLayout,
                                  VkImageLayout newLayout,
                                  VkPipelineStageFlags2 srcStage,
                                  VkAccessFlags2        srcAccess,
                                  VkPipelineStageFlags2 dstStage,
                                  VkAccessFlags2        dstAccess)
{
    VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    b.srcStageMask  = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask  = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.image = img;
    b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel   = baseMipLevel;
    b.subresourceRange.levelCount     = levelCount;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount     = 1;

    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    ctx.vkCmdPipelineBarrier2_(cmd, &dep);
}

// Full chain length for the given dims (log2, clamped to >= 1).
static uint32_t RTFullMipChain_(int width, int height)
{
    uint32_t levels = 1;
    uint32_t dim = (uint32_t)((width > height) ? width : height);
    while (dim > 1u) { ++levels; dim >>= 1; }
    return levels;
}

// =============================================================================
// RenderTargetVK
// =============================================================================
bool RenderTargetVK::Init(VkContext& ctx, const RenderTargetVKCreateInfo& ci)
{
    if (initialized_)
    {
        LOG_ERROR("RenderTargetVK: Init called on already-initialized target");
        return false;
    }

    // Keep the full CreateInfo so Resize can recreate the target with the
    // SAME configuration (donor lesson: rebuilding from just width/height
    // silently dropped colorFormat, changing blend math with no log).
    createInfo_ = ci;

    width_  = (ci.width  > 0) ? ci.width  : 1;
    height_ = (ci.height > 0) ? ci.height : 1;
    filter_ = ci.filter;

    // Honor an explicit format from the caller; default to swapchain format
    // (the donor's legacy behavior).
    vk_colorFormat_ = (ci.colorFormat != VK_FORMAT_UNDEFINED)
        ? ci.colorFormat
        : ctx.swapchainFormat;

    // Resolve the mip request (extension over the donor).
    //   0 or 1 -> 1;  -1 -> full chain;  N > 1 -> min(N, full chain).
    const uint32_t fullChain = RTFullMipChain_(width_, height_);
    if (ci.mipLevels == -1)
        mipLevels_ = fullChain;
    else if (ci.mipLevels > 1)
        mipLevels_ = ((uint32_t)ci.mipLevels < fullChain) ? (uint32_t)ci.mipLevels : fullChain;
    else
        mipLevels_ = 1;

    // Mips need linear blit support for the format (same runtime check as
    // sys_vk's VK_BuildRGBA8Texture). Every desktop driver reports it for
    // RGBA8/BGRA8; fall back to a single level with a log if not.
    if (mipLevels_ > 1)
    {
        bool canBlit = false;
        if (ctx.vkGetPhysicalDeviceFormatProperties_ && ctx.vkCmdBlitImage_)
        {
            VkFormatProperties fp{};
            ctx.vkGetPhysicalDeviceFormatProperties_(ctx.phys, vk_colorFormat_, &fp);
            const VkFormatFeatureFlags need =
                VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
            canBlit = ((fp.optimalTilingFeatures & need) == need);
        }
        if (!canBlit)
        {
            LOG_ERROR("RenderTargetVK: format %d lacks linear blit support; "
                      "falling back to single mip level", (int)vk_colorFormat_);
            mipLevels_ = 1;
        }
    }

    VkImageCreateInfo ii{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ii.imageType   = VK_IMAGE_TYPE_2D;
    ii.format      = vk_colorFormat_;
    ii.extent      = { (uint32_t)width_, (uint32_t)height_, 1 };
    ii.mipLevels   = mipLevels_;
    ii.arrayLayers = 1;
    ii.samples     = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling      = VK_IMAGE_TILING_OPTIMAL;
    ii.usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mipLevels_ > 1)
    {
        // GenerateMips blits OUT of level N (TRANSFER_SRC) INTO level N+1
        // (TRANSFER_DST).
        ii.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (ctx.vkCreateImage_(ctx.device, &ii, nullptr, &vk_colorImage_) != VK_SUCCESS)
    {
        LOG_ERROR("RenderTargetVK: vkCreateImage failed");
        return false;
    }

    VkMemoryRequirements mr{};
    ctx.vkGetImageMemoryRequirements_(ctx.device, vk_colorImage_, &mr);

    uint32_t memIdx = RTFindMemoryTypeIdx_(ctx, mr.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memIdx == 0xFFFFFFFFu)
    {
        LOG_ERROR("RenderTargetVK: no DEVICE_LOCAL memory type for color image");
        Shutdown(ctx);
        return false;
    }

    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = memIdx;
    if (ctx.vkAllocateMemory_(ctx.device, &mai, nullptr, &vk_colorMemory_) != VK_SUCCESS)
    {
        LOG_ERROR("RenderTargetVK: vkAllocateMemory failed for color image");
        Shutdown(ctx);
        return false;
    }
    if (ctx.vkBindImageMemory_(ctx.device, vk_colorImage_, vk_colorMemory_, 0) != VK_SUCCESS)
    {
        LOG_ERROR("RenderTargetVK: vkBindImageMemory failed for color image");
        Shutdown(ctx);
        return false;
    }

    // Sampled view covers the WHOLE mip chain. GenerateMips blits by image
    // handle + subresource, so no per-level views are needed.
    VkImageViewCreateInfo iv{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    iv.image    = vk_colorImage_;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format   = vk_colorFormat_;
    iv.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    iv.subresourceRange.baseMipLevel   = 0;
    iv.subresourceRange.levelCount     = mipLevels_;
    iv.subresourceRange.baseArrayLayer = 0;
    iv.subresourceRange.layerCount     = 1;
    if (ctx.vkCreateImageView_(ctx.device, &iv, nullptr, &vk_colorView_) != VK_SUCCESS)
    {
        LOG_ERROR("RenderTargetVK: vkCreateImageView failed");
        Shutdown(ctx);
        return false;
    }

    // Attachment view: a dynamic-rendering color attachment view must cover
    // exactly one mip level, so a mipped target needs a dedicated level-0
    // view. Single-level targets reuse the sampled view (donor behavior).
    if (mipLevels_ > 1)
    {
        VkImageViewCreateInfo av = iv;
        av.subresourceRange.baseMipLevel = 0;
        av.subresourceRange.levelCount   = 1;
        if (ctx.vkCreateImageView_(ctx.device, &av, nullptr, &vk_attachView_) != VK_SUCCESS)
        {
            LOG_ERROR("RenderTargetVK: vkCreateImageView (attachment) failed");
            Shutdown(ctx);
            return false;
        }
    }
    else
    {
        vk_attachView_ = vk_colorView_;
    }

    // Sampler: CLAMP_TO_EDGE (donor behavior). Single-level targets keep the
    // donor's filter choice with mipmapMode NEAREST / maxLod 0. Mipped targets
    // use LINEAR mag/min with LINEAR mipmap mode (GL_LINEAR_MIPMAP_LINEAR,
    // matching the GL raster chain's fbo texture params) and maxLod = chain
    // length so textureLod() in the CRT shaders can reach every level.
    VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    if (mipLevels_ > 1)
    {
        si.magFilter  = VK_FILTER_LINEAR;
        si.minFilter  = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.maxLod     = (float)mipLevels_;
    }
    else
    {
        const VkFilter f = (filter_ == rtFilterVK::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        si.magFilter  = f;
        si.minFilter  = f;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.maxLod     = 0.0f;
    }
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.minLod       = 0.0f;
    si.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    if (ctx.vkCreateSampler_(ctx.device, &si, nullptr, &vk_sampler_) != VK_SUCCESS)
    {
        LOG_ERROR("RenderTargetVK: vkCreateSampler failed");
        Shutdown(ctx);
        return false;
    }

    vk_currentLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    vk_mipTailLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    passOpen_ = false;
    initialized_ = true;

    LOG_INFO("RenderTargetVK: created %dx%d (filter=%s, format=%d, mips=%u)",
             width_, height_,
             (filter_ == rtFilterVK::Nearest) ? "NEAREST" : "LINEAR",
             (int)vk_colorFormat_, mipLevels_);
    return true;
}

void RenderTargetVK::Shutdown(VkContext& ctx)
{
    if (vk_sampler_)     { ctx.vkDestroySampler_(ctx.device, vk_sampler_, nullptr);      vk_sampler_     = VK_NULL_HANDLE; }
    if (vk_attachView_ && vk_attachView_ != vk_colorView_)
    {
        ctx.vkDestroyImageView_(ctx.device, vk_attachView_, nullptr);
    }
    vk_attachView_ = VK_NULL_HANDLE;
    if (vk_colorView_)   { ctx.vkDestroyImageView_(ctx.device, vk_colorView_, nullptr);  vk_colorView_   = VK_NULL_HANDLE; }
    if (vk_colorImage_)  { ctx.vkDestroyImage_(ctx.device, vk_colorImage_, nullptr);     vk_colorImage_  = VK_NULL_HANDLE; }
    if (vk_colorMemory_) { ctx.vkFreeMemory_(ctx.device, vk_colorMemory_, nullptr);      vk_colorMemory_ = VK_NULL_HANDLE; }
    vk_currentLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    vk_mipTailLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    initialized_ = false;
    passOpen_    = false;
}

void RenderTargetVK::Resize(VkContext& ctx, int newWidth, int newHeight)
{
    if (!initialized_) return;
    if (newWidth == width_ && newHeight == height_) return;

    // CRITICAL (donor lesson, kept verbatim in spirit): wait for the device
    // to be idle before tearing down the old image. With kFramesInFlight = 2,
    // the OTHER slot's GPU work may still be sampling the old RT image.
    // Destroying a VkImage while it is in use is UB; on NVIDIA it surfaces as
    // VK_ERROR_DEVICE_LOST on a later submit (black screen + repeating
    // "vkQueueSubmit2 returned -4" a frame or two after a resize).
    if (ctx.device && ctx.vkDeviceWaitIdle_)
        ctx.vkDeviceWaitIdle_(ctx.device);

    // Recreate with the ORIGINAL CreateInfo (colorFormat, filter, and the mip
    // REQUEST - a full-chain request resolves against the new dims), only the
    // dimensions changed.
    RenderTargetVKCreateInfo ci = createInfo_;
    ci.width  = newWidth;
    ci.height = newHeight;

    Shutdown(ctx);
    Init(ctx, ci);
}

void RenderTargetVK::Begin(VkContext& ctx, VkCommandBuffer cmd,
                           bool clear, float r, float g, float b, float a)
{
    if (!initialized_ || cmd == VK_NULL_HANDLE || !vk_colorImage_)
    {
        LOG_ERROR("RenderTargetVK: Begin called with invalid state/cmd");
        return;
    }
    if (passOpen_)
    {
        LOG_ERROR("RenderTargetVK: Begin called twice without End");
        return;
    }

    VkPipelineStageFlags2 srcStage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    VkAccessFlags2        srcAccess = 0;
    if (vk_currentLayout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        srcStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        srcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }

    // First-use hardening (donor behavior): transitioning from UNDEFINED
    // legally discards the image contents, so LOAD_OP_LOAD would read
    // garbage. Force a clear for the very first pass on this image.
    if (vk_currentLayout_ == VK_IMAGE_LAYOUT_UNDEFINED && !clear)
    {
        LOG_INFO("RenderTargetVK: first Begin on this image had clear=false; forcing clear (contents are undefined)");
        clear = true;
    }

    // Only level 0 is ever a render attachment; the mip tail keeps its own
    // tracked layout and is handled by GenerateMips.
    RTImageLayoutBarrier_(ctx, cmd,
        vk_colorImage_,
        0, 1,
        vk_currentLayout_,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        srcStage,  srcAccess,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);

    vk_currentLayout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Publish this pass's attachment format so subsystems recording into it
    // can select a matching pipeline (see VK_ActiveColorFormat in sys_vk.h).
    ctx.activeColorFormat = vk_colorFormat_;

    VkClearValue clearVal{};
    clearVal.color.float32[0] = r;
    clearVal.color.float32[1] = g;
    clearVal.color.float32[2] = b;
    clearVal.color.float32[3] = a;

    // Attach the single-level (level 0) view -- see vk_attachView_ in the
    // header for why the full-chain sampled view cannot be the attachment.
    VkRenderingAttachmentInfo colorAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAtt.imageView   = vk_attachView_;
    colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAtt.loadOp      = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.clearValue  = clearVal;

    VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    ri.renderArea.offset = { 0, 0 };
    ri.renderArea.extent = { (uint32_t)width_, (uint32_t)height_ };
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &colorAtt;
    ctx.vkCmdBeginRendering_(cmd, &ri);

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width  = (float)width_;
    vp.height = (float)height_;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    ctx.vkCmdSetViewport_(cmd, 0, 1, &vp);

    VkRect2D sc{};
    sc.offset = { 0, 0 };
    sc.extent = { (uint32_t)width_, (uint32_t)height_ };
    ctx.vkCmdSetScissor_(cmd, 0, 1, &sc);

    passOpen_ = true;
}

void RenderTargetVK::End(VkContext& ctx, VkCommandBuffer cmd)
{
    if (!initialized_ || cmd == VK_NULL_HANDLE || !vk_colorImage_) return;
    if (!passOpen_) return;

    ctx.vkCmdEndRendering_(cmd);

    RTImageLayoutBarrier_(ctx, cmd,
        vk_colorImage_,
        0, 1,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    vk_currentLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // This pass is closed; no active attachment format until the next pass.
    ctx.activeColorFormat = VK_FORMAT_UNDEFINED;
    passOpen_ = false;
}

// -----------------------------------------------------------------------------
// GenerateMips
//
// Blit-cascade downsample, adapted from sys_vk's VK_BuildRGBA8Texture mip
// cascade (same barrier/blit pattern) with two differences:
//   - level 0 enters in SHADER_READ_ONLY_OPTIMAL (post-End), not TRANSFER_DST
//     from a staging upload; it is transitioned straight to TRANSFER_SRC.
//   - runs per-frame inside the frame's command buffer, not a one-shot upload
//     submission.
//
// Layout transitions recorded, in order (N = mipLevels_):
//   1. level 0:        SHADER_READ_ONLY      -> TRANSFER_SRC
//   2. levels 1..N-1:  UNDEFINED/SHADER_READ -> TRANSFER_DST  (one barrier)
//   3. loop i = 1..N-1:
//        if i > 1: level i-1: TRANSFER_DST -> TRANSFER_SRC
//        blit level i-1 -> level i (linear filter)
//   4. levels 0..N-2:  TRANSFER_SRC -> SHADER_READ_ONLY
//      level  N-1:     TRANSFER_DST -> SHADER_READ_ONLY  (paired barriers)
//
// On exit ALL levels are SHADER_READ_ONLY_OPTIMAL, ready for sampled reads
// (including textureLod in the CRT shaders and minified layout composites).
// -----------------------------------------------------------------------------
void RenderTargetVK::GenerateMips(VkContext& ctx, VkCommandBuffer cmd)
{
    if (!initialized_ || cmd == VK_NULL_HANDLE || !vk_colorImage_)
    {
        LOG_ERROR("RenderTargetVK: GenerateMips called with invalid state/cmd");
        return;
    }
    if (mipLevels_ <= 1)
    {
        LOG_ERROR("RenderTargetVK: GenerateMips called on a single-level target");
        return;
    }

    // CONTRACT: must be recorded OUTSIDE any dynamic-rendering pass
    // (vkCmdBlitImage is illegal inside a render pass instance). Every pass
    // owner in this codebase publishes ctx.activeColorFormat while its pass
    // is open and resets it to UNDEFINED on close, so a non-UNDEFINED value
    // here means a pass is still open.
    if (ctx.activeColorFormat != VK_FORMAT_UNDEFINED)
    {
        LOG_ERROR("RenderTargetVK: GenerateMips called inside an open rendering "
                  "pass (activeColorFormat=%d); call it between End() and the "
                  "next Begin/composite pass", (int)ctx.activeColorFormat);
        return;
    }

    // CONTRACT: level 0 must be in SHADER_READ_ONLY_OPTIMAL, i.e. End() ran.
    if (vk_currentLayout_ != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        LOG_ERROR("RenderTargetVK: GenerateMips called before End() (level 0 "
                  "layout=%d)", (int)vk_currentLayout_);
        return;
    }

    // 1. Level 0: SHADER_READ_ONLY -> TRANSFER_SRC. Chains with End()'s
    //    barrier (its dst stage, FRAGMENT_SHADER, is this barrier's src
    //    stage), so the attachment write is already available.
    RTImageLayoutBarrier_(ctx, cmd,
        vk_colorImage_,
        0, 1,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT);

    // 2. Mip tail (levels 1..N-1) -> TRANSFER_DST. First frame the tail is
    //    still UNDEFINED (never written); afterwards it is SHADER_READ_ONLY
    //    from the previous GenerateMips. UNDEFINED discards, which is fine:
    //    every tail level is fully overwritten by the cascade below.
    {
        VkPipelineStageFlags2 tailSrcStage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        VkAccessFlags2        tailSrcAccess = 0;
        if (vk_mipTailLayout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            tailSrcStage  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            tailSrcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        }
        RTImageLayoutBarrier_(ctx, cmd,
            vk_colorImage_,
            1, mipLevels_ - 1,
            vk_mipTailLayout_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            tailSrcStage, tailSrcAccess,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);
    }

    // 3. Blit cascade: level (i-1) -> level i, linear downsample. Mirrors
    //    sys_vk's VK_BuildRGBA8Texture loop; the only difference is level 0
    //    is already TRANSFER_SRC (step 1), so the DST->SRC promotion of the
    //    source level starts at i = 2.
    int32_t mipW = (int32_t)width_;
    int32_t mipH = (int32_t)height_;

    for (uint32_t i = 1; i < mipLevels_; ++i)
    {
        if (i > 1)
        {
            RTImageLayoutBarrier_(ctx, cmd,
                vk_colorImage_,
                i - 1, 1,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);
        }

        int32_t nextW = (mipW > 1) ? (mipW >> 1) : 1;
        int32_t nextH = (mipH > 1) ? (mipH >> 1) : 1;

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipW, mipH, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;

        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { nextW, nextH, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        ctx.vkCmdBlitImage_(cmd,
            vk_colorImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            vk_colorImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        mipW = nextW;
        mipH = nextH;
    }

    // 4. Final transitions to SHADER_READ_ONLY_OPTIMAL. Levels [0..N-2] are
    //    in TRANSFER_SRC; level [N-1] is in TRANSFER_DST (it was the last
    //    blit's destination). Same paired-barrier shape as sys_vk.
    {
        VkImageMemoryBarrier2 bfinal[2]{};

        VkImageMemoryBarrier2& bA = bfinal[0];
        bA.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        bA.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        bA.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        bA.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        bA.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        bA.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        bA.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bA.image = vk_colorImage_;
        bA.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        bA.subresourceRange.baseMipLevel   = 0;
        bA.subresourceRange.levelCount     = mipLevels_ - 1;
        bA.subresourceRange.baseArrayLayer = 0;
        bA.subresourceRange.layerCount     = 1;

        VkImageMemoryBarrier2& bB = bfinal[1];
        bB.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        bB.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        bB.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        bB.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        bB.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        bB.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bB.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bB.image = vk_colorImage_;
        bB.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        bB.subresourceRange.baseMipLevel   = mipLevels_ - 1;
        bB.subresourceRange.levelCount     = 1;
        bB.subresourceRange.baseArrayLayer = 0;
        bB.subresourceRange.layerCount     = 1;

        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.imageMemoryBarrierCount = 2;
        dep.pImageMemoryBarriers = bfinal;
        ctx.vkCmdPipelineBarrier2_(cmd, &dep);
    }

    vk_currentLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vk_mipTailLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
