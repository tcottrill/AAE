//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME
// code, 0.29 through .90 mixed with code of my own. This emulator was
// created solely for my amusement and learning and is provided only
// as an archival experience.
//
// All MAME code used and abused in this emulator remains the copyright
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
//
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

// New 2024
#include "os_basic.h"
#include "newfbo.h"
#include "MathUtils.h"
#include <chrono> // For code profiling
#include "path_helper.h"
#include "old_mame_raster.h"

//Please fix this
extern void osd_get_pen(int pen, unsigned char* red, unsigned char* green, unsigned char* blue);

Rect2 *screen_rect = nullptr;
//Raster rendering
Fpoly* sc;
extern float vid_scale;

// Notes for reference:
//
// config.artwork
// config.overlay
// config.bezel
// Artwork Setting.
// Backdrop : layer 0	 art_tex[0]
// Overlay : layer 1		 art_tex[1]
// Bezel Mask : layer 2  art_tex[2] Only used for the tempest and tacscan bezel mask, please fix with a shader or different blending
// Bezel : layer 3		 art_tex[3]
// Screen burn layer 4:   art_tex[4] (Not currently used)

//NEW CODE 2024 ////////////////////////////////////////////
// Rendering Screen Variables
enum RotationDir { NONE, RIGHT, LEFT, OVER } rotation;
multifbo* fbo;
int adj_horiz = 0;
int adj_vert = 0;

void emulator_on_window_resize(int newW, int newH)
{
	if (!screen_rect) return;

	auto& ws = GetWindowSetup();

	screen_rect->UpdateScreenRect(ws.clientWidth, ws.clientHeight, ws.aspectRatio, 0);
	LOG_INFO("New Screen Rect, Width: %d Height: %d", ws.clientWidth, ws.clientHeight);
}

void raster_poly_update(void)
{
	//LOG_INFO("Update Screen Called");
	int x, y;
	unsigned char  c = 0;
	unsigned char r1, g1, b1;

	vid_scale = 3.0;

	for (x = Machine->drv->visible_area.min_y; x < Machine->drv->visible_area.max_y + 1; x++)
	{
		for (y = Machine->drv->visible_area.min_x; y < Machine->drv->visible_area.max_x + 1; y++)
		{
			c = main_bitmap->line[x][y];
			//Only update if it is non black?
			if (main_bitmap->line[x][y])
			{
				osd_get_pen(Machine->pens[c], &r1, &g1, &b1);
				sc->addPoly(y, x, vid_scale, MAKE_RGBA(r1, g1, b1, 0xff));
			}
		}
	}
}
//
// END OF NEW CODE 2024

void Widescreen_calc()
{
	float val = 0;

	if (config.widescreen == 0) val = 1.3333f;
	if (config.widescreen == 1) val = 1.77f;
	if (config.widescreen == 2) val = 1.6f;

	wideadj = 1.3333 / val;
}

void set_ortho_proper()
{
	glViewport(0, 0, Machine->gamedrv->screen_width * 3, Machine->gamedrv->screen_height * 3);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, Machine->gamedrv->screen_width * 3, Machine->gamedrv->screen_height * 3, 0, -1, 1); // Define a 2D orthographic view
	glMatrixMode(GL_MODELVIEW);
}

void set_ortho(GLint width, GLint height)
{
	glMatrixMode(GL_PROJECTION);							// Select The Projection Matrix
	glLoadIdentity();										// Reset The Projection Matrix
	glViewport(0, 0, width, height);
	glOrtho(0, width, 0, height, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);								// Select The Modelview Matrix
	glLoadIdentity();
}

