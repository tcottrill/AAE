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

#include <chrono>

#include "glcode.h"
#include "aae_mame_driver.h"
#include "fonts.h"
#include "vector_fonts.h"
#include "vector.h"
#include "loaders.h"
#include "gl_fbo.h"
#include "gl_texturing.h"
#include "gl_shader.h"

// New 2024
#include "os_basic.h"

#include "newfbo.h"
#include "MathUtils.h"
#include <chrono> // For code profiling

using namespace std;
using namespace chrono;

Rect2 screen_rect;

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
//
//
//

void calc_screen_rect(int screen_width, int screen_height, char* aspect, int rotated)
{
	float indices[32] =
	{
		//normal
		0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0,
		//rotated right
		1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0,
		//rotated left
		0.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0,
		//flip
		1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0
	};

	float target_width = 0.0;
	float new_screen_height = 0.0;
	float xadj = 0.0;
	float yadj = 0.0;
	int temp = 0;

	//Lets decode our aspect first!
	int aspectx, aspecty;

	if (sscanf(aspect, "%d:%d", &aspectx, &aspecty) != 2 || aspectx == 0 || aspecty == 0)
	{
		wrlog("error: invalid value for aspect ratio: %s\n", aspect);
		wrlog("Setting aspect to 4/3 because of invalid config file setting.");
		aspectx = 4; aspecty = 3;
	}

	//rotated = RotationDir::RIGHT;
	if (rotated)
	{
		temp = aspecty;
		aspecty = aspectx;
		aspectx = temp;
	}

	//First, lets see if it will fit on the screen
	target_width = (float)screen_height * (float)((float)aspectx / (float)aspecty);

	if (target_width > screen_width)
	{
		//Oh no, now we have to adjust the Y value.
		new_screen_height = (float)screen_width * (float)((float)aspecty / (float)aspectx);
		wrlog("New Screen Height is %f", new_screen_height);
		//Get the new width value that should fit on the screen
		target_width = (float)new_screen_height * (float)((float)aspectx / (float)aspecty);
		yadj = ((float)screen_height - (float)new_screen_height) / 2;
		wrlog("Yadj (Divided by 2) is %f", yadj);
		screen_height = new_screen_height;
	}
	wrlog("Target width is %f", target_width);

	//Now adjust to center in the screen
	xadj = (screen_width - target_width) / 2.0;
	wrlog("Xadj (Divided by 2) is %f", xadj);

	int v = 8 * rotated; //0,16,24
	//Now set the points
	screen_rect.BottomLeft(xadj, yadj, indices[v], indices[v + 1]);
	wrlog("Bottom Left %f %f", xadj, yadj);
	screen_rect.TopLeft(xadj, screen_height + yadj, indices[v + 2], indices[v + 3]);
	wrlog("Top Left %f %f", xadj, screen_height + yadj);
	screen_rect.TopRight(target_width + xadj, screen_height + yadj, indices[v + 4], indices[v + 5]);
	wrlog("Top Right %f %f", target_width + xadj, screen_height + yadj);
	screen_rect.BottomRight(target_width + xadj, yadj, indices[v + 6], indices[v + 7]);
	wrlog("Bottom Right %f %f", target_width + xadj, yadj);
}

//
//
//
// END OF NEW CODE 2024

void Widescreen_calc()
{
	float val = 0;

	if (config.widescreen == 0) val = 1.3333;
	if (config.widescreen == 1) val = 1.77;
	if (config.widescreen == 2) val = 1.6;

	wideadj = 1.3333 / val;//val;  1.6
}

void set_ortho(GLint width, GLint height)
{
	glMatrixMode(GL_PROJECTION);							// Select The Projection Matrix
	glLoadIdentity();										// Reset The Projection Matrix
	glViewport(0, 0, width - 1, height - 1);
	glOrtho(0, width - 1, 0, height - 1, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);								// Select The Modelview Matrix
	glLoadIdentity();
}

