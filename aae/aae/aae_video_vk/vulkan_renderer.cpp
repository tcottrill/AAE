// ===========================================================================
// vulkan_renderer.cpp - Vulkan chain orchestration (Phase 4a Plan 2).
//
// Owns the VkContext and maps the dispatch entry points onto the sys_vk
// frame loop (spec sec. 3.4):
//   vkchain_set_render   -> VK_BeginFrame (acquire, open pass, clear)
//   vkchain_render       -> record draws. Raster (Plan 4 Task 3): suspend
//                           the frame pass, game -> mipped RT via FpolyVK,
//                           GenerateMips, resume, RecordRect composite.
//                           Vector (Plan 5) and GUI (Plan 6) follow.
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
#include "render_target_vk.h"  // RenderTargetVK - offscreen game RT (Plan 4)
#include "screen_quad_vk.h"    // ScreenQuadVK - RT->swapchain composite (Plan 4)
#include "vector_draw_vk.h"    // VectorDrawVK - beam vector renderer (Plan 5);
                               // pulls in vector_draw.h (beam batch access)
#include "vector_post_vk.h"    // VectorPostVK - SSAA RT + trail + glow (Plan 7)
#include "../aae_emulator.h"   // emulator_is_gui_active (GUI keeps the direct path)
#include "vk_texture_loader.h" // VkTex_GetSolidWhite (Plan 6 - GUI solid quads)
#include "vector_fonts.h"      // VF singleton - CPU init under VK (Plan 6 fix)
#include "../aae_video/opengl_renderer.h" // GuiPointVertex full definition (GL-free header)

static VkContext g_vk;
static bool      s_initialized = false;
static bool      s_frameOpen = false;
static uint32_t  s_imageIndex = 0;

// Raster path: FpolyVK draws main_bitmap's pixels as quads into the game RT
// (s_rtGame, below). s_rasterW/H are the post-orientation game dims in
// SOURCE pixels; they double as the FpolyVK ortho extents AND the RT dims
// (the shared emit loop outputs unscaled source-pixel coords with size =
// config.prescale, exactly like the GL path - no vid_scale anywhere; the
// RecordRect composite does the scaling to window size). s_fpolyFailed
// latches an Init failure so the lazy per-frame retry does not spam the
// log; it resets on every game load.
static FpolyVK  g_fpoly;
static bool     s_fpolyInit = false;
static bool     s_fpolyFailed = false;
static int      s_rasterW = 0;     // post-orientation dims, source pixels
static int      s_rasterH = 0;
// Set when a dims-change rebuild is requested while a frame is open (the
// rebuild drains the device and destroys objects prior frames may still
// reference, so it must not run mid-frame). Serviced by vkchain_set_render
// at the next frame boundary, before VK_BeginFrame. Covers BOTH the FpolyVK
// rebuild and the s_rtGame resize (same trigger, same timing constraints).
static bool     s_fpolyRebuildPending = false;

// Offscreen game render target (Plan 4 Task 3): FpolyVK now draws into this
// mipped RT instead of straight into the swapchain pass; ScreenQuadVK then
// composites RT level 0 into the aspect-fit letterbox rect. Sized
// s_rasterW x s_rasterH (post-orientation SOURCE pixels, unit tiling) --
// NOT visible_area x config.prescale like the GL fbo_raster: the GL emit
// draws prescale-sized cells into a prescaled ortho, but the VK chain
// standardized on unit tiling at source resolution (Plan 3 corrected
// finding), and prescale is a GL-side supersampling knob that this RT's
// full mip chain supersedes (minification quality comes from the trilinear
// mip cascade, not from rendering oversized and downscaling).
static RenderTargetVK s_rtGame;

// RT->swapchain composite quad. Game-independent (per-format pipeline
// variants are built lazily), so it is initialized ONCE when the VK chain
// comes up (vkchain_init's first-run path only -- no per-frame retry, so a
// failure is a single log line) and shut down in vkchain_shutdown before
// VK_Shutdown. While false, the raster branch records nothing (black
// screen), mirroring the s_fpolyFailed degradation.
static ScreenQuadVK g_screenQuad;
static bool         s_screenQuadInit = false;

// Beam vector renderer (Plan 5 Task 1): draws the frame's BeamLine/BeamJoin/
// BeamShot batches direct to the swapchain inside the open frame pass (the
// SSAA RT + phosphor/glow chain is Plan 5 Task 3). Game-independent (the
// pipelines build against the swapchain format), so like ScreenQuadVK it is
// initialized once, lazily on the first vector frame, and shut down in
// vkchain_shutdown. s_vectorFailed latches an Init failure so the per-frame
// retry does not spam the log; it resets on every game load (a later load
// gets a fresh attempt). VectorDrawVK::Init is idempotent (backport fix 4c),
// so re-Init on a format change would be safe if ever needed.
static VectorDrawVK g_vectorDraw;
static bool         s_vectorInit = false;
static bool         s_vectorFailed = false;

// Vector post chain (Plan 7 = Plan 5 Task 3): NON-GUI vector games render
// beams into VectorPostVK's SSAA RT through a SECOND VectorDrawVK instance
// (g_vectorDrawRT - its pipelines are built against the RT's RGBA8 format and
// its AA feather uses the RT's ssaa; g_vectorDraw above stays swapchain-format
// for the gated GUI direct path). VectorPostVK then runs trail/glow and the
// composite. Same lazy-init + failure-latch discipline as the other
// subsystems; on failure the game falls back to the direct path (visible, no
// post effects) rather than black. s_trailClearPending forces a one-time
// trail-accumulator clear on each new game load (vkchain_init re-entrant
// path) so the previous game's phosphor never ghosts into the next.
static VectorDrawVK g_vectorDrawRT;
static VectorPostVK g_vectorPost;
static bool         s_vecPostInit = false;
static bool         s_vecPostFailed = false;
static bool         s_trailClearPending = true;

