// ===========================================================================
// vulkan_renderer.cpp - Vulkan chain orchestration, the twin of the GL chain
// in aae_video/opengl_renderer.cpp.
//
// Owns the VkContext and maps the dispatch entry points onto the sys_vk
// frame loop:
//   vkchain_set_render   -> VK_BeginFrame (acquire, open pass, clear)
//   vkchain_render       -> record draws. Raster: suspend the frame pass,
//                           game texture -> mipped RT via one quad,
//                           GenerateMips, resume, composite. Then vector,
//                           then GUI.
//   vkchain_swap_buffers -> VK_EndFrame (submit + present)
// A failed begin (resize, minimize, OUT_OF_DATE) recreates the swapchain and
// skips the rest of that frame; s_frameOpen keeps end-of-frame honest.
// ===========================================================================
#include "vulkan_renderer.h"
#include "sys_log.h"
#include "sys_vk.h"
#include "sys_window.h"
#include "config.h"
#include "mame_vector.h"       // vector_start / vector_clear_list; pulls in
                               // aae_mame_driver.h for Machine and
                               // VIDEO_TYPE_VECTOR (safe direction: driver
                               // headers into the VK TU, not vulkan.h into
                               // core TUs, so the VULKAN_H_ leak guards in
                               // acommon.cpp et al. are unaffected)
#include "fast_poly_vk.h"      // FpolyVK - GUI starfield point renderer
#include "raster_emit.h"       // backend-neutral raster emit loop
#include "raster_tex_vk.h"     // RasterTexVK - streamed game image texture
#include "render_target_vk.h"  // RenderTargetVK - offscreen game RT
#include "screen_quad_vk.h"    // ScreenQuadVK - RT->swapchain composite
#include "crt_post_vk.h"       // CrtPostVK - raster CRT/monitor + scanline overlay
#include "layout_vk.h"         // LayoutQuadVK + the MAME .lay raster compositor
#include "sys_str.h"           // aae_stricmp (raster_effect "NONE" test)
#include "vector_draw_vk.h"    // VectorDrawVK - beam vector renderer;
                               // pulls in vector_draw.h (beam batch access)
#include "vector_post_vk.h"    // VectorPostVK - SSAA RT + trail + glow
#include "shot_draw_vk.h"      // ShotDrawVK - textured vector shots;
                               // pulls in vector_draw_gl.h (txdata,
                               // tex_shot_verts - GL-free header)
#include "../aae_emulator.h"   // emulator_is_gui_active (GUI keeps the direct path)
#include "vk_texture_loader.h" // VkTex_GetSolidWhite (GUI solid quads);
                               // VkArt_* per-game artwork cache
#include "snapshot_vk.h"       // F12 screenshot readback (swapchain -> PNG)
#include "gpu_profiler_vk.h"   // GPU_ZONE - per-pass GPU timing ([main] vk_profile)
#include "vector_fonts.h"      // VF singleton - CPU init under VK
#include "../aae_video/opengl_renderer.h" // GuiPointVertex full definition (GL-free header)
#include <math.h>              // log2f - CRT halation mip bias (GL parity)

static VkContext g_vk;
static bool      s_initialized = false;
static bool      s_frameOpen = false;
static uint32_t  s_imageIndex = 0;

// Raster path: main_bitmap's pixels are emitted into a NATIVE-sized RGBA8
// texture (RasterTexVK) and drawn as ONE NEAREST-magnified quad into the game
// RT (s_rtGame, below). s_rasterTexFailed latches an Init failure so the lazy
// per-frame retry does not spam the log; it resets on every game load.
//
// A texture is used rather than the quad-per-pixel emit the legacy
// fixed-function GL Fpoly path uses: the shared emit loop visits every source
// pixel, and a quad sink turns each one into a quad, so pacman (288x224) would
// rebuild ~64,500 quads / ~258,000 vertices on the CPU and upload them EVERY
// FRAME. Desktop immediate-mode GPUs absorb that; a tiler (Pi 5 / Mesa v3d)
// bins every primitive up front and pacman ran 53 fps there even at prescale 1
// with no shaders. FpolyVK itself stays - the GUI starfield (g_guiPoints)
// still uses it.
//
// TWO dimension pairs, and they are NOT interchangeable - read this before
// touching either:
//
//   s_rasterNativeW/H    the post-orientation game dims in NATIVE source
//                        pixels (raster_dst_dims). This is the GL chain's
//                        vw/vh. It is what the monitor shaders' uSrcSize
//                        wants (a beam-spot radius is measured in GAME
//                        pixels, not texels) and what the letterbox aspect
//                        is computed from.
//
//   s_rasterScaledW/H    native * config.prescale, truncated exactly like
//                        GL's (int)((float)vw * config.prescale). This is
//                        the GL chain's rw/rh: the size of the game RT, the
//                        destination rect of the game quad, and the scanline
//                        overlay's tiling target. It is a SUPERSAMPLED image
//                        - more texels than game pixels.
//
// The RasterTexVK texture is sized from the NATIVE pair; the quad stretches it
// across the SCALED pair with NEAREST filtering, which is where prescale's
// solid blocks now come from (see the prescale note above the draw).
//
// At config.prescale == 1 (the default) the two pairs are equal and every
// consumer sees exactly the numbers it saw before prescale was honored.
static RasterTexVK g_rasterTex;
static bool     s_rasterTexInit = false;
static bool     s_rasterTexFailed = false;
static int      s_rasterNativeW = 0;   // post-orientation dims, NATIVE game pixels
static int      s_rasterNativeH = 0;
static int      s_rasterScaledW = 0;   // native * config.prescale (GL's rw)
static int      s_rasterScaledH = 0;
// Set when a dims-change rebuild is requested while a frame is open (the
// rebuild drains the device and destroys objects prior frames may still
// reference, so it must not run mid-frame). Serviced by vkchain_set_render
// at the next frame boundary, before VK_BeginFrame. Covers BOTH the
// RasterTexVK rebuild and the s_rtGame resize (same trigger, same timing
// constraints).
static bool     s_rasterRebuildPending = false;

// Offscreen game render target: the game quad draws into this
// mipped RT instead of straight into the swapchain pass; ScreenQuadVK then
// composites it into the aspect-fit letterbox rect.
//
// Sized s_rasterScaledW x s_rasterScaledH, i.e. visible_area * config.prescale
// - the GL fbo_raster / img5a shape, allocated by the same truncating math
// (gl_fbo.cpp fbo_init_raster, opengl_renderer.cpp glchain_set_render).
//
// Sizing this RT at unscaled source pixels does NOT work, even though the full
// mip chain covers the DOWNSCALE to the window: anything that samples this
// texture directly still sees the coarser image, which is exactly what the CRT
// monitor passes do. Their 7x3 Gaussian beam taps are spaced in SOURCE-pixel
// units but read whatever detail the texture actually carries, and the
// halation textureLod picks a mip level relative to level 0, so at unscaled
// size both get a prescale-times coarser reconstruction than GL (the mono CRT
// then looks like it is only rendering at prescale 1). The tiled scanline
// overlay has the same problem from the other end: a scan_y-tall pattern
// cannot resolve when the target has 1/prescale as many texels per period.
static RenderTargetVK s_rtGame;

// ---------------------------------------------------------------------------
// Intermediate CRT monitor target - the VK mirror of GL's fbo_mono / img5b.
//
// GL NEVER draws the monitor shader straight to the screen: render_mono_monitor
// / render_color_monitor run img5a -> img5b, where img5b is resized EVERY FRAME
// to the on-screen game rectangle (fbo_resize_mono from Layout_GetScreenPixelSize),
// and then `screenTex = img5b` is handed to Layout_Render, whose screen drawable
// does the compositing: the dual-texture overlay gel multiply AND the rigid
// whole-layout rotation. That indirection is what lets the gel and the rotation
// apply to the MONITOR OUTPUT rather than the raw game image.
//
// This RT reproduces it for the two cases the direct-to-swapchain monitor draw
// cannot express, because CrtPostVK::DrawQuad_ drives its quad from a uvrect
// (which can flip but cannot turn 90 degrees) and its shaders take ONE texture:
//   * an overlay color gel (layout dual-source multiply), and
//   * system rotation (the composite's permuted corner UVs).
// When neither applies the monitor still draws DIRECTLY onto the swapchain -
// one resample fewer.
//
// Format R8G8B8A8_UNORM (never _SRGB): the monitor output is gamma-space bytes
// that the composite must read back unchanged, same contract as s_rtGame.
// Single mip level: it is sized to the exact on-screen pixel count of the quad
// that samples it, so nothing downstream ever minifies it (GL regenerates
// img5b's mips only because img5b starts life at 4x native before the first
// resize lands).
static RenderTargetVK s_rtMonitor;
static bool           s_rtMonitorFailed = false;
// Deferred resize request (dims when a size change is noticed mid-frame).
// Resize destroys the image the OTHER in-flight frame may still be sampling,
// so it must run at a frame boundary - the same discipline, and the same
// servicing point (vkchain_set_render), as s_rasterRebuildPending.
static int  s_rtMonitorPendW = 0;
static int  s_rtMonitorPendH = 0;

// ---------------------------------------------------------------------------
// System-rotation output target (-ror / -rol / 180, config.system_rotation).
//
// GL does display-time rotation with ONE quad: everything (vector composite,
// artwork layers, UI overlays) lands in the square 1024x1024 fbo4 and
// end_render_fbo4 blits it through Rect2, whose UpdateScreenRect swaps the
// quad's per-corner UVs (texrect.cpp indices[32]). This RT is the VK mirror
// of fbo4 for exactly that purpose, and it exists ONLY while a rotation is
// configured: at rotation 0 nothing below is created, entered or drawn, and
// every pre-rotation code path runs untouched.
//
// Format: created with colorFormat UNDEFINED, i.e. the SWAPCHAIN format. That
// is load-bearing, not incidental - g_vectorDraw builds its pipelines against
// ctx.swapchainFormat once at init and cannot legally record into a pass with
// any other format, and the UI overlays must record inside this RT to rotate
// with the frame (GL parity). ScreenQuadVK and VectorPostVK pick their
// variants off VK_ActiveColorFormat, so they simply reuse the swapchain
// variants they already built. s_rtRotFormat catches a swapchain format
// change across a recreate and forces a rebuild.
static RenderTargetVK s_rtRot;
static VkFormat       s_rtRotFormat = VK_FORMAT_UNDEFINED;
static bool           s_rtRotFailed = false;
// True only while draws are being recorded INTO s_rtRot. The overlay seams
// (vkchain_gui_draw_quad / vkchain_ui_dim_quad / ComputeUiOverlayMap) consult
// it to switch from the window letterbox to the square canvas.
static bool           s_rotTargetActive = false;
// The square canvas edge - GL's fbo4 size, and the space every overlay and
// composite coordinate below is already expressed in.
static const int      kRotCanvas = 1024;

// RT->swapchain composite quad. Game-independent (per-format pipeline
// variants are built lazily), so it is initialized ONCE when the VK chain
// comes up (vkchain_init's first-run path only -- no per-frame retry, so a
// failure is a single log line) and shut down in vkchain_shutdown before
// VK_Shutdown. While false, the raster branch records nothing (black
// screen), mirroring the s_rasterTexFailed degradation.
static ScreenQuadVK g_screenQuad;
static bool         s_screenQuadInit = false;

// Raster CRT post chain: the tiled scanline overlay
// (GL render_scanlines) plus the mono/color monitor shaders (GL
// render_mono_monitor / render_color_monitor). Game-independent - the
// scanline pipeline is built against the game RT's RGBA8_UNORM format and the
// monitor pipelines against the swapchain format - so like ScreenQuadVK it is
// initialized once, lazily on the first raster frame, and shut down in
// vkchain_shutdown. s_crtPostFailed latches an Init failure so the per-frame
// retry does not spam the log; it resets on every game load, and on failure
// the raster path falls back to the plain ScreenQuadVK composite (the game
// image with no CRT effects).
static CrtPostVK g_crtPost;
static bool      s_crtPostInit = false;
static bool      s_crtPostFailed = false;

// MAME .lay layout compositor for RASTER games. The GL chain
// composites raster artwork in mame_layout.cpp's Layout_Render, NOT in
// final_render's art_tex[] path (that one is vector-only); this is the quad
// recorder that mirrors it. Game-independent like ScreenQuadVK: one init at
// chain start, one shutdown at the end. When it fails to init - or the game
// has no .lay file - the raster branch keeps its plain aspect-fit letterbox
// composite.
static LayoutQuadVK g_layoutQuad;
static bool         s_layoutQuadInit = false;

// Scanline / raster-effect overlay texture - the VK twin of GL's
// g_scanrezTex. Loaded per-game (and on a live menu change) by
// vkchain_init_raster_overlay, which mirrors glchain_init_raster_overlay's
// gates exactly. s_scanReloadPending defers a mid-frame menu-triggered reload
// to the next frame boundary: the destroy needs a device drain, which must
// never run with a frame open (same constraint as the raster-texture rebuild).
static VkTexture s_scanTex{};
static bool      s_scanHave = false;
static bool      s_scanReloadPending = false;

// Beam vector renderer: draws the frame's BeamLine/BeamJoin/BeamShot batches
// direct to the swapchain inside the open frame pass, the path the GUI uses.
// Game-independent (the pipelines build against the swapchain format), so like
// ScreenQuadVK it is initialized once, lazily on the first vector frame, and
// shut down in vkchain_shutdown. s_vectorFailed latches an Init failure so the
// per-frame retry does not spam the log; it resets on every game load (a later
// load gets a fresh attempt). VectorDrawVK::Init is idempotent, so re-Init on
// a format change would be safe if ever needed.
static VectorDrawVK g_vectorDraw;
static bool         s_vectorInit = false;
static bool         s_vectorFailed = false;

// Vector post chain: NON-GUI vector games render
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

// Textured vector shots: with config.shots_textured set, add_tex diverts every
// shot into the CPU txdata list that only GL draws from, so without this pass
// shots are invisible under VK. ShotDrawVK records that list into the beam
// RT right after the beams (the GL fbo1 analog), so shots pick up glow/
// trail/artwork. Init rides EnsureVectorPost (pipeline is built against the
// beam RT's format); its failure only disables shots, never the whole chain.
static ShotDrawVK g_shotDraw;
static bool       s_shotInit = false;