// This should only be ran ONCE per each instantiation of this program.

int init_gl(void)
{
	GLint texSize;

	static int init_one = 0;

	if (!init_one)
	{
		allegro_gl_clear_settings();
		allegro_gl_set(AGL_COLOR_DEPTH, 32); //bpp
		allegro_gl_set(AGL_DOUBLEBUFFER, config.dblbuffer);//config.dblbuffer
		allegro_gl_set(AGL_Z_DEPTH, 24);
		allegro_gl_set(AGL_WINDOWED, 1); //windowed
		allegro_gl_set(AGL_RENDERMETHOD, 1);
		//allegro_gl_set(AGL_FLOAT_COLOR    ,1);
		//allegro_gl_set(AGL_SAMPLE_BUFFERS,1);//sample_enable);
		//allegro_gl_set(AGL_SAMPLES,16);//config.drawzero);
		//allegro_gl_set(AGL_SUGGEST, AGL_COLOR_DEPTH | AGL_DOUBLEBUFFER | AGL_RENDERMETHOD
		//	| AGL_Z_DEPTH | AGL_WINDOWED );//| AGL_FLOAT_COLOR);
		allegro_gl_set(AGL_SUGGEST, AGL_COLOR_DEPTH | AGL_DOUBLEBUFFER | AGL_RENDERMETHOD
			| AGL_Z_DEPTH | AGL_WINDOWED);//|AGL_SAMPLES |AGL_SAMPLE_BUFFERS);
		set_color_depth(32);
		request_refresh_rate(60); //config.screenw  config.screenh
		if (set_gfx_mode(GFX_OPENGL, config.screenw, config.screenh, 0, 0)) {
			allegro_message("Unable to set screen mode: %s", allegro_error);
			return 0;
		}
		allegro_gl_use_alpha_channel(TRUE);

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
			//screenRect2 = new Rect2DVBO;
			//config.screenw = 1280;
			//config.screenh = 1024;
		calc_screen_rect(config.screenw, config.screenh, config.aspect, 0);

		// END NEW CODE
		Widescreen_calc();
		//center_window();
		MoveWindow(win_get_window(), 0, 0, config.screenw, config.screenh, TRUE);
		setwindow();
		SetTopMost(TRUE);
		center_window();

		//Check for texture support
#if defined AGL_VERSION_2_0
		wrlog("OpenGL 2.0 support detected, good.");
#endif
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texSize);
		wrlog("Max Texture Size supported by this card: %dx%d", texSize, texSize);
		if (texSize < 1024)  wrlog("Warning!! Your card does not support a large enough texture size to run this emulator!!!!");
		glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS_EXT, &texSize);
		wrlog("Max number of color buffers per fbo supported on this card: %d", texSize);
		//Extensions Check

		if (allegro_gl_extensions_GL.ARB_multisample) { glEnable(GL_MULTISAMPLE_ARB); wrlog("ARB_Multisample Extension Supported"); }
		if (allegro_gl_extensions_GL.EXT_texture_filter_anisotropic) { wrlog("Anisotropic Filtering Supported"); }

		if (config.forcesync)
		{
			if (allegro_gl_extensions_WGL.EXT_swap_control) { wglSwapIntervalEXT(1); wrlog("Your video card supports vysnc, good."); }
			else wrlog("Your video card does not support vsync! Either your drivers suck, or your card does.\nPlease \
		       update your video drivers and run this again!!");
		}
		else {
			if (allegro_gl_extensions_WGL.EXT_swap_control) { wglSwapIntervalEXT(0); wrlog("Setting vsync off"); }
			else wrlog("Problem turning off vsync?");
		}

		if (allegro_gl_extensions_GL.EXT_framebuffer_object) { wrlog("EXT_Frambuffer Object Supported (Required)"); }
		else { allegro_message("I'm sorry, but you need EXT_framebuffer_object support to \n run this program. Update your card or drivers."); exit(1); }

		//////////// THESE MAY NEED TO BE REINSTANCIATED /////////////////////////////////////////////////////////////////////
		make_single_bitmap(&error_tex[0], "error.png", "aae.zip", 0); //This has to be loaded before any driver init.
		make_single_bitmap(&error_tex[1], "info.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[0], "joystick.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[1], "spinner.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[2], "settings.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[3], "video.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[4], "sound.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[5], "gamma.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[6], "buttons.png", "aae.zip", 0);
		////////////////////////////////////////////////////////////////////////////////

		wrlog("Initalizing FBO's");
		fbo_init();
		// NEW CODE
		//fbo = new multifbo(1024, 768, 16, 0, fboFilter::FB_NEAREST);
		wrlog("Building Font");
		BuildFont(); // Note to self: Remove this dependency. Move to using vector font everywhere including gui
		font_init(); //Build the Vector Font
		init_shader();
		wrlog("Finished configuration of OpenGl sucessfully");
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		init_one++;
	}

	allegro_gl_flip();
	return 1;
}