// GUI starfield (Plan 6 Task 1): a second, dedicated FpolyVK instance draws
// the front-end GUI's point-sprite stars. Kept separate from g_fpoly (the
// raster game quad renderer) because it draws at a different time in the
// frame (during run_gui(), i.e. inside cpu_run() -- BEFORE vkchain_render
// runs) and into different space (see GuiBeamToWindowPx below): stars are
// pre-transformed to explicit window-pixel coordinates on the CPU, so this
// instance's own ortho spans the full swapchain 1:1 (no SetViewportRect
// letterbox override -- the letterbox is already baked into the per-star
// coordinates). Game-independent, so like g_screenQuad it inits once
// (lazily, on the first GUI frame) and persists across game loads.
static FpolyVK g_guiPoints;
static bool    s_guiPointsInit = false;
static bool    s_guiPointsFailed = false;

// The shared emit loop passes cell indices with size = config.prescale
// (GL cell semantics: the GL Fpoly::addPoly multiplies x,y by size into a
// prescaled ortho - aae/aae/vidhrdwr/fast_poly.cpp). FpolyVK::addPoly is
// absolute-coordinate (donor-verbatim), and our ortho spans the unscaled
// source dims, so exact unit tiling is correct here (see the s_rtGame
// comment for why prescale stays a GL-only knob under VK).
//
// sRGB contingency note (Plan 3 Task 3, documented, NOT implemented): the
// swapchain can be an sRGB format while the pen colors are sRGB-authored
// bytes fed as UNORM vertex colors - identical to the proven Bosconian
// setup, so it ships as-is. IF the user gate reports washed-out/too-bright
// colors vs the GL chain side by side, the fix is a CPU sRGB->linear
// conversion of the pen RGB here (8-bit LUT applied to rgba's color bytes).
static void VkRasterSink(void* user, float x, float y, float size, uint32_t rgba)
{
	(void)size;
	((FpolyVK*)user)->addPoly(x, y, 1.0f, rgba);
}

static bool GameIsRaster(void)
{
	return Machine && Machine->gamedrv &&
		!(Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR);
}

static bool GameIsVector(void)
{
	return Machine && Machine->gamedrv &&
		(Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR) != 0;
}

// Column-major 4x4 ortho (same math as FpolyVK::MakeOrtho / aae::math::ortho).
// Maps x=l -> NDC -1, x=r -> +1, y=b -> NDC -1, y=t -> +1. Inverted ranges
// (b > t) are valid and produce the corresponding axis flip.
static void MakeOrthoColMajor(float l, float r, float b, float t, float* m)
{
	for (int i = 0; i < 16; ++i) m[i] = 0.0f;
	const float rl = (r - l);
	const float tb = (t - b);
	m[0] = 2.0f / rl;
	m[5] = 2.0f / tb;
	m[10] = 1.0f;
	m[12] = -(r + l) / rl;
	m[13] = -(t + b) / tb;
	m[15] = 1.0f;
}

// ---------------------------------------------------------------------------
// GUI-space -> window-pixel mapping (Plan 6 Task 1).
//
// The front-end GUI driver is VIDEO_TYPE_VECTOR, so vkchain_render's vector
// branch (below) already maps beam-space (0..1024 x 0..1024) coordinates to
// the swapchain via the game_rect box + aspect-fit letterbox + one vertical
// flip (see that branch's "Projection" comment for the full paper-trace
// derivation, verified against the GL reference). GL's GUI draws (VF text,
// VF::DrawQuad, the starfield) all land in the SAME fbo1 canvas the beams
// use and go through that SAME single flip during the fbo1->fbo4 composite
// (opengl_renderer.cpp's final_render, the flip_v=true drawTexturedQuad
// call) -- so replicating that exact transform for GUI content reproduces
// GL's output, regardless of each content type's own CPU-side authoring
// convention (VF's own ortho is y-up 0..768; the starfield's Star.x/y are
// already authored directly in the shared 0..1024 beam space, matching
// fillStars()'s kFBOHeight=1024 in driver_gui.cpp).
//
// GuiBeamToWindowPx below is the forward (beam-space -> window-pixel) form
// of that same map, for GUI draw paths that need explicit window-pixel
// coordinates (ScreenQuadVK::RecordRect, or CPU-computed vertices for the
// starfield's FpolyVK instance) instead of a vertex-shader projection
// matrix. Deliberately NOT unified with the vector branch's inline
// MakeOrthoColMajor call below (some duplicated letterbox arithmetic) to
// avoid touching that already-gated, working Plan 5 code path.
struct GuiBeamMap { float lx, ly, vw, vh, grL, grR, grB, grT; };

static GuiBeamMap ComputeGuiBeamMap(void)
{
	GuiBeamMap m{};
	const int sw = (int)g_vk.swapchainExtent.width;
	const int sh = (int)g_vk.swapchainExtent.height;

	float vecAspect = GetWindowSetup().aspectRatio;
	if (vecAspect <= 0.0f)
		vecAspect = 4.0f / 3.0f;
	int vw = sw, vh = (int)(sw / vecAspect + 0.5f);
	if (vh > sh) { vh = sh; vw = (int)(sh * vecAspect + 0.5f); }
	if (vw < 1) vw = 1;
	if (vh < 1) vh = 1;

	m.lx = (float)((sw - vw) / 2);
	m.ly = (float)((sh - vh) / 2);
	m.vw = (float)vw;
	m.vh = (float)vh;

	m.grL = (float)game_rect_left;   m.grR = (float)game_rect_right;
	m.grB = (float)game_rect_bottom; m.grT = (float)game_rect_top;
	if (m.grR - m.grL < 1.0f) { m.grL = 0.0f; m.grR = 1024.0f; }
	if (m.grT - m.grB < 1.0f) { m.grB = 0.0f; m.grT = 1024.0f; }
	return m;
}

