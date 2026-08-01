// -----------------------------------------------------------------------------
// screen_quad_vk.cpp - Textured-rect quad renderer (Phase 4a Plan 4, Task 2).
// Imported from the Bosconian donor; see screen_quad_vk.h for what was
// dropped (legacy ctx-owned fullscreen path) and what was adapted (explicit
// SPV paths, ctx-carried vkCmdBindVertexBuffers).
// ASCII-only comments.
// -----------------------------------------------------------------------------

#include "screen_quad_vk.h"
#include "sys_log.h"

#include <string.h>
#include <stdio.h>
#include <vector>
#include <string>

// -----------------------------------------------------------------------------
// File-local helpers
// -----------------------------------------------------------------------------
static bool RectReadFileBytes_(const char* path, std::vector<uint8_t>& out)
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

// Adapted from the donor: takes the path as-is (the donor prefixed its
// engine's Shader_GetPath(); AAE passes explicit paths like FpolyVK does).
static VkShaderModule RectCreateShaderModule_(VkContext& ctx, const char* path)
{
    std::vector<uint8_t> bytes;
    if (!RectReadFileBytes_(path, bytes))
    {
        LOG_ERROR("ScreenQuadVK(rect): failed to read %s", path ? path : "(null)");
        return VK_NULL_HANDLE;
    }
    if ((bytes.size() & 3u) != 0u)
    {
        LOG_ERROR("ScreenQuadVK(rect): SPV size not 4-byte aligned: %s", path);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = bytes.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(bytes.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    if (ctx.vkCreateShaderModule_(ctx.device, &ci, nullptr, &mod) != VK_SUCCESS)
    {
        LOG_ERROR("ScreenQuadVK(rect): vkCreateShaderModule failed for %s", path);
        return VK_NULL_HANDLE;
    }
    return mod;
}

static void RectMakeOrtho_(float l, float r, float b, float t, float* out16)
{
    const float rl = r - l;
    const float tb = t - b;
    out16[0]  = 2.0f / rl;  out16[1]  = 0.0f;       out16[2]  = 0.0f;  out16[3]  = 0.0f;
    out16[4]  = 0.0f;       out16[5]  = 2.0f / tb;  out16[6]  = 0.0f;  out16[7]  = 0.0f;
    out16[8]  = 0.0f;       out16[9]  = 0.0f;       out16[10] = -1.0f; out16[11] = 0.0f;
    out16[12] = -(r + l) / rl;
    out16[13] = -(t + b) / tb;
    out16[14] = 0.0f;
    out16[15] = 1.0f;
}

// -----------------------------------------------------------------------------
// Init / Shutdown
// The donor's Init also built the legacy fullscreen VB and tolerated a
// RectInit_ failure (legacy callers kept working). With the legacy path
// dropped, RectInit_ failure is fatal.
// -----------------------------------------------------------------------------
bool ScreenQuadVK::Init(VkContext& ctx, const ScreenQuadVKCreateInfo* ci)
{
    if (!ctx.device)
        return false;

    if (ci)
    {
        if (ci->vertSpvPath && ci->vertSpvPath[0]) rect_vertSpvPath_ = ci->vertSpvPath;
        if (ci->fragSpvPath && ci->fragSpvPath[0]) rect_fragSpvPath_ = ci->fragSpvPath;
    }

    if (!RectInit_(ctx))
    {
        LOG_ERROR("ScreenQuadVK: RectInit_ failed");
        return false;
    }

    return true;
}

void ScreenQuadVK::Shutdown(VkContext& ctx)
{
    RectShutdown_(ctx);
}

bool ScreenQuadVK::CreateBuffer(VkContext& ctx,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memProps,
    VkBuffer& outBuf,
    VkDeviceMemory& outMem)
{
    outBuf = VK_NULL_HANDLE;
    outMem = VK_NULL_HANDLE;

    VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult vr = ctx.vkCreateBuffer_(ctx.device, &bci, nullptr, &outBuf);
    if (vr != VK_SUCCESS)
        return false;

    VkMemoryRequirements mr{};
    ctx.vkGetBufferMemoryRequirements_(ctx.device, outBuf, &mr);

    const uint32_t typeIndex = FindMemoryType(ctx, mr.memoryTypeBits, memProps);
    if (typeIndex == 0xFFFFFFFFu)
    {
        ctx.vkDestroyBuffer_(ctx.device, outBuf, nullptr);
        outBuf = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = typeIndex;

    vr = ctx.vkAllocateMemory_(ctx.device, &mai, nullptr, &outMem);
    if (vr != VK_SUCCESS)
    {
        ctx.vkDestroyBuffer_(ctx.device, outBuf, nullptr);
        outBuf = VK_NULL_HANDLE;
        return false;
    }

    vr = ctx.vkBindBufferMemory_(ctx.device, outBuf, outMem, 0);
    if (vr != VK_SUCCESS)
    {
        ctx.vkDestroyBuffer_(ctx.device, outBuf, nullptr);
        ctx.vkFreeMemory_(ctx.device, outMem, nullptr);
        outBuf = VK_NULL_HANDLE;
        outMem = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

uint32_t ScreenQuadVK::FindMemoryType(VkContext& ctx, uint32_t typeBits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mp{};
    ctx.vkGetPhysicalDeviceMemoryProperties_(ctx.phys, &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) == 0)
            continue;

        if ((mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }

    return 0xFFFFFFFFu;
}

// =============================================================================
//                     RECT-AWARE PATH (RecordRect)
// =============================================================================
// Owns its own pipeline, descriptor pool, and per-frame VBO/UBO ring.
// Architecture mirrors the donor engine's RenderTargetCompositor: same
// per-slot offset trick to avoid races on still-in-flight reads, same
// y-flipped-viewport convention.
// =============================================================================

// -----------------------------------------------------------------------------
// RectInit_
// -----------------------------------------------------------------------------
bool ScreenQuadVK::RectInit_(VkContext& ctx)
{
    // Descriptor set layout: binding 0 = UBO (mat4), binding 1 = sampler2D.
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dsl.bindingCount = 2;
    dsl.pBindings    = bindings;
    if (ctx.vkCreateDescriptorSetLayout_(ctx.device, &dsl, nullptr, &rect_setLayout_) != VK_SUCCESS)
    {
        LOG_ERROR("ScreenQuadVK(rect): vkCreateDescriptorSetLayout failed");
        return false;
    }

    // Fragment-stage push constant carries the per-call tint (vec4, 16 bytes).
    // Multiplied with the sampled texel in the rect fragment shader.
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(float) * 4;

    VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &rect_setLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pcRange;
    if (ctx.vkCreatePipelineLayout_(ctx.device, &pli, nullptr, &rect_pipeLayout_) != VK_SUCCESS)
    {
        LOG_ERROR("ScreenQuadVK(rect): vkCreatePipelineLayout failed");
        RectShutdown_(ctx);
        return false;
    }

    // Descriptor pool. kFramesInFlight * kRectSlotsPerFrame sets.
    const uint32_t totalSets = VkContext::kFramesInFlight * kRectSlotsPerFrame;

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = totalSets;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = totalSets;

    VkDescriptorPoolCreateInfo dpi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpi.maxSets       = totalSets;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes    = poolSizes;
    if (ctx.vkCreateDescriptorPool_(ctx.device, &dpi, nullptr, &rect_descPool_) != VK_SUCCESS)
    {
        LOG_ERROR("ScreenQuadVK(rect): vkCreateDescriptorPool failed");
        RectShutdown_(ctx);
        return false;
    }

    for (uint32_t fi = 0; fi < VkContext::kFramesInFlight; ++fi)
    {
        VkDescriptorSetLayout layouts[kRectSlotsPerFrame];
        for (uint32_t s = 0; s < kRectSlotsPerFrame; ++s)
            layouts[s] = rect_setLayout_;

        VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        dsai.descriptorPool     = rect_descPool_;
        dsai.descriptorSetCount = kRectSlotsPerFrame;
        dsai.pSetLayouts        = layouts;
        if (ctx.vkAllocateDescriptorSets_(ctx.device, &dsai, rect_descSets_[fi]) != VK_SUCCESS)
        {
            LOG_ERROR("ScreenQuadVK(rect): vkAllocateDescriptorSets failed (fi=%u)", fi);
            RectShutdown_(ctx);
            return false;
        }
    }

    // Per-frame VBO holds (kRectSlotsPerFrame * 6) verts. Per-slot offsets
    // prevent races between still-in-flight slots in the same frame.
    const VkDeviceSize vboBytes = (VkDeviceSize)kRectSlotsPerFrame * 6 * sizeof(QuadVertex);
    const VkDeviceSize uboBytes = sizeof(float) * 16;

    for (uint32_t fi = 0; fi < VkContext::kFramesInFlight; ++fi)
    {
        // VBO
        if (!CreateBuffer(ctx, vboBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          rect_vbo_[fi], rect_vboMem_[fi]))
        {
            LOG_ERROR("ScreenQuadVK(rect): CreateBuffer (VBO) failed (fi=%u)", fi);
            RectShutdown_(ctx);
            return false;
        }
        if (ctx.vkMapMemory_(ctx.device, rect_vboMem_[fi], 0, VK_WHOLE_SIZE, 0, &rect_vboMapped_[fi]) != VK_SUCCESS)
        {
            LOG_ERROR("ScreenQuadVK(rect): vkMapMemory (VBO) failed (fi=%u)", fi);
            RectShutdown_(ctx);
            return false;
        }

        // UBO
        if (!CreateBuffer(ctx, uboBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          rect_ubo_[fi], rect_uboMem_[fi]))
        {
            LOG_ERROR("ScreenQuadVK(rect): CreateBuffer (UBO) failed (fi=%u)", fi);
            RectShutdown_(ctx);
            return false;
        }
        if (ctx.vkMapMemory_(ctx.device, rect_uboMem_[fi], 0, VK_WHOLE_SIZE, 0, &rect_uboMapped_[fi]) != VK_SUCCESS)
        {
            LOG_ERROR("ScreenQuadVK(rect): vkMapMemory (UBO) failed (fi=%u)", fi);
            RectShutdown_(ctx);
            return false;
        }

        // Pre-write the UBO binding for every slot in this frame. The
        // sampler binding is written per-call in RecordRect (varies by call).
        VkDescriptorBufferInfo bi{};
        bi.buffer = rect_ubo_[fi];
        bi.offset = 0;
        bi.range  = uboBytes;

        VkWriteDescriptorSet writes[kRectSlotsPerFrame]{};
        for (uint32_t s = 0; s < kRectSlotsPerFrame; ++s)
        {
            writes[s].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[s].dstSet          = rect_descSets_[fi][s];
            writes[s].dstBinding      = 0;
            writes[s].descriptorCount = 1;
            writes[s].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[s].pBufferInfo     = &bi;
        }
        ctx.vkUpdateDescriptorSets_(ctx.device, kRectSlotsPerFrame, writes, 0, nullptr);

        rect_slotCursor_[fi] = 0;
    }

    // Pipeline: pre-create the swapchain-format variant so shader problems
    // surface at init. Other formats (e.g. an RGBA8_UNORM game RT) are
    // created lazily by RectGetOrCreatePipeline_ on first use.
    {
        VkPipeline p = VK_NULL_HANDLE;
        if (!RectBuildPipelineForFormat_(ctx, ctx.swapchainFormat, p))
        {
            RectShutdown_(ctx);
            return false;
        }
        rect_pipeFormat_[0] = ctx.swapchainFormat;
        rect_pipeVariant_[0] = p;
        rect_pipeVariantCount_ = 1;
    }

    LOG_INFO("ScreenQuadVK(rect): initialized (swapchainFormat=%d)",
             (int)ctx.swapchainFormat);
    return true;
}

// -----------------------------------------------------------------------------
// RectShutdown_
// -----------------------------------------------------------------------------
void ScreenQuadVK::RectShutdown_(VkContext& ctx)
{
    for (uint32_t i = 0; i < rect_pipeVariantCount_; ++i)
    {
        if (rect_pipeVariant_[i])
            ctx.vkDestroyPipeline_(ctx.device, rect_pipeVariant_[i], nullptr);
        rect_pipeVariant_[i] = VK_NULL_HANDLE;
        rect_pipeFormat_[i] = VK_FORMAT_UNDEFINED;
    }
    rect_pipeVariantCount_ = 0;

    if (rect_pipeLayout_) { ctx.vkDestroyPipelineLayout_(ctx.device, rect_pipeLayout_, nullptr); rect_pipeLayout_ = VK_NULL_HANDLE; }
    if (rect_setLayout_)  { ctx.vkDestroyDescriptorSetLayout_(ctx.device, rect_setLayout_, nullptr); rect_setLayout_ = VK_NULL_HANDLE; }
    if (rect_descPool_)   { ctx.vkDestroyDescriptorPool_(ctx.device, rect_descPool_, nullptr); rect_descPool_  = VK_NULL_HANDLE; }

    for (uint32_t fi = 0; fi < VkContext::kFramesInFlight; ++fi)
    {
        if (rect_vboMapped_[fi])
        {
            ctx.vkUnmapMemory_(ctx.device, rect_vboMem_[fi]);
            rect_vboMapped_[fi] = nullptr;
        }
        if (rect_vbo_[fi])    { ctx.vkDestroyBuffer_(ctx.device, rect_vbo_[fi], nullptr);    rect_vbo_[fi]    = VK_NULL_HANDLE; }
        if (rect_vboMem_[fi]) { ctx.vkFreeMemory_(ctx.device, rect_vboMem_[fi], nullptr);    rect_vboMem_[fi] = VK_NULL_HANDLE; }

        if (rect_uboMapped_[fi])
        {
            ctx.vkUnmapMemory_(ctx.device, rect_uboMem_[fi]);
            rect_uboMapped_[fi] = nullptr;
        }
        if (rect_ubo_[fi])    { ctx.vkDestroyBuffer_(ctx.device, rect_ubo_[fi], nullptr);    rect_ubo_[fi]    = VK_NULL_HANDLE; }
        if (rect_uboMem_[fi]) { ctx.vkFreeMemory_(ctx.device, rect_uboMem_[fi], nullptr);    rect_uboMem_[fi] = VK_NULL_HANDLE; }

        for (uint32_t s = 0; s < kRectSlotsPerFrame; ++s)
            rect_descSets_[fi][s] = VK_NULL_HANDLE;
        rect_slotCursor_[fi] = 0;
    }
}

// -----------------------------------------------------------------------------
// RectGetOrCreatePipeline_
// Returns the pipeline variant matching the active pass's color format,
// creating it lazily on first use. Variants live until RectShutdown_, so
// there is never a destroy while a recorded frame still references one.
// -----------------------------------------------------------------------------
VkPipeline ScreenQuadVK::RectGetOrCreatePipeline_(VkContext& ctx)
{
    const VkFormat fmt = VK_ActiveColorFormat(ctx);

    for (uint32_t i = 0; i < rect_pipeVariantCount_; ++i)
    {
        if (rect_pipeFormat_[i] == fmt)
            return rect_pipeVariant_[i];
    }

    if (rect_pipeVariantCount_ >= kRectMaxPipelineVariants)
    {
        LOG_ERROR("ScreenQuadVK(rect): pipeline variant cache full (format %d requested)", (int)fmt);
        return VK_NULL_HANDLE;
    }

    VkPipeline p = VK_NULL_HANDLE;
    if (!RectBuildPipelineForFormat_(ctx, fmt, p))
        return VK_NULL_HANDLE;

    LOG_INFO("ScreenQuadVK(rect): created pipeline variant for color format %d", (int)fmt);
    rect_pipeFormat_[rect_pipeVariantCount_] = fmt;
    rect_pipeVariant_[rect_pipeVariantCount_] = p;
    ++rect_pipeVariantCount_;
    return p;
}

// -----------------------------------------------------------------------------
// RectBuildPipelineForFormat_
// Builds one rect-aware pipeline variant against the given color format.
// -----------------------------------------------------------------------------
bool ScreenQuadVK::RectBuildPipelineForFormat_(VkContext& ctx, VkFormat colorFormat, VkPipeline& outPipeline)
{
    outPipeline = VK_NULL_HANDLE;

    VkShaderModule vs = RectCreateShaderModule_(ctx, rect_vertSpvPath_.c_str());
    VkShaderModule fs = RectCreateShaderModule_(ctx, rect_fragSpvPath_.c_str());
    if (!vs || !fs)
    {
        if (vs) ctx.vkDestroyShaderModule_(ctx.device, vs, nullptr);
        if (fs) ctx.vkDestroyShaderModule_(ctx.device, fs, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription vibd{};
    vibd.binding   = 0;
    vibd.stride    = sizeof(QuadVertex);
    vibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription viad[2]{};
    viad[0].location = 0;
    viad[0].binding  = 0;
    viad[0].format   = VK_FORMAT_R32G32_SFLOAT;
    viad[0].offset   = (uint32_t)offsetof(QuadVertex, x);
    viad[1].location = 1;
    viad[1].binding  = 0;
    viad[1].format   = VK_FORMAT_R32G32_SFLOAT;
    viad[1].offset   = (uint32_t)offsetof(QuadVertex, u);

    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &vibd;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions    = viad;

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ds.dynamicStateCount = 2;
    ds.pDynamicStates    = dyn;

    VkPipelineRenderingCreateInfo prci{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    prci.colorAttachmentCount    = 1;
    prci.pColorAttachmentFormats = &colorFormat;

    VkGraphicsPipelineCreateInfo gpi{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpi.pNext               = &prci;
    gpi.stageCount          = 2;
    gpi.pStages             = stages;
    gpi.pVertexInputState   = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState      = &vp;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState   = &ms;
    gpi.pColorBlendState    = &cb;
    gpi.pDynamicState       = &ds;
    gpi.layout              = rect_pipeLayout_;

    VkResult pr = ctx.vkCreateGraphicsPipelines_(
        ctx.device, ctx.pipelineCache, 1, &gpi, nullptr, &outPipeline);

    ctx.vkDestroyShaderModule_(ctx.device, vs, nullptr);
    ctx.vkDestroyShaderModule_(ctx.device, fs, nullptr);

    if (pr != VK_SUCCESS)
    {
        LOG_ERROR("ScreenQuadVK(rect): vkCreateGraphicsPipelines failed (VkResult=%d, format=%d)",
                  (int)pr, (int)colorFormat);
        outPipeline = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// OnFrameBegin
// -----------------------------------------------------------------------------
void ScreenQuadVK::OnFrameBegin(uint32_t frameIndex)
{
    if (frameIndex >= VkContext::kFramesInFlight)
        return;

    rect_slotCursor_[frameIndex] = 0;
    rect_lastFrameIndexSeen_ = frameIndex;
}

// -----------------------------------------------------------------------------
// RecordRect
// -----------------------------------------------------------------------------
void ScreenQuadVK::RecordRect(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                              VkImageView srcView, VkSampler srcSampler,
                              float leftPx, float bottomPx, float rightPx, float topPx,
                              uint32_t targetWidth, uint32_t targetHeight,
                              bool flipUV_Y,
                              rgb_t tint)
{
    if (cmd == VK_NULL_HANDLE)
        return;

    if (!rect_pipeLayout_)
    {
        LOG_ERROR("ScreenQuadVK::RecordRect: pipeline not initialized");
        return;
    }

    if (srcView == VK_NULL_HANDLE || srcSampler == VK_NULL_HANDLE)
    {
        LOG_ERROR("ScreenQuadVK::RecordRect: invalid source view/sampler");
        return;
    }

    if (targetWidth == 0 || targetHeight == 0)
    {
        LOG_ERROR("ScreenQuadVK::RecordRect: zero target dims");
        return;
    }

    if (frameIndex >= VkContext::kFramesInFlight)
    {
        LOG_ERROR("ScreenQuadVK::RecordRect: frameIndex out of range (%u)", frameIndex);
        return;
    }

    // Pipeline variant matching the active pass's color format. A new format
    // simply gets its own variant, which also handles drawing inside RTs
    // whose format is not the swapchain's.
    VkPipeline rectPipeline = RectGetOrCreatePipeline_(ctx);
    if (rectPipeline == VK_NULL_HANDLE)
        return;

    const uint32_t fi = frameIndex;

    // Lazy slot-cursor reset on frame boundary (fallback when OnFrameBegin
    // was not called). ctx.frameIndex alternates 0..kFramesInFlight-1 each
    // frame, so a change in frameIndex passed to us across calls signals a
    // new frame. Reset the incoming slot's cursor to 0 so it gets fresh
    // slots, not stale ones from kFramesInFlight frames ago.
    if (rect_lastFrameIndexSeen_ != fi)
    {
        rect_slotCursor_[fi] = 0;
        rect_lastFrameIndexSeen_ = fi;
    }

    // Allocate a slot in the per-frame VBO/descriptor ring.
    const uint32_t slot = rect_slotCursor_[fi];
    if (slot >= kRectSlotsPerFrame)
    {
        LOG_ERROR("ScreenQuadVK::RecordRect: exceeded kRectSlotsPerFrame=%u in one frame; dropping draw",
                  kRectSlotsPerFrame);
        return;
    }
    rect_slotCursor_[fi] = slot + 1;

    // Update the per-frame UBO ortho to match the caller's coord frame.
    // Note: written every call. Within a single frame, multiple calls with
    // different target dims would clobber each other -- but in practice each
    // frame uses one consistent target (RT or swapchain) for all rect draws,
    // and all calls within that frame supply the same dims, so this is safe.
    if (rect_uboMapped_[fi])
    {
        float mvp[16];
        RectMakeOrtho_(0.0f, (float)targetWidth, 0.0f, (float)targetHeight, mvp);
        memcpy(rect_uboMapped_[fi], mvp, sizeof(mvp));
    }

    // Write the sampler binding for this slot's descriptor set.
    VkDescriptorImageInfo dii{};
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView   = srcView;
    dii.sampler     = srcSampler;

    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet          = rect_descSets_[fi][slot];
    w.dstBinding      = 1;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo      = &dii;
    ctx.vkUpdateDescriptorSets_(ctx.device, 1, &w, 0, nullptr);

    // Build the 6-vertex quad in screen-pixel space at this slot's offset.
    // BL, TL, TR, then TR, BR, BL (two triangles, CCW).
    //
    // UVs (donor contract, preserved): a texture uploaded without a vertical
    // flip has v=0 at the IMAGE TOP. To render right-side up, the BL screen
    // vertex samples the IMAGE BOTTOM (v=1) and the TL vertex samples the
    // IMAGE TOP (v=0). flipUV_Y=false therefore maps to v0=1, v1=0 here;
    // callers that need an explicit vertical flip pass flipUV_Y=true.
    // (The donor fixed an earlier version that had v0/v1 swapped, which
    // forced every caller to pass flipUV_Y=true for right-side-up output.)
    const float u0 = 0.0f;
    const float u1 = 1.0f;
    const float v0 = flipUV_Y ? 0.0f : 1.0f;
    const float v1 = flipUV_Y ? 1.0f : 0.0f;

    QuadVertex tri6[6] = {
        // BL, TL, TR
        { leftPx,  bottomPx, u0, v0 },
        { leftPx,  topPx,    u0, v1 },
        { rightPx, topPx,    u1, v1 },
        // TR, BR, BL
        { rightPx, topPx,    u1, v1 },
        { rightPx, bottomPx, u1, v0 },
        { leftPx,  bottomPx, u0, v0 },
    };

    const VkDeviceSize slotByteOffset =
        (VkDeviceSize)slot * 6 * sizeof(QuadVertex);

    if (rect_vboMapped_[fi])
    {
        uint8_t* base = static_cast<uint8_t*>(rect_vboMapped_[fi]);
        memcpy(base + slotByteOffset, tri6, sizeof(tri6));
    }

    // Y-flipped viewport sized for the active framebuffer (engine convention).
    VkViewport vpr{};
    vpr.x        = 0.0f;
    vpr.y        = (float)targetHeight;
    vpr.width    = (float)targetWidth;
    vpr.height   = -(float)targetHeight;
    vpr.minDepth = 0.0f;
    vpr.maxDepth = 1.0f;
    ctx.vkCmdSetViewport_(cmd, 0, 1, &vpr);

    VkRect2D scr{};
    scr.offset = { 0, 0 };
    scr.extent = { targetWidth, targetHeight };
    ctx.vkCmdSetScissor_(cmd, 0, 1, &scr);

    ctx.vkCmdBindPipeline_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rectPipeline);

    VkDescriptorSet ds = rect_descSets_[fi][slot];
    ctx.vkCmdBindDescriptorSets_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        rect_pipeLayout_, 0, 1, &ds, 0, nullptr);

    VkDeviceSize offsets[1] = { slotByteOffset };
    ctx.vkCmdBindVertexBuffers_(cmd, 0, 1, &rect_vbo_[fi], offsets);

    // Push the per-call tint as a fragment-stage push constant. MAKE_RGBA
    // packs R in the low byte (r | g<<8 | b<<16 | a<<24); convert each
    // byte to 0..1 and ship as vec4. RGB_WHITE (0xFFFFFFFF) maps to
    // (1,1,1,1) which multiplies to identity in the shader.
    const float pcData[4] = {
        (float)((tint >> 0)  & 0xFF) / 255.0f,
        (float)((tint >> 8)  & 0xFF) / 255.0f,
        (float)((tint >> 16) & 0xFF) / 255.0f,
        (float)((tint >> 24) & 0xFF) / 255.0f,
    };
    ctx.vkCmdPushConstants_(cmd, rect_pipeLayout_,
        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pcData), pcData);

    ctx.vkCmdDraw_(cmd, 6, 1, 0, 0);
}
