//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// -----------------------------------------------------------------------------
// hdr_output_vk.cpp - HDR10 scene image and final encode pass. See
// hdr_output_vk.h. ASCII-only comments.
// -----------------------------------------------------------------------------

#include "hdr_output_vk.h"
#include "sys_log.h"

#include <stdio.h>
#include <string.h>
#include <vector>

namespace {

const char* const kVertSpv = "shaders/vk/hdr_output_vk.vert.spv";
const char* const kFragSpv = "shaders/vk/hdr_output_vk.frag.spv";

struct EncodePush
{
    float paperWhite;
    float maxNits;
};

bool ReadFileBytes(const char* path, std::vector<uint8_t>& out)
{
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

VkShaderModule LoadModule(VkContext& ctx, const char* path)
{
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes))
    {
        LOG_ERROR("HdrOutputVK: failed to read %s", path);
        return VK_NULL_HANDLE;
    }
    if ((bytes.size() & 3u) != 0u)
    {
        LOG_ERROR("HdrOutputVK: SPV size not 4-byte aligned: %s", path);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = bytes.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(bytes.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    if (ctx.vkCreateShaderModule_(ctx.device, &ci, nullptr, &mod) != VK_SUCCESS)
    {
        LOG_ERROR("HdrOutputVK: vkCreateShaderModule failed for %s", path);
        return VK_NULL_HANDLE;
    }
    return mod;
}

uint32_t FindMemoryType(VkContext& ctx, uint32_t typeBits, VkMemoryPropertyFlags props)
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

} // namespace

// -----------------------------------------------------------------------------
// Init
// -----------------------------------------------------------------------------
bool HdrOutputVK::Init(VkContext& ctx)
{
    if (!ctx.device)
        return false;

    // Nearest + clamp: the scene is the same size as the swapchain, so the
    // encode pass is a 1:1 per-pixel conversion with no filtering.
    VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (ctx.vkCreateSampler_(ctx.device, &sci, nullptr, &sampler_) != VK_SUCCESS)
    {
        LOG_ERROR("HdrOutputVK: vkCreateSampler failed");
        Shutdown(ctx);
        return false;
    }

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
        LOG_ERROR("HdrOutputVK: vkCreateDescriptorSetLayout failed");
        Shutdown(ctx);
        return false;
    }

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    if (ctx.vkCreateDescriptorPool_(ctx.device, &dpi, nullptr, &descPool_) != VK_SUCCESS)
    {
        LOG_ERROR("HdrOutputVK: vkCreateDescriptorPool failed");
        Shutdown(ctx);
        return false;
    }

    VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool = descPool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &setLayout_;
    if (ctx.vkAllocateDescriptorSets_(ctx.device, &dsai, &descSet_) != VK_SUCCESS)
    {
        LOG_ERROR("HdrOutputVK: vkAllocateDescriptorSets failed");
        Shutdown(ctx);
        return false;
    }

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = (uint32_t)sizeof(EncodePush);

    VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &setLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    if (ctx.vkCreatePipelineLayout_(ctx.device, &pli, nullptr, &pipeLayout_) != VK_SUCCESS)
    {
        LOG_ERROR("HdrOutputVK: vkCreatePipelineLayout failed");
        Shutdown(ctx);
        return false;
    }

    if (!BuildPipeline(ctx))
    {
        Shutdown(ctx);
        return false;
    }