// This should only be ran ONCE per each instantiation of this program.
int init_gl(void)
{
	static int init_one = 0;

	if (!init_one)
	{
		if (config.forcesync)
		{
			if (wglewIsSupported("WGL_EXT_swap_control"))
			{
				wglSwapIntervalEXT(1);
				LOG_INFO("Enabling vSync per the config.forcesync setting.");
			}
			else LOG_INFO("Your video card does not support vsync. Please check and update your video drivers.");
		}
		else {
			if (wglewIsSupported("WGL_EXT_swap_control"))
			{
				wglSwapIntervalEXT(0);
				LOG_INFO("Disabling vSync");
			}
			else LOG_INFO("There was a problem disabling vSync, please check your video card drivers.");
		}

		// Reset The Current Viewport
		set_ortho(1024, 768);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);					// Black Background

		// Set Line Antialiasing
		glEnable(GL_BLEND);	// Enable Blending
		glEnable(GL_LINE_SMOOTH);
		glEnable(GL_POINT_SMOOTH);
		glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
		glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);		// Type Of Blending To Use
		glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
		glLineWidth(config.linewidth);//linewidth
		glPointSize(config.pointsize);//pointsize
		// New CODE
		auto& ws = GetWindowSetup();

		screen_rect = new Rect2(ws.clientWidth, ws.clientHeight, ws.aspectRatio, 0);
		//calc_screen_rect(config.screenw, config.screenh, config.aspect, 0);
		

		// TODO:  Reevaluate
		/*
		make_single_bitmap(&error_tex[0], "error.png", "aae.zip", 0); //This has to be loaded before any driver init.
		make_single_bitmap(&error_tex[1], "info.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[0], "joystick.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[1], "spinner.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[2], "settings.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[3], "video.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[4], "sound.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[5], "gamma.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[6], "buttons.png", "aae.zip", 0);
		*/
		////////////////////////////////////////////////////////////////////////////////

		LOG_INFO("Initalizing FBO's");
		fbo_init();
		LOG_INFO("Building Font");
		VF.Initialize(1024, 768);
		init_shader();
		LOG_INFO("Finished configuration of OpenGl sucessfully");
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		sc = new Fpoly();
		
		init_one++;
	}
	return 1;
}

void end_gl()
{
	LOG_INFO("AAE Gl Shutdown");
}

int make_single_bitmap(GLuint* texture, const char* filename, const char* archname, int mtype)
{
	//char temppath[MAX_PATH] = { 0 };
	//int test = 0;
	//int ret = 0;
	//unsigned char* zipdata = 0;
	std::string temppath;

	temppath = getpathM("artwork", archname);

	//strcpy(temppath, "artwork\\");
	//strcat(temppath, archname);
	//strcat(temppath, "\0");

	LOG_INFO("Artwork Path: %s", temppath.c_str());

	*texture = load_texture(filename, temppath.c_str(), 4, 1);

	if (texture) return 1;
	else return 0;
}

void load_artwork(const struct artworks* p)
{
	int i;
	int goodload = 0;
	int overlay = 0;
	int artwork = 0;
	int bezel = 0;
	int type = 0;
	//OK save the current artwork settings for comparison.
	artwork = config.artwork;
	overlay = config.overlay;
	bezel = config.bezel;
	//config.artwork=0;
	//config.overlay=0;
	//config.bezel=0;
	//Artwork Setting.
	//Backdrop : layer 0
	//Overlay : layer 1
	// Bezel Mask : layer 2
	// Bezel : layer 3
	//Screen burn layer 4:

	for (i = 0; p[i].filename != NULL; i++)
	{
		//goodload = 0;
		switch (p[i].type)
		{
		case FUN_TEX:  goodload = make_single_bitmap(&fun_tex[p[i].target], p[i].filename, p[i].zipfile, 0); break;
		case ART_TEX: {
			goodload = make_single_bitmap(&art_tex[p[i].target], p[i].filename, p[i].zipfile, type);
			if (goodload) { art_loaded[p[i].target] = 1; }
		}break;

		case GAME_TEX: goodload = make_single_bitmap(&game_tex[p[i].target], p[i].filename, p[i].zipfile, 0); break;
		default: LOG_ERROR("You have defined something wrong in the artwork loading!!!!"); break;
		}
		if (goodload == 0) { LOG_ERROR("A requested artwork file was not found!"); have_error = 15; }
	}
}

void free_game_textures()
{
	int i = 0;

	glDeleteTextures(1, &game_tex[0]);
	glDeleteTextures(1, &art_tex[0]);
	glDeleteTextures(1, &art_tex[1]);
	glDeleteTextures(1, &art_tex[3]);
}

