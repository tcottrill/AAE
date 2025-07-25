#pragma once

#ifndef _loader_shim_h_
#define _loader_shim_h_


#include <string>
#include <stdbool.h>

// Loads all ROMs defined in a RomModule list from a ZIP archive
int load_roms(const char* archname, const struct RomModule* p);

// Returns the size of the last file loaded (non-zip)
unsigned int get_last_file_size();

// Returns the size of the last extracted file from a zip archive
unsigned int get_last_zip_file_size();

// Loads a raw file from disk into a newly allocated buffer
// Caller is responsible for freeing the returned buffer
unsigned char* load_file(const char* filename);

// Saves a buffer to disk as binary
int save_file(const char* filename, const unsigned char* buf, int size);

// Saves a char buffer to disk as text
int save_file_char(const char* filename, const char* buf, int size);

// Loads a specific file from a ZIP archive into a newly allocated buffer
// Caller is responsible for freeing the returned buffer
unsigned char* load_zip_file(const char* archname, const char* filename);

// Saves data into a ZIP archive (in-place)
bool save_zip_file(const char* archname, const char* filename, const unsigned char* data);

// Check if file exists string
bool file_exists(const std::string& filename);

// Check if file exists char
bool file_exists(const char* filename);

int load_hi_aae(int start, int size, int image);
int save_hi_aae(int start, int size, int image);
int verify_rom(const char* archname, const struct RomModule* p, int romnum);
int verify_sample(const char** p, int num);
int read_samples(const char** p, int val);

#endif
