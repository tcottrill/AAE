#ifndef GLCODE_H
#define GLCODE_H

#include <allegro.h>
#include "alleggl.h"
#include "texrect.h"

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
	int init_shader();
	void WriteShaderError(GLhandleARB obj, const char* shaderName);
	void RendertoTarget();
	void RendertoTarget2();
	
	void Widescreen_calc();
	

	

#ifdef __cplusplus
}
#endif

#endif