void set_render_fbo4()
{
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo4);
	//Write To Texture img1a
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	
	set_ortho(1024, 1024);
	// Then render as normal
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);	// Clear Screen And Depth Buffer
	glEnable(GL_BLEND);
	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST); //Disable depth testing
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_LINE_SMOOTH);
	glEnable(GL_POINT_SMOOTH);
	//Required for some older cards.
	glDisable(GL_DITHER);
}

void end_render_fbo4()
{
	//Set Ortho to 1024, and drawing to backbuffer
	//LOG_INFO("END RENDER FBO 4");
	//LogCurrentViewportAndProjection();
	check_gl_error_named("end_render_fbo_a");
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);
	glActiveTexture(GL_TEXTURE0);

	auto& ws = GetWindowSetup();
	set_ortho(ws.clientWidth, ws.clientHeight);
	
	set_texture(&img4a, 1, 0, 1, 0);
	screen_rect->Render(1.33f);
	check_gl_error_named("end_render_fbo_b");
}

////////////////////////////////////////////////////////////////////////////////
// FBO / SHADER DOWNSAMPLING and COMPOSITING CODE  /////////////////////////////
////////////////////////////////////////////////////////////////////////////////

//
//	Downsample Part 1,
//  This copies fbo1, img1b to fbo2, img2a at 512x512
//
void copy_main_img_to_fbo2()
{
	GLuint fbo2_tex = 0;
	glLoadIdentity();
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo2);
	//LOG_INFO("Debug Remove : img_copy to FBO2 1.0");
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	set_ortho(512, 512);
	glDisable(GL_BLEND);
	set_texture(&img1b, 1, 0, 0, 0);
	//LOG_INFO("Debug Remove : img_copy to FBO2 2.0");
	glActiveTexture(GL_TEXTURE0);
	//LOG_INFO("Debug Remove : img_copy to FBO2 2.5");
	bind_shader(fragBlur);
	//LOG_INFO("Debug Remove : img_copy to FBO2 2.6");
	check_gl_error_named("copy_main_img_to_fbo2");
	set_uniform1i(fragBlur, "colorMap", fbo2_tex);
	//LOG_INFO("Debug Remove : img_copy to FBO2 2.6A");
	set_uniform1f(fragBlur, "width", 512.0f);
	set_uniform1f(fragBlur, "height", 512.0f);
	FS_Rect(0, 512);
	unbind_shader();
	//LOG_INFO("Debug Remove : img_copy to FBO2 4.0");
}

//
//	Downsample Part 2
//  This copies the 512x512 texture at fbo2, img1a to fbo3 img1a
//
void copy_fbo2_to_fbo3()
{
	GLuint fbo3_tex = 0;
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo3);

	// Clear buffers between frames.
	// This is a requirement
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	set_ortho(256, 256);
	glDisable(GL_BLEND);
	check_gl_error_named("openglerror in copy_fbo2_to_fbo3:");

	bind_shader(fragBlur);
	set_uniform1i(fragBlur, "colorMap", fbo3_tex);
	set_uniform1f(fragBlur, "width", 256.0f);
	set_uniform1f(fragBlur, "height", 256.0f);
	set_texture(&img2a, 1, 0, 0, 1);
	FS_Rect(0, 256);
	unbind_shader();
}

