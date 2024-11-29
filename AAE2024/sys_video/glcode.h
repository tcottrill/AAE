#ifndef GLCODE_H
#define GLCODE_H

#include <allegro.h>
#include "alleggl.h"

extern GLuint error_tex[2];
extern GLuint pause_tex[2];
extern GLuint fun_tex[4];
extern GLuint art_tex[8];
extern GLuint game_tex[10];
extern GLuint menu_tex[7];
extern GLuint font_tex[2];
extern GLuint fbotex;

#ifdef __cplusplus
extern "C" {
#endif

	void load_artwork(const struct artworks* p);
	int make_single_bitmap(GLuint* texture, const char* filename, const char* archname, int mtype);
	void draw_center_tex(GLuint* texture, int size, int x, int y, int rotation, int facing, int r, int g, int b, int alpha, int blend);
	//NEW 2024
	void calc_screen_rect(int screen_width, int screen_height, char* aspect, int rotated);
	void blit_any_tex(GLuint *texture, int blending, float alpha, int x, int y, int w, int h);
	void draw_a_quad(int tleft, int bleft, int tright, int bright, int r, int g, int b, int alpha, int blend);
	void menu_textureit(GLuint *texture, int x, int y, int xsize, int ysize);
	void complete_render();
	void set_render();
	void fbo_init();
	void render();
	void resize_art_textures();
	void final_render(int xmin, int xmax, int ymin, int ymax, int shiftx, int shifty);
	void Any_Rect(int facing, int xmin, int xmax, int ymin, int ymax);
	void free_game_textures();
	void texture_reinit();
	void pause_loop();
	int init_gl(void);
	void end_gl();
	int init_shader();
	void WriteShaderError(GLhandleARB obj, const char* shaderName);
	void RendertoTarget();
	void RendertoTarget2();
	void set_blending2();
	void complete_fbo_render();
	void show_error(void);
	void fadeit(void);
	void Widescreen_calc();
	//void do_an_effect(int effect);
	//void textureit(float x, float y, int size);
	//void run_gui(void);
	//void gui_loop(void);
	//void gui_input(int from, int to);
	//void show_info(void);
	//void gui_animate(int reset);
	//void shot_animate(int i);
	//void blit_a_tex(GLuint texture[], int blending, int x, int y, int alpha, float scale);
	//void blit_fs_tex(GLuint texture[], int blending, int zoom);
	//void blit_fsa_texub(GLuint texture[], int blending, int r, int g, int b, int alpha, float zoom);
	//void gl_reset(int xlow,int xhigh,int ylow,int yhigh, int blendmode);
	//void myblending();
	//void FinalRenderB();
	//int re_init_gl(void);
	//void draw2D(float x, float y);
	//void final_render_blur(int xmin, int xmax, int ymin, int ymax, int shiftx, int shifty);
	//void final_render_gauss(int xmin, int xmax, int ymin, int ymax, int shiftx, int shifty, float amount, float num);
	//void blit_sized_texub(GLuint texture[], int blending, int clevel, int alpha, int xmin, int xmax, int ymin, int ymax, int shiftx, int shifty);
	//void ping_pong();

	

#ifdef __cplusplus
}
#endif

#endif