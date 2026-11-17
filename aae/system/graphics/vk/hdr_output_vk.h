//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// hdr_output_vk.h - HDR10 scene image and final encode pass.
//
// When HDR output is active sys_vk draws the whole frame into a swapchain-sized
// R16G16B16A16_SFLOAT "scene" image (this class owns it) and, at the end of the
// frame, runs one fullscreen pass that converts the scene to PQ / BT.2020 on
// the 10-bit swapchain image. See shaders/vk/hdr_output_vk.{vert,frag}.
//
// Built only from VkContext function pointers: it has no dependency on the
// emulator's renderer classes (RenderTargetVK etc.), so sys_vk can own it.
//
// Lifetime: sys_vk calls Init from CreateSwapchain when HDR becomes active and
// Shutdown from DestroySwapchain (so the pipeline, which is built against the
// swapchain format, is rebuilt for every swapchain). All calls happen with the
// device idle. ASCII-only comments.
// -----------------------------------------------------------------------------
#pragma once

#include "sys_vk.h"

class HdrOutputVK
{
public:
    static constexpr VkFormat kSceneFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    // Creates sampler, descriptor set, encode pipeline (against
    // ctx.swapchainFormat) and the scene image at ctx.swapchainExtent.
    // Returns false (everything released) on any failure.
    bool Init(VkContext& ctx);

    // Destroys everything Init created. Safe to call when not initialised.
    void Shutdown(VkContext& ctx);

    // (Re)creates the scene image at w x h and points the descriptor at it.
    // A zero extent is a no-op. The device must be idle.
    bool Resize(VkContext& ctx, uint32_t w, uint32_t h);

    bool        IsValid() const { return image_ != VK_NULL_HANDLE; }
    VkImage     SceneImage() const { return image_; }
    VkImageView SceneView() const { return view_; }

    // Records the encode draw: opens a dynamic-rendering pass on swapchain image
    // imageIndex, draws the fullscreen triangle sampling the scene, closes the
    // pass. The caller has already transitioned the scene image to
    // SHADER_READ_ONLY_OPTIMAL and the swapchain image to
    // COLOR_ATTACHMENT_OPTIMAL.
    void RecordEncode(VkContext& ctx, VkCommandBuffer cmd, uint32_t imageIndex,
                      float paperWhite, float maxNits);

private:
    bool BuildPipeline(VkContext& ctx);
    void DestroyImage(VkContext& ctx);

    VkImage               image_ = VK_NULL_HANDLE;
    VkDeviceMemory        memory_ = VK_NULL_HANDLE;
    VkImageView           view_ = VK_NULL_HANDLE;
    VkSampler             sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet       descSet_ = VK_NULL_HANDLE;
    VkPipelineLayout      pipeLayout_ = VK_NULL_HANDLE;
    VkPipeline            pipeline_ = VK_NULL_HANDLE;
    uint32_t              width_ = 0;
    uint32_t              height_ = 0;
};
