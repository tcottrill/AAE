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

// Per-frame artwork layer set for the layered composite (the VK mirror of GL
// final_render's art_tex[] usage). Views/samplers come from the VkArt_* cache
// (vk_texture_loader) - a null view means "layer absent this frame"; the
// caller applies the GL gates (config flag + art_loaded + video_attributes)
// before filling this in. All textures are loaded WITHOUT the GL chain's
// stbi vertical flip; the uvrects inside VectorPostVK encode the orientation
// (backdrop/bezel upright, overlays V-flipped exactly like GL's swapped-
// bottom/top drawTexturedQuad calls).
struct VectorArtworkVK
{
    VkImageView backdropView   = VK_NULL_HANDLE;  // GL art_tex[0], config.artwork
    VkSampler   backdropSampler = VK_NULL_HANDLE;
    VkImageView overlayView    = VK_NULL_HANDLE;  // GL art_tex[1], config.overlay
    VkSampler   overlaySampler = VK_NULL_HANDLE;
    VkImageView bezelView      = VK_NULL_HANDLE;  // GL art_tex[3], config.bezel
    VkSampler   bezelSampler   = VK_NULL_HANDLE;

    bool overlay1 = false;   // VECTOR_USES_OVERLAY1: modulates the CRT image
    bool overlay2 = false;   // VECTOR_USES_OVERLAY2: gel drawn over the CRT
    bool rasterBW = false;   // VIDEO_TYPE_RASTER_BW: overlay1 uses DST_COLOR/ZERO

    bool  artcrop  = false;  // cabinet crop scaling for backdrop/bezel
    float bezelX   = 0.0f;
    float bezelY   = 0.0f;
    float bezelZoom = 1.0f;

    bool Any() const
    {
        return backdropView != VK_NULL_HANDLE || bezelView != VK_NULL_HANDLE ||
               (overlayView != VK_NULL_HANDLE && (overlay1 || overlay2));
    }
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
    // the first EndBeamPass has run. This is the NO-ARTWORK fast path; when
    // artwork layers are active use RecordFrameBuild + RecordCompositeLayered.
    //
    // targetW/targetH: dimensions of the framebuffer this records into.
    // 0/0 (the default) means the swapchain, which is what every non-rotated
    // caller passes; the system-rotation path passes the square output RT's
    // dims so the same composite lands in GL's fbo4-equivalent canvas.
    void RecordComposite(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                         float x0, float y0, float x1, float y1,
                         int vectrail, int vecglow,
                         int targetW = 0, int targetH = 0);

    // ---- Artwork path (GL final_render LAYER 5A/5B/5C/6 mirror) ----
    //
    // RecordFrameBuild: builds the "CRT image" into the 1024x1024 frame RT
    // (GL img4b): beam+glow+trail composite quad at the game_rect box
    // (grL/grR/grB/grT in GL's Y-UP 1024-space, i.e. game_rect_* globals),
    // then the OVERLAY1 modulate quad (DST_COLOR/SRC_COLOR - DST_COLOR/ZERO
    // for RASTER_BW). Must run OUTSIDE any pass (between RecordPost and
    // VK_ResumeFramePass). Requires a prior EndBeamPass this session.
    void RecordFrameBuild(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                          float grL, float grR, float grB, float grT,
                          int vectrail, int vecglow,
                          const VectorArtworkVK& art);

    // RecordCompositeLayered: the layered swapchain draw (GL LAYER 5C + 6):
    // backdrop (alpha blend, 0.5 tint, cabinet scaling) -> frame RT ONE/ONE
    // -> crt_boost additive re-draw (0.2 with backdrop / 0.25 overlay2-only)
    // -> overlay2 (ONE_MINUS_SRC_ALPHA/SRC_COLOR, alpha 0.5, at game_rect)
    // -> bezel (no blend, alpha-test 0.2, cabinet scaling). Records into the
    // CURRENTLY OPEN swapchain pass. lx/lyUp/vw/vh = aspect-fit letterbox of
    // the square 1024 canvas in Y-UP window pixels (ComputeGuiBeamMap values);
    // grL..grT as in RecordFrameBuild. Does nothing until the first
    // RecordFrameBuild has run (callers fall back to RecordComposite).
    // targetW/targetH as in RecordComposite (0/0 = the swapchain).
    void RecordCompositeLayered(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                                float lx, float lyUp, float vw, float vh,
                                float grL, float grR, float grB, float grT,
                                const VectorArtworkVK& art,
                                int targetW = 0, int targetH = 0);

    bool IsValid()    const { return initialized_; }
    bool BeamReady()  const { return beamReady_; }
    bool FrameReady() const { return frameReady_; }
    int  BeamDim()    const { return beamDim_; }
    VkFormat BeamFormat() const { return rtBeam_.GetFormat(); }

private:
    // Worst case single-sampler draws per frame: trail(1) + glow downsamples
    // (2) + glow ping-pong (8) + overlay1 modulate (1) + layered composite
    // backdrop/frameRT/boost/overlay2/bezel (5) = 17, plus spare.
    static const uint32_t kSingleSlotsPerFrame = 20;
    static const uint32_t kMultiSlotsPerFrame  = 2;   // frame build OR direct composite (+spare)
    static const uint32_t kMaxCompositeVariants = 4;

