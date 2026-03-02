#ifndef GLCODE_H
#define GLCODE_H

#include "framework.h"
#include "texrect.h"
#include "sys_gl.h"

// Sane Global Rectangle Coordinates
extern int game_rect_left;
extern int game_rect_right;
extern int game_rect_bottom;
extern int game_rect_top;

void set_ortho(GLint width, GLint height);
void set_render();
void render();
void final_render(int left, int right, int bottom, int top); 
void set_render_fbo4();
void end_render_fbo4();
void glcode_vector_hard_clear_fbo1();
int init_gl(void);
void end_gl();
void emulator_on_window_resize(int newW, int newH);
void Widescreen_calc();
void init_raster_overlay();
void shutdown_raster_overlay();

#endif