//
//	Downsample Part 3
//  This copies img2a to the 256x256 blur texture at fbo3, img3a to img1b pingpong back and forth to blur
//
// This code will be replaced by shader code.
void render_blur_image_fbo3()
{
	static constexpr float v1 = 1.0f;
	static constexpr float v2 = 2.0f;

	GLuint fbo3ab_tex = 0; // Texture bound to the shader

	// 8 directional shift pairs (each 4 values = two shifts per pass)
	// Directional float shifts for v1 and v2
	float fshifta[] = {
		v1,  0,  -v1,   0, // RIGHT
	   -v1,  0,   v1,   0, // LEFT
		 0,  v1,   0, -v1, // UP
		 0, -v1,   0,  v1, // DOWN
		v1,  v1, -v1, -v1, // DIAGONAL: UP-RIGHT, DOWN-LEFT
	   -v1, -v1,  v1,  v1, // DIAGONAL: DOWN-LEFT, UP-RIGHT
	   -v1,  v1,  v1, -v1, // DIAGONAL: UP-LEFT, DOWN-RIGHT
		v1, -v1, -v1,  v1  // DIAGONAL: DOWN-RIGHT, UP-LEFT
	};

	float fshiftb[] = {
		v2,  0,  -v2,   0, // RIGHT
	   -v2,  0,   v2,   0, // LEFT
		 0,  v2,   0, -v2, // UP
		 0, -v2,   0,  v2, // DOWN
		v2,  v2, -v2, -v2, // DIAGONAL: UP-RIGHT, DOWN-LEFT
	   -v2, -v2,  v2,  v2, // DIAGONAL: DOWN-LEFT, UP-RIGHT
	   -v2,  v2,  v2, -v2, // DIAGONAL: UP-LEFT, DOWN-RIGHT
		v2, -v2, -v2,  v2  // DIAGONAL: DOWN-RIGHT, UP-LEFT
	};

	bind_shader(fragBlur);
	set_uniform1i(fragBlur, "colorMap", fbo3ab_tex);
	set_uniform1f(fragBlur, "width", 256.0f);
	set_uniform1f(fragBlur, "height", 256.0f);

	glEnable(GL_TEXTURE_2D); // Compatibility profile
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glColor4f(0.1f, 0.1f, 0.1f, 0.1f); // Used by FS_Rect

	int i = 0; // Index into shift arrays

	// Apply a global nudge downward to compensate for drift
	glTranslatef(0.0f, -0.20f, 0.0f);
	// Apply a global nudge to the left to compensate for drift as well.
	glTranslatef(-0.05f, 0.0f, 0.0f);

	// 4 passes = 8 blur directions (2 shifts per pass)
	for (int pass = 0; pass < 4; ++pass)
	{
		// A → B
		glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
		glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
		set_texture(&img3a, 1, 0, 0, 0);
		glTranslatef(fshifta[i], fshifta[i + 1], 0.0f);
		FS_Rect(0, height3);
		glTranslatef(fshifta[i + 2], fshifta[i + 3], 0.0f);

		// B → A
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		glReadBuffer(GL_COLOR_ATTACHMENT1_EXT);
		set_texture(&img3b, 1, 0, 0, 0);
		glBindTexture(GL_TEXTURE_2D, img3b); // Redundant safety bind
		glTranslatef(fshiftb[i], fshiftb[i + 1], 0.0f);
		FS_Rect(0, height3);
		glTranslatef(fshiftb[i + 2], fshiftb[i + 3], 0.0f);

		i += 4; // Advance to next shift pair
	}

	check_gl_error_named("OpenGL error in render_blur_image_fbo3");
	unbind_shader();
}

////////////////////////////////////////////////////////////////////////////////
// END  FBO / SHADER DOWNSAMPLING and COMPOSITING CODE						  //
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//			 FINAL COMPOSITING AND RENDERING CODE STARTS HERE				  //
////////////////////////////////////////////////////////////////////////////////

// Rendering Start, this is STEP 1 This sets the screen viewport and projection to 1014x1024 for rendering
// to FBO1, img1a.
void set_render()
{
	// First we bind to FBO1 so we can render to it
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
	//Write To Texture img1a
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

	// Set the projection to 1024x1024
	set_ortho(1024, 1024);

	// Then render as normal
	//glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);// | GL_DEPTH_BUFFER_BIT);	// Clear Screen And Depth Buffer
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Leaving these here for review
	//glDisable(GL_LIGHTING);
	//glDisable(GL_DEPTH_TEST);
	// LOG_INFO("Setting FBO renderer");
	//glEnable(GL_MULTISAMPLE_ARB);
	//glEnable(GL_SAMPLE_COVERAGE);
	//glEnable(GL_LINE_SMOOTH);
	//glEnable(GL_POINT_SMOOTH);
	// Required for some older cards.
	//glDisable(GL_DITHER);
	//LOG_INFO("SET RENDER");
	//LogCurrentViewportAndProjection();

	check_gl_error_named("openglerror in set_render");
}

