#pragma once
#include "osd_cpu.h"

#define osd_fread_msbfirst osd_fread_swap
#define osd_fwrite_msbfirst osd_fwrite_swap
#define osd_fread_lsbfirst osd_fread
#define osd_fwrite_lsbfirst osd_fwrite

/* file handling routines */
#define OSD_FILETYPE_ROM 1
#define OSD_FILETYPE_SAMPLE 2
#define OSD_FILETYPE_HIGHSCORE 3
#define OSD_FILETYPE_CONFIG 4
#define OSD_FILETYPE_INPUTLOG 5
#define OSD_FILETYPE_STATE 6
#define OSD_FILETYPE_ARTWORK 7
#define OSD_FILETYPE_MEMCARD 8
#define OSD_FILETYPE_SCREENSHOT 9

/* gamename holds the driver name, filename is only used for ROMs and samples. */
/* if 'write' is not 0, the file is opened for write. Otherwise it is opened */
/* for read. */
void* osd_fopen(const char* gamename, const char* filename, int filetype, int write);
int osd_fread(void* file, void* buffer, int length);
int osd_fread_swap(void* file, void* buffer, int length);
int osd_fread_scatter(void* file, void* buffer, int length, int increment);
int osd_fwrite(void* file, const void* buffer, int length);
int osd_fseek(void* file, int offset, int whence);
unsigned int osd_fcrc(void* file);
void osd_fclose(void* file);
//From inputport.cpp, all file ops should be together!
int readint(void* f, UINT32* num);
void writeint(void* f, UINT32 num);
int readword(void* f, UINT16* num);
void writeword(void* f, UINT16 num);
