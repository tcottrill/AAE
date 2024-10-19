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
#include "fonts.h"
#include "vector_fonts.h"
#include "../config.h"
#include "../sndhrdw/samples.h"
#include "../vidhrdwr/vector.h"
#include "../config.h"
#include "../acommon.h"
#include "../fileio/loaders.h"
//include "texture_loading_shim.h"

#define v1 1.5 //1.7 //1.0
#define v2 2.5 //2.7 //2.0
#define STRINGIFY(A)  #A
#include "../Shaders/Texturing.vs"
#include "../Shaders/MultiTexture.fs"
#include "../Shaders/blur.fs"
#include "../Shaders/vertex.vs"
#include "../Shaders/pass1frag.glsl"
#include "../Shaders/pass1vec.glsl"
#include "../Shaders/pass2frag.glsl"
#include "../Shaders/pass2vec.glsl"

#include "../Shaders/motion.fs"
#include "../Shaders/motion.vs"

#define BLSZ  512

GLuint error_tex[1];
GLuint pause_tex[2];
GLuint fun_tex[4];
GLuint art_tex[8];
GLuint game_tex[10];
GLuint menu_tex[7];
GLuint font_tex[2];
GLuint fbotex;

GLuint mytex[5];
GLuint blur_tex[2];

// FBO Handles
GLuint fbo1;
GLuint fbo2;
GLuint fbo3;

//Texture Handles. Set to default -1 for my fbo handler
GLuint img1a = -1;
GLuint img1b = -1;
GLuint img1c = -1;
GLuint img2a = -1;
GLuint img2b = -1;
GLuint img3a = -1;
GLuint img3b = -1;

//Shader Handles
GLuint fragBlur;
GLuint fragMulti;
GLuint Pass1shader;
GLuint Pass2shader;
GLuint MotionBlur;

//Shader/texturing Ortho sizes

float width = 1024.0;
float height = 1024.0;

const float width2 = 512.0;
const float height2 = 512.0;

const float width3 = 256.0;
const float height3 = 256.0;

//Error Checking Variables
int status;
static GLenum errCode;
const GLubyte* errString;
static int lastkey = 0;
int errorsound = 0;
int blending = 2;//11
int testblend = 0;
static float wideadj = 1;

void set_ortho(GLint width, GLint height)
{
	glMatrixMode(GL_PROJECTION);							// Select The Projection Matrix
	glLoadIdentity();										// Reset The Projection Matrix
	glViewport(0, 0, width - 1, height - 1);
	glOrtho(0, width - 1, 0, height - 1, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);								// Select The Modelview Matrix
	glLoadIdentity();
}

void set_texture(GLuint texture[], GLboolean linear, GLboolean mipmapping, GLboolean blending, GLboolean set_color)
{
	GLenum filter = linear ? GL_LINEAR : GL_NEAREST;
	glBindTexture(GL_TEXTURE_2D, *texture);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if (mipmapping)
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	else
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glEnable(GL_TEXTURE_2D);
	if (set_color) glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	if (blending)  glEnable(GL_BLEND);
	//glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	//glBlendFunc(GL_DST_COLOR, GL_ZERO);}
	//glBlendFunc(GL_ONE_MINUS_DST_COLOR , GL_ONE);
	//glBlendFunc(GL_ONE , GL_ONE);
}

void Widescreen_calc()
{
	//float val = SCREEN_W / SCREEN_H;//config.screenw / config.screenh; //Aspect ratio
	float val = 0;

	if (config.widescreen == 0) val = 1.3333;
	if (config.widescreen == 1) val = 1.77;
	if (config.widescreen == 2) val = 1.6;

	wideadj = 1.3333 / val;//val;  1.6
}

void Bezel_Rect(int facing, int xmin, int xmax, int ymin, int ymax)
{
	float x = 0;

	x = (1.00 - wideadj) / 2;
	glTranslatef(x * xmax, 0, 0);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 1); glVertex2f(xmin, ymin);
	glTexCoord2f(0, 0); glVertex2f(xmin, ymax);
	glTexCoord2f(1, 0); glVertex2f(xmax * wideadj, ymax); //x
	glTexCoord2f(1, 1); glVertex2f(xmax * wideadj, ymin); //x,size
	glEnd();
	glTranslatef(-(x * xmax), 0, 0);
}

void Any_Rect(int facing, int xmin, int xmax, int ymin, int ymax)
{
	glBegin(GL_QUADS);
	glTexCoord2f(0, 1); glVertex2f(xmin, ymin);
	glTexCoord2f(0, 0); glVertex2f(xmin, ymax);
	glTexCoord2f(1, 0); glVertex2f(xmax, ymax);
	glTexCoord2f(1, 1); glVertex2f(xmax, ymin);
	glEnd();
}

void FS_Rect(int facing, int size)
{
	glBegin(GL_QUADS);
	glTexCoord2f(0, 1); glVertex2f(0, size);
	glTexCoord2f(0, 0); glVertex2f(0, 0);
	glTexCoord2f(1, 0); glVertex2f(size, 0); //x
	glTexCoord2f(1, 1); glVertex2f(size, size); //x,size

	glEnd();
}
void Screen_Rect(int facing, int size)
{
	float x;
	if (config.widescreen && config.windowed == 0)
	{
		x = (1.00 - wideadj) / 2;
		glTranslatef(x * size, 0, 0);

		glBegin(GL_QUADS);
		glTexCoord2f(0, 1); glVertex2f(0, size);
		glTexCoord2f(0, 0); glVertex2f(0, 0);
		glTexCoord2f(1, 0); glVertex2f(size * wideadj, 0); //x
		glTexCoord2f(1, 1); glVertex2f(size * wideadj, size); //x,size
		glEnd();
		glTranslatef(x * size, 0, 0);
	}
	else
	{
		// glScalef(1.25f,1.25f,1);
		// glTranslatef(792,-210,0);
		//glRotatef(90,0,0,1);
		glBegin(GL_QUADS);
		glTexCoord2f(0, 1); glVertex2f(0, size);
		glTexCoord2f(0, 0); glVertex2f(0, 0);
		glTexCoord2f(1, 0); glVertex2f(size, 0); //x
		glTexCoord2f(1, 1); glVertex2f(size, size); //x,size
		glEnd();
	}
}

void Special_Rect(int facing, int size)
{
	float x;
	float sizeC = size * .75;

	x = (1.00 - wideadj);
	glTranslatef(-((x * size) / 2), 0, 0);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 1); glVertex2f(0, size);
	glTexCoord2f(0, 0); glVertex2f(0, 0);
	glTexCoord2f(1, 0); glVertex2f(size * wideadj, 0); //x
	glTexCoord2f(1, 1); glVertex2f(size * wideadj, size); //x,size
	glEnd();
	glTranslatef((x * size) / 2, 0, 0);
}

void Centered_Rect(int facing, int size)
{
	float x;
	float sizeC = size * .75;
	/*
	if (config.widescreen && config.windowed == 0)
	{
		x = (1.00 - wideadj) / 2;
		glTranslatef(x * size, 0, 0);
		glBegin(GL_QUADS);
		glTexCoord2f(0, 1); glVertex2f(0, sizeC);
		glTexCoord2f(0, 0); glVertex2f(0, 0);
		glTexCoord2f(1, 0); glVertex2f(size * wideadj, 0); //x
		glTexCoord2f(1, 1); glVertex2f(size * wideadj, sizeC); //x,size
		glEnd();
		glTranslatef(x * size, 0, 0);
	}
	else
	{
	*/
	glBegin(GL_QUADS);
	glTexCoord2f(0, 1); glVertex2f(0, sizeC);
	glTexCoord2f(0, 0); glVertex2f(0, 0);
	glTexCoord2f(1, 0); glVertex2f(size, 0);
	glTexCoord2f(1, 1); glVertex2f(size, sizeC);
	glEnd();
	//}
}

void Resize_Rect(int facing, int size)
{
	glBegin(GL_QUADS);
	glTexCoord2f(0, 1); glVertex2f(0, size * .75);
	glTexCoord2f(0, 0); glVertex2f(0, 0);
	glTexCoord2f(1, 0); glVertex2f(size, 0);
	glTexCoord2f(1, 1); glVertex2f(size, size * .75);
	glEnd();
}