// Maps beam-space (bx,by in the 0..1024 box) to window pixels, y-up
// (0 = window bottom) -- ScreenQuadVK::RecordRect's coordinate convention.
static void GuiBeamToWindowPx(const GuiBeamMap& m, float bx, float by, float& outX, float& outY)
{
	const float fx = m.grL + (bx / 1024.0f) * (m.grR - m.grL);
	const float fy = m.grT - (by / 1024.0f) * (m.grT - m.grB);
	outX = m.lx + (fx / 1024.0f) * m.vw;
	outY = m.ly + (fy / 1024.0f) * m.vh;
}

// Lazy once-per-session init of the beam renderer (see the g_vectorDraw
// comment). Defaults resolve to shaders/vk/vector_{line,disc,shot}_vk
// CustomBuild output; colorFormat is left UNDEFINED so the pipelines build
// against the swapchain format (direct-to-swapchain first cut); ssaa=1
// (backport fix 4b divide is in place for when the SSAA RT lands).
// beam_init() is deliberately NOT called: it is GL-only (compiles shaders,
// builds VAOs/VBOs); the CPU-side beam arrays need no init (proven Plan 2).
static void EnsureVectorRenderer(void)
{
	if (!GameIsVector() || s_vectorFailed || s_vectorInit)
		return;

	VectorDrawVKCreateInfo ci{};
	ci.ssaa = 1;
	if (g_vectorDraw.Init(g_vk, &ci))
	{
		s_vectorInit = true;
		LOG_INFO("vkchain: VectorDrawVK online (direct to swapchain)");
	}
	else
	{
		s_vectorFailed = true;
		LOG_ERROR("vkchain: VectorDrawVK init failed; vector game will show black");
	}
}

