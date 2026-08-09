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
#include "aae_emulator.h"   // emulator_exit_now
#include "memory.h"
#include <stdlib.h>
#include <string.h>
#include "sys_log.h"
#include <vector>


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
	"REGION_USER3",
	"REGION_MAX"
};

std::vector<int>memory_allocation_tracker;

/*-------------------------------------------------
	Clear memory tracker, in preperation to keep
	track of system memory allocations
-------------------------------------------------*/
void reset_memory_tracking()
{
	//memory_allocation_tracker.clear();
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

/*-------------------------------------------------
	memory_region_length - byte length of a region
	(0 if the region is not allocated)
-------------------------------------------------*/
int memory_region_length(int num)
{
	if (num >= 0 && num < MAX_MEMORY_REGIONS)
		return Machine->memory_region_length[num];
	return 0;
}

void free_all_memory_regions()
{
	for (std::vector<int>::iterator it = memory_allocation_tracker.begin(); it != memory_allocation_tracker.end(); ++it)
	{
		LOG_INFO("Freeing Memory Region %s", rom_regions[*it]);
		if (Machine->memory_region[*it])
			free(Machine->memory_region[*it]);
		Machine->memory_region[*it] = nullptr;
	}
}

void free_memory_region(int num)
{
	if (num < MAX_MEMORY_REGIONS)
	{
		LOG_INFO("Freeing Memory Region %d", num);
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

void new_memory_region(int num, int size, int type)
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
			LOG_INFO("Allocating Game Memory, Region# %d Amount 0x%x", num, size);
		}

		Machine->memory_region[num] = (unsigned char*)malloc(size);
		Machine->memory_region_length[num] = size;
		Machine->memory_region_type[num] = type;
		
		if (Machine->memory_region[num] == nullptr)
		{
			// emulator_exit_now rather than exit(): this runs with the render
			// context and the input, audio and LED worker threads all live, so
			// running the static destructors here reports a 0xC0000409
			// fail-fast instead of the allocation failure that actually
			// happened.
			LOG_ERROR("Can't allocate system ram for Cpu Emulation! - This is bad. Exiting System!");
			emulator_exit_now(1);
		}

		memset(Machine->memory_region[num], 0, size);
		memory_allocation_tracker.push_back(num);

		if (config.debug_profile_code) {
			LOG_INFO("Memory Allocation Completed for Rom Region %d", num);
		}
	}
	else
		LOG_ERROR("Error, your'trying to allocate a memory space num that does not exist: ", num);
}