int init_gl(void)
{
	GLint texSize;

	static int init_one = 0;

	allegro_gl_clear_settings();
	allegro_gl_set(AGL_COLOR_DEPTH, 32); //bpp
	allegro_gl_set(AGL_DOUBLEBUFFER, config.dblbuffer);//config.dblbuffer
	allegro_gl_set(AGL_Z_DEPTH, 24);
	allegro_gl_set(AGL_WINDOWED, config.windowed); //windowed
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
	//glDisable(GL_DEPTH_TEST);
	//glDepthFunc  ( GL_ALWAYS );
	//glEnable(GL_LINE_SMOOTH);
	//glEnable(GL_POINT_SMOOTH);
	//glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	//glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);		// Type Of Blending To Use
	// glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glLineWidth(config.linewidth);//linewidth
	glPointSize(config.pointsize);//pointsize

	Widescreen_calc();
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

	wglSwapIntervalEXT(1);

	//if (in_gui)  {KillFont();glDeleteTextures(1, &blur_tex[0]);}
	if (!init_one)
	{
		wrlog("Generating Phospher Texture");
		glGenTextures(1, &blur_tex[0]);

		wrlog("Initalizing FBO's");
		fbo_init();

		make_single_bitmap(&error_tex[0], "error.png", "aae.zip", 0); //This has to be loaded before any driver init.
		make_single_bitmap(&error_tex[1], "info.png", "aae.zip", 0);
		//make_single_bitmap( &menu_tex[0],"buttons.png", "aae.zip",0);
		make_single_bitmap(&menu_tex[0], "joystick.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[1], "spinner.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[2], "settings.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[3], "video.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[4], "sound.png", "aae.zip", 0);
		make_single_bitmap(&menu_tex[5], "gamma.png", "aae.zip", 0);
		make_single_bitmap(&font_tex[1], "font.png", "aae.zip", 0);
		wrlog("Building Font");
		BuildFont();
		font_init(); //Build the Vector Font
		init_shader();
		wrlog("Finished configuration of OpenGl sucessfully");
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		//glPrint_centered(384,"PLEASE WAIT,BOOTING",255,255,255,255,2,0,0);
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
	glDeleteTextures(1, &blur_tex[0]);
}

void texture_reinit()
{
	glEnable(GL_TEXTURE_2D);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo2);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo3);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	allegro_gl_flip();

	glDeleteTextures(1, &art_tex[0]);
	glDeleteTextures(1, &art_tex[1]);
	glDeleteTextures(1, &art_tex[2]);
	glDeleteTextures(1, &art_tex[3]);
	glDeleteTextures(1, &game_tex[0]);
}

void resize_art_textures()
{
	set_render();
	if (config.artwork) {
		glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
		glReadBuffer(GL_COLOR_ATTACHMENT2_EXT);
		glClear(GL_COLOR_BUFFER_BIT);
		glEnable(GL_TEXTURE_2D);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		glBindTexture(GL_TEXTURE_2D, art_tex[0]);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glDisable(GL_BLEND);
		// Norm_Rect(0,1024);
		 //Centered_Rect(0,1024);
		Resize_Rect(0, 1024);
		glBindTexture(GL_TEXTURE_2D, 0);
		glDeleteTextures(1, &art_tex[0]);
		glGenTextures(1, &art_tex[0]);
		glBindTexture(GL_TEXTURE_2D, art_tex[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); //nearest!!
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, 1024, 1024, 0); //RGBA8?
		glEnable(GL_BLEND);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_TEXTURE_2D);
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
	}
	if (config.overlay) {
		glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
		glReadBuffer(GL_COLOR_ATTACHMENT2_EXT);
		glClear(GL_COLOR_BUFFER_BIT);
		glEnable(GL_TEXTURE_2D);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		glBindTexture(GL_TEXTURE_2D, art_tex[1]);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glDisable(GL_BLEND);
		//Norm_Rect(0,1024);
		//Centered_Rect(0,1024);
		Resize_Rect(0, 1024);
		glBindTexture(GL_TEXTURE_2D, 0);
		glDeleteTextures(1, &art_tex[1]);
		glGenTextures(1, &art_tex[1]);
		glBindTexture(GL_TEXTURE_2D, art_tex[1]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); //nearest!!
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, 1024, 1024, 0); //RGBA8?
		glEnable(GL_BLEND);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_TEXTURE_2D);
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
	}
	complete_render();
}

void draw_center_tex(GLuint texture[], int size, int x, int y, int rotation, int facing, int r, int g, int b, int alpha, int blend)
{
	glPushMatrix();
	//glPushAttrib(GL_COLOR_BUFFER_BIT);
	//glLoadIdentity();
	glColor4ub(r, g, b, alpha);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	switch (blend)
	{
	case 1:glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
	case 2:glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
	case 3:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA); break;
	case 4: {
		glDisable(GL_BLEND);
		//glEnable(GL_ALPHA_TEST);
		//glAlphaFunc(GL_GREATER, 0.5);
	}
	}
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, *texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	glTranslated(x, y, 0);
	glRotatef(rotation, 0.0, 0.0, 1.0); //-90

	switch (facing)
	{
	case 1: {//normal
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex2f(-size, -size);
		glTexCoord2f(0, 1); glVertex2f(-size, size);
		glTexCoord2f(1, 1); glVertex2f(size, size);
		glTexCoord2f(1, 0); glVertex2f(size, -size);
		glEnd();
		break;
	}
	case 2: {//upside down
		glBegin(GL_QUADS);
		glTexCoord2d(1, 1); glVertex2f(-size, -size);
		glTexCoord2d(1, 0); glVertex2f(-size, size);
		glTexCoord2d(0, 0); glVertex2f(size, size);
		glTexCoord2d(0, 1); glVertex2f(size, -size);
		glEnd();
		break;
	}
	case 3: {//right
		glBegin(GL_QUADS);
		glTexCoord2f(0, 1); glVertex2f(size, size);
		glTexCoord2f(0, 0); glVertex2f(-size, size);
		glTexCoord2f(1, 0); glVertex2f(-size, -size);
		glTexCoord2f(1, 1); glVertex2f(size, -size);
		glEnd();
		break;
	}
	case 4: { //left
		glBegin(GL_QUADS);
		glTexCoord2f(0, 1); glVertex2f(-size, -size);
		glTexCoord2f(0, 0); glVertex2f(size, -size);
		glTexCoord2f(1, 0); glVertex2f(size, size);
		glTexCoord2f(1, 1); glVertex2f(-size, size);
		glEnd();
		break;
	}
	}
	switch (blend)
	{
	case 1:glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
	case 2:glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
	case 3:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA); break;
	case 4: {
		glEnable(GL_BLEND);
		//glDisable(GL_ALPHA_TEST);
	   // glAlphaFunc(GL_GREATER, 0.5);
	}
	}
	glPopMatrix();
	//glPopAttrib();
	//glDisable(GL_TEXTURE_2D);
}

void set_blending2()
{
	float k = 1.0 / 10.0;//12
	//glColor4f(1.0f,1.0f,1.0f,1.0f);
	glColor4f(k, k, k, k);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	/*
	switch (blending)
	{
	case 0:glBlendFunc(GL_DST_COLOR, GL_ZERO);break;
	case 1:glBlendFunc(GL_SRC_COLOR, GL_ONE);break;
	case 2:glBlendFunc(GL_SRC_ALPHA, GL_ONE);break;
	case 3:glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);break;
	case 4:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);break;
	case 5:glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);break;
	case 6:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);break;
	case 7:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);break;
	case 8:glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);break;
	case 9:glBlendFunc(GL_SRC_ALPHA, GL_DST_COLOR);break;
	case 10:glBlendFunc(GL_ONE, GL_ONE);break;
	case 11:glBlendFunc(GL_ONE, GL_ZERO);break;
	case 12:glBlendFunc(GL_ZERO, GL_ONE);break;
	case 13:glBlendFunc (GL_ONE, GL_DST_COLOR);break;
	case 14:glBlendFunc (GL_SRC_ALPHA, GL_DST_COLOR);break;
	case 15:glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);break;
	}
	*/
}
void set_blending(int blendval)
{
	if (blendval == 0) { glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
	else if (blendval == 1) { glBlendFunc(GL_DST_COLOR, GL_ZERO); }
	else if (blendval == 2) { glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE); }
}

