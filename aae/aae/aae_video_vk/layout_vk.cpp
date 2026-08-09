//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// layout_vk.cpp - Vulkan MAME .lay layout compositor. See layout_vk.h for
// what the GL side is and which subset is ported.
//
// Everything here is a line-by-line mirror of mame_layout.cpp's
// Layout_LoadTextures / BakeProceduralTexture / Layout_Render. Where a GL
// idiom has no VK equivalent the comment says so and states what replaces it.
//
// ASCII-only comments.
// -----------------------------------------------------------------------------

#include "layout_vk.h"

#include "mame_layout.h"      // g_layoutData / g_activeView / Layout_* API
#include "aae_mame_driver.h"  // AAEDriver, Machine, VIDEO_TYPE_RASTER_BW
#include "config.h"
#include "menu.h"             // g_*Available menu flags
#include "sys_window.h"       // GetWindowSetup (aspect override)
#include "sys_log.h"

#include "stb_image.h"        // header-only; implementation lives in sys_texture.cpp
#include "aae_fileio.h"       // load_zip_file / get_last_zip_file_size

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// =============================================================================
//                                 LayoutQuadVK
// =============================================================================

namespace
{
    bool LQReadFileBytes_(const char* path, std::vector<uint8_t>& out)
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

    VkShaderModule LQCreateShaderModule_(VkContext& ctx, const char* path)
    {
        std::vector<uint8_t> bytes;
        if (!LQReadFileBytes_(path, bytes))
        {
            LOG_ERROR("LayoutQuadVK: failed to read %s", path ? path : "(null)");
            return VK_NULL_HANDLE;
        }
        if ((bytes.size() & 3u) != 0u)
        {
            LOG_ERROR("LayoutQuadVK: SPV size not 4-byte aligned: %s", path);
            return VK_NULL_HANDLE;
        }
        VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        ci.codeSize = bytes.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(bytes.data());

        VkShaderModule mod = VK_NULL_HANDLE;
        if (ctx.vkCreateShaderModule_(ctx.device, &ci, nullptr, &mod) != VK_SUCCESS)
        {
            LOG_ERROR("LayoutQuadVK: vkCreateShaderModule failed for %s", path);
            return VK_NULL_HANDLE;
        }
        return mod;
    }

    void LQMakeOrtho_(float l, float r, float b, float t, float* out16)
    {
        const float rl = r - l;
        const float tb = t - b;
        out16[0] = 2.0f / rl;  out16[1] = 0.0f;      out16[2] = 0.0f;  out16[3] = 0.0f;
        out16[4] = 0.0f;       out16[5] = 2.0f / tb; out16[6] = 0.0f;  out16[7] = 0.0f;
        out16[8] = 0.0f;       out16[9] = 0.0f;      out16[10] = -1.0f; out16[11] = 0.0f;
        out16[12] = -(r + l) / rl;
        out16[13] = -(t + b) / tb;
        out16[14] = 0.0f;
        out16[15] = 1.0f;
    }
}

