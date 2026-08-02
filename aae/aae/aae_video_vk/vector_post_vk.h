// -----------------------------------------------------------------------------
// vector_post_vk.h - Vulkan vector post chain: SSAA beam RT, phosphor trail,
// glow blur cascade, composite (Phase 4a Plan 7 = Plan 5 Task 3).
//
// New code (no donor): a VK port of the GL chain's vector post processing in
// opengl_renderer.cpp final_render + copy_main_img_to_fbo2 / copy_fbo2_to_fbo3
// / render_blur_image_fbo3, built on RenderTargetVK. GL -> VK object map:
//
//   fbo1 img1a/img1b (beam frame, 1024^2)  -> rtBeam_ (1024*ssaa, RGBA8, mips)
//   fbo1 img1c (trail accumulator)         -> rtTrail_ (1024, RGBA8, mips,
//                                             persistent: LOAD, never cleared
//                                             except first use / new game)
//   fbo2 img2a (512 downsample)            -> rtGlowHalf_ (512)
//   fbo3 img3a/img3b (256 ping-pong)       -> rtGlowA_ / rtGlowB_ (256)
//   fragBlur / plain quad / fragMulti      -> vector_post_{blur,tex,multi}_vk
//
// The GL chain's img1a->img1b copy is skipped: it exists to fold the frame
// into a second attachment; here the beam RT itself serves as "img1b".
//
// Frame shape (all offscreen passes run between VK_SuspendFramePass and
// VK_ResumeFramePass; the composite runs inside the resumed swapchain pass):
//   OnFrameBegin -> BeginBeamPass -> [caller records VectorDrawVK] ->
//   EndBeamPass (mips) -> RecordPost (trail + glow) -> [resume] ->
//   RecordComposite (game_rect/letterbox quad)
//
// Pause parity with GL's frozen FBO: skip BeginBeamPass..RecordPost while
// paused and call RecordComposite alone - the RTs retain the last frame.
// BeamReady()/readiness flags keep the composite from ever sampling an
// UNDEFINED-layout image (unused/never-rendered slots bind the beam view and
// are multiplied by 0 in the shader).
//
// Coordinate convention: STANDARD viewport + Y-DOWN pixel rects (see
// vector_post_vk.vert) so intermediate copies are row-preserving; the chain's
// single vertical flip (GL's flip_v=true fbo1->fbo4 quad) is the composite
// draw's V-swapped uvrect.
//
// License: GPL-3.0-or-later (as the rest of AAE).
// ASCII-only comments.
// -----------------------------------------------------------------------------
#pragma once
#ifndef VECTOR_POST_VK_H
#define VECTOR_POST_VK_H

#include "sys_vk.h"
#include "render_target_vk.h"

#include <string>

struct VectorPostVKCreateInfo
{
    const char* vertSpv  = "shaders/vk/vector_post_vk.vert.spv";
    const char* blurSpv  = "shaders/vk/vector_post_blur_vk.frag.spv";
    const char* texSpv   = "shaders/vk/vector_post_tex_vk.frag.spv";
    const char* multiSpv = "shaders/vk/vector_post_multi_vk.frag.spv";

    // Supersample factor for the beam RT (dim = 1024 * ssaa). The caller
    // passes the same value to VectorDrawVKCreateInfo::ssaa so the AA feather
    // divide (backport fix 4b) matches the RT density.
    int ssaa = 2;
};

class VectorPostVK
{
public:
    VectorPostVK() = default;
    ~VectorPostVK() = default;
    VectorPostVK(const VectorPostVK&) = delete;
    VectorPostVK& operator=(const VectorPostVK&) = delete;

    bool Init(VkContext& ctx, const VectorPostVKCreateInfo* ci = nullptr);
    void Shutdown(VkContext& ctx);

    // Reset the frame slot's descriptor cursor. Call once per frame after
    // VK_BeginFrame (its fence wait proves the slot's previous frame done,
    // making the slot's descriptor sets safe to rewrite).
    void OnFrameBegin(uint32_t frameIndex);

    // Opens the beam RT pass (cleared black, like the GL fbo1 clear). The
    // caller records VectorDrawVK into it (proj = ortho 0..1024 both axes,
    // target dims = BeamDim()). Must run OUTSIDE the swapchain pass.
    void BeginBeamPass(VkContext& ctx, VkCommandBuffer cmd);

    // Ends the beam pass and regenerates its mip chain (the trail pass and
    // the 512 downsample both sample it minified).
    void EndBeamPass(VkContext& ctx, VkCommandBuffer cmd);

