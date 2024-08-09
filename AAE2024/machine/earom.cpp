//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
// 
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.  
//==========================================================================


#include <stdio.h>
#include "earom.h"
#include "../aae_mame_driver.h"

extern int gamenum;

#define EAROM_SIZE	0x40
static int earom_offset;
static int earom_data;
static int earom[EAROM_SIZE];

UINT8 EaromRead(UINT32 address, struct MemoryReadByte* psMemRead)
{
	//wrlog("Earom Read? address %x data %d", address,earom_data);
	return (earom_data);
}

//Fix this with the new 6502 code
void EaromWrite(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	earom_offset = (address & 0x00ff);
	if (gamenum == REDBARON) { earom_offset -= 0x20; }
	if (gamenum == GRAVITAR || gamenum == BWIDOW) { earom_offset -= 0x40; }

	earom_data = data;
	//if (debug) wrlog("Earom write? address %x data %d", earom_offset,data);
}

/* 0,8 and 14 get written to this location, too.
 * Don't know what they do exactly
 */
void EaromCtrl(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	/*
		0x01 = clock
		0x02 = set data latch? - writes only (not always)
		0x04 = write mode? - writes only
		0x08 = set addr latch?
	*/

	if (data & 0x01)
		earom_data = earom[earom_offset];
	if ((data & 0x0c) == 0x0c)
	{
		earom[earom_offset] = earom_data;
	}
	//if (debug) wrlog("Earom Control Write: address %x data %d", address,data);
}

void LoadEarom()
{
	FILE* fp;
	char c;
	int i = 0;
	char fullpath[180];

	strcpy(fullpath, "hi\\");
	strcat(fullpath, driver[gamenum].name);
	strcat(fullpath, ".aae");

	//allegro_message(" OK, fullpath is %s",fullpath);
	fp = fopen(fullpath, "rb");

	if (!fp) { return; }
	else {
		do {
			c = getc(fp);    /* get one character from the file */
			earom[i] = c;         /* display it on the monitor       */
			i++;
			//wrlog("Earom Read for %x",c);
		} while (i < 62);    /* repeat until EOF (end of file)  */
	}
	fclose(fp);
	wrlog("Loaded Earom Data for %s", driver[gamenum].name);
}

void SaveEarom(void)
{
	FILE* fp;
	int i;
	char fullpath[180];

	strcpy(fullpath, "hi\\");
	strcat(fullpath, driver[gamenum].name);
	strcat(fullpath, ".aae");
	//allegro_message(" OK, fullpath is %s",fullpath);
	fp = fopen(fullpath, "wb");
	if (fp == NULL) { return; }
	for (i = 0; i < 62; i++)

	{
		fprintf(fp, "%c", earom[i]);
	}//allegro_message("Writing %x",earom[i&0xff]);

	fclose(fp);
	wrlog("Saved Earom Data for %s", driver[gamenum].name);
}

int atari_vg_earom_load()
{
	/* We read the EAROM contents from disk */
		/* No check necessary */
	void* f;

	if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_HIGHSCORE, 0)) != 0)
	{
		osd_fread(f, &earom[0], 0x40);
		osd_fclose(f);
	}
	else  wrlog("Hi Score not found.\n");
	return 1;
}

void atari_vg_earom_save()
{
	void* f;

	if ((f = osd_fopen(Machine->gamedrv->name, 0, OSD_FILETYPE_HIGHSCORE, 1)) != 0)
	{
		osd_fwrite(f, &earom[0], 0x40);
		osd_fclose(f);
	}
}

void atari_vg_earom_handler(void* file, int read_or_write)
{
	if (read_or_write)
		osd_fwrite(file, earom, EAROM_SIZE);
	else
	{
		if (file)
			osd_fread(file, earom, EAROM_SIZE);
		else
			memset(earom, 0, EAROM_SIZE);
	}
}