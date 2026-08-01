// ===========================================================================
// vulkan_renderer.cpp - Vulkan chain orchestration (Phase 4a Plan 2).
//
// Owns the VkContext and maps the dispatch entry points onto the sys_vk
// frame loop (spec sec. 3.4):
//   vkchain_set_render   -> VK_BeginFrame (acquire, open pass, clear)
//   vkchain_render       -> record draws (nothing yet; Plans 3-6 fill this in)
//   vkchain_swap_buffers -> VK_EndFrame (submit + present)
// A failed begin (resize, minimize, OUT_OF_DATE) recreates the swapchain and
// skips the rest of that frame; s_frameOpen keeps end-of-frame honest.
// ===========================================================================
#include "vulkan_renderer.h"
#include "sys_log.h"
#include "sys_vk.h"
#include "sys_window.h"
#include "config.h"
#include "emu_vector_draw.h"   // cache_clear - backend-neutral, no GL headers

static VkContext g_vk;
static bool      s_initialized = false;
static bool      s_frameOpen = false;
static uint32_t  s_imageIndex = 0;

// Set when a swapchain recreate attempt fails (window minimized, mid-drag,
// or any other transient reason CreateSwapchain can fail for). vkchain_set_
// render runs once per emulator tick regardless of window state (audio keeps
// running while minimized), and CreateSwapchain logs a line every time it is
// called with a zero extent, so retrying it every tick would spam
// systemlog.txt at frame rate. While this flag is set, vkchain_set_render
// polls GetDrawableSize roughly every 2 seconds (s_deferRetryTick) before
// attempting a recreate: a genuinely minimized window reports zero area, so
// the recreate call (and its log line) is skipped and the loop stays silent;
// a window with real area is retried, self-healing without needing an
// explicit resize event. A resize notification (vkchain_on_window_resize,
// fired by WM_SIZE when the window is NOT minimized) also retries
// immediately, so a drag-resize recovers on the next frame instead of
// waiting out the timer.
static bool     s_deferredZeroExtent = false;
static uint32_t s_deferRetryTick = 0;

// Shared "recreate or defer" path used by every call site that can hit a
// swapchain recreate failure outside vkchain_set_render's own retry timer
// above (VK_EndFrame's OUT_OF_DATE recovery, a vsync toggle, and a window
// resize notification). Routing all of them through the same latch means a
// failed recreate anywhere leaves s_deferredZeroExtent set, so the very next
// vkchain_set_render call goes straight into the polling retry instead of
// calling VK_BeginFrame again and logging a stray "ctx.swapchain is NULL"
// error before the retry timer would have caught it.
static void RecreateSwapchainOrDefer(void)
{
	if (VK_RecreateSwapchain(g_vk))
	{
		if (s_deferredZeroExtent)
			LOG_INFO("vulkan_renderer: deferred swapchain recreated, resuming frames");
		s_deferredZeroExtent = false;
		s_deferRetryTick = 0;
		return;
	}

	// Honest wording: a recreate can fail for reasons other than a
	// zero-area surface (device-lost, out-of-memory, etc.), not just a
	// minimized window, so this does not claim a specific cause.
	if (!s_deferredZeroExtent)
	{
		s_deferredZeroExtent = true;
		s_deferRetryTick = 0;
		LOG_INFO("vulkan_renderer: swapchain recreate failed; deferring (retry ~2s or on resize)");
	}
}

int vkchain_init(void)
{
	if (s_initialized)
		return 1;   // re-entrant like glchain_init: run_game calls per load

	IPresentSurface* present = GetSystemWindow().Presentation();
	if (!present)
	{
		LOG_ERROR("vkchain_init: no presentation surface (headless backend?)");
		return 0;
	}

	const bool validation = (config.vk_validation != 0);
	if (!VK_Init(g_vk, *present, validation, /*vsync=*/true))
	{
		LOG_ERROR("vkchain_init: VK_Init failed");
		VK_Shutdown(g_vk);
		return 0;
	}

	s_initialized = true;
	s_deferredZeroExtent = false;
	s_deferRetryTick = 0;
	LOG_INFO("vkchain_init: Vulkan chain online (validation=%d)", validation ? 1 : 0);
	return 1;
}

void vkchain_shutdown(void)
{
	if (!s_initialized)
		return;
	VK_Shutdown(g_vk);
	s_initialized = false;
	s_frameOpen = false;
	s_deferredZeroExtent = false;
	s_deferRetryTick = 0;
}

void vkchain_set_render(void)
{
	if (!s_initialized || s_frameOpen)
		return;

	if (s_deferredZeroExtent)
	{
		if (++s_deferRetryTick < 120)   // retry roughly every 2 s at 60 fps
			return;
		s_deferRetryTick = 0;

		// Poll drawable size before attempting a recreate: a genuinely
		// minimized window reports zero here, so skip the recreate call
		// (and CreateSwapchain's internal log line) and stay silent. Only
		// retry the recreate once the surface has real area again.
		int dw = 0, dh = 0;
		if (g_vk.present)
			g_vk.present->GetDrawableSize(&dw, &dh);
		if (dw <= 0 || dh <= 0)
			return;                     // genuinely minimized: silent, no log

		RecreateSwapchainOrDefer();
		return;
	}

	if (!VK_BeginFrame(g_vk, s_imageIndex))
	{
		RecreateSwapchainOrDefer();
		return;             // skip this frame; next tick re-acquires
	}
	s_frameOpen = true;
}

void vkchain_render(void)
{
	// Plan 2: the frame pass opened by VK_BeginFrame clears to the gate
	// color; there is nothing to record yet. Raster (Plan 3), post/artwork
	// (Plan 4), vector (Plan 5) and GUI (Plan 6) record here.

	// Drain the emu-side beam queue so vector games do not grow unbounded;
	// Plan 5's VectorDrawVK consumes this queue instead. cache_clear is pure
	// CPU (clears the beam line/join/shot lists via beam_clear plus the
	// legacy textured-shot list, which add_tex also fills every frame).
	cache_clear();
}

void vkchain_swap_buffers(void)
{
	if (!s_initialized || !s_frameOpen)
		return;
	s_frameOpen = false;
	if (!VK_EndFrame(g_vk, s_imageIndex))
		RecreateSwapchainOrDefer();
}

void vkchain_set_vsync(bool enabled)
{
	if (!s_initialized)
		return;
	if (g_vk.vsync == enabled)
		return;
	g_vk.vsync = enabled;
	RecreateSwapchainOrDefer();   // waits device idle internally
}

void vkchain_on_window_resize(int newW, int newH)
{
	(void)newW; (void)newH;
	if (!s_initialized)
		return;

	// winmain.cpp only calls emulator_on_window_resize (and therefore this
	// function) on WM_SIZE when the window is NOT minimized, so this is a
	// fast-path retry: it does not wait for s_deferRetryTick's ~2s poll.
	RecreateSwapchainOrDefer();
}

// --- Plans 3-6 fill these in -----------------------------------------------
void vkchain_gui_points_init(int) {}
void vkchain_gui_points_draw(const GuiPointVertex*, int, float) {}
void vkchain_gui_points_shutdown(void) {}
void vkchain_vector_hard_clear(void) {}
void vkchain_init_raster_overlay(void) {}
void vkchain_shutdown_raster_overlay(void) {}
int  vkchain_get_error(void) { return 0; }