// GUI starfield: the only FpolyVK instance in the chain, drawing the
// front-end GUI's point-sprite stars (the raster path uses RasterTexVK - see
// the note above). It draws at a different time in the frame (during run_gui(), i.e. inside
// cpu_run() -- BEFORE vkchain_render runs) and into different space (see
// GuiBeamToWindowPx below): stars are
// pre-transformed to explicit window-pixel coordinates on the CPU, so this
// instance's own ortho spans the full swapchain 1:1 (no SetViewportRect
// letterbox override -- the letterbox is already baked into the per-star
// coordinates). Game-independent, so like g_screenQuad it inits once
// (lazily, on the first GUI frame) and persists across game loads.
static FpolyVK g_guiPoints;
static bool    s_guiPointsInit = false;
static bool    s_guiPointsFailed = false;

// VkRasterScaledDims
// The GL raster-FBO shape, computed exactly as GL computes it:
//   rw = (int)((float)vw * config.prescale)     [gl_fbo.cpp fbo_init_raster,
//   rh = (int)((float)vh * config.prescale)      opengl_renderer.cpp:894]
// with vw/vh the POST-orientation native dims (raster_dst_dims applies the
// same ORIENTATION_SWAP_XY transpose GL does). config.prescale is read RAW,
// not clamped, so a fractional value truncates identically to GL.
// sanity_check_config (config.cpp:412) already pins it to 1..5; the >= 1 floor
// below only stops a zero-sized RT if that ever changes.
//
// The emit loop's `size` argument (also config.prescale) is IGNORED by the
// sink: the texture is native-sized and this scaled pair is the quad's
// destination rect, so the magnification does the multiply.
static void VkRasterScaledDims(int nativeW, int nativeH, int& outW, int& outH)
{
	outW = (int)((float)nativeW * config.prescale);
	outH = (int)((float)nativeH * config.prescale);
	if (outW < 1) outW = 1;
	if (outH < 1) outH = 1;
}

// Texture sink for raster_emit_polys. yFlip = 0, so (x,y) are destination-local
// integer coords with y growing DOWNWARD - natural top-down image order, so
// buf[y*w + x] puts the game's TOP scanline in texture row 0, which is the
// row-0 convention everything downstream expects. `size` (= config.prescale)
// is deliberately unused: the texture is NATIVE-sized and the single quad's
// destination rect carries the prescale.
//
// rgba is MAKE_RGBA: R in the low byte, so a uint32_t array is byte-for-byte
// VK_FORMAT_R8G8B8A8_UNORM on a little-endian host (x86-64 and ARM64, the two
// targets). No swizzle, no conversion.
//
// The bounds test is cheap insurance against a geometry desync between
// EnsureRasterRenderer (which sizes the buffer) and this loop; without it a
// mismatch would be a heap overrun rather than a missing pixel.
//
// No sRGB conversion happens anywhere in this path: sys_vk CreateSwapchain
// prefers a UNORM swapchain, so the sRGB-authored pen bytes display
// byte-for-byte as they do in GL's non-sRGB window. An sRGB swapchain would
// re-encode them and wash the picture out.
struct VkRasterTexSinkCtx
{
	uint32_t* buf;
	int       w;
	int       h;
};