void end_gl()
{
	// KillFont();
	glDeleteTextures(1, &error_tex[0]);
	glDeleteTextures(1, &error_tex[1]);
	glDeleteTextures(1, &menu_tex[0]);
	glDeleteTextures(1, &menu_tex[1]);
	glDeleteTextures(1, &menu_tex[2]);
	glDeleteTextures(1, &menu_tex[3]);
	glDeleteTextures(1, &menu_tex[4]);
	glDeleteTextures(1, &menu_tex[5]);
	glDeleteTextures(1, &menu_tex[6]);
}

int make_single_bitmap(GLuint* texture, const char* filename, const char* archname, int mtype)
{
	char temppath[255] = { 0 };
	int test = 0;
	int ret = 0;
	unsigned char* zipdata = 0;

	strcpy(temppath, "artwork\\");
	strcat(temppath, archname);
	strcat(temppath, "\0");

	*texture = load_texture(filename, temppath, 4, 1);

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
			if (goodload) {	art_loaded[p[i].target] = 1;}	
		}break;
					 
		case GAME_TEX: goodload = make_single_bitmap(&game_tex[p[i].target], p[i].filename, p[i].zipfile, 0); break;
		default: wrlog("You have defined something wrong in the artwork loading!!!!"); break;
		}
		if (goodload == 0) { wrlog("A requested artwork file was not found!"); have_error = 15; }
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

// Code below from https://blog.nobel-joergensen.com/2013/01/29/debugging-opengl-using-glgeterror/
void CheckGLError(const char* file, int line) {
	GLenum err(glGetError());

	while (err != GL_NO_ERROR) {
		std::string error;

		switch (err) {
		case GL_INVALID_OPERATION:      error = "INVALID_OPERATION";      break;
		case GL_INVALID_ENUM:           error = "INVALID_ENUM";           break;
		case GL_INVALID_VALUE:          error = "INVALID_VALUE";          break;
		case GL_OUT_OF_MEMORY:          error = "OUT_OF_MEMORY";          break;
		case GL_STACK_OVERFLOW:         error = "GL_STACK_OVERFLOW";      break;
		case GL_STACK_UNDERFLOW:        error = "GL_STACK_UNDERFLOW";     break;
		case GL_INVALID_FRAMEBUFFER_OPERATION:  error = "INVALID_FRAMEBUFFER_OPERATION";  break;
		}

		wrlog("OpenGL Error: %s in %s file at line %d ", error.c_str(), file, line);
		err = glGetError();
	}
}

void GLCheckError(const char* call)
{
	GLenum errCode;

	char enums[][20] =
	{
		"invalid enumeration", // GL_INVALID_ENUM
		"invalid value",       // GL_INVALID_VALUE
		"invalid operation",   // GL_INVALID_OPERATION
		"stack overflow",      // GL_STACK_OVERFLOW
		"stack underflow",     // GL_STACK_UNDERFLOW
		"out of memory"        // GL_OUT_OF_MEMORY
	};

	GLenum errcode = glGetError();
	if (errcode == GL_NO_ERROR)
		return;

	errcode -= GL_INVALID_ENUM;
	wrlog("OpenGL %s in '%s'", enums[errcode], call);
}