    struct PostPush
    {
        float rect[4];    // x0,y0,x1,y1 target px, y-down
        float tsize[4];   // target w,h,0,0
        float uvrect[4];  // u,v at rect min; u,v at rect max
        float tint[4];
        float params[4];  // blur: w,h | multi: glowamt, usefb, useglow
    };

    // Per-color-format pipeline bundle for draws into the swapchain pass.
    // multi (the beam+glow+trail composite) builds on first use; the four
    // artwork pipelines build together on the first layered composite.
    struct CompVariant
    {
        VkFormat   fmt       = VK_FORMAT_UNDEFINED;
        VkPipeline multi     = VK_NULL_HANDLE;  // multi frag, ONE/ONE
        VkPipeline artAlpha  = VK_NULL_HANDLE;  // tex frag, SRC_ALPHA/1-SRC_ALPHA (backdrop)
        VkPipeline artAdd    = VK_NULL_HANDLE;  // tex frag, ONE/ONE (frame RT + boost)
        VkPipeline artOver2  = VK_NULL_HANDLE;  // tex frag, 1-SRC_ALPHA/SRC_COLOR (overlay2)
        VkPipeline artOpaque = VK_NULL_HANDLE;  // tex frag, no blend (bezel, alpha-test)
    };

    bool CreateLayouts(VkContext& ctx);
    bool CreateDescriptors(VkContext& ctx);
    bool BuildPipeline(VkContext& ctx, VkFormat colorFormat,
                       const char* fragPath, int blendMode,
                       VkPipelineLayout layout, VkPipeline& out);
    CompVariant* GetVariant(VkContext& ctx);         // find-or-add slot for the active format
    VkPipeline   GetCompositePipeline(VkContext& ctx);
    bool         EnsureArtPipelines(VkContext& ctx, CompVariant& v);

    // Records one quad with the single-sampler layout into the open pass.
    void DrawQuadS(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                   VkPipeline pipeline, VkImageView view, VkSampler sampler,
                   const PostPush& push, int targetW, int targetH);

    // Records one beam+glow+trail quad with the triple-sampler layout into
    // the open pass (readiness-substituted bindings; see RecordComposite).
    void DrawMultiQuad(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                       VkPipeline pipeline, const PostPush& push,
                       int targetW, int targetH);

    bool initialized_ = false;
    int  beamDim_ = 2048;
    int  ssaa_ = 2;

    std::string vertSpv_, blurSpv_, texSpv_, multiSpv_;

    RenderTargetVK rtBeam_;      // 1024*ssaa, mipped
    RenderTargetVK rtTrail_;     // 1024, mipped, persistent
    RenderTargetVK rtGlowHalf_;  // 512
    RenderTargetVK rtGlowA_;     // 256 ping-pong A (GL img3a)
    RenderTargetVK rtGlowB_;     // 256 ping-pong B (GL img3b - composite source)
    RenderTargetVK rtFrame_;     // 1024, no mips (GL img4b - the CRT image,
                                 // overlay1-modulated; only used with artwork)

    // Readiness = the RT has been through at least one End() and is safe to
    // sample (layout SHADER_READ_ONLY). trail/glow also gate the composite's
    // usefb/useglow so a mid-game config toggle can never sample stale-off
    // content as "on" before its first pass has run this session.
    bool beamReady_  = false;
    bool trailReady_ = false;
    bool glowReady_  = false;
    bool frameReady_ = false;  // rtFrame_ holds a built CRT image

    VkDescriptorSetLayout setLayoutS_ = VK_NULL_HANDLE;  // 1 sampler
    VkDescriptorSetLayout setLayoutM_ = VK_NULL_HANDLE;  // 3 samplers
    VkPipelineLayout pipeLayoutS_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayoutM_ = VK_NULL_HANDLE;

    VkPipeline pipeBlurCopy_  = VK_NULL_HANDLE;  // no blend, blur frag (RGBA8)
    VkPipeline pipeBlurAccum_ = VK_NULL_HANDLE;  // SRC_ALPHA/ONE, blur frag (RGBA8)
    VkPipeline pipeTrail_     = VK_NULL_HANDLE;  // ONE_MINUS_DST_COLOR/SRC_ALPHA, tex frag (RGBA8)

    // Frame-RT (RGBA8) artwork pipelines, fixed-format, built at Init.
    VkPipeline pipeFrameMulti_ = VK_NULL_HANDLE; // multi frag, ONE/ONE (CRT into frame RT)
    VkPipeline pipeArtMul_     = VK_NULL_HANDLE; // tex frag, DST_COLOR/SRC_COLOR (overlay1)
    VkPipeline pipeArtMulBW_   = VK_NULL_HANDLE; // tex frag, DST_COLOR/ZERO (overlay1, RASTER_BW)

    // Swapchain-pass pipeline variants per active color format (lazy like
    // ScreenQuadVK's cache).
    CompVariant comp_[kMaxCompositeVariants]{};
    uint32_t    compCount_ = 0;

    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet  setsS_[VkContext::kFramesInFlight][kSingleSlotsPerFrame]{};
    VkDescriptorSet  setsM_[VkContext::kFramesInFlight][kMultiSlotsPerFrame]{};
    uint32_t cursorS_[VkContext::kFramesInFlight]{};
    uint32_t cursorM_[VkContext::kFramesInFlight]{};
};

#endif // VECTOR_POST_VK_H
