// -----------------------------------------------------------------------------
// shot_draw_vk.h - Vulkan textured vector shot pass.
//
// VK port of the GL legacy textured-shot pass (emu_vector_draw.cpp
// draw_textured_shots): when config.shots_textured is set, add_tex diverts
// every shot into the CPU txdata triangle list instead of the procedural
// beam shots, and GL draws that list additively (SRC_ALPHA/ONE) with the
// texColorFrag shader (texture * per-vertex color + radial edge fade) into
// fbo1 right after the beams, under the SAME projection. This class records
// the same list into VectorPostVK's beam RT between the beam Record and
// EndBeamPass, so shots inherit glow / phosphor trails / artwork modulation
// exactly like GL.
//
// The shot texture is the per-game GAME_TEX slot 0 (shot.png / cineshot.png
// from the driver's ART_LOAD table), loaded UNORM by the VkArt cache
// (vk_texture_loader). When a game has no shot texture, nothing is drawn -
// GL samples an incomplete texture there (black) so shots are invisible
// under GL too.
//
// Vertex stream is txdata verbatim (x,y float; tx,ty float; packed RGBA):
// per-frame-in-flight host-visible VBOs, grown on demand. The old buffer is
// destroyed IMMEDIATELY on growth: this is only ever called after
// VK_BeginFrame's fence wait proved the slot's previous submission complete,
// so no in-flight frame can still reference it (same reasoning as the
// house-standard retire lists, without the list - one draw per frame).
//
// License: GPL-3.0-or-later (as the rest of AAE).
// ASCII-only comments.
// -----------------------------------------------------------------------------
#pragma once
#ifndef SHOT_DRAW_VK_H
#define SHOT_DRAW_VK_H

#include "sys_vk.h"
#include "../aae_video/vector_draw_gl.h"   // txdata (GL-free: colordefs + MathUtils)

#include <string>

struct ShotDrawVKCreateInfo
{
    const char* vertSpv = "shaders/vk/vector_shot_tex_vk.vert.spv";
    const char* fragSpv = "shaders/vk/vector_shot_tex_vk.frag.spv";

    // Color format of the target this pass renders into (the beam RT).
    // VK_FORMAT_UNDEFINED falls back to ctx.swapchainFormat.
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;

    uint32_t initialVertexCapacity = 1536;   // 256 shots (6 verts each)
};

class ShotDrawVK
{
public:
    ShotDrawVK() = default;
    ~ShotDrawVK() = default;
    ShotDrawVK(const ShotDrawVK&) = delete;
    ShotDrawVK& operator=(const ShotDrawVK&) = delete;

    bool Init(VkContext& ctx, const ShotDrawVKCreateInfo* ci = nullptr);
    void Shutdown(VkContext& ctx);

    // Records the shot triangle list into the ALREADY-OPEN pass on 'cmd'
    // (the beam RT pass). 'proj' is the same column-major beam ortho passed
    // to VectorDrawVK::Record; the viewport is flipped (negative height)
    // to match its convention. count is in VERTICES (6 per shot). fadeInner/
    // fadeOuter mirror GL's kShotFadeInner/kShotFadeOuter.
    void Record(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                const float proj[16], const txdata* verts, uint32_t count,
                VkImageView texView, VkSampler texSampler,
                uint32_t targetWidth, uint32_t targetHeight,
                float fadeInner = 0.20f, float fadeOuter = 1.00f);

    bool IsValid() const { return initialized_; }

private:
    struct ShotPush
    {
        float proj[16];
        float fade[4];   // x = inner, y = outer
    };

    bool EnsureBuffer(VkContext& ctx, uint32_t frameIndex, VkDeviceSize neededBytes);

    bool initialized_ = false;
    std::string vertSpv_, fragSpv_;
    uint32_t initialCap_ = 1536;

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descSets_[VkContext::kFramesInFlight]{};

    VkBuffer       vbo_[VkContext::kFramesInFlight]{};
    VkDeviceMemory vboMem_[VkContext::kFramesInFlight]{};
    void*          vboMapped_[VkContext::kFramesInFlight]{};
    VkDeviceSize   vboCap_[VkContext::kFramesInFlight]{};
};

#endif // SHOT_DRAW_VK_H
