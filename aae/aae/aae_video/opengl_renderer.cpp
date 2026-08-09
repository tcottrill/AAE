//==========================================================================
// AAE - Another Arcade Emulator
// A MAME (TM) derivative based on early MAME code (0.29 through 0.90)
// mixed with original code. Created for amusement and archival purposes.
//
// All MAME code used in this emulator remains the copyright of the MAME
// Team. All MAME-derived code should be considered as belonging to them.
//
// Original AAE code copyright (C) 2025/2026 Tim Cottrill, released under
// the GNU GPL v3 or later. See accompanying source files for full details.
//==========================================================================
//
// opengl_renderer.cpp
//
// Core OpenGL rendering pipeline for AAE. Manages the multi-stage FBO
// compositing pipeline used to render both vector and raster games.
//
// Rendering pipeline overview:
//
//   STEP 1 - set_render()
//     Binds FBO1/img1a, sets 1024x1024 ortho. Vectors and raster polys
//     are drawn into this texture by the game-specific draw code.
//
//   STEP 2 - render()
//     Dispatches to draw_all() (vector) or raster_poly_update() + sc->Render()
//     (raster), then calls final_render().
//
//   STEP 3 - final_render()
//     Composites all layers (game image, overlay, feedback trail, glow blur,
//     bezel, scanlines) and writes the finished frame to FBO4.
//     end_render_fbo4() then blits FBO4 to the backbuffer, scaled to the
//     actual window size and aspect ratio.
//
// FBO / Texture layout:
//   FBO1 - img1a (attachment 0): current frame render target (1024x1024)
//          img1b (attachment 1): feedback/trail accumulation buffers
//          img1c (attachment 2): additional feedback blend buffer
//   FBO2 - img2a: 512x512 downsampled image for glow blur pass 1
//   FBO3 - img3a (attachment 0): 256x256 pingpong blur target A
//          img3b (attachment 1): 256x256 pingpong blur target B
//   FBO4 - img4a: final composited frame, blitted to screen at window size
//
// Artwork texture layout:
//   art_tex[0] - Backdrop (behind game screen)
//   art_tex[1] - Overlay  (color gel over game screen)
//   art_tex[2] - Bezel mask (used for Tempest/Tacscan rotation bezels, Depricated)
//   art_tex[3] - Bezel frame (rendered on top of everything)
//   art_tex[4] - Screen burn (reserved, not currently used)
//
//==========================================================================

#include "opengl_renderer.h"
#include "sys_window.h"  // GetWindowSetup
#include "sys_gl.h"
#include "sys_str.h"     // aae_stricmp
#include "iniFile.h"     // get_config_int, for the vsync diagnostic in init_gl
#include "aae_mame_driver.h"
#include "old_mame_raster.h"  // main_bitmap; no longer pulled in via osd_video.h
#include "vector_fonts.h"
#include "texture_handler.h"
#include "gl_fbo.h"
#include "gl_texturing.h"
#include "gl_shader.h"
#include "vector_draw_gl.h"   // draw_textured_shots
#include "vector_draw.h"
#include "fast_poly.h"
#include "raster_emit.h"
#include "os_basic.h"
#include "MathUtils.h"
#include "menu.h"
#include "controller_help.h"   // controller_help_active (PAUSED overlay gate)
#include "aae_emulator.h"   // get_exit_confirm_status / get_exit_confirm_selection
#include "mame_layout.h"
// iniFile.h, with the capital F the file actually has. Spelled "inifile.h"
// until 2026-07-29 and it built everywhere anyway: Windows is case-insensitive,
// and so is the /mnt/c drvfs mount the WSL Linux build runs on. It first failed
// inside the Flatpak, where sources are copied onto a real case-sensitive
// filesystem - which is also what a from-source build on the Steam Machine or a
// Pi would hit.
#include "iniFile.h"
#include "mame_vector.h"
#include "config.h"                            // RENDERER_VULKAN (overlay reuse guard)
#include "../aae_video_vk/vulkan_renderer.h"   // vkchain_ui_dim_quad (overlay reuse seam)
#include <chrono>   // for optional frame-time profiling
#include <cstring>  // strcmp for raster_effect name check
#include <cmath>    // log2f for the mono monitor halation mip bias
#include "aae_avg.h"
// ---------------------------------------------------------------------------
// Module-level globals
// ---------------------------------------------------------------------------
extern int AVG_BUSY;
// Calculated screen rectangle used to blit FBO4 to the window at the correct
// size and aspect ratio. Allocated in init_gl(), freed on shutdown.
Rect2* screen_rect = nullptr;

// Projection mirrored from set_ortho*/set_ortho_raster for the core-profile quad shaders.
aae::math::mat4 g_proj;

// Raster polygon renderer. One instance per application lifetime.
Fpoly* sc;

// Vector shot mode (procedural vs textured) lives in config.shots_textured, loaded
// from aae.ini and the Video menu. The modern beam is the only vector engine.

// Scale factor applied when mapping raster pixels to polygon positions.
//extern float vid_scale;

// Scanlines / raster-effect overlay texture handle.
// Loaded per-game by init_raster_overlay(); 0 means disabled or not loaded.
static GLuint g_scanrezTex = 0;

// Scanline fullscreen-quad GPU resources (VAO/VBO, created once in init_gl).
static GLuint g_scanVAO = 0;
static GLuint g_scanVBO = 0;

// Vertex layout for the scanline quad: NDC position + tiling texcoord.
struct ScanQuadVert { float px, py, tx, ty; };

// Screen rectangle coordinates (in 1024-space) for the active game image.
// Set by setup_game_config() via Widescreen_calc() and layout helpers.
// sx/sy = top-left corner, ex/ey = bottom-right corner.
// These are referenced by final_render() and the bezel placement code.
extern int sx, sy, ex, ey;

// Bezel crop/zoom parameters read from the artwork config.
extern int bezelx, bezely;
extern float bezelzoom;

// Rotation direction for the current game.
enum RotationDir { NONE, RIGHT, LEFT, OVER } rotation;

// Per-axis adjustment sliders (currently unused, reserved for future use).
int adj_horiz = 0;
int adj_vert = 0;

// ---------------------------------------------------------------------------
// orientation_to_rect2_rotation
// Converts ORIENTATION_xxx flags (from Machine->orientation or
// config.system_rotation) to the Rect2 rotation index used by
// UpdateScreenRect():
//   0 = normal, 1 = rotate right (CW 90), 2 = rotate left (CCW 90), 3 = 180
// ---------------------------------------------------------------------------
static int orientation_to_rect2_rotation(int orientation)
{
	// Only the system rotation component determines the Rect2 index.
	// The driver rotation describes the cabinet monitor orientation and
	// is handled by the game's coordinate generation; the system rotation
	// is the user-requested display-time rotation (-ror / -rol).
	switch (orientation)
	{
	case ROT90:  return 1; // -ror: rotate right
	case ROT270: return 2; // -rol: rotate left
	case ROT180: return 3; // 180 flip
	default:     return 0; // no system rotation
	}
}

// ---------------------------------------------------------------------------
// emulator_on_window_resize
// Called by the OS message handler whenever the client area changes size.
// Updates screen_rect so the final blit tracks the new window dimensions.
// ---------------------------------------------------------------------------
void glchain_on_window_resize(int newW, int newH)
{
	if (!screen_rect) return;

	auto& ws = GetWindowSetup();
	int rot = orientation_to_rect2_rotation(config.system_rotation);
	screen_rect->UpdateScreenRect(ws.clientWidth, ws.clientHeight, ws.aspectRatio, rot);
	LOG_INFO("Window resized - new client area: %d x %d (rotation=%d)", ws.clientWidth, ws.clientHeight, rot);
}

// ---------------------------------------------------------------------------
// raster_poly_update
// The backend-neutral emit loop lives in raster_emit.cpp; this wrapper feeds
// it into the GL Fpoly (Y-down ortho, so no flip).
// ---------------------------------------------------------------------------
static void GlRasterSink(void* user, float x, float y, float size, uint32_t rgba)
{
	((Fpoly*)user)->addPoly(x, y, size, rgba);
}

void raster_poly_update(void)
{
	if (!sc)
		return;
	raster_emit_polys(GlRasterSink, sc, /*yFlip=*/0);
}

// TBD: Change or remove.
// ---------------------------------------------------------------------------
// Widescreen_calc
// Computes wideadj - a horizontal scale factor applied to the game viewport
// rectangle - so the game image fills the selected aspect ratio correctly.
//   0 = 4:3  (classic arcade)
//   1 = 16:9 (widescreen)
//   2 = 16:10
// ---------------------------------------------------------------------------
void Widescreen_calc()
{
	float val = 0;

	if (config.widescreen == 0) val = 1.3333f;
	if (config.widescreen == 1) val = 1.77f;
	if (config.widescreen == 2) val = 1.6f;

	//wideadj = (float)(1.3333 / val);
}

