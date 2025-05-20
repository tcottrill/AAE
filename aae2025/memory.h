#ifndef MEMORY_H
#define MEMORY_H

extern const char* rom_regions[];


enum {
	//REGION_INVALID = 0x80,
	REGION_CPU1,
	REGION_CPU2,
	REGION_CPU3,
	REGION_CPU4,
	REGION_GFX1,
	REGION_GFX2,
	REGION_GFX3,
	REGION_GFX4,
	REGION_PROMS,
	REGION_SOUND1,
	REGION_SOUND2,
	REGION_SOUND3,
	REGION_SOUND4,
	REGION_USER1,
	REGION_USER2,
	REGION_USER3,
	REGION_MAX
};

#define REGIONFLAG_MASK			0xf8000000
#define REGIONFLAG_DISPOSE		0x80000000           /* Dispose of this region when done */
#define REGIONFLAG_SOUNDONLY	0x40000000           /* load only if sound emulation is turned on */

#define PROM_MEMORY_REGION(region) ((const unsigned char *)((-(region))-1))

unsigned char *memory_region(int num);
void new_memory_region(int num, int size);
void free_memory_region(int num);
void byteswap(unsigned char* mem, int length);
void free_all_memory_regions();
void reset_memory_tracking();





#endif