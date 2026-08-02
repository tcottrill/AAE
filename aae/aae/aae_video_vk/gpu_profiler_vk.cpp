// -----------------------------------------------------------------------------
// gpu_profiler_vk.cpp - per-section GPU timing for the Vulkan chain.
// See gpu_profiler_vk.h for the contract and the call-site macros.
// ASCII-only comments.
// -----------------------------------------------------------------------------

#include "gpu_profiler_vk.h"
#include "sys_log.h"

#include <string.h>
#include <vector>

namespace
{
    // Sizing. Worst case today is the vector post path: frame + beam + shots +
    // beam_mips + trail + glow_down + glow_blur + framebuild + composite +
    // rot_blit + ui = 11 sections. 24 leaves headroom for future passes without
    // making the pool big enough to matter (96 timestamps total).
    constexpr uint32_t kMaxSections = 24;
    constexpr uint32_t kQueriesPerSlot = kMaxSections * 2;
    constexpr uint32_t kMaxNames = 24;

    // Frames between summary lines. 120 is ~2 s at 60 fps and ~0.2 s at the
    // rates this chain is being tuned for; either way the log tail stays short.
    constexpr uint32_t kSummaryFrames = 120;

    struct Rec
    {
        uint8_t  name;    // index into s_names
        uint8_t  depth;   // 0 = the implicit "frame" section, 1 = a pass
        uint16_t q0;      // begin query, POOL-ABSOLUTE
        uint16_t q1;      // end query, POOL-ABSOLUTE
    };

    struct Slot
    {
        Rec      recs[kMaxSections];
        uint32_t recCount = 0;
        uint32_t used = 0;       // queries consumed this frame (from base)
        bool     pending = false; // a frame was recorded into this slot and not yet collected
    };

    struct Acc
    {
        double   totalMs = 0.0;
        uint32_t frames = 0;      // measured frames this section appeared in
        uint32_t lastFrame = 0;   // dedupe for `frames` when a name repeats in one frame
        uint8_t  depth = 1;
    };

    VkQueryPool s_pool = VK_NULL_HANDLE;
    double      s_period = 1.0;        // nanoseconds per tick (timestampPeriod)
    uint64_t    s_tsMask = ~0ull;      // timestampValidBits mask

    VkContext*      s_ctx = nullptr;
    VkCommandBuffer s_cmd = VK_NULL_HANDLE;
    uint32_t        s_slot = 0;
    bool            s_frameOpen = false;

    Slot s_slots[VkContext::kFramesInFlight];

    const char* s_names[kMaxNames] = {};
    uint32_t    s_nameCount = 0;

    Acc      s_acc[kMaxNames];
    uint32_t s_measured = 0;      // frames collected since the last summary
    double   s_frameTotalMs = 0.0;

    // Open-section stack (indices into the current slot's recs).
    uint16_t s_stack[kMaxSections];
    uint32_t s_depth = 0;

    // Begins that could not be recorded (slot full). Their matching Ends must
    // NOT pop the stack or they would close an outer section instead. Because
    // sections close LIFO and capacity only ever shrinks within a frame, the
    // next `s_dropped` Ends are exactly the dropped Begins' partners, so a
    // plain counter pairs them correctly.
    uint32_t s_dropped = 0;

    bool s_overflowLogged = false;

    uint8_t InternName(const char* name)
    {
        if (!name) name = "?";
        for (uint32_t i = 0; i < s_nameCount; ++i)
        {
            if (strcmp(s_names[i], name) == 0)
                return (uint8_t)i;
        }
        if (s_nameCount >= kMaxNames)
            return (uint8_t)(kMaxNames - 1);   // fold into the last bucket
        s_names[s_nameCount] = name;
        s_acc[s_nameCount] = Acc{};
        return (uint8_t)s_nameCount++;
    }

    void ResetAccumulators()
    {
        for (uint32_t i = 0; i < kMaxNames; ++i)
        {
            s_acc[i].totalMs = 0.0;
            s_acc[i].frames = 0;
            s_acc[i].lastFrame = 0;
        }
        s_measured = 0;
        s_frameTotalMs = 0.0;
    }

