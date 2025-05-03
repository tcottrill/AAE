#include "emu_vector_draw.h"
#include "colordefs.h"
#include "log.h"
#include "glcode.h"
#include "gl_texturing.h" // For game_tex[0]

#pragma warning( disable :  4244)

#define clip(a, b, c) min(max((a), (b)), (c))

float xoffset;
float yoffset;
GLuint* tex;

colors vec_colors[256];

std::vector<fpoint> linelist;
std::vector<txdata> texlist;


void set_texture_id(GLuint* id)
{
	tex = id;
}

void set_blendmode(GLenum sfactor, GLenum dfactor)
{
	glBlendFunc(sfactor, dfactor);
}

rgb_t cache_tex_color(int intensity, rgb_t col)
{
	UINT8* test = (UINT8*)&col;

	test[0] = clip((test[0] & intensity) + config.gain, 0, 0xff);
	test[1] = clip((test[1] & intensity) + config.gain, 0, 0xff);
	test[2] = clip((test[2] & intensity) + config.gain, 0, 0xff);
	test[3] = intensity;

	return col;
}

void cache_texpoint(float ex, float ey, float tx, float ty, int intensity, rgb_t col)
{
	texlist.emplace_back(ex - xoffset, ey - yoffset, tx, ty, cache_tex_color(intensity, col));
}

void add_line(float sx, float sy, float ex, float ey, int intensity, rgb_t col)
{

	UINT8* test = (UINT8*)&col;
	rgb_t temp_col;
	rgb_t temp_half_col;

	test[0] = clip((test[0] & intensity) + config.gain, 0, 0xff);
	test[1] = clip((test[1] & intensity) + config.gain, 0, 0xff);
	test[2] = clip((test[2] & intensity) + config.gain, 0, 0xff);
	test[3] = 0xff;
	//test[3] = clip(intensity + config.gain, 0, 0xff);
	temp_col = col;

	if (Machine->drv->video_attributes & VECTOR_USES_COLOR)
	{
		test[0] = clip((test[0] & intensity) + config.gain / 2, 0, 0xff);
		test[1] = clip((test[1] & intensity) + config.gain / 2, 0, 0xff);
		test[2] = clip((test[2] & intensity) + config.gain / 2, 0, 0xff);
		test[3] = 0x7f;
		temp_half_col = col;
	}

	linelist.emplace_back(sx, sy, temp_col, temp_half_col);
	linelist.emplace_back(ex, ey, temp_col, temp_half_col);
}

void add_tex(float ex, float ey, int intensity, rgb_t col)
{
	float xoffset = config.fire_point_size;
	float yoffset = config.fire_point_size;
	/*
	//V0
	txlist.emplace_back(CurX, CurY, s, t, fcolor);
	//V1
	txlist.emplace_back(DstX, CurY, s + w, t, fcolor);
	//V2
	txlist.emplace_back(DstX, DstY, s + w, t - h, fcolor);
	//V2
	txlist.emplace_back(DstX, DstY, s + w, t - h, fcolor);
	//V3
	txlist.emplace_back(CurX, DstY, s, t - h, fcolor);
	//V0
	txlist.emplace_back(CurX, CurY, s, t, fcolor);
		*/

	cache_texpoint(ex - xoffset, ey - yoffset, 0, 0, intensity, col);
	cache_texpoint(ex + xoffset, ey - yoffset, 1, 0, intensity, col);
	cache_texpoint(ex + xoffset, ey + yoffset, 1, 1, intensity, col);
	cache_texpoint(ex - xoffset, ey + yoffset, 0, 1, intensity, col);
}

void cache_clear()
{
	//Reset index pointers.
	texlist.clear();
	linelist.clear();
}

void draw_all()
{
	if (Machine->gamedrv->video_attributes & VECTOR_USES_COLOR)
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	else
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glDisable(GL_TEXTURE_2D);
	//Draw Lines and endpoints
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(fpoint), &linelist[0].color);
	glVertexPointer(2, GL_FLOAT, sizeof(fpoint), &linelist[0].x);
	// Draw Our Lines
	glDrawArrays(GL_LINES, 0, (GLsizei)linelist.size());
	// Change our color array
	if (Machine->drv->video_attributes & VECTOR_USES_COLOR)
	{
		glColorPointer(3, GL_UNSIGNED_BYTE, sizeof(fpoint), &linelist[0].colorshalf);
	}
	glDrawArrays(GL_POINTS, 0, (GLsizei)linelist.size());

	

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	if (!texlist.empty())
	{
		//Draw Textured Shots for Asteroids. This code is only currently used when running Asteroids/Asteroids Deluxe or one of it's clones.
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, *tex);
		glBlendFunc(GL_ONE, GL_ONE);
		//	glColor4f(1.0f, 1.0f, 1.0f,1.0f);

		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(2, GL_FLOAT, sizeof(txdata), &texlist[0].x);

		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, sizeof(txdata), &texlist[0].tx);

		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(txdata), &texlist[0].color);

		//glDrawArrays(GL_TRIANGLES, 0, (GLsizei)texlist.size());
		glDrawArrays(GL_QUADS, 0, (GLsizei)texlist.size());

		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);

		//Change Blending back to Line Drawing
		glDisable(GL_TEXTURE_2D);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

}