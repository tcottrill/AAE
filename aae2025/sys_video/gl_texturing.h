#pragma once

#ifndef GL_TEX_H
#define GL_TEX_H

#include "sys_gl.h"

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

void Any_Rect(int facing, int xmin, int xmax, int ymin, int ymax);
void FS_Rect(int facing, int size);
void Screen_Rect(int facing, int size);
void Centered_Rect(int facing, int size);
void Resize_Rect(int facing, int size);
void texture_reinit();
void resize_art_textures();
void show_error(void);
void pause_loop();



#endif