// ---------------------------------------------------------------------------
// set_ortho
// Convenience wrapper: sets viewport and a top-left-origin 2D ortho
// projection to the given dimensions. Used throughout the pipeline to
// switch between 1024x1024 (FBO space) and window-size (backbuffer) spaces.
// ---------------------------------------------------------------------------
void set_ortho(GLint width, GLint height)
{
	// Core path: only the viewport and the g_proj projection (consumed via the
	// uProj uniform by every draw) are needed. The fixed-function matrix stack is
	// no longer read by anything (Rect2 + Layout_Render are core now), so it is
	// not touched here -- which also keeps this valid under a core-profile context.
	glViewport(0, 0, width, height);
	g_proj = aae::math::ortho(0.0f, (float)width, 0.0f, (float)height);
}

// ---------------------------------------------------------------------------
// set_ortho_raster
// Y-DOWN ortho projection used by the raster rendering path.
// Unlike set_ortho (which is Y-up, origin at bottom-left), this sets origin
// at top-left with Y increasing downward, matching the raster bitmap layout.
// This fixes the Y-flip that would occur if the standard vector ortho were used.
// ---------------------------------------------------------------------------
void set_ortho_raster(GLint width, GLint height)
{
	// Core path (see set_ortho): viewport + g_proj only. Y-DOWN ortho for raster.
	glViewport(0, 0, width, height);
	g_proj = aae::math::ortho(0.0f, (float)width, (float)height, 0.0f);
}

GLuint glcode_get_scanrez_tex()
{
	return g_scanrezTex;
}

// ---------------------------------------------------------------------------
// Scanline quad VAO/VBO helpers
// ---------------------------------------------------------------------------
static void init_scanline_quad()
{
	glGenVertexArrays(1, &g_scanVAO);
	glGenBuffers(1, &g_scanVBO);

	glBindVertexArray(g_scanVAO);
	glBindBuffer(GL_ARRAY_BUFFER, g_scanVBO);
	glBufferData(GL_ARRAY_BUFFER, 4 * sizeof(ScanQuadVert), nullptr, GL_DYNAMIC_DRAW);

	// Attribute 0: NDC position (2 floats)
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ScanQuadVert), (void*)0);
	// Attribute 1: texcoord   (2 floats)
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ScanQuadVert), (void*)(2 * sizeof(float)));

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void shutdown_scanline_quad()
{
	if (g_scanVAO) { glDeleteVertexArrays(1, &g_scanVAO); g_scanVAO = 0; }
	if (g_scanVBO) { glDeleteBuffers(1, &g_scanVBO);       g_scanVBO = 0; }
}

// ---------------------------------------------------------------------------
// init_gl
// One-time OpenGL initialization. Creates FBOs, compiles shaders, builds
// the font renderer, and initializes supporting subsystems.
//
// Protected by a static flag so it is safe to call more than once (e.g.,
// if the GUI calls it before a game is selected, and the emulator calls it
// again when launching - only the first call does anything).
//
// GUI note: This is intentionally called once and left active for the
// lifetime of the process. The GUI overlay driver can safely use all
// GL resources initialized here without re-initializing them.
// ---------------------------------------------------------------------------
int glchain_init(void)
{
	static int init_one = 0;
	check_gl_error_named("init_gl start");
	if (!init_one)
	{
		// --- VSync control ---
		// glchain_set_vsync (sys_gl.h) already does exactly this on both
		// platforms - WGL_EXT_swap_control on Windows, GLX_EXT/MESA/SGI swap
		// control on Linux - including reporting when no swap-control
		// extension exists. Calling wgl* directly here duplicated that and
		// was Windows-only. Called directly (not via the SetvSync dispatch):
		// this is the GL chain's own init, so it stays within the GL layer
		// rather than calling back up into renderer_dispatch.cpp.
		glchain_set_vsync(config.forcesync);
		LOG_INFO("VSync %s (config.forcesync=%d, raw ini force_vsync=%d).",
		         config.forcesync ? "enabled" : "disabled",
		         config.forcesync,
		         get_config_int("main", "force_vsync", -1));

		// --- Base GL state ---
		set_ortho(1024, 768);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// --- Screen rectangle (tracks window size and aspect ratio) ---
		auto& ws = GetWindowSetup();
		int rot = orientation_to_rect2_rotation(config.system_rotation);
		screen_rect = new Rect2(ws.clientWidth, ws.clientHeight, ws.aspectRatio, rot);

		// NOTE: Scanlines texture loading is NOT done here. It is deferred to
		// init_raster_overlay(), which is called per-game from run_game() after
		// setup_game_config() has set the correct config.raster_effect value.

		// --- FBO allocation ---
		LOG_INFO("Initializing FBOs...");
		fbo_init();
		// Temp init for the mame vector renderer
		if (Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR)
		{
			vector_start();
			beam_init(1);          // ssaa = 1: fbo1 is a plain 1024 buffer
		}

		// --- Shader compilation ---
		init_shader();

		// --- Scanline overlay quad (VAO/VBO) ---
		init_scanline_quad();

		// --- Vector font renderer ---
		LOG_INFO("Building vector font...");
		VF.Initialize(1024, 768);

		// --- Raster polygon renderer ---
		sc = new Fpoly();

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		LOG_INFO("OpenGL initialization complete.");

		init_one++;
	}
	check_gl_error_named("init_gl");
	return 1;
}

// ---------------------------------------------------------------------------
// end_gl
// Shutdown: release subsystems that require explicit cleanup.
// Call this once when the application exits.
// ---------------------------------------------------------------------------
void glchain_end()
{
	shutdown_scanline_quad();
	//TiledEffect_Shutdown();
	LOG_INFO("AAE GL shutdown.");
}

// ---------------------------------------------------------------------------
// init_raster_overlay
// Loads the per-game scanlines/raster-effect overlay texture.
//
// Must be called AFTER init_gl() and setup_game_config() so that:
//   - The GL context is ready.
//   - config.raster_effect holds the correct per-game texture name.
//
// Safe to call multiple times - always releases any previously loaded
// texture before attempting a new load.
//
// Sets g_scanrezTex to a valid GL texture handle on success, or 0 if
// loading is disabled or the file is not found.
// ---------------------------------------------------------------------------
void glchain_init_raster_overlay()
{
	// Release any texture left over from a previous game.
	if (g_scanrezTex != 0)
	{
		glDeleteTextures(1, &g_scanrezTex);
		g_scanrezTex = 0;
	}

	// Skip if no raster effect is configured.
	if (!config.raster_effect ||
		config.raster_effect[0] == '\0' ||
		std::strcmp(config.raster_effect, "NONE") == 0)
	{
		LOG_INFO("Raster overlay: disabled (raster_effect = NONE).");
		return;
	}

	// Only raster games use the scanlines overlay; skip for vector games.
	if (Machine && Machine->drv &&
		!(Machine->drv->video_attributes & VIDEO_RASTER_CLASS_MASK))
	{
		LOG_INFO("Raster overlay: skipped (not a raster game).");
		return;
	}

	// Load from the shared aae.zip artwork archive.
	if (!make_single_bitmap(&g_scanrezTex, config.raster_effect, "aae.zip", 0))
	{
		LOG_INFO("Raster overlay: '%s' not found in aae.zip; disabled.", config.raster_effect);
		g_scanrezTex = 0;
	}
	else
	{
		LOG_INFO("Raster overlay: loaded '%s' (texID=%u).", config.raster_effect, g_scanrezTex);
	}
}

// ---------------------------------------------------------------------------
// shutdown_raster_overlay
// Releases the scanlines texture. Safe to call even if nothing is loaded.
// Called from emulator_stop_game() during per-game teardown.
// ---------------------------------------------------------------------------
void glchain_shutdown_raster_overlay()
{
	if (g_scanrezTex != 0)
	{
		glDeleteTextures(1, &g_scanrezTex);
		g_scanrezTex = 0;
		LOG_INFO("Raster overlay: texture released.");
	}
}

// ---------------------------------------------------------------------------
// glcode_vector_hard_clear_fbo1
// Clears all three attachments of FBO1 (img1a, img1b, img1c) to opaque
// black. Used when starting a new vector game to flush any leftover trail
// or feedback data from a previous session.
// Saves and restores the previously bound FBO and viewport.
// ---------------------------------------------------------------------------
void glchain_vector_hard_clear_fbo1()
{
	if (!fbo1)
		return;

	GLint prevFbo = 0;
	GLint prevVP[4] = { 0, 0, 0, 0 };
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
	glGetIntegerv(GL_VIEWPORT, prevVP);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo1);
	glViewport(0, 0, 1024, 1024);

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glClearColor(0, 0, 0, 0);

	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(GL_COLOR_ATTACHMENT1);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(GL_COLOR_ATTACHMENT2);
	glClear(GL_COLOR_BUFFER_BIT);

	// Restore previous FBO and viewport.
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
	glViewport(prevVP[0], prevVP[1], prevVP[2], prevVP[3]);
}