// Rendering Continued, this is STEP 2, this is where the vectors are drawn to our 1024x1024 texture.
void render()
{
	if (paused) { pause_loop(); return; }

	if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
	{
		draw_all();
		final_render(sx, sy, ex, ey, 0, 0);
	}
	else
	{
		raster_poly_update();
		sc->Render();
		//final_render_raster();
		final_render(sx, sy, ex, ey, 0, 0);
	}
}

// Note:
// art_tex[0] is always Backdrop
// art_tex[1] is always Overlay
// art_tex[3] is always Bezel
// FINAL RENDERING and COMPOSITING HERE:
// Rendering Continued, this is STEP 3
void final_render(int xmin, int xmax, int ymin, int ymax, int shiftx, int shifty)
{
	GLint bleh = 0;
	// Glow Shader variable
	GLfloat glowamt = 0.0;
	// Glow enabled variable
	int useglow = 0;
	// Used for code profiling disable in final release.
	auto start = std::chrono::steady_clock::now();

	/////////////////////////////	//SWITCH TO FBO1/TEX 2 HERE FOR FINAL COMPOSITING!!!!////////////////

	glEnable(GL_TEXTURE_2D);
	// This creates the feedback texture used for the blur texture in the next segment.
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f); //THIS IS FOR THE VARIOUS MONITOR TYPES???
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	set_texture(&img1a, 1, 0, 0, 1);
	glTranslatef(.25, .25, 0);  // This may need to go.
	glBlendFunc(GL_ONE, GL_ONE);
	// Draw the vecture texture to fbo1, img1b
	Any_Rect(0, xmin, xmax, ymin, ymax);
	//////////////////////////////////////////////////////////////////////////////////////////////////////

	// DRAW OVERLAY to FBO1, Image img1b from image1a Ortho 1024x1014, Viewport 1024x1024
	// Rotated overlays for tempest and tacscan are handled as bezels down below, fix.
	
	if (config.overlay && art_loaded[1]) {
		if (Machine->gamedrv->video_attributes & VECTOR_USES_OVERLAY1)

		{
			glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);
			glColor4f(1.0f, 1.0f, 1.0f, .5f);
		}

		set_texture(&art_tex[1], 1, 0, 0, 0);
		FS_Rect(0, 1024);
	}

	//// Render to fbo2, attachment2 (img1b) from img1b.  //////////////////////////////////////////////////

	if (config.vectrail)
	{
		// FBO1 is still bound at this point, switching to drawing the feedback texture blending on top of itself, frame additive
		glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
		glDisable(GL_DITHER);
		set_texture(&img1b, 1, 0, 0, 0);
		//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);

		// This sets the amount of phosphor persistence.
		switch (config.vectrail)
		{
		case 1:  glColor4f(1.0f, 1.0f, 1.0f, .825f); break;//glColor4f(.75,.75,.75,.925); break; // glColor4f(.85,.85,.85,.80);
		case 2:  glColor4f(1.0f, 1.0f, 1.0f, .86f); break;//glColor4f(.70,.70,.70,.86);break;
		case 3:  glColor4f(1.0f, 1.0f, 1.0f, .93f); break;//glColor4f(.7f,.7f,.7f,.93); break;
		default:  glColor4f(.95f, .95f, .95f, 1.0f); break;
		}
		FS_Rect(0, 1024);
	}

	/////// RENDER the blur texture to FBO2 and FBO3, using Image img1b and img2a
	if (config.vecglow)
	{
		//Bind FBO2.
		copy_main_img_to_fbo2();
		copy_fbo2_to_fbo3(); // Bind FBO3 and set target to img3a
		render_blur_image_fbo3(); // RENDER final blur texture to FBO3, using Image img3a and img3b to blend
	}

	//Set Ortho to 1024, and drawing to backbuffer
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);
	set_ortho(1024, 768);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glDisable(GL_DITHER);

	if (!paused) glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	//Render combined texture
	if (config.vecglow) { useglow = 1; }
	bind_shader(fragMulti);
	// Get the variables for the shader.

	bleh = glGetUniformLocation(fragMulti, "mytex1"); glUniform1i(bleh, 0);
	bleh = glGetUniformLocation(fragMulti, "mytex2"); glUniform1i(bleh, 1);
	bleh = glGetUniformLocation(fragMulti, "mytex3"); glUniform1i(bleh, 2);
	bleh = glGetUniformLocation(fragMulti, "mytex4"); glUniform1i(bleh, 3);
	bleh = glGetUniformLocation(fragMulti, "useart");

	// If artwork is loaded, check to see if backdrop artwork should be used. How is this working?
	if (art_loaded[0]) { glUniform1i(bleh, config.artwork); }
	else { glUniform1i(bleh, 0); }

	set_uniform1i(fragMulti, "usefb", config.vectrail);
	set_uniform1i(fragMulti, "useglow", useglow);
	set_uniform1f(fragMulti, "glowamt", config.vecglow * .01);
	set_uniform1i(fragMulti, "brighten", gamenum);

	//Activate all 3 texture units
	glActiveTexture(GL_TEXTURE0);
	set_texture(&art_tex[0], 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE1);
	set_texture(&img1b, 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, img3a);
	set_texture(&img3b, 1, 0, 0, 0);
	// I don't see where this texture is being modified anywhere?
	glActiveTexture(GL_TEXTURE3);
	set_texture(&img1c, 1, 0, 0, 0);
	
	// FINAL RENDERING TO SCREEN IS RIGHT HERE
	// Enable fbo4, and render everything below to it, then render to the screen with the correct size and aspect.
	set_render_fbo4();
	if (config.bezel && art_loaded[3])
	{
		glColor4f(1.0, 1.0, 1.0, 1.0);
		//if (config.debug)	//{	Any_Rect(0, msx, msy, esy, esx);}
		Any_Rect(0, b1sx, b1sy, b2sy, b2sx);
	}
	else
	{
		glColor4f(1.0, 1.0, 1.0, 1.0);
		set_ortho(config.screenw, config.screenh);
		glDisable(GL_BLEND);
		// Render the main combined image to the back buffer.
		Screen_Rect(0, 1024);
	}

	//// Turn off the Shader /////////////////////////////////
	unbind_shader();

	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE3_ARB);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_2D, 0); // Don't disable GL_TEXTURE_2D on the main texture unit.

	if (config.bezel && art_loaded[3]) {
		if (config.artcrop)
		{
			glScalef(bezelzoom, bezelzoom, 0);
			glTranslatef(bezelx, bezely, 0);
		}

		glEnable(GL_ALPHA_TEST);
		glDisable(GL_BLEND);
		glAlphaFunc(GL_GREATER, 0.2f);

		set_texture(&art_tex[3], 1, 0, 0, 1);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		Resize_Rect(0, 1024);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_ALPHA_TEST);
	}

	glLoadIdentity();

	video_loop();
	end_render_fbo4();
	glDisable(GL_TEXTURE_2D);

	if (config.debug_profile_code) {
		auto end = std::chrono::steady_clock::now();
		auto diff = end - start;
		LOG_INFO("Profiler: Render Time after final compositing %f ", std::chrono::duration <double, std::milli>(diff).count());
	}
}

////////////////////////////////////////////////////////////////////////////////
// FINAL COMPOSITING AND RENDERING CODE ENDS HERE  ///////////////////////////
////////////////////////////////////////////////////////////////////////////////
/*
void set_render_raster()
{
	LOG_INFO("Set Render Raster Called");
	// First we bind to FBO1 so we can render to it
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo_raster);
	//Write To Texture img1a
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

	set_ortho_proper();

	// Then render as normal
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);// | GL_DEPTH_BUFFER_BIT);	// Clear Screen And Depth Buffer
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_TEXTURE_2D);

	check_gl_error_named("openglerror in set_render:");
}

void final_render_raster()
{
	LOG_INFO("Final Render Raster Called");
	int err = glGetError();
	check_gl_error_named("openglerror in final_render_fbo_raster:");

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);

	glColor4f(1.0, 1.0, 1.0, 1.0);
	set_texture(&img5a, 1, 0, 1, 0);
	screen_rect.Render(1.0f);

	//Centered_Rect(0, 1024);

	check_gl_error_named("openglerror in end_render_fbo_b:");
}
*/