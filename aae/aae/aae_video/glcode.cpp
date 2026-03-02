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
// glcode.cpp
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

#include "glcode.h"
#include "aae_mame_driver.h"
#include "vector_fonts.h"
#include "texture_handler.h"
#include "gl_fbo.h"
#include "gl_texturing.h"
#include "gl_shader.h"
#include "emu_vector_draw.h"
#include "fast_poly.h"
#include "os_basic.h"
#include "MathUtils.h"
#include "tiled_effect.h"
#include "menu.h"
#include "aae_emulator.h"   // get_exit_confirm_status / get_exit_confirm_selection
#include <chrono>   // for optional frame-time profiling
#include <cstring>  // strcmp for raster_effect name check

// ---------------------------------------------------------------------------
// Module-level globals
// ---------------------------------------------------------------------------

// Calculated screen rectangle used to blit FBO4 to the window at the correct
// size and aspect ratio. Allocated in init_gl(), freed on shutdown.
Rect2* screen_rect = nullptr;

// Raster polygon renderer. One instance per application lifetime.
Fpoly* sc;

// Scale factor applied when mapping raster pixels to polygon positions.
extern float vid_scale;

// Scanlines / raster-effect overlay texture handle.
// Loaded per-game by init_raster_overlay(); 0 means disabled or not loaded.
static GLuint g_scanrezTex = 0;

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
// emulator_on_window_resize
// Called by the OS message handler whenever the client area changes size.
// Updates screen_rect so the final blit tracks the new window dimensions.
// ---------------------------------------------------------------------------
void emulator_on_window_resize(int newW, int newH)
{
	if (!screen_rect) return;

	auto& ws = GetWindowSetup();
	screen_rect->UpdateScreenRect(ws.clientWidth, ws.clientHeight, ws.aspectRatio, 0);
	LOG_INFO("Window resized - new client area: %d x %d", ws.clientWidth, ws.clientHeight);
}

// ---------------------------------------------------------------------------
// raster_poly_update
// Reads the MAME bitmap (main_bitmap) for the current frame, converts each
// non-zero pixel to an RGBA color via osd_get_pen(), and submits it to the
// Fpoly renderer (sc) as a small rectangle scaled by vid_scale.
//
// Handles all four MAME orientation flags so rotated/flipped games display
// correctly without needing separate draw paths.
// ---------------------------------------------------------------------------
void raster_poly_update(void)
{
	unsigned char r1, g1, b1;
	// TBD: Make this configurable with a scaled fbo size.
	vid_scale = 3.0f;

	const rectangle& va = Machine->drv->visible_area;
	const int minX = va.min_x, maxX = va.max_x;
	const int minY = va.min_y, maxY = va.max_y;

	const int W = (maxX - minX + 1);
	const int H = (maxY - minY + 1);

	const int rot = Machine->drv->rotation;

	for (int srcY = minY; srcY <= maxY; ++srcY)
	{
		unsigned char* srcRow = main_bitmap->line[srcY];

		for (int srcX = minX; srcX <= maxX; ++srcX)
		{
			const unsigned char c = srcRow[srcX];
			if (!c) continue;   // skip transparent/black pixels

			osd_get_pen(Machine->pens[c], &r1, &g1, &b1);

			// Convert from bitmap coords to local (0-based) source coords.
			int x = srcX - minX;
			int y = srcY - minY;

			// Apply MAME orientation flags in the correct order.
			if (rot & ORIENTATION_SWAP_XY) {
				int tmp = x; x = y; y = tmp;
			}
			if (rot & ORIENTATION_FLIP_X) {
				x = (W - 1) - x;
			}
			if (rot & ORIENTATION_FLIP_Y) {
				y = (H - 1) - y;
			}

			// Submit polygon at destination position, re-adding minX/minY so
			// the image lands in the correct place in the 1024x1024 ortho space.
			sc->addPoly((float)(minX + x), (float)(minY + y), vid_scale,
				MAKE_RGBA(r1, g1, b1, 0xff));
		}
	}
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

	wideadj = (float)(1.3333 / val);
}

