
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

unsigned char *memory_region(int num)
{
	int i;

	if (num < MAX_MEMORY_REGIONS)

		return Machine->memory_region[num];
	else
	
	return 0;
}


int new_memory_region(int num, int length)
{
	int i;

	if (num < MAX_MEMORY_REGIONS)
	{
		Machine->memory_region_length[num] = length;
		Machine->memory_region[num] = (unsigned char*) malloc(length);
		return (Machine->memory_region[num] == NULL) ? 1 : 0;
	}
	
	return 1;
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


void cpu_mem(int cpunum, int size)
{
	wrlog("Made it here, %d %d",cpunum,size);
	
	if (Machine->memory_region[cpunum])
	{
		free(Machine->memory_region[cpunum]);
		Machine->memory_region[cpunum] = nullptr;
	}
	
	wrlog("Allocating Game Memory, Cpu# %d Amount 0x%x", cpunum, size);

	Machine->memory_region[cpunum] = (unsigned char *)malloc(size);

	if (Machine->memory_region[cpunum] == nullptr)
	{
		wrlog("Can't allocate system ram for Cpu Emulation! - This is bad. Exiting System!"); exit(1);
	}

	memset(Machine->memory_region[cpunum], 0, size);

	wrlog("Memory Allocation Completed for Rom Region %d",cpunum);
}