void set_render_fbo4()
{
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo4);
	//Write To Texture img1a
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	set_ortho(SCREEN_W, SCREEN_H);
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
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);
	set_texture(&img4a, 1, 0, 1, 0);
	screen_rect.Render(1.33);
}

////////////////////////////////////////////////////////////////////////////////
// FBO / SHADER DOWNSAMPLING and COMPOSITING CODE  /////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//Downsample part 1
// This copies fbo1, img1b to fbo2, img2a at 512x512
void copy_main_img_to_fbo2()
{
	GLint loc = 0;

	glLoadIdentity();
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo2);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	set_ortho(512, 512);
	glDisable(GL_BLEND);
	set_texture(&img1b, 1, 0, 0, 0);

	glActiveTexture(GL_TEXTURE0);
	glUseProgram(fragBlur);

	loc = glGetUniformLocation(fragBlur, "colormap"); glUniform1i(loc, 0);
	loc = glGetUniformLocation(fragBlur, "width"); glUniform1f(loc, 512.0);
	loc = glGetUniformLocation(fragBlur, "height"); glUniform1f(loc, 512.0);

	FS_Rect(0, 512);
	glUseProgram(0);
}

// Downsample part 2
// This copies the 512x512 texture at fbo2, img1a to fbo3 img1a/
void copy_fbo2_to_fbo3()
{
	GLint loc = 0;

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo3);

	//Clear buffers between frames......
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

	set_ortho(256, 256);

	glUseProgram(fragBlur);

	loc = glGetUniformLocation(fragBlur, "colormap"); glUniform1i(loc, 0);
	loc = glGetUniformLocation(fragBlur, "width");    glUniform1f(loc, 256.0);
	loc = glGetUniformLocation(fragBlur, "height");   glUniform1f(loc, 256.0);

	glDisable(GL_BLEND);
	set_texture(&img2a, 1, 0, 0, 1);
	FS_Rect(0, 256);
	glUseProgram(0);
}

