//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// layout_vk.h - Vulkan port of the MAME .lay layout compositor
// (RASTER-game artwork).
//
// WHAT THE GL SIDE ACTUALLY IS
// ----------------------------
// Raster artwork under OpenGL does NOT go through the legacy art_tex[] slots
// (that path is vector-only, final_render). It goes through a small MAME
// layout engine in aae_video/mame_layout.cpp:
//
//   * Layout_Parse()        - tinyxml2 parse of a .lay file (from the game's
//                             artwork ZIP or a loose folder). Elements are
//                             either <image file="x.png"> or a stack of
//                             <rect> children (procedural color gels).
//                             Views hold drawables tagged backdrop / screen /
//                             overlay / bezel, each with layout-space bounds
//                             and an optional <color alpha="...">.
//   * Layout_LoadTextures() - stb_image PNG loads + procedural rect baking.
//   * Layout_Render()       - the whole raster composite: fit the view into
//                             the window (or zoom to the screen area), apply
//                             the display-aspect override, then draw
//                             backdrop -> screen -> bezel. The OVERLAY layer
//                             is never drawn on its own; it is multiplied
//                             into the screen layer by a dual-texture shader.
//                             Display-time system rotation turns the WHOLE
//                             layout as one rigid body.
//
// This module is the Vulkan mirror. The parser, the view model and the
// artwork search order are shared verbatim with GL (mame_layout.cpp is
// renderer-neutral apart from its texture upload and its GL draw calls), so
// only two things are ported here: the texture upload, and the compositing.
//
// PORTED SUBSET (everything the shipping .lay files use)
//   - <element> with <image file> (PNG via stb_image -> UNORM VkTexture)
//   - <element> with <rect><bounds/><color/> (baked to RGBA exactly like
//     BakeProceduralTexture, then uploaded)
//   - backdrop / screen / overlay / bezel layers, layer draw order, per
//     drawable <color alpha>
//   - view fit, zoom-to-screen (menu zoom / config.artcrop / bezel hidden),
//     the -aspect / GAME ASPECT override viewport (with the layout stretched
//     to fill it), and whole-layout system rotation (90 / 180 / 270)
//   - the config.artwork / config.overlay / config.bezel menu toggles and the
//     g_*Available menu-availability flags
//
// NOT PORTED (GL does not implement these either - the parser drops them):
//   - <orientation flipx/rotate> on a drawable, multi-screen views, <disk>,
//     <text>, animated/state-driven elements, view <bounds> overrides.
//
// Lifetime discipline: LayoutVK_LoadForGame frees the previous game's
// textures first and MUST be called with no frame open and the device
// drained (vkchain_load_layout does both). Nothing is ever freed mid-frame.
//
// License: GPL-3.0-or-later (as the rest of AAE).
// ASCII-only comments.
// -----------------------------------------------------------------------------

#pragma once
#ifndef LAYOUT_VK_H
#define LAYOUT_VK_H

#include "sys_vk.h"
#include <stdint.h>
#include <string>

struct AAEDriver;

// -----------------------------------------------------------------------------
// LQBlendVK - blend state for a LayoutQuadVK draw. One pipeline variant per
// (active color format, blend); the cache is keyed by both.
//
//   Alpha    - SRC_ALPHA / ONE_MINUS_SRC_ALPHA. GL's backdrop and bezel
//              layers (glBlendFunc in Layout_Render's per-layer branches).
//   Additive - ONE / ONE. GL's screen layer: game pixels ADD light on top of
//              the backdrop like a real CRT, so black adds nothing.
// -----------------------------------------------------------------------------
enum class LQBlendVK : int
{
    Alpha    = 0,
    Additive = 1,
    Count    = 2
};

struct LayoutQuadVKCreateInfo
{
    // The vertex stage is ScreenQuadVK's rect vertex shader, reused verbatim
    // (position + uv in, ortho UBO at set 0 binding 0 - which is exactly the
    // layout compositor's binding 0 too).
    const char* vertSpvPath = "shaders/vk/screen_quad_rect_vk.vert.spv";
    const char* fragSpvPath = "shaders/vk/layout_quad_vk.frag.spv";
};