static void VkRasterTexSink(void* user, float x, float y, float size, uint32_t rgba)
{
	(void)size;
	const VkRasterTexSinkCtx* c = (const VkRasterTexSinkCtx*)user;
	const int ix = (int)x;
	const int iy = (int)y;
	if ((unsigned)ix < (unsigned)c->w && (unsigned)iy < (unsigned)c->h)
		c->buf[(size_t)iy * (size_t)c->w + (size_t)ix] = rgba;
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

// ---------------------------------------------------------------------------
// VkRotationIndex
// The VK mirror of opengl_renderer.cpp's orientation_to_rect2_rotation (which
// is static there; the GL chain is not touched by this file). Converts the
// user's display-time rotation to the Rect2 index:
//   0 = none, 1 = rotate right (CW 90), 2 = rotate left (CCW 90), 3 = 180.
// ONLY config.system_rotation feeds it: the DRIVER rotation describes the
// cabinet's monitor and is already baked into the game's coordinate
// generation (vector_update's beam transform; raster_emit's orientation
// loop), so it must not be applied a second time at display time.
// ---------------------------------------------------------------------------
static int VkRotationIndex(void)
{
	switch (config.system_rotation)
	{
	case ROT90:  return 1;   // -ror
	case ROT270: return 2;   // -rol
	case ROT180: return 3;
	default:     return 0;
	}
}

// Lazily creates (or rebuilds after a swapchain format change) the square
// output RT. Returns false if it is unavailable, in which case the caller
// falls back to its normal unrotated path - a wrong-way-up picture beats a
// black screen, and the failure is logged once.
static bool EnsureRotTarget(void)
{
	if (s_rtRotFailed)
		return false;
	if (s_rtRot.IsValid() && s_rtRotFormat == g_vk.swapchainFormat)
		return true;

	if (s_rtRot.IsValid())
	{
		// Format changed under a swapchain recreate: the pipelines recorded
		// into this RT are keyed on the format, so it must be rebuilt. Never
		// mid-frame (the destroy would race in-flight reads).
		if (s_frameOpen)
			return false;
		if (g_vk.device && g_vk.vkDeviceWaitIdle_)
			g_vk.vkDeviceWaitIdle_(g_vk.device);
		s_rtRot.Shutdown(g_vk);
	}

	RenderTargetVKCreateInfo ci{};
	ci.width = kRotCanvas;
	ci.height = kRotCanvas;
	ci.filter = rtFilterVK::Linear;
	ci.colorFormat = VK_FORMAT_UNDEFINED;   // = the swapchain format (see s_rtRot)
	ci.mipLevels = 1;                       // blitted 1:1-ish, never minified hard
	if (!s_rtRot.Init(g_vk, ci))
	{
		s_rtRotFailed = true;
		LOG_ERROR("vkchain: rotation output RT init failed; system rotation disabled this session");
		return false;
	}
	s_rtRotFormat = g_vk.swapchainFormat;
	LOG_INFO("vkchain: rotation output RT online (%dx%d, swapchain format %d)",
		kRotCanvas, kRotCanvas, (int)s_rtRotFormat);
	return true;
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
// GUI-space -> window-pixel mapping.
//
// The front-end GUI driver is VIDEO_TYPE_VECTOR, so vkchain_render's vector
// branch (below) already maps beam-space (0..1024 x 0..1024) coordinates to
// the swapchain via the game_rect box + aspect-fit letterbox + one vertical
// flip (see that branch's "Projection" comment for the full derivation).
// GL's GUI draws (VF text,
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
// MakeOrthoColMajor call below: the duplicated letterbox arithmetic is the
// price of leaving that gated, working code path alone.
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
	// Reject only a GENUINELY degenerate rect (zero extent, which would divide
	// by zero downstream). fabsf is load-bearing: video.ini expresses per-game
	// FLIPS as an INVERTED range, and an ordered comparison here silently threw
	// those away and substituted the full box - so every Cinematronics/SegaG80
	// game rendered unflipped (upside down / mirrored) and lost its bezel
	// sizing. Real examples: solarq full_left=1023 full_right=0 (X flip) with
	// full_bottom=1023 full_top=0 (Y flip); starcas full_bottom=976 full_top=7;
	// tacscan/elim2/zektor/armora likewise. Atari titles happen to use ordered
	// ranges (asteroid 0..1024 / -225..1067), which is why only some games
	// looked wrong. GL never had this guard - it hands the raw values to the
	// quad and an inverted range flips it naturally, which is the intent.
	// Out-of-range values (asteroid's -225/1067 overscan) are also deliberate
	// and must NOT be clamped.
	if (fabsf(m.grR - m.grL) < 1.0f) { m.grL = 0.0f; m.grR = 1024.0f; }
	if (fabsf(m.grT - m.grB) < 1.0f) { m.grB = 0.0f; m.grT = 1024.0f; }
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

// ---------------------------------------------------------------------------
// In-game UI overlay mapping (menu / PAUSED / exit dialog / FPS).
//
// GL reference: render_ui_overlays (opengl_renderer.cpp) draws overlay content
// into fbo4's FULL 0..1024 space for vector games (end_render_fbo4's
// screen_rect then letterboxes that square onto ws.aspectRatio), and straight
// onto the backbuffer for raster games with the viewport narrowed to a hard,
// centered 4:3 box when the window is wider than that. Two consequences for
// the VK map:
//  - the box is ALWAYS the default 0..1024 rect, never the per-game
//    game_rect (overlays do not ride the game's CRT-rect quad under GL);
//  - the letterbox aspect is ws.aspectRatio for vector games (fbo4 blit
//    parity) and hard 4:3 for raster games (viewport-narrowing parity).
// s_uiOverlayActive marks the render_ui_overlays() reuse window so the shared
// vkchain_gui_draw_quad seam (menu backgrounds via VF::DrawQuad) picks this
// map instead of the GUI-driver game_rect map.
// ---------------------------------------------------------------------------
static bool s_uiOverlayActive = false;

static GuiBeamMap ComputeUiOverlayMap(void)
{
	GuiBeamMap m{};
	const int sw = (int)g_vk.swapchainExtent.width;
	const int sh = (int)g_vk.swapchainExtent.height;

	// Recording into the square rotation RT: the canvas IS the letterbox, so
	// the identity fit is the whole target. This is exactly GL's rotated
	// overlay path, which draws render_ui_overlays into fbo4's full 0..1024
	// space and lets the (rotated) screen_rect blit do the letterboxing.
	if (s_rotTargetActive)
	{
		m.lx = 0.0f; m.ly = 0.0f;
		m.vw = (float)kRotCanvas; m.vh = (float)kRotCanvas;
		m.grL = 0.0f; m.grR = 1024.0f;
		m.grB = 0.0f; m.grT = 1024.0f;
		return m;
	}

	int vw = sw, vh = sh;
	if (GameIsVector())
	{
		// Vector: aspect-fit box in ws.aspectRatio (fbo4 blit parity).
		float aspect = GetWindowSetup().aspectRatio;
		if (aspect <= 0.0f)
			aspect = 4.0f / 3.0f;
		vh = (int)(sw / aspect + 0.5f);
		if (vh > sh) { vh = sh; vw = (int)(sh * aspect + 0.5f); }
	}
	else
	{
		// Raster: exact GL viewport parity (render_ui_overlays). GL narrows
		// the viewport to a centered 4:3 box ONLY when the window is WIDER
		// than 4:3, and ALWAYS keeps the full window height; a window
		// narrower than 4:3 keeps the whole window and lets the 1024x768
		// ortho stretch. The old code here aspect-FIT a 4:3 box instead,
		// which vertically letterboxed a short landscape strip into every
		// portrait window - i.e. every rotated raster game - shrinking the
		// FPS line, menu and PAUSED text into a small band mid-screen.
		if ((float)sw / (float)sh > (4.0f / 3.0f + 0.01f))
			vw = (int)(sh * (4.0f / 3.0f) + 0.5f);
	}
	if (vw < 1) vw = 1;
	if (vh < 1) vh = 1;

	m.lx = (float)((sw - vw) / 2);
	m.ly = (float)((sh - vh) / 2);
	m.vw = (float)vw;
	m.vh = (float)vh;
	m.grL = 0.0f; m.grR = 1024.0f;
	m.grB = 0.0f; m.grT = 1024.0f;
	return m;
}

// Lazy once-per-session init of the beam renderer (see the g_vectorDraw
// comment). Defaults resolve to shaders/vk/vector_{line,disc,shot}_vk
// CustomBuild output; colorFormat is left UNDEFINED so the pipelines build
// against the swapchain format (this instance draws direct to the swapchain);
// ssaa=1, since there is no supersampled RT on this path.
// beam_init() is deliberately NOT called: it is GL-only (compiles shaders,
// builds VAOs/VBOs), and the CPU-side beam arrays need no init.
//
// NOT gated on GameIsVector(): raster games need this renderer too, for the
// in-game UI overlay text (RecordUiOverlays below) - VF glyph strokes ride
// the same beam queue on every game type under Vulkan. The pipelines are
// game-independent (swapchain format), so one lazy init serves the session.
static void EnsureVectorRenderer(void)
{
	if (s_vectorFailed || s_vectorInit)
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
// renderer (see the g_vectorPost comment).
//
// The beam RT is 1024 * vk_ssaa square and the composite's trilinear
// minification is the supersample resolve; the AA feather divide keeps beam
// widths matching the GL look at either setting.
//
// The factor is [main] vk_ssaa, defaulting to 1 for GL parity - GL's
// beam_init(1) uses a plain 1024 buffer. At 2 the VK chain does FOUR TIMES the
// beam fill plus a 4x larger per-frame mip cascade, which measures as roughly
// a 6x frame-rate difference on the same GPU (1400 fps GL vs 217 fps VK,
// asteroid deluxe, RTX 4070 at 4K). Set 2 for the smoother 2048x2048 buffer
// only where there is headroom for it.
static void EnsureVectorPost(void)
{
	if (s_vecPostFailed || s_vecPostInit)
		return;

	VectorPostVKCreateInfo pci{};
	pci.ssaa = (config.vk_ssaa == 2) ? 2 : 1;   // sanity_check_config pins it to 1 or 2
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

	// Textured shots: non-fatal - a failure just means shots fall back to
	// invisible while everything else works.
	ShotDrawVKCreateInfo sci{};
	sci.colorFormat = g_vectorPost.BeamFormat();
	if (g_shotDraw.Init(g_vk, &sci))
		s_shotInit = true;
	else
		LOG_ERROR("vkchain: ShotDrawVK init failed; textured shots disabled");
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

// Mirrors EnsureVectorList for the raster path: (re)build the game image
// texture whenever a raster game needs one. Handles fresh init, a raster
// game following a vector game (s_rasterTexInit false), and a raster game with
// DIFFERENT post-orientation dims following another raster game (Shutdown +
// re-Init so the texture matches the new game).
//
// Called from both vkchain_init paths AND from vkchain_render's raster
// block. The render-path call is required, not belt-and-braces: run_game
// calls init_gl (Step 4, aae_emulator.cpp:920) before vh_open (Step 11,
// :1017) creates main_bitmap, so raster_dst_dims() returns 0 during
// vkchain_init on a fresh load and the real init happens lazily on the
// first rendered frame.
// Machine->gamedrv/Machine->drv ARE populated by init_gl time (run_game
// logs gamedrv->desc at :874 and reads drv->rotation at :887), so
// GameIsRaster and the dims math are safe in both call sites. Init failure latches s_rasterTexFailed (reset per game
// load in vkchain_init) so the per-frame call does not retry-spam.
static void EnsureRasterRenderer(void)
{
	if (!GameIsRaster() || s_rasterTexFailed)
		return;

	int newW = 0, newH = 0;
	if (!raster_dst_dims(&newW, &newH))
		return;     // main_bitmap not created yet (pre-vh_open); retry later

	// Prescaled shape too: config.prescale is re-read from the per-game ini
	// on every load (config.cpp:252), so it can differ between two games of
	// the SAME native shape - and, pathologically, two games of different
	// native shapes can land on the same prescaled shape. Both pairs are
	// therefore part of the up-to-date test.
	int newScaledW = 0, newScaledH = 0;
	VkRasterScaledDims(newW, newH, newScaledW, newScaledH);

	if (s_rasterTexInit &&
		newW == s_rasterNativeW && newH == s_rasterNativeH &&
		newScaledW == s_rasterScaledW && newScaledH == s_rasterScaledH)
		return;     // up to date (four int compares; cheap on the frame path)

	if (s_rasterTexInit)
	{
		// Game shape changed: rebuild. In-flight frames may still be sampling
		// the old texture images (and the old RT image), so the device is
		// drained first. That drain (and the destroys) must never run while a
		// frame is open, so a mid-frame request is deferred to the next frame
		// boundary (vkchain_set_render services the flag before
		// VK_BeginFrame).
		if (s_frameOpen)
		{
			s_rasterRebuildPending = true;
			return;
		}
		if (g_vk.device && g_vk.vkDeviceWaitIdle_)
			g_vk.vkDeviceWaitIdle_(g_vk.device);
		g_rasterTex.Shutdown(g_vk);
		s_rasterTexInit = false;
	}
	s_rasterRebuildPending = false;

	s_rasterNativeW = newW;
	s_rasterNativeH = newH;
	s_rasterScaledW = newScaledW;
	s_rasterScaledH = newScaledH;

	// (Re)create the offscreen game RT. It stays PRESCALED (native *
	// config.prescale) while the source texture below is NATIVE - that split
	// is what preserves prescale's supersampled detail for the CRT/scanline
	// consumers while the per-frame upload stays native-sized.
	//
	// Format trace: pen bytes are written to this R8G8B8A8_UNORM RT
	// byte-for-byte (UNORM attachment: no encode), RecordRect samples the
	// UNORM view (raw bytes back as the same floats) and writes them to the
	// UNORM swapchain (sys_vk CreateSwapchain prefers UNORM) - ZERO encodes
	// anywhere, so the presented bytes equal the GL chain's exactly. An _SRGB
	// RT here would decode-on-sample and shift every mid-tone; an sRGB
	// swapchain would encode on store and wash the picture out.
	if (!s_rtGame.IsValid())
	{
		RenderTargetVKCreateInfo rtci{};
		rtci.width = s_rasterScaledW;
		rtci.height = s_rasterScaledH;
		rtci.filter = rtFilterVK::Linear;
		rtci.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
		rtci.mipLevels = -1;   // full chain: CRT halation + minified composite
		if (!s_rtGame.Init(g_vk, rtci))
		{
			s_rasterTexFailed = true;
			LOG_ERROR("vkchain: game RenderTargetVK init failed; raster game will show black");
			return;
		}
	}
	else if (s_rtGame.GetWidth() != s_rasterScaledW || s_rtGame.GetHeight() != s_rasterScaledH)
	{
		// Resize waits device-idle internally (the other in-flight frame may
		// still be sampling the old image) -- redundant
		// with the drain above on the rebuild path, but harmless, and it
		// keeps Resize safe for any future caller.
		s_rtGame.Resize(g_vk, s_rasterScaledW, s_rasterScaledH);
		if (!s_rtGame.IsValid())
		{
			s_rasterTexFailed = true;
			LOG_ERROR("vkchain: game RenderTargetVK resize failed; raster game will show black");
			return;
		}
	}

	// The streamed game image: NATIVE dims, one image + one staging buffer per
	// frame-in-flight, created ONCE here and only updated per frame. No
	// per-frame create/destroy, so no allocation churn and no chance of
	// destroying an image an in-flight frame is still sampling.
	if (g_rasterTex.Init(g_vk, (uint32_t)s_rasterNativeW, (uint32_t)s_rasterNativeH))
	{
		s_rasterTexInit = true;
		LOG_INFO("vkchain: raster game texture online (%dx%d native x%.1f prescale = %dx%d into %s RT, %u mips)",
			s_rasterNativeW, s_rasterNativeH, config.prescale,
			s_rasterScaledW, s_rasterScaledH,
			(s_rtGame.GetFormat() == VK_FORMAT_R8G8B8A8_UNORM) ? "RGBA8_UNORM" : "non-UNORM",
			s_rtGame.GetMipLevels());
	}
	else
	{
		s_rasterTexFailed = true;
		LOG_ERROR("vkchain: raster game texture init failed; raster game will show black");
	}
}

// ---------------------------------------------------------------------------
// Raster CRT post chain - lazy init + the per-frame gates.
//
// The gates below are line-for-line twins of the GL ones in
// opengl_renderer.cpp; keep them in sync. config is read every frame (never
// cached) because the in-game menus mutate these fields live, exactly as the
// GL path re-uploads every uniform each frame.
// ---------------------------------------------------------------------------
static void EnsureCrtPost(void)
{
	if (s_crtPostInit || s_crtPostFailed)
		return;
	// Needs the game RT's format for the scanline pipeline variant.
	if (!s_rtGame.IsValid())
		return;

	CrtPostVKCreateInfo ci{};
	ci.gameRtFormat = s_rtGame.GetFormat();
	if (g_crtPost.Init(g_vk, &ci))
	{
		s_crtPostInit = true;
		LOG_INFO("vkchain: CrtPostVK online (raster scanline + monitor shaders)");
	}
	else
	{
		s_crtPostFailed = true;
		LOG_ERROR("vkchain: CrtPostVK init failed; raster CRT effects disabled");
	}
}

// Monitor phosphor tint presets, indexed by config.mono_tint. Verbatim from
// opengl_renderer.cpp's k_monoTints.
static const float k_vkMonoTints[3][3] = {
	{ 1.00f, 1.00f, 1.00f },   // 0: P4 white
	{ 0.30f, 1.00f, 0.40f },   // 1: P1 green
	{ 1.00f, 0.75f, 0.20f },   // 2: P3 amber
};

// GL twin: the scanline-overlay test in final_render_raster
// (opengl_renderer.cpp:1524).
static bool VkScanlinesActive(int vattr)
{
	return s_scanHave && s_crtPostInit && Machine && Machine->drv &&
		(g_scanline_override == 1 ||
			(g_scanline_override == 0 && !(vattr & VIDEO_TYPE_RASTER_BW)));
}

// GL twin: mono_monitor_active (opengl_renderer.cpp:1300). The GL test also
// requires fragMonoMonitor and fbo_mono to exist; s_crtPostInit is the VK
// equivalent of "the shader and its target came up".
static bool VkMonoMonitorActive(int vattr)
{
	return config.mono_enable != 0 && s_crtPostInit &&
		(vattr & VIDEO_TYPE_RASTER_BW) != 0;
}

// GL twin: color_monitor_active (opengl_renderer.cpp:1411), including the
// "a selected texture overlay stands the shader down" rule.
static bool VkColorMonitorActive(int vattr)
{
	const bool overlay_selected = config.raster_effect && config.raster_effect[0] &&
		aae_stricmp(config.raster_effect, "NONE") != 0;

	return !overlay_selected && config.color_enable != 0 && s_crtPostInit &&
		(vattr & VIDEO_TYPE_RASTER_COLOR) != 0;
}

// ---------------------------------------------------------------------------
// EnsureMonitorTarget - GL fbo_resize_mono (gl_fbo.cpp:424).
//
// Lazily creates, and thereafter tracks the size of, the intermediate monitor
// RT. w/h are the on-screen pixel dims of the quad that will sample it, in the
// monitor image's OWN (pre-rotation) frame - see the sizing derivation at the
// call site. Clamp range is GL's, verbatim.
//
// Returns false when the target is unusable this frame, in which case the
// caller falls back to compositing the raw game RT - i.e. exactly what the
// chain did before the monitor pass existed.
// ---------------------------------------------------------------------------
static bool EnsureMonitorTarget(int w, int h)
{
	if (s_rtMonitorFailed)
		return false;

	// GL fbo_resize_mono's clamps, so a degenerate window behaves the same way.
	if (w < 64)   w = 64;
	if (h < 64)   h = 64;
	if (w > 4096) w = 4096;
	if (h > 4096) h = 4096;

	if (!s_rtMonitor.IsValid())
	{
		// First use: a create destroys nothing, so it is safe with a frame
		// open (unlike the resize below).
		RenderTargetVKCreateInfo ci{};
		ci.width = w;
		ci.height = h;
		ci.filter = rtFilterVK::Linear;
		ci.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;   // never _SRGB (see s_rtMonitor)
		ci.mipLevels = 1;                            // sampled 1:1, never minified
		if (!s_rtMonitor.Init(g_vk, ci))
		{
			s_rtMonitorFailed = true;
			LOG_ERROR("vkchain: CRT monitor RT init failed; gel/rotated monitor pass disabled");
			return false;
		}
		s_rtMonitorPendW = 0;
		s_rtMonitorPendH = 0;
		LOG_INFO("vkchain: CRT monitor RT online (%dx%d RGBA8_UNORM)", w, h);
		return true;
	}

	if (s_rtMonitor.GetWidth() == w && s_rtMonitor.GetHeight() == h)
		return true;

	// Size changed (window resize, aspect override, a bezel/artcrop toggle
	// moving the layout camera). Resize drains the device and destroys the
	// old image, which the other in-flight frame may still be sampling, so a
	// mid-frame request is deferred to the next frame boundary exactly as
	// EnsureRasterRenderer defers its rebuild. This frame keeps the previous
	// composite - one frame of the pre-fix picture on a resize, never a
	// use-after-free.
	if (s_frameOpen)
	{
		s_rtMonitorPendW = w;
		s_rtMonitorPendH = h;
		return false;
	}

	s_rtMonitorPendW = 0;
	s_rtMonitorPendH = 0;
	s_rtMonitor.Resize(g_vk, w, h);
	if (!s_rtMonitor.IsValid())
	{
		s_rtMonitorFailed = true;
		LOG_ERROR("vkchain: CRT monitor RT resize failed; gel/rotated monitor pass disabled");
		return false;
	}
	LOG_INFO("vkchain: CRT monitor RT resized to %dx%d", w, h);
	return true;
}

// ---------------------------------------------------------------------------
// FillMonitorParams - the per-frame config -> CrtMonitorParamsVK copy, shared
// by the offscreen and direct monitor routes (GL re-uploads every uniform
// every frame for the same reason: the menus mutate config live).
// ---------------------------------------------------------------------------
static void FillMonitorParams(bool monoPass, CrtMonitorParamsVK& mp)
{
	// uSrcSize: the game's visible area in NATIVE pixels, oriented - NOT the
	// game RT size, which is now prescale times larger. GL does exactly this:
	// render_mono_monitor / render_color_monitor derive vw/vh straight from
	// visible_area + ORIENTATION_SWAP_XY and pass those, even though img5a is
	// the prescaled texture (opengl_renderer.cpp:1312-1321, :1355). The
	// shader's `px = 1.0 / uSrcSize` is the beam-tap spacing, and a beam spot
	// is a fixed size in GAME pixels regardless of how finely the source is
	// sampled; prescale only buys the taps more detail to land on.
	mp.srcW = (float)s_rasterNativeW;
	mp.srcH = (float)s_rasterNativeH;
	// uLodBias: the source texture is native * prescale, so a halation radius
	// expressed in game pixels sits log2(prescale) levels up the pyramid.
	// GL's line verbatim (opengl_renderer.cpp:1352/1360, :1463/1469),
	// including the >1 clamp that keeps the bias non-negative.
	{
		const float ps = (config.prescale > 1.0f) ? config.prescale : 1.0f;
		mp.lodBias = log2f(ps);
	}

	if (monoPass)
	{
		const int ti = (config.mono_tint >= 0 && config.mono_tint <= 2)
			? config.mono_tint : 0;
		mp.blurH     = config.mono_blur_h;
		mp.blurV     = config.mono_blur_v;
		mp.halation  = config.mono_halation;
		mp.halRadius = config.mono_halation_radius;
		mp.scanline  = config.mono_scanline;
		mp.contrast  = config.mono_contrast;
		mp.bright    = config.mono_brightness;
		mp.tint[0]   = k_vkMonoTints[ti][0];
		mp.tint[1]   = k_vkMonoTints[ti][1];
		mp.tint[2]   = k_vkMonoTints[ti][2];
	}
	else
	{
		mp.blurH        = config.color_blur_h;
		mp.blurV        = config.color_blur_v;
		mp.converge     = config.color_converge;
		mp.halation     = config.color_halation;
		mp.halRadius    = config.color_halation_radius;
		mp.scanline     = config.color_scanline;
		mp.contrast     = config.color_contrast;
		mp.bright       = config.color_brightness;
		mp.saturation   = config.color_saturation;
		mp.maskType     = (float)config.color_mask_type;
		mp.maskStrength = config.color_mask_strength;
		mp.maskScale    = config.color_mask_scale;
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
		// New game load: allow a fresh raster-texture init attempt even if the
		// previous game's init failed, then rebuild if the shape changed
		// (raster->raster with different dims) or init for the first time
		// (raster after vector). Usually defers to the render-path call
		// because main_bitmap does not exist yet at this point in run_game.
		s_rasterTexFailed = false;
		EnsureRasterRenderer();
		// Same latch-reset for the beam renderer: a later load gets a
		// fresh init attempt (the renderer itself persists across games).
		s_vectorFailed = false;
		// Post chain: fresh init attempt per load, and clear the trail
		// accumulator once so the previous game's phosphor never ghosts.
		s_vecPostFailed = false;
		s_trailClearPending = true;
		// CRT post chain: fresh init attempt per load, same discipline. The
		// monitor RT's failure latch clears with it; the target itself
		// persists (it is game-independent - sized to the on-screen rect,
		// which EnsureMonitorTarget re-checks every frame anyway).
		s_crtPostFailed = false;
		s_rtMonitorFailed = false;
		EnsureCrtPost();
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

	// Same lifetime as ScreenQuadVK. A failure here only costs raster artwork:
	// LayoutVK_Active() is consulted alongside s_layoutQuadInit, so the chain
	// falls back to the plain letterbox composite.
	if (g_layoutQuad.Init(g_vk, nullptr))
		s_layoutQuadInit = true;
	else
		LOG_ERROR("vkchain_init: LayoutQuadVK init failed; raster artwork disabled");

	EnsureVectorList();
	s_rasterTexFailed = false;
	EnsureRasterRenderer();   // usually defers (see helper comment)
	s_vectorFailed = false;   // beam renderer inits lazily on the first vector frame
	s_vecPostFailed = false;  // post chain likewise
	s_crtPostFailed = false;  // raster CRT post chain likewise
	EnsureCrtPost();          // defers until the game RT exists
	s_trailClearPending = true;

	// GPU section profiler ([main] vk_profile, default 0). Creates the query
	// pool and latches timestampPeriod; returns false and stays completely
	// inert when the key is 0 or the device cannot timestamp on this queue.
	GpuProf::Init(g_vk, config.vk_profile != 0);

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
	// RasterTexVK, ScreenQuadVK and the game RT all own device objects, so they
	// must go before VK_Shutdown (which destroys the device). VK_Shutdown
	// waits device-idle internally, but buffer/pipeline destruction here
	// races nothing: the app is tearing down and no frame is open (winmain
	// drains before shutdown). One drain up front covers all three.
	if (g_vk.device && g_vk.vkDeviceWaitIdle_)
		g_vk.vkDeviceWaitIdle_(g_vk.device);
	if (s_rasterTexInit)
	{
		g_rasterTex.Shutdown(g_vk);
		s_rasterTexInit = false;
	}
	if (s_screenQuadInit)
	{
		g_screenQuad.Shutdown(g_vk);
		s_screenQuadInit = false;
	}
	if (s_layoutQuadInit)
	{
		g_layoutQuad.Shutdown(g_vk);
		s_layoutQuadInit = false;
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
	if (s_shotInit)
	{
		g_shotDraw.Shutdown(g_vk);
		s_shotInit = false;
	}
	if (s_guiPointsInit)
	{
		g_guiPoints.Shutdown(g_vk);
		s_guiPointsInit = false;
	}
	s_guiPointsFailed = false;
	if (s_crtPostInit)
	{
		g_crtPost.Shutdown(g_vk);
		s_crtPostInit = false;
	}
	s_crtPostFailed = false;
	if (s_scanHave)
	{
		VK_DestroyTexture(g_vk, s_scanTex);
		s_scanTex = VkTexture{};
		s_scanHave = false;
	}
	s_scanReloadPending = false;
	VkArt_FreeAll(g_vk);         // per-game artwork textures
	LayoutVK_FreeTextures(g_vk); // per-game .lay layout textures
	VkTex_ShutdownCache(g_vk);   // GUI solid-white texture + any cached loads
	if (s_rtGame.IsValid())
		s_rtGame.Shutdown(g_vk);
	if (s_rtMonitor.IsValid())
		s_rtMonitor.Shutdown(g_vk);
	s_rtMonitorFailed = false;
	s_rtMonitorPendW = 0;
	s_rtMonitorPendH = 0;
	if (s_rtRot.IsValid())
		s_rtRot.Shutdown(g_vk);
	s_rtRotFormat = VK_FORMAT_UNDEFINED;
	s_rtRotFailed = false;
	s_rotTargetActive = false;
	s_rasterTexFailed = false;
	s_rasterRebuildPending = false;
	s_rasterNativeW = 0;
	s_rasterNativeH = 0;
	s_rasterScaledW = 0;
	s_rasterScaledH = 0;
	VkSnapshot::DropPending("renderer shutting down");
	GpuProf::Shutdown(g_vk);     // query pool, before the device goes
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
		// A screenshot latched just before the swapchain went away can never
		// be serviced from this frame; drop it instead of firing it whenever
		// the window comes back.
		VkSnapshot::DropPending("swapchain deferred (window minimized?)");

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

	// Service a deferred raster-texture rebuild here, where no frame is open (the
	// s_frameOpen early-out above guarantees it), so the device drain and
	// object destruction never overlap an open frame.
	if (s_rasterRebuildPending)
	{
		s_rasterRebuildPending = false;
		EnsureRasterRenderer();
	}

	// Same treatment for a deferred CRT monitor RT resize (window resize, an
	// aspect override, a bezel/artcrop toggle moving the layout camera): the
	// recreate destroys an image an in-flight frame may still be sampling.
	if (s_rtMonitorPendW > 0 && s_rtMonitorPendH > 0)
	{
		const int pw = s_rtMonitorPendW;
		const int ph = s_rtMonitorPendH;
		s_rtMonitorPendW = 0;
		s_rtMonitorPendH = 0;
		EnsureMonitorTarget(pw, ph);   // no frame open here: takes the resize path
	}

	// Same treatment for a menu-triggered scanline-overlay reload: the destroy
	// needs a device drain, which must not overlap an open frame.
	if (s_scanReloadPending)
	{
		s_scanReloadPending = false;
		vkchain_init_raster_overlay();
	}

	if (!VK_BeginFrame(g_vk, s_imageIndex))
	{
		RecreateSwapchainOrDefer();
		return;             // skip this frame; next tick re-acquires
	}
	s_frameOpen = true;

	// GPU profiler frame boundary ([main] vk_profile; inert at 0). This is the
	// once-per-frame site where the slot's fence wait has just proven the
	// slot's PREVIOUS submission complete, which is exactly the precondition
	// for reading back that submission's timestamps - so BeginFrame collects
	// here, then resets this slot's query range, then opens the "frame"
	// section. The reset needs no pass open, which is why BeginFrame brackets
	// it in VK_SuspendFramePass/VK_ResumeFramePass (VK_BeginFrame above leaves
	// the swapchain pass OPEN).
	GpuProf::BeginFrame(g_vk, g_vk.cmdBuffers[g_vk.frameIndex],
		g_vk.frameIndex, s_imageIndex);

	// Deterministic per-frame slot-cursor reset (see the ScreenQuadVK header):
	// once per frame, right after VK_BeginFrame has fixed the frame slot and
	// its fence wait has proven the slot's previous frame is done. set_render
	// IS that once-per-frame site.
	if (s_screenQuadInit)
		g_screenQuad.OnFrameBegin(g_vk.frameIndex);
	if (s_layoutQuadInit)
		g_layoutQuad.OnFrameBegin(g_vk.frameIndex);
	if (s_crtPostInit)
		g_crtPost.OnFrameBegin(g_vk.frameIndex);

	// Beam renderer slot reset: drain the slot's retired
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

// ---------------------------------------------------------------------------
// RecordUiOverlays - in-game UI overlays (pause dim, PAUSED text, exit
// confirmation dialog, TAB menu, FPS counter, debug/error overlays) for BOTH
// game types, GL parity via the shared render_ui_overlays() content:
//
//  1. render_ui_overlays() runs the exact GL overlay content (its raw GL
//     calls are guarded by active_renderer() inside). Under Vulkan the VF
//     text accumulates in the CPU beam queue, VF::DrawQuad menu backgrounds
//     route through vkchain_gui_draw_quad, and the dim quads route through
//     vkchain_ui_dim_quad - both picking the overlay map below via
//     s_uiOverlayActive. It also runs video_loop()'s frame logic (menu
//     auto-pause, LED refresh, debug adjust), which GL runs every frame.
//  2. The queued glyph strokes are then drawn direct-to-swapchain with
//     g_vectorDraw (the swapchain-format instance the GUI path proved),
//     inside the still-open frame pass, using the overlay map folded into
//     one inverted ortho - the same derivation as the vector branch's
//     "Projection" comment, with the default 0..1024 box.
//
// Coordinate parity trace (VF.Print y=Y, 16:9 window, vector game):
//   GL: VF ortho 0..768 y-up into fbo4's full 1024 viewport -> fbo4 row
//       Y*1024/768 (y-up); screen_rect letterboxes fbo4 (no flip) -> window
//       y-up = ly + (Y*1024/768)/1024 * vh.
//   VK: VF::End emits y_beam = (768-Y)*1024/768; the map's fy = grT -
//       y_beam = Y*1024/768; window y-up = ly + fy/1024 * vh. Identical.
//
// Deliberately NOT routed through the vector post chain's beam RT: overlays
// would inherit trail/glow and freeze while paused. Direct-to-swapchain
// matches GL's crisp post-composite overlay draw. Blend is alpha-over
// (additive=false), matching GL's VF::End (always alpha-over for text).
//
// Known accepted deviations from GL (documented, cosmetic):
//  - the dim rect covers exactly the letterbox box; GL's raster-window dim
//    covers the full window height of the 4:3 strip (same thing whenever
//    the window is 4:3 or wider);
// (A third deviation - vertically letterboxing the overlay when the window
// is NARROWER than 4:3 instead of GL's full-height stretch - was removed
// 2026-08-03: every rotated raster game has a portrait window, so it shrank
// the FPS/menu/PAUSED overlays into a small mid-screen band. The raster
// branch of ComputeUiOverlayMap now reproduces GL's viewport rule exactly.)
//
// System rotation: both game types now route this function's output through
// the square s_rtRot canvas and one rotated blit, so the overlays turn with
// the game exactly as they do under GL (which gets it free from fbo4). The
// switch is s_rotTargetActive, consulted here and by the two quad seams.
// ---------------------------------------------------------------------------
static void RecordUiOverlays(void)
{
	if (!s_frameOpen || !Machine || !Machine->drv)
		return;

	// One zone wherever this runs - direct-to-swapchain, or inside the square
	// rotation RT on the two rotated paths. vkCmdWriteTimestamp is legal inside
	// a render pass, so no suspension is needed for either case.
	GPU_ZONE("ui");

	// Raster games need the beam renderer online too (overlay text).
	EnsureVectorRenderer();

	// Overlay isolation (see the ownership note at the tail of vkchain_render).
	// VectorFont::End routes glyph strokes through the SAME global beam queue
	// the game's RETAINED beams live in, so stash the game's batches aside for
	// the duration: the overlays emit into empty arrays, get recorded alone,
	// and the pop below discards them and swaps the game's data back
	// bit-exactly. Without this the overlay Record would redraw the whole game
	// geometry direct-to-swapchain (unprocessed - no glow/trail) and the drain
	// would wipe the retained set the NEXT frame needs.
	beam_stash_push();

	// Lazily-opened swapchain pass: the overlay Record below targets the
	// swapchain unless the rotation canvas is open (which is its own offscreen
	// pass, already open). On a frame where nothing else drew to the swapchain
	// this is the first open.
	if (!s_rotTargetActive)
		VK_EnsureFramePass(g_vk, g_vk.cmdBuffers[g_vk.frameIndex]);

	s_uiOverlayActive = true;
	render_ui_overlays(1024, 768, false);
	s_uiOverlayActive = false;

	// Target framebuffer: the square rotation RT while it is open (so the
	// overlays turn with the frame, GL's fbo4 behavior), the swapchain
	// otherwise. The map above follows the same switch.
	const int sw = s_rotTargetActive ? kRotCanvas : (int)g_vk.swapchainExtent.width;
	const int sh = s_rotTargetActive ? kRotCanvas : (int)g_vk.swapchainExtent.height;
	if (s_vectorInit && sw > 0 && sh > 0)
	{
		// Fold letterbox + default box + the single V flip into one ortho by
		// inverting the beam->window map at the swapchain edges (see the
		// vector branch). With the default box (grL=0, grR-grL=1024) the
		// box term cancels for x; y keeps the grT - fy flip.
		const GuiBeamMap m = ComputeUiOverlayMap();
		const float ol = (0.0f - m.lx) * 1024.0f / m.vw;        // beam x at window left
		const float orr = ((float)sw - m.lx) * 1024.0f / m.vw;  // beam x at window right
		const float fy0 = (0.0f - m.ly) * 1024.0f / m.vh;       // fbo4 y at window bottom (y-up)
		const float fy1 = ((float)sh - m.ly) * 1024.0f / m.vh;  // fbo4 y at window top
		const float ob = m.grT - fy0;                           // beam y at window bottom
		const float ot = m.grT - fy1;                           // beam y at window top

		float proj[16];
		MakeOrthoColMajor(ol, orr, ob, ot, proj);

		VkCommandBuffer cmd = g_vk.cmdBuffers[g_vk.frameIndex];
		// 0/0 = record against the swapchain extent; the rotated path names
		// the square RT explicitly. g_vectorDraw's pipelines are built for the
		// swapchain format, which is exactly what the rotation RT carries.
		g_vectorDraw.Record(g_vk, cmd, g_vk.frameIndex, proj,
			/*additive=*/false,
			s_rotTargetActive ? (uint32_t)kRotCanvas : 0u,
			s_rotTargetActive ? (uint32_t)kRotCanvas : 0u);
	}

	// Discard the overlay strokes (or the orphaned queue if Record was
	// skipped) and restore the game's retained batches. Pure CPU, no copying.
	// NOT cache_clear(): the legacy textured-shot list (texlist) is retained
	// across frames exactly like the beam batches, and the overlays never
	// write to it.
	beam_stash_pop();
}

void vkchain_render(void)
{
	// Display-time system rotation (-ror / -rol / 180). Resolved once here so
	// every branch below agrees, and so the whole feature is one compare away
	// from being provably inert: at rot == 0 every `rotActive` below is false
	// and no rotation code records anything.
	const int rot = VkRotationIndex();
	// Set when the rotated path has already run the UI overlays (inside the
	// output RT, so they turn with the frame); stops the tail of this function
	// running render_ui_overlays a second time.
	bool uiOverlaysDone = false;

	// Raster path: suspend the swapchain frame pass, render the game into the
	// mipped offscreen RT (one textured quad), regenerate its mip chain, resume
	// the frame pass, and composite the RT into the aspect-fit letterbox rect
	// via ScreenQuadVK. The scanline/CRT passes slot in between the game draw
	// and the composite; when the game has a .lay file the layout system
	// replaces the RecordRect composite (and brings the aspect overrides).
	// Display-time system rotation is handled here: the composite quad takes
	// the inverted aspect box and rotated corner UVs. Vector and GUI follow.
	if (s_frameOpen && GameIsRaster())
	{
		// Lazy init/rebuild: main_bitmap does not exist yet when
		// vkchain_init runs (see EnsureRasterRenderer's comment), so the
		// first frame of a raster game lands here with s_rasterTexInit false.
		// Called unconditionally (dims-equal early-out is two int compares)
		// so a stale ortho can never survive a session; a dims-change
		// rebuild requested here is deferred to the next frame boundary.
		EnsureRasterRenderer();
		EnsureCrtPost();   // needs the game RT's format; no-op once online

		if (s_rasterTexInit && s_rtGame.IsValid() && s_screenQuadInit)
		{
			VkCommandBuffer cmd = g_vk.cmdBuffers[g_vk.frameIndex];
			const int vattr = (Machine && Machine->drv) ? Machine->drv->video_attributes : 0;

			// -------------------------------------------------------------
			// Frame geometry, resolved BEFORE the pass work begins.
			//
			// The intermediate monitor pass (below) has to open and close its
			// own render pass, which is only legal inside the suspended-pass
			// window - so the decision of WHETHER to run it, and at what size,
			// cannot wait for the composite section. Everything hoisted here
			// is pure computation over the swapchain extent, the raster dims,
			// rot and config; none of it records commands.
			// -------------------------------------------------------------

			// Aspect-fit letterbox: fit the
			// post-orientation game rect into the swapchain, centered.
			// RecordRect takes the rect in y-up screen pixels, but a
			// centered rect is symmetric, so the same offsets serve.
			//
			// System rotation: s_rasterNativeW/H are post-DRIVER-orientation
			// only (raster_emit reads Machine->drv->rotation, which run_game
			// never XORs the user rotation into - that goes to
			// Machine->orientation), so a 90-degree system rotation turns the
			// image at DISPLAY time here: the fitted box takes the INVERTED
			// aspect and the composite quad samples with rotated corner UVs.
			// Same split as the GL chain, where the game image is emitted
			// driver-oriented into the raster FBO and Layout_Render turns the
			// whole composition.
			//
			// NATIVE dims, not the prescaled RT dims: prescale is a uniform
			// scale on both axes so the ratio is the same either way, but the
			// native pair is the one GL's layout math uses
			// (Layout_InitGameDimensions, mame_layout.cpp:1494) and it avoids
			// carrying the RT's per-axis integer truncation into the aspect.
			float gameAspect = (float)s_rasterNativeW / (float)s_rasterNativeH;
			if (rot == 1 || rot == 2)
				gameAspect = 1.0f / gameAspect;
			const int sw = (int)g_vk.swapchainExtent.width;
			const int sh = (int)g_vk.swapchainExtent.height;
			int vw = sw, vh = (int)(sw / gameAspect + 0.5f);
			if (vh > sh) { vh = sh; vw = (int)(sh * gameAspect + 0.5f); }
			const float lx = (float)((sw - vw) / 2);
			const float ly = (float)((sh - vh) / 2);

			// PHASE 1 (GL final_render_raster): the MAME .lay layout
			// composite. GL hands the whole raster frame to Layout_Render,
			// which fits the view, then draws backdrop -> screen (with the
			// overlay color gel multiplied in) -> bezel, and owns the display
			// rotation for all of them. LayoutVK_ComputeFrame resolves the
			// same geometry; when it declines (this game has no .lay, or the
			// compositor failed to init) the chain keeps the plain aspect-fit
			// letterbox composite.
			LayoutVKFrame lay{};
			const bool layoutActive = s_layoutQuadInit &&
				LayoutVK_ComputeFrame(sw, sh, lay) && lay.hasScreen;

			// Monitor rect: the layout's SCREEN element when a layout is
			// active (so the backdrop and bezel frame it), otherwise the plain
			// letterbox. CrtPostVK takes y-DOWN pixels, which is exactly what
			// LayoutVKFrame stores.
			const float mx0 = layoutActive ? lay.sx0 : lx;
			const float my0 = layoutActive ? lay.sy0 : ly;
			const float mx1 = layoutActive ? lay.sx1 : (lx + (float)vw);
			const float my1 = layoutActive ? lay.sy1 : (ly + (float)vh);

			// PHASE B (GL final_render_raster): the monitor CRT pass, and the
			// choice of the two routes to the screen.
			//
			// GL ALWAYS goes offscreen: render_mono_monitor / render_color_monitor
			// write img5b (fbo_mono), Layout_Render then composites img5b in
			// place of img5a - which is how the gel multiply and the rigid
			// rotation come to act on the MONITOR OUTPUT rather than the raw
			// game image. Two things about the direct-to-swapchain monitor draw
			// cannot express that:
			//   * the gel - CrtPostVK's shaders take ONE texture, so they
			//     cannot multiply the layout's overlay; and
			//   * rotation - DrawQuad_ drives its quad from a uvrect, which can
			//     flip but cannot turn 90 degrees.
			// So the intermediate RT is engaged exactly when either applies,
			// and the composite that samples it (the layout's dual-source gel
			// quad, or ScreenQuadVK's permuted corner UVs, or both) supplies
			// what the monitor quad cannot. When NEITHER applies the monitor
			// still draws straight onto the swapchain at the rect above: the
			// same output-sized post with one resample fewer, and byte-for-byte
			// the picture this chain already shipped.
			const bool wantMono  = VkMonoMonitorActive(vattr);
			const bool wantColor = !wantMono && VkColorMonitorActive(vattr);
			const bool monitorWanted = wantMono || wantColor;
			const bool gelActive = layoutActive && lay.hasOverlay;
			const bool needIntermediate = monitorWanted && (gelActive || rot != 0);

			// Intermediate size = the on-screen pixel extent of the quad that
			// will sample it, measured in the monitor image's OWN frame.
			//
			// GL's number is Layout_GetScreenPixelSize, which mame_layout.cpp
			// sets to d.w*scaleX x d.h*scaleY - the screen quad's edge lengths
			// BEFORE the rigid rotation ("rigid rotation preserves edge
			// lengths ... regardless of rotMode"). LayoutRectPx here reports
			// the ROTATED bounding box instead, and a 90/270 turn swaps those
			// two axes, so they are swapped back. The letterbox path is the
			// same story: vw/vh were fitted to the INVERTED aspect for rot
			// 1/2, so un-swapping recovers the unrotated extent - exactly what
			// GL's synthetic screen-only layout produces for a game with no
			// .lay file. Net effect either way: one monitor fragment covers
			// one screen pixel of the composited quad, so the shadow mask and
			// beam ripple resolve on screen pixels just as GL's img5b resize
			// achieves.
			int mw = (int)((mx1 - mx0) + 0.5f);
			int mh = (int)((my1 - my0) + 0.5f);
			if (rot == 1 || rot == 2) { const int t = mw; mw = mh; mh = t; }

			// EnsureMonitorTarget returns false when the RT is unavailable or
			// a resize had to be deferred to the next frame boundary; then
			// BOTH routes stand down and the composite samples the raw game RT
			// - the pre-monitor picture, never a stale or destroyed image.
			const bool monitorOffscreen = needIntermediate && EnsureMonitorTarget(mw, mh);
			const bool monitorDirect    = monitorWanted && !needIntermediate;

			CrtMonitorParamsVK mp{};
			if (monitorOffscreen || monitorDirect)
				FillMonitorParams(wantMono, mp);

			// Offscreen work is illegal inside the swapchain pass (the RT
			// opens its own pass; vkCmdBlitImage may not appear inside any
			// pass), so the frame pass is suspended around it. No swapchain
			// barriers: the image stays COLOR_ATTACHMENT_OPTIMAL and the
			// resume re-opens with LOAD_OP_LOAD (see sys_vk.cpp).
			VK_SuspendFramePass(g_vk, cmd);

			// -------------------------------------------------------------
			// Game image upload. vkCmdCopyBufferToImage may NOT be recorded
			// inside a dynamic-rendering pass, so this sits in the suspended
			// window and BEFORE s_rtGame.Begin.
			//
			// The buffer is pre-filled with opaque black so a source row the
			// emit skips (main_bitmap->line[y] == NULL) shows the same black
			// the RT clear used to supply, not last frame's pixels. The emit
			// otherwise covers every texel: the orientation transform is a
			// bijection from the visible area onto the destination.
			//
			// In-flight safety: the CPU buffer is private to this thread, and
			// Upload only touches slot g_vk.frameIndex's staging buffer and
			// image - the slot VK_BeginFrame just proved idle by waiting its
			// fence. The other in-flight frame owns the other slot.
			// -------------------------------------------------------------
			VkRasterTexSinkCtx sinkCtx{ g_rasterTex.Pixels(),
				(int)g_rasterTex.GetWidth(), (int)g_rasterTex.GetHeight() };
			g_rasterTex.ClearPixels(MAKE_RGBA(0, 0, 0, 0xff));
			raster_emit_polys(VkRasterTexSink, &sinkCtx, /*yFlip=*/0);
			g_rasterTex.Upload(g_vk, cmd, g_vk.frameIndex);

			// Game pass: clear to opaque black (the GL fbo_raster clears
			// black too), then ONE textured quad over the whole RT - the
			// replacement for the ~64,500-quad emit this path used to do.
			//
			// PRESCALE: the texture is s_rasterNativeW/H and the rect is
			// s_rasterScaledW/H, sampled NEAREST. Destination pixel j takes
			// texel floor((j+0.5) * native / scaled); the old quad path gave
			// pixel j the cell floor((j+0.5) / prescale). scaled ==
			// native*prescale EXACTLY whenever that product is an integer -
			// which covers every integer prescale - so the two are identical
			// there, block for block. Only a prescale whose product truncates
			// (e.g. 2.3) differs, and then only in where a block boundary
			// lands near the right/bottom edge; the quad path also left a
			// sub-pixel sliver of the RT unwritten in that case, which this
			// does not.
			//
			// UV/flip trace: the emit ran with yFlip=0, so the game's TOP
			// scanline is texture row 0. RecordRect with flipUV_Y=false maps
			// v=0 to the rect's TOP vertex, and its y-flipped viewport puts
			// world y = topPx at RT image row 0 - so the game's top scanline
			// lands in RT row 0 and column 0 stays on the left, exactly what
			// the FpolyVK path produced. Everything downstream (scanlines,
			// mips, monitor, layout, rotation, composite) is untouched.
			//
			// Blend None: a straight RGBA copy. The emitted texels are opaque
			// (alpha 0xff), so alpha blending would compute the same result;
			// None just says so and skips the blend hardware.
			GPU_ZONE_BEGIN("raster");
			s_rtGame.Begin(g_vk, cmd, /*clear=*/true, 0.0f, 0.0f, 0.0f, 1.0f);
			g_screenQuad.RecordRect(g_vk, cmd, g_vk.frameIndex,
				g_rasterTex.View(g_vk.frameIndex), g_rasterTex.Sampler(),
				0.0f, 0.0f, (float)s_rasterScaledW, (float)s_rasterScaledH,
				(uint32_t)s_rasterScaledW, (uint32_t)s_rasterScaledH,
				/*flipUV_Y=*/false, RGB_WHITE, /*uvRotation=*/0,
				SQBlendVK::None);
			GPU_ZONE_END();

			// PHASE A (GL final_render_raster): the tiled scanline overlay
			// multiplied over the game image, INSIDE the game RT's pass - GL
			// draws it into fbo_raster/img5a at exactly this point, so the mip
			// chain built below (and therefore the monitor pass's halation)
			// includes it, same as GL.
			//
			// PRESCALED dims: GL's render_scanlines covers its rw x rh FBO and
			// tiles u = rw/scan_x, v = rh/scan_y. Now that this RT is the same
			// rw x rh, the target IS the tiling extent and RecordScanlines no
			// longer carries a prescale compensation factor.
			if (VkScanlinesActive(vattr))
			{
				GPU_ZONE("scanlines");
				g_crtPost.RecordScanlines(g_vk, cmd, g_vk.frameIndex,
					s_scanTex.view,
					(int)s_scanTex.width, (int)s_scanTex.height,
					s_rasterScaledW, s_rasterScaledH);
			}

			// One zone for the RT pass close and the mip cascade. The pass
			// close and its COLOR->SHADER barrier are what make the image
			// mippable in the first place, and both always run, so timing them
			// together keeps the sections flat without hiding anything.
			GPU_ZONE_BEGIN("game_mips");
			s_rtGame.End(g_vk, cmd);

			// Mips are load-bearing downstream (CRT halation textureLod,
			// minified layout composite) and the trilinear sampler already
			// reads them when the letterbox rect is smaller than the RT.
			if (s_rtGame.GetMipLevels() > 1)
				s_rtGame.GenerateMips(g_vk, cmd);
			GPU_ZONE_END();

			// PHASE B, offscreen route (GL render_mono_monitor /
			// render_color_monitor -> img5b). Still inside the suspended-pass
			// window: a render pass cannot nest inside the swapchain pass, and
			// this one must also come AFTER GenerateMips because the halation
			// taps read the game RT's mip pyramid via textureLod - the same
			// ordering GL enforces with fbo_generate_mipmaps({img5a}) sitting
			// between the scanline draw and the monitor pass.
			//
			// Rect (0,0)-(w,h) over the whole RT with targetW/H = the RT dims,
			// so the shader's mask origin push (pc.tsize.zw) is (0,0) - which
			// is GL's fbo_mono origin exactly. The direct route has to
			// re-anchor to the letterbox corner; here the anchor falls out of
			// the render target, so the color pass's mask phase now tracks GL
			// more closely than the direct route does, with no shader change.
			if (monitorOffscreen)
			{
				GPU_ZONE("monitor");
				const int rtW = s_rtMonitor.GetWidth();
				const int rtH = s_rtMonitor.GetHeight();

				// Clear is belt-and-braces: the monitor quad is blend-disabled
				// (GL glDisable(GL_BLEND), "straight replace into img5b") and
				// covers every pixel. It also satisfies the RT's first-use
				// rule without relying on the UNDEFINED-layout hardening.
				s_rtMonitor.Begin(g_vk, cmd, /*clear=*/true, 0.0f, 0.0f, 0.0f, 1.0f);
				g_crtPost.RecordMonitor(g_vk, cmd, g_vk.frameIndex,
					/*colorPass=*/wantColor,
					s_rtGame.VK_GetColorView(), s_rtGame.VK_GetSampler(),
					mp,
					0.0f, 0.0f, (float)rtW, (float)rtH,
					rtW, rtH);
				s_rtMonitor.End(g_vk, cmd);
			}

			VK_ResumeFramePass(g_vk, cmd, s_imageIndex);

			// The texture the SCREEN layer samples - GL's one-line
			// `screenTex = img5b` swap in final_render_raster. When the monitor
			// ran offscreen, everything downstream (the gel multiply, the
			// rotation, the additive blend over the backdrop) acts on the MONITOR
			// OUTPUT; otherwise it acts on the raw game image, exactly as before.
			// Both images carry the game's top scanline in row 0 and are sampled
			// with normalized UVs over their full extent, so nothing about the UV
			// conventions below changes with the swap.
			VkImageView screenView = monitorOffscreen
				? s_rtMonitor.VK_GetColorView() : s_rtGame.VK_GetColorView();
			VkSampler screenSampler = monitorOffscreen
				? s_rtMonitor.VK_GetSampler() : s_rtGame.VK_GetSampler();

			// Layers BELOW and INCLUDING the game: every backdrop drawable, then
			// the screen quad itself unless the DIRECT monitor route takes that
			// rect. The offscreen route still wants the screen quad drawn - that
			// quad is what puts the monitor output on screen, gel and all.
			if (layoutActive)
			{
				GPU_ZONE("composite");
				LayoutVK_RecordUnderlay(g_vk, cmd, g_vk.frameIndex,
					g_layoutQuad, lay,
					screenView, screenSampler,
					sw, sh, /*drawScreen=*/!monitorDirect);
			}

			if (monitorDirect)
			{
				GPU_ZONE("monitor");
				// Direct route (unchanged from the pre-fix chain): no gel and no
				// rotation, so the monitor quad IS the output-sized draw - straight
				// onto the swapchain at the letterbox or layout screen rect, so
				// 1 fragment = 1 screen pixel with one resample fewer than GL.
				//
				// Rect orientation: RecordRect takes y-up screen pixels and
				// CrtPostVK takes y-down; the rect is vertically centered with the
				// same integer margins either way, so the numbers are identical.
				// RecordMonitor's uvrect puts v=0 at the rect TOP, which is RT
				// image row 0 = the game's top scanline (see the flipUV_Y trace
				// below) - upright, the same result as flipUV_Y=false.
				g_crtPost.RecordMonitor(g_vk, cmd, g_vk.frameIndex,
					/*colorPass=*/wantColor,
					s_rtGame.VK_GetColorView(), s_rtGame.VK_GetSampler(),
					mp,
					mx0, my0, mx1, my1,
					sw, sh);
			}
			else if (layoutActive)
			{
				// The screen quad was already recorded by RecordUnderlay at the
				// layout's screen bounds; nothing to composite here.
			}
			else
			{
				// flipUV_Y trace (Gate A verifies): the emit runs with yFlip=0,
				// so the game's TOP scanline (post-orientation y=0) is texture
				// row 0; the game quad draws that with flipUV_Y=false into the
				// RT's y-flipped viewport, so the game's top scanline lands in
				// RT image row 0 (same row-0 convention the FpolyVK emit had).
				// RecordRect with flipUV_Y=false samples image row 0 (v=0) at
				// the rect's TOP vertex -- game top at screen top, upright.
				// So: false.
				// System rotation rides the rotated corner UVs (rot != 0) - which
				// is also how a rotated MONITOR image reaches the screen for a
				// game with no .lay file, GL's synthetic screen-only layout doing
				// the same rigid turn on img5b.
				GPU_ZONE("composite");
				g_screenQuad.RecordRect(g_vk, cmd, g_vk.frameIndex,
					screenView, screenSampler,
					lx, ly, lx + (float)vw, ly + (float)vh,
					(uint32_t)sw, (uint32_t)sh,
					/*flipUV_Y=*/false, RGB_WHITE, rot);
			}

			// Bezel layer, LAST - on top of the game and on top of the CRT
			// monitor pass, exactly where Layout_Render's layer order puts it.
			if (layoutActive)
			{
				GPU_ZONE("composite");
				LayoutVK_RecordOverlayArt(g_vk, cmd, g_vk.frameIndex,
					g_layoutQuad, lay, sw, sh);
			}

			// Rotated raster overlays (GL final_render_raster parity): GL
			// routes the menu/PAUSED/FPS through fbo4 + the rotated
			// screen_rect blit when system-rotated, so they turn with the
			// game. Same shape here - overlays into the square output RT
			// (cleared transparent), then ONE rotated premultiplied-over
			// blit. Non-rotated raster keeps the crisp direct draw below.
			if (rot != 0 && EnsureRotTarget())
			{
				// No pre-clear: RecordUiOverlays isolates its own strokes
				// (beam_stash_push/pop), and a cache_clear() here would now be
				// a rule violation - the beam batches belong to the sim, not
				// the renderer. On a raster game they are empty anyway (nothing
				// but the overlays touches the beam queue), and the MAME vector
				// list is drained once at this function's tail.
				VK_SuspendFramePass(g_vk, cmd);
				s_rtRot.Begin(g_vk, cmd, /*clear=*/true, 0.0f, 0.0f, 0.0f, 0.0f);
				s_rotTargetActive = true;
				RecordUiOverlays();
				s_rotTargetActive = false;
				uiOverlaysDone = true;
				s_rtRot.End(g_vk, cmd);
				VK_ResumeFramePass(g_vk, cmd, s_imageIndex);

				// Letterbox for the blit = the same window fit the game quad
				// used, so the overlay canvas lands exactly over the game
				// (GL's screen_rect covers the fbo4 square identically).
				GPU_ZONE("rot_blit");
				g_screenQuad.RecordRect(g_vk, cmd, g_vk.frameIndex,
					s_rtRot.VK_GetColorView(), s_rtRot.VK_GetSampler(),
					lx, ly, lx + (float)vw, ly + (float)vh,
					(uint32_t)sw, (uint32_t)sh,
					/*flipUV_Y=*/false, RGB_WHITE, rot, SQBlendVK::PremulOver);
			}
		}
	}

	// Vector post chain: NON-GUI vector games render beams into the SSAA RT,
	// run trail/glow, and composite - mirroring the GL chain's fbo1 ->
	// trail/blur -> final_render shape. The GUI keeps the gated direct path
	// below (GL excludes the GUI from trail/glow anyway), as does any game if
	// the post chain failed to init (visible beams beat a black screen).
	//
	// The in-game menu/pause text rides the same CPU beam queue under VK
	// (VF strokes go through beam_add_line), but it is emitted and
	// recorded by RecordUiOverlays AFTER this composite and against a stashed
	// (empty) queue, so it stays out of the beam RT and out of trail/glow -
	// post-composite and crisp, same as GL.
	const bool wantPostChain = s_frameOpen && GameIsVector() &&
		!emulator_is_gui_active();
	if (wantPostChain)
		EnsureVectorPost();

	if (wantPostChain && s_vecPostInit)
	{
		VkCommandBuffer cmd = g_vk.cmdBuffers[g_vk.frameIndex];
		const int sw = (int)g_vk.swapchainExtent.width;
		const int sh = (int)g_vk.swapchainExtent.height;

		// Artwork layer set for this frame, mirroring final_render's gates
		// exactly: config flag AND art_loaded AND (for the overlay) the
		// driver's video_attributes. Views come from the per-game VkArt cache
		// loaded by vkchain_load_artwork (run_game Step 6).
		VectorArtworkVK art{};
		{
			const int vattr = (Machine && Machine->drv) ? Machine->drv->video_attributes : 0;
			VkTexture* t = nullptr;
			if (config.artwork && art_loaded[0] && (t = VkArt_Get(0)) != nullptr)
			{
				art.backdropView = t->view;
				art.backdropSampler = t->sampler;
			}
			if (config.overlay && art_loaded[1] && (t = VkArt_Get(1)) != nullptr)
			{
				art.overlayView = t->view;
				art.overlaySampler = t->sampler;
				art.overlay1 = (vattr & VECTOR_USES_OVERLAY1) != 0;
				art.overlay2 = (vattr & VECTOR_USES_OVERLAY2) != 0;
			}
			if (config.bezel && art_loaded[3] && (t = VkArt_Get(3)) != nullptr)
			{
				art.bezelView = t->view;
				art.bezelSampler = t->sampler;
			}
			art.rasterBW = (vattr & VIDEO_TYPE_RASTER_BW) != 0;
			art.artcrop = (config.artcrop != 0);
			art.bezelX = (float)bezelx;
			art.bezelY = (float)bezely;
			art.bezelZoom = bezelzoom;
		}
		const bool artActive = art.Any();

		const GuiBeamMap m = ComputeGuiBeamMap();

		// System rotation (GL: everything lands in the square fbo4 and ONE
		// Rect2 blit turns it). Same here: the composite AND the UI overlays
		// record into the square output RT with the identity square fit, then
		// a single rotated quad puts that canvas in the window's letterbox
		// box. m.lx/ly/vw/vh is already the POST-rotation letterbox - Step 12
		// of run_game stores the rotated aspect in GetWindowSetup(), which is
		// the same source GL's screen_rect reads - so the box is correct
		// as-is and only the content inside it needs turning.
		// Requires the composite blitter; if either the RT or ScreenQuadVK is
		// unavailable this stays false and the untouched non-rotated path runs.
		const bool rotActive = (rot != 0) && s_screenQuadInit && EnsureRotTarget();

		// Mirror glchain_render's !paused guard; unlike the direct path,
		// pause here shows the RETAINED frame (composite below still runs),
		// matching GL's frozen fbo1.
		// Tracks whether the swapchain pass is currently suspended. The
		// rotated path keeps it suspended past the beam work so the output RT
		// pass can open straight away; when paused it opens the suspension
		// itself (the composite must still be rebuilt every frame - the
		// overlays move while the game is frozen).
		bool passSuspended = false;

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
			passSuspended = true;
			GPU_ZONE_BEGIN("beam");
			g_vectorPost.BeginBeamPass(g_vk, cmd);
			g_vectorDrawRT.Record(g_vk, cmd, g_vk.frameIndex, proj, additive,
				(uint32_t)g_vectorPost.BeamDim(), (uint32_t)g_vectorPost.BeamDim());
			GPU_ZONE_END();
			// Textured shots: the GL analog draws texlist right
			// after beam_draw_all into fbo1 with the same projection
			// (opengl_renderer.cpp: draw_textured_shots(proj)). Same here:
			// into the beam RT, so shots get glow/trail/artwork. Skipped
			// when the game has no shot texture - GL samples an incomplete
			// texture there and shows nothing either.
			if (s_shotInit && config.shots_textured)
			{
				int shotVerts = 0;
				const txdata* shots = tex_shot_verts(&shotVerts);
				VkTexture* shotTex = VkArt_GetShotTex();
				if (shots && shotVerts > 0 && shotTex)
				{
					GPU_ZONE("shots");
					g_shotDraw.Record(g_vk, cmd, g_vk.frameIndex, proj,
						shots, (uint32_t)shotVerts,
						shotTex->view, shotTex->sampler,
						(uint32_t)g_vectorPost.BeamDim(),
						(uint32_t)g_vectorPost.BeamDim());
				}
			}
			g_vectorPost.EndBeamPass(g_vk, cmd);
			// Glow selection + pyramid tuning straight from config every
			// frame, so the VECTOR MONITOR SETUP menu adjusts it live.
			VectorPostVK::VectorGlowVK glow;
			glow.filter = config.glow_filter;
			glow.gain   = config.glow2_gain;
			glow.spread = config.glow2_spread;
			glow.tail   = config.glow2_tail;
			glow.core   = config.glow2_core;
			g_vectorPost.RecordPost(g_vk, cmd, g_vk.frameIndex,
				config.vectrail, config.vecglow, s_trailClearPending, glow);
			// Consume the flag only when the trail pass actually ran: with
			// vectrail off at load time the accumulator still holds the
			// PREVIOUS game's phosphor, and the pending clear must survive
			// until a later mid-game vectrail toggle first draws into it.
			if (config.vectrail > 0)
				s_trailClearPending = false;
			// Artwork: build the CRT image (beam+glow+trail composite
			// + OVERLAY1 modulate) into the frame RT - GL's img4b - so the
			// overlay colors ONLY the CRT image, never the backdrop. Offscreen,
			// so it must run before the frame pass resumes.
			if (artActive)
			{
				GPU_ZONE("framebuild");
				g_vectorPost.RecordFrameBuild(g_vk, cmd, g_vk.frameIndex,
					m.grL, m.grR, m.grB, m.grT,
					config.vectrail, config.vecglow, art);
			}
			// The rotated path stays suspended: its output RT is another
			// offscreen pass, and resuming only to suspend again would be a
			// pointless swapchain pass open/close.
			if (!rotActive)
			{
				VK_ResumeFramePass(g_vk, cmd, s_imageIndex);
				passSuspended = false;
			}
		}

		if (sw > 0 && sh > 0 && rotActive)
		{
			// ---- Rotated composite: build GL's fbo4 in s_rtRot, then blit.
			//
			// The letterbox collapses to the identity square (lx=ly=0,
			// vw=vh=1024), which turns every mapped coordinate below back
			// into the plain fbo4-space value GL uses; the game_rect box is
			// unchanged (it IS an fbo4-space rect). The final quad then does
			// what end_render_fbo4 does.
			if (!passSuspended)
			{
				VK_SuspendFramePass(g_vk, cmd);
				passSuspended = true;
			}
			// Clear transparent, like GL's set_render_fbo4 (glClearColor 0,0,0,0).
			s_rtRot.Begin(g_vk, cmd, /*clear=*/true, 0.0f, 0.0f, 0.0f, 0.0f);
			s_rotTargetActive = true;

			if (artActive && g_vectorPost.FrameReady())
			{
				GPU_ZONE("composite");
				g_vectorPost.RecordCompositeLayered(g_vk, cmd, g_vk.frameIndex,
					0.0f, 0.0f, (float)kRotCanvas, (float)kRotCanvas,
					m.grL, m.grR, m.grB, m.grT, art,
					kRotCanvas, kRotCanvas);
			}
			else
			{
				// Same expression as the unrotated branch with lx=ly=0 and
				// vw=vh=1024 substituted, i.e. the game_rect quad in fbo4
				// space, y-down.
				GPU_ZONE("composite");
				g_vectorPost.RecordComposite(g_vk, cmd, g_vk.frameIndex,
					m.grL, (float)kRotCanvas - m.grT,
					m.grR, (float)kRotCanvas - m.grB,
					config.vectrail, config.vecglow,
					kRotCanvas, kRotCanvas);
			}

			// UI overlays go in the SAME canvas so they turn with the frame -
			// that is precisely what GL gets for free by drawing them into
			// fbo4. The game's beam batches are still live here and must STAY
			// live - they are RETAINED for the frames the sim does not refresh
			// (see the ownership note at the tail of this function).
			// RecordUiOverlays stashes them across its own emit+record and
			// swaps them back, so nothing is drained here.
			RecordUiOverlays();
			uiOverlaysDone = true;

			s_rotTargetActive = false;
			s_rtRot.End(g_vk, cmd);
			VK_ResumeFramePass(g_vk, cmd, s_imageIndex);
			passSuspended = false;

			// GL end_render_fbo4: blit the square canvas onto the letterbox
			// rect with blending DISABLED and the rotation's corner UVs.
			GPU_ZONE("rot_blit");
			g_screenQuad.RecordRect(g_vk, cmd, g_vk.frameIndex,
				s_rtRot.VK_GetColorView(), s_rtRot.VK_GetSampler(),
				m.lx, m.ly, m.lx + m.vw, m.ly + m.vh,
				(uint32_t)sw, (uint32_t)sh,
				/*flipUV_Y=*/false, RGB_WHITE, rot, SQBlendVK::None);
		}
		else if (sw > 0 && sh > 0)
		{
			// The composite draws to the SWAPCHAIN, and it is the only part of
			// this branch that runs while PAUSED - the !paused block above (the
			// one whose Suspend/Resume pair used to leave the pass open for us)
			// is skipped entirely then. Since the pass became lazily-opened,
			// that left a paused frame recording these draws with NO pass open
			// at all. Ensure it here; a no-op on the unpaused path, where the
			// Resume at the end of the beam block already opened it.
			VK_EnsureFramePass(g_vk, cmd);

			if (artActive && g_vectorPost.FrameReady())
			{
				// Layered composite (GL LAYER 5C + 6): backdrop -> frame RT
				// -> crt_boost -> overlay2 -> bezel, all mapped through the
				// same letterbox the direct composite uses.
				GPU_ZONE("composite");
				g_vectorPost.RecordCompositeLayered(g_vk, cmd, g_vk.frameIndex,
					m.lx, m.ly, m.vw, m.vh,
					m.grL, m.grR, m.grB, m.grT, art);
			}
			else
			{
				// No-artwork fast path (the known-gated behavior): game_rect
				// box scaled into the aspect-fit letterbox. RecordComposite
				// takes Y-DOWN window pixels, so the y-up letterbox coords
				// flip here.
				const float x0 = m.lx + (m.grL / 1024.0f) * m.vw;
				const float x1 = m.lx + (m.grR / 1024.0f) * m.vw;
				const float yTopUp = m.ly + (m.grT / 1024.0f) * m.vh;
				const float yBotUp = m.ly + (m.grB / 1024.0f) * m.vh;
				GPU_ZONE("composite");
				g_vectorPost.RecordComposite(g_vk, cmd, g_vk.frameIndex,
					x0, (float)sh - yTopUp, x1, (float)sh - yBotUp,
					config.vectrail, config.vecglow);
			}
		}

		// Safety net: a suspension can only survive to here if rotActive was
		// true and the sw/sh guard above went false between the two blocks
		// (impossible today - same values, same frame - but an unmatched
		// suspend would leave the frame with no open pass).
		if (passSuspended)
		{
			VK_ResumeFramePass(g_vk, cmd, s_imageIndex);
			passSuspended = false;
		}
	}
	// Vector path: draw the frame's beam batches direct to the swapchain,
	// inside the frame pass VK_BeginFrame opened (no suspend/resume needed --
	// VectorDrawVK records into an already-open pass). Order: vector_update
	// (CPU convert), Record (consume), then the clears below (post-consume
	// drain). This path serves the GUI driver and the post-chain-init-failed
	// fallback only; everything else goes through the post chain above.
	else if (s_frameOpen && GameIsVector())
	{
		EnsureVectorRenderer();

		// Mirror glchain_render's !paused guard. Known limitation of this
		// path: GL freezes the last frame in its FBO while paused; direct-to-
		// swapchain has no retained image, so a paused vector game shows
		// black until unpause. The post chain above has no such gap.
		if (s_vectorInit && !paused &&
		    g_vk.swapchainExtent.width > 0 && g_vk.swapchainExtent.height > 0)
		{
			// CPU-only conversion (mame_vector.cpp vector_update ->
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
			// config.cpp; defaults 0..1024). Only a ZERO-extent rect falls
			// back - see the fabsf rationale in ComputeGuiBeamMap: an INVERTED
			// range is how video.ini expresses a per-game flip, and the folded
			// ortho below reproduces it correctly (the negative denominator
			// flips the axis, exactly as GL's inverted quad does).
			float grL = (float)game_rect_left,   grR = (float)game_rect_right;
			float grB = (float)game_rect_bottom, grT = (float)game_rect_top;
			if (fabsf(grR - grL) < 1.0f) { grL = 0.0f; grR = 1024.0f; }
			if (fabsf(grT - grB) < 1.0f) { grB = 0.0f; grT = 1024.0f; }

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
			//
			// EXCEPT the front-end GUI. Its driver declares VECTOR_USES_COLOR,
			// so this test picked additive - but the GUI's beam queue is not
			// game beams, it is VF GLYPH STROKES (under VK they are routed here,
			// where GL draws them through VF's own path). GL draws that text
			// with VF::Begin's SRC_ALPHA/ONE_MINUS_SRC_ALPHA and
			// beam_draw_lines/caps(additive=false), i.e. ALPHA-OVER. Drawing it
			// additively instead summed every overlapping stroke: the menu read
			// far brighter than GL and glyph joins - where two strokes and a
			// join disc stack - blew out to near-white. Alpha-over here also
			// puts the joins on the m_pipeDiscOver path, matching GL's caps.
			const bool additive = !emulator_is_gui_active() &&
				Machine && Machine->drv &&
				(Machine->drv->video_attributes & VECTOR_USES_COLOR) != 0;

			VkCommandBuffer cmd = g_vk.cmdBuffers[g_vk.frameIndex];
			// Direct-to-swapchain path: no suspend/resume brackets it, so it
			// must open the lazily-opened pass itself.
			VK_EnsureFramePass(g_vk, cmd);
			// 0,0 target dims = record against the swapchain extent; the post
			// chain's instance passes the beam RT's dims instead.
			g_vectorDraw.Record(g_vk, cmd, g_vk.frameIndex, proj, additive, 0, 0);
		}
	}

	// ---- Who owns the beam-batch clear: the SIM, not the renderer. ----------
	//
	// AAE's vector sims do NOT rebuild the display list every video frame. A
	// game triggers a redraw (AVG VGGO and friends) only when it wants to, and
	// on the frames in between the renderer is expected to REDRAW THE RETAINED
	// beam data. So the clear belongs to the producer, and every sim/driver
	// does it at the start of a new vector frame: aae_avg.cpp,
	// old_mame_vecsim_dvg.cpp, ccpu.cpp, SegaG80vid.cpp, vertigo_video.cpp,
	// tempest.cpp, mhavoc.cpp, cchasm.cpp, aztarac.cpp,
	// cinematronics_driver.cpp, segag80.cpp - plus the front-end GUI at
	// driver_gui.cpp's run_gui.
	//
	// The GL chain honours that (glchain_render's vector branch calls
	// vector_update / beam_draw_all / vector_clear_list and NEVER cache_clear),
	// and this chain must too. A cache_clear() here wiped the batches on every
	// frame the sim had not refreshed, so BeginBeamPass's freshly cleared beam
	// RT had nothing to draw into it: a blank frame, i.e. the AVG flicker on
	// bwidow / spacduel. It also blanked the retained batches while paused, so
	// the first frame after an unpause drew nothing until the next sim redraw.
	//
	// The UI-overlay strokes that also ride this queue under VK are isolated by
	// RecordUiOverlays' own beam_stash_push/pop, so nothing here has to drain
	// them - and the legacy textured-shot list (texlist) is retained on exactly
	// the same terms as the beam batches, which is why cache_clear() is gone
	// rather than replaced by a beam-only clear.

	// Drain the MAME vector display list (AVG/DVG sims append via
	// vector_add_point). THIS one is the renderer's: vector_update APPENDS the
	// list into the beam batches, so an undrained list would be re-appended
	// every frame and double the geometry. GL's vector branch calls it in
	// exactly the same place (opengl_renderer.cpp). The sims call it too when
	// they start a new frame; both are needed, since a sim that skips a frame
	// never reaches its own call.
	// Pure CPU: resets the list write index (mame_vector.cpp).
	vector_clear_list();

	// In-game UI overlays LAST, so the menu/PAUSED/exit dialog/FPS land on
	// top of whatever the branches above composited - and on EVERY frame,
	// including paused ones (GL parity: final_render always runs the overlay
	// draw). It stashes the game's retained batches for the duration, so the
	// overlay Record only ever consumes overlay strokes, never game beams.
	//
	// Skipped when a rotated branch already ran them inside the output RT:
	// render_ui_overlays() also drives video_loop() frame logic (menu
	// auto-pause, LED refresh), so calling it twice in a frame would
	// double-step that logic, not just double-draw.
	if (!uiOverlaysDone)
		RecordUiOverlays();

	// Close the profiler's "frame" section. Everything this chain records for
	// the frame is now behind us; VK_EndFrame's pass close, present barrier and
	// submit are all that follow. The swapchain pass is still open here, which
	// is fine - vkCmdWriteTimestamp is legal inside a render pass.
	GpuProf::EndFrame();
}

// ---------------------------------------------------------------------------
// vkchain_request_snapshot - the Vulkan half of snapshot() (F12).
//
// Called from the emulator's input handling MID-TICK: the frame pass is open
// (or the swapchain is deferred and no frame exists), and there is no GL
// context to glReadPixels from. So nothing is read here - the request is
// latched and serviced at the next vkchain_swap_buffers, which captures the
// frame the user actually saw.
//
// The skip cases are logged and dropped rather than left latched, so a press
// while minimized does not silently fire minutes later on restore.
// ---------------------------------------------------------------------------
void vkchain_request_snapshot(void)
{
	if (!s_initialized)
	{
		LOG_ERROR("snapshot (vulkan): renderer not initialized; dropped");
		return;
	}
	if (s_deferredZeroExtent)
	{
		LOG_ERROR("snapshot (vulkan): swapchain deferred (window minimized?); dropped");
		return;
	}
	if (g_vk.swapchainExtent.width == 0 || g_vk.swapchainExtent.height == 0)
	{
		LOG_ERROR("snapshot (vulkan): swapchain extent is zero; dropped");
		return;
	}
	VkSnapshot::Request();
}

void vkchain_swap_buffers(void)
{
	if (!s_initialized || !s_frameOpen)
		return;
	s_frameOpen = false;

	// F12: service a latched screenshot request here, the last point at which
	// this frame's command buffer is still recording and its pass still open.
	// The readback is recorded into that same command buffer, so what lands in
	// the PNG is provably the frame about to be presented. Nothing happens on
	// frames with no request pending.
	VkSnapshot::Capture shot;
	VkSnapshot::BeginCapture(g_vk, g_vk.cmdBuffers[g_vk.frameIndex], s_imageIndex, shot);

	const bool submitted = VK_EndFrame(g_vk, s_imageIndex);

	// After submit+present: drains the device, maps the staging buffer and
	// writes the PNG. No-op (and frees nothing) when no capture was recorded.
	if (shot.active)
		VkSnapshot::FinishCapture(g_vk, shot, submitted);

	if (!submitted)
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
	if (!s_initialized)
		return;

	// Same-size calls are common, not exotic: WindowUtil_UpdateAspect re-fits
	// the viewport through emulator_on_window_resize on EVERY game launch, and
	// at a fixed window size (explicit resolution, borderless fullscreen) the
	// client area never changed. Recreating the swapchain at an identical
	// extent is a full device drain for nothing - and a stream of them is the
	// recreate-storm signature seen when the GUI launched games. Skip when the
	// live swapchain already matches, unless a deferred recreate is pending
	// (then this call doubles as the fast-path retry described below).
	if (!s_deferredZeroExtent &&
	    g_vk.swapchainExtent.width  == (uint32_t)newW &&
	    g_vk.swapchainExtent.height == (uint32_t)newH)
		return;

	// winmain.cpp only calls emulator_on_window_resize (and therefore this
	// function) on WM_SIZE when the window is NOT minimized, so this is a
	// fast-path retry: it does not wait for s_deferRetryTick's ~2s poll.
	RecreateSwapchainOrDefer();
}

// ---------------------------------------------------------------------------
// GUI starfield. See the g_guiPoints comment above for why
// this FpolyVK instance is drawn immediately here (during
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

	// The swapchain pass is opened lazily (VK_BeginFrame no longer opens it -
	// see VK_EnsureFramePass). This entry point draws straight to the
	// swapchain, and for the front-end GUI it runs during cpu_run, BEFORE
	// vkchain_render, so it can genuinely be the first thing to open the pass.
	// Skipped while the rotation canvas is active: that is an offscreen pass
	// which is already open and must not be disturbed.
	if (!s_rotTargetActive)
		VK_EnsureFramePass(g_vk, g_vk.cmdBuffers[g_vk.frameIndex]);

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

// ---------------------------------------------------------------------------
// vkchain_present_blank_frame - clear the window to black and present it.
//
// run_game spends real time on ROMs, artwork and samples with nothing
// repainting, so without this the previous game's (or the front-end's) last
// presented frame sits frozen on screen for the whole load. Presenting one
// black frame first makes that read as a deliberate transition.
//
// Nothing needs drawing: the swapchain pass opens with LOAD_OP_CLEAR to black,
// so an empty frame IS the clear. VK_EndFrame's backstop opens it even though
// no draw call ever touches the swapchain this frame.
// ---------------------------------------------------------------------------
void vkchain_present_blank_frame(void)
{
	if (!s_initialized || s_frameOpen)
		return;

	vkchain_set_render();
	if (!s_frameOpen)
		return;                 // acquire failed / swapchain deferred; nothing to present
	vkchain_swap_buffers();
}

void vkchain_gui_points_shutdown(void)
{
	if (s_guiPointsInit)
	{
		// DRAIN FIRST. FpolyVK::Shutdown destroys the pipeline, the descriptor
		// pool and every slot's VBO/UBO with no drain of its own - by its own
		// documented contract the CALLER must have device-waited-idle (which is
		// why vkchain_shutdown and EnsureRasterRenderer both do).
		//
		// This entry point is the one that did not, and it is the worst place
		// to skip it: it runs from end_gui() during emulator_stop_game(), i.e.
		// immediately after the front-end drew its LAST frame - a frame whose
		// command buffer draws the starfield out of exactly these buffers with
		// exactly this pipeline, was submitted by VK_EndFrame, and is never
		// waited on by anything. Destroying them here while the GPU is still
		// executing that submission is a use-after-free that faults the device.
		//
		// It presents as: launch a game from the front-end, VK_ERROR_DEVICE_LOST
		// (-4) on the next GPU operation - usually the new game's first artwork
		// upload, before it ever renders a frame - then a dead swapchain while
		// the emulation keeps running. Intermittent, because it is a race with
		// however long the GPU still needed: an unrelated log line either side
		// of it was enough to flip the outcome.
		if (g_vk.device && g_vk.vkDeviceWaitIdle_)
			g_vk.vkDeviceWaitIdle_(g_vk.device);
		g_guiPoints.Shutdown(g_vk);
		s_guiPointsInit = false;
	}
	s_guiPointsFailed = false;
}

// ---------------------------------------------------------------------------
// vkchain_vector_hard_clear.
// GL's glchain_vector_hard_clear_fbo1 clears fbo1's three attachments
// (img1a/img1b/img1c) to flush leftover trail/phosphor-feedback data from a
// previous vector game when the GUI starts up. The GUI's VK path has no
// equivalent to clear: it goes through the direct VectorDrawVK instance, which
// renders to the swapchain every frame with no retained image and no
// trail/feedback state. The post chain's retained RTs belong to the game path
// and are cleared on game load via s_trailClearPending, so this is a no-op.
// ---------------------------------------------------------------------------
void vkchain_vector_hard_clear(void) {}

// ---------------------------------------------------------------------------
// GUI solid-color quads. VectorFont::DrawQuad's GL path
// draws through its own GL program/VAO (vector_fonts.cpp) -- GL-direct,
// invisible under Vulkan. This seam reuses ScreenQuadVK::RecordRect (already
// online for the raster composite) with a 1x1 white texture tinted by the
// caller's color, alpha-blended (RecordRect's pipeline is SRC_ALPHA /
// ONE_MINUS_SRC_ALPHA, matching VF::Begin()'s GL blend state).
// ---------------------------------------------------------------------------
bool vkchain_ui_overlay_active(void) { return s_uiOverlayActive; }

void vkchain_gui_draw_quad(float x, float y, float width, float height, rgb_t color)
{
	if (!s_initialized || !s_frameOpen || !s_screenQuadInit)
		return;

	VkTexture* white = VkTex_GetSolidWhite(g_vk);
	if (!white)
		return;

	// The swapchain pass is opened lazily (VK_BeginFrame no longer opens it -
	// see VK_EnsureFramePass). This entry point draws straight to the
	// swapchain, and for the front-end GUI it runs during cpu_run, BEFORE
	// vkchain_render, so it can genuinely be the first thing to open the pass.
	// Skipped while the rotation canvas is active: that is an offscreen pass
	// which is already open and must not be disturbed.
	if (!s_rotTargetActive)
		VK_EnsureFramePass(g_vk, g_vk.cmdBuffers[g_vk.frameIndex]);

	// GUI-local space (VF::Initialize(1024,768)): x centered directly in the
	// shared 0..1024 box; y gets the 768->1024 rescale and the SAME conditional
	// Y mirror VF text emission applies (vector_fonts.cpp VectorFont::End).
	// Quads and text must share one convention or a menu background lands
	// mirrored away from its rows - so this switch has to track that one
	// exactly: mirror for the in-game overlay pass (default 0..1024 box, no
	// per-game flip), not for the GUI front-end (whose [gui] video.ini rect is
	// inverted and supplies the flip itself, exactly as under GL).
	const bool mirrorY = s_uiOverlayActive;
	const float scaleY = 1024.0f / 768.0f;
	const float minx = x - width * 0.5f,  maxx = x + width * 0.5f;
	const float ylo = y - height * 0.5f, yhi = y + height * 0.5f;
	const float miny = (mirrorY ? (768.0f - ylo) : ylo) * scaleY;
	const float maxy = (mirrorY ? (768.0f - yhi) : yhi) * scaleY;

	// In-game overlay quads (TAB menu background during a game) use the
	// overlay map: default 0..1024 box, overlay letterbox aspect. The GUI
	// driver's own quads keep the game_rect map its text renders with.
	const GuiBeamMap m = s_uiOverlayActive ? ComputeUiOverlayMap()
	                                       : ComputeGuiBeamMap();
	float x0, y0, x1, y1;
	GuiBeamToWindowPx(m, minx, miny, x0, y0);
	GuiBeamToWindowPx(m, maxx, maxy, x1, y1);

	// The by->fy map includes a flip, so min/max in beam space does not
	// necessarily land in the same order in window space -- sort explicitly.
	const float L = (x0 < x1) ? x0 : x1;
	const float R = (x0 < x1) ? x1 : x0;
	const float B = (y0 < y1) ? y0 : y1;
	const float T = (y0 < y1) ? y1 : y0;

	// Target dims follow the map above: the square rotation RT while it is
	// open, the swapchain otherwise.
	const uint32_t sw = s_rotTargetActive ? (uint32_t)kRotCanvas : g_vk.swapchainExtent.width;
	const uint32_t sh = s_rotTargetActive ? (uint32_t)kRotCanvas : g_vk.swapchainExtent.height;
	VkCommandBuffer cmd = g_vk.cmdBuffers[g_vk.frameIndex];

	g_screenQuad.RecordRect(g_vk, cmd, g_vk.frameIndex,
		white->view, white->sampler,
		L, B, R, T, sw, sh,
		/*flipUV_Y=*/false, color);
}

// ---------------------------------------------------------------------------
// In-game overlay dim (pause / exit-confirm). Called from the shared
// render_ui_overlays() (opengl_renderer.cpp) in place of its GL
// quad_from_center. GL dims fbo4's full 1024 space, which the screen_rect
// blit letterboxes onto the window - so the on-screen dim covers exactly the
// overlay letterbox box; this draws that box directly. Same ScreenQuadVK +
// solid-white-texture seam as vkchain_gui_draw_quad (alpha-over pipeline).
// ---------------------------------------------------------------------------
void vkchain_ui_dim_quad(int alpha)
{
	if (!s_initialized || !s_frameOpen || !s_screenQuadInit)
		return;

	VkTexture* white = VkTex_GetSolidWhite(g_vk);
	if (!white)
		return;

	// The swapchain pass is opened lazily (VK_BeginFrame no longer opens it -
	// see VK_EnsureFramePass). This entry point draws straight to the
	// swapchain, and for the front-end GUI it runs during cpu_run, BEFORE
	// vkchain_render, so it can genuinely be the first thing to open the pass.
	// Skipped while the rotation canvas is active: that is an offscreen pass
	// which is already open and must not be disturbed.
	if (!s_rotTargetActive)
		VK_EnsureFramePass(g_vk, g_vk.cmdBuffers[g_vk.frameIndex]);

	if (alpha < 0) alpha = 0;
	if (alpha > 255) alpha = 255;

	const GuiBeamMap m = ComputeUiOverlayMap();
	const uint32_t tw = s_rotTargetActive ? (uint32_t)kRotCanvas : g_vk.swapchainExtent.width;
	const uint32_t th = s_rotTargetActive ? (uint32_t)kRotCanvas : g_vk.swapchainExtent.height;
	VkCommandBuffer cmd = g_vk.cmdBuffers[g_vk.frameIndex];
	g_screenQuad.RecordRect(g_vk, cmd, g_vk.frameIndex,
		white->view, white->sampler,
		m.lx, m.ly, m.lx + m.vw, m.ly + m.vh,
		tw, th,
		/*flipUV_Y=*/false, MAKE_RGBA(0, 0, 0, alpha));
}

// ---------------------------------------------------------------------------
// vkchain_load_artwork. The VK mirror of run_game Step 6's GL load_artwork
// call - run_game invokes this per game load, between games.
// Frees the previous game's artwork VkTextures first; frames submitted for
// the previous game may still be in flight sampling them, so the device is
// drained before the destroy (same discipline as EnsureRasterRenderer's
// rebuild). A mid-frame call would be a caller bug: the drain cannot run
// with a frame open, so it is refused loudly instead of deadlocking.
// ---------------------------------------------------------------------------
void vkchain_load_artwork(const struct artworks* p)
{
	if (!s_initialized)
		return;
	if (s_frameOpen)
	{
		LOG_ERROR("vkchain_load_artwork: called mid-frame; skipping (artwork not loaded)");
		return;
	}
	if (g_vk.device && g_vk.vkDeviceWaitIdle_)
		g_vk.vkDeviceWaitIdle_(g_vk.device);
	VkArt_LoadForGame(g_vk, p);
}

// ---------------------------------------------------------------------------
// vkchain_load_layout. The VK mirror of run_game Step 7's GL
// Layout_LoadForGame call - the raster artwork path. Same drain discipline as
// vkchain_load_artwork: the previous game's layout textures are destroyed
// inside LayoutVK_LoadForGame, so the device must be idle first and no frame
// may be open.
//
// Note the GL call is unconditional for raster games because Layout_LoadForGame
// also builds a synthetic screen-only view; the VK loader deliberately does
// not (see layout_vk.h), so games with no .lay simply leave the layout off.
// ---------------------------------------------------------------------------
void vkchain_load_layout(const struct AAEDriver* drv)
{
	if (!s_initialized)
		return;
	if (s_frameOpen)
	{
		LOG_ERROR("vkchain_load_layout: called mid-frame; skipping (layout not loaded)");
		return;
	}
	if (g_vk.device && g_vk.vkDeviceWaitIdle_)
		g_vk.vkDeviceWaitIdle_(g_vk.device);
	LayoutVK_LoadForGame(g_vk, drv);
}

// ---------------------------------------------------------------------------
// vkchain_init_raster_overlay / vkchain_shutdown_raster_overlay
//
// VK twins of glchain_init_raster_overlay / glchain_shutdown_raster_overlay
// (opengl_renderer.cpp:384/427). Same gates, same load source
// (config.raster_effect out of aae.zip through the artwork search order), same
// "release whatever was there first" contract; run_game calls this per game
// load and the VIDEO menu calls it live when the user picks a different
// effect.
//
// Two VK-only wrinkles:
//  * the destroy needs a device drain (an in-flight frame may still sample the
//    old image), and a drain must not run with a frame open. The menu call
//    site sits inside cpu_run, i.e. mid-frame, so that case is deferred to the
//    next frame boundary via s_scanReloadPending (serviced in
//    vkchain_set_render).
//  * the texture is loaded stbi-FLIPPED, matching GL's load_texture
//    (stbi_set_flip_vertically_on_load(1)); see RecordScanlines' V-direction
//    note for why that matters for tiling phase. No mips: GL uses GL_NEAREST
//    min/mag on this texture.
// ---------------------------------------------------------------------------
void vkchain_init_raster_overlay(void)
{
	if (!s_initialized)
		return;

	// Mid-frame (live menu change): defer the whole thing to the next frame
	// boundary rather than draining the device under an open frame.
	if (s_frameOpen)
	{
		s_scanReloadPending = true;
		return;
	}

	// Release any texture left over from a previous game / effect.
	if (s_scanHave)
	{
		if (g_vk.device && g_vk.vkDeviceWaitIdle_)
			g_vk.vkDeviceWaitIdle_(g_vk.device);
		VK_DestroyTexture(g_vk, s_scanTex);
		s_scanTex = VkTexture{};
		s_scanHave = false;
	}

	// Skip if no raster effect is configured.
	if (!config.raster_effect ||
		config.raster_effect[0] == '\0' ||
		strcmp(config.raster_effect, "NONE") == 0)
	{
		LOG_INFO("Raster overlay (VK): disabled (raster_effect = NONE).");
		return;
	}

	// Only raster games use the scanlines overlay; skip for vector games.
	if (Machine && Machine->drv &&
		!(Machine->drv->video_attributes & VIDEO_RASTER_CLASS_MASK))
	{
		LOG_INFO("Raster overlay (VK): skipped (not a raster game).");
		return;
	}

	if (!VkArt_LoadFromArchive(g_vk, config.raster_effect, "aae.zip",
		/*flipY=*/true, /*generateMips=*/false, s_scanTex))
	{
		LOG_INFO("Raster overlay (VK): '%s' not found in aae.zip; disabled.",
			config.raster_effect);
		s_scanTex = VkTexture{};
		s_scanHave = false;
		return;
	}

	s_scanHave = true;
	LOG_INFO("Raster overlay (VK): loaded '%s' (%ux%u).",
		config.raster_effect, s_scanTex.width, s_scanTex.height);
}

void vkchain_shutdown_raster_overlay(void)
{
	if (!s_initialized)
		return;

	s_scanReloadPending = false;

	if (!s_scanHave)
		return;

	if (g_vk.device && g_vk.vkDeviceWaitIdle_)
		g_vk.vkDeviceWaitIdle_(g_vk.device);
	VK_DestroyTexture(g_vk, s_scanTex);
	s_scanTex = VkTexture{};
	s_scanHave = false;
}

int  vkchain_get_error(void) { return 0; }
