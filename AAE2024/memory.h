#ifndef MEMORY_H
#define MEMORY_H



enum {
	//REGION_INVALID = 0x80,
	REGION_CPU1,
	REGION_CPU2,
	REGION_CPU3,
	REGION_CPU4,
	REGION_CPU5,
	REGION_CPU6,
	REGION_CPU7,
	REGION_CPU8,
	REGION_GFX1,
	REGION_GFX2,
	REGION_GFX3,
	REGION_GFX4,
	REGION_GFX5,
	REGION_GFX6,
	REGION_GFX7,
	REGION_GFX8,
	REGION_PROMS,
	REGION_SOUND1,
	REGION_SOUND2,
	REGION_SOUND3,
	REGION_SOUND4,
	REGION_SOUND5,
	REGION_SOUND6,
	REGION_SOUND7,
	REGION_SOUND8,
	REGION_USER1,
	REGION_USER2,
	REGION_USER3,
	REGION_USER4,
	REGION_USER5,
	REGION_USER6,
	REGION_USER7,
	REGION_USER8,
	REGION_MAX
};

#define REGIONFLAG_MASK			0xf8000000
#define REGIONFLAG_DISPOSE		0x80000000           /* Dispose of this region when done */
#define REGIONFLAG_SOUNDONLY	0x40000000           /* load only if sound emulation is turned on */

#define PROM_MEMORY_REGION(region) ((const unsigned char *)((-(region))-1))

//unsigned char *memory_region(int num);
int new_memory_region(int num, int length);
void free_memory_region(int num);
void cpu_mem(int cpunum, int size);





#endif