// -----------------------------------------------------------------------------
// LayoutQuadVK
// Rect recorder for the layout composite. Same architecture as
// ScreenQuadVK's rect path (per-format pipeline variants, per-frame VBO /
// UBO / descriptor ring, y-flipped viewport, y-up caller rects) with two
// differences that ScreenQuadVK cannot express:
//   * a second sampler binding for the overlay color gel, so the screen
//     layer can be multiplied by it in ONE draw, exactly as GL's dual-tex
//     program does;
//   * a caller-supplied SCISSOR, so an aspect-override viewport clips the
//     layout the way glViewport clips it under GL.
// The tint is a float alpha here rather than ScreenQuadVK's packed rgb_t, so
// a <color alpha="0.3"> survives without an 8-bit round trip.
// -----------------------------------------------------------------------------
class LayoutQuadVK
{
public:
    struct QuadVertex
    {
        float x, y;   // caller screen-pixel space (y-up)
        float u, v;
    };

    LayoutQuadVK() = default;
    ~LayoutQuadVK() = default;
    LayoutQuadVK(const LayoutQuadVK&) = delete;
    LayoutQuadVK& operator=(const LayoutQuadVK&) = delete;

    bool Init(VkContext& ctx, const LayoutQuadVKCreateInfo* ci = nullptr);
    void Shutdown(VkContext& ctx);
    void OnFrameBegin(uint32_t frameIndex);
    bool IsValid() const { return initialized_; }

    // Records one layout quad into the currently open pass.
    //   srcView/srcSampler   - the layer texture (or the game RT for a screen)
    //   ovView/ovSampler     - the overlay gel; pass the same view/sampler as
    //                          src when overlayMode < 0 (never sampled then)
    //   left/bottom/right/top- destination rect, y-up target pixels
    //   scissor*             - clip box in y-DOWN target pixels (the aspect
    //                          override viewport; pass the full target to
    //                          disable clipping)
    //   uvRotation           - GL Rect2 index (0 none, 1 CW90, 2 CCW90, 3 180)
    //   alpha                - the drawable's <color alpha>
    //   overlayMode          - <0 single texture, 0 pure multiply (BW),
    //                          1 doubled multiply (color)
    //   uvXform              - screen UV -> overlay UV (scaleU, scaleV,
    //                          offsetU, offsetV); ignored when mode < 0
    void RecordQuad(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                    VkImageView srcView, VkSampler srcSampler,
                    VkImageView ovView, VkSampler ovSampler,
                    float leftPx, float bottomPx, float rightPx, float topPx,
                    uint32_t targetWidth, uint32_t targetHeight,
                    int32_t scissorX, int32_t scissorY,
                    uint32_t scissorW, uint32_t scissorH,
                    int uvRotation,
                    float alpha,
                    float overlayMode,
                    const float uvXform[4],
                    LQBlendVK blend);

private:
    bool BuildPipeline_(VkContext& ctx, VkFormat colorFormat, LQBlendVK blend,
                        VkPipeline& outPipeline);
    VkPipeline GetOrCreatePipeline_(VkContext& ctx, LQBlendVK blend);
    bool CreateBuffer_(VkContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags memProps,
                       VkBuffer& outBuf, VkDeviceMemory& outMem);
    uint32_t FindMemoryType_(VkContext& ctx, uint32_t typeBits, VkMemoryPropertyFlags props);

    struct LayoutPush
    {
        float xform[4];
        float params[4];   // x = alpha, y = overlay mode
    };

    // Shipping worst case is one screen + a handful of bezel/backdrop quads
    // (the fattest .lay view in the tree draws 5). 32 leaves ample headroom
    // and costs only descriptor sets.
    static constexpr uint32_t kSlotsPerFrame = 32;
    static constexpr uint32_t kMaxPipelineVariants = 8;

    bool initialized_ = false;
    std::string vertSpvPath_, fragSpvPath_;

    VkDescriptorSetLayout setLayout_  = VK_NULL_HANDLE;
    VkPipelineLayout      pipeLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      descPool_   = VK_NULL_HANDLE;

    VkFormat   pipeFormat_[kMaxPipelineVariants]{};
    LQBlendVK  pipeBlend_[kMaxPipelineVariants]{};
    VkPipeline pipeVariant_[kMaxPipelineVariants]{};
    uint32_t   pipeVariantCount_ = 0;

    VkBuffer       vbo_[VkContext::kFramesInFlight]{};
    VkDeviceMemory vboMem_[VkContext::kFramesInFlight]{};
    void*          vboMapped_[VkContext::kFramesInFlight]{};

    VkBuffer       ubo_[VkContext::kFramesInFlight]{};
    VkDeviceMemory uboMem_[VkContext::kFramesInFlight]{};
    void*          uboMapped_[VkContext::kFramesInFlight]{};
    VkDeviceSize   uboStride_ = 0;

