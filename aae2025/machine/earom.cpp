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
#include "aae_mame_driver.h"

#define EAROM_SIZE	0x40
static int earom_offset;
static int earom_data;
static int earom[EAROM_SIZE];

UINT8 EaromRead(UINT32 address, struct MemoryReadByte* psMemRead)
{
	return (earom_data);
}

void EaromWrite(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	//LOG_INFO("Earom write? address %x data %d", earom_offset,data);
	earom_offset = (address & 0x00ff);

	earom_data = data;
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
	//if (debug) LOG_INFO("Earom Control Write: address %x data %d", address,data);
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
	else  LOG_INFO("Hi Score not found.\n");
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