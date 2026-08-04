// -----------------------------------------------------------------------------
// vector_draw_vk.cpp - Vulkan backend for the beam vector renderer.
// See vector_draw_vk.h for the buffer/append discipline and the SSAA feather.
//
// Draws the current frame's batches as instanced TRIANGLE_STRIP quads:
//   lines -> coverage-AA segments   (vector_line_vk)
//   joins -> round caps / corners   (vector_disc_vk)   rebuilt from the lines
//   shots -> procedural radial core (vector_shot_vk)
// Blend variants mirror beam_draw_all(): color = additive (lines) + VK_BLEND_OP_MAX
// (joins); B/W = alpha-over. Records into a pass the caller already opened.
//
// ASCII-only comments.
// -----------------------------------------------------------------------------
#include "vector_draw_vk.h"
#include "config.h"          // config.line_smoothing / config.corner_strength
#include "sys_log.h"

#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstddef>
#include <cstdint>

// Push block: must match the shared layout in vector_*_vk.{vert,frag} exactly.
struct BeamPushVK
{
    float proj[16];        // offset 0
    float uAA;             // 64
    float uStrength;       // 68
    float uPremult;        // 72
    float uCorePower;      // 76
    float uBloomPower;     // 80
    float uBloomIntensity; // 84
    float uOverdrive;      // 88
};                         // 92 bytes (<= 128 guaranteed push-constant limit)

enum { VKBLEND_ADDITIVE, VKBLEND_OVER, VKBLEND_MAX };

