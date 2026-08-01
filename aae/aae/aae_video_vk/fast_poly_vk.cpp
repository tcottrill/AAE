// -----------------------------------------------------------------------------
// fast_poly_vk.cpp - Vulkan fast-poly quad renderer (Phase 4a Plan 3, Task 2).
// Imported from the Bosconian donor; see fast_poly_vk.h for the rename list
// and the single functional addition (viewport-rect override in Render).
// -----------------------------------------------------------------------------

#include "fast_poly_vk.h"
#include <vector>
#include <string>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include "sys_log.h"

static void SafeZero(void* p, size_t n)
{
    if (p && n) memset(p, 0, n);
}

FpolyVK::FpolyVK() = default;

FpolyVK::~FpolyVK()
{
    // Shutdown must be called explicitly
}

// -----------------------------------------------------------------------------
// MakeOrtho
// Column-major 4x4 ortho matrix.
// -----------------------------------------------------------------------------
void FpolyVK::MakeOrtho(float l, float r, float b, float t, float* out16_colMajor)
{
    float* m = out16_colMajor;
    SafeZero(m, sizeof(float) * 16);

    const float rl = (r - l);
    const float tb = (t - b);

    m[0] = 2.0f / rl;
    m[5] = 2.0f / tb;
    m[10] = 1.0f;
    m[12] = -(r + l) / rl;
    m[13] = -(t + b) / tb;
    m[15] = 1.0f;
}

bool FpolyVK::ReadFileBytes(const char* path, std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (!path || !path[0])
        return false;

    // AAE builds with SDL checks (C4996 is an error), so use fopen_s on
    // Windows; the donor's plain fopen remains for other platforms.
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path, "rb");
#else
    f = fopen(path, "rb");
#endif
    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0)
    {
        fclose(f);
        return false;
    }

    outBytes.resize((size_t)sz);
    size_t rd = fread(outBytes.data(), 1, (size_t)sz, f);
    fclose(f);

    return (rd == (size_t)sz);
}

VkShaderModule FpolyVK::CreateShaderModuleFromFile(VkContext& ctx, const char* path)
{
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes))
    {
        LOG_ERROR("FpolyVK: failed to read SPV: %s", path ? path : "(null)");
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = bytes.size();
    ci.pCode = (const uint32_t*)bytes.data();

    VkShaderModule mod = VK_NULL_HANDLE;
    VkResult r = ctx.vkCreateShaderModule_(ctx.device, &ci, nullptr, &mod);
    if (r != VK_SUCCESS || mod == VK_NULL_HANDLE)
    {
        LOG_ERROR("FpolyVK: vkCreateShaderModule failed for %s (VkResult=%d)", path, (int)r);
        return VK_NULL_HANDLE;
    }

    return mod;
}

uint32_t FpolyVK::FindMemoryTypeIdx(VkContext& ctx, uint32_t typeBits, VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties mp{};
    ctx.vkGetPhysicalDeviceMemoryProperties_(ctx.phys, &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) == 0)
            continue;
        if ((mp.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }

    return 0xFFFFFFFFu;
}

bool FpolyVK::CreateBuffer(VkContext& ctx,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memFlags,
    VkBuffer& outBuf,
    VkDeviceMemory& outMem,
    void** outMapped)
{
    outBuf = VK_NULL_HANDLE;
    outMem = VK_NULL_HANDLE;
    if (outMapped) *outMapped = nullptr;

    VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult r = ctx.vkCreateBuffer_(ctx.device, &bci, nullptr, &outBuf);
    if (r != VK_SUCCESS || outBuf == VK_NULL_HANDLE)
    {
        LOG_ERROR("FpolyVK: vkCreateBuffer failed (VkResult=%d)", (int)r);
        return false;
    }

    VkMemoryRequirements mr{};
    ctx.vkGetBufferMemoryRequirements_(ctx.device, outBuf, &mr);

    uint32_t memType = FindMemoryTypeIdx(ctx, mr.memoryTypeBits, memFlags);
    if (memType == 0xFFFFFFFFu)
    {
        LOG_ERROR("FpolyVK: no suitable memory type");
        return false;
    }

    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = memType;

    r = ctx.vkAllocateMemory_(ctx.device, &mai, nullptr, &outMem);
    if (r != VK_SUCCESS || outMem == VK_NULL_HANDLE)
    {
        LOG_ERROR("FpolyVK: vkAllocateMemory failed (VkResult=%d)", (int)r);
        return false;
    }

    r = ctx.vkBindBufferMemory_(ctx.device, outBuf, outMem, 0);
    if (r != VK_SUCCESS)
    {
        LOG_ERROR("FpolyVK: vkBindBufferMemory failed (VkResult=%d)", (int)r);
        return false;
    }

    if (outMapped)
    {
        void* mapped = nullptr;
        r = ctx.vkMapMemory_(ctx.device, outMem, 0, VK_WHOLE_SIZE, 0, &mapped);
        if (r != VK_SUCCESS || !mapped)
        {
            LOG_ERROR("FpolyVK: vkMapMemory failed (VkResult=%d)", (int)r);
            return false;
        }
        *outMapped = mapped;
    }

    return true;
}