uint32_t LayoutQuadVK::FindMemoryType_(VkContext& ctx, uint32_t typeBits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mp{};
    ctx.vkGetPhysicalDeviceMemoryProperties_(ctx.phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) == 0) continue;
        if ((mp.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    return 0xFFFFFFFFu;
}

bool LayoutQuadVK::CreateBuffer_(VkContext& ctx, VkDeviceSize size,
                                 VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps,
                                 VkBuffer& outBuf, VkDeviceMemory& outMem)
{
    outBuf = VK_NULL_HANDLE;
    outMem = VK_NULL_HANDLE;

    VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (ctx.vkCreateBuffer_(ctx.device, &bci, nullptr, &outBuf) != VK_SUCCESS)
        return false;

    VkMemoryRequirements mr{};
    ctx.vkGetBufferMemoryRequirements_(ctx.device, outBuf, &mr);

    const uint32_t typeIndex = FindMemoryType_(ctx, mr.memoryTypeBits, memProps);
    if (typeIndex == 0xFFFFFFFFu)
    {
        ctx.vkDestroyBuffer_(ctx.device, outBuf, nullptr);
        outBuf = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = typeIndex;
    if (ctx.vkAllocateMemory_(ctx.device, &mai, nullptr, &outMem) != VK_SUCCESS)
    {
        ctx.vkDestroyBuffer_(ctx.device, outBuf, nullptr);
        outBuf = VK_NULL_HANDLE;
        return false;
    }

    if (ctx.vkBindBufferMemory_(ctx.device, outBuf, outMem, 0) != VK_SUCCESS)
    {
        ctx.vkDestroyBuffer_(ctx.device, outBuf, nullptr);
        ctx.vkFreeMemory_(ctx.device, outMem, nullptr);
        outBuf = VK_NULL_HANDLE;
        outMem = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool LayoutQuadVK::Init(VkContext& ctx, const LayoutQuadVKCreateInfo* ci)
{
    if (!ctx.device)
        return false;

    LayoutQuadVKCreateInfo def;
    const LayoutQuadVKCreateInfo& c = ci ? *ci : def;
    vertSpvPath_ = c.vertSpvPath;
    fragSpvPath_ = c.fragSpvPath;

    // set 0: binding 0 = ortho UBO (vertex), 1 = screen tex, 2 = overlay gel.
    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dsl.bindingCount = 3;
    dsl.pBindings = bindings;
    if (ctx.vkCreateDescriptorSetLayout_(ctx.device, &dsl, nullptr, &setLayout_) != VK_SUCCESS)
    {
        LOG_ERROR("LayoutQuadVK: vkCreateDescriptorSetLayout failed");
        return false;
    }

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(LayoutPush);

    VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &setLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcRange;
    if (ctx.vkCreatePipelineLayout_(ctx.device, &pli, nullptr, &pipeLayout_) != VK_SUCCESS)
    {
        LOG_ERROR("LayoutQuadVK: vkCreatePipelineLayout failed");
        Shutdown(ctx);
        return false;
    }

    const uint32_t totalSets = VkContext::kFramesInFlight * kSlotsPerFrame;

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = totalSets;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = totalSets * 2;   // two samplers per set

    VkDescriptorPoolCreateInfo dpi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpi.maxSets = totalSets;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = poolSizes;
    if (ctx.vkCreateDescriptorPool_(ctx.device, &dpi, nullptr, &descPool_) != VK_SUCCESS)
    {
        LOG_ERROR("LayoutQuadVK: vkCreateDescriptorPool failed");
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
        if (ctx.vkAllocateDescriptorSets_(ctx.device, &dsai, descSets_[fi]) != VK_SUCCESS)
        {
            LOG_ERROR("LayoutQuadVK: vkAllocateDescriptorSets failed (fi=%u)", fi);
            Shutdown(ctx);
            return false;
        }
    }

    const VkDeviceSize vboBytes = (VkDeviceSize)kSlotsPerFrame * 6 * sizeof(QuadVertex);

    // One ortho per SLOT (not per frame) for the same reason ScreenQuadVK does
    // it: a later call with different target dims must not retroactively
    // change an already-recorded draw.
    const VkDeviceSize uboMatBytes = sizeof(float) * 16;
    VkDeviceSize uboAlign = 256;
    if (ctx.vkGetPhysicalDeviceProperties_ && ctx.phys)
    {
        VkPhysicalDeviceProperties props{};
        ctx.vkGetPhysicalDeviceProperties_(ctx.phys, &props);
        if (props.limits.minUniformBufferOffsetAlignment > 0)
            uboAlign = props.limits.minUniformBufferOffsetAlignment;
    }
    uboStride_ = ((uboMatBytes + uboAlign - 1) / uboAlign) * uboAlign;
    const VkDeviceSize uboBytes = uboStride_ * kSlotsPerFrame;

    for (uint32_t fi = 0; fi < VkContext::kFramesInFlight; ++fi)
    {
        if (!CreateBuffer_(ctx, vboBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           vbo_[fi], vboMem_[fi]) ||
            ctx.vkMapMemory_(ctx.device, vboMem_[fi], 0, VK_WHOLE_SIZE, 0, &vboMapped_[fi]) != VK_SUCCESS)
        {
            LOG_ERROR("LayoutQuadVK: VBO create/map failed (fi=%u)", fi);
            Shutdown(ctx);
            return false;
        }

        if (!CreateBuffer_(ctx, uboBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           ubo_[fi], uboMem_[fi]) ||
            ctx.vkMapMemory_(ctx.device, uboMem_[fi], 0, VK_WHOLE_SIZE, 0, &uboMapped_[fi]) != VK_SUCCESS)
        {
            LOG_ERROR("LayoutQuadVK: UBO create/map failed (fi=%u)", fi);
            Shutdown(ctx);
            return false;
        }

        VkDescriptorBufferInfo bis[kSlotsPerFrame]{};
        VkWriteDescriptorSet writes[kSlotsPerFrame]{};
        for (uint32_t s = 0; s < kSlotsPerFrame; ++s)
        {
            bis[s].buffer = ubo_[fi];
            bis[s].offset = uboStride_ * s;
            bis[s].range = uboMatBytes;

            writes[s].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[s].dstSet = descSets_[fi][s];
            writes[s].dstBinding = 0;
            writes[s].descriptorCount = 1;
            writes[s].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[s].pBufferInfo = &bis[s];
        }
        ctx.vkUpdateDescriptorSets_(ctx.device, kSlotsPerFrame, writes, 0, nullptr);
        slotCursor_[fi] = 0;
    }

    // Pre-create the swapchain-format alpha variant so shader problems surface
    // at init rather than mid-game (house pattern).
    {
        VkPipeline p = VK_NULL_HANDLE;
        if (!BuildPipeline_(ctx, ctx.swapchainFormat, LQBlendVK::Alpha, p))
        {
            Shutdown(ctx);
            return false;
        }
        pipeFormat_[0] = ctx.swapchainFormat;
        pipeBlend_[0] = LQBlendVK::Alpha;
        pipeVariant_[0] = p;
        pipeVariantCount_ = 1;
    }

    initialized_ = true;
    LOG_INFO("LayoutQuadVK: initialized (swapchainFormat=%d)", (int)ctx.swapchainFormat);
    return true;
}

void LayoutQuadVK::Shutdown(VkContext& ctx)
{
    if (!ctx.device)
        return;

    for (uint32_t i = 0; i < pipeVariantCount_; ++i)
    {
        if (pipeVariant_[i]) ctx.vkDestroyPipeline_(ctx.device, pipeVariant_[i], nullptr);
        pipeVariant_[i] = VK_NULL_HANDLE;
        pipeFormat_[i] = VK_FORMAT_UNDEFINED;
        pipeBlend_[i] = LQBlendVK::Alpha;
    }
    pipeVariantCount_ = 0;

    if (pipeLayout_) { ctx.vkDestroyPipelineLayout_(ctx.device, pipeLayout_, nullptr); pipeLayout_ = VK_NULL_HANDLE; }
    if (setLayout_) { ctx.vkDestroyDescriptorSetLayout_(ctx.device, setLayout_, nullptr); setLayout_ = VK_NULL_HANDLE; }
    if (descPool_) { ctx.vkDestroyDescriptorPool_(ctx.device, descPool_, nullptr); descPool_ = VK_NULL_HANDLE; }

    for (uint32_t fi = 0; fi < VkContext::kFramesInFlight; ++fi)
    {
        if (vboMapped_[fi]) { ctx.vkUnmapMemory_(ctx.device, vboMem_[fi]); vboMapped_[fi] = nullptr; }
        if (vbo_[fi]) { ctx.vkDestroyBuffer_(ctx.device, vbo_[fi], nullptr); vbo_[fi] = VK_NULL_HANDLE; }
        if (vboMem_[fi]) { ctx.vkFreeMemory_(ctx.device, vboMem_[fi], nullptr); vboMem_[fi] = VK_NULL_HANDLE; }

        if (uboMapped_[fi]) { ctx.vkUnmapMemory_(ctx.device, uboMem_[fi]); uboMapped_[fi] = nullptr; }
        if (ubo_[fi]) { ctx.vkDestroyBuffer_(ctx.device, ubo_[fi], nullptr); ubo_[fi] = VK_NULL_HANDLE; }
        if (uboMem_[fi]) { ctx.vkFreeMemory_(ctx.device, uboMem_[fi], nullptr); uboMem_[fi] = VK_NULL_HANDLE; }

        for (uint32_t s = 0; s < kSlotsPerFrame; ++s)
            descSets_[fi][s] = VK_NULL_HANDLE;
        slotCursor_[fi] = 0;
    }

    initialized_ = false;
}

void LayoutQuadVK::OnFrameBegin(uint32_t frameIndex)
{
    if (frameIndex >= VkContext::kFramesInFlight)
        return;
    slotCursor_[frameIndex] = 0;
    lastFrameIndexSeen_ = frameIndex;
}

VkPipeline LayoutQuadVK::GetOrCreatePipeline_(VkContext& ctx, LQBlendVK blend)
{
    const VkFormat fmt = VK_ActiveColorFormat(ctx);
    for (uint32_t i = 0; i < pipeVariantCount_; ++i)
        if (pipeFormat_[i] == fmt && pipeBlend_[i] == blend)
            return pipeVariant_[i];

    if (pipeVariantCount_ >= kMaxPipelineVariants)
    {
        LOG_ERROR("LayoutQuadVK: pipeline variant cache full (format %d blend %d)",
                  (int)fmt, (int)blend);
        return VK_NULL_HANDLE;
    }

    VkPipeline p = VK_NULL_HANDLE;
    if (!BuildPipeline_(ctx, fmt, blend, p))
        return VK_NULL_HANDLE;

    LOG_INFO("LayoutQuadVK: created pipeline variant for format %d blend %d", (int)fmt, (int)blend);
    pipeFormat_[pipeVariantCount_] = fmt;
    pipeBlend_[pipeVariantCount_] = blend;
    pipeVariant_[pipeVariantCount_] = p;
    ++pipeVariantCount_;
    return p;
}

bool LayoutQuadVK::BuildPipeline_(VkContext& ctx, VkFormat colorFormat, LQBlendVK blend,
                                  VkPipeline& outPipeline)
{
    outPipeline = VK_NULL_HANDLE;

    VkShaderModule vs = LQCreateShaderModule_(ctx, vertSpvPath_.c_str());
    VkShaderModule fs = LQCreateShaderModule_(ctx, fragSpvPath_.c_str());
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

    VkVertexInputBindingDescription vibd{};
    vibd.binding = 0;
    vibd.stride = sizeof(QuadVertex);
    vibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription viad[2]{};
    viad[0].location = 0;
    viad[0].binding = 0;
    viad[0].format = VK_FORMAT_R32G32_SFLOAT;
    viad[0].offset = (uint32_t)offsetof(QuadVertex, x);
    viad[1].location = 1;
    viad[1].binding = 0;
    viad[1].format = VK_FORMAT_R32G32_SFLOAT;
    viad[1].offset = (uint32_t)offsetof(QuadVertex, u);

    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vibd;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = viad;

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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
    cba.blendEnable = VK_TRUE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (blend == LQBlendVK::Additive)
    {
        // GL Layout_Render screen layer: glBlendFunc(GL_ONE, GL_ONE).
        // GL applies the one pair to alpha as well.
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    else
    {
        // GL backdrop / bezel layers: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
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
    gpi.layout = pipeLayout_;

    const VkResult pr = ctx.vkCreateGraphicsPipelines_(ctx.device, ctx.pipelineCache, 1, &gpi,
                                                       nullptr, &outPipeline);
    ctx.vkDestroyShaderModule_(ctx.device, vs, nullptr);
    ctx.vkDestroyShaderModule_(ctx.device, fs, nullptr);

    if (pr != VK_SUCCESS)
    {
        LOG_ERROR("LayoutQuadVK: vkCreateGraphicsPipelines failed (VkResult=%d, format=%d)",
                  (int)pr, (int)colorFormat);
        outPipeline = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void LayoutQuadVK::RecordQuad(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
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
                              LQBlendVK blend)
{
    if (cmd == VK_NULL_HANDLE || !initialized_ || !pipeLayout_)
        return;
    if (srcView == VK_NULL_HANDLE || srcSampler == VK_NULL_HANDLE)
        return;
    if (targetWidth == 0 || targetHeight == 0)
        return;
    if (frameIndex >= VkContext::kFramesInFlight)
        return;

    // No overlay: bind the source to slot 2 as well so the descriptor is
    // always complete. The shader's mode branch never samples it.
    if (ovView == VK_NULL_HANDLE || ovSampler == VK_NULL_HANDLE)
    {
        ovView = srcView;
        ovSampler = srcSampler;
    }

    VkPipeline pipe = GetOrCreatePipeline_(ctx, blend);
    if (pipe == VK_NULL_HANDLE)
        return;

    const uint32_t fi = frameIndex;
    if (lastFrameIndexSeen_ != fi)
    {
        slotCursor_[fi] = 0;
        lastFrameIndexSeen_ = fi;
    }

    const uint32_t slot = slotCursor_[fi];
    if (slot >= kSlotsPerFrame)
    {
        LOG_ERROR("LayoutQuadVK: exceeded kSlotsPerFrame=%u in one frame; dropping draw",
                  kSlotsPerFrame);
        return;
    }
    slotCursor_[fi] = slot + 1;

    if (uboMapped_[fi])
    {
        float mvp[16];
        LQMakeOrtho_(0.0f, (float)targetWidth, 0.0f, (float)targetHeight, mvp);
        uint8_t* base = static_cast<uint8_t*>(uboMapped_[fi]);
        memcpy(base + (size_t)(uboStride_ * slot), mvp, sizeof(mvp));
    }

    VkDescriptorImageInfo dii[2]{};
    dii[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii[0].imageView = srcView;
    dii[0].sampler = srcSampler;
    dii[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii[1].imageView = ovView;
    dii[1].sampler = ovSampler;

    VkWriteDescriptorSet w[2]{};
    for (int i = 0; i < 2; ++i)
    {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = descSets_[fi][slot];
        w[i].dstBinding = (uint32_t)(1 + i);
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i].pImageInfo = &dii[i];
    }
    ctx.vkUpdateDescriptorSets_(ctx.device, 2, w, 0, nullptr);

    // UV corners. Identical derivation to ScreenQuadVK::RecordRect: work in
    // "image parameter" space d = (across, DOWN) so d == (u,v) at rotation 0,
    // then permute per the GL Rect2 indices table. The GL layout renderer
    // rotates the QUAD GEOMETRY and keeps standard UVs; a rigid 90/180/270
    // rotation of an axis-aligned rect is still axis-aligned, so the same
    // picture falls out of an axis-aligned rect with permuted corner UVs -
    // which is what this records. (See the dkong/invaders paper traces in
    // the commit message.)
    const float dBL[2] = { 0.0f, 1.0f };
    const float dTL[2] = { 0.0f, 0.0f };
    const float dTR[2] = { 1.0f, 0.0f };
    const float dBR[2] = { 1.0f, 1.0f };

    auto rotUV = [uvRotation](const float d[2], float& u, float& v)
    {
        switch (uvRotation)
        {
        case 1:  u = d[1];        v = 1.0f - d[0]; break;   // CW 90
        case 2:  u = 1.0f - d[1]; v = d[0];        break;   // CCW 90
        case 3:  u = 1.0f - d[0]; v = 1.0f - d[1]; break;   // 180
        default: u = d[0];        v = d[1];        break;
        }
    };

    float uBL, vBL, uTL, vTL, uTR, vTR, uBR, vBR;
    rotUV(dBL, uBL, vBL);
    rotUV(dTL, uTL, vTL);
    rotUV(dTR, uTR, vTR);
    rotUV(dBR, uBR, vBR);

    QuadVertex tri6[6] = {
        { leftPx,  bottomPx, uBL, vBL },
        { leftPx,  topPx,    uTL, vTL },
        { rightPx, topPx,    uTR, vTR },
        { rightPx, topPx,    uTR, vTR },
        { rightPx, bottomPx, uBR, vBR },
        { leftPx,  bottomPx, uBL, vBL },
    };

    const VkDeviceSize slotByteOffset = (VkDeviceSize)slot * 6 * sizeof(QuadVertex);
    if (vboMapped_[fi])
    {
        uint8_t* base = static_cast<uint8_t*>(vboMapped_[fi]);
        memcpy(base + slotByteOffset, tri6, sizeof(tri6));
    }

    VkViewport vpr{};
    vpr.x = 0.0f;
    vpr.y = (float)targetHeight;
    vpr.width = (float)targetWidth;
    vpr.height = -(float)targetHeight;
    vpr.minDepth = 0.0f;
    vpr.maxDepth = 1.0f;
    ctx.vkCmdSetViewport_(cmd, 0, 1, &vpr);

    // Caller-supplied scissor. GL clips the layout with glViewport(vpX, vpY,
    // vpW, vpH) when a display-aspect override is active; the y-flipped
    // viewport above already carries the coordinate mapping, so the clip is
    // expressed as a scissor instead. Clamped to the target.
    int64_t sx = (int64_t)scissorX;
    int64_t sy = (int64_t)scissorY;
    int64_t sw = (int64_t)scissorW;
    int64_t sh = (int64_t)scissorH;
    if (sx < 0) { sw += sx; sx = 0; }
    if (sy < 0) { sh += sy; sy = 0; }
    if (sx + sw > (int64_t)targetWidth)  sw = (int64_t)targetWidth - sx;
    if (sy + sh > (int64_t)targetHeight) sh = (int64_t)targetHeight - sy;
    if (sw <= 0 || sh <= 0)
        return;

    VkRect2D scr{};
    scr.offset = { (int32_t)sx, (int32_t)sy };
    scr.extent = { (uint32_t)sw, (uint32_t)sh };
    ctx.vkCmdSetScissor_(cmd, 0, 1, &scr);

    ctx.vkCmdBindPipeline_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);

    VkDescriptorSet dsHandle = descSets_[fi][slot];
    ctx.vkCmdBindDescriptorSets_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 pipeLayout_, 0, 1, &dsHandle, 0, nullptr);

    VkDeviceSize offsets[1] = { slotByteOffset };
    ctx.vkCmdBindVertexBuffers_(cmd, 0, 1, &vbo_[fi], offsets);

    LayoutPush push{};
    push.xform[0] = uvXform ? uvXform[0] : 1.0f;
    push.xform[1] = uvXform ? uvXform[1] : 1.0f;
    push.xform[2] = uvXform ? uvXform[2] : 0.0f;
    push.xform[3] = uvXform ? uvXform[3] : 0.0f;
    push.params[0] = alpha;
    push.params[1] = overlayMode;
    ctx.vkCmdPushConstants_(cmd, pipeLayout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(push), &push);

    ctx.vkCmdDraw_(cmd, 6, 1, 0, 0);
}

// =============================================================================
//                        Per-game layout texture cache
// =============================================================================

namespace
{
    // Element name -> uploaded texture. Element names are unique within a
    // LayoutData (Layout_Parse stores them in a std::map keyed by name), and
    // g_layoutData is cleared between games, so the name is a safe key.
    std::map<std::string, VkTexture> s_layoutTex;

    // A layout is loaded and composited for this game - either a parsed .lay
    // or the synthetic screen-only view. Both walk the same layer loop.
    bool s_layoutLoaded = false;

    VkTexture* LayoutTexFor(const LayoutElement* elem)
    {
        if (!elem) return nullptr;
        auto it = s_layoutTex.find(elem->name);
        return (it == s_layoutTex.end()) ? nullptr : &it->second;
    }

    // Line-for-line mirror of mame_layout.cpp's BakeProceduralTexture, minus
    // the GL upload: same bounding box, same 1:1 rasterization, same 2048 cap,
    // same painter's-algorithm overwrite, same float->byte color conversion.
    bool BakeProceduralRGBA(const LayoutElement& elem, std::vector<uint8_t>& pixels,
                            int& outW, int& outH)
    {
        if (elem.rects.empty())
            return false;

        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (const auto& r : elem.rects)
        {
            minX = (std::min)(minX, r.x);
            minY = (std::min)(minY, r.y);
            maxX = (std::max)(maxX, r.x + r.w);
            maxY = (std::max)(maxY, r.y + r.h);
        }

        int texW = (int)std::ceil(maxX - minX);
        int texH = (int)std::ceil(maxY - minY);
        if (texW < 1) texW = 1;
        if (texH < 1) texH = 1;
        if (texW > 2048) texW = 2048;
        if (texH > 2048) texH = 2048;

        pixels.assign((size_t)texW * texH * 4, 0);

        for (const auto& r : elem.rects)
        {
            int rx0 = (int)(r.x - minX);
            int ry0 = (int)(r.y - minY);
            int rx1 = (int)(r.x + r.w - minX);
            int ry1 = (int)(r.y + r.h - minY);
            rx0 = (std::max)(0, (std::min)(rx0, texW));
            ry0 = (std::max)(0, (std::min)(ry0, texH));
            rx1 = (std::max)(0, (std::min)(rx1, texW));
            ry1 = (std::max)(0, (std::min)(ry1, texH));

            const uint8_t cr = (uint8_t)(r.r * 255.0f);
            const uint8_t cg = (uint8_t)(r.g * 255.0f);
            const uint8_t cb = (uint8_t)(r.b * 255.0f);
            const uint8_t ca = (uint8_t)(r.a * 255.0f);

            for (int y = ry0; y < ry1; y++)
            {
                for (int x = rx0; x < rx1; x++)
                {
                    const size_t idx = ((size_t)y * texW + x) * 4;
                    pixels[idx + 0] = cr;
                    pixels[idx + 1] = cg;
                    pixels[idx + 2] = cb;
                    pixels[idx + 3] = ca;
                }
            }
        }

        outW = texW;
        outH = texH;
        return true;
    }

    // Mirror of Layout_LoadTextures: ZIP first, loose file second, then the
    // procedural bake. No stbi vertical flip (GL calls
    // stbi_set_flip_vertically_on_load(0) here too), so image row 0 is the
    // texture's v=0 - which is what the quad UVs assume.
    void LoadLayoutTextures(VkContext& ctx, const std::string& zipFile,
                            const std::string& artworkDir)
    {
        for (auto& pair : g_layoutData.elements)
        {
            LayoutElement& elem = pair.second;
            if (s_layoutTex.find(elem.name) != s_layoutTex.end())
                continue;

            if (!elem.imageFile.empty())
            {
                int w = 0, h = 0, channels = 0;
                unsigned char* imgData = nullptr;

                if (!zipFile.empty())
                {
                    unsigned char* zipData = load_zip_file(zipFile.c_str(), elem.imageFile.c_str());
                    if (zipData)
                    {
                        const size_t zipSize = get_last_zip_file_size();
                        stbi_set_flip_vertically_on_load(0);
                        imgData = stbi_load_from_memory(zipData, (int)zipSize, &w, &h, &channels, 4);
                        free(zipData);
                    }
                }

                if (!imgData)
                {
                    const std::string fullPath = artworkDir + "/" + elem.imageFile;
                    stbi_set_flip_vertically_on_load(0);
                    imgData = stbi_load(fullPath.c_str(), &w, &h, &channels, 4);
                }

                if (!imgData)
                {
                    LOG_WARN("LayoutVK: failed to load '%s'", elem.imageFile.c_str());
                    continue;
                }

                VkTexture tex{};
                // Mips: the letterboxed composite minifies bezel/backdrop art
                // heavily (a 4000-unit bezel into a ~1000px window), and GL's
                // layout textures sample with GL_LINEAR against a full-res
                // image. A mip chain here is the trilinear equivalent and is
                // what VkArt uses for the vector artwork for the same reason.
                const bool ok = VK_CreateTextureRGBA8_UNORM_FromPixels(
                    ctx, imgData, (uint32_t)w, (uint32_t)h, tex,
                    /*generateMips=*/true, /*nearestFilter=*/false);
                stbi_image_free(imgData);

                if (!ok)
                {
                    LOG_ERROR("LayoutVK: GPU upload failed for '%s'", elem.imageFile.c_str());
                    continue;
                }

                elem.texWidth = w;
                elem.texHeight = h;
                s_layoutTex.emplace(elem.name, tex);
                LOG_INFO("LayoutVK: loaded '%s' (%dx%d)", elem.imageFile.c_str(), w, h);
            }
            else if (!elem.rects.empty())
            {
                std::vector<uint8_t> pixels;
                int w = 0, h = 0;
                if (!BakeProceduralRGBA(elem, pixels, w, h))
                    continue;

                VkTexture tex{};
                if (!VK_CreateTextureRGBA8_UNORM_FromPixels(
                        ctx, pixels.data(), (uint32_t)w, (uint32_t)h, tex,
                        /*generateMips=*/true, /*nearestFilter=*/false))
                {
                    LOG_ERROR("LayoutVK: GPU upload failed for procedural '%s'", elem.name.c_str());
                    continue;
                }

                elem.texWidth = w;
                elem.texHeight = h;
                s_layoutTex.emplace(elem.name, tex);
                LOG_INFO("LayoutVK: baked procedural '%s': %dx%d", elem.name.c_str(), w, h);
            }
        }
    }
}

void LayoutVK_FreeTextures(VkContext& ctx)
{
    for (auto& kv : s_layoutTex)
        VK_DestroyTexture(ctx, kv.second);
    s_layoutTex.clear();
    s_layoutLoaded = false;
}

bool LayoutVK_Active(void)
{
    return s_layoutLoaded && g_layoutEnabled && g_activeView != nullptr;
}

void LayoutVK_LoadForGame(VkContext& ctx, const AAEDriver* drv)
{
    // Previous game's textures. The caller (vkchain_load_layout) has already
    // drained the device, so nothing in flight can still be sampling them.
    LayoutVK_FreeTextures(ctx);

    if (!drv)
        return;

    // Synthetic screen-only fallback, shared with the GL loader; every exit
    // below that would otherwise leave the layout off takes it. Textures are
    // freed first because Layout_CreateSyntheticForGame clears the element
    // map that owns their names.
    auto synthetic = [&](void) {
        LayoutVK_FreeTextures(ctx);
        Layout_CreateSyntheticForGame(drv);
        s_layoutLoaded = g_layoutEnabled && g_activeView != nullptr;
        };

    std::string zipFile, artDir;
    if (!Layout_FindArtworkSource(drv, zipFile, artDir))
    {
        LOG_INFO("LayoutVK: no .lay file found for game '%s'", drv->name ? drv->name : "?");
        synthetic();
        return;
    }

    if (!Layout_Parse(drv->layoutFile, zipFile, artDir, g_layoutData))
    {
        LOG_WARN("LayoutVK: layout parse failed for game '%s'", drv->name ? drv->name : "?");
        synthetic();
        return;
    }

    LoadLayoutTextures(ctx, zipFile, artDir);

    const std::string viewName = drv->defaultView ? drv->defaultView : "";
    g_activeView = Layout_FindView(g_layoutData, viewName);
    if (!g_activeView)
    {
        LOG_WARN("LayoutVK: layout loaded but view '%s' not found", viewName.c_str());
        synthetic();
        return;
    }

    g_layoutAspect = g_activeView->boundsW / g_activeView->boundsH;
    g_layoutEnabled = true;
    s_layoutLoaded = true;

    // Same menu-availability bookkeeping as Layout_LoadForGame. The GL loader
    // tests element->textureID; the VK cache is the equivalent presence test.
    for (const auto& d : g_activeView->drawables)
    {
        if (!d.element || !LayoutTexFor(d.element))
            continue;
        if (d.layer == LayerType::Bezel) { g_bezelAvailable = 1; g_artcropAvailable = 1; }
        if (d.layer == LayerType::Backdrop) g_artworkAvailable = 1;
        if (d.layer == LayerType::Overlay)  g_overlayAvailable = 1;
    }

    LOG_INFO("LayoutVK: layout loaded: view='%s' aspect=%.3f elements=%d",
             g_activeView->name.c_str(), g_layoutAspect, (int)s_layoutTex.size());
}

// =============================================================================
//                              Frame geometry
// =============================================================================
//
// Straight transcription of the top of Layout_Render, with GL's NDC math
// replaced by pixel math (RecordQuad takes pixels; the ortho does the NDC
// conversion). Every branch, every comparison and every constant matches.
// =============================================================================

namespace
{
    // Maps a layout-space rect to its axis-aligned destination rect in y-DOWN
    // pixels, relative to the (possibly override-constrained) viewport.
    //
    // Non-rotated: LayoutToNDC's offset + scale, expressed in pixels.
    // Rotated: LayoutRotRectNDC's four corners, computed exactly as GL does
    // (center on the layout midpoint, scale, rigid rotate in y-down pixel
    // space, then window-center origin), reduced to their bounding box. The
    // rotations are multiples of 90 degrees, so the box IS the rotated quad -
    // no area is added, and the corner-UV permutation in RecordQuad restores
    // GL's rigid texture rotation.
    void LayoutRectPx(const LayoutVKFrame& f, float x, float y, float w, float h,
                      float& px0, float& py0, float& px1, float& py1)
    {
        if (f.rotMode == 0)
        {
            px0 = f.offsetX + x * f.scaleX;
            py0 = f.offsetY + y * f.scaleY;
            px1 = f.offsetX + (x + w) * f.scaleX;
            py1 = f.offsetY + (y + h) * f.scaleY;
            return;
        }

        const float lx[4] = { x,     x + w, x + w, x };
        const float ly[4] = { y,     y,     y + h, y + h };

        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (int i = 0; i < 4; ++i)
        {
            const float ex = (lx[i] - f.cx) * f.sLx;
            const float ey = (ly[i] - f.cy) * f.sLy;

            float rx, ry;
            switch (f.rotMode)
            {
            case 1: rx = -ey; ry = ex;  break;   // CW 90  (top edge -> right)
            case 2: rx = ey;  ry = -ex; break;   // CCW 90 (top edge -> left)
            case 3: rx = -ex; ry = -ey; break;   // 180
            default: rx = ex; ry = ey;  break;
            }

            const float ppx = f.winW * 0.5f + rx;
            const float ppy = f.winH * 0.5f + ry;
            minX = (std::min)(minX, ppx); maxX = (std::max)(maxX, ppx);
            minY = (std::min)(minY, ppy); maxY = (std::max)(maxY, ppy);
        }

        px0 = minX; py0 = minY; px1 = maxX; py1 = maxY;
    }
}

bool LayoutVK_ComputeFrame(int swapW, int swapH, LayoutVKFrame& out)
{
    out = LayoutVKFrame{};

    if (!LayoutVK_Active() || swapW < 1 || swapH < 1)
        return false;

    const LayoutView& view = *g_activeView;

    // Layout_Render's first act: pull the menu toggles into the layout flags.
    g_layoutShowBackdrop = (config.artwork != 0);
    g_layoutShowOverlay = (config.overlay != 0);
    g_layoutShowBezel = (config.bezel != 0);

    const int sysRot = config.system_rotation;
    int rotMode = 0;
    if (sysRot == ROT90)       rotMode = 1;
    else if (sysRot == ROT270) rotMode = 2;
    else if (sysRot == ROT180) rotMode = 3;

    const bool zoomToScreen = g_layoutZoomToScreen
        || (config.artcrop != 0)
        || (!g_layoutShowBezel);

    float camX, camY, camW, camH;
    if (zoomToScreen && view.screenW > 0 && view.screenH > 0)
    {
        camX = view.screenX; camY = view.screenY;
        camW = view.screenW; camH = view.screenH;
    }
    else
    {
        camX = view.boundsX; camY = view.boundsY;
        camW = view.boundsW; camH = view.boundsH;
    }
    if (camW <= 0 || camH <= 0)
        return false;

    const float aspectCam = camW / camH;

    // Display-aspect override: same priority ladder as GL (forced -aspect /
    // use_aspect first, then the menu GAME ASPECT, then AUTO).
    int winW = swapW, winH = swapH;
    int vpX = 0, vpY = 0, vpW = swapW, vpH = swapH;
    bool aspectOverride = false;
    {
        auto& ws = GetWindowSetup();

        float dispAspect = 0.0f;
        if (ws.aspectOverrideActive && ws.aspectRatio > 0.0f)
            dispAspect = ws.aspectRatio;
        else if (config.game_aspect && config.game_aspect[0])
            dispAspect = aspect_from_string(config.game_aspect);

        if (dispAspect > 0.0f)
        {
            aspectOverride = true;
            const float windowAspect = (float)winW / (float)winH;
            if (windowAspect > dispAspect)
            {
                vpW = (int)((float)winH * dispAspect + 0.5f);
                vpX = (winW - vpW) / 2;
            }
            else if (windowAspect < dispAspect)
            {
                vpH = (int)((float)winW / dispAspect + 0.5f);
                vpY = (winH - vpH) / 2;
            }
            winW = vpW;
            winH = vpH;
        }
    }
    const float aspectWin = (float)winW / (float)winH;

    out.vpX = vpX;
    out.vpY = vpY;
    out.vpW = (uint32_t)((vpW > 0) ? vpW : 0);
    out.vpH = (uint32_t)((vpH > 0) ? vpH : 0);
    out.rotMode = rotMode;
    out.winW = (float)winW;
    out.winH = (float)winH;

    if (rotMode == 0)
    {
        if (aspectOverride)
        {
            out.scaleX = (float)winW / camW;
            out.scaleY = (float)winH / camH;
            out.offsetX = -camX * out.scaleX;
            out.offsetY = -camY * out.scaleY;
        }
        else if (aspectWin > aspectCam)
        {
            const float scale = (float)winH / camH;
            out.scaleX = out.scaleY = scale;
            out.offsetX = (winW - (camW * scale)) / 2.0f - camX * scale;
            out.offsetY = -camY * scale;
        }
        else
        {
            const float scale = (float)winW / camW;
            out.scaleX = out.scaleY = scale;
            out.offsetX = -camX * scale;
            out.offsetY = (winH - (camH * scale)) / 2.0f - camY * scale;
        }
    }
    else
    {
        out.cx = camX + camW * 0.5f;
        out.cy = camY + camH * 0.5f;

        if (aspectOverride)
        {
            if (rotMode == 3) { out.sLx = (float)winW / camW; out.sLy = (float)winH / camH; }
            else              { out.sLx = (float)winH / camW; out.sLy = (float)winW / camH; }
        }
        else
        {
            const float rbW = (rotMode == 3) ? camW : camH;
            const float rbH = (rotMode == 3) ? camH : camW;
            const float s = (aspectWin > (rbW / rbH)) ? ((float)winH / rbH) : ((float)winW / rbW);
            out.sLx = out.sLy = s;
        }
    }

    // First overlay drawable with a texture - GL's "use the first overlay
    // found" rule, gated on the same g_layoutShowOverlay flag.
    float overlayX = 0, overlayY = 0, overlayW = 0, overlayH = 0;
    bool haveOverlay = false;
    if (g_layoutShowOverlay)
    {
        for (const auto& d : view.drawables)
        {
            if (d.layer == LayerType::Overlay && d.element && LayoutTexFor(d.element))
            {
                overlayX = d.x; overlayY = d.y; overlayW = d.w; overlayH = d.h;
                haveOverlay = true;
                break;
            }
        }
    }

    // Any visible backdrop drawable, under the same toggle RecordUnderlay
    // gates on. The screen layer blends additively over whatever this puts
    // down, so the caller has to keep a route that can perform that blend.
    if (g_layoutShowBackdrop)
    {
        for (const auto& d : view.drawables)
        {
            if (d.layer == LayerType::Backdrop && d.element && LayoutTexFor(d.element))
            {
                out.hasBackdrop = true;
                break;
            }
        }
    }

    // Resolve the SCREEN drawable's destination rect (y-down swapchain px).
    for (const auto& d : view.drawables)
    {
        if (d.layer != LayerType::Screen)
            continue;

        float px0, py0, px1, py1;
        LayoutRectPx(out, d.x, d.y, d.w, d.h, px0, py0, px1, py1);

        out.hasScreen = true;
        out.sx0 = px0 + (float)vpX;
        out.sy0 = py0 + (float)vpY;
        out.sx1 = px1 + (float)vpX;
        out.sy1 = py1 + (float)vpY;
        out.screenAlpha = d.alpha;

        if (haveOverlay && overlayW > 0.0f && overlayH > 0.0f)
        {
            out.hasOverlay = true;
            out.ovXform[0] = d.w / overlayW;
            out.ovXform[1] = d.h / overlayH;
            out.ovXform[2] = -(overlayX - d.x) / overlayW;
            out.ovXform[3] = -(overlayY - d.y) / overlayH;
        }
        break;
    }

    const int vattr = (Machine && Machine->drv) ? Machine->drv->video_attributes : 0;
    out.overlayMode = (vattr & VIDEO_TYPE_RASTER_BW) ? 0 : 1;

    out.valid = true;
    return true;
}

// =============================================================================
//                              Layer recording
// =============================================================================

namespace
{
    // Converts a y-down viewport-relative pixel rect into the absolute y-up
    // swapchain rect RecordQuad wants, and records the draw.
    void RecordLayer(VkContext& ctx, VkCommandBuffer cmd, uint32_t fi,
                     LayoutQuadVK& quad, const LayoutVKFrame& f,
                     int swapW, int swapH,
                     VkImageView srcView, VkSampler srcSampler,
                     VkImageView ovView, VkSampler ovSampler,
                     float px0, float py0, float px1, float py1,
                     float alpha, float overlayMode, const float uvXform[4],
                     LQBlendVK blend)
    {
        // Viewport-relative y-down -> absolute y-down -> absolute y-up.
        const float ax0 = px0 + (float)f.vpX;
        const float ax1 = px1 + (float)f.vpX;
        const float ayTop = py0 + (float)f.vpY;
        const float ayBot = py1 + (float)f.vpY;

        const float bottomPx = (float)swapH - ayBot;
        const float topPx = (float)swapH - ayTop;

        quad.RecordQuad(ctx, cmd, fi, srcView, srcSampler, ovView, ovSampler,
                        ax0, bottomPx, ax1, topPx,
                        (uint32_t)swapW, (uint32_t)swapH,
                        f.vpX, f.vpY, f.vpW, f.vpH,
                        f.rotMode, alpha, overlayMode, uvXform, blend);
    }
}

void LayoutVK_RecordUnderlay(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                             LayoutQuadVK& quad, const LayoutVKFrame& f,
                             VkImageView gameView, VkSampler gameSampler,
                             int swapW, int swapH, bool drawScreen)
{
    if (!f.valid || !LayoutVK_Active())
        return;

    // Overlay gel texture for the screen multiply (same "first overlay" rule
    // ComputeFrame used).
    VkTexture* ovTex = nullptr;
    if (f.hasOverlay)
    {
        for (const auto& d : g_activeView->drawables)
        {
            if (d.layer == LayerType::Overlay && d.element)
            {
                ovTex = LayoutTexFor(d.element);
                if (ovTex) break;
            }
        }
    }

    static const float kIdentityXform[4] = { 1.0f, 1.0f, 0.0f, 0.0f };

    // Drawables are already sorted backdrop -> screen -> overlay -> bezel by
    // Layout_Parse's stable_sort, so a single forward walk gives GL's order.
    for (const auto& d : g_activeView->drawables)
    {
        if (d.layer == LayerType::Bezel)
            break;                                  // handled by RecordOverlayArt
        if (d.layer == LayerType::Overlay)
            continue;                               // merged into the screen layer
        if (d.layer == LayerType::Backdrop && !g_layoutShowBackdrop)
            continue;

        float px0, py0, px1, py1;
        LayoutRectPx(f, d.x, d.y, d.w, d.h, px0, py0, px1, py1);

        if (d.layer == LayerType::Screen)
        {
            if (!drawScreen)
                continue;
            RecordLayer(ctx, cmd, frameIndex, quad, f, swapW, swapH,
                        gameView, gameSampler,
                        ovTex ? ovTex->view : VK_NULL_HANDLE,
                        ovTex ? ovTex->sampler : VK_NULL_HANDLE,
                        px0, py0, px1, py1,
                        d.alpha,
                        (f.hasOverlay && ovTex) ? (float)f.overlayMode : -1.0f,
                        f.ovXform,
                        LQBlendVK::Additive);
        }
        else
        {
            VkTexture* t = LayoutTexFor(d.element);
            if (!t)
                continue;
            RecordLayer(ctx, cmd, frameIndex, quad, f, swapW, swapH,
                        t->view, t->sampler, VK_NULL_HANDLE, VK_NULL_HANDLE,
                        px0, py0, px1, py1,
                        d.alpha, -1.0f, kIdentityXform,
                        LQBlendVK::Alpha);
        }
    }
}

void LayoutVK_RecordOverlayArt(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                               LayoutQuadVK& quad, const LayoutVKFrame& f,
                               int swapW, int swapH)
{
    if (!f.valid || !LayoutVK_Active() || !g_layoutShowBezel)
        return;

    static const float kIdentityXform[4] = { 1.0f, 1.0f, 0.0f, 0.0f };

    for (const auto& d : g_activeView->drawables)
    {
        if (d.layer != LayerType::Bezel)
            continue;

        VkTexture* t = LayoutTexFor(d.element);
        if (!t)
            continue;

        float px0, py0, px1, py1;
        LayoutRectPx(f, d.x, d.y, d.w, d.h, px0, py0, px1, py1);
        RecordLayer(ctx, cmd, frameIndex, quad, f, swapW, swapH,
                    t->view, t->sampler, VK_NULL_HANDLE, VK_NULL_HANDLE,
                    px0, py0, px1, py1,
                    d.alpha, -1.0f, kIdentityXform,
                    LQBlendVK::Alpha);
    }
}