// =============================================================================
// File-local helpers (same patterns as fast_poly_vk.cpp / screen_quad_vk.cpp)
// =============================================================================
static uint32_t FindMemoryTypeIdx_(VkContext& ctx, uint32_t typeBits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp{};
    ctx.vkGetPhysicalDeviceMemoryProperties_(ctx.phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0xFFFFFFFFu;
}

static bool ReadFileBytes_(const char* path, std::vector<uint8_t>& out)
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

// SPV modules are loaded from explicit exe-relative paths given in the
// CreateInfo ("shaders/vk/...", the CustomBuild output next to the exe),
// matching FpolyVK / ScreenQuadVK.
static VkShaderModule CreateShaderModuleFromFile_(VkContext& ctx, const char* path)
{
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes_(path, bytes))
    {
        LOG_ERROR("VectorDrawVK: failed to read %s", path ? path : "(null)");
        return VK_NULL_HANDLE;
    }
    if ((bytes.size() & 3u) != 0u)
    {
        LOG_ERROR("VectorDrawVK: SPV size not 4-byte aligned: %s", path);
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = bytes.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(bytes.data());
    VkShaderModule mod = VK_NULL_HANDLE;
    if (ctx.vkCreateShaderModule_(ctx.device, &ci, nullptr, &mod) != VK_SUCCESS)
    {
        LOG_ERROR("VectorDrawVK: vkCreateShaderModule failed for %s", path);
        return VK_NULL_HANDLE;
    }
    return mod;
}

static bool CreateHostBuffer_(VkContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                              VkBuffer& outBuf, VkDeviceMemory& outMem, void** outMapped)
{
    outBuf = VK_NULL_HANDLE; outMem = VK_NULL_HANDLE; if (outMapped) *outMapped = nullptr;
    VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (ctx.vkCreateBuffer_(ctx.device, &bi, nullptr, &outBuf) != VK_SUCCESS || !outBuf) return false;

    VkMemoryRequirements mr{};
    ctx.vkGetBufferMemoryRequirements_(ctx.device, outBuf, &mr);
    uint32_t mt = FindMemoryTypeIdx_(ctx, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == 0xFFFFFFFFu) return false;

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = mr.size; ai.memoryTypeIndex = mt;
    if (ctx.vkAllocateMemory_(ctx.device, &ai, nullptr, &outMem) != VK_SUCCESS || !outMem) return false;
    if (ctx.vkBindBufferMemory_(ctx.device, outBuf, outMem, 0) != VK_SUCCESS) return false;
    if (outMapped && ctx.vkMapMemory_(ctx.device, outMem, 0, VK_WHOLE_SIZE, 0, outMapped) != VK_SUCCESS) return false;
    return true;
}

static void DestroyHostBuffer_(VkContext& ctx, VkBuffer& buf, VkDeviceMemory& mem, void** mapped)
{
    if (mapped && *mapped) { ctx.vkUnmapMemory_(ctx.device, mem); *mapped = nullptr; }
    if (buf) { ctx.vkDestroyBuffer_(ctx.device, buf, nullptr); buf = VK_NULL_HANDLE; }
    if (mem) { ctx.vkFreeMemory_(ctx.device, mem, nullptr);    mem = VK_NULL_HANDLE; }
}

static VkPipelineColorBlendAttachmentState MakeBlend_(int mode)
{
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_TRUE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (mode == VKBLEND_MAX)
    {   // GL_MAX: premultiplied coverage fills gaps without summing over the lines.
        cba.colorBlendOp = VK_BLEND_OP_MAX; cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.alphaBlendOp = VK_BLEND_OP_MAX; cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }
    else if (mode == VKBLEND_ADDITIVE)
    {   // glBlendFunc(GL_SRC_ALPHA, GL_ONE) on both color and alpha.
        cba.colorBlendOp = VK_BLEND_OP_ADD; cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.alphaBlendOp = VK_BLEND_OP_ADD; cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }
    else
    {   // Alpha-over with separate (premultiplied) alpha, matching the GL font path
        // and the compositor: correct accumulated alpha for the offscreen RT.
        cba.colorBlendOp = VK_BLEND_OP_ADD; cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp = VK_BLEND_OP_ADD; cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;       cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    return cba;
}

static VkPipeline CreatePipe_(VkContext& ctx, VkPipelineLayout layout, VkFormat colorFmt,
    const char* vsPath, const char* fsPath,
    const VkVertexInputAttributeDescription* attribs, uint32_t attribCount, uint32_t stride,
    int blendMode)
{
    VkShaderModule vs = CreateShaderModuleFromFile_(ctx, vsPath);
    VkShaderModule fs = CreateShaderModuleFromFile_(ctx, fsPath);
    if (!vs || !fs)
    {
        if (vs) ctx.vkDestroyShaderModule_(ctx.device, vs, nullptr);
        if (fs) ctx.vkDestroyShaderModule_(ctx.device, fs, nullptr);
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

    // Single per-instance binding (the quad corners come from gl_VertexIndex).
    VkVertexInputBindingDescription vibd{};
    vibd.binding = 0; vibd.stride = stride; vibd.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1;            vi.pVertexBindingDescriptions = &vibd;
    vi.vertexAttributeDescriptionCount = attribCount; vi.pVertexAttributeDescriptions = attribs;

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba = MakeBlend_(blendMode);
    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;

    VkPipelineRenderingCreateInfo prci{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    prci.colorAttachmentCount = 1; prci.pColorAttachmentFormats = &colorFmt;

    VkGraphicsPipelineCreateInfo gpi{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpi.pNext = &prci;
    gpi.stageCount = 2; gpi.pStages = stages;
    gpi.pVertexInputState = &vi; gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vp; gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms; gpi.pColorBlendState = &cb; gpi.pDynamicState = &ds;
    gpi.layout = layout;

    VkPipeline pipe = VK_NULL_HANDLE;
    VkResult pr = ctx.vkCreateGraphicsPipelines_(ctx.device, ctx.pipelineCache, 1, &gpi, nullptr, &pipe);

    ctx.vkDestroyShaderModule_(ctx.device, vs, nullptr);
    ctx.vkDestroyShaderModule_(ctx.device, fs, nullptr);

    if (pr != VK_SUCCESS)
    {
        LOG_ERROR("VectorDrawVK: vkCreateGraphicsPipelines failed (VkResult=%d)", (int)pr);
        return VK_NULL_HANDLE;
    }
    return pipe;
}

// =============================================================================
// VectorDrawVK
// =============================================================================
bool VectorDrawVK::Init(VkContext& ctx, const VectorDrawVKCreateInfo* ciPtr)
{
    // Idempotent re-Init. CreatePipelines writes a fresh layout + 5
    // pipelines; without this guard a second Init (new game load) leaks the
    // previous set. Mirrors the GL beam_ensure_lines() progLine guard.
    if (m_pipeLayout)
        Shutdown(ctx);

    VectorDrawVKCreateInfo ci = ciPtr ? *ciPtr : VectorDrawVKCreateInfo{};
    m_initialCap = (ci.initialInstanceCapacity > 0) ? ci.initialInstanceCapacity : 4096;
    m_ssaa = (ci.ssaa < 1) ? 1 : ci.ssaa;   // AA feather divisor
    m_flipViewportY = ci.flipViewportY;

    if (!CreatePipelines(ctx, ci))
    {
        Shutdown(ctx);
        return false;
    }
    LOG_INFO("VectorDrawVK: initialized (colorFormat=%d, ssaa=%d)", (int)m_colorFormat, m_ssaa);
    return true;
}

bool VectorDrawVK::CreatePipelines(VkContext& ctx, const VectorDrawVKCreateInfo& ci)
{
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0; pcr.size = sizeof(BeamPushVK);

    VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
    if (ctx.vkCreatePipelineLayout_(ctx.device, &pli, nullptr, &m_pipeLayout) != VK_SUCCESS)
    {
        LOG_ERROR("VectorDrawVK: vkCreatePipelineLayout failed");
        return false;
    }

    m_colorFormat = (ci.colorFormat != VK_FORMAT_UNDEFINED) ? ci.colorFormat : ctx.swapchainFormat;

    const VkVertexInputAttributeDescription lineA[4] = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT,  (uint32_t)offsetof(BeamLine, p0)    },
        { 1, 0, VK_FORMAT_R32G32_SFLOAT,  (uint32_t)offsetof(BeamLine, p1)    },
        { 2, 0, VK_FORMAT_R32_SFLOAT,     (uint32_t)offsetof(BeamLine, half)  },
        { 3, 0, VK_FORMAT_R8G8B8A8_UNORM, (uint32_t)offsetof(BeamLine, color) },
    };
    const VkVertexInputAttributeDescription discA[3] = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT,  (uint32_t)offsetof(BeamJoin, center) },
        { 1, 0, VK_FORMAT_R32_SFLOAT,     (uint32_t)offsetof(BeamJoin, half)   },
        { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, (uint32_t)offsetof(BeamJoin, color)  },
    };
    const VkVertexInputAttributeDescription shotA[3] = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT,  (uint32_t)offsetof(BeamShot, pos)   },
        { 1, 0, VK_FORMAT_R32_SFLOAT,     (uint32_t)offsetof(BeamShot, size)  },
        { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, (uint32_t)offsetof(BeamShot, color) },
    };

    m_pipeLineAdd  = CreatePipe_(ctx, m_pipeLayout, m_colorFormat, ci.lineVertSpv, ci.lineFragSpv, lineA, 4, sizeof(BeamLine), VKBLEND_ADDITIVE);
    m_pipeLineOver = CreatePipe_(ctx, m_pipeLayout, m_colorFormat, ci.lineVertSpv, ci.lineFragSpv, lineA, 4, sizeof(BeamLine), VKBLEND_OVER);
    m_pipeDiscMax  = CreatePipe_(ctx, m_pipeLayout, m_colorFormat, ci.discVertSpv, ci.discFragSpv, discA, 3, sizeof(BeamJoin), VKBLEND_MAX);
    m_pipeDiscOver = CreatePipe_(ctx, m_pipeLayout, m_colorFormat, ci.discVertSpv, ci.discFragSpv, discA, 3, sizeof(BeamJoin), VKBLEND_OVER);
    m_pipeShotAdd  = CreatePipe_(ctx, m_pipeLayout, m_colorFormat, ci.shotVertSpv, ci.shotFragSpv, shotA, 3, sizeof(BeamShot), VKBLEND_ADDITIVE);

    if (!m_pipeLineAdd || !m_pipeLineOver || !m_pipeDiscMax || !m_pipeDiscOver || !m_pipeShotAdd)
    {
        LOG_ERROR("VectorDrawVK: one or more pipelines failed to build");
        return false;
    }
    return true;
}