    // Trail + glow offscreen passes, gated by the vectrail/vecglow values
    // (pass the config fields). clearTrail forces a one-time trail clear
    // (new game load). Must run after EndBeamPass, outside any pass.
    void RecordPost(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                    int vectrail, int vecglow, bool clearTrail);

    // Composite quad into the CURRENTLY OPEN pass (the resumed swapchain
    // pass) at the given rect in Y-DOWN window pixels. Blend ONE/ONE over
    // the cleared-black frame, matching the GL additive build of img4b.
    // Safe to call while paused (samples retained RTs); does nothing until
    // the first EndBeamPass has run.
    void RecordComposite(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                         float x0, float y0, float x1, float y1,
                         int vectrail, int vecglow);

    bool IsValid()   const { return initialized_; }
    bool BeamReady() const { return beamReady_; }
    int  BeamDim()   const { return beamDim_; }
    VkFormat BeamFormat() const { return rtBeam_.GetFormat(); }

private:
    static const uint32_t kSingleSlotsPerFrame = 12; // trail + 2 downsample + 8 ping-pong + spare
    static const uint32_t kMultiSlotsPerFrame  = 2;
    static const uint32_t kMaxCompositeVariants = 4;

    struct PostPush
    {
        float rect[4];    // x0,y0,x1,y1 target px, y-down
        float tsize[4];   // target w,h,0,0
        float uvrect[4];  // u,v at rect min; u,v at rect max
        float tint[4];
        float params[4];  // blur: w,h | multi: glowamt, usefb, useglow
    };

    bool CreateLayouts(VkContext& ctx);
    bool CreateDescriptors(VkContext& ctx);
    bool BuildPipeline(VkContext& ctx, VkFormat colorFormat,
                       const char* fragPath, int blendMode,
                       VkPipelineLayout layout, VkPipeline& out);
    VkPipeline GetCompositePipeline(VkContext& ctx);

    // Records one quad with the single-sampler layout into the open pass.
    void DrawQuadS(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                   VkPipeline pipeline, VkImageView view, VkSampler sampler,
                   const PostPush& push, int targetW, int targetH);

    bool initialized_ = false;
    int  beamDim_ = 2048;
    int  ssaa_ = 2;

    std::string vertSpv_, blurSpv_, texSpv_, multiSpv_;

    RenderTargetVK rtBeam_;      // 1024*ssaa, mipped
    RenderTargetVK rtTrail_;     // 1024, mipped, persistent
    RenderTargetVK rtGlowHalf_;  // 512
    RenderTargetVK rtGlowA_;     // 256 ping-pong A (GL img3a)
    RenderTargetVK rtGlowB_;     // 256 ping-pong B (GL img3b - composite source)

    // Readiness = the RT has been through at least one End() and is safe to
    // sample (layout SHADER_READ_ONLY). trail/glow also gate the composite's
    // usefb/useglow so a mid-game config toggle can never sample stale-off
    // content as "on" before its first pass has run this session.
    bool beamReady_  = false;
    bool trailReady_ = false;
    bool glowReady_  = false;

    VkDescriptorSetLayout setLayoutS_ = VK_NULL_HANDLE;  // 1 sampler
    VkDescriptorSetLayout setLayoutM_ = VK_NULL_HANDLE;  // 3 samplers
    VkPipelineLayout pipeLayoutS_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayoutM_ = VK_NULL_HANDLE;

    VkPipeline pipeBlurCopy_  = VK_NULL_HANDLE;  // no blend, blur frag (RGBA8)
    VkPipeline pipeBlurAccum_ = VK_NULL_HANDLE;  // SRC_ALPHA/ONE, blur frag (RGBA8)
    VkPipeline pipeTrail_     = VK_NULL_HANDLE;  // ONE_MINUS_DST_COLOR/SRC_ALPHA, tex frag (RGBA8)

    // Composite pipeline variants per active color format (swapchain format
    // in practice; lazy like ScreenQuadVK's cache).
    VkFormat   compFormat_[kMaxCompositeVariants]{};
    VkPipeline compPipe_[kMaxCompositeVariants]{};
    uint32_t   compCount_ = 0;

    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet  setsS_[VkContext::kFramesInFlight][kSingleSlotsPerFrame]{};
    VkDescriptorSet  setsM_[VkContext::kFramesInFlight][kMultiSlotsPerFrame]{};
    uint32_t cursorS_[VkContext::kFramesInFlight]{};
    uint32_t cursorM_[VkContext::kFramesInFlight]{};
};

#endif // VECTOR_POST_VK_H