void blit_a_tex(GLuint texture[], int blending, int x, int y, int alpha, float scale)
{
	glLoadIdentity();
	glPushAttrib(GL_COLOR_BUFFER_BIT);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	// glBlendFunc(GL_SRC_ALPHA, GL_ONE); //PROPER
	glBindTexture(GL_TEXTURE_2D, *texture);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	//glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	//glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	glTranslated(-130, 0, 0);
	//glColor4ub(255, 255, 255, 255);  //Reset Blending Mode. alpha
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glBegin(GL_QUADS);
	glTexCoord2i(0, 0); glVertex2f(0, 0);
	glTexCoord2i(0, 1); glVertex2f(0, (100 * scale));
	glTexCoord2i(1, 1); glVertex2f((245 * scale), (100 * scale));
	glTexCoord2i(1, 0); glVertex2f((245 * scale), 0);
	glEnd();

	glLoadIdentity();
	glPopAttrib();
}

// Draw this bitmap directly to the screen. Its width will be the width of the bitmap in pixels.
void draw2D(float x, float y)
{
	// unpack in bytes, not words
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	// go to (0,0)...
	glRasterPos2f(0, 0);
	// ...then, in screen (not world) coordinates, move to (x,y)
	glBitmap(0, 0, 0, 0, x, y, NULL);
	//glDrawPixels(1024,1024, GL_RGB, GL_UNSIGNED_BYTE, myBitmap);
}

void menu_textureit(GLuint texture[], int x, int y, int xsize, int ysize)
{
	glEnable(GL_TEXTURE_2D);
	//allegro_gl_use_mipmapping(FALSE);
	glDisable(GL_BLEND);
	//glLoadIdentity();
	glColor3f(1, 1, 1);
	glBindTexture(GL_TEXTURE_2D, *texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	//glBlendFunc(GL_ZERO, GL_ONE);

	glBegin(GL_POLYGON);
	glTexCoord2f(0, 1); glVertex2f(x + xsize, y + ysize);
	glTexCoord2f(0, 0); glVertex2f(x - xsize, y + ysize);
	glTexCoord2f(1, 0); glVertex2f(x - xsize, y - ysize);
	glTexCoord2f(1, 1); glVertex2f(x + xsize, y - ysize);
	glEnd();
	glEnable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void blit_any_tex(GLuint texture[], int blending, float alpha, int x, int y, int w, int h)
{
	glPushMatrix();
	glLoadIdentity();
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBindTexture(GL_TEXTURE_2D, *texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);	// Linear Filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);	// Linear Filtering

	//glHint(GL_POLYGON_SMOOTH_HINT, GL_DONT_CARE);

	//if (blending==1) glBlendFunc(GL_DST_COLOR, GL_ZERO);
	//if (blending==0) glBlendFunc(GL_SRC_COLOR, GL_ONE); //PROPER
	//if (blending==2)glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//if (blending==4)glBlendFunc(GL_ONE, GL_ONE);

	switch (blending)
	{
	case 0:glBlendFunc(GL_DST_COLOR, GL_ZERO); break;
	case 1:glBlendFunc(GL_SRC_COLOR, GL_ONE); break;
	case 2:glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
	case 3:glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
	case 4:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE); break;
	case 5:glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR); break;
	case 6:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA); break;
	case 7:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO); break;
	case 8:glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR); break;
	case 9:glBlendFunc(GL_SRC_ALPHA, GL_DST_COLOR); break;
	case 10:glBlendFunc(GL_ONE, GL_ONE); break;
	case 11:glBlendFunc(GL_ONE, GL_ZERO); break;
	case 12:glBlendFunc(GL_ZERO, GL_ONE); break;
	case 13:glBlendFunc(GL_ONE, GL_DST_COLOR); break;
	case 14:glBlendFunc(GL_SRC_ALPHA, GL_DST_COLOR); break;
	case 15:glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR); break;
		//case 16: glDisable(GL_BLEND);break;
	}

	glColor4f(1.0f, 1.0f, 1.0f, alpha);
	//glScalef(1.1,1.1,0 );
	glTranslated(x, y, 0);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 1); glVertex2i(0, h);
	glTexCoord2f(0, 0); glVertex2i(0, 0);
	glTexCoord2f(1, 0); glVertex2i(w, 0);
	glTexCoord2f(1, 1); glVertex2i(w, h);
	glEnd();
	glDisable(GL_TEXTURE_2D);
	glPopMatrix();
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void show_error(void)
{
	static int fade = 255;
	static int dir = 0;

	if (have_error) {
		if (!errorsound) { sample_start(5, num_samples - 4, 0); errorsound = 1; }
		glPushMatrix();
		glLoadIdentity();
		glDisable(GL_TEXTURE_2D);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glColor4ub(40, 0, 0, 220);
		glBegin(GL_QUADS);					// Start Drawing Quads
		glTexCoord2f(0, 0); glVertex3d(282, 234, 0);
		glTexCoord2f(0, 1); glVertex3d(282, 534, 0);
		glTexCoord2f(1, 1); glVertex3d(742, 534, 0);
		glTexCoord2f(1, 0); glVertex3d(742, 234, 0);
		glEnd();

		glEnable(GL_TEXTURE_2D);
		glColor4ub(255, 255, 255, 255);

		glBindTexture(GL_TEXTURE_2D, error_tex[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE); //PROPER
		glTranslatef(375, 475, 0);

		glBegin(GL_QUADS);
		glTexCoord2d(0, 0); glVertex2d(-24, -24); //24
		glTexCoord2d(0, 1); glVertex2d(-24, 24);
		glTexCoord2d(1, 1); glVertex2d(24, 24);
		glTexCoord2d(1, 0); glVertex2d(24, -24);
		glEnd();
		glLoadIdentity();
		glPrint(418, 457, 255, 255, 255, 255, 1.1, 0, 0, "An Error Occurred!!");
		switch (have_error)
		{
		case 2: glPrint(380, 390, 32, 178, 170, 255, 1.3, 0, 0, "Emu EXE Not Found"); break;
		case 3: glPrint(320, 400, 32, 178, 170, 255, 1.1, 0, 0, "A Config Value is Set Wrong"); break;
		case 10: { glPrint(418, 417, 32, 178, 170, 255, 1.1, 0, 0, "REQUIRED ROMS");
			glPrint(418, 380, 32, 178, 170, 255, 1.1, 0, 0, "NOT FOUND!!"); break;	}
		case 15: glPrint(418, 417, 32, 178, 170, 255, 1.1, 0, 0, "Texture not found"); break;
		case 20: glPrint(330, 405, 32, 178, 170, 255, 1.2, 0, 0, "Sorry, not playable (YET)!"); break;
		}
		glPrint(385, 350, 255, 255, 255, 255, 1.1, 0, 0, "Please see AAE.LOG");
		glPrint(385, 320, 255, 255, 255, 255, 1.1, 0, 0, "for further details.");
		glPrint(365, 235, fade, 255, 255, fade, 1.1, 0, 0, "[Press Exit to Close]");
		if (dir == 0) {
			fade -= 5;
			if (fade < 40) { dir = 1; }
		}
		if (dir == 1) {
			fade += 5;
			if (fade > 255) { fade = 255; dir = 0; }
		}
		glPopMatrix();
		glLoadIdentity();
		glDisable(GL_TEXTURE_2D);
	}
}

void fadeit(void)
{  //THIS NEEDS TO BE FIXED BEFORE USE
	static int fader = 255;

	if (fader > 1) {
		glDisable(GL_TEXTURE_2D);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glColor4ub(0, 0, 0, fader);
		glBegin(GL_QUADS);					// Start Drawing Quads
		glTexCoord2f(0, 0); glVertex3d(0, 0, 0);
		glTexCoord2f(0, 1); glVertex3d(1024, 0, 0);
		glTexCoord2f(1, 1); glVertex3d(1024, 768, 0);
		glTexCoord2f(1, 0); glVertex3d(0, 768, 0);
		glEnd();
		fader -= 10;

		glEnable(GL_TEXTURE_2D);
	}
}

void load_artwork(const struct artworks p[])
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
	//Screen burn layer 4: Yea I know, but...

	for (i = 0; p[i].filename != NULL; i++)
	{
		goodload = 0;
		switch (p[i].type)
		{
		case FUN_TEX:  goodload = make_single_bitmap(&fun_tex[p[i].target], p[i].filename, p[i].zipfile, 0); break;
		case ART_TEX: {
			if (p[i].target > 1) { type = 1; }
			goodload = make_single_bitmap(&art_tex[p[i].target], p[i].filename, p[i].zipfile, type);
			switch (p[i].target)
			{
			case 0: if (goodload) art_loaded[0] = 1; break;//if (artwork) config.artwork=1;break;
			case 1: if (goodload) art_loaded[1] = 1; break;//if (overlay) config.overlay=1;break;
			case 3: if (goodload) art_loaded[3] = 1; break;//if (bezel) config.bezel=1;break;
			}
		}break;
		case GAME_TEX: goodload = make_single_bitmap(&game_tex[p[i].target], p[i].filename, p[i].zipfile, 0); break;
		default: wrlog("You have defines something wrong in the artwork loading!!!!"); break;
		}
		if (goodload == 0) { wrlog("A requested artwork file was not found!"); have_error = 15; }
	}
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

void free_game_textures()
{
	int i = 0;

	glDeleteTextures(1, &game_tex[0]);
	glDeleteTextures(1, &art_tex[0]);
	glDeleteTextures(1, &art_tex[1]);
	glDeleteTextures(1, &art_tex[3]);

	//for (i=0;i<9;i++){if (art_tex[i]) {wrlog("Releasing Texture Number = %d",i); glDeleteTextures(1, &art_tex[i]);art_tex[i]=0;rest(50);}}

		//for (i=0;i<4;i++) {wrlog("Releasing Texture Number = %d",i); glDeleteTextures(1, &art_tex[i]);}
	//if (glIsTexture(game_tex[0])) {wrlog("Releasing Game Tex 0"); glDeleteTextures(1, &game_tex[0]);game_tex[0]=0;rest(50);}
	//if (glIsTexture(game_tex[1])) {wrlog("Releasing Game Tex 1"); glDeleteTextures(1, &game_tex[1]);game_tex[1]=0;rest(50);}
	//if (glIsTexture(game_tex[2])){wrlog("Releasing Game Tex 2"); glDeleteTextures(1, &game_tex[2]);game_tex[2]=0;rest(50);}
	//glDeleteTextures(1, &game_tex[0]);//glDeleteTextures(1, &game_tex[1]);glDeleteTextures(1, &game_tex[2]);
}

void draw_a_quad(int tleft, int bleft, int tright, int bright, int r, int g, int b, int alpha, int blend)
{
	// glPushMatrix();
	//glLoadIdentity();
  // glDisable(GL_TEXTURE_2D);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glColor4ub(r, g, b, alpha);
	glBegin(GL_QUADS);					// Start Drawing Quads
	glVertex3f(tleft, tright, 0.0f);			// Left And Up 1 Unit (Top Left)(left)
	glVertex3f(bleft, tright, 0.0f);			// Right And Up 1 Unit (Top Right)(top)
	glVertex3f(bleft, bright, 0.0f);			// Right And Down One Unit (Bottom Right)(right)
	glVertex3f(tleft, bright, 0.0f);			// Left And Down One Unit (Bottom Left)(bottom)
	glEnd();
	//glEnable(GL_TEXTURE_2D); //Reenable Texturing
		//glPopMatrix();
}

void RendertoTarget() //Downsample part 1
{
	GLint loc = 0;

	//glEnable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	set_texture(&img1b, 1, 0, 0, 0);

	glActiveTexture(GL_TEXTURE0);
	glUseProgram(fragBlur);

	loc = glGetUniformLocation(fragBlur, "colormap"); glUniform1i(loc, 0);
	loc = glGetUniformLocation(fragBlur, "width"); glUniform1f(loc, 512.0);
	loc = glGetUniformLocation(fragBlur, "height"); glUniform1f(loc, 512.0);

	FS_Rect(0, height2);
	glUseProgram(0);
}

void RendertoTarget2() //Downsample part 2
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
	loc = glGetUniformLocation(fragBlur, "width"); glUniform1f(loc, 256.0);//206
	loc = glGetUniformLocation(fragBlur, "height"); glUniform1f(loc, 256.0);

	glDisable(GL_BLEND);
	set_texture(&img2a, 1, 0, 0, 1);
	FS_Rect(0, height3);
	glUseProgram(0);
}

