// -----------------------------------------------------------------------------
// shot_draw_vk.cpp - Vulkan textured vector shot pass.
// See shot_draw_vk.h for the GL reference and lifetime reasoning.
// ASCII-only comments.
// -----------------------------------------------------------------------------

#include "shot_draw_vk.h"
#include "sys_log.h"

#include <stdio.h>
#include <string.h>
#include <vector>

// -----------------------------------------------------------------------------
// File-local helpers (same patterns as vector_draw_vk.cpp)
// -----------------------------------------------------------------------------
static uint32_t ShotFindMemoryTypeIdx_(VkContext& ctx, uint32_t typeBits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp{};
    ctx.vkGetPhysicalDeviceMemoryProperties_(ctx.phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0xFFFFFFFFu;
}

static bool ShotReadFileBytes_(const char* path, std::vector<uint8_t>& out)
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

static VkShaderModule ShotCreateShaderModule_(VkContext& ctx, const char* path)
{
    std::vector<uint8_t> bytes;
    if (!ShotReadFileBytes_(path, bytes))
    {
        LOG_ERROR("ShotDrawVK: failed to read %s", path ? path : "(null)");
        return VK_NULL_HANDLE;
    }
    if ((bytes.size() & 3u) != 0u)
    {
        LOG_ERROR("ShotDrawVK: SPV size not 4-byte aligned: %s", path);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = bytes.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(bytes.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    if (ctx.vkCreateShaderModule_(ctx.device, &ci, nullptr, &mod) != VK_SUCCESS)
    {
        LOG_ERROR("ShotDrawVK: vkCreateShaderModule failed for %s", path);
        return VK_NULL_HANDLE;
    }
    return mod;
}

// -----------------------------------------------------------------------------
// Init / Shutdown
// -----------------------------------------------------------------------------
bool ShotDrawVK::Init(VkContext& ctx, const ShotDrawVKCreateInfo* ci)
{
    if (!ctx.device)
        return false;
    if (initialized_)
        Shutdown(ctx);   // idempotent re-Init: never leak the previous set

    ShotDrawVKCreateInfo def{};
    if (!ci) ci = &def;

    vertSpv_ = ci->vertSpv;
    fragSpv_ = ci->fragSpv;
    initialCap_ = (ci->initialVertexCapacity >= 6) ? ci->initialVertexCapacity : 6;

    const VkFormat colorFormat = (ci->colorFormat != VK_FORMAT_UNDEFINED)
        ? ci->colorFormat : ctx.swapchainFormat;

    // Descriptor set layout: binding 0 = the shot texture.
    {
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
            LOG_ERROR("ShotDrawVK: descriptor set layout failed");
            return false;
        }
    }

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc.offset = 0;
    pc.size = sizeof(ShotPush);

    VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &setLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pc;
    if (ctx.vkCreatePipelineLayout_(ctx.device, &pli, nullptr, &pipeLayout_) != VK_SUCCESS)
    {
        LOG_ERROR("ShotDrawVK: pipeline layout failed");
        Shutdown(ctx);
        return false;
    }

    // Descriptor pool + one set per frame in flight (the shot texture is
    // per-game-stable; the set is rewritten per frame after the fence wait).
    {
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = VkContext::kFramesInFlight;

        VkDescriptorPoolCreateInfo dpi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        dpi.maxSets = VkContext::kFramesInFlight;
        dpi.poolSizeCount = 1;
        dpi.pPoolSizes = &ps;
        if (ctx.vkCreateDescriptorPool_(ctx.device, &dpi, nullptr, &descPool_) != VK_SUCCESS)
        {
            LOG_ERROR("ShotDrawVK: descriptor pool failed");
            Shutdown(ctx);
            return false;
        }

        VkDescriptorSetLayout layouts[VkContext::kFramesInFlight];
        for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
            layouts[i] = setLayout_;
        VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        ai.descriptorPool = descPool_;
        ai.descriptorSetCount = VkContext::kFramesInFlight;
        ai.pSetLayouts = layouts;
        if (ctx.vkAllocateDescriptorSets_(ctx.device, &ai, descSets_) != VK_SUCCESS)
        {
            LOG_ERROR("ShotDrawVK: descriptor alloc failed");
            Shutdown(ctx);
            return false;
        }
    }

    // Pipeline: txdata vertex stream, TRIANGLE_LIST, additive SRC_ALPHA/ONE
    // (GL draw_textured_shots), dynamic viewport/scissor.
    {
        VkShaderModule vs = ShotCreateShaderModule_(ctx, vertSpv_.c_str());
        VkShaderModule fs = ShotCreateShaderModule_(ctx, fragSpv_.c_str());
        if (!vs || !fs)
        {
            if (vs) ctx.vkDestroyShaderModule_(ctx.device, vs, nullptr);
            if (fs) ctx.vkDestroyShaderModule_(ctx.device, fs, nullptr);
            Shutdown(ctx);
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
        vibd.stride = sizeof(txdata);
        vibd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // txdata: x,y | tx,ty | packed RGBA byte color (normalized, exactly
        // GL's GL_UNSIGNED_BYTE + GL_TRUE attrib).
        VkVertexInputAttributeDescription viad[3]{};
        viad[0].location = 0;
        viad[0].binding = 0;
        viad[0].format = VK_FORMAT_R32G32_SFLOAT;
        viad[0].offset = (uint32_t)offsetof(txdata, x);
        viad[1].location = 1;
        viad[1].binding = 0;
        viad[1].format = VK_FORMAT_R32G32_SFLOAT;
        viad[1].offset = (uint32_t)offsetof(txdata, tx);
        viad[2].location = 2;
        viad[2].binding = 0;
        viad[2].format = VK_FORMAT_R8G8B8A8_UNORM;
        viad[2].offset = (uint32_t)offsetof(txdata, color);

        VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &vibd;
        vi.vertexAttributeDescriptionCount = 3;
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
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

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

        VkResult pr = ctx.vkCreateGraphicsPipelines_(
            ctx.device, ctx.pipelineCache, 1, &gpi, nullptr, &pipeline_);

        ctx.vkDestroyShaderModule_(ctx.device, vs, nullptr);
        ctx.vkDestroyShaderModule_(ctx.device, fs, nullptr);

        if (pr != VK_SUCCESS)
        {
            LOG_ERROR("ShotDrawVK: vkCreateGraphicsPipelines failed (VkResult=%d, format=%d)",
                      (int)pr, (int)colorFormat);
            pipeline_ = VK_NULL_HANDLE;
            Shutdown(ctx);
            return false;
        }
    }

    initialized_ = true;
    LOG_INFO("ShotDrawVK: online (textured shots into format %d)", (int)colorFormat);
    return true;
}

void ShotDrawVK::Shutdown(VkContext& ctx)
{
    if (pipeline_)   { ctx.vkDestroyPipeline_(ctx.device, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipeLayout_) { ctx.vkDestroyPipelineLayout_(ctx.device, pipeLayout_, nullptr); pipeLayout_ = VK_NULL_HANDLE; }
    if (setLayout_)  { ctx.vkDestroyDescriptorSetLayout_(ctx.device, setLayout_, nullptr); setLayout_ = VK_NULL_HANDLE; }
    if (descPool_)   { ctx.vkDestroyDescriptorPool_(ctx.device, descPool_, nullptr); descPool_ = VK_NULL_HANDLE; }

    for (uint32_t fi = 0; fi < VkContext::kFramesInFlight; ++fi)
    {
        if (vboMapped_[fi]) { ctx.vkUnmapMemory_(ctx.device, vboMem_[fi]); vboMapped_[fi] = nullptr; }
        if (vbo_[fi])       { ctx.vkDestroyBuffer_(ctx.device, vbo_[fi], nullptr); vbo_[fi] = VK_NULL_HANDLE; }
        if (vboMem_[fi])    { ctx.vkFreeMemory_(ctx.device, vboMem_[fi], nullptr); vboMem_[fi] = VK_NULL_HANDLE; }
        vboCap_[fi] = 0;
        descSets_[fi] = VK_NULL_HANDLE;
    }

    initialized_ = false;
}

// -----------------------------------------------------------------------------
// EnsureBuffer - per-slot host-visible VBO, grown on demand. The old buffer
// is destroyed immediately: Record only runs after VK_BeginFrame's fence
// wait proved this slot's previous submission complete (see header).
// -----------------------------------------------------------------------------
bool ShotDrawVK::EnsureBuffer(VkContext& ctx, uint32_t frameIndex, VkDeviceSize neededBytes)
{
    if (vboCap_[frameIndex] >= neededBytes && vbo_[frameIndex])
        return true;

    if (vboMapped_[frameIndex]) { ctx.vkUnmapMemory_(ctx.device, vboMem_[frameIndex]); vboMapped_[frameIndex] = nullptr; }
    if (vbo_[frameIndex])       { ctx.vkDestroyBuffer_(ctx.device, vbo_[frameIndex], nullptr); vbo_[frameIndex] = VK_NULL_HANDLE; }
    if (vboMem_[frameIndex])    { ctx.vkFreeMemory_(ctx.device, vboMem_[frameIndex], nullptr); vboMem_[frameIndex] = VK_NULL_HANDLE; }
    vboCap_[frameIndex] = 0;

    VkDeviceSize cap = (VkDeviceSize)initialCap_ * sizeof(txdata);
    while (cap < neededBytes)
        cap *= 2;

    VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = cap;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (ctx.vkCreateBuffer_(ctx.device, &bi, nullptr, &vbo_[frameIndex]) != VK_SUCCESS)
    {
        LOG_ERROR("ShotDrawVK: vkCreateBuffer failed (%llu bytes)", (unsigned long long)cap);
        return false;
    }

    VkMemoryRequirements mr{};
    ctx.vkGetBufferMemoryRequirements_(ctx.device, vbo_[frameIndex], &mr);
    uint32_t mt = ShotFindMemoryTypeIdx_(ctx, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xFFFFFFFFu)
    {
        LOG_ERROR("ShotDrawVK: no host-visible memory type");
        return false;
    }

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mt;
    if (ctx.vkAllocateMemory_(ctx.device, &ai, nullptr, &vboMem_[frameIndex]) != VK_SUCCESS ||
        ctx.vkBindBufferMemory_(ctx.device, vbo_[frameIndex], vboMem_[frameIndex], 0) != VK_SUCCESS ||
        ctx.vkMapMemory_(ctx.device, vboMem_[frameIndex], 0, VK_WHOLE_SIZE, 0, &vboMapped_[frameIndex]) != VK_SUCCESS)
    {
        LOG_ERROR("ShotDrawVK: buffer memory alloc/bind/map failed");
        return false;
    }

    vboCap_[frameIndex] = cap;
    return true;
}

// -----------------------------------------------------------------------------
// Record
// -----------------------------------------------------------------------------
void ShotDrawVK::Record(VkContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
                        const float proj[16], const txdata* verts, uint32_t count,
                        VkImageView texView, VkSampler texSampler,
                        uint32_t targetWidth, uint32_t targetHeight,
                        float fadeInner, float fadeOuter)
{
    if (!initialized_ || cmd == VK_NULL_HANDLE || !verts || count == 0)
        return;
    if (frameIndex >= VkContext::kFramesInFlight)
        return;
    if (texView == VK_NULL_HANDLE || texSampler == VK_NULL_HANDLE)
        return;
    if (targetWidth == 0 || targetHeight == 0)
        return;

    const VkDeviceSize bytes = (VkDeviceSize)count * sizeof(txdata);
    if (!EnsureBuffer(ctx, frameIndex, bytes))
        return;
    memcpy(vboMapped_[frameIndex], verts, (size_t)bytes);

    // Rewriting this slot's set is safe post-fence-wait (see header).
    VkDescriptorImageInfo ii{};
    ii.sampler = texSampler;
    ii.imageView = texView;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet = descSets_[frameIndex];
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    ctx.vkUpdateDescriptorSets_(ctx.device, 1, &w, 0, nullptr);

    ctx.vkCmdBindPipeline_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    ctx.vkCmdBindDescriptorSets_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 pipeLayout_, 0, 1, &descSets_[frameIndex], 0, nullptr);

    // Flipped viewport, matching VectorDrawVK's convention so the same beam
    // ortho lands shots on the exact RT rows as the beams beside them.
    VkViewport vpo{};
    vpo.x = 0.0f;
    vpo.y = (float)targetHeight;
    vpo.width = (float)targetWidth;
    vpo.height = -(float)targetHeight;
    vpo.minDepth = 0.0f;
    vpo.maxDepth = 1.0f;
    ctx.vkCmdSetViewport_(cmd, 0, 1, &vpo);

    VkRect2D sc{};
    sc.extent.width = targetWidth;
    sc.extent.height = targetHeight;
    ctx.vkCmdSetScissor_(cmd, 0, 1, &sc);

    ShotPush push{};
    memcpy(push.proj, proj, sizeof(push.proj));
    push.fade[0] = fadeInner;
    push.fade[1] = fadeOuter;
    ctx.vkCmdPushConstants_(cmd, pipeLayout_,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(ShotPush), &push);

    VkDeviceSize off = 0;
    ctx.vkCmdBindVertexBuffers_(cmd, 0, 1, &vbo_[frameIndex], &off);
    ctx.vkCmdDraw_(cmd, count, 1, 0, 0);
}