// Growth RETIRES the old buffer to this slot's stale list instead of
// destroying it immediately -- draws recorded earlier this frame (or by the
// slot's still-in-flight previous submission before OnFrameBegin ran) may
// still reference it. OnFrameBegin drains the list once the slot's fence
// wait has proven the GPU done. Same pattern as FpolyVK::EnsureVBOCapacity.
bool VectorDrawVK::EnsureBuffer(VkContext& ctx, int batch, uint32_t fi, VkDeviceSize neededBytes)
{
    if (m_buf[batch][fi] && m_cap[batch][fi] >= neededBytes) return true;

    // Start from the configured instance-capacity hint (assume a ~32-byte instance),
    // then double until the request fits. Stabilizes after the first busy frame.
    VkDeviceSize cap = m_cap[batch][fi] ? m_cap[batch][fi] : (VkDeviceSize)m_initialCap * 32;
    if (cap < 4096) cap = 4096;
    while (cap < neededBytes) cap *= 2;

    // Retire the old buffer (do NOT destroy: earlier recorded draws bound it).
    if (m_buf[batch][fi] || m_mem[batch][fi] || m_mapped[batch][fi])
    {
        StaleBuffer sb{ m_buf[batch][fi], m_mem[batch][fi], m_mapped[batch][fi] };
        m_stale[fi].push_back(sb);
        m_buf[batch][fi] = VK_NULL_HANDLE;
        m_mem[batch][fi] = VK_NULL_HANDLE;
        m_mapped[batch][fi] = nullptr;
    }

    if (!CreateHostBuffer_(ctx, cap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           m_buf[batch][fi], m_mem[batch][fi], &m_mapped[batch][fi]))
    {
        m_cap[batch][fi] = 0;
        LOG_ERROR("VectorDrawVK: instance buffer alloc failed (batch=%d frame=%u)", batch, fi);
        return false;
    }
    m_cap[batch][fi] = cap;
    return true;
}