void render_blur_image_fbo3() //Downsample part 3
{
	static constexpr auto v1 = 1.5f;  //1.7 //1.0
	static constexpr auto v2 = 2.5f;    //2.7 //2.0
	GLint loc = 0;
	int i = 0;
	float fshifta[] = { v1,0,-v1,0,//RIGHT
					  -v1,0,v1,0,//LEFT
					  0, v1,0,-v1,
					  0,-v1,0, v1,
					  v1,v1,-v1,-v1,
					  -v1,-v1,v1,v1,
					  -v1,v1,v1,-v1,
					  v1,-v1,-v1,v1,
	};

	float fshiftb[] = { v2,0,-v2,0,//RIGHT
					 -v2,0,v2,0,//LEFT
					 0, v2,0,-v2,
					 0,-v2,0, v2,
					 v2,v2,-v2,-v2,
					 -v2,-v2,v2,v2,
					 -v2,v2,v2,-v2,
					 v2,-v2,-v2,v2,
	};

	glUseProgram(fragBlur);

	loc = glGetUniformLocation(fragBlur, "colormap");
	glUniform1i(loc, 0);
	loc = glGetUniformLocation(fragBlur, "width");
	glUniform1f(loc, 256.0);//225
	loc = glGetUniformLocation(fragBlur, "height");
	glUniform1f(loc, 256.0);//225

	for (int x = 4; x < 8; x++) //4 to 8
	{
		glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		//glActiveTexture(GL_TEXTURE0);
		set_blending2();

		set_texture(&img3a, 1, 0, 0, 0);
		glTranslatef(fshifta[i], fshifta[(i + 1)], 0);
		FS_Rect(0, height3);
		glTranslatef(fshifta[(i + 2)], fshifta[(i + 3)], 0);

		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		//glActiveTexture(GL_TEXTURE0);
		set_blending2();
		set_texture(&img3b, 1, 0, 0, 0);
		glBindTexture(GL_TEXTURE_2D, img3b);

		glTranslatef(fshiftb[(i)], fshiftb[(i + 1)], 0);
		FS_Rect(0, height3);
		glTranslatef(fshiftb[(i + 2)], fshiftb[(i + 3)], 0);
		i += 4;
	}
	glUseProgram(0);
}
////////////////////////////////////////////////////////////////////////////////
// END  FBO / SHADER DOWNSAMPLING and COMPOSITING CODE  /////////////////////////////
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// FINAL COMPOSITING AND RENDERING CODE STARTS HERE  ///////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Rendering Start, this is STEP 1 This sets the screen viewport and projection to 1014x1024 for rendering
// to FBO1, img1a.
void set_render()
{
	//glPushAttrib(GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT);
	//glLoadIdentity;
	//	GLint viewport[4];
	//	glGetIntegerv(GL_VIEWPORT, viewport);
		//Viewport here is : 0, 0, 1023, 767
		// viewport[0] = x coordinate of the lower-left corner
		// viewport[1] = y coordinate of the lower-left corner
		// viewport[2] = width of the viewport
		// viewport[3] = height of the viewport
		//wrlog("Viewport % d, % d, % d, % d", viewport[0], viewport[1], viewport[2], viewport[3]);

		// First we bind to FBO1 so we can render to it
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
	//Write To Texture img1a
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

	// Set the projection to 1024x1024
	set_ortho(1024, 1024);

	// Then render as normal
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);	// Clear Screen And Depth Buffer
	glEnable(GL_BLEND);
	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST); //Disable depth testing
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	// wrlog("Setting FBO renderer");
	 //glEnable(GL_MULTISAMPLE_ARB);
	 //glEnable(GL_SAMPLE_COVERAGE);
	glEnable(GL_LINE_SMOOTH);
	glEnable(GL_POINT_SMOOTH);
	// Required for some older cards.
	glDisable(GL_DITHER);
}

// Rendering Continued, this is STEP 2, this is where the vectors are drawn to our 1024x1024 texture.
void render()
{
	cache_end();

	if (paused) { pause_loop(); return; }

	if (gamenum)
	{
		if (driver[gamenum].vid_type == VEC_COLOR)
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			draw_color_vectors();
		}
		else if (driver[gamenum].vid_type == VEC_BW_16) { glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); draw_lines(); }
		else if (driver[gamenum].vid_type == VEC_BW_64) { glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); draw_color_vectors(); }
		//glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		glBlendFunc(GL_ONE, GL_ONE);
		glColor4f(1, 1, 1, 1);
		draw_texs();
	}

	if (config.debug) {
		if (config.bezel == 0) { final_render(msx, msy, esx, esy, 0, 0); } //ignore if bezel is enabled
		else { final_render(sx, sy, ex, ey, 0, 0); }
	}
	else { final_render(sx, sy, ex, ey, 0, 0); }
}

