//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// crt_post_vk.h - Vulkan RASTER CRT post chain.
//
// VK port of the three GL raster post passes in opengl_renderer.cpp:
//   * render_scanlines()      -> RecordScanlines : tiled scanline texture
//                                multiplied over the game image, drawn INTO
//                                the game RT's open pass (GL draws it into
//                                fbo_raster / img5a, same place, same order).
//   * render_mono_monitor()   -> RecordMonitor(mono)  : fragMonoMonitor
//   * render_color_monitor()  -> RecordMonitor(color) : fragColorMonitor
//
// GL runs the monitor shader img5a -> img5b, where img5b is resized every
// frame to the ON-SCREEN game rectangle (Layout_GetScreenPixelSize) - the
// "pixel-exact output sizing" trick that makes the mask/scanline pitch
// independent of window size - and then hands img5b to Layout_Render, whose
// screen drawable does the compositing (the dual-texture overlay gel multiply
// and the rigid whole-layout rotation). RecordMonitor is target-agnostic and
// serves BOTH shapes; vulkan_renderer.cpp picks per frame:
//
//   * OFFSCREEN (GL's shape) - draw into s_rtMonitor, an intermediate RT sized
//     to the on-screen screen rect, and let the layout's gel quad or the
//     rotated ScreenQuadVK blit composite it. Required whenever the composite
//     must do something this class's quad cannot express: multiply an overlay
//     gel (the shaders take ONE texture) or turn 90 degrees (DrawQuad_ drives
//     its quad from a uvrect, which flips but does not rotate).
//   * DIRECT - no gel and no rotation: draw the quad straight onto the
//     swapchain at the letterbox / layout screen rect. Same output-sized post
//     with one resample fewer, 1 fragment == 1 screen pixel.
//
// The mask's fragment origin follows the target either way: the caller passes
// the rect corner as the origin push (pc.tsize.zw), which is the letterbox
// corner on the direct route and (0,0) - GL's fbo_mono origin exactly - on the
// offscreen route. See crt_color_vk.frag.
//
// Ordering contract (mirrors final_render_raster exactly):
//   rtGame.Begin -> FpolyVK draws the frame -> RecordScanlines -> rtGame.End
//   -> rtGame.GenerateMips -> [offscreen: rtMonitor.Begin -> RecordMonitor ->
//   rtMonitor.End] -> resume swapchain pass -> [direct: RecordMonitor]
// The mono/color halation taps read the source mip pyramid via textureLod, so
// GenerateMips MUST run after the scanline draw and before RecordMonitor -
// exactly what GL does (fbo_generate_mipmaps({img5a}) sits between
// render_scanlines and the monitor pass).
//
// License: GPL-3.0-or-later (as the rest of AAE).
// ASCII-only comments.
// -----------------------------------------------------------------------------
#pragma once
#ifndef CRT_POST_VK_H
#define CRT_POST_VK_H

#include "sys_vk.h"

#include <string>

// -----------------------------------------------------------------------------
// CrtMonitorParamsVK
// One-to-one with the GL uniform sets of fragMonoMonitor / fragColorMonitor.
// vulkan_renderer.cpp fills this from config every frame (never cached - the
// menus mutate config at runtime, and the GL path re-uploads every uniform
// every frame for the same reason).
// -----------------------------------------------------------------------------
struct CrtMonitorParamsVK
{
    // Shared (both passes)
    float srcW = 1.0f;          // uSrcSize.x - visible area in NATIVE px, oriented
    float srcH = 1.0f;          // uSrcSize.y
    float lodBias = 0.0f;       // uLodBias = log2(config.prescale), GL's value
                                //   (the game RT is native * prescale). 0 at
                                //   the default prescale 1.
    float blurH = 0.0f;         // uBlurH
    float blurV = 0.0f;         // uBlurV
    float halation = 0.0f;      // uHalation
    float halRadius = 1.0f;     // uHalRadius
    float scanline = 0.0f;      // uScanline
    float contrast = 1.0f;      // uContrast
    float bright = 0.0f;        // uBright

    // Mono pass only
    float tint[3] = { 1.0f, 1.0f, 1.0f };   // uTint

    // Color pass only
    float converge = 0.0f;      // uConverge
    float saturation = 1.0f;    // uSaturation
    float maskType = 0.0f;      // uMaskType (0 grille, 1 slot, 2 dot triad)
    float maskStrength = 0.0f;  // uMaskStrength
    float maskScale = 1.0f;     // uMaskScale
};

