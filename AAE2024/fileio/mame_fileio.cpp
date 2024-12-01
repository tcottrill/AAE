
#include <stdio.h>
#include <stdlib.h>
#include "aae_mame_driver.h"
#include "log.h"
#include "config.h"
#include "mame_fileio.h"

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


/* file handling routines from mame (TM) */

/* gamename holds the driver name, filename is only used for ROMs and samples. */
/* if 'write' is not 0, the file is opened for write. Otherwise it is opened */
/* for read. */
void *osd_fopen(const char *gamename, const char *filename, int filetype, int write)
{
	char name[100];
	void *f;
	const char *dirname;

	//set_config_file ("mame.cfg");

	switch (filetype)
	{
	case OSD_FILETYPE_ROM:
	case OSD_FILETYPE_SAMPLE:
		sprintf(name, "%s/%s", gamename, filename);
		write_to_log("Trying Opening sample  name,%s/%s", gamename, filename);
		f = fopen(name, write ? "wb" : "rb");
		if (f == 0)
		{
			write_to_log("Trying Opening sample zip name,%s/%s", gamename, filename);
			/* try with a .zip directory (if ZipMagic is installed) */
			sprintf(name, "%s.zip/%s", gamename, filename);
			f = fopen(name, write ? "wb" : "rb");
		}

		if (f == 0)
		{

			/* try again in the appropriate subdirectory */
			dirname = "";
			if (filetype == OSD_FILETYPE_ROM)
				dirname = get_config_string("directory", "rompath", "roms");
			if (filetype == OSD_FILETYPE_SAMPLE)
				dirname = get_config_string("directory","samplepath","samples");

			write_to_log("Trying Opening sample in dir: %s gamename: %s filename: %s", dirname, gamename, filename);
			sprintf(name, "%s/%s/%s", dirname, gamename, filename);
			f = fopen(name, write ? "wb" : "rb");
			if (f == 0)
			{
				write_to_log("Trying Opening sample in dir %s name as zip, last try,%s/%s", dirname, gamename, filename);
				/* try with a .zip directory (if ZipMagic is installed) */
				sprintf(name, "%s/%s.zip/%s", dirname, gamename, filename);
				f = fopen(name, write ? "wb" : "rb");
			}

		}
		if (f) wrlog("Yay found and loaded file!");
		return f;
		break;
	case OSD_FILETYPE_HIGHSCORE:
		/* try again in the appropriate subdirectory */
		dirname = get_config_string("directory","hi","HI");

		sprintf(name, "%s/%s.hi", dirname, gamename);
		f = fopen(name, write ? "wb" : "rb");
		if (f == 0)
		{
			/* try with a .zip directory (if ZipMagic is installed) */
			sprintf(name, "%s.zip/%s.hi", dirname, gamename);
			f = fopen(name, write ? "wb" : "rb");
		}

		return f;
		break;

	case OSD_FILETYPE_NVRAM:
		/* try again in the appropriate subdirectory */
		dirname = get_config_string("directory", "hi", "HI");

		sprintf(name, "%s/%s.nv", dirname, gamename);
		f = fopen(name, write ? "wb" : "rb");
		if (f == 0)
		{
			/* try with a .zip directory (if ZipMagic is installed) */
			sprintf(name, "%s.zip/%s.nv", dirname, gamename);
			f = fopen(name, write ? "wb" : "rb");
		}

		return f;
		break;
	case OSD_FILETYPE_CONFIG:
		/* try again in the appropriate subdirectory */
		dirname = get_config_string("directory","cfg","CFG");

		sprintf(name, "%s/%s.cfg", dirname, gamename);
		f = fopen(name, write ? "wb" : "rb");
		if (f == 0)
		{
			/* try with a .zip directory (if ZipMagic is installed) */
			sprintf(name, "%s.zip/%s.cfg", dirname, gamename);
			f = fopen(name, write ? "wb" : "rb");
		}

		return f;
		break;
	case OSD_FILETYPE_INPUTLOG:
		sprintf(name, "%s.inp", filename);
		return fopen(name, write ? "wb" : "rb");
		break;
	default:
		return 0;
		break;
	}
}


int osd_fread_scatter(void *file, void *buffer, int length, int increment)
{
	/*
	unsigned char *buf = buffer;
	FakeFileHandle *f = (FakeFileHandle *) file;
	unsigned char tempbuf[4096];
	int totread, r, i;

	switch( f->type )
	{
	case kPlainFile:
		totread = 0;
		while (length)
		{
			r = length;
			if( r > 4096 )
				r = 4096;
			r = fread (tempbuf, 1, r, f->file);
			if( r == 0 )
				return totread;		   // error
			for( i = 0; i < r; i++ )
			{
				*buf = tempbuf[i];
				buf += increment;
			}
			totread += r;
			length -= r;
		}
		return totread;
		break;
	case kZippedFile:
	case kRAMFile:
		// reading from the RAM image of a file
		if( f->data )
		{
			if( length + f->offset > f->length )
				length = f->length - f->offset;
			for( i = 0; i < length; i++ )
			{
				*buf = f->data[f->offset + i];
				buf += increment;
			}
			f->offset += length;
			return length;
		}
		break;
	}
*/
	return 0;
}


int osd_fread(void *file, void *buffer, int length)
{
	return fread(buffer, 1, length, (FILE *)file);
}


int osd_fread_swap(void *file, void *buffer, int length)
{
	int i;
	unsigned char *buf;
	unsigned char temp;
	int res;


	res = osd_fread(file, buffer, length);

	buf = (unsigned char *)buffer;
	for (i = 0; i < length; i += 2)
	{
		temp = buf[i];
		buf[i] = buf[i + 1];
		buf[i + 1] = temp;
	}

	return res;
}


int osd_fwrite(void *file, const void *buffer, int length)
{
	return fwrite(buffer, 1, length, (FILE *)file);
}



int osd_fseek(void *file, int offset, int whence)
{
	return fseek((FILE *)file, offset, whence);
}


void osd_fclose(void *file)
{
	fclose((FILE *)file);
}


unsigned int osd_fcrc(void *file)
{

	return 0;
}


int readint(void *f, UINT32 *num)
{
	int i;

	*num = 0;
	for (i = 0; i < sizeof(UINT32); i++)
	{
		unsigned char c;


		*num <<= 8;
		if (osd_fread(f, &c, 1) != 1)
			return -1;
		*num |= c;
	}

	return 0;
}


void writeint(void *f, UINT32 num)
{
	int i;


	for (i = 0; i < sizeof(UINT32); i++)
	{
		unsigned char c;


		c = (num >> 8 * (sizeof(UINT32) - 1)) & 0xff;
		osd_fwrite(f, &c, 1);
		num <<= 8;
	}
}


int readword(void *f, UINT16 *num)
{
	int i, res;


	res = 0;
	for (i = 0; i < sizeof(UINT16); i++)
	{
		unsigned char c;


		res <<= 8;
		if (osd_fread(f, &c, 1) != 1)
			return -1;
		res |= c;
	}

	*num = res;
	return 0;
}


void writeword(void *f, UINT16 num)
{
	int i;


	for (i = 0; i < sizeof(UINT16); i++)
	{
		unsigned char c;


		c = (num >> 8 * (sizeof(UINT16) - 1)) & 0xff;
		osd_fwrite(f, &c, 1);
		num <<= 8;
	}
}