// ---------------------------------------------------------------------------
// set_render_fbo4
// Binds FBO4 and prepares it for final compositing. All game image layers,
// the bezel, and UI overlays are drawn here before the result is blitted to
// the backbuffer by end_render_fbo4().
// ---------------------------------------------------------------------------
void set_render_fbo4()
{
	glBindFramebuffer(GL_FRAMEBUFFER, fbo4);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);

	set_ortho(1024, 1024);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glEnable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DITHER);   // required for some older cards
}

// ---------------------------------------------------------------------------
// end_render_fbo4
// Unbinds FBO4 and blits img4a (the composited frame) to the backbuffer,
// scaled and positioned by screen_rect to match the window size and aspect.
// ---------------------------------------------------------------------------
void end_render_fbo4()
{
	check_gl_error_named("end_render_fbo4 (enter)");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDrawBuffer(GL_BACK);
	glActiveTexture(GL_TEXTURE0);

	// Clear the backbuffer so pillarbox/letterbox bars are always clean.
	// Without this, stale pixels persist outside the screen_rect quad
	// when the aspect ratio changes in fullscreen.
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	auto& ws = GetWindowSetup();
	set_ortho(ws.clientWidth, ws.clientHeight);

	glDisable(GL_BLEND);

	// Blit img4a to the screen. Blending disabled: this is a straight copy.
	// screen_rect->Render() handles letterboxing / pillarboxing for the
	// configured aspect ratio (1.33f = 4:3).
	set_texture(&img4a, 1, 0, 0, 0);
	screen_rect->Render(aae::math::value_ptr(g_proj));   // g_proj == the set_ortho above

	check_gl_error_named("end_render_fbo4 (exit)");
}

////////////////////////////////////////////////////////////////////////////////
// FBO DOWNSAMPLING AND BLUR CODE (supports the vector glow effect)           //
//                                                                             //
// The glow effect is produced by downsampling the rendered frame to 512x512  //
// (fbo2), then to 256x256 (fbo3), and blurring at the lower resolution with  //
// a multi-pass offset shader. The blurred result is composited in the final   //
// shader as an additive glow layer.                                           //
////////////////////////////////////////////////////////////////////////////////

// ---------------------------------------------------------------------------
// copy_main_img_to_fbo2
// Downsample step 1: copies img1b (1024x1024 feedback buffer) into fbo2 at
// 512x512 via the blur shader. The downsampled image is stored in img2a.
// ---------------------------------------------------------------------------
void copy_main_img_to_fbo2()
{
	fbo_generate_mipmaps({ img1b });

	GLuint fbo2_tex = 0;
	glBindFramebuffer(GL_FRAMEBUFFER, fbo2);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	set_ortho(512, 512);
	glDisable(GL_BLEND);

	set_texture(&img1b, 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE0);

	bind_shader(fragBlur);
	check_gl_error_named("copy_main_img_to_fbo2");
	set_uniform1i(fragBlur, "colorMap", fbo2_tex);
	set_uniform1f(fragBlur, "width", 512.0f);
	set_uniform1f(fragBlur, "height", 512.0f);

	FS_Rect(0, 512);
	unbind_shader();
}

// ---------------------------------------------------------------------------
// copy_fbo2_to_fbo3
// Downsample step 2: copies img2a (512x512) into fbo3 at 256x256 via the
// blur shader. Result is stored in img3a (attachment 0).
// Clears attachment 1 (img3b) first so the pingpong is clean each frame.
// ---------------------------------------------------------------------------
void copy_fbo2_to_fbo3()
{
	GLuint fbo3_tex = 0;
	glBindFramebuffer(GL_FRAMEBUFFER, fbo3);

	// Clear both pingpong buffers before each frame.
	glDrawBuffer(GL_COLOR_ATTACHMENT1);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	set_ortho(256, 256);
	glDisable(GL_BLEND);

	check_gl_error_named("copy_fbo2_to_fbo3");

	bind_shader(fragBlur);
	set_uniform1i(fragBlur, "colorMap", fbo3_tex);
	set_uniform1f(fragBlur, "width", 256.0f);
	set_uniform1f(fragBlur, "height", 256.0f);

	set_texture(&img2a, 1, 0, 0, 1);
	FS_Rect(0, 256);
	unbind_shader();
}

// ---------------------------------------------------------------------------
// render_blur_image_fbo3
// Blur step: pingpongs between img3a and img3b in fbo3 across 4 passes,
// each time drawing with a small sub-pixel offset (fshifta / fshiftb arrays)
// and additive blending to accumulate a soft glow.
//
// v1 and v2 control the near and far sample distances. Increasing them
// widens the glow at the cost of some precision.
// ---------------------------------------------------------------------------
void render_blur_image_fbo3()
{
	static constexpr float v1 = 1.0f;  // near sample offset (pixels at 256x256)
	static constexpr float v2 = 2.0f;  // far sample offset

	// Global sub-pixel correction applied to all quads to keep the blurred
	// image centered relative to the source.
	const float globalOffsetX = -0.05f;
	const float globalOffsetY = -0.20f;

	// Each row's first pair (x0,y0) is the tap direction for one pingpong pass;
	// (x1,y1) is currently unused. Rows 0-3 are the axis taps (E/W/N/S); rows 4-7
	// are the diagonals (NE/SW/NW/SE). The BLUR_8TAP toggle below picks 4 or 8.
	float fshifta[] = {
		 v1,  0,  -v1,   0,
		-v1,  0,   v1,   0,
		  0,  v1,   0, -v1,
		  0, -v1,   0,  v1,
		 v1,  v1, -v1, -v1,
		-v1, -v1,  v1,  v1,
		-v1,  v1,  v1, -v1,
		 v1, -v1, -v1,  v1
	};

	float fshiftb[] = {
		 v2,  0,  -v2,   0,
		-v2,  0,   v2,   0,
		  0,  v2,   0, -v2,
		  0, -v2,   0,  v2,
		 v2,  v2, -v2, -v2,
		-v2, -v2,  v2,  v2,
		-v2,  v2,  v2, -v2,
		 v2, -v2, -v2,  v2
	};

	bind_shader(fragBlur);
	set_uniform1i(fragBlur, "colorMap", 0);
	set_uniform1f(fragBlur, "width", 256.0f);
	set_uniform1f(fragBlur, "height", 256.0f);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive blend accumulates glow

	// Lambda to draw one offset quad. Converts float offsets to screen-space
	// by adding globalOffset and sizing to height3 (the FBO3 height, 256).
	auto DrawQuadOffset = [&](float ox, float oy) {
		float x1 = ox + globalOffsetX;
		float y1 = oy + globalOffsetY;
		float x2 = (float)height3 + x1;
		float y2 = (float)height3 + y1;
		// (left,right)=X span [x1,x2]; (bottom,top)=Y span [y2,y1].
		// y2 maps size+y1 down to y1, matching the orientation of FS_Rect(0,size).
		drawTexturedQuad(x1, x2, y2, y1, 1);
		};

	const int kBlurPasses = 4;   // rows 0-3: axis only

	int i = 0;

	for (int pass = 0; pass < kBlurPasses; ++pass)
	{
		// A -> B: draw img3a into attachment 1 (img3b) with near offset.
		glDrawBuffer(GL_COLOR_ATTACHMENT1);
		set_texture(&img3a, 1, 0, 0, 0);
		DrawQuadOffset(fshifta[i], fshifta[i + 1]);

		// B -> A: draw img3b into attachment 0 (img3a) with far offset.
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		set_texture(&img3b, 1, 0, 0, 0);
		DrawQuadOffset(fshiftb[i], fshiftb[i + 1]);

		i += 4;
	}

	check_gl_error_named("render_blur_image_fbo3");
	unbind_shader();
}

