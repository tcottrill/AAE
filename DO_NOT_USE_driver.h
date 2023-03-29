#ifndef DRIVER_H
#define DRIVER_H


#include "mamecommon.h"
#include "mame.h"
#include "cpuintrf.h"
#include "inptport.h"
//#include "usrintrf.h"
#include "log.h"



/***************************************************************************

IN and OUT ports are handled like memory accesses, the hook template is the
same so you can interchange them. Of course there is no 'base' pointer for
IO ports.

***************************************************************************/
struct IOReadPort
{
	int start,end;
	int (*handler)(int offset);	/* see special values below */
};

#define IORP_NOP 0	/* don't care, return 0 */


struct IOWritePort
{
	int start,end;
	void (*handler)(int offset,int data);	/* see special values below */
};

#define IOWP_NOP 0	/* do nothing */





struct GfxDecodeInfo
{
	int memory_region;	/* memory region where the data resides (usually 1) */
						/* -1 marks the end of the array */
	int start;	/* beginning of data to decode */
	struct GfxLayout *gfxlayout;
	int color_codes_start;	/* offset in the color lookup table where color codes start */
	int total_color_codes;	/* total number of color codes */
};



struct MachineCPU
{
	int cpu_type;	/* see #defines below. */
	int cpu_clock;	/* in Hertz */
	int memory_region;	/* number of the memory region (allocated by loadroms()) where */
						/* this CPU resides */
	const struct MemoryReadAddress *memory_read;
	const struct MemoryWriteAddress *memory_write;
	const struct IOReadPort *port_read;
	const struct IOWritePort *port_write;
	int (*interrupt)(void);
	int interrupts_per_frame;	/* usually 1 */
};

#define CPU_Z80    1
#define CPU_M6502  2
#define CPU_I86    3
#define CPU_M6809  4
#define CPU_M68000 5 /* LBO */

/* set this if the CPU is used as a slave for audio. It will not be emulated if */
/* play_sound == 0, therefore speeding up a lot the emulation. */
#define CPU_AUDIO_CPU 0x8000

#define CPU_FLAGS_MASK 0xff00


#define MAX_CPU 5 /* LBO */


/* ASG 081897 -- added these flags for the video hardware */

/* bit 0 of the video attributes indicates raster or vector video hardware */
#define	VIDEO_TYPE_RASTER			0x0000
#define	VIDEO_TYPE_VECTOR			0x0001

/* bit 1 of the video attributes indicates whether or not dirty rectangles will work */
#define	VIDEO_SUPPORTS_DIRTY		0x0002

/* bit 2 of the video attributes indicates whether or not the driver modifies the palette */
#define	VIDEO_MODIFIES_PALETTE	0x0004



struct MachineDriver
{
	
	/* basic machine hardware */
	struct MachineCPU cpu[MAX_CPU];
	int frames_per_second;
	void (*init_machine)(void);

	/* video hardware */
	int screen_width,screen_height;
	struct rectangle visible_area;
	struct GfxDecodeInfo *gfxdecodeinfo;
	int total_colors;	/* palette is 3*total_colors bytes long */
	int color_table_len;	/* length in bytes of the color lookup table */
	void (*vh_convert_color_prom)(unsigned char *palette, unsigned char *colortable,const unsigned char *color_prom);

	int video_attributes;	/* ASG 081897 */
	int (*vh_init)(const char *gamename);
	int (*vh_start)(void);
	void (*vh_stop)(void);
	void (*vh_update)(struct osd_bitmap *bitmap);

	/* sound hardware */
	unsigned char *samples;
	int (*sh_init)(const char *gamename);
	int (*sh_start)(void);
	void (*sh_stop)(void);
	void (*sh_update)(void);
};



struct GameDriver
{

	const char *description;
	const char *name;
	const char *credits;
	const struct MachineDriver *drv;

	const struct RomModule *rom;
	void (*rom_decode)(void);		/* used to decrypt the ROMs after loading them */
	void (*opcode_decode)(void);	/* used to decrypt the CPU opcodes in the ROMs, */
									/* if the encryption is different from the above. */
	const char **samplenames;	/* optional array of names of samples to load. */
						/* drivers can retrieve them in Machine->samples */

	struct InputPort *input_ports;
	
		/* if they are available, provide a dump of the color proms (there is no */
		/* copyright infringement in that, since you can't copyright a color scheme) */
		/* and a function to convert them to a usable palette and colortable (the */
		/* function pointer is in the MachineDriver, not here) */
		/* Otherwise, leave this field null and provide palette and colortable. */
	const unsigned char *color_prom;
	const unsigned char *palette;
	const unsigned char *colortable;
	int orientation;	/* orientation of the monitor; see defines below */

	int (*hiscore_load)(const char *name);	/* will be called every vblank until it */
						/* returns nonzero */
	void (*hiscore_save)(const char *name);	/* will not be called if hiscore_load() hasn't yet */
						/* returned nonzero, to avoid saving an invalid table */
};


#define	ORIENTATION_DEFAULT		0x00
#define	ORIENTATION_FLIP_X		0x01	/* mirror everything in the X direction */
#define	ORIENTATION_FLIP_Y		0x02	/* mirror everything in the Y direction */
#define ORIENTATION_SWAP_XY		0x04	/* mirror along the top-left/bottom-rigth diagonal */
#define	ORIENTATION_ROTATE_90	(ORIENTATION_SWAP_XY|ORIENTATION_FLIP_X)	/* rotate clockwise 90 degrees */
#define	ORIENTATION_ROTATE_180	(ORIENTATION_FLIP_X|ORIENTATION_FLIP_Y)	/* rotate 180 degrees */
#define	ORIENTATION_ROTATE_270	(ORIENTATION_SWAP_XY|ORIENTATION_FLIP_Y)	/* rotate counter-clockwise 90 degrees */
/* IMPORTANT: to perform more than one transformation, DO NOT USE |, use ^ instead. */
/* For example, to rotate 90 degrees counterclockwise and flip horizontally, use: */
/* ORIENTATION_ROTATE_270 ^ ORIENTATION_FLIP_X */
/* FLIP is performed *after* SWAP_XY. */

//const unsigned char *mypalette;

extern const struct GameDriver *drivers[];

#ifdef WIN32
#pragma warning(disable:4113)
//#define PI 3.1415926535
#define inline __inline
#endif

#endif