void VectorDrawVK::DrainStaleBuffers(VkContext& ctx, uint32_t fi)
{
    for (StaleBuffer& sb : m_stale[fi])
        DestroyHostBuffer_(ctx, sb.buf, sb.mem, &sb.mapped);
    m_stale[fi].clear();
}

// Once-per-frame slot reset. Only call after VK_BeginFrame's fence
// wait on this slot -- that is the proof the retired buffers (and the data
// regions below the write heads) are no longer referenced by the GPU.
void VectorDrawVK::OnFrameBegin(VkContext& ctx, uint32_t frameIndex)
{
    if (frameIndex >= VkContext::kFramesInFlight) return;
    DrainStaleBuffers(ctx, frameIndex);
    for (int b = 0; b < BATCH_COUNT; ++b)
        m_head[b][frameIndex] = 0;
}

void VectorDrawVK::Record(VkContext& ctx, VkCommandBuffer cmd, uint32_t fi,
                          const float proj[16], bool additive,
                          uint32_t targetWidth, uint32_t targetHeight)
{
    if (cmd == VK_NULL_HANDLE || fi >= VkContext::kFramesInFlight || !m_pipeLayout) return;

    // Viewport/scissor for the active framebuffer (the caller's RenderTarget
    // when inside an RT pass; the swapchain otherwise). The viewport is
    // Y-flipped (negative height) so the SAME GL-style Y-up ortho passed in
    // 'proj' produces the same orientation Vulkan-side -- without this every
    // beam renders vertically mirrored. Same compensation as sprite/fpoly/
    // debug_draw/fonts.
    const uint32_t tw = (targetWidth  > 0) ? targetWidth  : ctx.swapchainExtent.width;
    const uint32_t th = (targetHeight > 0) ? targetHeight : ctx.swapchainExtent.height;
    if (tw == 0 || th == 0) return;

    VkViewport vp{};
    vp.x = 0.0f;
    vp.width = (float)tw;
    if (m_flipViewportY)
    {
        vp.y = (float)th;
        vp.height = -(float)th;
    }
    else
    {
        vp.y = 0.0f;
        vp.height = (float)th;
    }
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    VkRect2D sc{};
    sc.offset = { 0, 0 };
    sc.extent = { tw, th };

    ctx.vkCmdSetViewport_(cmd, 0, 1, &vp);
    ctx.vkCmdSetScissor_(cmd, 0, 1, &sc);

    // Lines + shots come from the shared CPU builders; joins are rebuilt from the
    // lines exactly as beam_draw_all() does (endcap 1.0, corner = config.corner_strength).
    const std::vector<BeamLine>& srcLines = beam_get_lines();
    const std::vector<BeamShot>& shots    = beam_get_shots();
    std::vector<BeamJoin> joins;
    beam_build_caps(srcLines.data(), (int)srcLines.size(), 1.0f, config.corner_strength, joins);

    if (srcLines.empty() && joins.empty() && shots.empty()) return;

    // B/W (alpha-over) needs the painter's sort so brighter beams occlude darker.
    // Additive / MAX are order-independent, so skip the copy + sort there.
    std::vector<BeamLine> sortedLines;
    const std::vector<BeamLine>* lines = &srcLines;
    if (!additive)
    {
        sortedLines = srcLines;
        std::sort(sortedLines.begin(), sortedLines.end(),
            [](const BeamLine& a, const BeamLine& b) { return (uint32_t)a.color < (uint32_t)b.color; });
        std::sort(joins.begin(), joins.end(),
            [](const BeamJoin& a, const BeamJoin& b) { return (uint32_t)a.color < (uint32_t)b.color; });
        lines = &sortedLines;
    }

    BeamPushVK pc{};
    memcpy(pc.proj, proj, sizeof(pc.proj));
    // AA feather divided by the supersample factor, mirroring the GL
    // beam_draw_all (g_uAA = config.line_smoothing / g_ssaa). Without the
    // divide the feather is ssaa-times too wide on a supersampled RT.
    pc.uAA = ((config.line_smoothing > 0.0001f) ? config.line_smoothing : 0.0001f)
             / (float)((m_ssaa < 1) ? 1 : m_ssaa);
    pc.uStrength = 1.0f;                     // disc radius already baked per-instance
    pc.uPremult  = additive ? 1.0f : 0.0f;   // disc: premultiplied coverage for the MAX path
    pc.uCorePower = 6.0f; pc.uBloomPower = 2.5f; pc.uBloomIntensity = 0.3f; pc.uOverdrive = 1.5f;

    const VkShaderStageFlags pcStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkDeviceSize zero = 0;

    // Append at this slot's write head and draw from the base as
    // firstInstance (instance-rate vertex fetch offsets by firstInstance *
    // stride), so a second Record in the same frame cannot overwrite data
    // the first Record's draws still reference.
    auto drawBatch = [&](int batch, const void* data, uint32_t count, uint32_t stride, VkPipeline pipe)
    {
        if (count == 0 || !pipe) return;
        const VkDeviceSize bytes = (VkDeviceSize)count * stride;
        const VkDeviceSize base  = m_head[batch][fi];
        if (!EnsureBuffer(ctx, batch, fi, base + bytes)) return;
        memcpy((uint8_t*)m_mapped[batch][fi] + base, data, (size_t)bytes);
        m_head[batch][fi] = base + bytes;
        ctx.vkCmdBindPipeline_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        ctx.vkCmdPushConstants_(cmd, m_pipeLayout, pcStages, 0, sizeof(pc), &pc);
        ctx.vkCmdBindVertexBuffers_(cmd, 0, 1, &m_buf[batch][fi], &zero);
        ctx.vkCmdDraw_(cmd, 4, count, 0, (uint32_t)(base / stride));   // 4-vert strip quad
    };

    drawBatch(BATCH_LINE, lines->data(),  (uint32_t)lines->size(),  sizeof(BeamLine),
              additive ? m_pipeLineAdd : m_pipeLineOver);
    drawBatch(BATCH_JOIN, joins.data(),   (uint32_t)joins.size(),   sizeof(BeamJoin),
              additive ? m_pipeDiscMax : m_pipeDiscOver);
    drawBatch(BATCH_SHOT, shots.data(),   (uint32_t)shots.size(),   sizeof(BeamShot),
              m_pipeShotAdd);
}