// ---------------------------------------------------------------------------
// render_blur_dualfilter - [main] glow_filter=1 prototype.
//
// Dual-filter pyramid (Kawase/Bjorge, the ARM tile-GPU bloom): img3a (256)
// is downsampled 128 -> 64 -> 32 with a 5-tap kernel, then upsampled back
// 64 -> 128 -> 256 with an 8-tap kernel, ending in img3b so the composite
// (fragMulti's mytex3) needs no changes. Spread comes from pyramid depth;
// ~0.8M taps vs the classic path's ~7.7M, with no blending (no destination
// tile loads) and no per-frame mipmap generation on any pyramid level.
//
// The classic accumulate blur's signature - hot core, long soft tail - is
// reproduced by re-injecting each down level during the matching up pass
// (kTail) and the unblurred 256 source at the end (kCore), instead of by
// additive accumulation.
//
// Tuning: config.glow2_* (aae.ini [main] glow2_* keys + the VECTOR MONITOR
// SETUP menu). Read per frame so menu adjustments apply live:
//   glow2_spread  tap radius scale inside every level    (default 1.0)
//   glow2_tail    down-level re-injection weight          (default 0.6)
//   glow2_core    unblurred-source weight in final pass   (default 1.0)
//   glow2_gain    final output gain                       (default 10.0)
//
// The gain default is NOT arbitrary. The classic accumulate blur roughly
// doubles the glow buffer's energy on each of its 8 additive passes, driving
// it into saturation, and the composite then scales by glowamt (vecglow *
// 0.01 - e.g. 0.07). This chain is energy-preserving, so at gain 1.0 it
// feeds that same * 0.07 composite a signal ~50x dimmer: a technically
// perfect, completely invisible glow (measured the hard way). The large
// gain + RGB8's natural clamp at 1.0 reproduces the classic path's
// blown-out-core-with-soft-tail, on purpose.
// ---------------------------------------------------------------------------
void render_blur_dualfilter()
{
	const float kSpread = config.glow2_spread;
	const float kTail   = config.glow2_tail;
	const float kCore   = config.glow2_core;
	const float kGain   = config.glow2_gain;

	glDisable(GL_BLEND);   // pure overwrites - the whole point on a tiler

	// One pass: bind dst FBO, source texture(s), draw a full-target quad.
	auto down = [&](rfbo_t dstFbo, int dstSize, rtex_t srcTex) {
		glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		set_ortho(dstSize, dstSize);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, srcTex);
		set_uniform2f(fragDualDown, "uHalfPixel",
		              0.5f / (float)dstSize * kSpread,
		              0.5f / (float)dstSize * kSpread);
		FS_Rect(0, dstSize);
	};

	auto up = [&](rfbo_t dstFbo, GLenum dstAttach, int dstSize, int srcSize,
	              rtex_t srcTex, rtex_t addTex, float addWeight, float gain) {
		glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
		glDrawBuffer(dstAttach);
		set_ortho(dstSize, dstSize);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, addTex);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, srcTex);
		set_uniform2f(fragDualUp, "uHalfPixel",
		              0.5f / (float)srcSize * kSpread,
		              0.5f / (float)srcSize * kSpread);
		set_uniform1f(fragDualUp, "uAddWeight", addWeight);
		set_uniform1f(fragDualUp, "uGain", gain);
		FS_Rect(0, dstSize);
	};

	// img3a is a trilinear texture and the first down pass minifies it 2:1 -
	// that samples mip level ~1, which is STALE here (mips are only generated
	// after the blur). Drop it to plain bilinear for the pyramid read: at an
	// exact 2:1 ratio, level-0 bilinear IS the correct box filter. Restored
	// after the chain so the rest of the pipeline sees the usual state.
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, img3a);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	// Down chain: img3a 256 -> 128 -> 64 -> 32.
	bind_shader(fragDualDown);
	set_uniform1i(fragDualDown, "uSrc", 0);
	down(fbo_pyr[0], 128, img3a);
	down(fbo_pyr[1],  64, img_pyr[0]);
	down(fbo_pyr[2],  32, img_pyr[1]);

	// Up chain: 32 -> 64 -> 128 -> 256 (img3b), re-adding detail on the way.
	bind_shader(fragDualUp);
	set_uniform1i(fragDualUp, "uSrc", 0);
	set_uniform1i(fragDualUp, "uAdd", 1);
	up(fbo_pyr[3],          GL_COLOR_ATTACHMENT0,  64, 32, img_pyr[2], img_pyr[1], kTail, 1.0f);
	up(fbo_pyr[4],          GL_COLOR_ATTACHMENT0, 128, 64, img_pyr[3], img_pyr[0], kTail, 1.0f);
	up(fbo3,                GL_COLOR_ATTACHMENT1, 256, 128, img_pyr[4], img3a,     kCore, kGain);

	// Restore img3a's trilinear MIN filter (see note above the down chain).
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, img3a);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

	check_gl_error_named("render_blur_dualfilter");
	unbind_shader();
}

