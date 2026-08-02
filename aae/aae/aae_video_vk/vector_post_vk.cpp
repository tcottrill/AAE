// -----------------------------------------------------------------------------
// vector_post_vk.cpp - Vulkan vector post chain (Phase 4a Plan 7).
// See vector_post_vk.h for the GL -> VK object map and frame shape.
// ASCII-only comments.
// -----------------------------------------------------------------------------

#include "vector_post_vk.h"
#include "sys_log.h"

#include <stdio.h>
#include <string.h>
#include <vector>

// -----------------------------------------------------------------------------
// File-local helpers (same pattern as screen_quad_vk.cpp)
// -----------------------------------------------------------------------------
static bool PostReadFileBytes_(const char* path, std::vector<uint8_t>& out)
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

static VkShaderModule PostCreateShaderModule_(VkContext& ctx, const char* path)
{
    std::vector<uint8_t> bytes;
    if (!PostReadFileBytes_(path, bytes))
    {
        LOG_ERROR("VectorPostVK: failed to read %s", path ? path : "(null)");
        return VK_NULL_HANDLE;
    }
    if ((bytes.size() & 3u) != 0u)
    {
        LOG_ERROR("VectorPostVK: SPV size not 4-byte aligned: %s", path);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = bytes.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(bytes.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    if (ctx.vkCreateShaderModule_(ctx.device, &ci, nullptr, &mod) != VK_SUCCESS)
    {
        LOG_ERROR("VectorPostVK: vkCreateShaderModule failed for %s", path);
        return VK_NULL_HANDLE;
    }
    return mod;
}

// Blend modes for BuildPipeline.
enum
{
    POST_BLEND_NONE = 0,   // downsample copies
    POST_BLEND_ADD  = 1,   // SRC_ALPHA/ONE      (GL glow accumulate)
    POST_BLEND_TRAIL = 2,  // ONE_MINUS_DST_COLOR/SRC_ALPHA (GL trail)
    POST_BLEND_ONEONE = 3, // ONE/ONE            (GL composite into cleared img4b)
};

// -----------------------------------------------------------------------------
// Init / Shutdown
// -----------------------------------------------------------------------------
bool VectorPostVK::Init(VkContext& ctx, const VectorPostVKCreateInfo* ci)
{
    if (!ctx.device)
        return false;
    if (initialized_)
        Shutdown(ctx);   // idempotence, same discipline as VectorDrawVK fix 4c

    VectorPostVKCreateInfo def{};
    if (!ci) ci = &def;

    vertSpv_  = ci->vertSpv;
    blurSpv_  = ci->blurSpv;
    texSpv_   = ci->texSpv;
    multiSpv_ = ci->multiSpv;
    ssaa_     = (ci->ssaa >= 1) ? ci->ssaa : 1;
    beamDim_  = 1024 * ssaa_;

    // All post RTs are RGBA8_UNORM for the same reason as the raster game RT
    // (format trace in vulkan_renderer.cpp): byte-identical blending math to
    // the GL chain's non-color-managed FBOs, single sRGB encode at the
    // swapchain composite.
    RenderTargetVKCreateInfo rt{};
    rt.filter = rtFilterVK::Linear;
    rt.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

    rt.width = beamDim_;  rt.height = beamDim_;  rt.mipLevels = -1;
    if (!rtBeam_.Init(ctx, rt)) { LOG_ERROR("VectorPostVK: beam RT init failed"); Shutdown(ctx); return false; }

    rt.width = 1024;      rt.height = 1024;      rt.mipLevels = -1;
    if (!rtTrail_.Init(ctx, rt)) { LOG_ERROR("VectorPostVK: trail RT init failed"); Shutdown(ctx); return false; }

    rt.mipLevels = 1;
    rt.width = 512;  rt.height = 512;
    if (!rtGlowHalf_.Init(ctx, rt)) { LOG_ERROR("VectorPostVK: glow 512 RT init failed"); Shutdown(ctx); return false; }
    rt.width = 256;  rt.height = 256;
    if (!rtGlowA_.Init(ctx, rt)) { LOG_ERROR("VectorPostVK: glow A RT init failed"); Shutdown(ctx); return false; }
    if (!rtGlowB_.Init(ctx, rt)) { LOG_ERROR("VectorPostVK: glow B RT init failed"); Shutdown(ctx); return false; }

    if (!CreateLayouts(ctx))     { Shutdown(ctx); return false; }
    if (!CreateDescriptors(ctx)) { Shutdown(ctx); return false; }

    // The three RGBA8 offscreen pipelines are fixed-format; build them now so
    // shader problems surface at init. The composite variant is lazy (active
    // format known only inside the swapchain pass).
    const VkFormat rgba8 = VK_FORMAT_R8G8B8A8_UNORM;
    if (!BuildPipeline(ctx, rgba8, blurSpv_.c_str(), POST_BLEND_NONE,  pipeLayoutS_, pipeBlurCopy_) ||
        !BuildPipeline(ctx, rgba8, blurSpv_.c_str(), POST_BLEND_ADD,   pipeLayoutS_, pipeBlurAccum_) ||
        !BuildPipeline(ctx, rgba8, texSpv_.c_str(),  POST_BLEND_TRAIL, pipeLayoutS_, pipeTrail_))
    {
        Shutdown(ctx);
        return false;
    }

    beamReady_ = trailReady_ = glowReady_ = false;
    initialized_ = true;
    LOG_INFO("VectorPostVK: online (beam %dx%d ssaa=%d, trail 1024, glow 512/256/256)",
             beamDim_, beamDim_, ssaa_);
    return true;
}

void VectorPostVK::Shutdown(VkContext& ctx)
{
    for (uint32_t i = 0; i < compCount_; ++i)
    {
        if (compPipe_[i]) ctx.vkDestroyPipeline_(ctx.device, compPipe_[i], nullptr);
        compPipe_[i] = VK_NULL_HANDLE;
        compFormat_[i] = VK_FORMAT_UNDEFINED;
    }
    compCount_ = 0;

    if (pipeBlurCopy_)  { ctx.vkDestroyPipeline_(ctx.device, pipeBlurCopy_,  nullptr); pipeBlurCopy_  = VK_NULL_HANDLE; }
    if (pipeBlurAccum_) { ctx.vkDestroyPipeline_(ctx.device, pipeBlurAccum_, nullptr); pipeBlurAccum_ = VK_NULL_HANDLE; }
    if (pipeTrail_)     { ctx.vkDestroyPipeline_(ctx.device, pipeTrail_,     nullptr); pipeTrail_     = VK_NULL_HANDLE; }

    if (pipeLayoutS_) { ctx.vkDestroyPipelineLayout_(ctx.device, pipeLayoutS_, nullptr); pipeLayoutS_ = VK_NULL_HANDLE; }
    if (pipeLayoutM_) { ctx.vkDestroyPipelineLayout_(ctx.device, pipeLayoutM_, nullptr); pipeLayoutM_ = VK_NULL_HANDLE; }
    if (setLayoutS_)  { ctx.vkDestroyDescriptorSetLayout_(ctx.device, setLayoutS_, nullptr); setLayoutS_ = VK_NULL_HANDLE; }
    if (setLayoutM_)  { ctx.vkDestroyDescriptorSetLayout_(ctx.device, setLayoutM_, nullptr); setLayoutM_ = VK_NULL_HANDLE; }
    if (descPool_)    { ctx.vkDestroyDescriptorPool_(ctx.device, descPool_, nullptr); descPool_ = VK_NULL_HANDLE; }

    for (uint32_t fi = 0; fi < VkContext::kFramesInFlight; ++fi)
    {
        for (uint32_t s = 0; s < kSingleSlotsPerFrame; ++s) setsS_[fi][s] = VK_NULL_HANDLE;
        for (uint32_t s = 0; s < kMultiSlotsPerFrame; ++s)  setsM_[fi][s] = VK_NULL_HANDLE;
        cursorS_[fi] = 0;
        cursorM_[fi] = 0;
    }

    if (rtGlowB_.IsValid())    rtGlowB_.Shutdown(ctx);
    if (rtGlowA_.IsValid())    rtGlowA_.Shutdown(ctx);
    if (rtGlowHalf_.IsValid()) rtGlowHalf_.Shutdown(ctx);
    if (rtTrail_.IsValid())    rtTrail_.Shutdown(ctx);
    if (rtBeam_.IsValid())     rtBeam_.Shutdown(ctx);

    beamReady_ = trailReady_ = glowReady_ = false;
    initialized_ = false;
}

// -----------------------------------------------------------------------------
// Layouts / descriptors
// -----------------------------------------------------------------------------
bool VectorPostVK::CreateLayouts(VkContext& ctx)
{
    // Single-sampler layout (blur/tex passes).
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dsl{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        dsl.bindingCount = 1;
        dsl.pBindings = &b;
        if (ctx.vkCreateDescriptorSetLayout_(ctx.device, &dsl, nullptr, &setLayoutS_) != VK_SUCCESS)
        {
            LOG_ERROR("VectorPostVK: descriptor set layout (single) failed");
            return false;
        }
    }

    // Triple-sampler layout (composite).
    {
        VkDescriptorSetLayoutBinding b[3]{};
        for (uint32_t i = 0; i < 3; ++i)
        {
            b[i].binding = i;
            b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b[i].descriptorCount = 1;
            b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dsl{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        dsl.bindingCount = 3;
        dsl.pBindings = b;
        if (ctx.vkCreateDescriptorSetLayout_(ctx.device, &dsl, nullptr, &setLayoutM_) != VK_SUCCESS)
        {
            LOG_ERROR("VectorPostVK: descriptor set layout (multi) failed");
            return false;
        }
    }

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc.offset = 0;
    pc.size = sizeof(PostPush);

    VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount = 1;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pc;

    pli.pSetLayouts = &setLayoutS_;
    if (ctx.vkCreatePipelineLayout_(ctx.device, &pli, nullptr, &pipeLayoutS_) != VK_SUCCESS)
    {
        LOG_ERROR("VectorPostVK: pipeline layout (single) failed");
        return false;
    }
    pli.pSetLayouts = &setLayoutM_;
    if (ctx.vkCreatePipelineLayout_(ctx.device, &pli, nullptr, &pipeLayoutM_) != VK_SUCCESS)
    {
        LOG_ERROR("VectorPostVK: pipeline layout (multi) failed");
        return false;
    }
    return true;
}

bool VectorPostVK::CreateDescriptors(VkContext& ctx)
{
    const uint32_t totalS = VkContext::kFramesInFlight * kSingleSlotsPerFrame;
    const uint32_t totalM = VkContext::kFramesInFlight * kMultiSlotsPerFrame;

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = totalS * 1 + totalM * 3;

    VkDescriptorPoolCreateInfo dpi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpi.maxSets = totalS + totalM;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    if (ctx.vkCreateDescriptorPool_(ctx.device, &dpi, nullptr, &descPool_) != VK_SUCCESS)
    {
        LOG_ERROR("VectorPostVK: descriptor pool failed");
        return false;
    }

    for (uint32_t fi = 0; fi < VkContext::kFramesInFlight; ++fi)
    {
        {
            VkDescriptorSetLayout layouts[kSingleSlotsPerFrame];
            for (uint32_t s = 0; s < kSingleSlotsPerFrame; ++s) layouts[s] = setLayoutS_;
            VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            ai.descriptorPool = descPool_;
            ai.descriptorSetCount = kSingleSlotsPerFrame;
            ai.pSetLayouts = layouts;
            if (ctx.vkAllocateDescriptorSets_(ctx.device, &ai, setsS_[fi]) != VK_SUCCESS)
            {
                LOG_ERROR("VectorPostVK: descriptor alloc (single, fi=%u) failed", fi);
                return false;
            }
        }
        {
            VkDescriptorSetLayout layouts[kMultiSlotsPerFrame];
            for (uint32_t s = 0; s < kMultiSlotsPerFrame; ++s) layouts[s] = setLayoutM_;
            VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            ai.descriptorPool = descPool_;
            ai.descriptorSetCount = kMultiSlotsPerFrame;
            ai.pSetLayouts = layouts;
            if (ctx.vkAllocateDescriptorSets_(ctx.device, &ai, setsM_[fi]) != VK_SUCCESS)
            {
                LOG_ERROR("VectorPostVK: descriptor alloc (multi, fi=%u) failed", fi);
                return false;
            }
        }
        cursorS_[fi] = 0;
        cursorM_[fi] = 0;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Pipeline construction (dynamic rendering, no vertex input, dynamic
// viewport/scissor - same skeleton as ScreenQuadVK::RectBuildPipelineForFormat_
// minus the vertex buffer).
// -----------------------------------------------------------------------------
bool VectorPostVK::BuildPipeline(VkContext& ctx, VkFormat colorFormat,
                                 const char* fragPath, int blendMode,
                                 VkPipelineLayout layout, VkPipeline& out)
{
    out = VK_NULL_HANDLE;

    VkShaderModule vs = PostCreateShaderModule_(ctx, vertSpv_.c_str());
    VkShaderModule fs = PostCreateShaderModule_(ctx, fragPath);
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

    // GL glBlendFunc sets ONE pair for color and alpha alike; GL substitutes
    // the alpha-channel equivalent for color-only factors (DST_COLOR ->
    // DST_ALPHA), mirrored below for the trail mode.
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    switch (blendMode)
    {
    case POST_BLEND_NONE:
        cba.blendEnable = VK_FALSE;
        break;
    case POST_BLEND_ADD:
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case POST_BLEND_TRAIL:
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case POST_BLEND_ONEONE:
    default:
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
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
    prci.pColorAttachmentFormats = &colorFormat;

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
    gpi.layout = layout;

    VkResult pr = ctx.vkCreateGraphicsPipelines_(
        ctx.device, ctx.pipelineCache, 1, &gpi, nullptr, &out);

    ctx.vkDestroyShaderModule_(ctx.device, vs, nullptr);
    ctx.vkDestroyShaderModule_(ctx.device, fs, nullptr);

    if (pr != VK_SUCCESS)
    {
        LOG_ERROR("VectorPostVK: vkCreateGraphicsPipelines failed (VkResult=%d, blend=%d, format=%d)",
                  (int)pr, blendMode, (int)colorFormat);
        out = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

VkPipeline VectorPostVK::GetCompositePipeline(VkContext& ctx)
{
    const VkFormat fmt = VK_ActiveColorFormat(ctx);
    for (uint32_t i = 0; i < compCount_; ++i)
        if (compFormat_[i] == fmt)
            return compPipe_[i];

    if (compCount_ >= kMaxCompositeVariants)
    {
        LOG_ERROR("VectorPostVK: composite variant cache full (format %d)", (int)fmt);
        return VK_NULL_HANDLE;
    }

    VkPipeline p = VK_NULL_HANDLE;
    if (!BuildPipeline(ctx, fmt, multiSpv_.c_str(), POST_BLEND_ONEONE, pipeLayoutM_, p))
        return VK_NULL_HANDLE;

    LOG_INFO("VectorPostVK: composite pipeline variant for format %d", (int)fmt);
    compFormat_[compCount_] = fmt;
    compPipe_[compCount_] = p;
    ++compCount_;
    return p;
}

// -----------------------------------------------------------------------------
// Per-frame
// -----------------------------------------------------------------------------
void VectorPostVK::OnFrameBegin(uint32_t frameIndex)
{
    if (frameIndex >= VkContext::kFramesInFlight)
        return;
    cursorS_[frameIndex] = 0;
    cursorM_[frameIndex] = 0;
}

void VectorPostVK::BeginBeamPass(VkContext& ctx, VkCommandBuffer cmd)
{
    if (!initialized_) return;
    // Clear to transparent black: the GL fbo1 clear is (0,0,0,0) and the
    // trail/glow blend math reads the accumulated beam ALPHA (SRC_ALPHA
    // factors), so an opaque clear would saturate both effects.
    rtBeam_.Begin(ctx, cmd, /*clear=*/true, 0.0f, 0.0f, 0.0f, 0.0f);
}

void VectorPostVK::EndBeamPass(VkContext& ctx, VkCommandBuffer cmd)
{
    if (!initialized_) return;
    rtBeam_.End(ctx, cmd);
    if (rtBeam_.GetMipLevels() > 1)
        rtBeam_.GenerateMips(ctx, cmd);
    beamReady_ = true;
}

// -----------------------------------------------------------------------------
// DrawQuadS - one single-sampler quad into the currently open pass.
// -----------------------------------------------------------------------------
void VectorPostVK::DrawQuadS(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                             VkPipeline pipeline, VkImageView view, VkSampler sampler,
                             const PostPush& push, int targetW, int targetH)
{
    if (frameIndex >= VkContext::kFramesInFlight || !pipeline)
        return;
    if (cursorS_[frameIndex] >= kSingleSlotsPerFrame)
    {
        LOG_ERROR("VectorPostVK: single-descriptor ring exhausted this frame");
        return;
    }
    VkDescriptorSet set = setsS_[frameIndex][cursorS_[frameIndex]++];

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

    ctx.vkCmdBindPipeline_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    ctx.vkCmdBindDescriptorSets_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 pipeLayoutS_, 0, 1, &set, 0, nullptr);

    // STANDARD viewport (see header: y-down rects, row-preserving copies).
    VkViewport vpo{};
    vpo.x = 0.0f;
    vpo.y = 0.0f;
    vpo.width = (float)targetW;
    vpo.height = (float)targetH;
    vpo.minDepth = 0.0f;
    vpo.maxDepth = 1.0f;
    ctx.vkCmdSetViewport_(cmd, 0, 1, &vpo);

    VkRect2D sc{};
    sc.extent.width = (uint32_t)targetW;
    sc.extent.height = (uint32_t)targetH;
    ctx.vkCmdSetScissor_(cmd, 0, 1, &sc);

    ctx.vkCmdPushConstants_(cmd, pipeLayoutS_,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(PostPush), &push);
    ctx.vkCmdDraw_(cmd, 4, 1, 0, 0);
}

// -----------------------------------------------------------------------------
// RecordPost - trail accumulate + glow cascade (offscreen; no pass open on
// entry or exit).
// -----------------------------------------------------------------------------
void VectorPostVK::RecordPost(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                              int vectrail, int vecglow, bool clearTrail)
{
    if (!initialized_ || !beamReady_)
        return;

    PostPush push{};

    // ---- Trail (GL final_render LAYER 3): beam frame -> trail accumulator
    // with blend ONE_MINUS_DST_COLOR/SRC_ALPHA and the per-level decay tint.
    if (vectrail > 0)
    {
        float tr = 1.0f, tg = 1.0f, tb = 1.0f, ta = 1.0f;
        switch (vectrail)
        {
        case 1:  ta = 0.825f; break;
        case 2:  ta = 0.86f;  break;
        case 3:  ta = 0.93f;  break;
        default: tr = tg = tb = 0.95f; ta = 1.0f; break;
        }

        rtTrail_.Begin(ctx, cmd, /*clear=*/clearTrail, 0.0f, 0.0f, 0.0f, 0.0f);
        push.rect[0] = 0.0f; push.rect[1] = 0.0f;
        push.rect[2] = 1024.0f; push.rect[3] = 1024.0f;
        push.tsize[0] = 1024.0f; push.tsize[1] = 1024.0f;
        push.uvrect[0] = 0.0f; push.uvrect[1] = 0.0f;
        push.uvrect[2] = 1.0f; push.uvrect[3] = 1.0f;
        push.tint[0] = tr; push.tint[1] = tg; push.tint[2] = tb; push.tint[3] = ta;
        DrawQuadS(ctx, cmd, frameIndex, pipeTrail_,
                  rtBeam_.VK_GetColorView(), rtBeam_.VK_GetSampler(),
                  push, 1024, 1024);
        rtTrail_.End(ctx, cmd);
        if (rtTrail_.GetMipLevels() > 1)
            rtTrail_.GenerateMips(ctx, cmd);
        trailReady_ = true;
    }

    // ---- Glow (GL copy_main_img_to_fbo2 / copy_fbo2_to_fbo3 /
    // render_blur_image_fbo3): beam -> 512 -> 256, then 4 ping-pong additive
    // blur passes; the composite samples the B side (GL's img3b).
    if (vecglow > 0)
    {
        push.tint[0] = push.tint[1] = push.tint[2] = push.tint[3] = 1.0f;

        // Downsample 1: beam (trilinear-mipped) -> 512, fragBlur at 512.
        rtGlowHalf_.Begin(ctx, cmd, /*clear=*/true, 0.0f, 0.0f, 0.0f, 0.0f);
        push.rect[0] = 0.0f; push.rect[1] = 0.0f; push.rect[2] = 512.0f; push.rect[3] = 512.0f;
        push.tsize[0] = 512.0f; push.tsize[1] = 512.0f;
        push.uvrect[0] = 0.0f; push.uvrect[1] = 0.0f; push.uvrect[2] = 1.0f; push.uvrect[3] = 1.0f;
        push.params[0] = 512.0f; push.params[1] = 512.0f;
        DrawQuadS(ctx, cmd, frameIndex, pipeBlurCopy_,
                  rtBeam_.VK_GetColorView(), rtBeam_.VK_GetSampler(),
                  push, 512, 512);
        rtGlowHalf_.End(ctx, cmd);

        // Downsample 2: 512 -> 256 (A side), fragBlur at 256.
        rtGlowA_.Begin(ctx, cmd, /*clear=*/true, 0.0f, 0.0f, 0.0f, 0.0f);
        push.rect[2] = 256.0f; push.rect[3] = 256.0f;
        push.tsize[0] = 256.0f; push.tsize[1] = 256.0f;
        push.params[0] = 256.0f; push.params[1] = 256.0f;
        DrawQuadS(ctx, cmd, frameIndex, pipeBlurCopy_,
                  rtGlowHalf_.VK_GetColorView(), rtGlowHalf_.VK_GetSampler(),
                  push, 256, 256);
        rtGlowA_.End(ctx, cmd);

        // Ping-pong (GL render_blur_image_fbo3): rows 0-3 of fshifta/fshiftb
        // (axis taps), near offset v1 A->B, far offset v2 B->A, additive
        // SRC_ALPHA/ONE, global sub-pixel correction applied to every quad.
        // B is cleared once per frame via the first A->B pass's clear
        // (GL clears img3b up front; LOAD+first-draw-into-clear is identical).
        static constexpr float v1 = 1.0f;
        static constexpr float v2 = 2.0f;
        static constexpr float gx = -0.05f;
        static constexpr float gy = -0.20f;
        static const float fshifta[] = {
             v1, 0,  -v1, 0,   0, v1,   0, -v1
        };
        static const float fshiftb[] = {
             v2, 0,  -v2, 0,   0, v2,   0, -v2
        };

        auto pingpong = [&](RenderTargetVK& dst, RenderTargetVK& src,
                            float ox, float oy, bool clear)
        {
            dst.Begin(ctx, cmd, clear, 0.0f, 0.0f, 0.0f, 0.0f);
            push.rect[0] = ox + gx;          push.rect[1] = oy + gy;
            push.rect[2] = 256.0f + ox + gx; push.rect[3] = 256.0f + oy + gy;
            push.tsize[0] = 256.0f; push.tsize[1] = 256.0f;
            push.params[0] = 256.0f; push.params[1] = 256.0f;
            DrawQuadS(ctx, cmd, frameIndex, pipeBlurAccum_,
                      src.VK_GetColorView(), src.VK_GetSampler(),
                      push, 256, 256);
            dst.End(ctx, cmd);
        };

        for (int pass = 0; pass < 4; ++pass)
        {
            pingpong(rtGlowB_, rtGlowA_, fshifta[pass * 2], fshifta[pass * 2 + 1],
                     /*clear=*/pass == 0);
            pingpong(rtGlowA_, rtGlowB_, fshiftb[pass * 2], fshiftb[pass * 2 + 1],
                     /*clear=*/false);
        }
        glowReady_ = true;
    }
}

// -----------------------------------------------------------------------------
// RecordComposite - fragMulti quad into the open swapchain pass.
// -----------------------------------------------------------------------------
void VectorPostVK::RecordComposite(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                                   float x0, float y0, float x1, float y1,
                                   int vectrail, int vecglow)
{
    if (!initialized_ || !beamReady_)
        return;
    if (frameIndex >= VkContext::kFramesInFlight)
        return;
    if (cursorM_[frameIndex] >= kMultiSlotsPerFrame)
    {
        LOG_ERROR("VectorPostVK: multi-descriptor ring exhausted this frame");
        return;
    }

    VkPipeline pipe = GetCompositePipeline(ctx);
    if (!pipe)
        return;

    // Readiness-gated flags: a config toggle can enable vectrail/vecglow on a
    // frame where that RT has not been rendered yet this session (or ever, if
    // paused) - the flags keep the shader contribution at 0 AND the bindings
    // below substitute the beam view so no UNDEFINED-layout image is sampled.
    const bool usefb   = (vectrail > 0) && trailReady_;
    const bool useglow = (vecglow  > 0) && glowReady_;

    VkDescriptorSet set = setsM_[frameIndex][cursorM_[frameIndex]++];

    VkDescriptorImageInfo ii[3]{};
    ii[0].sampler = rtBeam_.VK_GetSampler();
    ii[0].imageView = rtBeam_.VK_GetColorView();
    ii[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ii[1] = ii[0];
    if (glowReady_)
    {
        ii[1].sampler = rtGlowB_.VK_GetSampler();
        ii[1].imageView = rtGlowB_.VK_GetColorView();
    }
    ii[2] = ii[0];
    if (trailReady_)
    {
        ii[2].sampler = rtTrail_.VK_GetSampler();
        ii[2].imageView = rtTrail_.VK_GetColorView();
    }

    VkWriteDescriptorSet w[3]{};
    for (uint32_t i = 0; i < 3; ++i)
    {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i].pImageInfo = &ii[i];
    }
    ctx.vkUpdateDescriptorSets_(ctx.device, 3, w, 0, nullptr);

    const float sw = (float)ctx.swapchainExtent.width;
    const float sh = (float)ctx.swapchainExtent.height;

    PostPush push{};
    push.rect[0] = x0; push.rect[1] = y0;
    push.rect[2] = x1; push.rect[3] = y1;
    push.tsize[0] = sw; push.tsize[1] = sh;
    // The chain's ONE vertical flip (GL flip_v=true): V swapped so the rect's
    // top (min y, y-down) samples v=1 = beam row H = MAME y=0 = screen top.
    push.uvrect[0] = 0.0f; push.uvrect[1] = 1.0f;
    push.uvrect[2] = 1.0f; push.uvrect[3] = 0.0f;
    push.tint[0] = push.tint[1] = push.tint[2] = push.tint[3] = 1.0f;
    push.params[0] = (float)vecglow * 0.01f;      // glowamt
    push.params[1] = usefb   ? 1.0f : 0.0f;
    push.params[2] = useglow ? 1.0f : 0.0f;

    ctx.vkCmdBindPipeline_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    ctx.vkCmdBindDescriptorSets_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 pipeLayoutM_, 0, 1, &set, 0, nullptr);

    VkViewport vpo{};
    vpo.width = sw;
    vpo.height = sh;
    vpo.maxDepth = 1.0f;
    ctx.vkCmdSetViewport_(cmd, 0, 1, &vpo);

    VkRect2D sc{};
    sc.extent = ctx.swapchainExtent;
    ctx.vkCmdSetScissor_(cmd, 0, 1, &sc);

    ctx.vkCmdPushConstants_(cmd, pipeLayoutM_,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(PostPush), &push);
    ctx.vkCmdDraw_(cmd, 4, 1, 0, 0);
}