// Lazy once-per-session init of the vector post chain + its RT-format beam
// renderer (see the g_vectorPost comment). ssaa=2: the beam RT renders at
// 2048x2048 and the composite's trilinear minification is the supersample
// resolve (the GL chain runs beam_init(1) today - the SSAA RT was always
// this task's deliverable, and the AA feather divide keeps beam widths
// matching the GL look).
static void EnsureVectorPost(void)
{
	if (s_vecPostFailed || s_vecPostInit)
		return;

	VectorPostVKCreateInfo pci{};
	pci.ssaa = 2;
	if (!g_vectorPost.Init(g_vk, &pci))
	{
		s_vecPostFailed = true;
		LOG_ERROR("vkchain: VectorPostVK init failed; vector game falls back to direct path");
		return;
	}

	VectorDrawVKCreateInfo ci{};
	ci.colorFormat = g_vectorPost.BeamFormat();
	ci.ssaa = pci.ssaa;
	if (!g_vectorDrawRT.Init(g_vk, &ci))
	{
		g_vectorPost.Shutdown(g_vk);
		s_vecPostFailed = true;
		LOG_ERROR("vkchain: VectorDrawVK (RT) init failed; vector game falls back to direct path");
		return;
	}

	s_vecPostInit = true;
	LOG_INFO("vkchain: vector post chain online (beam RT %dx%d, trail+glow)",
		g_vectorPost.BeamDim(), g_vectorPost.BeamDim());
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
		return;     // up to date (two-int compare; cheap on the frame path)

	if (s_fpolyInit)
	{
		// Game shape changed: rebuild. In-flight frames may still reference
		// the old pipeline/VBO (and sample the old RT image), so the device
		// is drained first. That drain (and the destroys) must never run
		// while a frame is open, so a mid-frame request is deferred to the
		// next frame boundary (vkchain_set_render services the flag before
		// VK_BeginFrame).
		if (s_frameOpen)
		{
			s_fpolyRebuildPending = true;
			return;
		}
		if (g_vk.device && g_vk.vkDeviceWaitIdle_)
			g_vk.vkDeviceWaitIdle_(g_vk.device);
		g_fpoly.Shutdown(g_vk);
		s_fpolyInit = false;
	}
	s_fpolyRebuildPending = false;

	s_rasterW = newW;
	s_rasterH = newH;

	// (Re)create the offscreen game RT BEFORE FpolyVK: the FpolyVK pipeline
	// is built against the RT's color format below.
	//
	// Format trace (why UNORM keeps colors IDENTICAL to Plan 3's direct
	// swapchain draw): Plan 3 fed sRGB-authored pen bytes as UNORM vertex
	// colors straight into the sRGB swapchain attachment, so the hardware
	// applied exactly one linear->sRGB encode on store. Now the same pen
	// bytes are written to this R8G8B8A8_UNORM RT (UNORM attachment: stored
	// byte-for-byte, no encode), RecordRect samples the UNORM view (no
	// decode -- the raw bytes come back as the same floats) and writes them
	// to the sRGB swapchain, where the hardware applies the same single
	// encode Plan 3 got. One encode either way; the final swapchain bytes
	// are identical. An _SRGB RT here would decode-on-sample and re-encode
	// twice, shifting every mid-tone.
	if (!s_rtGame.IsValid())
	{
		RenderTargetVKCreateInfo rtci{};
		rtci.width = s_rasterW;
		rtci.height = s_rasterH;
		rtci.filter = rtFilterVK::Linear;
		rtci.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
		rtci.mipLevels = -1;   // full chain (Plan 4 CRT halation + minified composite)
		if (!s_rtGame.Init(g_vk, rtci))
		{
			s_fpolyFailed = true;
			LOG_ERROR("vkchain: game RenderTargetVK init failed; raster game will show black");
			return;
		}
	}
	else if (s_rtGame.GetWidth() != s_rasterW || s_rtGame.GetHeight() != s_rasterH)
	{
		// Resize waits device-idle internally (donor discipline: the other
		// in-flight frame may still be sampling the old image) -- redundant
		// with the drain above on the rebuild path, but harmless, and it
		// keeps Resize safe for any future caller.
		s_rtGame.Resize(g_vk, s_rasterW, s_rasterH);
		if (!s_rtGame.IsValid())
		{
			s_fpolyFailed = true;
			LOG_ERROR("vkchain: game RenderTargetVK resize failed; raster game will show black");
			return;
		}
	}

	FastPolyVKCreateInfo ci{};
	ci.vertSpvPath = "shaders/vk/fast_poly_vk.vert.spv";
	ci.fragSpvPath = "shaders/vk/fast_poly_vk.frag.spv";
	ci.flipViewportY = true;
	// Plan 4 Task 3: FpolyVK renders into s_rtGame, so its pipeline must be
	// built against the RT's format, not the swapchain's (dynamic rendering
	// requires the pipeline's declared attachment format to match the pass).
	ci.colorFormat = s_rtGame.GetFormat();
	// CORRECTED (Task 1 finding): the shared emit loop outputs UNSCALED
	// source-pixel coords with size = config.prescale, exactly like the
	// GL path. The ortho therefore spans the post-orientation source
	// dims - no vid_scale anywhere.
	if (g_fpoly.Init(g_vk, s_rasterW, s_rasterH, &ci))
	{
		s_fpolyInit = true;
		// Permanent full-RT viewport override: FpolyVK's default (no
		// override) viewport is ctx.swapchainExtent, which is wrong for RT
		// rendering. The aspect-fit letterboxing that used to live here
		// moved to the RecordRect composite in vkchain_render.
		g_fpoly.SetViewportRect(0, 0, s_rasterW, s_rasterH);
		LOG_INFO("vkchain: FpolyVK online (%dx%d source pixels into %s RT, %u mips)",
			s_rasterW, s_rasterH,
			(s_rtGame.GetFormat() == VK_FORMAT_R8G8B8A8_UNORM) ? "RGBA8_UNORM" : "non-UNORM",
			s_rtGame.GetMipLevels());
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
		// Same latch-reset for the beam renderer: a later load gets a
		// fresh init attempt (the renderer itself persists across games).
		s_vectorFailed = false;
		// Post chain: fresh init attempt per load, and clear the trail
		// accumulator once so the previous game's phosphor never ghosts.
		s_vecPostFailed = false;
		s_trailClearPending = true;
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

	// ScreenQuadVK is game-independent: init once with the VK context, keep
	// it across game loads, shut down in vkchain_shutdown. Defaults resolve
	// to shaders/vk/screen_quad_rect_vk.{vert,frag}.spv (CustomBuild output).
	if (g_screenQuad.Init(g_vk, nullptr))
		s_screenQuadInit = true;
	else
		LOG_ERROR("vkchain_init: ScreenQuadVK init failed; raster composite disabled");

	EnsureVectorList();
	s_fpolyFailed = false;
	EnsureRasterRenderer();   // usually defers (see helper comment)
	s_vectorFailed = false;   // beam renderer inits lazily on the first vector frame
	s_vecPostFailed = false;  // post chain likewise
	s_trailClearPending = true;

	// Vector-font CPU state (screen dims + projection fields). Under GL this
	// runs in glchain_init (opengl_renderer.cpp); the VK chain must do it too
	// or every VF.Print computes glyphs against 0x0 dims and the GUI/overlay
	// text silently vanishes. Initialize skips its GL object creation on the
	// VK chain (guard inside). Idempotent.
	VF.Initialize(1024, 768);

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
	// FpolyVK, ScreenQuadVK and the game RT all own device objects, so they
	// must go before VK_Shutdown (which destroys the device). VK_Shutdown
	// waits device-idle internally, but buffer/pipeline destruction here
	// races nothing: the app is tearing down and no frame is open (winmain
	// drains before shutdown). One drain up front covers all three.
	if (g_vk.device && g_vk.vkDeviceWaitIdle_)
		g_vk.vkDeviceWaitIdle_(g_vk.device);
	if (s_fpolyInit)
	{
		g_fpoly.Shutdown(g_vk);
		s_fpolyInit = false;
	}
	if (s_screenQuadInit)
	{
		g_screenQuad.Shutdown(g_vk);
		s_screenQuadInit = false;
	}
	if (s_vectorInit)
	{
		g_vectorDraw.Shutdown(g_vk);
		s_vectorInit = false;
	}
	s_vectorFailed = false;
	if (s_vecPostInit)
	{
		g_vectorDrawRT.Shutdown(g_vk);
		g_vectorPost.Shutdown(g_vk);
		s_vecPostInit = false;
	}
	s_vecPostFailed = false;
	if (s_guiPointsInit)
	{
		g_guiPoints.Shutdown(g_vk);
		s_guiPointsInit = false;
	}
	s_guiPointsFailed = false;
	VkTex_ShutdownCache(g_vk);   // GUI solid-white texture + any cached loads
	if (s_rtGame.IsValid())
		s_rtGame.Shutdown(g_vk);
	s_fpolyFailed = false;
	s_fpolyRebuildPending = false;
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

	// Service a deferred FpolyVK rebuild here, where no frame is open (the
	// s_frameOpen early-out above guarantees it), so the device drain and
	// object destruction never overlap an open frame.
	if (s_fpolyRebuildPending)
	{
		s_fpolyRebuildPending = false;
		EnsureRasterRenderer();
	}

	if (!VK_BeginFrame(g_vk, s_imageIndex))
	{
		RecreateSwapchainOrDefer();
		return;             // skip this frame; next tick re-acquires
	}
	s_frameOpen = true;

	// Deterministic per-frame slot-cursor reset (bug catalog entry 9 /
	// ScreenQuadVK header): once per frame, right after VK_BeginFrame has
	// fixed the frame slot and its fence wait has proven the slot's previous
	// frame is done. The engine donor does the same at its per-frame render
	// site (bg_renderer.cpp); here set_render IS that once-per-frame site.
	if (s_screenQuadInit)
		g_screenQuad.OnFrameBegin(g_vk.frameIndex);

	// Beam renderer slot reset (backport fix 4a): drain the slot's retired
	// instance buffers and reset its write heads, here where the fence wait
	// has just proven the slot's previous frame is done.
	if (s_vectorInit)
		g_vectorDraw.OnFrameBegin(g_vk, g_vk.frameIndex);

	// Post-chain slot resets (same contract): the RT-format beam renderer's
	// buffer drain and VectorPostVK's descriptor-ring cursor.
	if (s_vecPostInit)
	{
		g_vectorDrawRT.OnFrameBegin(g_vk, g_vk.frameIndex);
		g_vectorPost.OnFrameBegin(g_vk.frameIndex);
	}
}

void vkchain_render(void)
{
	// Raster path (Plan 4 Task 3): suspend the swapchain frame pass, render
	// the game into the mipped offscreen RT (FpolyVK), regenerate its mip
	// chain, resume the frame pass, and composite the RT into the aspect-fit
	// letterbox rect via ScreenQuadVK. The scanline/CRT passes (Plan 4
	// Tasks 4-5) slot in between the game draw and the composite; the layout
	// system (Task 6) replaces the RecordRect composite (and brings
	// rotation/aspect overrides -- this composite is non-rotated).
	// Vector (Plan 5) and GUI (Plan 6) follow.
	if (s_frameOpen && GameIsRaster())
	{
		// Lazy init/rebuild: main_bitmap does not exist yet when
		// vkchain_init runs (see EnsureRasterRenderer's comment), so the
		// first frame of a raster game lands here with s_fpolyInit false.
		// Called unconditionally (dims-equal early-out is two int compares)
		// so a stale ortho can never survive a session; a dims-change
		// rebuild requested here is deferred to the next frame boundary.
		EnsureRasterRenderer();

		if (s_fpolyInit && s_rtGame.IsValid() && s_screenQuadInit)
		{
			VkCommandBuffer cmd = g_vk.cmdBuffers[g_vk.frameIndex];

			// Offscreen work is illegal inside the swapchain pass (the RT
			// opens its own pass; vkCmdBlitImage may not appear inside any
			// pass), so the frame pass is suspended around it. No swapchain
			// barriers: the image stays COLOR_ATTACHMENT_OPTIMAL and the
			// resume re-opens with LOAD_OP_LOAD (see sys_vk.cpp).
			VK_SuspendFramePass(g_vk, cmd);

			// Game pass: clear to opaque black (the GL fbo_raster clears
			// black too), emit the frame's pixels, record the draws.
			s_rtGame.Begin(g_vk, cmd, /*clear=*/true, 0.0f, 0.0f, 0.0f, 1.0f);
			raster_emit_polys(VkRasterSink, &g_fpoly, /*yFlip=*/1);
			g_fpoly.Render(g_vk, cmd,
				s_imageIndex, g_vk.frameIndex, false, 0.0f, 0.0f, 0.0f, 0.0f);
			s_rtGame.End(g_vk, cmd);

			// Mips are load-bearing downstream (CRT halation textureLod,
			// minified layout composite) and the trilinear sampler already
			// reads them when the letterbox rect is smaller than the RT.
			if (s_rtGame.GetMipLevels() > 1)
				s_rtGame.GenerateMips(g_vk, cmd);

			VK_ResumeFramePass(g_vk, cmd, s_imageIndex);

			// Aspect-fit letterbox (Plan 3 math, unchanged): fit the
			// post-orientation game rect into the swapchain, centered.
			// RecordRect takes the rect in y-up screen pixels, but a
			// centered rect is symmetric, so the same offsets serve.
			const float gameAspect = (float)s_rasterW / (float)s_rasterH;
			const int sw = (int)g_vk.swapchainExtent.width;
			const int sh = (int)g_vk.swapchainExtent.height;
			int vw = sw, vh = (int)(sw / gameAspect + 0.5f);
			if (vh > sh) { vh = sh; vw = (int)(sh * gameAspect + 0.5f); }
			const float lx = (float)((sw - vw) / 2);
			const float ly = (float)((sh - vh) / 2);

			// flipUV_Y trace (Gate A verifies): the emit runs with yFlip=1,
			// so the game's TOP scanline (post-orientation y=0) is emitted
			// at world y = rtH-1; FpolyVK's y-up ortho (0..rtH) plus its
			// flipped viewport map world y=rtH to RT image row 0, so the
			// game's top scanline lands in RT row 0. RecordRect with
			// flipUV_Y=false samples image row 0 (v=0) at the rect's TOP
			// vertex -- game top at screen top, upright. So: false.
			g_screenQuad.RecordRect(g_vk, cmd, g_vk.frameIndex,
				s_rtGame.VK_GetColorView(), s_rtGame.VK_GetSampler(),
				lx, ly, lx + (float)vw, ly + (float)vh,
				(uint32_t)sw, (uint32_t)sh,
				/*flipUV_Y=*/false, RGB_WHITE);
		}
	}

	// Vector post chain (Plan 7 = Plan 5 Task 3): NON-GUI vector games render
	// beams into the SSAA RT, run trail/glow, and composite - mirroring the
	// GL chain's fbo1 -> trail/blur -> final_render shape. The GUI keeps the
	// gated Plan 5/6 direct path below (GL excludes the GUI from trail/glow
	// anyway), as does any game if the post chain failed to init (visible
	// beams beat a black screen).
	//
	// Documented deviation: the in-game menu/pause text rides the beam queue
	// under VK (Plan 6 routes VF strokes through beam_add_line), so it gets
	// trail/glow here; GL draws those overlays post-composite, crisp.
	const bool wantPostChain = s_frameOpen && GameIsVector() &&
		!emulator_is_gui_active();
	if (wantPostChain)
		EnsureVectorPost();

	if (wantPostChain && s_vecPostInit)
	{
		VkCommandBuffer cmd = g_vk.cmdBuffers[g_vk.frameIndex];
		const int sw = (int)g_vk.swapchainExtent.width;
		const int sh = (int)g_vk.swapchainExtent.height;

		// Mirror glchain_render's !paused guard; unlike the direct path,
		// pause here shows the RETAINED frame (composite below still runs) -
		// parity with GL's frozen fbo1 restored, as the Plan 5 comment
		// promised for this task.
		if (!paused && sw > 0 && sh > 0)
		{
			// CPU-only conversion, same as the direct path (see its comment).
			vector_update();

			// Beams draw into the SQUARE beam RT with the GL chain's own
			// fbo1 projection - ortho(0,1024,0,1024) - not the direct path's
			// folded window ortho; the composite quad applies game_rect +
			// letterbox + the single V flip instead (exactly like GL's
			// final_render quad).
			float proj[16];
			MakeOrthoColMajor(0.0f, 1024.0f, 0.0f, 1024.0f, proj);

			const bool additive = Machine && Machine->drv &&
				(Machine->drv->video_attributes & VECTOR_USES_COLOR) != 0;

			VK_SuspendFramePass(g_vk, cmd);
			g_vectorPost.BeginBeamPass(g_vk, cmd);
			g_vectorDrawRT.Record(g_vk, cmd, g_vk.frameIndex, proj, additive,
				(uint32_t)g_vectorPost.BeamDim(), (uint32_t)g_vectorPost.BeamDim());
			g_vectorPost.EndBeamPass(g_vk, cmd);
			g_vectorPost.RecordPost(g_vk, cmd, g_vk.frameIndex,
				config.vectrail, config.vecglow, s_trailClearPending);
			s_trailClearPending = false;
			VK_ResumeFramePass(g_vk, cmd, s_imageIndex);
		}

		if (sw > 0 && sh > 0)
		{
			// Same beam-space -> window map as the GUI helpers: game_rect box
			// scaled into the aspect-fit letterbox. RecordComposite takes
			// Y-DOWN window pixels, so the y-up letterbox coords flip here.
			const GuiBeamMap m = ComputeGuiBeamMap();
			const float x0 = m.lx + (m.grL / 1024.0f) * m.vw;
			const float x1 = m.lx + (m.grR / 1024.0f) * m.vw;
			const float yTopUp = m.ly + (m.grT / 1024.0f) * m.vh;
			const float yBotUp = m.ly + (m.grB / 1024.0f) * m.vh;
			g_vectorPost.RecordComposite(g_vk, cmd, g_vk.frameIndex,
				x0, (float)sh - yTopUp, x1, (float)sh - yBotUp,
				config.vectrail, config.vecglow);
		}
	}
	// Vector path (Plan 5 Task 1): draw the frame's beam batches direct to
	// the swapchain, inside the frame pass VK_BeginFrame opened (no
	// suspend/resume needed -- VectorDrawVK records into an already-open
	// pass). Order per plan: vector_update (CPU convert), Record (consume),
	// then the clears below (post-consume drain). Since Plan 7 this path
	// serves the GUI driver and the post-chain-init-failed fallback only.
	else if (s_frameOpen && GameIsVector())
	{
		EnsureVectorRenderer();

		// Mirror glchain_render's !paused guard. Known first-cut limitation:
		// GL freezes the last frame in its FBO while paused; direct-to-
		// swapchain has no retained image, so a paused vector game shows
		// black until unpause (the Task 3 RT chain restores parity).
		if (s_vectorInit && !paused &&
		    g_vk.swapchainExtent.width > 0 && g_vk.swapchainExtent.height > 0)
		{
			// CPU-only conversion (audited: mame_vector.cpp vector_update ->
			// add_line/add_tex -> beam_add_line/beam_add_shot, no GL calls):
			// transforms the AVG/late-DVG display list into 0..1024 beam
			// space with driver rotation applied. Old-DVG sims (asteroid)
			// already fed the beam arrays directly during cpu_run.
			//
			// Note: when config.shots_textured is set, add_tex routes shots
			// into the legacy textured-shot list (a GL-only draw path) and
			// they will not appear under VK; the default procedural beam
			// shots render fine.
			vector_update();

			// ---- Projection: mirror the GL chain's net beam -> screen map.
			//
			// GL reference (opengl_renderer.cpp): beams draw into a square
			// 1024x1024 FBO with ortho(0,1024,0,1024); final_render then maps
			// that FBO onto the quad [game_rect_left..right] x
			// [game_rect_bottom..top] in fbo4's Y-up 1024-space with
			// flip_v=true (the chain's single vertical flip, righting MAME's
			// y-down beam coords); end_render_fbo4's screen_rect finally
			// letterboxes square fbo4 onto ws.aspectRatio in the window.
			//
			// Net GL map, reproduced here directly (fx,fy = fbo4 coords):
			//   fx = grL + (bx/1024)*(grR-grL)
			//   fy = grT - (by/1024)*(grT-grB)      <- the one flip
			//   window px (y-up) = letterbox(lx,ly,vw,vh) of fbo4 0..1024
			// We draw with a full-swapchain viewport and fold the letterbox +
			// game_rect + flip into one ortho by inverting that map at the
			// swapchain edges (beam-space coords at window left/right/bottom/
			// top). The ortho comes out Y-inverted (b > t), which combined
			// with VectorDrawVK's flipped (negative-height) viewport yields:
			//
			// Paper trace (defaults grL=0 grR=1024 grB=0 grT=1024, exact-fit
			// letterbox): beam (0,0) = MAME top-left. x: bx=0 -> fx=0 ->
			// window x = lx (letterbox left). y: by=0 -> fy=1024 -> ortho t
			// ~= 0 -> NDC y=+1 -> negative-height viewport maps NDC +1 to
			// framebuffer row 0 = window TOP. So beam (0,0) renders at the
			// letterbox's top-left, exactly where GL puts it (GL: by=0 ->
			// FBO v=0 -> flip_v quad top -> fbo4 y=grT -> screen_rect top).
			const int sw = (int)g_vk.swapchainExtent.width;
			const int sh = (int)g_vk.swapchainExtent.height;

			// Aspect-fit letterbox, same math as the raster branch /
			// Rect2::UpdateScreenRect. Vector display aspect = the window
			// setup's aspect (Step 12 stores the game aspect there for GL's
			// screen_rect; 4:3 fallback matches Rect2's).
			float vecAspect = GetWindowSetup().aspectRatio;
			if (vecAspect <= 0.0f)
				vecAspect = 4.0f / 3.0f;
			int vw = sw, vh = (int)(sw / vecAspect + 0.5f);
			if (vh > sh) { vh = sh; vw = (int)(sh * vecAspect + 0.5f); }
			if (vw < 1) vw = 1;
			if (vh < 1) vh = 1;
			const float lx = (float)((sw - vw) / 2);
			const float ly = (float)((sh - vh) / 2);

			// Per-game CRT rect in fbo4 1024-space (aae_mame_driver.h /
			// config.cpp; defaults 0..1024). Degenerate values fall back.
			float grL = (float)game_rect_left,   grR = (float)game_rect_right;
			float grB = (float)game_rect_bottom, grT = (float)game_rect_top;
			if (grR - grL < 1.0f) { grL = 0.0f; grR = 1024.0f; }
			if (grT - grB < 1.0f) { grB = 0.0f; grT = 1024.0f; }

			// Invert the beam->window map at the swapchain edges.
			const float fx0 = (0.0f - lx) * 1024.0f / (float)vw;       // window left
			const float fx1 = ((float)sw - lx) * 1024.0f / (float)vw;  // window right
			const float ol = (fx0 - grL) * 1024.0f / (grR - grL);
			const float orr = (fx1 - grL) * 1024.0f / (grR - grL);
			const float fy0 = (0.0f - ly) * 1024.0f / (float)vh;       // window bottom (y-up)
			const float fy1 = ((float)sh - ly) * 1024.0f / (float)vh;  // window top
			const float ob = (grT - fy0) * 1024.0f / (grT - grB);      // beam y at window bottom
			const float ot = (grT - fy1) * 1024.0f / (grT - grB);      // beam y at window top

			float proj[16];
			MakeOrthoColMajor(ol, orr, ob, ot, proj);

			// Blend selection mirrors beam_draw_all(): color games additive,
			// B/W alpha-over with the painter's sort inside Record.
			const bool additive = Machine && Machine->drv &&
				(Machine->drv->video_attributes & VECTOR_USES_COLOR) != 0;

			VkCommandBuffer cmd = g_vk.cmdBuffers[g_vk.frameIndex];
			// 0,0 target dims = record against the swapchain extent
			// (direct-to-swapchain first cut; Task 3 passes the RT dims).
			g_vectorDraw.Record(g_vk, cmd, g_vk.frameIndex, proj, additive, 0, 0);
		}
	}

	// Post-consume clears (Plan 5 order: vector_update, Record, clear).
	// cache_clear is pure CPU: clears the beam line/join/shot arrays
	// VectorDrawVK just consumed (beam_clear) plus the legacy textured-shot
	// list. Also keeps the queues from growing unbounded on raster/GUI
	// frames, exactly as the Plan 2 drain-only calls did.
	cache_clear();

	// Drain the MAME vector display list (AVG/DVG sims append via
	// vector_add_point; vector_update consumed it above on vector frames).
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

// ---------------------------------------------------------------------------
// GUI starfield (Plan 6 Task 1). See the g_guiPoints comment above for why
// this is a separate FpolyVK instance drawn immediately here (during
// run_gui(), called from cpu_run() before vkchain_render runs this frame --
// same ordering as GL's glchain_set_render-before-cpu_run) instead of being
// queued into the vector beam batch: this call needs to land BEHIND the
// quad/text draws that follow it later in the same frame's command buffer,
// and record order is z-order within one dynamic-rendering pass.
//
// vkchain_set_render() (called by set_render(), before cpu_run()/run_gui())
// has already opened the frame by the time this runs, so s_frameOpen is
// true and g_vk.cmdBuffers[g_vk.frameIndex] is a valid, currently-recording
// command buffer inside the open swapchain pass.
// ---------------------------------------------------------------------------
void vkchain_gui_points_init(int maxPoints)
{
	(void)maxPoints;   // FpolyVK grows its VBO on demand; nothing to pre-size here.
}

void vkchain_gui_points_draw(const GuiPointVertex* pts, int count, float pointSize)
{
	if (!s_initialized || !s_frameOpen || !pts || count <= 0)
		return;

	if (!s_guiPointsInit && !s_guiPointsFailed)
	{
		FastPolyVKCreateInfo ci{};
		ci.vertSpvPath = "shaders/vk/fast_poly_vk.vert.spv";
		ci.fragSpvPath = "shaders/vk/fast_poly_vk.frag.spv";
		ci.flipViewportY = true;   // OpenGL-style: input (0,0) = window bottom-left.
		// colorFormat left UNDEFINED -> builds against ctx.swapchainFormat
		// (this instance always draws direct to the swapchain, like the
		// vector beam renderer).
		if (g_guiPoints.Init(g_vk, (int)g_vk.swapchainExtent.width, (int)g_vk.swapchainExtent.height, &ci))
		{
			s_guiPointsInit = true;
		}
		else
		{
			s_guiPointsFailed = true;
			LOG_ERROR("vkchain: GUI starfield FpolyVK init failed; stars will not draw");
		}
	}
	if (!s_guiPointsInit)
		return;

	// Ortho is rebuilt from m_surfaceW/H every Render() call (FpolyVK::
	// UpdateGlobals), so this is cheap and keeps it correct across resizes.
	g_guiPoints.SetSurfaceSize((int)g_vk.swapchainExtent.width, (int)g_vk.swapchainExtent.height);

	// Stars are authored directly in the shared 0..1024 beam-space (see the
	// GuiBeamToWindowPx comment), so no extra 768/1024 rescale like VF text
	// needs. Pre-transform to window pixels on the CPU and size the point
	// quad by the same letterbox zoom factor so stars scale with the window
	// like everything else in the vector/GUI chain.
	const GuiBeamMap m = ComputeGuiBeamMap();
	const float sizeScale = m.vw / 1024.0f;
	const float sizePx = pointSize * sizeScale;

	for (int i = 0; i < count; ++i)
	{
		float wx, wy;
		GuiBeamToWindowPx(m, pts[i].x, pts[i].y, wx, wy);

		const uint32_t rgba = MAKE_RGBA(
			(int)(pts[i].r * 255.0f + 0.5f),
			(int)(pts[i].g * 255.0f + 0.5f),
			(int)(pts[i].b * 255.0f + 0.5f),
			(int)(pts[i].a * 255.0f + 0.5f));

		g_guiPoints.addPoly(wx - sizePx * 0.5f, wy - sizePx * 0.5f, sizePx, rgba);
	}

	VkCommandBuffer cmd = g_vk.cmdBuffers[g_vk.frameIndex];
	g_guiPoints.Render(g_vk, cmd, s_imageIndex, g_vk.frameIndex, false, 0.0f, 0.0f, 0.0f, 0.0f);
}

void vkchain_gui_points_shutdown(void)
{
	if (s_guiPointsInit)
	{
		g_guiPoints.Shutdown(g_vk);
		s_guiPointsInit = false;
	}
	s_guiPointsFailed = false;
}

// ---------------------------------------------------------------------------
// vkchain_vector_hard_clear (Plan 6 Task 1 investigation finding).
// GL's glchain_vector_hard_clear_fbo1 clears fbo1's three attachments
// (img1a/img1b/img1c) to flush leftover trail/phosphor-feedback data from a
// previous vector game when the GUI starts up. Under Vulkan there is no
// persistent fbo1-equivalent: VectorDrawVK renders direct to the swapchain
// every frame (Plan 5) with no retained image and no trail/feedback state
// (the SSAA RT + phosphor/glow chain is deferred to Plan 5 Task 3, not yet
// implemented) -- so there is nothing to hard-clear yet. No-op until that
// lands, at which point this should clear whatever retained buffer it adds.
// ---------------------------------------------------------------------------
void vkchain_vector_hard_clear(void) {}

// ---------------------------------------------------------------------------
// GUI solid-color quads (Plan 6 Task 1). VectorFont::DrawQuad's GL path
// draws through its own GL program/VAO (vector_fonts.cpp) -- GL-direct,
// invisible under Vulkan. This seam reuses ScreenQuadVK::RecordRect (already
// online for the raster composite) with a 1x1 white texture tinted by the
// caller's color, alpha-blended (RecordRect's pipeline is SRC_ALPHA /
// ONE_MINUS_SRC_ALPHA, matching VF::Begin()'s GL blend state).
// ---------------------------------------------------------------------------
void vkchain_gui_draw_quad(float x, float y, float width, float height, rgb_t color)
{
	if (!s_initialized || !s_frameOpen || !s_screenQuadInit)
		return;

	VkTexture* white = VkTex_GetSolidWhite(g_vk);
	if (!white)
		return;

	// GUI-local space (VF::Initialize(1024,768)): x centered directly in the
	// shared 0..1024 box; y needs the same 768->1024 rescale VF text uses
	// (GuiBeamToWindowPx works in the native 0..1024 beam space).
	const float scaleY = 1024.0f / 768.0f;
	const float minx = x - width * 0.5f,  maxx = x + width * 0.5f;
	const float miny = (y - height * 0.5f) * scaleY, maxy = (y + height * 0.5f) * scaleY;

	const GuiBeamMap m = ComputeGuiBeamMap();
	float x0, y0, x1, y1;
	GuiBeamToWindowPx(m, minx, miny, x0, y0);
	GuiBeamToWindowPx(m, maxx, maxy, x1, y1);

	// The by->fy map includes a flip, so min/max in beam space does not
	// necessarily land in the same order in window space -- sort explicitly.
	const float L = (x0 < x1) ? x0 : x1;
	const float R = (x0 < x1) ? x1 : x0;
	const float B = (y0 < y1) ? y0 : y1;
	const float T = (y0 < y1) ? y1 : y0;

	const uint32_t sw = g_vk.swapchainExtent.width;
	const uint32_t sh = g_vk.swapchainExtent.height;
	VkCommandBuffer cmd = g_vk.cmdBuffers[g_vk.frameIndex];

	g_screenQuad.RecordRect(g_vk, cmd, g_vk.frameIndex,
		white->view, white->sampler,
		L, B, R, T, sw, sh,
		/*flipUV_Y=*/false, color);
}

void vkchain_init_raster_overlay(void) {}
void vkchain_shutdown_raster_overlay(void) {}
int  vkchain_get_error(void) { return 0; }
