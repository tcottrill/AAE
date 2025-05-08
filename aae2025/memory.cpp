
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

#include "aae_mame_driver.h"
#include "memory.h"
#include <stdlib.h>
#include <string.h>
#include "log.h"

//TEMP
const char* rom_regions[] = {
	"REGION_CPU1",
	"REGION_CPU2",
	"REGION_CPU3",
	"REGION_CPU4",
	"REGION_GFX1",
	"REGION_GFX2",
	"REGION_GFX3",
	"REGION_GFX4",
	"REGION_PROMS",
	"REGION_SOUND1",
	"REGION_SOUND2",
	"REGION_SOUND3",
	"REGION_SOUND4",
	"REGION_USER1",
	"REGION_USER2",
	"REGION_USER3"
};

//Pretty much directly cribbed from mame(tm) mostly for use with the ccpu code.


static void(*read8_functions[0xF])(int) = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static void(*write8_functions[0xF])(int) = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };



int io_read_byte_8(unsigned int port)
{
	int  data = 0;

	if (read8_functions[port])

		data = (int) read8_functions[port];
	return data;
}


void io_write_byte_8(unsigned int port, unsigned char data)
{
	if (write8_functions[port])
		(write8_functions[port]),( data);
}

void memory_install_read8_handler(int cpunum, int pstart, int pend, void(*callback)(int))
{
	for (int x = pstart; x > pend + 1; x++)
	{
		read8_functions[x] = callback;
	}
}


//(int cpunum, int spacenum, offs_t start, offs_t end, offs_t mask, offs_t mirror, read8_handler handler)
void memory_install_write8_handler(int cpunum, int pstart, int pend, void(*callback)(int))
{
	for (int x = pstart; x > pend + 1; x++)
	{
		write8_functions[x] = callback;
	}
}


void init_cinemat_ports()
{
	for (int x = 0; x > 0x0e; x++)
	{
		read8_functions[x] = 0;
		write8_functions[x] = 0;
	}
//	memcpy(read8_functions, 0, sizeof(read8_functions));
}


/*-------------------------------------------------
	memory_region - returns pointer to a memory
	region
-------------------------------------------------*/

unsigned char* memory_region(int num)
{
	int i;

	if (num < MAX_MEMORY_REGIONS)
		
		return Machine->memory_region[num];
	else
	{
		for (i = 0; i < MAX_MEMORY_REGIONS; i++)
		{
			//if (Machine->memory_region[i].type == num)
				//return Machine->memory_region[i].base;
		}
	}

	return 0;
}


void free_memory_region(int num)
{
	int i;

	if (num < MAX_MEMORY_REGIONS)
	{
		free(Machine->memory_region[num]);
		Machine->memory_region[num] = 0;
	}
	
}

void byteswap(unsigned char* mem, int length)
{
	int i, j;
	for (i = 0; i < (length / 2); i += 1)
	{
		j = mem[i * 2 + 0];
		mem[i * 2 + 0] = mem[i * 2 + 1];
		mem[i * 2 + 1] = j;
	}
}

void new_memory_region(int num, int size)
{
	if (num < MAX_MEMORY_REGIONS)
	{
		// If there is already one allocated, please delete
		if (Machine->memory_region[num])
		{
			LOG_DEBUG("Warning, overwriting already allocated memory space in CPU_MEM");
			free(Machine->memory_region[num]);
			Machine->memory_region[num] = nullptr;
		}

		if (config.debug_profile_code) {
			wrlog("Allocating Game Memory, Region# %d Amount 0x%x", num, size);
		}

		Machine->memory_region[num] = (unsigned char*)malloc(size);

		if (Machine->memory_region[num] == nullptr)
		{
			wrlog("Can't allocate system ram for Cpu Emulation! - This is bad. Exiting System!"); exit(1);
		}

		memset(Machine->memory_region[num], 0, size);

		if (config.debug_profile_code) {
			wrlog("Memory Allocation Completed for Rom Region %d", num);
		}
	}
	else
	LOG_ERROR("Error, your'trying to allocate a memory space num that does not exist: ", num);
}