    if (!Resize(ctx, ctx.swapchainExtent.width, ctx.swapchainExtent.height) || !IsValid())
    {
        LOG_ERROR("HdrOutputVK: scene image creation failed");
        Shutdown(ctx);
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// BuildPipeline - fullscreen triangle, no blending, writes the swapchain format.
// -----------------------------------------------------------------------------
bool HdrOutputVK::BuildPipeline(VkContext& ctx)
{
    VkShaderModule vs = LoadModule(ctx, kVertSpv);
    VkShaderModule fs = LoadModule(ctx, kFragSpv);
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

    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

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
    cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    // The pass this pipeline runs in is the real swapchain image, so it is
    // built against swapchainFormat, never frameFormat.
    const VkFormat colorFormat = ctx.swapchainFormat;
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

    VkResult pr = ctx.vkCreateGraphicsPipelines_(
        ctx.device, ctx.pipelineCache, 1, &gpi, nullptr, &pipeline_);

    ctx.vkDestroyShaderModule_(ctx.device, vs, nullptr);
    ctx.vkDestroyShaderModule_(ctx.device, fs, nullptr);

    if (pr != VK_SUCCESS)
    {
        LOG_ERROR("HdrOutputVK: vkCreateGraphicsPipelines failed (VkResult=%d, format=%d)",
                  (int)pr, (int)colorFormat);
        pipeline_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Resize
// -----------------------------------------------------------------------------
bool HdrOutputVK::Resize(VkContext& ctx, uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0)
        return true;
    if (image_ && w == width_ && h == height_)
        return true;

    DestroyImage(ctx);

    VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = kSceneFormat;
    ici.extent = { w, h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    // TRANSFER_SRC: the F12 screenshot path reads the scene back.
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (ctx.vkCreateImage_(ctx.device, &ici, nullptr, &image_) != VK_SUCCESS)
    {
        LOG_ERROR("HdrOutputVK: vkCreateImage failed (%ux%u)", w, h);
        image_ = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements mr{};
    ctx.vkGetImageMemoryRequirements_(ctx.device, image_, &mr);
    const uint32_t mt = FindMemoryType(ctx, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt == 0xFFFFFFFFu)
    {
        LOG_ERROR("HdrOutputVK: no device-local memory type for the scene image");
        DestroyImage(ctx);
        return false;
    }
    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mt;
    if (ctx.vkAllocateMemory_(ctx.device, &mai, nullptr, &memory_) != VK_SUCCESS)
    {
        LOG_ERROR("HdrOutputVK: vkAllocateMemory failed for the scene image");
        memory_ = VK_NULL_HANDLE;
        DestroyImage(ctx);
        return false;
    }
    if (ctx.vkBindImageMemory_(ctx.device, image_, memory_, 0) != VK_SUCCESS)
    {
        LOG_ERROR("HdrOutputVK: vkBindImageMemory failed");
        DestroyImage(ctx);
        return false;
    }

    VkImageViewCreateInfo iv{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    iv.image = image_;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = kSceneFormat;
    iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    iv.subresourceRange.levelCount = 1;
    iv.subresourceRange.layerCount = 1;
    if (ctx.vkCreateImageView_(ctx.device, &iv, nullptr, &view_) != VK_SUCCESS)
    {
        LOG_ERROR("HdrOutputVK: vkCreateImageView failed");
        view_ = VK_NULL_HANDLE;
        DestroyImage(ctx);
        return false;
    }

    width_ = w;
    height_ = h;

    VkDescriptorImageInfo dii{};
    dii.sampler = sampler_;
    dii.imageView = view_;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet wr{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    wr.dstSet = descSet_;
    wr.dstBinding = 0;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &dii;
    ctx.vkUpdateDescriptorSets_(ctx.device, 1, &wr, 0, nullptr);

    return true;
}

// -----------------------------------------------------------------------------
// RecordEncode
// -----------------------------------------------------------------------------
void HdrOutputVK::RecordEncode(VkContext& ctx, VkCommandBuffer cmd, uint32_t imageIndex,
                               float paperWhite, float maxNits)
{
    if (!pipeline_ || imageIndex >= ctx.swapchainViews.size())
    {
        LOG_ERROR("HdrOutputVK::RecordEncode: not ready (pipeline=%p, imageIndex=%u)",
                  (void*)pipeline_, imageIndex);
        return;
    }

    // The draw covers every pixel, so the previous contents are not needed.
    VkRenderingAttachmentInfo colorAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAtt.imageView = ctx.swapchainViews[imageIndex];
    colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    ri.renderArea.offset = { 0, 0 };
    ri.renderArea.extent = ctx.swapchainExtent;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &colorAtt;

    ctx.vkCmdBeginRendering_(cmd, &ri);

    VkViewport vp{};
    vp.width = (float)ctx.swapchainExtent.width;
    vp.height = (float)ctx.swapchainExtent.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    ctx.vkCmdSetViewport_(cmd, 0, 1, &vp);

    VkRect2D sc{};
    sc.extent = ctx.swapchainExtent;
    ctx.vkCmdSetScissor_(cmd, 0, 1, &sc);

    ctx.vkCmdBindPipeline_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    ctx.vkCmdBindDescriptorSets_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_,
                                 0, 1, &descSet_, 0, nullptr);

    EncodePush pc{ paperWhite, maxNits };
    ctx.vkCmdPushConstants_(cmd, pipeLayout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, (uint32_t)sizeof(pc), &pc);

    ctx.vkCmdDraw_(cmd, 3, 1, 0, 0);

    ctx.vkCmdEndRendering_(cmd);
}

// -----------------------------------------------------------------------------
// Shutdown / DestroyImage
// -----------------------------------------------------------------------------
void HdrOutputVK::DestroyImage(VkContext& ctx)
{
    if (view_)   { ctx.vkDestroyImageView_(ctx.device, view_, nullptr); view_ = VK_NULL_HANDLE; }
    if (image_)  { ctx.vkDestroyImage_(ctx.device, image_, nullptr); image_ = VK_NULL_HANDLE; }
    if (memory_) { ctx.vkFreeMemory_(ctx.device, memory_, nullptr); memory_ = VK_NULL_HANDLE; }
    width_ = 0;
    height_ = 0;
}

void HdrOutputVK::Shutdown(VkContext& ctx)
{
    if (!ctx.device)
        return;

    DestroyImage(ctx);

    if (pipeline_)   { ctx.vkDestroyPipeline_(ctx.device, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipeLayout_) { ctx.vkDestroyPipelineLayout_(ctx.device, pipeLayout_, nullptr); pipeLayout_ = VK_NULL_HANDLE; }
    // Destroying the pool frees the set allocated from it.
    if (descPool_)   { ctx.vkDestroyDescriptorPool_(ctx.device, descPool_, nullptr); descPool_ = VK_NULL_HANDLE; }
    descSet_ = VK_NULL_HANDLE;
    if (setLayout_)  { ctx.vkDestroyDescriptorSetLayout_(ctx.device, setLayout_, nullptr); setLayout_ = VK_NULL_HANDLE; }
    if (sampler_)    { ctx.vkDestroySampler_(ctx.device, sampler_, nullptr); sampler_ = VK_NULL_HANDLE; }
}