void VectorDrawVK::Shutdown(VkContext& ctx)
{
    VkPipeline* pipes[] = { &m_pipeLineAdd, &m_pipeLineOver, &m_pipeDiscMax, &m_pipeDiscOver, &m_pipeShotAdd };
    for (VkPipeline* p : pipes)
        if (*p) { ctx.vkDestroyPipeline_(ctx.device, *p, nullptr); *p = VK_NULL_HANDLE; }

    if (m_pipeLayout) { ctx.vkDestroyPipelineLayout_(ctx.device, m_pipeLayout, nullptr); m_pipeLayout = VK_NULL_HANDLE; }

    // Destroy every slot's live buffers AND drain every slot's stale list.
    // The caller device-waits-idle before Shutdown (vkchain_shutdown), so no
    // in-flight submission can still be reading any of these.
    for (int b = 0; b < BATCH_COUNT; ++b)
        for (uint32_t fi = 0; fi < VkContext::kFramesInFlight; ++fi)
        {
            DestroyHostBuffer_(ctx, m_buf[b][fi], m_mem[b][fi], &m_mapped[b][fi]);
            m_cap[b][fi] = 0;
            m_head[b][fi] = 0;
        }
    for (uint32_t fi = 0; fi < VkContext::kFramesInFlight; ++fi)
        DrainStaleBuffers(ctx, fi);
    m_colorFormat = VK_FORMAT_UNDEFINED;
}
