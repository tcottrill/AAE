#ifndef GLCODE_H
#define GLCODE_H

#include "framework.h"
#include "texrect.h"
#include "sys_gl.h"

#ifdef __cplusplus
extern "C" {
#endif

	void load_artwork(const struct artworks* p);
	int make_single_bitmap(GLuint* texture, const char* filename, const char* archname, int mtype);

	//NEW 2024
	void calc_screen_rect(int screen_width, int screen_height, char* aspect, int rotated);
	void set_ortho(GLint width, GLint height);
	void set_render();
	void render();
	void final_render(int xmin, int xmax, int ymin, int ymax, int shiftx, int shifty);
	void set_render_fbo4();
	void end_render_fbo4();

	void free_game_textures();
	int init_gl(void);
	void end_gl();
	//int init_shader();
	
	void Widescreen_calc();
	
	//void set_render_raster();
	//void final_render_raster();

#ifdef __cplusplus
}
#endif

#endif