void ping_pong()
{
	GLint loc = 0;
	int i, x, w;
	i = 0; w = 0;

	loc = glGetUniformLocation(fragBlur, "colormap"); glUniform1i(loc, 0);
	set_blending2();

	for (x = 0; x < 3; x++)
	{
		w ^= 1;
		//if (w) {glUseProgram(Pass2shader);}
		//else {glUseProgram(Pass1shader);}

		//First Texture, two pass blended
		loc = glGetUniformLocation(Pass2shader, "colormap");//RTScene

		glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
		set_texture(&img3a, 1, 0, 1, 0);

		glUseProgram(Pass1shader);
		FS_Rect(0, height3);
		glUseProgram(Pass2shader);
		FS_Rect(0, height3);

		//Second Texture, two pass blended
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		set_texture(&img3b, 1, 0, 1, 0);
		glBindTexture(GL_TEXTURE_2D, img3b);

		glUseProgram(Pass1shader);
		FS_Rect(0, height3);
		glUseProgram(Pass2shader);
		FS_Rect(0, height3);
	}
	glUseProgram(0);
}

void old_ping_pong()
{
	GLint loc = 0;
	int x;
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

	i = 0;

	glUseProgram(0);

	glUseProgram(fragBlur);

	loc = glGetUniformLocation(fragBlur, "colormap");
	glUniform1i(loc, 0);
	loc = glGetUniformLocation(fragBlur, "width");
	glUniform1f(loc, 256.0);//225
	loc = glGetUniformLocation(fragBlur, "height");
	glUniform1f(loc, 256.0);//225

	for (x = 4; x < 8; x++) //4 to 8
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
	//////////////////////////

	//glDisable(GL_TEXTURE_2D);
	//glDisable(GL_BLEND);
	//glBindTexture(GL_TEXTURE_2D, 0);
}

void WriteShaderError(GLhandleARB obj, const char* shaderName)
{
	int errorLength = 0;
	int written = 0;
	char* message;

	glGetObjectParameterivARB(obj, GL_OBJECT_INFO_LOG_LENGTH_ARB, &errorLength);
	if (errorLength > 0)
	{
		message = (char*)malloc(errorLength);
		glGetInfoLogARB(obj, errorLength, &written, message);
		wrlog("Shader compile error in %s: %s\n", shaderName, message);
		free(message);
	}
}

