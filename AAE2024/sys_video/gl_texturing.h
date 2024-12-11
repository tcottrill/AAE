#pragma once

#ifndef GL_TEX_H
#define GL_TEX_H

#include <allegro.h>
#include "alleggl.h"

extern GLuint error_tex[2];
extern GLuint pause_tex[2];
extern GLuint fun_tex[4];
extern GLuint art_tex[8];
extern GLuint game_tex[10];
extern GLuint menu_tex[7];



extern float wideadj;
extern int errorsound;

void quad_from_center(float x, float y, float width, float height, int r, int g, int b, int alpha);
void set_texture(GLuint* texture, GLboolean linear, GLboolean mipmapping, GLboolean blending, GLboolean set_color);
void draw_center_tex(GLuint* texture, int size, int x, int y, int rotation, int facing, int r, int g, int b, int alpha, int blend);
void draw_a_quad(int tleft, int bleft, int tright, int bright, int r, int g, int b, int alpha, int blend);
void Bezel_Rect(int facing, int xmin, int xmax, int ymin, int ymax);
void Any_Rect(int facing, int xmin, int xmax, int ymin, int ymax);
void FS_Rect(int facing, int size);
void Screen_Rect(int facing, int size);
void Special_Rect(int facing, int size);
void Centered_Rect(int facing, int size);
void Resize_Rect(int facing, int size);

void texture_reinit();
void resize_art_textures();
void set_blending2();
void menu_textureit(GLuint* texture, int x, int y, int xsize, int ysize);
void blit_any_tex(GLuint* texture, int blending, float alpha, int x, int y, int w, int h);
void show_error(void);
void fadeit(void);
void pause_loop();



#endif