    void LogSummary()
    {
        if (s_measured == 0 || s_frameTotalMs <= 0.0)
        {
            ResetAccumulators();
            return;
        }

        const double frameAvg = s_frameTotalMs / (double)s_measured;
        const double fps = (frameAvg > 0.0) ? (1000.0 / frameAvg) : 0.0;

        LOG_INFO("[vkprof] %u frames, GPU frame %.3f ms avg (%.0f fps ceiling)",
                 s_measured, frameAvg, fps);

        // Biggest cost first - the whole point is that the answer is the first
        // line you read.
        uint32_t order[kMaxNames];
        uint32_t n = 0;
        double   childTotal = 0.0;
        for (uint32_t i = 0; i < s_nameCount; ++i)
        {
            if (s_acc[i].depth != 1 || s_acc[i].frames == 0)
                continue;
            order[n++] = i;
            childTotal += s_acc[i].totalMs;
        }
        for (uint32_t a = 0; a + 1 < n; ++a)
        {
            for (uint32_t b = a + 1; b < n; ++b)
            {
                if (s_acc[order[b]].totalMs > s_acc[order[a]].totalMs)
                {
                    const uint32_t t = order[a]; order[a] = order[b]; order[b] = t;
                }
            }
        }

        for (uint32_t k = 0; k < n; ++k)
        {
            const Acc& a = s_acc[order[k]];
            // avg = cost on the frames where it actually ran (never averaged
            // down by frames it was skipped on); pct = its share of ALL the
            // measured GPU time, so the percentages tile the frame honestly
            // whether or not a section runs every frame.
            const double avg = a.totalMs / (double)a.frames;
            const double pct = 100.0 * a.totalMs / s_frameTotalMs;
            LOG_INFO("[vkprof]   %-12s %7.3f ms  %5.1f%%  x%u",
                     s_names[order[k]], avg, pct, a.frames);
        }

        const double unacc = s_frameTotalMs - childTotal;
        LOG_INFO("[vkprof]   %-12s %7.3f ms  %5.1f%%",
                 "(rest)", unacc / (double)s_measured,
                 100.0 * unacc / s_frameTotalMs);

        ResetAccumulators();
    }

    // Reads back the results the given slot wrote on its PREVIOUS use.
    //
    // SAFETY: the only caller is BeginFrame, which runs after VK_BeginFrame
    // returned - and VK_BeginFrame's first act is
    // vkWaitForFences(inFlight[frameIndex], VK_TRUE, UINT64_MAX). That fence is
    // signalled by the submit that recorded these very queries, so the GPU is
    // provably finished with them. No WAIT bit is used here (that would stall
    // the CPU on the queries themselves); VK_NOT_READY is treated as "this slot
    // never actually got submitted" and the frame is dropped.
    void Collect(VkContext& ctx, uint32_t slotIndex)
    {
        Slot& s = s_slots[slotIndex];
        if (!s.pending)
            return;
        s.pending = false;

        if (s.used == 0 || s.recCount == 0)
            return;

        uint64_t ts[kQueriesPerSlot];
        const uint32_t base = slotIndex * kQueriesPerSlot;
        const VkResult r = ctx.vkGetQueryPoolResults_(ctx.device, s_pool,
            base, s.used, sizeof(uint64_t) * s.used, ts, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT);
        if (r != VK_SUCCESS)
            return;                 // VK_NOT_READY (never submitted) - drop it

        ++s_measured;
        const uint32_t frameTag = s_measured;

        for (uint32_t i = 0; i < s.recCount; ++i)
        {
            const Rec& rc = s.recs[i];
            if (rc.q1 == 0xFFFF)
                continue;           // never closed (defensive; EndFrame closes strays)

            // Mask to timestampValidBits FIRST, then subtract in unsigned
            // arithmetic and mask again: wraparound of the valid range comes
            // out correct instead of as an absurd delta.
            const uint64_t t0 = ts[rc.q0 - base] & s_tsMask;
            const uint64_t t1 = ts[rc.q1 - base] & s_tsMask;
            const uint64_t ticks = (t1 - t0) & s_tsMask;

            const double ms = (double)ticks * s_period / 1000000.0;

            if (rc.depth == 0)
            {
                s_frameTotalMs += ms;
                continue;
            }

            Acc& a = s_acc[rc.name];
            a.depth = rc.depth;
            a.totalMs += ms;
            if (a.lastFrame != frameTag)
            {
                a.lastFrame = frameTag;
                ++a.frames;
            }
        }

        if (s_measured >= kSummaryFrames)
            LogSummary();
    }
}

