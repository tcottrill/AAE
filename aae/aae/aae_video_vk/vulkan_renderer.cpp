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
#include "mame_vector.h"       // vector_start / vector_clear_list; pulls in
                               // aae_mame_driver.h for Machine and
                               // VIDEO_TYPE_VECTOR (safe direction: driver
                               // headers into the VK TU, not vulkan.h into
                               // core TUs, so the VULKAN_H_ leak guards in
                               // acommon.cpp et al. are unaffected)
#include "fast_poly_vk.h"      // FpolyVK - raster quad renderer (Plan 3)
#include "raster_emit.h"       // backend-neutral raster emit loop (Plan 3)

static VkContext g_vk;
static bool      s_initialized = false;
static bool      s_frameOpen = false;
static uint32_t  s_imageIndex = 0;

// Raster path (Plan 3): FpolyVK draws main_bitmap's pixels as quads into the
// frame pass. s_rasterW/H are the post-orientation game dims in SOURCE
// pixels; they double as the FpolyVK ortho extents (the shared emit loop
// outputs unscaled source-pixel coords with size = config.prescale, exactly
// like the GL path - no vid_scale anywhere; the letterbox viewport does the
// scaling to window size). s_fpolyFailed latches an Init failure so the
// lazy per-frame retry does not spam the log; it resets on every game load.
static FpolyVK  g_fpoly;
static bool     s_fpolyInit = false;
static bool     s_fpolyFailed = false;
static int      s_rasterW = 0;     // post-orientation dims, source pixels
static int      s_rasterH = 0;

// sRGB contingency note (Plan 3 Task 3, documented, NOT implemented): the
// swapchain can be an sRGB format while the pen colors are sRGB-authored
// bytes fed as UNORM vertex colors - identical to the proven Bosconian
// setup, so it ships as-is. IF the user gate reports washed-out/too-bright
// colors vs the GL chain side by side, the fix is a CPU sRGB->linear
// conversion of the pen RGB here (8-bit LUT applied to rgba's color bytes).
static void VkRasterSink(void* user, float x, float y, float size, uint32_t rgba)
{
	((FpolyVK*)user)->addPoly(x, y, size, rgba);
}

static bool GameIsRaster(void)
{
	return Machine && Machine->gamedrv &&
		!(Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR);
}

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

// The GL chain allocates the MAME vector display list in glchain_init
// (vector_start) for vector games; under Vulkan that path never runs, so
// the AVG simulations (tempest et al.) would append through vector_add_point
// into a NULL vector_list. Allocate it here instead. vector_start is
// idempotent, so overlapping with the late-AVG/DVG driver start (which also
// calls it) is harmless. beam_init is deliberately NOT called: it is GL-only
// (compiles shaders, builds VAOs/VBOs) and the CPU-side beam queue needs no
// init.
static void EnsureVectorList(void)
{
	if (Machine && Machine->gamedrv &&
	    (Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR))
	{
		if (!vector_start())
			LOG_ERROR("vkchain_init: vector_start failed (out of memory?)");
	}
}

// Mirrors EnsureVectorList for the raster path: (re)build the FpolyVK
// renderer whenever a raster game needs one. Handles fresh init, a raster
// game following a vector game (s_fpolyInit false), and a raster game with
// DIFFERENT post-orientation dims following another raster game (Shutdown +
// re-Init so the ortho matches the new game).
//
// Called from both vkchain_init paths AND from vkchain_render's raster
// block. The render-path call is required, not belt-and-braces: run_game
// calls init_gl (Step 4, aae_emulator.cpp:920) before vh_open (Step 11,
// :1017) creates main_bitmap, so raster_dst_dims() returns 0 during
// vkchain_init on a fresh load and the real init happens lazily on the
// first rendered frame. Machine->gamedrv/Machine->drv ARE populated by
// init_gl time (run_game logs gamedrv->desc at :874 and reads
// drv->rotation at :887), so GameIsRaster and the dims math are safe in
// both call sites. Init failure latches s_fpolyFailed (reset per game
// load in vkchain_init) so the per-frame call does not retry-spam.
static void EnsureRasterRenderer(void)
{
	if (!GameIsRaster() || s_fpolyFailed)
		return;

	int newW = 0, newH = 0;
	if (!raster_dst_dims(&newW, &newH))
		return;     // main_bitmap not created yet (pre-vh_open); retry later

	if (s_fpolyInit && (newW == s_rasterW && newH == s_rasterH))
		return;     // up to date

	if (s_fpolyInit)
	{
		// Game shape changed: rebuild. In-flight frames may still reference
		// the old pipeline/VBO, so drain the device first (game switches are
		// rare; the wait is not on the per-frame path).
		if (g_vk.device && g_vk.vkDeviceWaitIdle_)
			g_vk.vkDeviceWaitIdle_(g_vk.device);
		g_fpoly.Shutdown(g_vk);
		s_fpolyInit = false;
	}

	s_rasterW = newW;
	s_rasterH = newH;

	FastPolyVKCreateInfo ci{};
	ci.vertSpvPath = "shaders/vk/fast_poly_vk.vert.spv";
	ci.fragSpvPath = "shaders/vk/fast_poly_vk.frag.spv";
	ci.flipViewportY = true;
	// CORRECTED (Task 1 finding): the shared emit loop outputs UNSCALED
	// source-pixel coords with size = config.prescale, exactly like the
	// GL path. The ortho therefore spans the post-orientation source
	// dims - no vid_scale anywhere.
	if (g_fpoly.Init(g_vk, s_rasterW, s_rasterH, &ci))
	{
		s_fpolyInit = true;
		LOG_INFO("vkchain: FpolyVK online (%dx%d source pixels)", s_rasterW, s_rasterH);
	}
	else
	{
		s_fpolyFailed = true;
		LOG_ERROR("vkchain: FpolyVK init failed; raster game will show black");
	}
}