void FpolyVK::DestroyBuffer(VkContext& ctx, VkBuffer& buf, VkDeviceMemory& mem, void** mapped)
{
    if (mapped && *mapped)
    {
        ctx.vkUnmapMemory_(ctx.device, mem);
        *mapped = nullptr;
    }

    if (buf)
    {
        ctx.vkDestroyBuffer_(ctx.device, buf, nullptr);
        buf = VK_NULL_HANDLE;
    }

    if (mem)
    {
        ctx.vkFreeMemory_(ctx.device, mem, nullptr);
        mem = VK_NULL_HANDLE;
    }
}

void FpolyVK::CmdSwapchainBarrier(VkContext& ctx,
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout)
{
    VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };

    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

    b.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    b.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

    b.oldLayout = oldLayout;
    b.newLayout = newLayout;

    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;

    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;

    ctx.vkCmdPipelineBarrier2_(cmd, &dep);
}

// -----------------------------------------------------------------------------
// Init / Shutdown
// -----------------------------------------------------------------------------
bool FpolyVK::Init(VkContext& ctx, int surfaceW, int surfaceH, const FastPolyVKCreateInfo* inCI)
{
    FastPolyVKCreateInfo ci{};
    if (inCI) ci = *inCI;

    m_surfaceW = surfaceW;
    m_surfaceH = surfaceH;
    m_flipViewportY = ci.flipViewportY;

    // Descriptor set layout: set=0 binding=0 uniform buffer (proj)
    VkDescriptorSetLayoutBinding b0{};
    b0.binding = 0;
    b0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b0.descriptorCount = 1;
    b0.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo slci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    slci.bindingCount = 1;
    slci.pBindings = &b0;

    VkResult r = ctx.vkCreateDescriptorSetLayout_(ctx.device, &slci, nullptr, &m_setLayout);
    if (r != VK_SUCCESS || !m_setLayout)
    {
        LOG_ERROR("FpolyVK: vkCreateDescriptorSetLayout failed (VkResult=%d)", (int)r);
        return false;
    }

    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &m_setLayout;

    r = ctx.vkCreatePipelineLayout_(ctx.device, &plci, nullptr, &m_pipeLayout);
    if (r != VK_SUCCESS || !m_pipeLayout)
    {
        LOG_ERROR("FpolyVK: vkCreatePipelineLayout failed (VkResult=%d)", (int)r);
        return false;
    }

    // Descriptor pool + sets
    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps.descriptorCount = VkContext::kFramesInFlight;

    VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpci.maxSets = VkContext::kFramesInFlight;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &ps;

    r = ctx.vkCreateDescriptorPool_(ctx.device, &dpci, nullptr, &m_descPool);
    if (r != VK_SUCCESS || !m_descPool)
    {
        LOG_ERROR("FpolyVK: vkCreateDescriptorPool failed (VkResult=%d)", (int)r);
        return false;
    }

    VkDescriptorSetLayout layouts[VkContext::kFramesInFlight];
    for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
        layouts[i] = m_setLayout;

    VkDescriptorSetAllocateInfo dsai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool = m_descPool;
    dsai.descriptorSetCount = VkContext::kFramesInFlight;
    dsai.pSetLayouts = layouts;

    r = ctx.vkAllocateDescriptorSets_(ctx.device, &dsai, m_descSets);
    if (r != VK_SUCCESS)
    {
        LOG_ERROR("FpolyVK: vkAllocateDescriptorSets failed (VkResult=%d)", (int)r);
        return false;
    }

    // UBOs (persistently mapped)
    for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
    {
        if (!CreateBuffer(ctx,
            sizeof(float) * 16,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_ubo[i], m_uboMem[i], &m_mappedUBO[i]))
        {
            LOG_ERROR("FpolyVK: CreateBuffer(UBO) failed");
            return false;
        }

        VkDescriptorBufferInfo dbi{};
        dbi.buffer = m_ubo[i];
        dbi.offset = 0;
        dbi.range = sizeof(float) * 16;

        VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        w.dstSet = m_descSets[i];
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &dbi;

        ctx.vkUpdateDescriptorSets_(ctx.device, 1, &w, 0, nullptr);
    }

    // VBOs (per-frame-slot, persistently mapped, grow as needed -- bug
    // catalog entry 3; see the member comment in fast_poly_vk.h). The donor
    // allocated a single initial buffer; now every slot gets one up front.
    m_colorFormat = ci.colorFormat;
    const uint32_t initCap = (ci.initialCapacityVerts > 0) ? ci.initialCapacityVerts : 8192;
    for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
    {
        m_vboCapacityVerts[i] = initCap;
        if (!EnsureVBOCapacity(ctx, i, initCap))
            return false;
    }

    // Shaders
    VkShaderModule vs = CreateShaderModuleFromFile(ctx, ci.vertSpvPath);
    VkShaderModule fs = CreateShaderModuleFromFile(ctx, ci.fragSpvPath);
    if (!vs || !fs)
        return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    // Vertex input: vec2 pos + R8G8B8A8_UNORM color
    VkVertexInputBindingDescription bind{};
    bind.binding = 0;
    bind.stride = sizeof(_fpdataVK);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr[2]{};
    attr[0].location = 0;
    attr[0].binding = 0;
    attr[0].format = VK_FORMAT_R32G32_SFLOAT;
    attr[0].offset = 0;

    attr[1].location = 1;
    attr[1].binding = 0;
    attr[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attr[1].offset = (uint32_t)offsetof(_fpdataVK, color);

    VkPipelineVertexInputStateCreateInfo vis{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vis.vertexBindingDescriptionCount = 1;
    vis.pVertexBindingDescriptions = &bind;
    vis.vertexAttributeDescriptionCount = 2;
    vis.pVertexAttributeDescriptions = attr;

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

    VkPipelineColorBlendAttachmentState cbA{};
    cbA.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbA.blendEnable = VK_TRUE;
    cbA.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cbA.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbA.colorBlendOp = VK_BLEND_OP_ADD;
    cbA.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cbA.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbA.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &cbA;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    // Dynamic rendering info: build against the caller-supplied color format
    // (Plan 4 Task 1), falling back to ctx.swapchainFormat when unset --
    // preserves Plan 3 behavior for the current caller (vulkan_renderer.cpp),
    // which does not yet pass a format and composites straight to the
    // swapchain. Task 3 passes the offscreen RenderTargetVK's format.
    const VkFormat pipelineColorFormat =
        (m_colorFormat != VK_FORMAT_UNDEFINED) ? m_colorFormat : ctx.swapchainFormat;

    VkPipelineRenderingCreateInfo pr{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    pr.colorAttachmentCount = 1;
    pr.pColorAttachmentFormats = &pipelineColorFormat;

    VkGraphicsPipelineCreateInfo gp{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gp.pNext = &pr;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vis;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dyn;
    gp.layout = m_pipeLayout;

    r = ctx.vkCreateGraphicsPipelines_(ctx.device, VK_NULL_HANDLE, 1, &gp, nullptr, &m_pipeline);

    // Shaders can be destroyed after pipeline creation
    ctx.vkDestroyShaderModule_(ctx.device, vs, nullptr);
    ctx.vkDestroyShaderModule_(ctx.device, fs, nullptr);

    if (r != VK_SUCCESS || !m_pipeline)
    {
        LOG_ERROR("FpolyVK: vkCreateGraphicsPipelines failed (VkResult=%d)", (int)r);
        return false;
    }

    return true;
}

void FpolyVK::Shutdown(VkContext& ctx)
{
    // Destroy every slot's buffer AND drain every slot's stale list. By
    // shutdown time the caller has already device-waited-idle (see
    // vulkan_renderer.cpp's vkchain_shutdown/EnsureRasterRenderer), so no
    // in-flight submission can still be reading any of these.
    for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
    {
        DestroyBuffer(ctx, m_vbo[i], m_vboMem[i], &m_mappedVBO[i]);
        DrainStaleBuffers(ctx, i);
        m_vboCapacityVerts[i] = 0;
    }

    for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
        DestroyBuffer(ctx, m_ubo[i], m_uboMem[i], &m_mappedUBO[i]);

    if (m_pipeline) { ctx.vkDestroyPipeline_(ctx.device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
    if (m_pipeLayout) { ctx.vkDestroyPipelineLayout_(ctx.device, m_pipeLayout, nullptr); m_pipeLayout = VK_NULL_HANDLE; }
    if (m_descPool) { ctx.vkDestroyDescriptorPool_(ctx.device, m_descPool, nullptr); m_descPool = VK_NULL_HANDLE; }
    if (m_setLayout) { ctx.vkDestroyDescriptorSetLayout_(ctx.device, m_setLayout, nullptr); m_setLayout = VK_NULL_HANDLE; }

    vertices.clear();
}

void FpolyVK::SetSurfaceSize(int surfaceW, int surfaceH)
{
    m_surfaceW = surfaceW;
    m_surfaceH = surfaceH;
}

// -----------------------------------------------------------------------------
// EnsureVBOCapacity
// Operates on the CURRENT slot (frameIndex) only. On growth, the old
// {buf, mem, mapped} is pushed onto that slot's stale list rather than
// destroyed immediately -- the GPU may still be reading it from an
// in-flight submission (bug catalog entry 3). The stale list is drained
// in DrainStaleBuffers, called at the top of Render() for this slot, after
// VK_BeginFrame's fence wait has proven the GPU is done with it.
// -----------------------------------------------------------------------------
bool FpolyVK::EnsureVBOCapacity(VkContext& ctx, uint32_t frameIndex, uint32_t wantVerts)
{
    if (wantVerts <= m_vboCapacityVerts[frameIndex] && m_vbo[frameIndex] && m_vboMem[frameIndex] && m_mappedVBO[frameIndex])
        return true;

    uint32_t newCap = (m_vboCapacityVerts[frameIndex] > 0) ? m_vboCapacityVerts[frameIndex] : 8192;
    while (newCap < wantVerts)
        newCap *= 2;

    // Retire the old buffer to this slot's stale list instead of destroying
    // it now -- do NOT call DestroyBuffer here.
    if (m_vbo[frameIndex] || m_vboMem[frameIndex] || m_mappedVBO[frameIndex])
    {
        StaleBuffer sb{ m_vbo[frameIndex], m_vboMem[frameIndex], m_mappedVBO[frameIndex] };
        m_staleBuffers[frameIndex].push_back(sb);
        m_vbo[frameIndex] = VK_NULL_HANDLE;
        m_vboMem[frameIndex] = VK_NULL_HANDLE;
        m_mappedVBO[frameIndex] = nullptr;
    }

    VkDeviceSize bytes = (VkDeviceSize)newCap * (VkDeviceSize)sizeof(_fpdataVK);
    if (!CreateBuffer(ctx,
        bytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_vbo[frameIndex], m_vboMem[frameIndex], &m_mappedVBO[frameIndex]))
    {
        LOG_ERROR("FpolyVK: CreateBuffer(VBO) failed (frameIndex=%u)", frameIndex);
        return false;
    }

    m_vboCapacityVerts[frameIndex] = newCap;
    return true;
}

// -----------------------------------------------------------------------------
// DrainStaleBuffers
// Unmaps + destroys every retired buffer for this slot. Only safe to call
// once VK_BeginFrame's fence wait has proven the GPU is done with the
// in-flight submission(s) that may have referenced these buffers -- i.e.
// at the top of Render() for this frameIndex, before any new upload.
// -----------------------------------------------------------------------------
void FpolyVK::DrainStaleBuffers(VkContext& ctx, uint32_t frameIndex)
{
    for (auto& sb : m_staleBuffers[frameIndex])
        DestroyBuffer(ctx, sb.buf, sb.mem, &sb.mapped);
    m_staleBuffers[frameIndex].clear();
}

void FpolyVK::UpdateGlobals(VkContext& ctx, uint32_t frameIndex)
{
    (void)ctx;

    float proj[16];
    MakeOrtho(0.0f, (float)m_surfaceW, 0.0f, (float)m_surfaceH, proj);

    if (frameIndex < VkContext::kFramesInFlight && m_mappedUBO[frameIndex])
        memcpy(m_mappedUBO[frameIndex], proj, sizeof(float) * 16);
}

// -----------------------------------------------------------------------------
// addPoly
// -----------------------------------------------------------------------------
void FpolyVK::addPoly(float x, float y, float size, uint32_t color)
{
    const float x0 = x;
    const float y0 = y;
    const float x1 = x + size;
    const float y1 = y + size;

    vertices.emplace_back(x0, y0, color);
    vertices.emplace_back(x1, y0, color);
    vertices.emplace_back(x1, y1, color);

    vertices.emplace_back(x1, y1, color);
    vertices.emplace_back(x0, y1, color);
    vertices.emplace_back(x0, y0, color);
}

// -----------------------------------------------------------------------------
// Render
// Records a swapchain overlay pass that draws all queued polys.
// -----------------------------------------------------------------------------
void FpolyVK::Render(VkContext& ctx,
    VkCommandBuffer cmd,
    uint32_t imageIndex,
    uint32_t frameIndex,
    bool clear,
    float clearR, float clearG, float clearB, float clearA)
{
    if (frameIndex >= VkContext::kFramesInFlight)
        return;

    // Drain this slot's stale buffers at the top of Render, before any
    // upload, regardless of whether this particular frame has polys queued.
    // We are only here because the caller's VK_BeginFrame already
    // fence-waited on this slot, which proves the GPU is done with whatever
    // this slot's previous frame referenced (bug catalog entry 3) -- so it
    // is safe to unmap/destroy any buffers this slot retired last time it
    // grew.
    DrainStaleBuffers(ctx, frameIndex);

    if (vertices.empty())
        return;

    if (!EnsureVBOCapacity(ctx, frameIndex, (uint32_t)vertices.size()))
        return;

    // Upload CPU vertices -> this slot's mapped VBO
    memcpy(m_mappedVBO[frameIndex], vertices.data(), sizeof(_fpdataVK) * vertices.size());

    UpdateGlobals(ctx, frameIndex);

    // Frame-pass contract change (2026-07 sys_vk update): VK_BeginFrame now
    // opens the swapchain dynamic-rendering pass (with barrier + clear) and
    // VK_EndFrame closes it and transitions to PRESENT. This function must
    // only record draws into that already-open pass. Do NOT begin/end
    // rendering or emit image barriers here -- barriers are illegal inside
    // an open rendering pass and nested passes crash the driver.
    // The clear/clearR..A parameters are ignored; the frame pass clears.
    (void)clear; (void)clearR; (void)clearG; (void)clearB; (void)clearA;
    (void)imageIndex;

    // Viewport/scissor (support OpenGL-style bottom-left if requested).
    // When the letterbox override is set, the viewport/scissor cover only
    // that rect instead of the full swapchain.
    const int rx = m_vpOverride ? m_vpX : 0;
    const int ry = m_vpOverride ? m_vpY : 0;
    const int rw = m_vpOverride ? m_vpW : (int)ctx.swapchainExtent.width;
    const int rh = m_vpOverride ? m_vpH : (int)ctx.swapchainExtent.height;

    VkViewport vp{};
    vp.x = (float)rx;
    vp.width = (float)rw;

    if (m_flipViewportY)
    {
        vp.y = (float)(ry + rh);
        vp.height = -(float)rh;
    }
    else
    {
        vp.y = (float)ry;
        vp.height = (float)rh;
    }

    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    VkRect2D sc{};
    sc.offset = { rx, ry };
    sc.extent = { (uint32_t)rw, (uint32_t)rh };

    ctx.vkCmdSetViewport_(cmd, 0, 1, &vp);
    ctx.vkCmdSetScissor_(cmd, 0, 1, &sc);

    ctx.vkCmdBindPipeline_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    ctx.vkCmdBindDescriptorSets_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipeLayout, 0, 1, &m_descSets[frameIndex], 0, nullptr);

    VkDeviceSize off = 0;
    ctx.vkCmdBindVertexBuffers_(cmd, 0, 1, &m_vbo[frameIndex], &off);

    ctx.vkCmdDraw_(cmd, (uint32_t)vertices.size(), 1, 0, 0);

    // clear after drawing for now.
    vertices.clear();
}
