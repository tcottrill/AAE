#ifndef LOADERS_H
#define LOADERS_H

#include "aae_mame_driver.h"

// Deletes all registered textures
void destroy_all_textures();

GLuint load_texture(const char* filename, const char* archname, int numcomponents, int filter, bool modernGL = false);
void snapshot();



#endif