// ====================================================================
// render_ui_overlays()
//
// Draws the pause dim, PAUSED text, exit confirmation dialog, menu,
// FPS counter, and debug overlays on top of the current game frame.
//
// Called by BOTH rendering paths:
//   - Vector pipeline: from final_render() INTO fbo4, before the
//     end_render_fbo4() screen_rect blit (so the overlay rotates with
//     the frame).
//   - Raster pipeline: from final_render_raster() onto the backbuffer.
//     For a system-rotated game it is instead rendered into fbo4
//     (fboSpace=true) so the same screen_rect blit rotates it to match.
//
// Projection: 1024x768 ortho normally, or the 1024x1024 FBO space when
// fboSpace is set. The caller must have rendered the game frame first.
// ====================================================================
void render_ui_overlays(int winW, int winH, bool fboSpace)
{
	if (winW < 1 || winH < 1) return;

	// Vulkan chain reuse (vkchain_render -> RecordUiOverlays): the CONTENT of
	// this function - the pause dim, PAUSED text, exit dialog, and everything
	// video_loop() draws (menu, FPS, debug, error) - is backend-neutral: the
	// VF.* calls accumulate CPU-side and route through the beam queue /
	// vkchain_gui_draw_quad under Vulkan. Only the raw GL calls (viewport/
	// ortho/blend/FBO state, quad_from_center) are skipped there, with the dim
	// quads routed to vkchain_ui_dim_quad instead. 'vk' is false for the whole
	// GL chain, so GL behavior is untouched.
	const bool vk = (active_renderer() == RENDERER_VULKAN);

	// WIDESCREEN CORRECTION:
	// If wider than 4:3, narrow and center the overlay viewport
	int vpX = 0;
	int vpW = winW;
	float aspect = (float)winW / (float)winH;
	if (aspect > (4.0f / 3.0f + 0.01f))
	{
		vpW = (int)(winH * (4.0f / 3.0f));
		vpX = (winW - vpW) / 2;
	}

	if (!vk)
	{
		glViewport(vpX, 0, vpW, winH);
		g_proj = aae::math::ortho(0.0f, 1024.0f, 0.0f, 768.0f);
	}

	// Tell VF not to override our viewport when Begin() is called.
	// VF's internal 1024x768 ortho projection still maps correctly
	// because glViewport stretches the output to the full window.
	VF.SetOverrideViewport(false);

	// Overlay logical height: matches the active ortho (1024 in the square-FBO
	// space, 768 on the raster backbuffer). The full-screen dim spans this so it
	// covers the whole frame, not just the bottom 768 of a 1024 space.
	float uiH = 768.0f;
	if (fboSpace || (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR))
	{
		// Vector overlays -- and rotated raster overlays composited into fbo4 --
		// render onto the 1024x1024 FBO, so switch the ortho to match. screen_rect
		// then rotates/letterboxes the blit to the window.
		if (!vk)
			set_ortho(1024, 1024);
		uiH = 1024.0f;
	}
	// else: keep the 1024x768 ortho already set above for raster window overlays.

	//------------------------------------------------------------------
	// Dim background and draw PAUSED text if needed
	//------------------------------------------------------------------
	// Skipped while the controller guide is up: it holds paused=1 itself, and
	// this block runs BEFORE video_loop -- on the VK chain these VF strokes
	// ride the deferred beam queue and would be recorded AFTER the guide's
	// backdrop, stamping PAUSED on top of it (GL flushes immediately and is
	// merely dimmed pointlessly under the opaque backdrop).
	if ((paused || get_menu_status()) && !controller_help_active())
	{
		if (vk)
			vkchain_ui_dim_quad(127);
		else
		{
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			quad_from_center(512.0f, uiH * 0.5f, 1024.0f, uiH, 0, 0, 0, 127);
		}

		if (get_menu_status() == 0)
		{
			VF.Begin();
			VF.PrintCentered(30, RGB_WHITE, 5.0f, "PAUSED");
			VF.End();
		}
	}

	//------------------------------------------------------------------
	// Exit confirmation dialog
	//------------------------------------------------------------------
	if (get_exit_confirm_status())
	{
		if (!paused && !get_menu_status())
		{
			if (vk)
				vkchain_ui_dim_quad(216);
			else
			{
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				quad_from_center(512.0f, uiH * 0.5f, 1024.0f, uiH, 0, 0, 0, 216);
			}
		}

		const int sel = get_exit_confirm_selection();

		const unsigned int colSelected = RGB_YELLOW;
		const unsigned int colUnselected = RGB_WHITE;
		const unsigned int colTitle = RGB_WHITE;

		const float yTitle = 430.0f;
		const float yOptions = 370.0f;
		const float yHint = 320.0f;
		const float scTitle = 4.0f;
		const float scOption = 3.0f;
		const float scHint = 1.6f;
		const float labelGap = 60.0f;
		const float centerX = 512.0f;

		const char* strYes = (sel == 0) ? "< YES >" : "YES";
		const char* strNo = (sel == 1) ? "< NO >" : "NO";

		const float pitchYes = VF.GetStringPitch(strYes, scOption, 0);
		const float pitchNo = VF.GetStringPitch(strNo, scOption, 0);

		const float xYes = centerX - (labelGap / 2.0f) - pitchYes;
		const float xNo = centerX + (labelGap / 2.0f);

		VF.Begin();

		{
			const char* strTitle = "EXIT GAME?";
			const float pitchTitle = VF.GetStringPitch(strTitle, scTitle, 0);
			const float xTitle = centerX - (pitchTitle / 2.0f);
			VF.Print(xTitle, (int)yTitle, colTitle, scTitle, strTitle);
		}

		VF.Print(xYes, (int)yOptions, (sel == 0) ? colSelected : colUnselected, scOption, strYes);
		VF.Print(xNo, (int)yOptions, (sel == 1) ? colSelected : colUnselected, scOption, strNo);

		{
			const char* strHint = "LEFT / RIGHT to choose ENTER OR A to confirm  ESC to cancel";
			const float pitchHint = VF.GetStringPitch(strHint, scHint, 0);
			const float xHint = centerX - (pitchHint / 2.0f);
			VF.Print(xHint, (int)yHint, RGB_CYAN, scHint, strHint);
		}

		VF.End();
	}

	//------------------------------------------------------------------
	// Per-game overlay / score display, menu, FPS, debug adjustments
	//------------------------------------------------------------------
	video_loop();

	if (!vk)
	{
		// Restore GL state for the next frame's vector pipeline
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	// Restore VF to default behavior for the vector pipeline
	VF.SetOverrideViewport(true);
}

////////////////////////////////////////////////////////////////////////////////
// RENDERING PIPELINE - STEPS 1, 2, and 3                                    //
////////////////////////////////////////////////////////////////////////////////

// ---------------------------------------------------------------------------
// set_render [STEP 1]
// Binds the correct FBO for the current game type and prepares the render
// target for the frame. Vector games use FBO1 (img1a) at 1024x1024 with
// Y-up ortho. Raster games use fbo_raster (img5a) at the game's native
// visible_area size * prescale, with Y-DOWN ortho so the bitmap pixels
// land correctly without a vertical flip.
// ---------------------------------------------------------------------------
void glchain_set_render()
{
	// Set 1024x1024 ortho to match the FBO dimensions.
	if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
	{	// Bind FBO1 and direct output to attachment 0 (img1a).
		glBindFramebuffer(GL_FRAMEBUFFER, fbo1);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		set_ortho(1024, 1024);
		VF.SetOverrideViewport(false);
	}
	else
	{
		const rectangle& va = Machine->drv->visible_area;

		int vw = (va.max_x - va.min_x + 1);
		int vh = (va.max_y - va.min_y + 1);

		// Match the raster FBO allocation shape for rotated games.
		if (Machine->drv->rotation & ORIENTATION_SWAP_XY)
		{
			int t = vw;
			vw = vh;
			vh = t;
		}

		const int rw = static_cast<int>((float)vw * config.prescale);
		const int rh = static_cast<int>((float)vh * config.prescale);

		glBindFramebuffer(GL_FRAMEBUFFER, fbo_raster);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);

		// Y-down ortho: matches the raster bitmap layout (origin top-left).
		// MUST match fbo_init_raster() dimensions exactly.
		set_ortho(rw, rh);
	}
	// Only clear the frame if the game is actively running!
	// This preserves the last frame in memory for the background while paused/in-menu.
	if (!paused)
	{
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	check_gl_error_named("set_render");
}

// ---------------------------------------------------------------------------
// render [STEP 2]
// Main per-frame render dispatch. Handles the paused state, then routes to
// the vector or raster draw path before calling the appropriate final_render.
// ---------------------------------------------------------------------------
void glchain_render()
{
	// Only process new game geometry if we are not paused.
	// (If paused, FBO1 retains the image from the last active frame).
	if (!paused)
	{
		if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
		{
			vector_update();
			aae::math::mat4 proj = aae::math::ortho(0.0f, 1024.0f, 0.0f, 1024.0f);
			beam_draw_all(proj);
			if (config.shots_textured)
				draw_textured_shots(proj);   // legacy textured shots over the modern beam
			vector_clear_list();
		}
		else
		{
			raster_poly_update();
			sc->Render();
		}
	}

	// ALWAYS composite the layers. This applies game_rect boundaries and
	// shaders to the frozen frame exactly as it did when running.
	if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
		final_render(game_rect_left, game_rect_right, game_rect_bottom, game_rect_top);
	else
		final_render_raster();
}

// Composites all rendering layers into FBO4 and presents the result.
//
// Parameters define the game screen rectangle in 1024-space:
//   xmin/xmax = horizontal extent (sx/ex from game config)
//   ymin/ymax = vertical extent   (sy/ey from game config)
//
// Layer order (back to front):
//   1. img1a -> img1b : copy current frame with optional B/W or additive blend
//   2. art_tex[1]     : color overlay (if enabled)
//   3. img1b -> img1c : vector trail / phosphor persistence (if enabled)
//   4. FBO2/3 blur    : glow downsample+blur passes (if enabled)
//   5. fragMulti shader: composites img1b + blur + backdrop in one pass
//   6. Bezel frame    : art_tex[3] drawn on top with alpha test (if enabled)
//   7. Scanlines      : TiledEffect_Draw() for raster games (if enabled)
//   8. video_loop()   : any game-specific per-frame overlay (score display etc.)
// ---------------------------------------------------------------------------

void final_render(int left, int right, int bottom, int top)
{
	// TODO: HACK - When the GUI is running and the menu opens, the dim overlay would otherwise
	// stretch to 1024x1024 and squash the GUI layout. Clamp bottom to 768-space.
	if (Machine && Machine->drv && strcmp(Machine->drv->name, "gui") == 0 && (get_menu_status() || paused))
	{
		//	bottom = 1088;  // or whatever value corrects the stretch for your 1024x1024->768 pipeline
	}
	// NOTE:
	// Overlay behavior is controlled by the driver's video_attributes flags.
	// We MUST use the same source consistently here, otherwise overlay types
	// can be mis-detected and end up affecting the cabinet backdrop.
	const int vattr = (Machine && Machine->drv) ? Machine->drv->video_attributes : 0;
	const bool uses_overlay1 = (vattr & VECTOR_USES_OVERLAY1) != 0;
	const bool uses_overlay2 = (vattr & VECTOR_USES_OVERLAY2) != 0;

	GLint bleh = 0;
	int   useglow = 0;

	auto start = std::chrono::steady_clock::now();

	//--------------------------------------------------------------------------
	// LAYER 1: Copy img1a (current frame) into img1b.
	//--------------------------------------------------------------------------
	glBindFramebuffer(GL_FRAMEBUFFER, fbo1);
	glDrawBuffer(GL_COLOR_ATTACHMENT1);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	set_texture(&img1a, 1, 0, 0, 1);

	if (Machine->drv->video_attributes & VIDEO_TYPE_RASTER_BW)
		glBlendFunc(GL_ONE, GL_ZERO);
	else
		glBlendFunc(GL_ONE, GL_ONE);

	// Always do a pure 1:1 copy for the FBO buffers!
	FS_Rect(0, 1024);

	//--------------------------------------------------------------------------
	// LAYER 3: Vector trail / phosphor persistence (img1b -> img1c).
	//--------------------------------------------------------------------------
	if (config.vectrail && !emulator_is_gui_active()) //No vectrail for the gui
	{
		glDrawBuffer(GL_COLOR_ATTACHMENT2);
		glDisable(GL_DITHER);
		set_texture(&img1b, 1, 0, 0, 0);
		glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);

		float tr = 1.0f, tg = 1.0f, tb = 1.0f, ta = 1.0f;
		switch (config.vectrail)
		{
		case 1:  ta = 0.825f; break;
		case 2:  ta = 0.86f;  break;
		case 3:  ta = 0.93f;  break;
		default: tr = tg = tb = 0.95f; ta = 1.0f; break;
		}

		FS_Rect(0, 1024, tr, tg, tb, ta);
		fbo_generate_mipmaps({ img1b });
	}

	//--------------------------------------------------------------------------
	// LAYER 4: Glow blur passes (FBO2 and FBO3).
	//--------------------------------------------------------------------------
	if (config.vecglow && !emulator_is_gui_active()) // No Vecglow for the GUI
	{
		// config.glow_filter: 0 = classic 8-pass accumulate blur (default),
		// 1 = dual-filter pyramid (see render_blur_dualfilter). Read live so
		// the menu toggle applies immediately; log only on change so a
		// capture can be matched to the path that produced it.
		static int s_loggedFilter = -1;
		if (s_loggedFilter != config.glow_filter) {
			s_loggedFilter = config.glow_filter;
			LOG_INFO("Glow blur path: %s (glow_filter=%d)",
			         s_loggedFilter == 1 ? "dual-filter pyramid" : "classic accumulate",
			         s_loggedFilter);
		}

		copy_main_img_to_fbo2();
		copy_fbo2_to_fbo3();
		if (config.glow_filter == 1)
		{
			render_blur_dualfilter();
			// img2a only: next frame's 512->256 trilinear downsample needs
			// fresh mips, but the pyramid path samples img3a at level 0 and
			// the composite MAGNIFIES img3b - two of the three per-frame
			// mipmap generations are dead weight here.
			fbo_generate_mipmaps({ img2a });
		}
		else
		{
			render_blur_image_fbo3();
			fbo_generate_mipmaps({ img2a, img3a, img3b });
		}
	}

	//--------------------------------------------------------------------------
	// LAYER 5A: Build the CRT/game image into img4b (FBO4 attachment 1).
	//--------------------------------------------------------------------------
	glBindFramebuffer(GL_FRAMEBUFFER, fbo4);
	glDrawBuffer(GL_COLOR_ATTACHMENT1);
	set_ortho(1024, 1024);

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glDisable(GL_DITHER);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	fbo_generate_mipmaps({ img1a, img1b, img1c });
	// I said, no glow for the GUI!
	if (config.vecglow && !emulator_is_gui_active()) useglow = 1;

	bind_shader(fragMulti);

	bleh = glGetUniformLocation(fragMulti, "mytex2"); glUniform1i(bleh, 1);
	bleh = glGetUniformLocation(fragMulti, "mytex3"); glUniform1i(bleh, 2);
	bleh = glGetUniformLocation(fragMulti, "mytex4"); glUniform1i(bleh, 3);

	set_uniform1i(fragMulti, "usefb", config.vectrail);

	set_uniform1i(fragMulti, "usefb", config.vectrail);
	set_uniform1i(fragMulti, "useglow", useglow);
	set_uniform1f(fragMulti, "glowamt", (float)(config.vecglow * 0.01));
	set_uniform1i(fragMulti, "brighten", gamenum);

	glActiveTexture(GL_TEXTURE1); set_texture(&img1b, 1, 1, 0, 0);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, img3a); set_texture(&img3b, 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE3); set_texture(&img1c, 1, 0, 0, 0);

	//LOG_DEBUG("img4a into render: left=%d right=%d top=%d bottom=%d", left, right, top, bottom);
	drawTexturedQuad((float)left, (float)right, (float)bottom, (float)top, true);

	unbind_shader();

	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);

	//--------------------------------------------------------------------------
	// LAYER 5B: VECTOR_USES_OVERLAY1 - colorize the CRT-only image in-place.
	//--------------------------------------------------------------------------
	if (config.overlay && art_loaded[1] && uses_overlay1)
	{
		//float overlay_height =  (Machine->drv->rotation & ORIENTATION_SWAP_XY) ? (float)bottom : ((float)bottom * 0.75f);

		set_texture(&art_tex[1], 1, 0, 0, 0);

		glEnable(GL_BLEND);
		if (Machine->drv->video_attributes & VIDEO_TYPE_RASTER_BW)
			glBlendFunc(GL_DST_COLOR, GL_ZERO);
		else
			glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);

		drawTexturedQuad((float)left, (float)right, (float)top, (float)bottom, false);
	}

	//--------------------------------------------------------------------------
	// LAYER 5C: Composite to img4a (FBO4 attachment 0)
	//--------------------------------------------------------------------------
	set_render_fbo4();

	auto DrawCabinetScaledLayer = [&](GLuint tex, bool is_pre_squished,
		float rT = 1.0f, float gT = 1.0f, float bT = 1.0f, float aT = 1.0f, float alphaTest = 0.0f) {
			if (!tex) return;
			set_texture(&tex, 1, 0, 0, 0);

			float base_h = 1024; //is_pre_squished ? 1024.0f : (1024.0f * 0.75f);

			if (config.artcrop) {
				float x1 = (float)bezelx;
				float y1 = (float)bezely;
				float x2 = 1024.0f * bezelzoom + bezelx;
				float y2 = base_h * bezelzoom + bezely;
				drawTexturedQuad(x1, x2, y1, y2, false, rT, gT, bT, aT, alphaTest);
			}
			else {
				drawTexturedQuad(0.0f, 1024.0f, 0.0f, base_h, false, rT, gT, bT, aT, alphaTest);
			}
		};

	if (config.artwork && art_loaded[0]) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		DrawCabinetScaledLayer(art_tex[0], false, 0.5f, 0.5f, 0.5f, 1.0f);
	}

	glDisable(GL_DITHER);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	set_texture(&img4b, 1, 0, 0, 0);

	// Base draw of the CRT image
	FS_Rect(0, 1024);

	// --- TWEAK: CRT Brightness Boost over Artwork ---
	// Because drawing over a backdrop can visually wash out the soft vector glow,
	// we do a secondary additive pass to punch up the midtones of the game image.
	// Todo: if (config.vectrail == 0) adjust more
	if ((config.artwork && art_loaded[0]) || (config.overlay && art_loaded[1] && uses_overlay2))
	{
		// TWEAK THIS: 0.0f = no boost, 1.0f = double brightness.
		// Around 0.4f - 0.6f usually gives vectors enough punch against dark artwork.
		// TODO: Make this configurable per game, this sucks with certain artwork.
		float crt_boost = (config.artwork && art_loaded[0]) ? 0.2f : 0.25f;
		FS_Rect(0, 1024, crt_boost, crt_boost, crt_boost, 1.0f);
	}

	// VECTOR_USES_OVERLAY2 - visible overlay art on top of the CRT only.
	if (config.overlay && art_loaded[1] && uses_overlay2)
	{
		set_texture(&art_tex[1], 1, 0, 0, 0);

		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);

		drawTexturedQuad((float)left, (float)right, (float)top, (float)bottom, false, 1.0f, 1.0f, 1.0f, 0.5f);
	}

	//--------------------------------------------------------------------------
	// LAYER 6: Bezel frame overlay
	//--------------------------------------------------------------------------
	if (config.bezel && art_loaded[3])
	{
		glDisable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Hard alpha cutoff via shader discard (replaces fixed-function GL_ALPHA_TEST).
		DrawCabinetScaledLayer(art_tex[3], false, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f);

		glDisable(GL_DEPTH_TEST);
	}

	render_ui_overlays(1024, 768);

	end_render_fbo4();

	if (config.debug_profile_code)
	{
		auto end = std::chrono::steady_clock::now();
		auto diff = end - start;
		LOG_INFO("Profiler: final_render took %.3f ms",
			std::chrono::duration<double, std::milli>(diff).count());
	}
}