void GLCheckError(const char* call)
{
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

int CHECK_FRAMEBUFFER_STATUS()
{
	GLenum status;
	status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
	// wrlog("%x\n", status);
	switch (status) {
	case GL_FRAMEBUFFER_COMPLETE_EXT:
		wrlog("Framebuffer Complete! A-OK");   break;
	case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
		wrlog("framebuffer GL_FRAMEBUFFER_UNSUPPORTED_EXT\n");
		/* you gotta choose different formats */
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
		wrlog("framebuffer INCOMPLETE_ATTACHMENT\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
		wrlog("framebuffer FRAMEBUFFER_MISSING_ATTACHMENT\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
		wrlog("framebuffer FRAMEBUFFER_DIMENSIONS\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DUPLICATE_ATTACHMENT_EXT:
		wrlog("framebuffer INCOMPLETE_DUPLICATE_ATTACHMENT\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
		wrlog("framebuffer INCOMPLETE_FORMATS\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
		wrlog("framebuffer INCOMPLETE_DRAW_BUFFER\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
		wrlog("framebuffer INCOMPLETE_READ_BUFFER\n");
		break;
	case GL_FRAMEBUFFER_BINDING_EXT:
		wrlog("framebuffer BINDING_EXT\n");
		break;
	}
	return status;
}

int init_fbo(int mipmap, int fasa, GLuint tex1) {
	GLuint fbo = -1;
	GLuint texture;

	glGenFramebuffersEXT(1, &fbo);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
	//glGenerateMipmapEXT(GL_TEXTURE_2D);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, texture, 0);
	CHECK_FRAMEBUFFER_STATUS(); //Check Framebuffer Status
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);	// Unbind the FBO for now

	tex1 = texture;
	return fbo;
}

void fbo_init()
{
	glGenFramebuffersEXT(1, &fbo1);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);

	glGenTextures(1, &img1a); glBindTexture(GL_TEXTURE_2D, img1a);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
	//glGenerateMipmapEXT(GL_TEXTURE_2D);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// And attach it to the FBO so we can render to it
	glGenerateMipmapEXT(GL_TEXTURE_2D);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, img1a, 0);

	//Gen Texture 2
	glGenTextures(1, &img1b); glBindTexture(GL_TEXTURE_2D, img1b);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);

	// glGenerateMipmapEXT(GL_TEXTURE_2D);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_2D, img1b, 0);

	//Gen Texture 3
	glGenTextures(1, &img1c); glBindTexture(GL_TEXTURE_2D, img1c);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT2_EXT, GL_TEXTURE_2D, img1c, 0);

	CHECK_FRAMEBUFFER_STATUS(); //Check Framebuffer Status
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);	// Unbind the FBO for now
	//////////////////////////////////////////////////////////////////////////////////////////////////

		// Setup our FBO 2
	glGenFramebuffersEXT(1, &fbo2);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo2);

	glGenTextures(1, &img2a); glBindTexture(GL_TEXTURE_2D, img2a);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width2, height2, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);

	// glGenerateMipmapEXT(GL_TEXTURE_2D);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, img2a, 0);

	//Gen Texture 2
	glGenTextures(1, &img2b); glBindTexture(GL_TEXTURE_2D, img2b);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width2, height2, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
	//glGenerateMipmapEXT(GL_TEXTURE_2D);
	//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	//And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_2D, img2b, 0);

	CHECK_FRAMEBUFFER_STATUS(); //Check Framebuffer Status
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);	// Unbind the FBO for now

	//////////////////////////////////////////////////////////////////////////////////////////////////////

		// Setup our FBO 3
	glGenFramebuffersEXT(1, &fbo3);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo3);

	glGenTextures(1, &img3a); glBindTexture(GL_TEXTURE_2D, img3a);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width3, height3, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);

	//glGenerateMipmapEXT(GL_TEXTURE_2D);
	 //glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	 //glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, img3a, 0);

	//Gen Texture 2
	glGenTextures(1, &img3b); glBindTexture(GL_TEXTURE_2D, img3b);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width3, height3, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
	//glGenerateMipmapEXT(GL_TEXTURE_2D);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_2D, img3b, 0);
	CHECK_FRAMEBUFFER_STATUS(); //Check Framebuffer Status
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);	// Unbind the FBO for now
}

/******************** Shaders Function *******************************/
int setupShaders(const char* vert, const char* frag) {
	GLuint v = 0;
	GLuint f = 0;
	GLuint p = -1;
	int param;
	GLint linked;

	v = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(v, 1, (const GLchar**)&vert, 0);
	glCompileShader(v);
	glGetShaderiv(v, GL_COMPILE_STATUS, &param);
	if (!param)
	{
		wrlog("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!vec 3");
		WriteShaderError(v, "stuff/multitex_vertex.vs");
	}

	f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(f, 1, (const GLchar**)&frag, 0);
	glCompileShader(f);
	if (!param)
	{
		wrlog("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!frag 3");
		WriteShaderError(f, "stuff/multitex_vertex.vs");
	}
	p = glCreateProgram();
	glAttachShader(p, f);
	glAttachShader(p, v);
	glLinkProgram(p);

	glGetProgramiv(p, GL_LINK_STATUS, &linked);
	if (linked)
	{
		wrlog("Program linked ok");
	}

	return p;
}

//int init_shader_one

int init_shader()
{
	//SHADER SETUP

	//int param;
	//int test;

	wrlog("Shader Init Start");

	Pass1shader = setupShaders(texvertText, pass1fragText);
	Pass2shader = setupShaders(texvertText, pass2fragText);
	fragBlur = setupShaders(vertText, fragText);
	fragMulti = setupShaders(texvertText, texfragText);
	MotionBlur = setupShaders(motionvs, motionfs);

	return 1;
}

void set_render()
{
	//glPushAttrib(GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT);
	// First we bind the FBO so we can render to it
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

	set_ortho(width - 1, height - 1);
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
	glDisable(GL_DITHER); //FIX!!!!!!!!!!!!!!
}

void set_fbo_render(int width, int height)
{
	glLoadIdentity();
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo2);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	// Save the view port and set it to the size of the texture
	//glPushAttrib(GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT);
	set_ortho(width, height);
	//allegro_gl_use_mipmapping(TRUE);
}

void complete_render()
{
	// Restore old view port and set rendering back to default frame buffer
	//glPopAttrib();
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);
	//glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);	// Clear Screen And Depth Buffer

	set_ortho(1024, 768);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//glDisable(GL_DEPTH_TEST);
}

void complete_fbo_render()
{
	//glPopAttrib();
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);
	set_ortho(1024, 768);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glDisable(GL_DITHER);
	//glEnable(GL_MULTISAMPLE_ARB);
}

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

