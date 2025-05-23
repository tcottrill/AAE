#pragma once

#ifndef _loader_shim_h_
#define _loader_shim_h_

#include <stdbool.h>

int load_roms(const char* archname, const struct RomModule* p);
unsigned int get_last_file_size();
unsigned int get_last_zip_file_size();
unsigned char* load_file(const char* filename);
int save_file(const char* filename, unsigned char* buf, int size);
int save_file_char(const char* filename, char* buf, int size);
unsigned char* load_zip_file(const char* archname, const char* filename);
bool save_zip_file(const char* archname, const char* filename, unsigned char* data);
//BOOL DirectoryExists(const char* dirName)


#endif
