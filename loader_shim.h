#pragma once

#ifndef _loader_shim_h_
#define _loader_shim_h_

#include <stdbool.h>


unsigned int get_last_file_size();
unsigned int get_last_zip_file_size();
unsigned char* load_file_shim(const char* filename);
bool save_file_shim(const char* filename, unsigned char* buf, int size);
unsigned char* load_zip_file(const char* filename, const char* archname);
bool save_zip_file(const char* filename, const char* archname, unsigned char* data);


















#endif