namespace GpuProf
{
    bool g_enabled = false;
}

bool GpuProf::Init(VkContext& ctx, bool enable)
{
    g_enabled = false;
    s_pool = VK_NULL_HANDLE;
    s_ctx = nullptr;
    s_cmd = VK_NULL_HANDLE;
    s_frameOpen = false;
    s_depth = 0;
    s_dropped = 0;
    s_nameCount = 0;
    s_overflowLogged = false;
    for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
        s_slots[i] = Slot{};
    ResetAccumulators();

    if (!enable)
        return false;

    if (!ctx.device || !ctx.vkCreateQueryPool_ || !ctx.vkDestroyQueryPool_ ||
        !ctx.vkCmdWriteTimestamp_ || !ctx.vkCmdResetQueryPool_ ||
        !ctx.vkGetQueryPoolResults_)
    {
        LOG_INFO("vk_profile: driver does not expose the timestamp query entry points; profiler off");
        return false;
    }

    // timestampPeriod is NANOseconds per tick and is emphatically NOT 1 on
    // every vendor (AMD reports 40-ish, Intel 83-ish, NVIDIA 1). Latch it.
    if (!ctx.vkGetPhysicalDeviceProperties_ || !ctx.phys)
    {
        LOG_INFO("vk_profile: cannot query device properties; profiler off");
        return false;
    }
    VkPhysicalDeviceProperties props{};
    ctx.vkGetPhysicalDeviceProperties_(ctx.phys, &props);
    s_period = (double)props.limits.timestampPeriod;
    if (s_period <= 0.0)
    {
        LOG_INFO("vk_profile: timestampPeriod is %f; profiler off", s_period);
        return false;
    }

    // timestampValidBits is PER QUEUE FAMILY and may legally be 0, which means
    // this queue cannot timestamp at all. Log and stand down rather than
    // publishing garbage numbers.
    uint32_t validBits = 0;
    if (ctx.vkGetPhysicalDeviceQueueFamilyProperties_)
    {
        uint32_t famCount = 0;
        ctx.vkGetPhysicalDeviceQueueFamilyProperties_(ctx.phys, &famCount, nullptr);
        if (famCount > 0 && ctx.gfxQueueFamily < famCount)
        {
            std::vector<VkQueueFamilyProperties> fams(famCount);
            ctx.vkGetPhysicalDeviceQueueFamilyProperties_(ctx.phys, &famCount, fams.data());
            validBits = fams[ctx.gfxQueueFamily].timestampValidBits;
        }
    }
    if (validBits == 0)
    {
        LOG_INFO("vk_profile: graphics queue family %u reports timestampValidBits=0 "
                 "(this queue cannot timestamp); profiler off", ctx.gfxQueueFamily);
        return false;
    }
    s_tsMask = (validBits >= 64) ? ~0ull : ((1ull << validBits) - 1ull);

    VkQueryPoolCreateInfo qi{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kQueriesPerSlot * VkContext::kFramesInFlight;

    if (ctx.vkCreateQueryPool_(ctx.device, &qi, nullptr, &s_pool) != VK_SUCCESS ||
        s_pool == VK_NULL_HANDLE)
    {
        LOG_ERROR("vk_profile: vkCreateQueryPool failed; profiler off");
        s_pool = VK_NULL_HANDLE;
        return false;
    }

    g_enabled = true;
    LOG_INFO("vk_profile: GPU section profiler ON (timestampPeriod %.3f ns, "
             "%u valid bits, %u queries, summary every %u frames)",
             s_period, validBits, qi.queryCount, kSummaryFrames);
    return true;
}

void GpuProf::Shutdown(VkContext& ctx)
{
    if (s_pool != VK_NULL_HANDLE && ctx.vkDestroyQueryPool_ && ctx.device)
        ctx.vkDestroyQueryPool_(ctx.device, s_pool, nullptr);
    s_pool = VK_NULL_HANDLE;
    g_enabled = false;
    s_ctx = nullptr;
    s_cmd = VK_NULL_HANDLE;
    s_frameOpen = false;
    s_depth = 0;
}

void GpuProf::BeginFrame(VkContext& ctx, VkCommandBuffer cmd,
                         uint32_t frameIndex, uint32_t imageIndex)
{
    if (!g_enabled)
        return;
    if (cmd == VK_NULL_HANDLE || frameIndex >= VkContext::kFramesInFlight)
        return;

    s_ctx = &ctx;
    s_cmd = cmd;
    s_slot = frameIndex;
    s_depth = 0;
    s_dropped = 0;

    // 1. Read back what this slot wrote last time round. See Collect's header
    //    for why the data is provably complete.
    Collect(ctx, frameIndex);

    // 2. Reset this slot's query range. vkCmdResetQueryPool may NOT appear
    //    inside a dynamic-rendering pass and VK_BeginFrame left the swapchain
    //    pass open, so close it, reset, re-open. The re-open is LOAD_OP_LOAD
    //    over the just-cleared attachment and restores the default
    //    viewport/scissor, so the state handed back to the chain is exactly
    //    what VK_BeginFrame produced. All of this is inside the vk_profile
    //    branch: at vk_profile=0 not one of these commands is recorded.
    Slot& s = s_slots[frameIndex];
    s.recCount = 0;
    s.used = 0;
    s.pending = true;

    VK_SuspendFramePass(ctx, cmd);
    ctx.vkCmdResetQueryPool_(cmd, s_pool, frameIndex * kQueriesPerSlot, kQueriesPerSlot);
    VK_ResumeFramePass(ctx, cmd, imageIndex);

    // 3. The frame section itself (depth 0).
    s_frameOpen = true;
    Begin("frame");
}

void GpuProf::EndFrame()
{
    if (!g_enabled || !s_frameOpen)
        return;
    // Close any section a call site left open (an early return past a
    // GPU_ZONE_END, say). Forgiving by design: a stray open section must not be
    // able to corrupt the slot's records. The frame is being torn down, so any
    // outstanding drop bookkeeping goes with it.
    s_dropped = 0;
    while (s_depth > 1)
        End();
    End();                  // the "frame" section
    s_frameOpen = false;
    s_cmd = VK_NULL_HANDLE;
}

void GpuProf::Begin(const char* name)
{
    if (!g_enabled || s_cmd == VK_NULL_HANDLE || !s_ctx)
        return;

    Slot& s = s_slots[s_slot];
    if (s.recCount >= kMaxSections || s.used + 2 > kQueriesPerSlot ||
        s_depth >= kMaxSections)
    {
        if (!s_overflowLogged)
        {
            s_overflowLogged = true;
            LOG_ERROR("vk_profile: more than %u sections in one frame; extras dropped",
                      kMaxSections);
        }
        ++s_dropped;
        return;
    }

    const uint32_t base = s_slot * kQueriesPerSlot;
    const uint32_t qi = base + s.used++;

    Rec& rc = s.recs[s.recCount];
    rc.name = InternName(name);
    rc.depth = (uint8_t)(s_depth > 255 ? 255 : s_depth);
    rc.q0 = (uint16_t)qi;
    rc.q1 = 0xFFFF;

    s_stack[s_depth++] = (uint16_t)s.recCount++;

    // BOTTOM_OF_PIPE at BOTH ends, deliberately. TOP_OF_PIPE on the opening
    // marker fires as soon as the command is parsed, while the previous
    // section's work is still draining, which double-counts that drain into
    // this section. Bottom/bottom makes each delta "time between the previous
    // marker's work completing and this marker's work completing", so adjacent
    // sections tile the frame with no overlap and their sum is comparable
    // against the "frame" total.
    s_ctx->vkCmdWriteTimestamp_(s_cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                s_pool, qi);
}

void GpuProf::End()
{
    if (!g_enabled || s_cmd == VK_NULL_HANDLE || !s_ctx)
        return;
    if (s_dropped > 0) { --s_dropped; return; }   // partner of a dropped Begin
    if (s_depth == 0)
        return;

    Slot& s = s_slots[s_slot];
    const uint16_t idx = s_stack[--s_depth];
    if (idx >= s.recCount)
        return;

    if (s.used + 1 > kQueriesPerSlot)
        return;                     // Begin already logged the overflow

    const uint32_t base = s_slot * kQueriesPerSlot;
    const uint32_t qi = base + s.used++;
    s.recs[idx].q1 = (uint16_t)qi;

    s_ctx->vkCmdWriteTimestamp_(s_cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                s_pool, qi);
}