struct CrtPostVKCreateInfo
{
    const char* vertSpv = "shaders/vk/crt_post_vk.vert.spv";
    const char* monoSpv = "shaders/vk/crt_mono_vk.frag.spv";
    const char* colorSpv = "shaders/vk/crt_color_vk.frag.spv";
    const char* scanSpv = "shaders/vk/crt_scanline_vk.frag.spv";

    // Format of the offscreen game RT the scanline pass draws into. The
    // monitor pipelines are built lazily against the ACTIVE pass format
    // (the swapchain) at record time, house pattern (ScreenQuadVK).
    VkFormat gameRtFormat = VK_FORMAT_R8G8B8A8_UNORM;
};

// =============================================================================
// CrtPostVK
// =============================================================================
class CrtPostVK
{
public:
    CrtPostVK() = default;
    ~CrtPostVK() = default;
    CrtPostVK(const CrtPostVK&) = delete;
    CrtPostVK& operator=(const CrtPostVK&) = delete;

    bool Init(VkContext& ctx, const CrtPostVKCreateInfo* ci = nullptr);
    void Shutdown(VkContext& ctx);

    // Resets the per-frame descriptor ring cursor. Called from the same place
    // as the other subsystems' OnFrameBegin.
    void OnFrameBegin(uint32_t frameIndex);

    // Tiled scanline multiply over the whole game RT, recorded into the RT's
    // ALREADY-OPEN pass. texW/texH are the scanline image's pixel dims;
    // targetW/targetH are the game RT's dims, i.e. native * config.prescale -
    // GL's rw/rh, which is what the tiling divides. No prescale argument: the
    // target already carries it (see the .cpp for the pitch trace).
    void RecordScanlines(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                         VkImageView texView,
                         int texW, int texH,
                         int targetW, int targetH);

    // Monitor CRT pass. srcView/srcSampler are the game RT's mipped sampled
    // view; the quad is drawn into the ALREADY-OPEN swapchain pass at the
    // y-down target-pixel rect (x0,y0)-(x1,y1).
    void RecordMonitor(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                       bool colorPass,
                       VkImageView srcView, VkSampler srcSampler,
                       const CrtMonitorParamsVK& p,
                       float x0, float y0, float x1, float y1,
                       int targetW, int targetH);

    // REPEAT + NEAREST sampler owned by this object, for the scanline
    // texture (GL sets GL_REPEAT/GL_NEAREST on the overlay every draw; the
    // sys_vk texture builder hands out CLAMP_TO_EDGE + linear).
    VkSampler TileSampler() const { return tileSampler_; }

    bool IsValid() const { return initialized_; }

private:
    // 128 bytes exactly - see crt_post_vk.vert.
    struct CrtPush
    {
        float rect[4];
        float tsize[4];
        float uvrect[4];
        float p0[4];
        float p1[4];
        float p2[4];
        float p3[4];
        float tint[4];
    };

    enum { KIND_MONO = 0, KIND_COLOR = 1, KIND_SCAN = 2, KIND_COUNT = 3 };
    static const uint32_t kMaxFormats = 4;
    // Worst case per frame today is 2 (one scanline + one monitor); the
    // headroom costs nothing but descriptor sets.
    static const uint32_t kSlotsPerFrame = 8;

    bool BuildPipeline_(VkContext& ctx, VkFormat fmt, const char* fragPath,
                        bool multiplyBlend, VkPipeline& out);
    VkPipeline GetPipeline_(VkContext& ctx, int kind, VkFormat fmt);
    void DrawQuad_(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                   VkPipeline pipe, VkImageView view, VkSampler sampler,
                   const CrtPush& push, int targetW, int targetH);

    bool initialized_ = false;

    std::string vertSpv_, fragSpv_[KIND_COUNT];
    VkFormat gameRtFormat_ = VK_FORMAT_R8G8B8A8_UNORM;

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout      pipeLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet       sets_[VkContext::kFramesInFlight][kSlotsPerFrame]{};
    uint32_t              cursor_[VkContext::kFramesInFlight]{};
    uint32_t              lastFrameIndexSeen_ = 0xFFFFFFFFu;

    VkFormat   pipeFmt_[KIND_COUNT][kMaxFormats]{};
    VkPipeline pipe_[KIND_COUNT][kMaxFormats]{};
    uint32_t   pipeCount_[KIND_COUNT]{};

    VkSampler tileSampler_ = VK_NULL_HANDLE;
};

#endif // CRT_POST_VK_H