////////////////////////////////////////////////////////////////////////////////
// END RENDERING PIPELINE                                                      //
////////////////////////////////////////////////////////////////////////////////

void render_scanlines()
{
	int scan_x = 0;
	int scan_y = 0;
	get_texture_size(g_scanrezTex, &scan_x, &scan_y);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo_raster);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);

	glEnable(GL_BLEND);

	// Option A: Standard Multiply
	glBlendFunc(GL_DST_COLOR, GL_ZERO);

	/*
	// Option B: The original 2x multiply hack
	glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
	*/

	const rectangle& va = Machine->drv->visible_area;
	int vw = (va.max_x - va.min_x + 1);
	int vh = (va.max_y - va.min_y + 1);

	// Match the FBO allocation shape for rotated games.
	if (Machine->drv->rotation & ORIENTATION_SWAP_XY)
	{
		int t = vw;
		vw = vh;
		vh = t;
	}

	const int rw = static_cast<int>((float)vw * config.prescale);
	const int rh = static_cast<int>((float)vh * config.prescale);

	glViewport(0, 0, rw, rh);

	// Build an ortho projection
	aae::math::mat4 proj = aae::math::ortho(0.0f, (float)rw, 0.0f, (float)rh);

	// Bind shader and set uniforms
	glUseProgram(fragScanlineMultiply);
	set_uniform1i(fragScanlineMultiply, "u_scanTex", 0);
	set_uniform_mat4f(fragScanlineMultiply, "u_projection", aae::math::value_ptr(proj));

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_scanrezTex);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	// Tile UVs: identical to the old (rw/scan_x, rh/scan_y) math
	const float u = (float)rw / (float)scan_x;
	const float v = (float)rh / (float)scan_y;

	// Pixel-space quad, projected by the ortho matrix
	ScanQuadVert verts[4] = {
		{ 0.0f,      0.0f,      0.0f, 0.0f },   // bottom-left
		{ (float)rw, 0.0f,      u,    0.0f },   // bottom-right
		{ (float)rw, (float)rh, u,    v    },   // top-right
		{ 0.0f,      (float)rh, 0.0f, v    }    // top-left
	};

	glBindVertexArray(g_scanVAO);
	glBindBuffer(GL_ARRAY_BUFFER, g_scanVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glUseProgram(0);

	// Cleanup (restore normal blend for the rest of the engine)
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// ---------------------------------------------------------------------------
// Mono monitor CRT effect (B/W raster games only)
//
// Runs img5a through the mono CRT shader (Gaussian beam spot, beam
// overdrive, mip-pyramid halation, optional beam ripple, black-level lift,
// phosphor tint) into img5b. Layout_Render then composites img5b instead
// of img5a, so overlays, bezels, and rotation all work unchanged.
//
// Ported from the PET emulator's mono monitor pass (pet_gl.cpp). The
// halation trick needs no extra blur FBOs: it samples the source texture's
// mip pyramid via textureLod, so the only per-frame cost beyond the quad is
// one glGenerateMipmap on img5a.
// ---------------------------------------------------------------------------

// Monitor phosphor tint presets, indexed by config.mono_tint.
static const float k_monoTints[3][3] = {
	{ 1.00f, 1.00f, 1.00f },   // 0: P4 white
	{ 0.30f, 1.00f, 0.40f },   // 1: P1 green
	{ 1.00f, 0.75f, 0.20f },   // 2: P3 amber
};

bool mono_monitor_active(int vattr)
{
	return config.mono_enable != 0 &&
		fragMonoMonitor != 0 &&
		fbo_mono != 0 &&
		(vattr & VIDEO_TYPE_RASTER_BW) != 0;
}

static void render_mono_monitor()
{
	// Oriented native visible-area size (game pixels) and the prescaled
	// render size. Must match the fbo_init_raster() / set_render() math.
	const rectangle& va = Machine->drv->visible_area;
	int vw = (va.max_x - va.min_x + 1);
	int vh = (va.max_y - va.min_y + 1);

	if (Machine->drv->rotation & ORIENTATION_SWAP_XY)
	{
		const int t = vw;
		vw = vh;
		vh = t;
	}

	// Output size: track the on-screen game rectangle (MAME-HLSL-style
	// output-sized post) so the scanline ripple lands 1:1 on screen pixels.
	// Falls back to the 4x-native size from fbo_init_raster() until the
	// first layout frame has reported a size.
	{
		int sw = 0, sh = 0;
		Layout_GetScreenPixelSize(&sw, &sh);
		if (sw > 0 && sh > 0)
			fbo_resize_mono(sw, sh);
	}
	const int rw = static_cast<int>(mono_fbo_w);
	const int rh = static_cast<int>(mono_fbo_h);

	// Halation samples the source's mip pyramid via textureLod, so rebuild
	// it now that the frame (and any scanline pass) is complete. Unbind the
	// FBO first: generating mips of a still-attached render target stalls
	// some drivers. img5a's MIN filter is already GL_LINEAR_MIPMAP_LINEAR
	// from create_texture().
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	fbo_generate_mipmaps({ img5a });

	glBindFramebuffer(GL_FRAMEBUFFER, fbo_mono);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glViewport(0, 0, rw, rh);
	glDisable(GL_BLEND);   // straight replace into img5b

	aae::math::mat4 proj = aae::math::ortho(0.0f, (float)rw, 0.0f, (float)rh);

	const int   tintIdx = (config.mono_tint >= 0 && config.mono_tint <= 2) ? config.mono_tint : 0;
	const float prescale = (config.prescale > 1.0f) ? config.prescale : 1.0f;

	// GL state gets swapped aggressively elsewhere in the renderer, so set
	// every uniform each frame rather than caching state (they're cheap).
	bind_shader(fragMonoMonitor);
	set_uniform1i(fragMonoMonitor, "uTex", 0);
	set_uniform_mat4f(fragMonoMonitor, "uProj", aae::math::value_ptr(proj));
	set_uniform2f(fragMonoMonitor, "uSrcSize", (float)vw, (float)vh);
	set_uniform1f(fragMonoMonitor, "uLodBias", log2f(prescale));
	set_uniform1f(fragMonoMonitor, "uBlurH", config.mono_blur_h);
	set_uniform1f(fragMonoMonitor, "uBlurV", config.mono_blur_v);
	set_uniform1f(fragMonoMonitor, "uHalation", config.mono_halation);
	set_uniform1f(fragMonoMonitor, "uHalRadius", config.mono_halation_radius);
	set_uniform1f(fragMonoMonitor, "uScanline", config.mono_scanline);
	set_uniform1f(fragMonoMonitor, "uContrast", config.mono_contrast);
	set_uniform1f(fragMonoMonitor, "uBright", config.mono_brightness);
	set_uniform3f(fragMonoMonitor, "uTint",
		k_monoTints[tintIdx][0], k_monoTints[tintIdx][1], k_monoTints[tintIdx][2]);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, img5a);

	// Full-target quad, UV 0..1. img5a and img5b share the same Y-down
	// orientation, so no flip is needed here or downstream.
	ScanQuadVert verts[4] = {
		{ 0.0f,      0.0f,      0.0f, 0.0f },
		{ (float)rw, 0.0f,      1.0f, 0.0f },
		{ (float)rw, (float)rh, 1.0f, 1.0f },
		{ 0.0f,      (float)rh, 0.0f, 1.0f }
	};

	glBindVertexArray(g_scanVAO);
	glBindBuffer(GL_ARRAY_BUFFER, g_scanVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	unbind_shader();
	glBindTexture(GL_TEXTURE_2D, 0);

	// img5b is larger than the window's game area, so Layout_Render MINIFIES
	// it (img5b has a trilinear mip filter from create_texture). Rebuild its
	// mip chain now or the composite samples stale placeholder mips.
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	fbo_generate_mipmaps({ img5b });

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	check_gl_error_named("render_mono_monitor");
}

// ---------------------------------------------------------------------------
// Color CRT monitor pass. Sibling of render_mono_monitor(): same img5a ->
// img5b chain (a game is either B/W or color raster, never both, so the
// mono FBO is free to reuse), same Gaussian-beam/halation pipeline, plus
// RGB misconvergence, saturation and shadow-mask emulation.
// ---------------------------------------------------------------------------
bool color_monitor_active(int vattr)
{
	// A selected texture-overlay raster effect (scanlines.png etc.) is the
	// ALTERNATE to this shader: when one is chosen, the shader pass stands
	// down. Twin of raster_effect_selected() in menu.cpp, which greys out
	// the COLOR MONITOR SETUP menu entry for the same reason.
	const bool overlay_selected = config.raster_effect && config.raster_effect[0] &&
		aae_stricmp(config.raster_effect, "NONE") != 0;

	return !overlay_selected &&
		config.color_enable != 0 &&
		fragColorMonitor != 0 &&
		fbo_mono != 0 &&
		(vattr & VIDEO_TYPE_RASTER_COLOR) != 0;
}

static void render_color_monitor()
{
	const rectangle& va = Machine->drv->visible_area;
	int vw = (va.max_x - va.min_x + 1);
	int vh = (va.max_y - va.min_y + 1);

	if (Machine->drv->rotation & ORIENTATION_SWAP_XY)
	{
		const int t = vw;
		vw = vh;
		vh = t;
	}

	// Track the on-screen game rectangle so the shadow mask and scanlines
	// land 1:1 on screen pixels (MAME-HLSL-style output-sized post).
	{
		int sw = 0, sh = 0;
		Layout_GetScreenPixelSize(&sw, &sh);
		if (sw > 0 && sh > 0)
			fbo_resize_mono(sw, sh);
	}
	const int rw = static_cast<int>(mono_fbo_w);
	const int rh = static_cast<int>(mono_fbo_h);

	// Halation samples the source's mip pyramid; rebuild it with the frame
	// complete (unbound first -- see render_mono_monitor).
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	fbo_generate_mipmaps({ img5a });

	glBindFramebuffer(GL_FRAMEBUFFER, fbo_mono);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	glViewport(0, 0, rw, rh);
	glDisable(GL_BLEND);   // straight replace into img5b

	aae::math::mat4 proj = aae::math::ortho(0.0f, (float)rw, 0.0f, (float)rh);

	const float prescale = (config.prescale > 1.0f) ? config.prescale : 1.0f;

	bind_shader(fragColorMonitor);
	set_uniform1i(fragColorMonitor, "uTex", 0);
	set_uniform_mat4f(fragColorMonitor, "uProj", aae::math::value_ptr(proj));
	set_uniform2f(fragColorMonitor, "uSrcSize", (float)vw, (float)vh);
	set_uniform1f(fragColorMonitor, "uLodBias", log2f(prescale));
	set_uniform1f(fragColorMonitor, "uBlurH", config.color_blur_h);
	set_uniform1f(fragColorMonitor, "uBlurV", config.color_blur_v);
	set_uniform1f(fragColorMonitor, "uConverge", config.color_converge);
	set_uniform1f(fragColorMonitor, "uHalation", config.color_halation);
	set_uniform1f(fragColorMonitor, "uHalRadius", config.color_halation_radius);
	set_uniform1f(fragColorMonitor, "uScanline", config.color_scanline);
	set_uniform1f(fragColorMonitor, "uContrast", config.color_contrast);
	set_uniform1f(fragColorMonitor, "uBright", config.color_brightness);
	set_uniform1f(fragColorMonitor, "uSaturation", config.color_saturation);
	set_uniform1i(fragColorMonitor, "uMaskType", config.color_mask_type);
	set_uniform1f(fragColorMonitor, "uMaskStrength", config.color_mask_strength);
	set_uniform1f(fragColorMonitor, "uMaskScale", config.color_mask_scale);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, img5a);

	ScanQuadVert verts[4] = {
		{ 0.0f,      0.0f,      0.0f, 0.0f },
		{ (float)rw, 0.0f,      1.0f, 0.0f },
		{ (float)rw, (float)rh, 1.0f, 1.0f },
		{ 0.0f,      (float)rh, 0.0f, 1.0f }
	};

	glBindVertexArray(g_scanVAO);
	glBindBuffer(GL_ARRAY_BUFFER, g_scanVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	unbind_shader();
	glBindTexture(GL_TEXTURE_2D, 0);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	fbo_generate_mipmaps({ img5b });

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	check_gl_error_named("render_color_monitor");
}

void final_render_raster()
{
	auto start = std::chrono::steady_clock::now();

	const int vattr = (Machine && Machine->drv) ? Machine->drv->video_attributes : 0;

	auto& ws = GetWindowSetup();

	// -----------------------------------------------------------------------
	// PHASE A: Scanlines over the game image. With multiple hacks JUST FOR WARLORDS
	// -----------------------------------------------------------------------

	if (Machine && Machine->drv && g_scanrezTex &&
		(g_scanline_override == 1 ||
			(g_scanline_override == 0 && !(Machine->drv->video_attributes & VIDEO_TYPE_RASTER_BW))))
	{
		render_scanlines();
	}

	// -----------------------------------------------------------------------
	// PHASE B: Mono monitor CRT effect (B/W raster games only).
	// Processes img5a -> img5b with the mono shader; Layout_Render below then
	// composites the processed texture. Color raster games get the color
	// CRT pass (shadow mask) through the same img5b chain when enabled.
	// -----------------------------------------------------------------------
	GLuint screenTex = img5a;

	if (Machine && Machine->drv && mono_monitor_active(vattr))
	{
		render_mono_monitor();
		screenTex = img5b;
	}
	else if (Machine && Machine->drv && color_monitor_active(vattr))
	{
		render_color_monitor();
		screenTex = img5b;
	}

	// 1. DISENGAGE FBO: Essential to "close" img5a so the GPU can read it.
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDrawBuffer(GL_BACK);

	// 2. VIEWPORT RESCUE: Handle the case where rendering reset might have failed
	int vW = (ws.clientWidth > 0) ? ws.clientWidth : 1024;
	int vH = (ws.clientHeight > 0) ? ws.clientHeight : 768;
	glViewport(0, 0, vW, vH);

	// 4. PROJECTION: Apply ortho Y-DOWN
	set_ortho_raster(vW, vH);

	// -----------------------------------------------------------------------
	// PHASE 1: MAME .lay layout compositing (if active).
	//
	// Layout_Render handles the complete compositing pass in layer order:
	//   backdrop -> screen (with overlay color gel) -> bezel
	//
	// The screen layer uses additive blending (GL_ONE, GL_ONE) so game
	// pixels add light on top of the backdrop like a real CRT -- black
	// pixels add nothing (transparent).
	//
	// The overlay color gel is applied via a dual-texture shader that
	// multiplies screen * overlay in the fragment shader. The multiply
	// mode (pure vs 2x) is selected by videoAttributes to match the
	// blend modes previously used in the FBO-side overlay compositing:
	//   BW games:    pure multiply   (screen * overlay)
	//   Color games: 2x multiply     (screen * overlay * 2, clamped)
	// -----------------------------------------------------------------------
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDrawBuffer(GL_BACK);
	glViewport(0, 0, ws.clientWidth, ws.clientHeight);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	Layout_Render(*g_activeView, screenTex, ws.clientWidth, ws.clientHeight, vattr);

	// State reset for UI overlay draws
	glUseProgram(0);
	if (glBindVertexArray) glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_BLEND);

	// UI overlays. For a system-rotated raster game the menu must rotate to match
	// the game (Layout_Render already rotated the game image). The vector path gets
	// this for free -- its overlay lives in fbo4 and screen_rect rotates the blit --
	// so when rotated we route the raster overlay through the SAME fbo4 + screen_rect
	// blit. Non-rotated raster keeps the crisp, full-resolution direct draw.
	if (orientation_to_rect2_rotation(config.system_rotation) != 0)
	{
		set_render_fbo4();                       // bind fbo4 (1024x1024), clear transparent
		render_ui_overlays(1024, 768, true);     // draw overlay in fbo4 space (like vector)

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDrawBuffer(GL_BACK);
		set_ortho(ws.clientWidth, ws.clientHeight);
		glEnable(GL_BLEND);
		// Premultiplied-over: the overlay was composited into fbo4 with premultiplied
		// alpha (black dim => rgb already premultiplied), so the dim and AA text edges
		// composite over the game without being darkened.
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		set_texture(&img4a, 1, 0, 0, 0);
		screen_rect->Render(aae::math::value_ptr(g_proj));   // rotated + letterboxed over the game
	}
	else
	{
		render_ui_overlays(ws.clientWidth, ws.clientHeight);
	}

	glDisable(GL_BLEND);

	check_gl_error_named("final_render_raster (exit)");

	if (config.debug_profile_code)
	{
		auto end = std::chrono::steady_clock::now();
		auto diff = end - start;
		LOG_INFO("Profiler: final_render_raster took %.3f ms",
			std::chrono::duration<double, std::milli>(diff).count());
	}
}