int vkchain_init(void)
{
	if (s_initialized)
	{
		// Re-entrant like glchain_init: run_game calls per load. A later
		// load can be a vector game even when the first was not, so the
		// list check runs on every load, not just the first.
		EnsureVectorList();
		// New game load: allow a fresh FpolyVK init attempt even if the
		// previous game's init failed, then rebuild if the shape changed
		// (raster->raster with different dims) or init for the first time
		// (raster after vector). Usually defers to the render-path call
		// because main_bitmap does not exist yet at this point in run_game.
		s_fpolyFailed = false;
		EnsureRasterRenderer();
		return 1;
	}

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

	EnsureVectorList();
	s_fpolyFailed = false;
	EnsureRasterRenderer();   // usually defers (see helper comment)

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
	// FpolyVK owns device objects, so it must go before VK_Shutdown (which
	// destroys the device). VK_Shutdown waits device-idle internally, but
	// buffer/pipeline destruction here races nothing: the app is tearing
	// down and no frame is open (winmain drains before shutdown).
	if (s_fpolyInit)
	{
		if (g_vk.device && g_vk.vkDeviceWaitIdle_)
			g_vk.vkDeviceWaitIdle_(g_vk.device);
		g_fpoly.Shutdown(g_vk);
		s_fpolyInit = false;
	}
	s_fpolyFailed = false;
	s_rasterW = 0;
	s_rasterH = 0;
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
	// Raster path (Plan 3): emit main_bitmap through the shared loop into
	// FpolyVK and record its draws into the frame pass VK_BeginFrame opened.
	// Post/artwork (Plan 4), vector (Plan 5) and GUI (Plan 6) follow.
	if (s_frameOpen && GameIsRaster())
	{
		// Lazy init/rebuild: main_bitmap does not exist yet when
		// vkchain_init runs (see EnsureRasterRenderer's comment), so the
		// first frame of a raster game lands here with s_fpolyInit false.
		if (!s_fpolyInit)
			EnsureRasterRenderer();

		if (s_fpolyInit)
		{
			raster_emit_polys(VkRasterSink, &g_fpoly, /*yFlip=*/1);

			// Aspect-fit letterbox: fit the post-orientation game rect into
			// the swapchain, centered. Natural pixel aspect for Plan 3; the
			// layout system's aspect overrides arrive with Plan 4.
			const float gameAspect = (float)s_rasterW / (float)s_rasterH;
			const int sw = (int)g_vk.swapchainExtent.width;
			const int sh = (int)g_vk.swapchainExtent.height;
			int vw = sw, vh = (int)(sw / gameAspect + 0.5f);
			if (vh > sh) { vh = sh; vw = (int)(sh * gameAspect + 0.5f); }
			g_fpoly.SetViewportRect((sw - vw) / 2, (sh - vh) / 2, vw, vh);

			g_fpoly.Render(g_vk, g_vk.cmdBuffers[g_vk.frameIndex],
				s_imageIndex, g_vk.frameIndex, false, 0.0f, 0.0f, 0.0f, 0.0f);
		}
	}

	// Drain the emu-side beam queue so vector games do not grow unbounded;
	// Plan 5's VectorDrawVK consumes this queue instead. cache_clear is pure
	// CPU (clears the beam line/join/shot lists via beam_clear plus the
	// legacy textured-shot list, which add_tex also fills every frame).
	cache_clear();

	// Drain the MAME vector display list (AVG/DVG sims append via
	// vector_add_point); Plan 5's VectorDrawVK consumes it instead.
	// Pure CPU: resets the list write index (mame_vector.cpp).
	vector_clear_list();
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
