#ifndef LOADERS_H
#define LOADERS_H

#include "aae_mame_driver.h"

GLuint load_texture(const char* filename, const char* archname, int numcomponents, int filter);
int verify_rom(const char* archname, const struct RomModule *p, int romnum);
int verify_sample(const char** p, int num);
int read_samples(const char** p, int val);
bool file_exist(const std::string& filename);
void snapshot();
int load_hi_aae(int start, int size, int image);
int save_hi_aae(int start, int size, int image);
int load_ambient();

#endif