// ---------------------------------------------------------------------------
// GUI point sprites (starfield). GL lives here so gui code stays GL-free.
// ---------------------------------------------------------------------------
static GLuint s_guiPointVAO = 0;
static GLuint s_guiPointVBO = 0;

void glchain_gui_points_init(int maxPoints)
{
	glGenVertexArrays(1, &s_guiPointVAO);
	glGenBuffers(1, &s_guiPointVBO);

	glBindVertexArray(s_guiPointVAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_guiPointVBO);
	glBufferData(GL_ARRAY_BUFFER, maxPoints * sizeof(GuiPointVertex), nullptr, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GuiPointVertex), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GuiPointVertex), (void*)(2 * sizeof(float)));

	glBindVertexArray(0);
}

void glchain_gui_points_draw(const GuiPointVertex* pts, int count, float pointSize)
{
	if (count <= 0 || !s_guiPointVAO) return;

	glPointSize(pointSize);
	bind_shader(fragStarPoint);
	set_uniform_mat4f(fragStarPoint, "uProj", aae::math::value_ptr(g_proj));

	glBindVertexArray(s_guiPointVAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_guiPointVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(GuiPointVertex), pts);
	glDrawArrays(GL_POINTS, 0, count);
	glBindVertexArray(0);

	unbind_shader();
	glPointSize(config.pointsize);
}

