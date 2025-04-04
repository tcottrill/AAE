#pragma once


//= ========================================================================== =
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
// 
// SOME CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.  
//============================================================================



#include "deftypes.h"

#define osd_fread_msbfirst osd_fread_swap
#define osd_fwrite_msbfirst osd_fwrite_swap
#define osd_fread_lsbfirst osd_fread
#define osd_fwrite_lsbfirst osd_fwrite

/* file handling routines */
enum
{
	OSD_FILETYPE_ROM = 1,
	OSD_FILETYPE_SAMPLE,
	OSD_FILETYPE_NVRAM,
	OSD_FILETYPE_HIGHSCORE,
	OSD_FILETYPE_CONFIG,
	OSD_FILETYPE_INPUTLOG,
	OSD_FILETYPE_STATE,
	OSD_FILETYPE_ARTWORK,
	OSD_FILETYPE_MEMCARD,
	OSD_FILETYPE_SCREENSHOT

};

/* gamename holds the driver name, filename is only used for ROMs and samples. */
/* if 'write' is not 0, the file is opened for write. Otherwise it is opened */
/* for read. */
void *osd_fopen(const char *gamename, const char *filename, int filetype, int write);
int osd_fread(void *file, void *buffer, int length);
int osd_fread_swap(void *file, void *buffer, int length);
int osd_fread_scatter(void *file, void *buffer, int length, int increment);
int osd_fwrite(void *file, const void *buffer, int length);
int osd_fseek(void *file, int offset, int whence);
unsigned int osd_fcrc(void *file);
void osd_fclose(void *file);
//From inputport.cpp, all file ops should be together!
int readint(void *f, UINT32 *num);
void writeint(void *f, UINT32 num);
int readword(void *f, UINT16 *num);
void writeword(void *f, UINT16 num);

