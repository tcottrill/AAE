// -----------------------------------------------------------------------------
// gpu_profiler_vk.h - per-section GPU timing for the Vulkan chain.
//
// WHY: "the VK chain is slower than the GL chain" is not actionable until you
// know WHICH pass costs what. This wraps each pass of the frame in a
// VkQueryPool timestamp pair and logs a per-section summary to systemlog.txt,
// so a performance change can be measured one variable at a time.
//
// GATE: [main] vk_profile (default 0). At 0 no query pool is created, not one
// timestamp is recorded, and every entry point below early-outs on a single
// bool test - the recorded command stream is byte-identical to a build with
// no profiler in it.
//
// USE (call sites stay one line):
//     GPU_ZONE("beam");                 // scoped: closes at end of block
//     GPU_ZONE_BEGIN("glow_down");      // explicit pair, for non-scoped regions
//     GPU_ZONE_END();
//
// Sections are FLAT by convention: GpuProf::BeginFrame opens the implicit
// "frame" section (depth 0) and everything else nests exactly one level inside
// it (depth 1). The summary reports each depth-1 section's share of "frame"
// plus an "(unaccounted)" remainder, so the numbers tile the frame and can be
// read at a glance. Nesting deeper is tolerated (it will not corrupt anything)
// but such a section is folded into its parent's remainder, not reported.
//
// A section that does not run on a given frame simply does not appear that
// frame - it is never reported as a free 0.000 ms.
//
// COMMAND BUFFER: the whole chain records into one command buffer per frame
// (g_vk.cmdBuffers[g_vk.frameIndex]), which BeginFrame latches. That is why
// Begin/End take only a name and the deep call sites (VectorPostVK::RecordPost
// and friends) need no ctx/cmd plumbing.
//
// WHY THE NUMBERS ARE TRUSTWORTHY: the chain RECORDS the whole frame and only
// then submits it once (vkchain_swap_buffers -> VK_EndFrame). The GPU
// therefore executes the entire buffer back to back with no CPU interleaving,
// so a section's delta is GPU work, never GPU idle waiting on the recorder.
// The one thing these numbers do NOT include is the present/vsync wait, which
// happens after the last timestamp - so the "frame" total is a GPU cost, not a
// frame time, and the fps it implies is a ceiling.
//
// ASCII-only comments.
// -----------------------------------------------------------------------------

#pragma once

#include "sys_vk.h"

#include <stdint.h>

namespace GpuProf
{
    // Enable flag, read directly by the hot-path guards below. Never write it
    // from outside gpu_profiler_vk.cpp.
    extern bool g_enabled;

    inline bool Enabled() { return g_enabled; }

    // Creates the query pool and latches timestampPeriod / timestampValidBits.
    // `enable` is config.vk_profile != 0. Returns false (and leaves the
    // profiler inert) when disabled, when the query entry points are missing,
    // or when the graphics queue family reports timestampValidBits == 0 - a
    // device that cannot timestamp on this queue is logged and skipped rather
    // than left to produce nonsense.
    bool Init(VkContext& ctx, bool enable);
    void Shutdown(VkContext& ctx);

    // Per-frame bookkeeping, called from vkchain_set_render immediately after
    // VK_BeginFrame succeeded. In order:
    //   1. Collect - reads back the results this frame SLOT wrote on its
    //      previous use. VK_BeginFrame has just waited inFlight[frameIndex],
    //      which proves that submission finished on the GPU; with
    //      kFramesInFlight == 2 that is frame N-2.
    //   2. Reset - vkCmdResetQueryPool over this slot's range. That command is
    //      ILLEGAL inside a dynamic-rendering pass and VK_BeginFrame leaves the
    //      swapchain pass OPEN, so the reset is bracketed by
    //      VK_SuspendFramePass / VK_ResumeFramePass. Resume re-opens with
    //      LOAD_OP_LOAD and restores the default viewport/scissor, so the state
    //      on return is exactly what VK_BeginFrame left behind.
    //   3. Opens the "frame" section.
    void BeginFrame(VkContext& ctx, VkCommandBuffer cmd,
                    uint32_t frameIndex, uint32_t imageIndex);

    // Closes the "frame" section. Call at the tail of vkchain_render, while
    // the swapchain pass is still open (vkCmdWriteTimestamp is legal in a pass).
    void EndFrame();

    void Begin(const char* name);
    void End();
}

// RAII scope guard behind GPU_ZONE. Both bodies collapse to a single bool test
// when the profiler is off.
class GpuZoneVK
{
public:
    explicit GpuZoneVK(const char* name) { GpuProf::Begin(name); }
    ~GpuZoneVK() { GpuProf::End(); }
private:
    GpuZoneVK(const GpuZoneVK&) = delete;
    GpuZoneVK& operator=(const GpuZoneVK&) = delete;
};

#define GPU_ZONE_CAT2_(a, b) a##b
#define GPU_ZONE_CAT_(a, b)  GPU_ZONE_CAT2_(a, b)

#define GPU_ZONE(name)       GpuZoneVK GPU_ZONE_CAT_(aae_gpuzone_, __LINE__)(name)
#define GPU_ZONE_BEGIN(name) GpuProf::Begin(name)
#define GPU_ZONE_END()       GpuProf::End()