void final_render(int xmin, int xmax, int ymin, int ymax, int shiftx, int shifty)
{
	GLint bleh = 0;
	GLfloat bval = 0.6f; //Art Blend variable
	GLfloat glowamt = 0.0; //Glow Shader variable
	int useglow = 0;
	GLint trail = 0;    //feedback texture enable;

	glEnable(GL_TEXTURE_2D);
	/////////////////////////////	//SWITCH TO FBO1/TEX 2 HERE FOR FINAL COMPOSITING!!!!//////////////////////////////
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f); //THIS IS FOR THE VARIOUS MONITOR TYPES???
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	set_texture(&img1a, 1, 0, 0, 1);

	glTranslated(shiftx, shifty, 0);
	//glTranslatef(.05,.0125,0);
	//glEnable(GL_MULTISAMPLE_ARB);
	glTranslatef(.25, .25, 0);
	glBlendFunc(GL_ONE, GL_ONE);
	Any_Rect(0, xmin, xmax, ymin, ymax);

	if (gamenum)
	{
		////DRAW OVERLAY TYPE 1
		if (driver[gamenum].rotation < 2 && config.overlay && art_loaded[1]) {
			set_texture(&art_tex[1], 1, 0, 0, 0);
			//Normal Overlay
			glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR); //GOOD

			glColor4f(.7f, .7f, .7f, 1.0f);
			glColor4f(1, 1, 1, 1);
			FS_Rect(0, 1024);
		}
	}

	//// //Render to feedback texture///////////////////////////////////////////////////////////////////////////////////
	if (config.vectrail && gamenum) {
		// glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
		glDrawBuffer(GL_COLOR_ATTACHMENT2_EXT);
		glDisable(GL_DITHER);
		set_texture(&img1b, 1, 0, 1, 0);
		//glEnable(GL_BLEND);
		//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);

		switch (config.vectrail)
		{
		case 1:  glColor4f(1.0f, 1.0f, 1.0f, .825f); break;//glColor4f(.75,.75,.75,.925); break; // glColor4f(.85,.85,.85,.80);
		case 2:  glColor4f(1.0f, 1.0f, 1.0f, .86f); break;//glColor4f(.70,.70,.70,.86);break;
		case 3:  glColor4f(1.0f, 1.0f, 1.0f, .93f); break;//glColor4f(.7f,.7f,.7f,.93); break;
		default:  glColor4f(.95f, .95f, .95f, 1.0f); break;
		}
		//glColor4f(1.0f,1.0f,1.0f,.925f);
		FS_Rect(0, 1024);
		// glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);
		// glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	}

	////////////////RENDER blur texture
	if (config.vecglow && gamenum) {
		set_fbo_render(512, 512);
		RendertoTarget();
		RendertoTarget2();
		//ping_pong();
		old_ping_pong();
	}

	complete_fbo_render();
	if (!paused) glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	//Render combined texture
	if (config.vecglow && gamenum) { useglow = 1; }
	glUseProgram(fragMulti);

	bleh = glGetUniformLocation(fragMulti, "mytex1"); glUniform1i(bleh, 0);
	bleh = glGetUniformLocation(fragMulti, "mytex2"); glUniform1i(bleh, 1);
	bleh = glGetUniformLocation(fragMulti, "mytex3"); glUniform1i(bleh, 2);
	bleh = glGetUniformLocation(fragMulti, "mytex4"); glUniform1i(bleh, 3);

	bleh = glGetUniformLocation(fragMulti, "useart"); if (gamenum && art_loaded[0]) {
		glUniform1i(bleh, config.artwork);
	}
	else { glUniform1i(bleh, 0); }
	bleh = glGetUniformLocation(fragMulti, "usefb");  glUniform1i(bleh, config.vectrail);
	bleh = glGetUniformLocation(fragMulti, "useglow"); glUniform1i(bleh, useglow);
	bleh = glGetUniformLocation(fragMulti, "glowamt"); glUniform1f(bleh, config.vecglow * .01);
	bleh = glGetUniformLocation(fragMulti, "brighten"); glUniform1i(bleh, gamenum);

	//Activate all 4 texture units
	glActiveTexture(GL_TEXTURE0);
	set_texture(&art_tex[0], 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE1);
	set_texture(&img1b, 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE2);
	//glBindTexture(GL_TEXTURE_2D, img3a );
	set_texture(&img3b, 1, 0, 0, 0);
	glActiveTexture(GL_TEXTURE3);
	set_texture(&img1c, 1, 0, 0, 0);

	//FINAL RENDERING TO SCREEN IS RIGHT HERE

	if (config.bezel && gamenum && art_loaded[3])
	{
		glColor4f(1.0, 1.0, 1.0, 1.0);
		if (config.debug)
		{
			Any_Rect(0, msx, msy, esy, esx);
		}
		else
		{
			if (config.widescreen && config.windowed == 0)
			{
				Bezel_Rect(0, b1sx, b1sy, b2sy, b2sx);
			}
			else {
				Any_Rect(0, b1sx, b1sy, b2sy, b2sx);
			}
		}
	}
	else {
		glColor4f(1.0, 1.0, 1.0, 1.0);
		Screen_Rect(0, 1024);
	}
	////////////////////////////////////////////////////////////////////////////
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
	glBindTexture(GL_TEXTURE_2D, 0);

	//POST COMBINING OVERLAY FOR CINEMATRONICS GAMES WITH MONITOR COVERS & NO BACKGROUND ARTWORK
	if (driver[gamenum].rotation == 2 && config.overlay && art_loaded[1] && gamenum) {
		set_texture(&art_tex[1], 1, 0, 0, 0);
		glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);
		glColor4f(1.0f, 1.0f, 1.0f, .5f);
		if (config.bezel == 0) {
			if (config.widescreen && config.windowed == 0)
			{
				Special_Rect(0, 1024);
			}
			else {
				FS_Rect(0, 1024);
			}
		}
		else {
			if (config.debug) { Any_Rect(0, msx, msy, esy, esx); }
			else {
				if (config.widescreen && config.windowed == 0)
				{
					Bezel_Rect(0, b1sx, b1sy, b2sy, b2sx);
				}
				else { Any_Rect(0, b1sx, b1sy, b2sy, b2sx); }
			}
		}
	}
	//HACKY WAY TO ADD BEZEL TO VERTICAL GAMES

	if (driver[gamenum].rotation == 1 && config.bezel == 0 && gamenum)
	{
		set_texture(&art_tex[2], 1, 0, 0, 1);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.99f);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glLoadIdentity();
		Centered_Rect(0, 1024);
		glDisable(GL_ALPHA_TEST);
	}

	if (config.bezel && art_loaded[3] && gamenum) {
		if (config.artcrop) {
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
	glDisable(GL_TEXTURE_2D);
}

void pause_loop()
{
	complete_fbo_render();

	glColor4f(.5f, .5f, .5f, .5f);
	glDisable(GL_BLEND);
	glPrint(440, 50, 255, 128, 128, 255, 1.3, 0, 0, "PAUSED");
}

/*

void myblending()
{
switch (testblend)
{
case 0:glBlendFunc(GL_DST_COLOR, GL_ZERO);break;
case 1:glBlendFunc(GL_SRC_COLOR, GL_ONE);break;
case 2:glBlendFunc(GL_SRC_ALPHA, GL_ONE);break;
case 3:glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);break;
case 4:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);break;
case 5:glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);break;
case 6:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);break;
case 7:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);break;
case 8:glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);break;
case 9:glBlendFunc(GL_SRC_ALPHA, GL_DST_COLOR);break;
case 10:glBlendFunc(GL_ONE, GL_ONE);break;
case 11:glBlendFunc(GL_ONE, GL_ZERO);break;
case 12:glBlendFunc(GL_ZERO, GL_ONE);break;
case 13:glBlendFunc (GL_ONE, GL_DST_COLOR);break;
case 14:glBlendFunc (GL_SRC_ALPHA, GL_DST_COLOR);break;
case 15:glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);break;
case 16:glBlendFunc(GL_ONE, GL_SRC_COLOR);break;
case 17:glBlendFunc(GL_ONE, GL_SRC_ALPHA);break;
case 18:glBlendFunc(GL_SRC_COLOR, GL_DST_COLOR);break;
}
}

void blit_sized_texub(GLuint texture[],int blending,int clevel,int alpha,int xmin,int xmax,int ymin,int ymax,int shiftx,int shifty)
{
glPushMatrix();
glPushAttrib(GL_COLOR_BUFFER_BIT);

glLoadIdentity();
glBindTexture(GL_TEXTURE_2D, *texture);
glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);	// Linear Filtering
glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);	// Linear Filtering
glEnable(GL_TEXTURE_2D);

switch (blending)
{
case 0:glBlendFunc(GL_DST_COLOR, GL_ZERO);break;
case 1:glBlendFunc(GL_SRC_COLOR, GL_ONE);break;
case 2:glBlendFunc(GL_SRC_ALPHA, GL_ONE);break;
case 3:glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);break;
case 4:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);break;
case 5:glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);break;
case 6:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);break;
case 7:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);break;
case 8:glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);break;
case 9:glBlendFunc(GL_SRC_ALPHA, GL_DST_COLOR);break;
case 10:glBlendFunc(GL_ONE, GL_ONE);break;
case 11:glBlendFunc(GL_ONE, GL_ZERO);break;
case 12:glBlendFunc(GL_ZERO, GL_ONE);break;
case 13:glBlendFunc (GL_ONE, GL_DST_COLOR);break;
case 14:glBlendFunc (GL_SRC_ALPHA, GL_DST_COLOR);break;
case 15:glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);break;
case 16: glDisable(GL_BLEND);break;
}

glColor4ub(clevel,clevel,clevel,alpha);
glTranslated(shiftx,shifty,0);
glTranslatef(.5,.5,0);
glBegin( GL_QUADS );
glTexCoord2f(0,1);glVertex2f(xmin,ymin);
glTexCoord2f(0,0);glVertex2f(xmin,ymax);
glTexCoord2f(1,0);glVertex2f(xmax,ymax);
glTexCoord2f(1,1);glVertex2f(xmax,ymin);
glEnd();
glDisable(GL_TEXTURE_2D);
glPopMatrix();
glPopAttrib();
}

void final_render_blur(int xmin, int xmax, int ymin, int ymax,int shiftx, int shifty)
{
   static int init=0;
   static int last=0;

//Blur stuff
   if (init==0){
	//glViewport(0,0,1024,768); //SET VIEWPORT
	glBindTexture(GL_TEXTURE_2D, blur_tex[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); //nearest!!
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0,1024, 1024, 0); //RGBA8?
	init=1;           }

	glEnable(GL_BLEND);
   //glViewport(0,0,1024,768); //SET VIEWPORT

   glBindTexture(GL_TEXTURE_2D, blur_tex[0]);
   glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // switch (config.vectrail) //
//	 { case 1:  glColor4f(.95f,.95f,.95f,.85f);break;
//	   case 2:  glColor4f(.95f,.95f,.95f,.85f);break;
//	   case 3:  glColor4f(.95f,.95f,.95f,.95f);break;
//	   default:  glColor4f(.95f,.95f,.95f,.95f);break;
//	 }
	 glColor4f(.95f,.95f,.95f,.95f);
	glBegin(GL_QUADS);
			glTexCoord2f(0,1);glVertex2i(0,1024);
			glTexCoord2f(0,0);glVertex2i(0,0);
			glTexCoord2f(1,0);glVertex2i(1024,0);
			glTexCoord2f(1,1);glVertex2i(1024,1024);
		glEnd();

	//glViewport(0,0,config.screenw,config.screenh); //RESET VIEWPORT
	glBindTexture(GL_TEXTURE_2D, img1a);

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,  GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,  GL_LINEAR);
	glTranslated(shiftx,shifty,0);
	glTranslatef(.5,.5,0);

	glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);

	switch (config.vectrail) //
	 { case 1:  glColor4f(.90,.90,.90,.83); break;
	   case 2:  glColor4f(.95,.95,.95,.86);break;
	   case 3:  glColor4f(.9f,.9f,.9f,.94f); break;
	   default:  glColor4f(.95f,.95f,.95f,.95f);break;
	 }

glColor4f(.8f,.8f,.8f,.8f);
//  glColor4f(.9f,.9f,.9f,.94f);
//	glColor4f(.95f,.95f,.95f,.95f);
glBegin( GL_QUADS );
glTexCoord2f(0,1);glVertex2i(0,1024);
			glTexCoord2f(0,0);glVertex2i(0,0);
			glTexCoord2f(1,0);glVertex2i(1024,0);
			glTexCoord2f(1,1);glVertex2i(1024,1024);
//glTexCoord2f(0,1);glVertex2f(xmin,ymin);
//glTexCoord2f(0,0);glVertex2f(xmin,ymax);
//glTexCoord2f(1,0);glVertex2f(xmax,ymax);
//glTexCoord2f(1,1);glVertex2f(xmax,ymin);
glEnd();

//Copy current texture into blur texture

	glBindTexture(GL_TEXTURE_2D, blur_tex[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); //nearest!!
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0,1024, 1024, 0); //RGBA8?

//glDisable(GL_TEXTURE_2D);
}

void final_render_gauss(int xmin,int xmax,int ymin,int ymax,int shiftx,int shifty, float amount, float num)
{
 //(int i,float l, int q, float o)

	//
	 // note:
	 // I is the screentexture
	 // L is blurr length
	 // Q is quality or the number of blur layers to be renderd
	 // O is the opacity (not here)

	//Draw the previous rendering on top of it
	int cob=10;
	int j=0;
	float l;
	int q;

	float s;
	float opac=0;

	l=amount;
	q=num;

	s=l/q;

	opac=(float)1/q;
	//wrlog("Starting Final Render Chain");
 // Now bind the texture to use it
	glEnable(GL_TEXTURE_2D);

	glBindTexture(GL_TEXTURE_2D, img);
//	If you enabled the mipmap filtering on setup earlier then you'll need to uncomment the line
//	below so OpenGL can generate all the mipmap data for the new main image each frame
	//glGenerateMipmapEXT(GL_TEXTURE_2D);
	//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,  GL_LINEAR_MIPMAP_LINEAR);
	//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,  GL_LINEAR_MIPMAP_LINEAR);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,  GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,  GL_LINEAR);
	glColor4f(1.0f,1.0f,1.0f,1.0f);

	glTranslated(shiftx,shifty,0);
	glTranslatef(.5,.5,0);

	glColor4f(opac,opac,opac,1);
	//glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	//glBlendFunc(GL_ONE, GL_ONE);

glBlendFunc(GL_ONE, GL_ONE);

	glEnable(GL_BLEND);
   glBegin(GL_QUADS);
   while(j<q)
	{
		glTexCoord2f(0,1);							// Texture Coordinate	(   0,   1 )
		glVertex2f(0-(j*s),0-(j*s));				// First Vertex		(   0,   0 )

		glTexCoord2f(0,0);							// Texture Coordinate	(   0,   0 )
		glVertex2f(0-(j*s),ymax+(j*s));				// Second Vertex	(   0, 480 )

		glTexCoord2f(1,0);							// Texture Coordinate	(   1,   0 )
		glVertex2f(xmax+(j*s),ymax+(j*s));			// Third Vertex		( 640, 480 )

		glTexCoord2f(1,1);							// Texture Coordinate	(   1,   1 )
		glVertex2f(xmax+(j*s),0-(j*s));				// Fourth Vertex	( 640,   0 )

	   j++;
	}
	glEnd();
glDisable(GL_TEXTURE_2D);
}
*/
/*
void Create_Shader()
{
glCompileShader(myVertexShader);
glGetShaderiv(myVertexShader, GL_COMPILE_STATUS, &success);
if (!success)
{
GLchar infoLog[MAX_INFO_LOG_SIZE];
glGetShaderInfoLog(myVertexShader, MAX_INFO_LOG_SIZE, NULL, infoLog);
fprintf(stderr, “Error in vertex shader compilation!\n”);
fprintf(stderr, “Info log: %s\n”, infoLog);
}
}
*/
/*
void show_info(void)
{
	 static int fade=255;
	 static int dir=0;

	 if (config.showinfo == 1){
	 showinfo=1;
	 glPushMatrix();

	 glLoadIdentity();
	 glDisable(GL_TEXTURE_2D);
	 glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	 glColor4ub( 10,30,10,240 );
	 glBegin(GL_QUADS);					// Start Drawing Quads
	 glTexCoord2f(0, 0); glVertex3d(282, 234, 0);
	 glTexCoord2f(0, 1); glVertex3d(282, 534, 0);
	 glTexCoord2f(1, 1); glVertex3d(742, 534, 0);
	 glTexCoord2f(1, 0); glVertex3d(742, 234, 0);
	 glEnd();

	 glEnable(GL_TEXTURE_2D);
	 glColor4ub(255, 255,255,255);

	 glBindTexture(GL_TEXTURE_2D, error_tex[1]);
	 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	 glBlendFunc(GL_SRC_ALPHA, GL_ONE); //PROPER
	 glTranslatef  ( 375,475,0);

	 glBegin( GL_QUADS );
	 glTexCoord2d(0,0); glVertex2d(-24,-24); //24
	 glTexCoord2d(0,1); glVertex2d(-24, 24);
	 glTexCoord2d(1,1); glVertex2d( 24, 24);
	 glTexCoord2d(1,0); glVertex2d( 24, -24);
	 glEnd();

	 glPrint(418,455,255,30,30,255,1.3,0,0,"Please Note!");

	 glPrint(330,410,255,255,255,255,1.1,0,0,"To Get the most from this");
	 glPrint(340,375,255,255,255,255,1.1,0,0,"emulator PLEASE READ");
	 glPrint(340,335,255,255,255,255,1.1,0,0,"readme.txt before using!!");

	 glPrint(330,275,255,30,30,255,.8,0,0,"This message will only display once.");
	 glPrint(365,235,255,fade,255,fade,1.1,0,0,"[Press Exit to Close]");
	 if (dir==0){
	 fade-=5;
	 if (fade < 40) {dir=1;}
	 }
	  if (dir==1){
	 fade+=5;
	 if (fade >255) {fade=255;dir=0;}
	 }
	glPopMatrix();
	glLoadIdentity();
	glDisable(GL_TEXTURE_2D);
	}
}

void gl_reset(int xlow,int xhigh,int ylow,int yhigh, int blendmode)
{
				// Reset The Current Viewport
	glMatrixMode(GL_PROJECTION);							// Select The Projection Matrix
	glLoadIdentity();										// Reset The Projection Matrix
	glViewport(0,0,config.screenw,config.screenh);
	glOrtho(xlow,xhigh,ylow,yhigh,-1.0f,1.0f);
	glMatrixMode(GL_MODELVIEW);								// Select The Modelview Matrix
	glLoadIdentity();
	glShadeModel(GL_SMOOTH);								// Enable Smooth Shading
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);					// Black Background

	// Set Line Antialiasing
	glEnable(GL_BLEND);	// Enable Blending
	glEnable(GL_DEPTH);
	glEnable(GL_LINE_SMOOTH);
	glEnable(GL_POINT_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
	//glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	//glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glLineWidth(config.linewidth);//linewidth
	glPointSize(config.pointsize);//pointsize
	glTranslatef(-.175,-.175,0);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);		// Type Of Blending To Use
	glLoadIdentity();
}

*/

/*
void do_an_effect(int effect)
{
int size=32;
int x;
int y=0;
glLoadIdentity();
glPushAttrib(GL_COLOR_BUFFER_BIT);
glEnable(GL_TEXTURE_2D);
glBindTexture(GL_TEXTURE_2D, effect_tex[0]);
glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);	// Linear Filtering
glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);	// Linear Filtering
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
glTranslatef(0.375, .375, 0);
glBlendFunc(GL_DST_COLOR, GL_ZERO); //working

//glBlendFunc(GL_ONE_MINUS_DST_COLOR , GL_ONE);
//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//glBlendFunc(GL_SRC_COLOR, GL_ONE);
glColor4f(1.0f,1.0f,1.0f,.5f);

for (x = 0; x < 1025; x+=32)
{
	for (y = 0; y < 769; y+=32)
	{
	glBegin(GL_QUADS);
	// glTexCoord2f(0,1);glVertex2f(x+size, y+size);
	// glTexCoord2f(0,0);glVertex2f(x, y+size);
	// glTexCoord2f(1,0);glVertex2f(x, y);
	// glTexCoord2f(1,1);glVertex2f(x+size, y);
	glTexCoord2f(0,1);glVertex2i(x,y);
	glTexCoord2f(0,0);glVertex2i(x,y+size);
	glTexCoord2f(1,0);glVertex2i(x+size,y+size);
	glTexCoord2f(1,1);glVertex2i(x+size,y);
	glEnd();
	}
}
glDisable(GL_TEXTURE_2D);
glPopAttrib();
//}
}
*/
/*
void blit_fs_tex(GLuint texture[],int blending,int zoom)
{
glPushMatrix();
glPushAttrib(GL_COLOR_BUFFER_BIT);
glLoadIdentity();
glEnable(GL_TEXTURE_2D);
glBindTexture(GL_TEXTURE_2D, *texture);

glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);	// Linear Filtering
glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);	// Linear Filtering

//glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
//glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_NEAREST);
//glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR );
//glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER, GL_NEAREST_MIPMAP_LINEAR);
//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4.0);

//glHint(GL_POLYGON_SMOOTH_HINT, GL_DONT_CARE);
if (blending==1) glBlendFunc(GL_ZERO, GL_SRC_COLOR);//(GL_DST_COLOR, GL_ZERO);//GL_ONE_MINUS_DST_COLOR
if (blending==0) glBlendFunc(GL_SRC_COLOR, GL_ONE); //PROPER
if (blending==2) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
if (blending==3) glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
glColor4f(1.0f,1.0f,1.0f,.5f);

glTranslated(0,(zoom/2),0);
glBegin(GL_QUADS);
   glTexCoord2f(0,1);glVertex2i(-zoom,769+zoom);
   glTexCoord2f(0,0);glVertex2i(-zoom,-zoom);
   glTexCoord2f(1,0);glVertex2i(1025+zoom,-zoom);
   glTexCoord2f(1,1);glVertex2i(1025+zoom,769+zoom);
glEnd();
glDisable(GL_TEXTURE_2D);
glPopMatrix();
glPopAttrib();
}

void blit_fsa_texub(GLuint texture[],int blending, int r,int g,int b,int alpha, float zoom)
{
glPushMatrix();
glPushAttrib(GL_COLOR_BUFFER_BIT);

//glViewport(0,0,config.screenw,config.screenh);
//glMatrixMode(GL_PROJECTION);
//glOrtho(0,1023,0,767,-1.0f,1.0f);
//glMatrixMode(GL_MODELVIEW);

glLoadIdentity();
glBindTexture(GL_TEXTURE_2D, *texture);
glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);	// Linear Filtering
glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);	// Linear Filtering
glEnable(GL_TEXTURE_2D);
//glHint(GL_POLYGON_SMOOTH_HINT, GL_DONT_CARE);

switch (blending)
{
case 0:glBlendFunc(GL_DST_COLOR, GL_ZERO);break;
case 1:glBlendFunc(GL_SRC_COLOR, GL_ONE);break;
case 2:glBlendFunc(GL_SRC_ALPHA, GL_ONE);break;
case 3:glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);break;
case 4:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);break;
case 5:glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);break;
case 6:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);break;
case 7:glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);break;
case 8:glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);break;
case 9:glBlendFunc(GL_SRC_ALPHA, GL_DST_COLOR);break;
case 10:glBlendFunc(GL_ONE, GL_ONE);break;
case 11:glBlendFunc(GL_ONE, GL_ZERO);break;
case 12:glBlendFunc(GL_ZERO, GL_ONE);break;
case 13:glBlendFunc (GL_ONE, GL_DST_COLOR);break;
case 14:glBlendFunc (GL_SRC_ALPHA, GL_DST_COLOR);break;
case 15:glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);break;
case 16: glDisable(GL_BLEND);break;
}

glColor4ub(r,g,b,alpha);
glTranslated(0,(zoom/2),0);
glBegin(GL_QUADS);
   glTexCoord2f(0,1);glVertex2f(-zoom,767+zoom);
   glTexCoord2f(0,0);glVertex2f(-zoom,-zoom);
   glTexCoord2f(1,0);glVertex2f(1023+zoom,-zoom);
   glTexCoord2f(1,1);glVertex2f(1023+zoom,767+zoom);
glEnd();
glDisable(GL_TEXTURE_2D);
glPopMatrix();
//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
glPopAttrib();
}
*/

/*
void Norm_Rect(int facing,int size)
{
	glBegin( GL_QUADS );
	glTexCoord2f(0,1);glVertex2f(0,config.screenh);
	glTexCoord2f(0,0);glVertex2f(0,0);
	glTexCoord2f(1,0);glVertex2f(config.screenw,0);
	glTexCoord2f(1,1);glVertex2f(config.screenw,config.screenh);

	glEnd();
}
*/

/*
char *textFileRead(char *fn) {
	FILE *fp;
	char *content = NULL;

	int count=0;

	if (fn != NULL) {
		fp = fopen(fn,"rt");

		if (fp != NULL) {
			fseek(fp, 0, SEEK_END);
			count = ftell(fp);
			rewind(fp);

			if (count > 0) {
				content = (char *)malloc(sizeof(char) * (count+1));
				count = fread(content,sizeof(char),count,fp);
				content[count] = '\0';
				}
			fclose(fp);
			}
		}

	if (content == NULL) {
	   wrlog("ERROR: could not load in file %s\n", fn);
	   exit(1);
	   }
	return content;
	}

*/

/******************** Shaders Function ******************************
void setupShaders( char *vert,  char *frag, GLuint p) {
	GLuint v, f;
	int param;
	const char *ff;
	const char *vv;

	char *vs = NULL,*fs = NULL;
v = glCreateShader(GL_VERTEX_SHADER);

vs = textFileRead(vert);
fs = textFileRead(frag);
ff = fs;
vv = vs;
glShaderSource(v, 1, (const GLchar**) &vv, 0);

free(vs);free(fs);
glCompileShader(v);
glGetShaderiv(v, GL_COMPILE_STATUS, &param);
	if (!param)
	{   wrlog("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!vec 3");
		WriteShaderError(v, "stuff/multitex_vertex.vs");
	}

f = glCreateShader(GL_FRAGMENT_SHADER);
glShaderSource(f, 1, (const GLchar**) &ff, 0);
glCompileShader(f);
 if (!param)
	{   wrlog("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!frag 3");
		WriteShaderError(f, "stuff/multitex_vertex.vs");
	}
p = glCreateProgram();
glAttachShader(p,f);
glAttachShader(p,v);
glLinkProgram(p);
//glUseProgram(p);
}
*/