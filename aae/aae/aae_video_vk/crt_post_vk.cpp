//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// crt_post_vk.cpp - Vulkan RASTER CRT post chain.
// See crt_post_vk.h for the GL correspondence and the ordering contract.
// ASCII-only comments.
// -----------------------------------------------------------------------------

#include "crt_post_vk.h"
#include "sys_log.h"

#include <string.h>
#include <stdio.h>
#include <vector>

// -----------------------------------------------------------------------------
// File-local helpers (same shape as screen_quad_vk.cpp / vector_post_vk.cpp).
// -----------------------------------------------------------------------------
static bool CrtReadFileBytes_(const char* path, std::vector<uint8_t>& out)
{
    if (!path || !*path) return false;
    FILE* fp = nullptr;
#ifdef _WIN32
    if (fopen_s(&fp, path, "rb") != 0 || !fp) return false;
#else
    fp = fopen(path, "rb");
    if (!fp) return false;
#endif
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n <= 0) { fclose(fp); return false; }
    out.resize((size_t)n);
    size_t r = fread(out.data(), 1, (size_t)n, fp);
    fclose(fp);
    return r == (size_t)n;
}

static VkShaderModule CrtCreateShaderModule_(VkContext& ctx, const char* path)
{
    std::vector<uint8_t> bytes;
    if (!CrtReadFileBytes_(path, bytes))
    {
        LOG_ERROR("CrtPostVK: failed to read %s", path ? path : "(null)");
        return VK_NULL_HANDLE;
    }
    if ((bytes.size() & 3u) != 0u)
    {
        LOG_ERROR("CrtPostVK: SPV size not 4-byte aligned: %s", path);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = bytes.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(bytes.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    if (ctx.vkCreateShaderModule_(ctx.device, &ci, nullptr, &mod) != VK_SUCCESS)
    {
        LOG_ERROR("CrtPostVK: vkCreateShaderModule failed for %s", path);
        return VK_NULL_HANDLE;
    }
    return mod;
}

// The push block is sized to the Vulkan guaranteed minimum exactly; adding a
// field would silently exceed it on minimum-spec drivers.
static_assert(sizeof(float) * 32 == 128, "CrtPush must stay at 128 bytes");

// -----------------------------------------------------------------------------
// Init / Shutdown
// -----------------------------------------------------------------------------
bool CrtPostVK::Init(VkContext& ctx, const CrtPostVKCreateInfo* ci)
{
    if (!ctx.device)
        return false;
    if (initialized_)
        Shutdown(ctx);   // idempotence, house discipline

    CrtPostVKCreateInfo def{};
    if (!ci) ci = &def;

    vertSpv_ = ci->vertSpv;
    fragSpv_[KIND_MONO] = ci->monoSpv;
    fragSpv_[KIND_COLOR] = ci->colorSpv;
    fragSpv_[KIND_SCAN] = ci->scanSpv;
    gameRtFormat_ = ci->gameRtFormat;

    // Descriptor set layout: binding 0 = the single sampled source.
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dsl.bindingCount = 1;
    dsl.pBindings = &b;
    if (ctx.vkCreateDescriptorSetLayout_(ctx.device, &dsl, nullptr, &setLayout_) != VK_SUCCESS)
    {
        LOG_ERROR("CrtPostVK: vkCreateDescriptorSetLayout failed");
        return false;
    }

    // 128-byte push block, visible to both stages (the VS reads rect/tsize/
    // uvrect, the FS reads everything else). 128 is the Vulkan guaranteed
    // minimum for maxPushConstantsSize - the block is sized to it exactly.
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = (uint32_t)sizeof(CrtPush);

    VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &setLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    if (ctx.vkCreatePipelineLayout_(ctx.device, &pli, nullptr, &pipeLayout_) != VK_SUCCESS)
    {
        LOG_ERROR("CrtPostVK: vkCreatePipelineLayout failed");
        Shutdown(ctx);
        return false;
    }

    // Descriptor ring.
    const uint32_t totalSets = VkContext::kFramesInFlight * kSlotsPerFrame;

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = totalSets;

    VkDescriptorPoolCreateInfo dpi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpi.maxSets = totalSets;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    if (ctx.vkCreateDescriptorPool_(ctx.device, &dpi, nullptr, &descPool_) != VK_SUCCESS)
    {
        LOG_ERROR("CrtPostVK: vkCreateDescriptorPool failed");
        Shutdown(ctx);
        return false;
    }

    for (uint32_t fi = 0; fi < VkContext::kFramesInFlight; ++fi)
    {
        VkDescriptorSetLayout layouts[kSlotsPerFrame];
        for (uint32_t s = 0; s < kSlotsPerFrame; ++s)
            layouts[s] = setLayout_;

        VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        dsai.descriptorPool = descPool_;
        dsai.descriptorSetCount = kSlotsPerFrame;
        dsai.pSetLayouts = layouts;
        if (ctx.vkAllocateDescriptorSets_(ctx.device, &dsai, sets_[fi]) != VK_SUCCESS)
        {
            LOG_ERROR("CrtPostVK: vkAllocateDescriptorSets failed (fi=%u)", fi);
            Shutdown(ctx);
            return false;
        }
        cursor_[fi] = 0;
    }

    // REPEAT + NEAREST sampler for the tiled scanline overlay. GL sets
    // GL_REPEAT/GL_REPEAT + GL_NEAREST min AND mag on the overlay texture on
    // every render_scanlines call; the sys_vk texture builder hands out
    // CLAMP_TO_EDGE + linear, so the scanline draw pairs the loaded texture's
    // VIEW with this sampler instead of the texture's own.
    {
        VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sci.magFilter = VK_FILTER_NEAREST;
        sci.minFilter = VK_FILTER_NEAREST;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.minLod = 0.0f;
        sci.maxLod = 0.0f;
        sci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        if (ctx.vkCreateSampler_(ctx.device, &sci, nullptr, &tileSampler_) != VK_SUCCESS)
        {
            LOG_ERROR("CrtPostVK: vkCreateSampler failed");
            Shutdown(ctx);
            return false;
        }
    }

    // Pre-build the variants we know we need so a shader problem surfaces at
    // init, not on the frame that first enables a CRT effect. Other formats
    // come lazily through GetPipeline_.
    if (!GetPipeline_(ctx, KIND_SCAN, gameRtFormat_) ||
        !GetPipeline_(ctx, KIND_MONO, ctx.swapchainFormat) ||
        !GetPipeline_(ctx, KIND_COLOR, ctx.swapchainFormat))
    {
        LOG_ERROR("CrtPostVK: pipeline pre-build failed");
        Shutdown(ctx);
        return false;
    }

    initialized_ = true;
    LOG_INFO("CrtPostVK: initialized (scan fmt=%d, monitor fmt=%d)",
             (int)gameRtFormat_, (int)ctx.swapchainFormat);
    return true;
}

void CrtPostVK::Shutdown(VkContext& ctx)
{
    for (int k = 0; k < KIND_COUNT; ++k)
    {
        for (uint32_t i = 0; i < pipeCount_[k]; ++i)
        {
            if (pipe_[k][i])
                ctx.vkDestroyPipeline_(ctx.device, pipe_[k][i], nullptr);
            pipe_[k][i] = VK_NULL_HANDLE;
            pipeFmt_[k][i] = VK_FORMAT_UNDEFINED;
        }
        pipeCount_[k] = 0;
    }

    if (tileSampler_) { ctx.vkDestroySampler_(ctx.device, tileSampler_, nullptr); tileSampler_ = VK_NULL_HANDLE; }
    if (pipeLayout_) { ctx.vkDestroyPipelineLayout_(ctx.device, pipeLayout_, nullptr); pipeLayout_ = VK_NULL_HANDLE; }
    if (descPool_) { ctx.vkDestroyDescriptorPool_(ctx.device, descPool_, nullptr); descPool_ = VK_NULL_HANDLE; }
    if (setLayout_) { ctx.vkDestroyDescriptorSetLayout_(ctx.device, setLayout_, nullptr); setLayout_ = VK_NULL_HANDLE; }

    for (uint32_t fi = 0; fi < VkContext::kFramesInFlight; ++fi)
    {
        for (uint32_t s = 0; s < kSlotsPerFrame; ++s)
            sets_[fi][s] = VK_NULL_HANDLE;
        cursor_[fi] = 0;
    }
    lastFrameIndexSeen_ = 0xFFFFFFFFu;
    initialized_ = false;
}

// -----------------------------------------------------------------------------
// Pipelines
// -----------------------------------------------------------------------------
bool CrtPostVK::BuildPipeline_(VkContext& ctx, VkFormat fmt, const char* fragPath,
                               bool multiplyBlend, VkPipeline& out)
{
    out = VK_NULL_HANDLE;

    VkShaderModule vs = CrtCreateShaderModule_(ctx, vertSpv_.c_str());
    VkShaderModule fs = CrtCreateShaderModule_(ctx, fragPath);
    if (!vs || !fs)
    {
        if (vs) ctx.vkDestroyShaderModule_(ctx.device, vs, nullptr);
        if (fs) ctx.vkDestroyShaderModule_(ctx.device, fs, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    // No vertex input: gl_VertexIndex generates the strip corners.
    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (multiplyBlend)
    {
        // GL render_scanlines: glBlendFunc(GL_DST_COLOR, GL_ZERO) -> dst*src.
        // GL applies the one glBlendFunc pair to alpha too, substituting the
        // alpha-channel equivalent (DST_COLOR -> DST_ALPHA), mirrored here.
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    else
    {
        // GL monitor passes: glDisable(GL_BLEND), straight replace.
        cba.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    VkPipelineRenderingCreateInfo prci{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    prci.colorAttachmentCount = 1;
    prci.pColorAttachmentFormats = &fmt;

    VkGraphicsPipelineCreateInfo gpi{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpi.pNext = &prci;
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vp;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pColorBlendState = &cb;
    gpi.pDynamicState = &ds;
    gpi.layout = pipeLayout_;

    VkResult pr = ctx.vkCreateGraphicsPipelines_(
        ctx.device, ctx.pipelineCache, 1, &gpi, nullptr, &out);

    ctx.vkDestroyShaderModule_(ctx.device, vs, nullptr);
    ctx.vkDestroyShaderModule_(ctx.device, fs, nullptr);

    if (pr != VK_SUCCESS)
    {
        LOG_ERROR("CrtPostVK: vkCreateGraphicsPipelines failed (VkResult=%d, frag=%s, format=%d)",
                  (int)pr, fragPath ? fragPath : "(null)", (int)fmt);
        out = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

VkPipeline CrtPostVK::GetPipeline_(VkContext& ctx, int kind, VkFormat fmt)
{
    if (kind < 0 || kind >= KIND_COUNT)
        return VK_NULL_HANDLE;

    for (uint32_t i = 0; i < pipeCount_[kind]; ++i)
    {
        if (pipeFmt_[kind][i] == fmt)
            return pipe_[kind][i];
    }
    if (pipeCount_[kind] >= kMaxFormats)
    {
        LOG_ERROR("CrtPostVK: pipeline variant cache full (kind %d, format %d)", kind, (int)fmt);
        return VK_NULL_HANDLE;
    }

    VkPipeline p = VK_NULL_HANDLE;
    if (!BuildPipeline_(ctx, fmt, fragSpv_[kind].c_str(), /*multiplyBlend=*/kind == KIND_SCAN, p))
        return VK_NULL_HANDLE;

    LOG_INFO("CrtPostVK: created pipeline variant (kind %d, format %d)", kind, (int)fmt);
    pipeFmt_[kind][pipeCount_[kind]] = fmt;
    pipe_[kind][pipeCount_[kind]] = p;
    ++pipeCount_[kind];
    return p;
}

// -----------------------------------------------------------------------------
// Per-frame
// -----------------------------------------------------------------------------
void CrtPostVK::OnFrameBegin(uint32_t frameIndex)
{
    if (frameIndex >= VkContext::kFramesInFlight)
        return;
    cursor_[frameIndex] = 0;
    lastFrameIndexSeen_ = frameIndex;
}

// -----------------------------------------------------------------------------
// DrawQuad_ - one quad into the currently open pass.
// -----------------------------------------------------------------------------
void CrtPostVK::DrawQuad_(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                          VkPipeline pipe, VkImageView view, VkSampler sampler,
                          const CrtPush& push, int targetW, int targetH)
{
    if (!pipe || cmd == VK_NULL_HANDLE)
        return;
    if (frameIndex >= VkContext::kFramesInFlight)
    {
        LOG_ERROR("CrtPostVK: frameIndex out of range (%u)", frameIndex);
        return;
    }
    if (view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE)
    {
        LOG_ERROR("CrtPostVK: invalid source view/sampler");
        return;
    }

    // Lazy cursor reset on a frame-slot change (fallback for a missed
    // OnFrameBegin; same guard ScreenQuadVK::RecordRect uses).
    if (lastFrameIndexSeen_ != frameIndex)
    {
        cursor_[frameIndex] = 0;
        lastFrameIndexSeen_ = frameIndex;
    }
    if (cursor_[frameIndex] >= kSlotsPerFrame)
    {
        LOG_ERROR("CrtPostVK: descriptor ring exhausted this frame (%u slots)", kSlotsPerFrame);
        return;
    }
    VkDescriptorSet set = sets_[frameIndex][cursor_[frameIndex]++];

    // Rewriting this slot's set is safe: the frame slot's fence wait (in
    // VK_BeginFrame, before OnFrameBegin) proved its previous submission done.
    VkDescriptorImageInfo ii{};
    ii.sampler = sampler;
    ii.imageView = view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet = set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    ctx.vkUpdateDescriptorSets_(ctx.device, 1, &w, 0, nullptr);

    ctx.vkCmdBindPipeline_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    ctx.vkCmdBindDescriptorSets_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 pipeLayout_, 0, 1, &set, 0, nullptr);

    // STANDARD (positive-height) viewport: the push rect is in Y-DOWN target
    // pixels, so rect.y0 is the top edge and an identity uvrect is
    // row-preserving (source row r -> dest row r). Same convention as
    // VectorPostVK, deliberately NOT ScreenQuadVK's flipped one.
    VkViewport vpo{};
    vpo.x = 0.0f;
    vpo.y = 0.0f;
    vpo.width = (float)targetW;
    vpo.height = (float)targetH;
    vpo.minDepth = 0.0f;
    vpo.maxDepth = 1.0f;
    ctx.vkCmdSetViewport_(cmd, 0, 1, &vpo);

    VkRect2D sc{};
    sc.offset = { 0, 0 };
    sc.extent.width = (uint32_t)targetW;
    sc.extent.height = (uint32_t)targetH;
    ctx.vkCmdSetScissor_(cmd, 0, 1, &sc);

    ctx.vkCmdPushConstants_(cmd, pipeLayout_,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, (uint32_t)sizeof(CrtPush), &push);
    ctx.vkCmdDraw_(cmd, 4, 1, 0, 0);
}

// -----------------------------------------------------------------------------
// RecordScanlines - GL render_scanlines (opengl_renderer.cpp:1202).
//
// SCANLINE PITCH TRACE (GL parity):
//   GL renders the game into fbo_raster at rw x rh = (oriented visible area)
//   * config.prescale and tiles the overlay over that whole target with
//   u = rw/texW, v = rh/texH - so the texture repeats rh/texH times down the
//   game image and one pattern period occupies texH TEXELS of the target,
//   whatever the window size. The letterbox blit then stretches the target
//   to the on-screen game rect, making the ON-SCREEN period
//   (game rect height in px) * texH / rh.
//
//   The VK game RT is now the SAME rw x rh (vulkan_renderer.cpp sizes it
//   native * config.prescale), so targetW/targetH ARE rw/rh and the tiling
//   is GL's, term for term, with no correction factor. The earlier
//   `targetW * prescale / texW` compensation existed only because the RT was
//   unscaled: it reproduced GL's repeat COUNT but squeezed each period into
//   1/prescale as many texels, which at prescale 4 with a 4-tall pattern is
//   a single texel per period - unrepresentable. That factor is gone.
//
// V DIRECTION: GL's loader (load_texture) always calls
//   stbi_set_flip_vertically_on_load(1), so v = 0 is the image's BOTTOM row,
//   and render_scanlines' y-UP ortho puts v = 0 at the game image's bottom.
//   The VK RT stores the game TOP in row 0, so v = 0 belongs at the rect's
//   BOTTOM corner here - hence the inverted V in uvrect below (the caller
//   loads the texture stbi-flipped to match).
// -----------------------------------------------------------------------------
void CrtPostVK::RecordScanlines(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                                VkImageView texView,
                                int texW, int texH,
                                int targetW, int targetH)
{
    if (!initialized_ || targetW <= 0 || targetH <= 0 || texW <= 0 || texH <= 0)
        return;

    VkPipeline pipe = GetPipeline_(ctx, KIND_SCAN, VK_ActiveColorFormat(ctx));
    if (!pipe)
        return;

    // GL render_scanlines: u = (float)rw / scan_x, v = (float)rh / scan_y over
    // the full rw x rh target. targetW/targetH are that same rw/rh (the caller
    // passes the PRESCALED game RT dims, already truncated the way GL
    // truncates them), so this is GL's line verbatim.
    const float u = (float)targetW / (float)texW;
    const float v = (float)targetH / (float)texH;

    CrtPush push{};
    push.rect[0] = 0.0f;            push.rect[1] = 0.0f;
    push.rect[2] = (float)targetW;  push.rect[3] = (float)targetH;
    push.tsize[0] = (float)targetW; push.tsize[1] = (float)targetH;
    push.tsize[2] = 0.0f;           push.tsize[3] = 0.0f;
    // (u,v) at the rect's TOP-left corner, (u,v) at the BOTTOM-right corner.
    push.uvrect[0] = 0.0f;          push.uvrect[1] = v;
    push.uvrect[2] = u;             push.uvrect[3] = 0.0f;

    DrawQuad_(ctx, cmd, frameIndex, pipe, texView, tileSampler_, push, targetW, targetH);
}

// -----------------------------------------------------------------------------
// RecordMonitor - GL render_mono_monitor / render_color_monitor.
// -----------------------------------------------------------------------------
void CrtPostVK::RecordMonitor(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                              bool colorPass,
                              VkImageView srcView, VkSampler srcSampler,
                              const CrtMonitorParamsVK& p,
                              float x0, float y0, float x1, float y1,
                              int targetW, int targetH)
{
    if (!initialized_ || targetW <= 0 || targetH <= 0)
        return;

    VkPipeline pipe = GetPipeline_(ctx, colorPass ? KIND_COLOR : KIND_MONO,
                                   VK_ActiveColorFormat(ctx));
    if (!pipe)
        return;

    CrtPush push{};
    push.rect[0] = x0; push.rect[1] = y0; push.rect[2] = x1; push.rect[3] = y1;
    push.tsize[0] = (float)targetW;
    push.tsize[1] = (float)targetH;
    // Shadow-mask fragment origin = the rect's corner (GL's fbo_mono origin).
    push.tsize[2] = x0;
    push.tsize[3] = y0;
    // Full source, no flip: the VK game RT stores the game's top scanline in
    // row 0, and the rect is y-down, so v = 0 at the top is upright.
    push.uvrect[0] = 0.0f; push.uvrect[1] = 0.0f;
    push.uvrect[2] = 1.0f; push.uvrect[3] = 1.0f;

    push.p0[0] = p.srcW;      push.p0[1] = p.srcH;
    push.p0[2] = p.lodBias;   push.p0[3] = p.blurH;
    push.p1[0] = p.blurV;     push.p1[1] = p.converge;
    push.p1[2] = p.halation;  push.p1[3] = p.halRadius;
    push.p2[0] = p.scanline;  push.p2[1] = p.contrast;
    push.p2[2] = p.bright;    push.p2[3] = p.saturation;
    push.p3[0] = p.maskType;  push.p3[1] = p.maskStrength;
    push.p3[2] = p.maskScale; push.p3[3] = 0.0f;
    push.tint[0] = p.tint[0]; push.tint[1] = p.tint[1];
    push.tint[2] = p.tint[2]; push.tint[3] = 0.0f;

    DrawQuad_(ctx, cmd, frameIndex, pipe, srcView, srcSampler, push, targetW, targetH);
}