// Note:
// art_tex[0] is always Backdrop
// art_tex[1] is always Overlay
// art_tex[3] is always Bezel
// GUI is always gamenum 0, so gamenum has to be > 0 for all games
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
	auto start = chrono::steady_clock::now();

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
	if (gamenum)
	{
		// DRAW OVERLAY to FBO1, Image img1b from image1a Ortho 1024x1014, Viewport 1024x1024
		// Rotated overlays for tempest and tacscan are handled as bezels down below, fix.
		if (driver[gamenum].rotation < 2 && config.overlay && art_loaded[1]) {
			set_texture(&art_tex[1], 1, 0, 0, 0);
			//Normal Overlay
			glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
			//glColor4f(.7f, .7f, .7f, 1.0f);
			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
			FS_Rect(0, 1024);
		}
	}
	//// Render to fbo2, attachment2 (img1b) from img1b.  //////////////////////////////////////////////////

	if (config.vectrail && gamenum)
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
	if (config.vecglow && gamenum)
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
	if (config.vecglow && gamenum) { useglow = 1; }
	glUseProgram(fragMulti);
	// Get the variables for the shader.
	bleh = glGetUniformLocation(fragMulti, "mytex1"); glUniform1i(bleh, 0);
	bleh = glGetUniformLocation(fragMulti, "mytex2"); glUniform1i(bleh, 1);
	bleh = glGetUniformLocation(fragMulti, "mytex3"); glUniform1i(bleh, 2);
	bleh = glGetUniformLocation(fragMulti, "mytex4"); glUniform1i(bleh, 3);
	bleh = glGetUniformLocation(fragMulti, "useart");
	// If artwork is loaded, check to see if backdrop artwork shouly be used.
	if (gamenum && art_loaded[0]) { glUniform1i(bleh, config.artwork); }
	else { glUniform1i(bleh, 0); }

	bleh = glGetUniformLocation(fragMulti, "usefb");  glUniform1i(bleh, config.vectrail);
	bleh = glGetUniformLocation(fragMulti, "useglow"); glUniform1i(bleh, useglow);
	bleh = glGetUniformLocation(fragMulti, "glowamt"); glUniform1f(bleh, config.vecglow * .01);
	bleh = glGetUniformLocation(fragMulti, "brighten"); glUniform1i(bleh, gamenum);

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

	if (config.bezel && gamenum && art_loaded[3])
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
	glUseProgram(0);

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
	glBindTexture(GL_TEXTURE_2D, 0); // Don't disable GL_TEXTURE_2D on the main texture unit?

	//POST COMBINING OVERLAY FOR CINEMATRONICS GAMES WITH MONITOR COVERS & NO BACKGROUND ARTWORK

	if (driver[gamenum].rotation == 2 && config.overlay && art_loaded[1] && gamenum)
	{
		set_texture(&art_tex[1], 1, 0, 0, 0);
		glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);
		glColor4f(1.0f, 1.0f, 1.0f, .5f);
		if (config.bezel == 0)
		{
			if (config.widescreen && config.windowed == 0)
			{
				Special_Rect(0, 1024);
			}
			else
			{
				glEnable(GL_BLEND);
				FS_Rect(0, 1024);
			}
		}
		else {
			if (config.debug) { Any_Rect(0, msx, msy, esy, esx); }
			else
			{
				if (config.widescreen && config.windowed == 0)
				{
					Bezel_Rect(0, b1sx, b1sy, b2sy, b2sx);
				}
				else
				{
					Any_Rect(0, b1sx, b1sy, b2sy, b2sx);
				}
			}
		}
	}
	// HACKY WAY TO ADD BEZEL TO VERTICAL GAMES
	// This is for the tempest and tacscan vertical bezels.
	// This needs to be reinplemented as well.
	if (driver[gamenum].rotation == 1 && config.bezel == 0 && gamenum)
	{
		set_texture(&art_tex[2], 1, 0, 0, 1);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.99f);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		Centered_Rect(0, 1024); //Tempest
		glDisable(GL_ALPHA_TEST);
	}

	if (config.bezel && art_loaded[3] && gamenum) {
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
		Centered_Rect(0, 1024);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_ALPHA_TEST);
	}

	glLoadIdentity();

	video_loop();
	end_render_fbo4();

	glDisable(GL_TEXTURE_2D);

	auto end = chrono::steady_clock::now();
	auto diff = end - start;
	wrlog("Profiler: Render Time after final compositing %f ", chrono::duration <double, milli>(diff).count());
}

////////////////////////////////////////////////////////////////////////////////
// FINAL COMPOSITING AND RENDERING CODE ENDS HERE  ///////////////////////////
////////////////////////////////////////////////////////////////////////////////