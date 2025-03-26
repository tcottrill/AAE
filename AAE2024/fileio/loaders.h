#ifndef LOADERS_H
#define LOADERS_H

#include <allegro.h>
#include "alleggl.h"

GLuint load_texture(const char* filename, const char* archname, int numcomponents, int filter);

int load_roms(const char* archname, const struct RomModule *p);
int verify_rom(const char* archname, const struct RomModule *p, int romnum);
int verify_sample(const char** p, int num);
int load_samples(const char **p, int val);
int file_exist(const char* filename);
void snapshot();
int load_hi_aae(int start, int size, int image);
int save_hi_aae(int start, int size, int image);
int load_ambient();

#endif