void glchain_gui_points_shutdown()
{
	if (s_guiPointVAO) { glDeleteVertexArrays(1, &s_guiPointVAO); s_guiPointVAO = 0; }
	if (s_guiPointVBO) { glDeleteBuffers(1, &s_guiPointVBO); s_guiPointVBO = 0; }
}

// ---------------------------------------------------------------------------
// glchain_present_blank_frame - clear the window to black and present it.
//
// run_game loads ROMs, artwork and samples with nothing repainting, so the
// previously presented frame would otherwise sit frozen on screen for the whole
// load. One black frame up front makes that read as a deliberate transition.
//
// The clear is EXPLICIT and targets the default framebuffer: swapping without
// it would present whatever stale content the back buffer happens to hold -
// an older frame - which reads as a flicker backwards rather than a blank.
// ---------------------------------------------------------------------------
void glchain_present_blank_frame()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDisable(GL_SCISSOR_TEST);       // a leftover scissor would clip the clear
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glchain_swap_buffers();
}

// ---------------------------------------------------------------------------
// glcode_get_gl_error
// Backend-neutral wrapper around glGetError() for callers outside the
// render .cpp files that just want to know if a GL error occurred, without
// pulling in GL headers themselves.
// ---------------------------------------------------------------------------
int glchain_get_gl_error()
{
	return static_cast<int>(glGetError());
}