// ---------------------------------------------------------------------------
// set_ortho
// Convenience wrapper: sets viewport and a top-left-origin 2D ortho
// projection to the given dimensions. Used throughout the pipeline to
// switch between 1024x1024 (FBO space) and window-size (backbuffer) spaces.
// ---------------------------------------------------------------------------
void set_ortho(GLint width, GLint height)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glViewport(0, 0, width, height);
	glOrtho(0, width, 0, height, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
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
int init_gl(void)
{
	static int init_one = 0;

	if (!init_one)
	{
		// --- VSync control ---
		if (wglewIsSupported("WGL_EXT_swap_control"))
		{
			if (config.forcesync)
			{
				wglSwapIntervalEXT(1);
				LOG_INFO("VSync enabled (config.forcesync).");
			}
			else
			{
				wglSwapIntervalEXT(0);
				LOG_INFO("VSync disabled.");
			}
		}
		else
		{
			LOG_INFO("WGL_EXT_swap_control not supported - VSync state unknown.");
		}

		// --- Base GL state ---
		set_ortho(1024, 768);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_LINE_SMOOTH);
		glEnable(GL_POINT_SMOOTH);
		glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

		glLineWidth(config.linewidth);
		glPointSize(config.pointsize);

		// --- Screen rectangle (tracks window size and aspect ratio) ---
		auto& ws = GetWindowSetup();
		screen_rect = new Rect2(ws.clientWidth, ws.clientHeight, ws.aspectRatio, 0);

		// NOTE: Scanlines texture loading is NOT done here. It is deferred to
		// init_raster_overlay(), which is called per-game from run_game() after
		// setup_game_config() has set the correct config.raster_effect value.

		// --- FBO allocation ---
		LOG_INFO("Initializing FBOs...");
		fbo_init();

		// --- Shader compilation ---
		init_shader();

		// --- Vector font renderer ---
		LOG_INFO("Building vector font...");
		VF.Initialize(1024, 768);

		// --- Tiled scanlines effect ---
		TiledEffect_Init();

		// --- Raster polygon renderer ---
		sc = new Fpoly();

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		LOG_INFO("OpenGL initialization complete.");

		init_one++;
	}

	return 1;
}

// ---------------------------------------------------------------------------
// end_gl
// Shutdown: release subsystems that require explicit cleanup.
// Call this once when the application exits.
// ---------------------------------------------------------------------------
void end_gl()
{
	TiledEffect_Shutdown();
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
void init_raster_overlay()
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
void shutdown_raster_overlay()
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
void glcode_vector_hard_clear_fbo1()
{
	if (!fbo1)
		return;

	GLint prevFbo = 0;
	GLint prevVP[4] = { 0, 0, 0, 0 };
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &prevFbo);
	glGetIntegerv(GL_VIEWPORT, prevVP);

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
	glViewport(0, 0, 1024, 1024);

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glClearColor(0, 0, 0, 0);

	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
	glClear(GL_COLOR_BUFFER_BIT);

	// Restore previous FBO and viewport.
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, (GLuint)prevFbo);
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
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo4);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

	set_ortho(1024, 1024);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glEnable(GL_BLEND);
	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_LINE_SMOOTH);
	glEnable(GL_POINT_SMOOTH);
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

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);
	glActiveTexture(GL_TEXTURE0);

	auto& ws = GetWindowSetup();
	set_ortho(ws.clientWidth, ws.clientHeight);

	glDisable(GL_BLEND);

	// Blit img4a to the screen. Blending disabled: this is a straight copy.
	// screen_rect->Render() handles letterboxing / pillarboxing for the
	// configured aspect ratio (1.33f = 4:3).
	set_texture(&img4a, 1, 0, 0, 0);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	screen_rect->Render(1.33f);

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
	glLoadIdentity();
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo2);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
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
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo3);

	// Clear both pingpong buffers before each frame.
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
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

	// Each row is: [x0, y0, x1, y1] for one half of a pingpong pass.
	// 8 rows * 4 values = 32 floats. We step by 4 per pass (4 passes total).
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

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive blend accumulates glow
	glColor4f(0.1f, 0.1f, 0.1f, 0.1f);

	// Lambda to draw one offset quad. Converts float offsets to screen-space
	// by adding globalOffset and sizing to height3 (the FBO3 height, 256).
	auto DrawQuadOffset = [&](float ox, float oy) {
		float x1 = ox + globalOffsetX;
		float y1 = oy + globalOffsetY;
		float x2 = (float)height3 + x1;
		// y2 maps size+y1 down to y1, matching the orientation of FS_Rect(0,size).
		drawTexturedQuad(x1, (float)height3 + y1, x2, y1, 1);
		};

	int i = 0;

	for (int pass = 0; pass < 4; ++pass)
	{
		// A -> B: draw img3a into attachment 1 (img3b) with near offset.
		glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
		set_texture(&img3a, 1, 0, 0, 0);
		DrawQuadOffset(fshifta[i], fshifta[i + 1]);

		// B -> A: draw img3b into attachment 0 (img3a) with far offset.
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		set_texture(&img3b, 1, 0, 0, 0);
		DrawQuadOffset(fshiftb[i], fshiftb[i + 1]);

		i += 4;
	}

	check_gl_error_named("render_blur_image_fbo3");
	unbind_shader();
}

