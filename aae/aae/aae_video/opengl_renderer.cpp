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
#include "menu.h"
#include "aae_emulator.h"   // get_exit_confirm_status / get_exit_confirm_selection
#include "mame_layout.h"
#include "inifile.h"
#include "mame_vector.h"
#include <chrono>   // for optional frame-time profiling
#include <cstring>  // strcmp for raster_effect name check
#include "aae_avg.h"
// ---------------------------------------------------------------------------
// Module-level globals
// ---------------------------------------------------------------------------
extern int AVG_BUSY;
// Calculated screen rectangle used to blit FBO4 to the window at the correct
// size and aspect ratio. Allocated in init_gl(), freed on shutdown.
Rect2* screen_rect = nullptr;

// Raster polygon renderer. One instance per application lifetime.
Fpoly* sc;

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

// Special Flag Just for Warlords with it's dual Monitor Types
// Near the top with other module-level state
int g_scanline_override = 0;  // 0 = default, 1 = force on, -1 = force off
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
void emulator_on_window_resize(int newW, int newH)
{
	if (!screen_rect) return;

	auto& ws = GetWindowSetup();
	int rot = orientation_to_rect2_rotation(config.system_rotation);
	screen_rect->UpdateScreenRect(ws.clientWidth, ws.clientHeight, ws.aspectRatio, rot);
	LOG_INFO("Window resized - new client area: %d x %d (rotation=%d)", ws.clientWidth, ws.clientHeight, rot);
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

	if (!Machine || !Machine->drv || !main_bitmap || !sc)
		return;

	const rectangle& va = Machine->drv->visible_area;
	const int minX = va.min_x;
	const int maxX = va.max_x;
	const int minY = va.min_y;
	const int maxY = va.max_y;

	const int srcW = (maxX - minX + 1);
	const int srcH = (maxY - minY + 1);

	if (srcW <= 0 || srcH <= 0)
		return;

	const int rot = Machine->drv->rotation;

	// Destination extents after orientation.
	int dstW = srcW;
	int dstH = srcH;

	if (rot & ORIENTATION_SWAP_XY)
	{
		dstW = srcH;
		dstH = srcW;
	}

	for (int srcY = minY; srcY <= maxY; ++srcY)
	{
		unsigned char* srcRow = main_bitmap->line[srcY];
		if (!srcRow)
			continue;

		for (int srcX = minX; srcX <= maxX; ++srcX)
		{
			const unsigned char c = srcRow[srcX];

			// Keep current behavior: draw all pixels, including black.
			// If you later want to skip transparent black, restore:
			// if (!c) continue;

			osd_get_pen(Machine->pens[c], &r1, &g1, &b1);

			// Convert source bitmap coords to local visible-area coords.
			int x = srcX - minX;
			int y = srcY - minY;

			// Apply MAME orientation flags.
			// IMPORTANT: FLIP is performed after SWAP_XY.
			if (rot & ORIENTATION_SWAP_XY)
			{
				const int t = x;
				x = y;
				y = t;
			}

			// Flip against destination extents, not source extents.
			if (rot & ORIENTATION_FLIP_X)
			{
				x = (dstW - 1) - x;
			}

			if (rot & ORIENTATION_FLIP_Y)
			{
				y = (dstH - 1) - y;
			}

			// Submit in visible-area-local coordinates.
			sc->addPoly((float)x, (float)y, config.prescale, MAKE_RGBA(r1, g1, b1, 0xff));
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
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glViewport(0, 0, width, height);
	glOrtho(0, width, 0, height, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
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
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glViewport(0, 0, width, height);
	glOrtho(0, width, height, 0, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
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
		}

		// --- Shader compilation ---
		init_shader();

		// --- Scanline overlay quad (VAO/VBO) ---
		init_scanline_quad();

		// --- Vector font renderer ---
		LOG_INFO("Building vector font...");
		VF.Initialize(1024, 768);

		// --- Tiled scanlines effect ---
		//TiledEffect_Init();

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
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	screen_rect->Render();

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

// ====================================================================
// render_ui_overlays()
//
// Draws the pause dim, PAUSED text, exit confirmation dialog, menu,
// FPS counter, and debug overlays on top of the current backbuffer.
//
// Called by BOTH rendering paths:
//   - Vector pipeline: from final_render() after FBO4 blit
//   - Raster pipeline: from emulator_run() after RasterRender_Present()
//
// Sets up its own 1024x768 ortho projection on the backbuffer.
// The caller must have already rendered the game frame before calling.
// ====================================================================
void render_ui_overlays(int winW, int winH)
{
	if (winW < 1 || winH < 1) return;

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

	glViewport(vpX, 0, vpW, winH);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 1024, 0, 768, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// Tell VF not to override our viewport when Begin() is called.
	// VF's internal 1024x768 ortho projection still maps correctly
	// because glViewport stretches the output to the full window.
	VF.SetOverrideViewport(false);

	if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
	{
		// Vector overlays render onto the 1024x1024 FBO - switch ortho to match.
		set_ortho(1024, 1024);
	}
	// else: keep the 1024x768 ortho already set above for raster window overlays.

	//------------------------------------------------------------------
	// Dim background and draw PAUSED text if needed
	//------------------------------------------------------------------
	if (paused || get_menu_status())
	{
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		quad_from_center(512.0f, 384.0f, 1024.0f, 768.0f, 0, 0, 0, 127);

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
			glDisable(GL_TEXTURE_2D);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			quad_from_center(512.0f, 384.0f, 1024.0f, 768.0f, 0, 0, 0, 216);
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

	// Restore GL state for the next frame's vector pipeline
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

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
void set_render()
{
	// Set 1024x1024 ortho to match the FBO dimensions.
	if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
	{	// Bind FBO1 and direct output to attachment 0 (img1a).
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
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

		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo_raster);
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

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
void render()
{
	// Only process new game geometry if we are not paused.
	// (If paused, FBO1 retains the image from the last active frame).
	if (!paused)
	{
		if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
		{
			vector_update();  // Test, add conditions or move fully to it.
			draw_all();
			vector_clear_list(); // Test - Move this out of here.
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
		GLint vp[4] = { 0,0,0,0 };
		glGetIntegerv(GL_VIEWPORT, vp);

		GLfloat proj[16] = { 0 };
		glGetFloatv(GL_PROJECTION_MATRIX, proj);

		GLint drawBuf = 0;
		glGetIntegerv(GL_DRAW_BUFFER, &drawBuf);

		GLint fboId = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &fboId);

		// Query the texture dimensions of art_tex[1]
		int texW = 0, texH = 0;
		glBindTexture(GL_TEXTURE_2D, art_tex[0]);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texW);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texH);

		// Also query what's currently on the draw target (img4b)
		int targetW = 0, targetH = 0;
		glBindTexture(GL_TEXTURE_2D, img4b);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &targetW);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &targetH);

		LOG_INFO("OVERLAY1 DEBUG: viewport=(%d,%d,%d,%d) drawbuf=0x%X fbo=%d",
			vp[0], vp[1], vp[2], vp[3], drawBuf, fboId);
		LOG_INFO("OVERLAY1 DEBUG: art_tex[1] size=%dx%d  img4b size=%dx%d",
			texW, texH, targetW, targetH);
		LOG_INFO("OVERLAY1 DEBUG: draw coords left=%d right=%d top=%d bottom=%d",
			left, right, top, bottom);
		LOG_INFO("OVERLAY1 DEBUG: proj row0=(%.3f, %.3f, %.3f, %.3f)",
			proj[0], proj[4], proj[8], proj[12]);
		LOG_INFO("OVERLAY1 DEBUG: proj row1=(%.3f, %.3f, %.3f, %.3f)",
			proj[1], proj[5], proj[9], proj[13]);
		LOG_INFO("OVERLAY1 DEBUG: proj row2=(%.3f, %.3f, %.3f, %.3f)",
			proj[2], proj[6], proj[10], proj[14]);
		LOG_INFO("OVERLAY1 DEBUG: proj row3=(%.3f, %.3f, %.3f, %.3f)",
			proj[3], proj[7], proj[11], proj[15]);






		//float overlay_height =  (Machine->drv->rotation & ORIENTATION_SWAP_XY) ? (float)bottom : ((float)bottom * 0.75f);

		glEnable(GL_TEXTURE_2D);
		set_texture(&art_tex[1], 1, 0, 0, 0);

		glEnable(GL_BLEND);
		if (Machine->drv->video_attributes & VIDEO_TYPE_RASTER_BW)
			glBlendFunc(GL_DST_COLOR, GL_ZERO);
		else
			glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);

		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

		drawTexturedQuad((float)left, (float)right, (float)top, (float) bottom, false);


		//drawTexturedQuad(0, 1024, 0, 1024, false);
	}

	//--------------------------------------------------------------------------
	// LAYER 5C: Composite to img4a (FBO4 attachment 0)
	//--------------------------------------------------------------------------
	set_render_fbo4();

	auto DrawCabinetScaledLayer = [&](GLuint tex, bool is_pre_squished) {
		if (!tex) return;
		glEnable(GL_TEXTURE_2D);
		set_texture(&tex, 1, 0, 0, 0);

		float base_h = 1024; //is_pre_squished ? 1024.0f : (1024.0f * 0.75f);

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
	// Todo: if (config.vectrail == 0) adjust more
	if ((config.artwork && art_loaded[0]) || (config.overlay && art_loaded[1] && uses_overlay2))
	{
		// TWEAK THIS: 0.0f = no boost, 1.0f = double brightness.
		// Around 0.4f - 0.6f usually gives vectors enough punch against dark artwork.
		// TODO: Make this configurable per game, this sucks with certain artwork.
		float crt_boost = (config.artwork && art_loaded[0]) ? 0.2f : 0.25f;
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

		//drawTexturedQuad((float)left, (float)right, (float)top, (float)bottom, false);
		drawTexturedQuad(0, 1024, 0, 1024, false);
	}

	//--------------------------------------------------------------------------
	// LAYER 6: Bezel frame overlay
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

	render_ui_overlays(1024, 768);

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

void render_scanlines()
{
	int scan_x = 0;
	int scan_y = 0;
	get_texture_size(g_scanrezTex, &scan_x, &scan_y);

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo_raster);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

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

	// 1. DISENGAGE FBO: Essential to "close" img5a so the GPU can read it.
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);

	// 2. VIEWPORT RESCUE: Handle the case where rendering reset might have failed
	int vW = (ws.clientWidth > 0) ? ws.clientWidth : 1024;
	int vH = (ws.clientHeight > 0) ? ws.clientHeight : 768;
	glViewport(0, 0, vW, vH);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

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
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);
	glViewport(0, 0, ws.clientWidth, ws.clientHeight);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	Layout_Render(*g_activeView, img5a, ws.clientWidth, ws.clientHeight, vattr);

	// State reset for UI overlay draws
	glUseProgram(0);
	if (glBindVertexArray) glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glActiveTexture(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_BLEND);

	//We need to check aspect here, if it's too wide, correct it.
	render_ui_overlays(ws.clientWidth, ws.clientHeight);

	glDisable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);

	check_gl_error_named("final_render_raster (exit)");

	if (config.debug_profile_code)
	{
		auto end = std::chrono::steady_clock::now();
		auto diff = end - start;
		LOG_INFO("Profiler: final_render_raster took %.3f ms",
			std::chrono::duration<double, std::milli>(diff).count());
	}
}