#ifndef LOADERS_H
#define LOADERS_H

#include "aae_mame_driver.h"


extern GLuint error_tex[2];
extern GLuint pause_tex[2];
extern GLuint fun_tex[4];
extern GLuint art_tex[8];
extern GLuint game_tex[10];
extern GLuint menu_tex[7];


// Load
GLuint load_texture(const char* filename, const char* archname, int numcomponents, int filter, bool modernGL = false);
// Use
void set_texture(GLuint* texture, GLboolean linear, GLboolean mipmapping, GLboolean blending, GLboolean set_color);
// Take snapshot
void snapshot();
// Deletes all registered textures
void destroy_all_textures();


#endif