////////////////////////////////////////////////////////////////////////////////
// RENDERING PIPELINE - STEPS 1, 2, and 3                                    //
////////////////////////////////////////////////////////////////////////////////

// ---------------------------------------------------------------------------
// set_render[STEP 1]
// Binds FBO1/attachment0 (img1a) and prepares it as the render target for
// the current frame. The game-specific draw code writes into this texture.
// ---------------------------------------------------------------------------
void set_render()
{
	// Bind FBO1 and direct output to attachment 0 (img1a).
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

	// Set 1024x1024 ortho to match the FBO dimensions.
	set_ortho(1024, 1024);

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
// render[STEP 2]
// Main per-frame render dispatch. Handles the paused state, then routes to
// the vector or raster draw path before calling final_render() to composite
// everything and push the result to the screen.
// ---------------------------------------------------------------------------
void render()
{
	// Only process new game geometry if we are not paused.
	// (If paused, FBO1 retains the image from the last active frame).
	if (!paused)
	{
		if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
		{
			draw_all();
		}
		else
		{
			raster_poly_update();
			sc->Render();
		}
	}

	// ALWAYS composite the layers. This applies game_rect boundaries and
	// shaders to the frozen frame exactly as it did when running.
	final_render(game_rect_left, game_rect_right, game_rect_bottom, game_rect_top);
}
// ---------------------------------------------------------------------------
// final_render  [STEP 3]
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
		bottom = 1088;  // or whatever value corrects the stretch for your 1024x1024->768 pipeline
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
	glEnable(GL_TEXTURE_2D);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
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
		glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
		glDisable(GL_DITHER);
		set_texture(&img1b, 1, 0, 0, 0);
		glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);

		switch (config.vectrail)
		{
		case 1:  glColor4f(1.0f, 1.0f, 1.0f, 0.825f); break;
		case 2:  glColor4f(1.0f, 1.0f, 1.0f, 0.86f);  break;
		case 3:  glColor4f(1.0f, 1.0f, 1.0f, 0.93f);  break;
		default: glColor4f(0.95f, 0.95f, 0.95f, 1.0f); break;
		}

		FS_Rect(0, 1024);
		fbo_generate_mipmaps({ img1b });
	}

	//--------------------------------------------------------------------------
	// LAYER 4: Glow blur passes (FBO2 and FBO3).
	//--------------------------------------------------------------------------
	if (config.vecglow && !emulator_is_gui_active()) // No Vecglow for the GUI
	{
		copy_main_img_to_fbo2();
		copy_fbo2_to_fbo3();
		render_blur_image_fbo3();
		fbo_generate_mipmaps({ img2a, img3a, img3b });
	}

	//--------------------------------------------------------------------------
	// LAYER 5A: Build the CRT/game image into img4b (FBO4 attachment 1).
	//--------------------------------------------------------------------------
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo4);
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	set_ortho(1024, 1024);

	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glDisable(GL_DITHER);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	fbo_generate_mipmaps({ img1a, img1b, img1c });
	if (config.vecglow && !emulator_is_gui_active()) useglow = 1; // I said, no glow for the GUI!

	bind_shader(fragMulti);

	bleh = glGetUniformLocation(fragMulti, "mytex1"); glUniform1i(bleh, 0);
	bleh = glGetUniformLocation(fragMulti, "mytex2"); glUniform1i(bleh, 1);
	bleh = glGetUniformLocation(fragMulti, "mytex3"); glUniform1i(bleh, 2);
	bleh = glGetUniformLocation(fragMulti, "mytex4"); glUniform1i(bleh, 3);

	bleh = glGetUniformLocation(fragMulti, "useart");
	glUniform1i(bleh, 0);

	set_uniform1i(fragMulti, "usefb", config.vectrail);
	set_uniform1i(fragMulti, "useglow", useglow);
	set_uniform1f(fragMulti, "glowamt", (float)(config.vecglow * 0.01));
	set_uniform1i(fragMulti, "brighten", gamenum);

	glActiveTexture(GL_TEXTURE1); set_texture(&img1b, 1, 1, 0, 0);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, img3a); set_texture(&img3b, 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE3); set_texture(&img1c, 1, 0, 0, 0);

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	//LOG_DEBUG("img4a into render: left=%d right=%d top=%d bottom=%d", left, right, top, bottom);
	drawTexturedQuad((float)left, (float)right, (float)bottom, (float)top, true);

	unbind_shader();

	glActiveTextureARB(GL_TEXTURE1_ARB); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE2_ARB); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE3_ARB); glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE0_ARB);

	//--------------------------------------------------------------------------
	// LAYER 5B: VECTOR_USES_OVERLAY1 - colorize the CRT-only image in-place.
	//--------------------------------------------------------------------------
	if (config.overlay && art_loaded[1] && uses_overlay1)
	{
		float overlay_height = (Machine->drv->rotation & ORIENTATION_SWAP_XY) ? (float)bottom : ((float)bottom * 0.75f);

		glEnable(GL_TEXTURE_2D);
		set_texture(&art_tex[1], 1, 0, 0, 0);

		glEnable(GL_BLEND);
		if (Machine->drv->video_attributes & VIDEO_TYPE_RASTER_BW)
			glBlendFunc(GL_DST_COLOR, GL_ZERO);
		else
			glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);

		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

		if (Machine->drv->video_attributes & VEC_OVER_HACK) // This is just for Solar Quest TBD REMOVAL
		{
			if (config.bezel && config.artcrop == 0)
			{
				drawTexturedQuad((float)left, (float)right, (float)top, bottom * .80f, false);
			}
			else if (config.artcrop)
			{
				drawTexturedQuad((float)left, (float)right, (float)top, bottom * .79f, false);
			}
			else
				drawTexturedQuad((float)left, (float)right, (float)top, bottom * .75f, false);
		}
		else
			drawTexturedQuad((float)left, (float)right, (float)top, overlay_height, false);
	}

	//--------------------------------------------------------------------------
	// LAYER 5C: Composite to img4a (FBO4 attachment 0)
	//--------------------------------------------------------------------------
	set_render_fbo4();

	auto DrawCabinetScaledLayer = [&](GLuint tex, bool is_pre_squished) {
		if (!tex) return;
		glEnable(GL_TEXTURE_2D);
		set_texture(&tex, 1, 0, 0, 0);

		float base_h = is_pre_squished ? 1024.0f : (1024.0f * 0.75f);

		if (config.artcrop) {
			float x1 = (float)bezelx;
			float y1 = (float)bezely;
			float x2 = 1024.0f * bezelzoom + bezelx;
			float y2 = base_h * bezelzoom + bezely;
			drawTexturedQuad(x1, x2, y1, y2, false);
		}
		else {
			drawTexturedQuad(0.0f, 1024.0f, 0.0f, base_h, false);
		}
		};

	if (config.artwork && art_loaded[0]) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
		DrawCabinetScaledLayer(art_tex[0], false);
	}

	glDisable(GL_DITHER);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glEnable(GL_TEXTURE_2D);
	set_texture(&img4b, 1, 0, 0, 0);

	// Base draw of the CRT image
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	FS_Rect(0, 1024);

	// --- TWEAK: CRT Brightness Boost over Artwork ---
	// Because drawing over a backdrop can visually wash out the soft vector glow,
	// we do a secondary additive pass to punch up the midtones of the game image.
	if ((config.artwork && art_loaded[0]) || (config.overlay && art_loaded[1] && uses_overlay2))
	{
		// TWEAK THIS: 0.0f = no boost, 1.0f = double brightness.
		// Around 0.4f - 0.6f usually gives vectors enough punch against dark artwork.
		// TODO: Make this configurable per game, this sucks with certain artwork.
		float crt_boost = (config.artwork && art_loaded[0]) ? 0.4f : 0.35f;
		glColor4f(crt_boost, crt_boost, crt_boost, 1.0f);
		FS_Rect(0, 1024);
	}

	// VECTOR_USES_OVERLAY2 - visible overlay art on top of the CRT only.
	if (config.overlay && art_loaded[1] && uses_overlay2)
	{
		glEnable(GL_TEXTURE_2D);
		set_texture(&art_tex[1], 1, 0, 0, 0);

		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);
		glColor4f(1.0f, 1.0f, 1.0f, 0.5f);

		if (Machine->drv->video_attributes & VEC_OVER_HACK) // This is just to fix Armor Attack TBD: Fix
		{
			if (config.bezel && config.artcrop == 0)
			{
				drawTexturedQuad((float)left, (float)right, (float)top, bottom * .783f, false);
			}
			else if (config.artcrop && config.bezel)
			{
				drawTexturedQuad((float)left, (float)right, (float)top, bottom * .776f, false);
			}
			else
				drawTexturedQuad((float)left, (float)right, (float)top, bottom * .75f, false);
		}
		else
			drawTexturedQuad((float)left, (float)right, (float)top, (float)bottom * 0.75f, false);
	}

	//--------------------------------------------------------------------------
	// LAYER 6: Scanlines / raster effect overlay.
	//--------------------------------------------------------------------------
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);

	if (Machine->drv->video_attributes & VIDEO_TYPE_RASTER_COLOR)
	{
		if (g_scanrezTex != 0)
		{
			GLint vp[4] = { 0, 0, 0, 0 };
			glGetIntegerv(GL_VIEWPORT, vp);
			TiledEffect_Draw(g_scanrezTex, vp[2], vp[3], 0.5f);
		}
	}

	//--------------------------------------------------------------------------
	// LAYER 7: Bezel frame overlay
	//--------------------------------------------------------------------------
	if (config.bezel && art_loaded[3])
	{
		glEnable(GL_ALPHA_TEST);
		glDisable(GL_BLEND);
		glAlphaFunc(GL_GREATER, 0.2f);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		DrawCabinetScaledLayer(art_tex[3], false);

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_ALPHA_TEST);
	}

	//--------------------------------------------------------------------------
	// LAYER 7.5: Dim background and draw PAUSED text if needed
	//--------------------------------------------------------------------------
	if (paused || get_menu_status())
	{
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// 50% opacity black quad covering the 1024x1024 FBO space
		glColor4f(0.0f, 0.0f, 0.0f, 0.5f);

		glBegin(GL_QUADS);
		glVertex2f(0.0f, 0.0f);
		glVertex2f(1024.0f, 0.0f);
		glVertex2f(1024.0f, 1024.0f);
		glVertex2f(0.0f, 1024.0f);
		glEnd();

		// Only draw the giant "PAUSED" text if the menu is NOT open.
		// (Otherwise it visually crashes into the menu text)
		if (get_menu_status() == 0)
		{
			VF.Begin();
			VF.PrintCentered(30, RGB_WHITE, 5.0f, "PAUSED");
			//LEFT
			VF.Print(50, 50, RGB_WHITE, 4.0f, 90.0f, "PAUSED");
			VF.Print(50, 520, RGB_WHITE, 4.0f, 90.0f, "PAUSED");
			//Right
			VF.Print(975, 260, RGB_WHITE, 4.0f, 270.0f, "PAUSED");
			VF.Print(975, 730, RGB_WHITE, 4.0f, 270.0f, "PAUSED");
			VF.End();
		}
	}

	//--------------------------------------------------------------------------
		// LAYER 7.6: Exit confirmation dialog (YES / NO prompt).
		//
		// Shown when the player presses ESC and config.confirm_exit is enabled.
		// Draws on top of the dim overlay from layer 7.5, using the same
		// 50% black quad as a base so the background is already dimmed.
		// The selected option is drawn in bright yellow; the other in white.
		// YES and NO are printed side by side, centered on screen.
		// Input is handled in msg_loop() in aae_emulator.cpp each frame.
		//--------------------------------------------------------------------------

	if (get_exit_confirm_status())
	{
		// If we are not already inside the paused/menu dim pass (layer 7.5),
		// draw a fresh dim quad so the dialog always has a readable background.
		if (!paused && !get_menu_status())
		{
			glDisable(GL_TEXTURE_2D);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor4f(0.0f, 0.0f, 0.0f, 0.85f);
			glBegin(GL_QUADS);
			glVertex2f(0.0f, 0.0f);
			glVertex2f(1024.0f, 0.0f);
			glVertex2f(1024.0f, 1024.0f);
			glVertex2f(0.0f, 1024.0f);
			glEnd();
		}

		// sel == 0 means YES is highlighted, sel == 1 means NO is highlighted.
		const int sel = get_exit_confirm_selection();

		// Color constants for selected vs unselected options.
		const unsigned int colSelected = RGB_YELLOW;
		const unsigned int colUnselected = RGB_WHITE;
		const unsigned int colTitle = RGB_WHITE;

		// Layout constants - all in 1024x1024 virtual space.
		const int   yTitle = 520;   // "EXIT GAME?" title line
		const int   yOptions = 450;   // YES / NO options row
		const int   yHint = 390;   // hint line below options
		const float scTitle = 4.0f;
		const float scOption = 3.0f;
		const float scHint = 1.6f;

		// Gap in pixels between the YES and NO labels (at scOption scale).
		const float labelGap = 60.0f;

		// The center of the virtual screen.
		const float centerX = 512.0f;

		// Build the label strings up front so we can measure them before drawing.
		// The highlighted label gets angle-bracket arrows so the player can
		// clearly see which option is currently selected.
		const char* strYes = (sel == 0) ? "< YES >" : "YES";
		const char* strNo = (sel == 1) ? "< NO >" : "NO";

		// Measure each label using the font's proportional pitch calculator.
		// GetStringPitch returns the total rendered width of the string in
		// virtual pixels at the given scale (third arg is font set, 0 = default).
		const float pitchYes = VF.GetStringPitch(strYes, scOption, 0);
		const float pitchNo = VF.GetStringPitch(strNo, scOption, 0);

		// Place both labels symmetrically around centerX with a gap between them.
		// YES right edge lands at (centerX - labelGap/2).
		// NO  left  edge starts at (centerX + labelGap/2).
		const float xYes = centerX - (labelGap / 2.0f) - pitchYes;
		const float xNo = centerX + (labelGap / 2.0f);

		VF.Begin();

		// Title line - measured and centered manually for consistency.
		{
			const char* strTitle = "EXIT GAME?";
			const float pitchTitle = VF.GetStringPitch(strTitle, scTitle, 0);
			const float xTitle = centerX - (pitchTitle / 2.0f);
			VF.Print(xTitle, yTitle, colTitle, scTitle, strTitle);
		}

		// YES and NO side by side, each at its computed X, same Y.
		VF.Print(xYes, yOptions, (sel == 0) ? colSelected : colUnselected, scOption, strYes);
		VF.Print(xNo, yOptions, (sel == 1) ? colSelected : colUnselected, scOption, strNo);

		// Hint line - measured and centered manually.
		{
			const char* strHint = "LEFT / RIGHT to choose ENTER OR A to confirm  ESC to cancel";
			const float pitchHint = VF.GetStringPitch(strHint, scHint, 0);
			const float xHint = centerX - (pitchHint / 2.0f);
			VF.Print(xHint, yHint, RGB_CYAN, scHint, strHint);
		}

		VF.End();
	}

	//--------------------------------------------------------------------------
	// LAYER 8: Per-game overlay / score display.
	//--------------------------------------------------------------------------
	video_loop();

	end_render_fbo4();

	glDisable(GL_TEXTURE_2D);

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