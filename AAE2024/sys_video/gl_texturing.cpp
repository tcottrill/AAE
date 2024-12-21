#include "aae_mame_driver.h"
#include "gl_texturing.h"
#include "../config.h"
#include "../sndhrdw/samples.h"
#include "log.h"
#include "glcode.h"
#include "gl_fbo.h"
#include "fonts.h"

float wideadj = 1;
int errorsound = 0;

GLuint error_tex[2];
GLuint pause_tex[2];
GLuint fun_tex[4];
GLuint art_tex[8];
GLuint game_tex[10];
GLuint menu_tex[7];


//
//   Texturing and drawing rectangle code Below.
//

void set_texture(GLuint* texture, GLboolean linear, GLboolean mipmapping, GLboolean blending, GLboolean set_color)
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

// Only used by the GUI
void draw_center_tex(GLuint* texture, int size, int x, int y, int rotation, int facing, int r, int g, int b, int alpha, int blend)
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

// This was only used by the Menu system
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

// This is only used by the menu system
void quad_from_center(float x, float y, float width, float height, int r, int g, int b, int alpha)
{
	float minx = 0.0f;
	float miny = 0.0f;
	float maxx = 0.0f;
	float maxy = 0.0f;

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glColor4ub(r, g, b, alpha);

	//Set Origin to Center of Texture
	minx = x - (width / 2.0f);
	miny = y - (height / 2.0f);
	maxx = x + (width / 2.0f);
	maxy = y + (height / 2.0f);

	glBegin(GL_QUADS);
	glVertex2f(minx, miny);
	glVertex2f(maxx, miny);
	glVertex2f(maxx, maxy);
	glVertex2f(minx, maxy);
	glEnd();
}



// Rectangle Drawing. Way too many.
void Bezel_Rect(int facing, int xmin, int xmax, int ymin, int ymax)
{
	float x = 0;

	x = (1.00 - wideadj) / 2;
	wrlog("Bezel Rect here is %d", x);
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
	//wrlog("AnyRect here is xmin:%d xmax:%d ymin:%d ymax:%d", xmin, xmax, ymin, ymax);
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
	if (config.widescreen)
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
	
	glBegin(GL_QUADS);
	glTexCoord2f(0, 1); glVertex2f(0, sizeC);
	glTexCoord2f(0, 0); glVertex2f(0, 0);
	glTexCoord2f(1, 0); glVertex2f(size, 0);
	glTexCoord2f(1, 1); glVertex2f(size, sizeC);
	glEnd();
	
}

void Resize_Rect(int facing, int size)
{
	//Hack, please find and fix. This one was frustrating. 
	/*
	if (gamenum == ARMORA) 
	{
		glBegin(GL_QUADS);
		glTexCoord2f(0, 1); glVertex2f(0, size);
		glTexCoord2f(0, 0); glVertex2f(0, 0);
		glTexCoord2f(1, 0); glVertex2f(size, 0);
		glTexCoord2f(1, 1); glVertex2f(size, size);
		glEnd();
	}
	else
	{*/
		glBegin(GL_QUADS);
		glTexCoord2f(0, 1); glVertex2f(0, size * .75);
		glTexCoord2f(0, 0); glVertex2f(0, 0);
		glTexCoord2f(1, 0); glVertex2f(size, 0);
		glTexCoord2f(1, 1); glVertex2f(size, size * .75);
		glEnd();
	
	//}
}

// This code should never have to be ran.
// All games run in the same internal resolution.

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

// This should only be called once per each game.
// This code is needed because of the weird way I loaded textures power of 2.
// Please fix if you find time in the future.

void resize_art_textures()
{

	set_render();
	/*
		glMatrixMode(GL_PROJECTION);							// Select The Projection Matrix
		glLoadIdentity();										// Reset The Projection Matrix
		glViewport(0, 0,1023, 1023);
		glOrtho(0, 1023, 1023,0, -1.0f, 1.0f);
		glMatrixMode(GL_MODELVIEW);								// Select The Modelview Matrix
		glLoadIdentity();
	*/
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
		//Any_Rect(0, 0, 1024, 0, 750);
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
		//Any_Rect(0, sx, sy, ex, ey);
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
	// Restore old view port and set rendering back to default frame buffer
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);
	//glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);	// Clear Screen And Depth Buffer
	set_ortho(1024, 768);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
}

void set_blending2()
{
	float k = 1.0 / 10.0;//12
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

void menu_textureit(GLuint* texture, int x, int y, int xsize, int ysize)
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

void blit_any_tex(GLuint* texture, int blending, float alpha, int x, int y, int w, int h)
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

// Part of the GUI
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
		case 10: {
			glPrint(418, 417, 32, 178, 170, 255, 1.1, 0, 0, "REQUIRED ROMS");
			glPrint(418, 380, 32, 178, 170, 255, 1.1, 0, 0, "NOT FOUND!!"); break;
		}
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

// Part of the GUI
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

//Currently Commented Out

void pause_loop()
{
	glColor4f(.5f, .5f, .5f, .5f);
	glDisable(GL_BLEND);
	glPrint(440, 50, 255, 128, 128, 255, 1.3, 0, 0, "PAUSED");
}