    VkDescriptorSet descSets_[VkContext::kFramesInFlight][kSlotsPerFrame]{};
    uint32_t        slotCursor_[VkContext::kFramesInFlight]{};
    uint32_t        lastFrameIndexSeen_ = 0xFFFFFFFFu;
};

// -----------------------------------------------------------------------------
// Per-game layout artwork (the VK mirror of Layout_LoadForGame)
//
// Runs the SHARED search order (Layout_FindArtworkSource), the SHARED parser
// (Layout_Parse) and the SHARED view selection (Layout_FindView), then
// uploads each element's texture as an UNORM VkTexture into a name-keyed
// cache. Sets g_layoutEnabled / g_activeView / g_layoutAspect and the menu
// availability flags exactly as the GL loader does.
//
// When no .lay file is found (or it fails to parse, or its view is missing)
// this takes the shared synthetic screen-only fallback,
// Layout_CreateSyntheticForGame - the same call the GL loader makes - so an
// artwork-less game still composites through LayoutVK_ComputeFrame.
//
// Must be called device-idle with no frame open.
// -----------------------------------------------------------------------------
void LayoutVK_LoadForGame(VkContext& ctx, const AAEDriver* drv);

// True when a layout is loaded and renderable for this game - a parsed .lay
// or the synthetic screen-only view built for a game without one.
bool LayoutVK_Active(void);

// Destroys the per-game layout textures (device-idle) and drops the cache.
void LayoutVK_FreeTextures(VkContext& ctx);

// -----------------------------------------------------------------------------
// LayoutVKFrame
//
// Everything Layout_Render computes at the top of a frame before it starts
// drawing: the fitted camera, the aspect-override viewport, the rotation
// mode, and the SCREEN element's resolved on-screen rect. Computed once by
// LayoutVK_ComputeFrame so the caller can place the CRT monitor pass at the
// screen rect BEFORE the layer walk records the bezel on top of it.
//
// screen* pixels are y-DOWN swapchain coordinates (CrtPostVK's convention);
// the recorders convert to the y-up frame RecordQuad wants.
// -----------------------------------------------------------------------------
struct LayoutVKFrame
{
    bool valid = false;

    // Aspect-override viewport (y-down swapchain pixels). Equals the whole
    // swapchain when no override is active.
    int      vpX = 0, vpY = 0;
    uint32_t vpW = 0, vpH = 0;

    // Whole-layout system rotation: 0 none, 1 CW90, 2 CCW90, 3 180.
    int rotMode = 0;

    // SCREEN element placement, y-down swapchain pixels.
    bool  hasScreen = false;
    float sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0;
    float screenAlpha = 1.0f;

    // Overlay color gel covering the screen (first overlay drawable found).
    bool  hasOverlay = false;
    int   overlayMode = 0;              // 0 = pure multiply, 1 = doubled
    float ovXform[4] = { 1, 1, 0, 0 };  // screen UV -> overlay UV

    // A visible backdrop drawable is recorded under the screen, so the screen
    // layer's additive blend has artwork to let its black pixels show through.
    bool  hasBackdrop = false;

    // Layout -> pixel mapping. Non-rotated path uses scale/offset; the
    // rotated path uses the layout center + per-axis scale.
    float scaleX = 1, scaleY = 1, offsetX = 0, offsetY = 0;
    float cx = 0, cy = 0, sLx = 1, sLy = 1;
    float winW = 0, winH = 0;   // effective (post-override) dimensions
};

// Resolves this frame's layout geometry. Returns false when no layout is
// active (or the window/view degenerates), in which case the caller keeps
// its own aspect-fit letterbox composite.
bool LayoutVK_ComputeFrame(int swapW, int swapH, LayoutVKFrame& out);

// Records the layers BELOW and INCLUDING the game screen: every backdrop
// drawable (alpha-over), then the screen quad (additive, multiplied by the
// overlay gel when there is one). drawScreen=false skips the screen quad so
// the caller's CRT monitor pass can occupy that rect instead.
void LayoutVK_RecordUnderlay(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                             LayoutQuadVK& quad, const LayoutVKFrame& f,
                             VkImageView gameView, VkSampler gameSampler,
                             int swapW, int swapH, bool drawScreen);

// Records the bezel layer (alpha-over), on top of everything - including the
// CRT monitor pass, matching GL's draw order.
void LayoutVK_RecordOverlayArt(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                               LayoutQuadVK& quad, const LayoutVKFrame& f,
                               int swapW, int swapH);

#endif // LAYOUT_VK_H
