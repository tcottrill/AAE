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

static VkContext g_vk;
static bool      s_initialized = false;
static bool      s_frameOpen = false;
static uint32_t  s_imageIndex = 0;

// Set while VK_BeginFrame/VK_RecreateSwapchain cannot back a swapchain
// because the surface has zero area (window minimized, or mid-drag).
// vkchain_set_render runs once per emulator tick regardless of window state
// (audio keeps running while minimized), and CreateSwapchain logs a
// "deferring" line every time it is called with a zero extent. Without this
// flag a minimized Vulkan session would call VK_RecreateSwapchain -> log
// "deferring" every single tick (tens of lines/sec in systemlog.txt). Instead
// we log our own state transition once and stop retrying from here; the
// window layer only calls vkchain_on_window_resize on WM_SIZE when the
// window is NOT minimized (see winmain.cpp), so restoring the window is what
// clears the deferral and retries.
static bool s_deferredZeroExtent = false;

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
}

void vkchain_set_render(void)
{
	if (!s_initialized || s_frameOpen)
		return;

	// While deferred (zero-area surface) do not retry from here: retrying
	// every tick would call VK_RecreateSwapchain -> CreateSwapchain, which
	// logs every attempt, at full frame rate. vkchain_on_window_resize is
	// the only path that clears s_deferredZeroExtent (see comment above).
	if (s_deferredZeroExtent)
		return;

	if (!VK_BeginFrame(g_vk, s_imageIndex))
	{
		if (!VK_RecreateSwapchain(g_vk))
		{
			s_deferredZeroExtent = true;
			LOG_INFO("vkchain_set_render: swapchain recreate deferred (zero-area surface); will retry on resize");
		}
		return;             // skip this frame; next tick re-acquires
	}
	s_frameOpen = true;
}

void vkchain_render(void)
{
	// Plan 2: the frame pass opened by VK_BeginFrame clears to the gate
	// color; there is nothing to record yet. Raster (Plan 3), post/artwork
	// (Plan 4), vector (Plan 5) and GUI (Plan 6) record here.
}

void vkchain_swap_buffers(void)
{
	if (!s_initialized || !s_frameOpen)
		return;
	s_frameOpen = false;
	if (!VK_EndFrame(g_vk, s_imageIndex))
		VK_RecreateSwapchain(g_vk);
}

void vkchain_set_vsync(bool enabled)
{
	if (!s_initialized)
		return;
	if (g_vk.vsync == enabled)
		return;
	g_vk.vsync = enabled;
	VK_RecreateSwapchain(g_vk);   // waits device idle internally
}

void vkchain_on_window_resize(int newW, int newH)
{
	(void)newW; (void)newH;
	if (!s_initialized)
		return;

	// This is the only retry path while s_deferredZeroExtent is set (see
	// vkchain_set_render): winmain.cpp only calls emulator_on_window_resize,
	// and therefore this function, on WM_SIZE when the window is NOT
	// minimized, so a nonzero extent is the expected case here.
	if (VK_RecreateSwapchain(g_vk))
	{
		if (s_deferredZeroExtent)
		{
			LOG_INFO("vkchain_on_window_resize: swapchain recreated, resuming frames");
			s_deferredZeroExtent = false;
		}
	}
	else if (!s_deferredZeroExtent)
	{
		s_deferredZeroExtent = true;
		LOG_INFO("vkchain_on_window_resize: swapchain recreate deferred (zero-area surface); will retry on resize");
	}
}

// --- Plans 3-6 fill these in -----------------------------------------------
void vkchain_gui_points_init(int) {}
void vkchain_gui_points_draw(const GuiPointVertex*, int, float) {}
void vkchain_gui_points_shutdown(void) {}
void vkchain_vector_hard_clear(void) {}
void vkchain_init_raster_overlay(void) {}
void vkchain_shutdown_raster_overlay(void) {}
int  vkchain_get_error(void) { return 0; }
