//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// raster_tex_vk.cpp - see raster_tex_vk.h for the design and the in-flight
// safety argument.
// ASCII-only comments.
// -----------------------------------------------------------------------------

#include "raster_tex_vk.h"
#include "sys_log.h"

#include <string.h>

// -----------------------------------------------------------------------------
// File-local helper (same pattern as shot_draw_vk.cpp / vector_draw_vk.cpp).
// -----------------------------------------------------------------------------
static uint32_t RasterTexFindMemoryTypeIdx_(VkContext& ctx, uint32_t typeBits,
                                            VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp{};
    ctx.vkGetPhysicalDeviceMemoryProperties_(ctx.phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0xFFFFFFFFu;
}

// -----------------------------------------------------------------------------
// CreateSlot_
// One staging buffer + one image + one view for frame-in-flight slot i.
// -----------------------------------------------------------------------------
bool RasterTexVK::CreateSlot_(VkContext& ctx, uint32_t i)
{
    const VkDeviceSize bytes = (VkDeviceSize)width_ * (VkDeviceSize)height_ * 4;

    // ---- staging buffer (host visible, coherent, persistently mapped) ----
    VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size        = bytes;
    bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (ctx.vkCreateBuffer_(ctx.device, &bi, nullptr, &staging_[i]) != VK_SUCCESS)
    {
        LOG_ERROR("RasterTexVK: vkCreateBuffer failed (%llu bytes)", (unsigned long long)bytes);
        return false;
    }

    VkMemoryRequirements bmr{};
    ctx.vkGetBufferMemoryRequirements_(ctx.device, staging_[i], &bmr);
    uint32_t bmt = RasterTexFindMemoryTypeIdx_(ctx, bmr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (bmt == 0xFFFFFFFFu)
    {
        LOG_ERROR("RasterTexVK: no host-visible coherent memory type");
        return false;
    }

    VkMemoryAllocateInfo bai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    bai.allocationSize  = bmr.size;
    bai.memoryTypeIndex = bmt;
    if (ctx.vkAllocateMemory_(ctx.device, &bai, nullptr, &stagingMem_[i]) != VK_SUCCESS ||
        ctx.vkBindBufferMemory_(ctx.device, staging_[i], stagingMem_[i], 0) != VK_SUCCESS ||
        ctx.vkMapMemory_(ctx.device, stagingMem_[i], 0, VK_WHOLE_SIZE, 0, &stagingMapped_[i]) != VK_SUCCESS)
    {
        LOG_ERROR("RasterTexVK: staging alloc/bind/map failed");
        return false;
    }

    // ---- image ----
    // UNORM only (never _SRGB): the pen bytes are gamma-space and must reach
    // the RGBA8_UNORM game RT unmodified.
    VkImageCreateInfo ii{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent.width  = width_;
    ii.extent.height = height_;
    ii.extent.depth  = 1;
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (ctx.vkCreateImage_(ctx.device, &ii, nullptr, &image_[i]) != VK_SUCCESS)
    {
        LOG_ERROR("RasterTexVK: vkCreateImage failed (%ux%u)", width_, height_);
        return false;
    }

    VkMemoryRequirements imr{};
    ctx.vkGetImageMemoryRequirements_(ctx.device, image_[i], &imr);
    uint32_t imt = RasterTexFindMemoryTypeIdx_(ctx, imr.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (imt == 0xFFFFFFFFu)
    {
        LOG_ERROR("RasterTexVK: no device-local memory type");
        return false;
    }

    VkMemoryAllocateInfo iai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    iai.allocationSize  = imr.size;
    iai.memoryTypeIndex = imt;
    if (ctx.vkAllocateMemory_(ctx.device, &iai, nullptr, &imageMem_[i]) != VK_SUCCESS ||
        ctx.vkBindImageMemory_(ctx.device, image_[i], imageMem_[i], 0) != VK_SUCCESS)
    {
        LOG_ERROR("RasterTexVK: image alloc/bind failed");
        return false;
    }

    VkImageViewCreateInfo iv{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    iv.image    = image_[i];
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format   = ii.format;
    iv.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    iv.subresourceRange.baseMipLevel   = 0;
    iv.subresourceRange.levelCount     = 1;
    iv.subresourceRange.baseArrayLayer = 0;
    iv.subresourceRange.layerCount     = 1;

    if (ctx.vkCreateImageView_(ctx.device, &iv, nullptr, &view_[i]) != VK_SUCCESS)
    {
        LOG_ERROR("RasterTexVK: vkCreateImageView failed");
        return false;
    }

    imageLive_[i] = false;
    return true;
}

// -----------------------------------------------------------------------------
// Init
// -----------------------------------------------------------------------------
bool RasterTexVK::Init(VkContext& ctx, uint32_t w, uint32_t h)
{
    if (initialized_)
    {
        LOG_ERROR("RasterTexVK::Init called twice without Shutdown");
        return false;
    }
    if (!ctx.device || w == 0 || h == 0)
    {
        LOG_ERROR("RasterTexVK::Init: no device or zero dims (%ux%u)", w, h);
        return false;
    }

    width_  = w;
    height_ = h;
    pixels_.assign((size_t)w * (size_t)h, 0xFF000000u);   // opaque black

    // NEAREST magnification is what turns one native texel into a solid
    // prescale x prescale block in the prescaled game RT - the quad path's
    // output, term for term. Single mip level, so mipmapMode is inert.
    VkSamplerCreateInfo sp{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sp.magFilter     = VK_FILTER_NEAREST;
    sp.minFilter     = VK_FILTER_NEAREST;
    sp.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sp.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sp.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sp.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sp.maxAnisotropy = 1.0f;
    sp.minLod        = 0.0f;
    sp.maxLod        = 0.0f;

    if (ctx.vkCreateSampler_(ctx.device, &sp, nullptr, &sampler_) != VK_SUCCESS)
    {
        LOG_ERROR("RasterTexVK: vkCreateSampler failed");
        Shutdown(ctx);
        return false;
    }

    for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
    {
        if (!CreateSlot_(ctx, i))
        {
            Shutdown(ctx);
            return false;
        }
    }

    initialized_ = true;
    return true;
}

// -----------------------------------------------------------------------------
// Shutdown
// Callers drain the device before rebuilding (EnsureRasterRenderer does), and
// at chain shutdown the device is idle already; the wait here is the same
// belt-and-braces VK_DestroyTexture applies.
// -----------------------------------------------------------------------------
void RasterTexVK::Shutdown(VkContext& ctx)
{
    if (!ctx.device)
    {
        initialized_ = false;
        return;
    }

    if (ctx.vkDeviceWaitIdle_)
        ctx.vkDeviceWaitIdle_(ctx.device);

    for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
    {
        if (view_[i])       { ctx.vkDestroyImageView_(ctx.device, view_[i], nullptr);   view_[i] = VK_NULL_HANDLE; }
        if (image_[i])      { ctx.vkDestroyImage_(ctx.device, image_[i], nullptr);      image_[i] = VK_NULL_HANDLE; }
        if (imageMem_[i])   { ctx.vkFreeMemory_(ctx.device, imageMem_[i], nullptr);     imageMem_[i] = VK_NULL_HANDLE; }

        if (stagingMapped_[i]) { ctx.vkUnmapMemory_(ctx.device, stagingMem_[i]);        stagingMapped_[i] = nullptr; }
        if (staging_[i])    { ctx.vkDestroyBuffer_(ctx.device, staging_[i], nullptr);   staging_[i] = VK_NULL_HANDLE; }
        if (stagingMem_[i]) { ctx.vkFreeMemory_(ctx.device, stagingMem_[i], nullptr);   stagingMem_[i] = VK_NULL_HANDLE; }

        imageLive_[i] = false;
    }

    if (sampler_) { ctx.vkDestroySampler_(ctx.device, sampler_, nullptr); sampler_ = VK_NULL_HANDLE; }

    pixels_.clear();
    pixels_.shrink_to_fit();
    width_  = 0;
    height_ = 0;
    initialized_ = false;
}

// -----------------------------------------------------------------------------
// ClearPixels
// -----------------------------------------------------------------------------
void RasterTexVK::ClearPixels(uint32_t rgba)
{
    if (pixels_.empty())
        return;

    uint32_t* p = pixels_.data();
    const size_t n = pixels_.size();
    for (size_t i = 0; i < n; ++i)
        p[i] = rgba;
}

// -----------------------------------------------------------------------------
// Upload
// One sequential memcpy into this slot's staging buffer, then the two barriers
// and the copy. Host writes issued before vkQueueSubmit are made visible by
// submit's implicit host-write dependency, so no host barrier is needed here -
// the same guarantee sys_vk's own texture upload relies on.
// -----------------------------------------------------------------------------
void RasterTexVK::Upload(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex)
{
    if (!initialized_ || cmd == VK_NULL_HANDLE)
        return;
    if (frameIndex >= VkContext::kFramesInFlight)
    {
        LOG_ERROR("RasterTexVK::Upload: frameIndex out of range (%u)", frameIndex);
        return;
    }
    if (!stagingMapped_[frameIndex] || !image_[frameIndex])
        return;

    const size_t bytes = (size_t)width_ * (size_t)height_ * 4u;
    memcpy(stagingMapped_[frameIndex], pixels_.data(), bytes);

    // Barrier 1: make the image writable by the transfer.
    // First use of this slot comes out of UNDEFINED; later frames come back
    // from SHADER_READ_ONLY_OPTIMAL, and the srcStage/srcAccess name the
    // fragment-shader sample that the previous submission on THIS slot did.
    {
        VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        b.srcStageMask  = imageLive_[frameIndex] ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                                                 : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        b.srcAccessMask = imageLive_[frameIndex] ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                                                 : (VkAccessFlags2)0;
        b.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        b.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.oldLayout     = imageLive_[frameIndex] ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                 : VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image         = image_[frameIndex];
        b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.baseMipLevel   = 0;
        b.subresourceRange.levelCount     = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount     = 1;

        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers    = &b;
        ctx.vkCmdPipelineBarrier2_(cmd, &dep);
    }

    // Copy. bufferRowLength/bufferImageHeight 0 = tightly packed, which the
    // CPU buffer is (width_ texels per row, no padding).
    {
        VkBufferImageCopy bic{};
        bic.bufferOffset      = 0;
        bic.bufferRowLength   = 0;
        bic.bufferImageHeight = 0;
        bic.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        bic.imageSubresource.mipLevel       = 0;
        bic.imageSubresource.baseArrayLayer = 0;
        bic.imageSubresource.layerCount     = 1;
        bic.imageOffset       = { 0, 0, 0 };
        bic.imageExtent       = { width_, height_, 1 };

        ctx.vkCmdCopyBufferToImage_(cmd, staging_[frameIndex], image_[frameIndex],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);
    }

    // Barrier 2: hand the image to the fragment shader that samples it.
    {
        VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        b.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image         = image_[frameIndex];
        b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.baseMipLevel   = 0;
        b.subresourceRange.levelCount     = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount     = 1;

        VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers    = &b;
        ctx.vkCmdPipelineBarrier2_(cmd, &dep);
    }

    imageLive_[frameIndex] = true;
}
