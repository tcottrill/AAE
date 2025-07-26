#include "aae_mame_driver.h"
#include "gl_texturing.h"
#include "glcode.h"
#include "texture_handler.h"
// For Vector Fonts.
#include "vector_fonts.h"
#include "colordefs.h"

#pragma warning( disable : 4305 4244 )

float wideadj = 1;
int errorsound = 0;

//
//   Texturing and drawing rectangle code Below.
//

inline void drawTexturedQuad(float x1, float y1, float x2, float y2)
{
	// Vertex coordinates (two triangles)
	GLfloat vertices[] = {
		x1, y1,
		x1, y2,
		x2, y2,

		x1, y1,
		x2, y2,
		x2, y1
	};

	// Corresponding texture coordinates
	GLfloat texCoords[] = {
		0.0f, 1.0f,
		0.0f, 0.0f,
		1.0f, 0.0f,

		0.0f, 1.0f,
		1.0f, 0.0f,
		1.0f, 1.0f
	};

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glTexCoordPointer(2, GL_FLOAT, 0, texCoords);

	glDrawArrays(GL_TRIANGLES, 0, 6);

	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
}

// This is only used by the menu system
void quad_from_center(float x, float y, float width, float height, int r, int g, int b, int alpha)
{
	float minx = x - (width / 2.0f);
	float miny = y - (height / 2.0f);
	float maxx = x + (width / 2.0f);
	float maxy = y + (height / 2.0f);

	// Set color and blend mode
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glColor4ub(r, g, b, alpha);

	// Vertex positions for two triangles forming a quad
	GLfloat vertices[] = {
		minx, miny,
		maxx, miny,
		maxx, maxy,

		minx, miny,
		maxx, maxy,
		minx, maxy
	};

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vertices);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDisableClientState(GL_VERTEX_ARRAY);
}

void Any_Rect(int facing, int xmin, int xmax, int ymin, int ymax)
{
	//LOG_INFO("AnyRect here is xmin:%d xmax:%d ymin:%d ymax:%d", xmin, xmax, ymin, ymax);
	drawTexturedQuad((float)xmin, (float)ymin, (float)xmax, (float)ymax);
}

void FS_Rect(int facing, int size)
{
	drawTexturedQuad(0.0f, (float)size, (float)size, 0.0f);
}
void Screen_Rect(int facing, int size)
{
	drawTexturedQuad(0.0f, (float)size, (float)size, 0.0f);
}

void Resize_Rect(int facing, int size)
{
	float h = size * 0.75f;
	drawTexturedQuad(0.0f, h, (float)size, 0.0f);
}

// This code should never have to be ran.
// All games run in the same internal resolution.

// This should only be called once per each game.
// This code is needed because of the weird way I loaded textures power of 2.
// Please fix if you find time in the future.

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

void resize_art_textures()
{
	set_render();

	if (art_loaded[0]) {
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

	if (art_loaded[1]) {
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

	// Restore old view port and set rendering back to default frame buffer
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);
	//glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);	// Clear Screen And Depth Buffer
	set_ortho(1024, 768);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// Part of the GUI
void show_error(void)
{
	static int fade = 255;
	static int dir = 0;

	if (have_error) {
		//if (!errorsound) { sample_start(5, num_samples - 4, 0); errorsound = 1; }
		glPushMatrix();
		glLoadIdentity();
		glDisable(GL_TEXTURE_2D);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glColor4ub(40, 0, 0, 220);
		drawTexturedQuad(282.0f, 234.0f, 742.0f, 534.0f);

		glEnable(GL_TEXTURE_2D);
		glColor4ub(255, 255, 255, 255);

		glBindTexture(GL_TEXTURE_2D, error_tex[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE); //PROPER
		glTranslatef(375, 475, 0);

		drawTexturedQuad(-24.0f, -24.0f, 24.0f, 24.0f);

		//TODO: Replace this with Vector drawing calls.

		/*
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
		*/
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

void pause_loop()
{
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDrawBuffer(GL_BACK);
	set_ortho(1024, 768);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glDisable(GL_DITHER);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	fontmode_start();
	fprint(460, 450, RGB_WHITE, 4.0, "PAUSED");
	fontmode_end();
}

/*

/// Apply one of several preset blend functions (0–15), or disable blending for any other value.
/// @param mode  Blend mode index (0–15); anything else turns off GL_BLEND.
inline void applyBlendMode(int mode)
{
	if (mode < 0 || mode > 15) {
		glDisable(GL_BLEND);
		return;
	}

	glEnable(GL_BLEND);
	switch (mode)
	{
	case 0:  glBlendFunc(GL_DST_COLOR, GL_ZERO);               break;
	case 1:  glBlendFunc(GL_SRC_COLOR, GL_ONE);                break;
	case 2:  glBlendFunc(GL_SRC_ALPHA, GL_ONE);                break;
	case 3:  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;  // default
	case 4:  glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);                break;
	case 5:  glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);          break;
	case 6:  glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);          break;
	case 7:  glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);               break;
	case 8:  glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);          break;
	case 9:  glBlendFunc(GL_SRC_ALPHA, GL_DST_COLOR);          break;
	case 10: glBlendFunc(GL_ONE, GL_ONE);                break;
	case 11: glBlendFunc(GL_ONE, GL_ZERO);               break;
	case 12: glBlendFunc(GL_ZERO, GL_ONE);                break;
	case 13: glBlendFunc(GL_ONE, GL_DST_COLOR);          break;
	case 14: glBlendFunc(GL_SRC_ALPHA, GL_DST_COLOR);          break;
	case 15: glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);          break;
	}
}

*/