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


#include "SegaG80.h"
#include <math.h>
//#include "segacrypt.cpp"
#include "../aae_mame_driver.h"
#include "../sndhrdw/samples.h"
#include "../vidhrdwr/vector.h"

// To Do, add interupt handler for entering test mode.
void sega_interrupt() {
	if (input_port_5_r(0) & 0x01)
		cpu_do_interrupt(INT_TYPE_NMI, CPU0);
	else
		cpu_do_interrupt(INT_TYPE_INT, CPU0);
}

void (*sega_decrypt)(int, unsigned int*);

int NUM_SPEECH_SAMPLES;

extern char* gamename[];
extern int gamenum;

#define MAKE_RGB(r,g,b) 	((((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff))

INLINE UINT8 pal2bit(UINT8 bits)
{
	bits &= 3;
	return (bits << 6) | (bits << 4) | (bits << 2) | bits;
}

#define VECTOR_COLOR222(c) \
	MAKE_RGB(pal2bit((c) >> 4), pal2bit((c) >> 2), pal2bit((c) >> 0))
/*
static struct AY8910interface interface =
{
   1,
   1500000,
   { 8},
   {2},
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 }
};
*/

int sega_rotate = 0;
int vectorram_size = 0;
static long int* sinTable, * cosTable;
int nodecrypt = 0;
static int intensity;
//MAJOR VARIABLES
unsigned char mult1;
unsigned short result;
unsigned char ioSwitch;
long int mz80clockticks = 0;
int coin_in = 0;
int coin_count = 0;

//float r,g,b;
unsigned char* vectorram;
int sysReset = 0;
int spinvert = 0;
int toggle_mouse = 1;
int lastcolor = 0;

#define VECTOR_CLOCK		15468480			/* master clock */
#define U34_CLOCK			(VECTOR_CLOCK/3)	/* clock for interrupt chain */
#define VCL_CLOCK			(U34_CLOCK/2)		/* clock for vector generator */
#define U51_CLOCK			(VCL_CLOCK/16)		/* clock for phase generator */
#define IRQ_CLOCK			(U34_CLOCK/0x1f788)	/* 40Hz interrupt */

static int min_x, min_y;

static void sega_decrypt64(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x03)
	{
	case 0x00:
		/* A */
		i = b;
		break;
	case 0x01:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x02:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x03:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	default: i = b; wrlog("OH NO______________________"); break;
	}

	*lo = i;
}
/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0070                      */
/****************************************************************************/
static void sega_decrypt70(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x09)
	{
	case 0x00:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x01:
		/* A */
		i = b;
		break;
	case 0x08:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x09:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	}

	*lo = i;
}

/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0076                      */
/****************************************************************************/
static void sega_decrypt76(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x09)
	{
	case 0x00:
		/* A */
		i = b;
		break;
	case 0x01:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x08:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x09:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	}

	*lo = i;
}

/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0082                      */
/****************************************************************************/
static void sega_decrypt82(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x11)
	{
	case 0x00:
		/* A */
		i = b;
		break;
	case 0x01:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x10:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x11:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	}

	*lo = i;
}

/****************************************************************************/
/* MB 971031 - Emulate no Sega G80 security chip                            */
/****************************************************************************/
static void sega_decrypt0(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	*lo = i;
}

void sega_security(int chip)
{
	switch (chip)
	{
		//case 62:
			//sega_decrypt=sega_decrypt62;
			//break;
		//case 63:
			//sega_decrypt=sega_decrypt63;
			//break;
	case 64:
		sega_decrypt = sega_decrypt64;
		break;
	case 70:
		sega_decrypt = sega_decrypt70;
		break;
	case 76:
		sega_decrypt = sega_decrypt76;
		break;
	case 82:
		sega_decrypt = sega_decrypt82;
		break;
	default:
		sega_decrypt = sega_decrypt64;
		break;
	}
}

void Decrypt(int gamenum)
{//960a
	switch (gamenum) {
	case 6:
		cMZ80[0].z80Base[0x443b] = 0x21;
		cMZ80[0].z80Base[0xa3e] = 0x21;
		cMZ80[0].z80Base[0xa41] = 0x31;
		cMZ80[0].z80Base[0xa52] = 0x20;
		cMZ80[0].z80Base[0x32e7] = 0x23;
		cMZ80[0].z80Base[0x32ea] = 0x24;
		cMZ80[0].z80Base[0x32f2] = 0x25;
		cMZ80[0].z80Base[0x32f5] = 0x28;
		cMZ80[0].z80Base[0xb7a] = 0x40;
		cMZ80[0].z80Base[0xb7d] = 0x3a;
		cMZ80[0].z80Base[0xbb2] = 0x1c;
		cMZ80[0].z80Base[0xbb5] = 0x1d;
		cMZ80[0].z80Base[0xbb8] = 0x1e;
		cMZ80[0].z80Base[0x4423] = 0x21;
		cMZ80[0].z80Base[0x4495] = 0x21;
		cMZ80[0].z80Base[0x44bb] = 0x21;
		cMZ80[0].z80Base[0xbd2] = 0x36;
		cMZ80[0].z80Base[0xbd5] = 0x37;
		cMZ80[0].z80Base[0x1fa4] = 0x0;
		cMZ80[0].z80Base[0x2c44] = 0x15;
		cMZ80[0].z80Base[0x1fc9] = 0x0;
		cMZ80[0].z80Base[0xb69] = 0x20;
		cMZ80[0].z80Base[0x443b] = 0x21;
		cMZ80[0].z80Base[0xb88] = 0x21;
		cMZ80[0].z80Base[0xb8b] = 0x22;
		cMZ80[0].z80Base[0xa90] = 0x20;
		cMZ80[0].z80Base[0xc46] = 0x24;
		cMZ80[0].z80Base[0xc4a] = 0x23;
		cMZ80[0].z80Base[0x334c] = 0x28;
		cMZ80[0].z80Base[0x3366] = 0x2e;
		cMZ80[0].z80Base[0x336b] = 0x24;
		cMZ80[0].z80Base[0x331c] = 0x2e;
		cMZ80[0].z80Base[0x3324] = 0x2e;
		cMZ80[0].z80Base[0x343c] = 0x29;
		cMZ80[0].z80Base[0x343f] = 0x25;
		cMZ80[0].z80Base[0x347c] = 0x24;
		cMZ80[0].z80Base[0x342d] = 0x29;
		cMZ80[0].z80Base[0x326d] = 0xc;
		cMZ80[0].z80Base[0x327d] = 0xc;
		cMZ80[0].z80Base[0x4031] = 0x20;
		cMZ80[0].z80Base[0x348c] = 0x34;
		cMZ80[0].z80Base[0x3490] = 0x23;
		cMZ80[0].z80Base[0x3493] = 0x24;
		cMZ80[0].z80Base[0x34ac] = 0x23;
		cMZ80[0].z80Base[0x34b6] = 0x34;
		cMZ80[0].z80Base[0x34f0] = 0x2e;
		cMZ80[0].z80Base[0x34f5] = 0x24;
		cMZ80[0].z80Base[0x3510] = 0x2e;
		cMZ80[0].z80Base[0x3517] = 0x2e;
		cMZ80[0].z80Base[0x3529] = 0x24;
		cMZ80[0].z80Base[0x352f] = 0x23;
		cMZ80[0].z80Base[0x3532] = 0x24;
		cMZ80[0].z80Base[0x213e] = 0x31;
		cMZ80[0].z80Base[0x217d] = 0x21;
		break;

	case 5: {//SPACEFURY
		cMZ80[0].z80Base[0x824] = 0xaf;
		cMZ80[0].z80Base[0x825] = 0xdd;
		cMZ80[0].z80Base[0x826] = 0xb2;
		cMZ80[0].z80Base[0x827] = 0xab;
		cMZ80[0].z80Base[0x834] = 0xd7;
		cMZ80[0].z80Base[0x835] = 0xc7;
		cMZ80[0].z80Base[0x839] = 0xe6;
		cMZ80[0].z80Base[0x83a] = 0x9f;
		cMZ80[0].z80Base[0x83b] = 0x66;
		cMZ80[0].z80Base[0x83c] = 0x93;
		cMZ80[0].z80Base[0x847] = 0xc4;
		cMZ80[0].z80Base[0x848] = 0xfd;
		cMZ80[0].z80Base[0x849] = 0xeb;
		cMZ80[0].z80Base[0x84a] = 0xf9;
		cMZ80[0].z80Base[0x84e] = 0xaa;
		cMZ80[0].z80Base[0x84f] = 0x45;
		cMZ80[0].z80Base[0x857] = 0x24;
		cMZ80[0].z80Base[0x858] = 0x03;

		cMZ80[0].z80Base[0xa3c] = 0x21;
		cMZ80[0].z80Base[0xa3f] = 0x31;
		cMZ80[0].z80Base[0xa50] = 0x20;
		cMZ80[0].z80Base[0xa8e] = 0x20;
		cMZ80[0].z80Base[0xb67] = 0x20;
		cMZ80[0].z80Base[0xb78] = 0x40;
		cMZ80[0].z80Base[0xb7b] = 0x3a;
		cMZ80[0].z80Base[0xb86] = 0x21;
		cMZ80[0].z80Base[0xbb0] = 0x1c;
		cMZ80[0].z80Base[0xbb3] = 0x1d;
		cMZ80[0].z80Base[0xbb6] = 0x1e;
		cMZ80[0].z80Base[0xbd0] = 0x36;
		cMZ80[0].z80Base[0xbd3] = 0x37;
		cMZ80[0].z80Base[0xc44] = 0x24;
		cMZ80[0].z80Base[0xc48] = 0x23;
		//962a
		cMZ80[0].z80Base[0x1fa2] = 0x00;
		cMZ80[0].z80Base[0x1fc7] = 0x00;
		//963a

		cMZ80[0].z80Base[0x213c] = 0x31;
		cMZ80[0].z80Base[0x2147] = 0x31;
		cMZ80[0].z80Base[0x216b] = 0x21;
		cMZ80[0].z80Base[0x217b] = 0x21;
		cMZ80[0].z80Base[0x21d0] = 0x20;
		cMZ80[0].z80Base[0x2672] = 0x34;
		cMZ80[0].z80Base[0x2677] = 0x35;
		cMZ80[0].z80Base[0x2687] = 0x34;
		//965
		cMZ80[0].z80Base[0x326a] = 0x0c;
		cMZ80[0].z80Base[0x327a] = 0x0c;
		cMZ80[0].z80Base[0x32e4] = 0x23;
		cMZ80[0].z80Base[0x32e7] = 0x24;
		cMZ80[0].z80Base[0x32ef] = 0x25;
		cMZ80[0].z80Base[0x32f2] = 0x28;
		cMZ80[0].z80Base[0x3363] = 0x2e;
		cMZ80[0].z80Base[0x3368] = 0x24;
		cMZ80[0].z80Base[0x342a] = 0x29;
		cMZ80[0].z80Base[0x343c] = 0x25;
		cMZ80[0].z80Base[0x3490] = 0x24;
		cMZ80[0].z80Base[0x34b3] = 0x34;
		cMZ80[0].z80Base[0x34f2] = 0x24;
		cMZ80[0].z80Base[0x3514] = 0x2e;
		cMZ80[0].z80Base[0x3526] = 0x24;
		cMZ80[0].z80Base[0x352c] = 0x23;
		cMZ80[0].z80Base[0x352f] = 0x24;
		//966
		cMZ80[0].z80Base[0x39a8] = 0xf8;
		cMZ80[0].z80Base[0x39ab] = 0xf9;
		cMZ80[0].z80Base[0x39ae] = 0xfa;
		//967
		cMZ80[0].z80Base[0x4412] = 0x21;
		cMZ80[0].z80Base[0x4423] = 0x21;
		cMZ80[0].z80Base[0x443b] = 0x21;
		cMZ80[0].z80Base[0x44bb] = 0x21;

		break;
	}

	case 0: {//SPACEFURY
		cMZ80[0].z80Base[0xa5f] = 0x21;
		cMZ80[0].z80Base[0xa62] = 0x31;
		cMZ80[0].z80Base[0xa73] = 0x20;
		cMZ80[0].z80Base[0xb8a] = 0x20;

		cMZ80[0].z80Base[0xba5] = 0x3a;
		cMZ80[0].z80Base[0xbda] = 0x1c;
		cMZ80[0].z80Base[0xbdd] = 0x1d;
		cMZ80[0].z80Base[0xbfd] = 0x37;

		cMZ80[0].z80Base[0xbb0] = 0x21;
		cMZ80[0].z80Base[0xbb3] = 0x22;
		cMZ80[0].z80Base[0xbda] = 0x1c;
		cMZ80[0].z80Base[0xbe0] = 0x1e;
		cMZ80[0].z80Base[0xbfa] = 0x36;
		cMZ80[0].z80Base[0xc86] = 0x24;
		cMZ80[0].z80Base[0xc8a] = 0x23;
		cMZ80[0].z80Base[0x1fc0] = 0x0;

		cMZ80[0].z80Base[0x215a] = 0x31;
		cMZ80[0].z80Base[0x21a7] = 0x21;
		cMZ80[0].z80Base[0x21b7] = 0x21;
		cMZ80[0].z80Base[0x26ae] = 0x34;
		cMZ80[0].z80Base[0x26b3] = 0x35;
		cMZ80[0].z80Base[0x26c3] = 0x34;
		cMZ80[0].z80Base[0x2c7d] = 0x15;
		cMZ80[0].z80Base[0x32bb] = 0xc;
		cMZ80[0].z80Base[0x32cb] = 0xc;
		cMZ80[0].z80Base[0x3335] = 0x23;

		cMZ80[0].z80Base[0x3338] = 0x24;
		cMZ80[0].z80Base[0x3340] = 0x25;
		cMZ80[0].z80Base[0x3343] = 0x28;
		cMZ80[0].z80Base[0x339a] = 0x28;
		cMZ80[0].z80Base[0x33b4] = 0x2e;
		cMZ80[0].z80Base[0x336a] = 0x2e;
		cMZ80[0].z80Base[0x3372] = 0x2e;
		cMZ80[0].z80Base[0x348a] = 0x29;
		cMZ80[0].z80Base[0x34ca] = 0x24;
		cMZ80[0].z80Base[0x347b] = 0x29;
		cMZ80[0].z80Base[0x34da] = 0x34;
		cMZ80[0].z80Base[0x34de] = 0x23;
		cMZ80[0].z80Base[0x34fa] = 0x23;
		cMZ80[0].z80Base[0x3504] = 0x34;
		cMZ80[0].z80Base[0x353e] = 0x2e;
		cMZ80[0].z80Base[0x3543] = 0x24;
		cMZ80[0].z80Base[0x355e] = 0x2e;
		cMZ80[0].z80Base[0x3577] = 0x24;
		cMZ80[0].z80Base[0x3580] = 0x24;

		cMZ80[0].z80Base[0x4024] = 0x20;
		cMZ80[0].z80Base[0x4412] = 0x21;
		cMZ80[0].z80Base[0x4484] = 0x21;
		cMZ80[0].z80Base[0x44aa] = 0x21;
		cMZ80[0].z80Base[0x442a] = 0x21;

		break; }

	case 2: { //zektor
		cMZ80[0].z80Base[0x9c90] = 0x21;
		cMZ80[0].z80Base[0xcbb] = 0x4c;
		cMZ80[0].z80Base[0xccc] = 0x20;
		cMZ80[0].z80Base[0x8ff1] = 0x34;
		cMZ80[0].z80Base[0x8ff4] = 0x35;
		cMZ80[0].z80Base[0xe3a] = 0x51;
		cMZ80[0].z80Base[0xe72] = 0x31;
		cMZ80[0].z80Base[0xe75] = 0x32;
		cMZ80[0].z80Base[0xe78] = 0x33;
		cMZ80[0].z80Base[0x8e1b] = 0x5;
		cMZ80[0].z80Base[0xdb9] = 0x1;
		cMZ80[0].z80Base[0x9c78] = 0x21;
		cMZ80[0].z80Base[0x9cea] = 0x21;
		cMZ80[0].z80Base[0x9d10] = 0x21;
		cMZ80[0].z80Base[0x6b5b] = 0x0;
		cMZ80[0].z80Base[0x6b77] = 0x0;
		cMZ80[0].z80Base[0x79bc] = 0x4;
		cMZ80[0].z80Base[0xf8a] = 0x35;
		cMZ80[0].z80Base[0xf8e] = 0x34;
		cMZ80[0].z80Base[0x9055] = 0x38;
		cMZ80[0].z80Base[0x905a] = 0x35;
		cMZ80[0].z80Base[0x9019] = 0x38;
		cMZ80[0].z80Base[0x907d] = 0x35;
		cMZ80[0].z80Base[0x8df0] = 0x5;
		cMZ80[0].z80Base[0x8e00] = 0x5;
		cMZ80[0].z80Base[0x890c] = 0x4b;
		cMZ80[0].z80Base[0x8948] = 0x2;
		cMZ80[0].z80Base[0x8ed7] = 0x23;
		cMZ80[0].z80Base[0x7010] = 0x23;
		cMZ80[0].z80Base[0x6d59] = 0x1;
		cMZ80[0].z80Base[0x70da] = 0x3;
		cMZ80[0].z80Base[0x988a] = 0x20;
		cMZ80[0].z80Base[0x90a0] = 0x4d;
		cMZ80[0].z80Base[0x90a4] = 0x34;
		cMZ80[0].z80Base[0x90c0] = 0x34;
		cMZ80[0].z80Base[0x90ca] = 0x4d;
		cMZ80[0].z80Base[0x9104] = 0x38;
		cMZ80[0].z80Base[0x9124] = 0x38;
		cMZ80[0].z80Base[0x913d] = 0x35;
		cMZ80[0].z80Base[0x9146] = 0x35;

		break; }

	case 3: {//startrek
		cMZ80[0].z80Base[0x6a58] = 0x3;
		cMZ80[0].z80Base[0x6a5e] = 0x4;
		cMZ80[0].z80Base[0x486c] = 0x2a;
		cMZ80[0].z80Base[0xe93] = 0x35;
		cMZ80[0].z80Base[0xe96] = 0x36;
		cMZ80[0].z80Base[0xe9b] = 0x6;
		cMZ80[0].z80Base[0xeb0] = 0x17;
		cMZ80[0].z80Base[0xeb3] = 0x18;
		cMZ80[0].z80Base[0xeb6] = 0x1a;
		cMZ80[0].z80Base[0xec2] = 0xd9;
		cMZ80[0].z80Base[0x2d7b] = 0x2a;
		cMZ80[0].z80Base[0x2d7f] = 0x29;
		cMZ80[0].z80Base[0x48ce] = 0x2a;
		cMZ80[0].z80Base[0xf6c] = 0x6;
		cMZ80[0].z80Base[0xf76] = 0x7;
		cMZ80[0].z80Base[0x105e] = 0x15;
		cMZ80[0].z80Base[0x12f2] = 0xd9;
		cMZ80[0].z80Base[0x290c] = 0x8;
		cMZ80[0].z80Base[0x2912] = 0x5;
		cMZ80[0].z80Base[0x291c] = 0x7;
		cMZ80[0].z80Base[0x2d9b] = 0x22;
		cMZ80[0].z80Base[0x2cc8] = 0x6e;
		cMZ80[0].z80Base[0x29ab] = 0xa;
		cMZ80[0].z80Base[0x350b] = 0x18;
		cMZ80[0].z80Base[0x3537] = 0x3b;
		cMZ80[0].z80Base[0x3576] = 0x3b;
		cMZ80[0].z80Base[0x6abe] = 0x3;
		cMZ80[0].z80Base[0x6ace] = 0x4;
		cMZ80[0].z80Base[0x3564] = 0x4f;
		cMZ80[0].z80Base[0x2d3a] = 0x23;
		cMZ80[0].z80Base[0x2d3f] = 0x50;
		cMZ80[0].z80Base[0x2d42] = 0x5a;
		cMZ80[0].z80Base[0x2d47] = 0x59;
		cMZ80[0].z80Base[0x2d4a] = 0x63;
		cMZ80[0].z80Base[0x2d07] = 0x59;
		cMZ80[0].z80Base[0x2d0a] = 0x63;
		cMZ80[0].z80Base[0x2ed0] = 0x17;
		cMZ80[0].z80Base[0x300b] = 0x1d;
		cMZ80[0].z80Base[0x1470] = 0x2;
		cMZ80[0].z80Base[0x32ea] = 0x17;
		cMZ80[0].z80Base[0x32f3] = 0x18;
		cMZ80[0].z80Base[0x334a] = 0x9a;
		cMZ80[0].z80Base[0x33df] = 0x2;
		cMZ80[0].z80Base[0x2e9b] = 0x22;
		cMZ80[0].z80Base[0x2f33] = 0x6;
		cMZ80[0].z80Base[0x48fc] = 0x2a;
		cMZ80[0].z80Base[0x491f] = 0x30;
		cMZ80[0].z80Base[0x495e] = 0x2a;
		cMZ80[0].z80Base[0x4980] = 0x2d;
		cMZ80[0].z80Base[0x4992] = 0x2a;
		cMZ80[0].z80Base[0x4998] = 0x29;
		cMZ80[0].z80Base[0x499b] = 0x2a;
		cMZ80[0].z80Base[0x3b38] = 0x32;
		cMZ80[0].z80Base[0x3d2a] = 0x32;
		cMZ80[0].z80Base[0x3d3b] = 0x94;
		cMZ80[0].z80Base[0x3dab] = 0x17;
		cMZ80[0].z80Base[0x3dae] = 0x18;
		cMZ80[0].z80Base[0x3db7] = 0x2;
		cMZ80[0].z80Base[0x3dbf] = 0x6;
		cMZ80[0].z80Base[0x3dc7] = 0x10;
		cMZ80[0].z80Base[0x3e0a] = 0x10;
		cMZ80[0].z80Base[0x3eee] = 0x17;
		cMZ80[0].z80Base[0x4053] = 0x13;
		cMZ80[0].z80Base[0x4058] = 0x0;
		cMZ80[0].z80Base[0x4064] = 0xa;
		cMZ80[0].z80Base[0x4070] = 0x3c;
		cMZ80[0].z80Base[0x4073] = 0x5a;
		break;
	}
	}
}

/*

	Vector system is clocked by a 15-phase clock.

	The counter is a LS161 4-bit binary counter at U51, and its output
	goes to a LS154 1-of-16 decoder at U50.

	Each phase various things happen. The phases are:

	 0 -> (sheet 7/7) clocks CD7 in

	 1 -> (sheet 5/7) loads CD0-7 into counters at U15/U16
		  (sheet 6/7) clear LS175 flip flops at U35, U36, U37, U38

	 2 -> (sheet 5/7) loads CD0-3 into counter at U17

	 3 -> (sheet 5/7) loads CD0-7 into counters at U18/U19

	 4 -> (sheet 5/7) loads CD0-3 into counter at U20

	 5 ->

	 6 ->

	 7 -> (sheet 6/7) at end, latches CD0-7 into LS374 tri-state flip flop at U55 (SYM angle)

	 8 -> (sheet 6/7) at end, latches CD0-1 into LS74 flip flops at U26 (upper SYM angle)

	 9 -> (sheet 4/7) at end, latches CD0-7 into 25LS14 multiplier X input at U8 (scale)

	10 -> (sheet 4/7) at end, latches CD0-CD7 into LS374 tri-state flip flop at U2 (attributes)
		  (sheet 7/7) at end, latches CD7 into U52 (low), which sets the preload value
			for the LS161 at U51 to be either 0 (if CD7==1) or 10 (if CD7==0)

	11 -> (sheet 4/7) at end, starts multiply circuit

	12 -> (sheet 6/7) at end, latches CD0-7 into LS374 tri-state flip flop at U56 (VEC angle)

	13 -> (sheet 6/7) at end, latches output from 2708 PROM into tri-state flip flop at U48
		  (sheet 6/7) at end, latches bit $200 of angle into D/UX output

	14 -> (sheet 6/7) at end, latches output from 2708 PROM into tri-state flip flop at U49
		  (sheet 6/7) at end, latches bit $200 of angle into D/UY output
		  (sheet 7/7) signals /PE on the LS161 at U51, loading the new value for the state clock
		  (sheet 7/7) sets up the DRAW signal to clock on the next VCL edge

	15 ->

	PROM inputs:
		A0 = GND
		A1-A8 = sum of VEC angle and SYM angle (low 8 bits)
		A9 = sum of bit 8 of VEC angle and SYM angle, plus 1 for phase 13

*/

INLINE int adjust_xy(int rawx, int rawy, int* outx, int* outy)
{
	int clipped = FALSE;

	/* first apply the XOR at 0x200 */
	*outx = (rawx & 0x7ff) ^ 0x200;
	*outy = (rawy & 0x7ff) ^ 0x200;

	/* apply clipping logic to X */
	if ((*outx & 0x600) == 0x200)
		*outx = 0x000, clipped = TRUE;
	else if ((*outx & 0x600) == 0x400)
		*outx = 0x3ff, clipped = TRUE;
	else
		*outx &= 0x3ff;

	/* apply clipping logic to Y */
	if ((*outy & 0x600) == 0x200)
		*outy = 0x000, clipped = TRUE;
	else if ((*outy & 0x600) == 0x400)
		*outy = 0x3ff, clipped = TRUE;
	else
		*outy &= 0x3ff;

	/* convert into .16 values */
	*outx = (*outx - (min_x - 512)) << 16;
	*outy = (*outy - (min_y - 512)) << 16;
	return clipped;
}

void sega_generate_vector_list(void)
{
	UINT8* sintable = GI[1];
	double total_time = 1.0 / (double)IRQ_CLOCK;
	UINT16 symaddr = 0;

	int red = 0;
	int green = 0;
	int blue = 0;

	int sx, sy, ex, ey;
	sx = 0;
	sy = 0;
	ex = 0;
	ey = 0;

	cache_clear();
	/* Loop until we run out of time. */
	while (total_time > 0)
	{
		UINT16 curx, cury, xaccum, yaccum;
		UINT16 vecaddr, symangle;
		UINT8 scale, draw;

		/* The "draw" flag is clocked at the end of phase 0. */
		draw = vectorram[symaddr++ & 0xfff];

		/* The low byte of the X coordinate is latched into the        */
		/* up/down counters at U15/U16 during phase 1. */
		curx = vectorram[symaddr++ & 0xfff];

		/* The low 3 bits of the high byte of the X coordinate are     */
		/* latched into the up/down counter at U17 during phase 2.     */
		/* Bit 2 of the input is latched as both bit 2 and 3. */
		curx |= (vectorram[symaddr++ & 0xfff] & 7) << 8;
		curx |= (curx << 1) & 0x800;

		/* The low byte of the Y coordinate is latched into the        */
		/* up/down counters at U18/U19 during phase 3. */
		cury = vectorram[symaddr++ & 0xfff];

		/* The low 3 bits of the high byte of the X coordinate are     */
		/* latched into the up/down counter at U17 during phase 4.     */
		/* Bit 2 of the input is latched as both bit 2 and 3. */
		cury |= (vectorram[symaddr++ & 0xfff] & 7) << 8;
		cury |= (cury << 1) & 0x800;

		/* The low byte of the vector address is latched into the      */
		/* counters at U10/U11 during phase 5. */
		vecaddr = vectorram[symaddr++ & 0xfff];

		/* The low 4 bits of the high byte of the vector address is    */
		/* latched into the counter at U12 during phase 6. */
		vecaddr |= (vectorram[symaddr++ & 0xfff] & 0xf) << 8;

		/* The low byte of the symbol angle is latched into the tri-   */
		/* state flip flop at U55 at the end of phase 7. */
		symangle = vectorram[symaddr++ & 0xfff];

		/* The low 2 bits of the high byte of the symbol angle are     */
		/* latched into flip flops at U26 at the end of phase 8. */
		symangle |= (vectorram[symaddr++ & 0xfff] & 3) << 8;

		/* The scale is latched in phase 9 as the X input to the       */
		/* 25LS14 multiplier at U8. */
		scale = vectorram[symaddr++ & 0xfff];

		/* Account for the 10 phases so far. */
		total_time -= 10.0 / (double)U51_CLOCK;

		/* Skip the rest if we're not drawing this symbol. */
		if (draw & 1)
		{
			int adjx, adjy, clipped;

			/* Add a starting point to the vector list. */
			clipped = adjust_xy(curx, cury, &adjx, &adjy);
			if (!clipped) sx = adjx; sy = adjy;
			//vector_add_point(adjx, adjy, 0, 0);

		/* Loop until we run out of time. */
			while (total_time > 0)
			{
				UINT16 vecangle, length, deltax, deltay;
				UINT8 attrib, intensity;
				UINT32 color;

				/* The 'attribute' byte is latched at the end of phase 10 into */
				/* the tri-state flip flop at U2. The low bit controls whether */
				/* or not the beam is enabled. Bits 1-6 control the RGB color  */
				/* (2 bits per component). In addition, bit 7 of this value is */
				/* latched into U52, which controls the pre-load value for the */
				/* phase generator. If bit 7 is high, then the phase generator */
				/* will reset back to 0 and draw a new symbol; if bit 7 is low */
				/* the phase generator will reset back to 10 and draw another  */
				/* vector. */
				attrib = vectorram[vecaddr++ & 0xfff];

				/* The length of the vector is loaded into the shift registers */
				/* at U6/U7 during phase 11. During phase 12, the 25LS14       */
				/* multiplier at U8 is used to multiply the length by the      */
				/* scale that was loaded during phase 9. The length is clocked */
				/* bit by bit out of U6/U7 and the result is clocked into the  */
				/* other side. After the multiply, the 9 MSBs are loaded into  */
				/* the counter chain at U15/16/17 and are used to count how    */
				/* long to draw the vector. */
				length = (vectorram[vecaddr++ & 0xfff] * scale) >> 7;

				/* The vector angle low byte is latched at the end of phase 12 */
				/* into the tri-state flip flop at U56. */
				vecangle = vectorram[vecaddr++ & 0xfff];

				/* The vector angle high byte is preset on the CD bus during   */
				/* phases 13 and 14, and is used as inputs to the adder at     */
				/* U46. */
				vecangle |= (vectorram[vecaddr++ & 0xfff] & 3) << 8;

				/* The X increment value is looked up first (phase 13). The    */
				/* sum of the latched symbol angle and the vector angle is     */
				/* used as input to the PROM at U39. A0 is tied to ground.     */
				/* A1-A9 map to bits 0-8 of the summed angles. The output from */
				/* the PROM is latched into U48. */
				deltax = sintable[((vecangle + symangle) & 0x1ff) << 1];

				/* The Y increment value is looked up second (phase 14). The   */
				/* angle sum is used once again as the input to the PROM, but  */
				/* this time an additional 0x100 is effectively added to it    */
				/* before it is used; this separates sin from cos. The output  */
				/* from the PROM is latched into U49. */
				deltay = sintable[((vecangle + symangle + 0x100) & 0x1ff) << 1];
				/* Account for the 4 phases for data fetching. */
				total_time -= 4.0 / (double)U51_CLOCK;

				/* Compute color/intensity values from the attributes */
				color = VECTOR_COLOR222((attrib >> 1) & 0x3f);
				//wrlog("color %x",color);
				blue = color & 0xff;
				green = (color >> 8) & 0xff;
				red = (color >> 16) & 0xff;

				if ((attrib & 1) && color)
					intensity = 0xff;
				else
					intensity = 0;

				/* Loop over the length of the vector. */
				clipped = adjust_xy(curx, cury, &adjx, &adjy);
				sx = adjx;
				sy = adjy;
				xaccum = 0;
				yaccum = 0;
				while (length-- != 0 && total_time > 0)
				{
					int newclip;

					/* The adders at U44/U45 are used as X accumulators. The value */
					/* from U48 is repeatedly added to itself here. The carry out  */
					/* of bit 8 clocks the up/down counters at U15/U16/U17. Bit 7  */
					/* of the input value from U48 is used as a carry in to round  */
					/* small values downward and larger values upward. */
					xaccum += deltax + (deltax >> 7);

					/* Bit 9 of the summed angles controls the direction the up/   */
					/* down counters at U15/U16/U17. */
					if (((vecangle + symangle) & 0x200) == 0)
						curx += xaccum >> 8;
					else
						curx -= xaccum >> 8;
					xaccum &= 0xff;

					/* The adders at U46/U47 are used as Y accumulators. The value */
					/* from U49 is repeatedly added to itself here. The carry out  */
					/* of bit 8 clocks the up/down counters at U18/U19/U20. Bit 7  */
					/* of the input value from U49 is used as a carry in to round  */
					/* small values downward and larger values upward. */
					yaccum += deltay + (deltay >> 7);

					/* Bit 9 of the summed angles controls the direction the up/   */
					/* down counters at U18/U19/U20. */
					if (((vecangle + symangle + 0x100) & 0x200) == 0)
						cury += yaccum >> 8;
					else
						cury -= yaccum >> 8;
					yaccum &= 0xff;

					/* Apply the clipping from the DAC circuit. If the values clip */
					/* the beam is turned off, but the computations continue right */
					/* on going. */
					newclip = adjust_xy(curx, cury, &adjx, &adjy);
					if (newclip != clipped)
					{
						/* if we're just becoming unclipped, add an empty point */
						if (!newclip) { ; }//sx=adjx;sy=adjy;}
						//vector_add_point(adjx, adjy, 0, 0);

					/* otherwise, add a colored point */
						else {
							if (intensity) {
								if (gamenum == TACSCAN) {
									add_color_line((sy >> 16), (sx >> 16), (adjy >> 16), (adjx >> 16), red, green, blue);
									add_color_point((sy >> 16), (sx >> 16), red, green, blue);
									add_color_point((adjy >> 16), (adjx >> 16), red, green, blue);
								}

								else {
									add_color_line((sx >> 16), (sy >> 16), (adjx >> 16), (adjy >> 16), red, green, blue);
									add_color_point((sx >> 16), (sy >> 16), red, green, blue);
									add_color_point((adjx >> 16), (adjy >> 16), red, green, blue);
								}
							}

							//wrlog("LINE from sx %d sy %d to endx %d endy %d",sx>>16,sy>>16,adjx>>16,adjy>>16);
						}
					}
					clipped = newclip;

					/* account for vector drawing time */
					total_time -= 1.0 / (double)VCL_CLOCK;
				}

				/* We're done; if we are not clipped, add a final point. */
				if (!clipped) {
					if (intensity) {
						if (gamenum == TACSCAN) {
							add_color_line((sy >> 16), (sx >> 16), (adjy >> 16), (adjx >> 16), red, green, blue);
							add_color_point((sy >> 16), (sx >> 16), red, green, blue);
							add_color_point((adjy >> 16), (adjx >> 16), red, green, blue);
						}

						else {
							add_color_line((sx >> 16), (sy >> 16), (adjx >> 16), (adjy >> 16), red, green, blue);
							add_color_point((sx >> 16), (sy >> 16), red, green, blue);
							add_color_point((adjx >> 16), (adjy >> 16), red, green, blue);
						}
					}
				}
				//vector_add_point(adjx, adjy, color, intensity);

			/* if the high bit of the attribute is set, we break out of   */
			/* this loop and fetch another symbol */
				if (attrib & 0x80)
					break;
			}
		}

		/* if the high bit of the draw flag is set, we break out of this loop */
		/* and stop the rendering altogether for this frame. */
		if (draw & 0x80)
			break;
	}
}

/***************************************************************************

  Stop the video hardware emulation.

***************************************************************************/
/*
static int coin_routine(int bits)
{
   if (coin_in > 0 && coin_in < 4){ bitclr(bits, 0x20);  coin_in++;}

  return bits;
}
*/

static void BWVectorGenerator(void)
{
	sega_generate_vector_list();
}

/*
static void swap(int *i, int *j)
{
	int t;
	t = *i;
	*i = *j;
	*j = t;
}
*/
READ_HANDLER(VectorRam_r)
{
	return vectorram[address - 0xe000];
}

READ_HANDLER(SoundRam)
{
	return 0x00;
}

WRITE_HANDLER(Vector_Write)
{
	int op = 0;
	int page = 0;
	int val = 0; //unsigned
	int offset = 0;
	unsigned int bad = 0;//
	int test = 0;

	val = mz80GetRegisterValue(0, CPUREG_PC);
	//wrlog("OP Handler, Z80PC is %x",mz80GetRegisterValue(temp, CPUREG_PC));
	test = val;
	val = val - 3;

	op = cMZ80[0].z80Base[val] & 0xff;
	//wrlog("current %x -3 val %x",val,test);
	if (op == 0x32) {
		bad = cMZ80[0].z80Base[(val + 1)] & 0xFF;
		page = (cMZ80[0].z80Base[(val + 2)] & 0xFF) << 8;
		(*sega_decrypt)(val, &bad);

		offset = (page & 0xFF00) | (bad & 0x00FF);
		//wrlog("VAL %x, OP %x BAD %x PAGE %x ADDRESS %x OFFSET %x",val,op,bad,page,address,offset);

		if (nodecrypt) { offset = address; }
		else { address = offset; }
		//if ((offset>=0x0000) && (offset<=0xbfff)){;}
		//if (offset >= 0xc800 && offset <= 0xdfff ) {cMZ80[0]->z80Base[offset]=data;} //memory+soundram
		//if (offset >= 0xe000 && offset <= 0xefff ) {vectorram[offset-0xe000]=data;cMZ80[0]->z80Base[offset]=data;} //vectorram
	}

	if ((address >= 0x0000) && (address <= 0xbfff)) { ; }
	else if (address >= 0xc800 && address <= 0xdfff) { cMZ80[0].z80Base[address] = data; }
	else if (address >= 0xe000 && address <= 0xefff) { vectorram[address - 0xe000] = data; cMZ80[0].z80Base[address] = data; }
	//if (op!=0x32){
	// if ((address>=0x0000) && (address<=0xbfff)){;}
	 //if (address >= 0xc800 && address <= 0xdfff ){z80.z80Base[address]=data;}
	 //if (address >= 0xe000 && address <= 0xefff ){vectorram[address-0xe000]=data;z80.z80Base[address]=data; }
	//}
}

PORT_READ_HANDLER(sega_sh_r)
{
	if (sample_playing(0))
		return 0x81;
	else
		return 0x80;
}
PORT_READ_HANDLER(sega_mult_r)
{
	int c;

	c = result & 0xff;
	result >>= 8;
	return (c);
}

/***************************************************************************

  The Sega games store the DIP switches in a very mangled format that's
  not directly useable by MAME.  This function mangles the DIP switches
  into a format that can be used.

  Original format:
  Port 0 - 2-4, 2-8, 1-4, 1-8
  Port 1 - 2-3, 2-7, 1-3, 1-7
  Port 2 - 2-2, 2-6, 1-2, 1-6
  Port 3 - 2-1, 2-5, 1-1, 1-5
  MAME format:
  Port 6 - 1-1, 1-2, 1-3, 1-4, 1-5, 1-6, 1-7, 1-8
  Port 7 - 2-1, 2-2, 2-3, 2-4, 2-5, 2-6, 2-7, 2-8
***************************************************************************/

PORT_READ_HANDLER(sega_ports_r)
{
	int dip1, dip2;

	dip1 = input_port_6_r(port);
	dip2 = input_port_7_r(port);

	switch (port)
	{
	case 0xf8:
		return ((input_port_0_r(0) & 0xF0) | ((dip2 & 0x08) >> 3) |
			((dip2 & 0x80) >> 6) | ((dip1 & 0x08) >> 1) | ((dip1 & 0x80) >> 4));
	case 0xf9:
		return ((input_port_1_r(0) & 0xF0) | ((dip2 & 0x04) >> 2) |
			((dip2 & 0x40) >> 5) | ((dip1 & 0x04) >> 0) | ((dip1 & 0x40) >> 3));
	case 0xfa:
		return ((input_port_2_r(0) & 0xF0) | ((dip2 & 0x02) >> 1) |
			((dip2 & 0x20) >> 4) | ((dip1 & 0x02) << 1) | ((dip1 & 0x20) >> 2));
	case 0xfb:
		return ((input_port_3_r(0) & 0xF0) | ((dip2 & 0x01) >> 0) |
			((dip2 & 0x10) >> 3) | ((dip1 & 0x01) << 2) | ((dip1 & 0x10) >> 1));
	}

	return 0;
}

PORT_READ_HANDLER(elim_in2_r)
{
	return input_port_4_r(port);
}

PORT_READ_HANDLER(sega_IN4_r) {
	/*
	 * The values returned are always increasing.  That is, regardless of whether
	 * you turn the spinner left or right, the self-test should always show the
	 * number as increasing. The direction is only reflected in the least
	 * significant bit.
	 */

	int delta;
	static int sign;
	static int spinner;

	if (ioSwitch & 1) /* ioSwitch = 0x01 or 0xff */
		return readinputportbytag("IN4");

	/* else ioSwitch = 0xfe */

	/* I'm sure this can be further simplified ;-) BW */
	delta = readinputportbytag("IN8");
	if (delta != 0)
	{
		sign = delta >> 7;
		if (sign)
			delta = 0x80 - delta;
		spinner += delta;
	}
	return (~((spinner << 1) | sign));
}

PORT_READ_HANDLER(elim4_IN4_r)
{
	/* If the ioPort ($f8) is 0x1f, we're reading the 4 coin inputs.    */
	/* If the ioPort ($f8) is 0x1e, we're reading player 3 & 4 controls.*/

	if (ioSwitch == 0x1e)
		return readinputport(4);
	if (ioSwitch == 0x1f)
		return readinputport(8);
	return (0);
}

PORT_WRITE_HANDLER(sega_switch_w)
{
	ioSwitch = data;
}

PORT_WRITE_HANDLER(sega_mult1_w)
{
	mult1 = data;
}

PORT_WRITE_HANDLER(sega_mult2_w)
{
	result = mult1 * data;
}

PORT_WRITE_HANDLER(sega_coin_counter_w)
{
	;
}

///////////////////////  MAIN LOOP /////////////////////////////////////
void run_segag80(void)
{
	//wrlog("FRAME-------------------------------");
		//    if (KeyCheck(config.ktest))     {set_pending_interrupt(INT_TYPE_NMI, 0);}
		//	if (KeyCheck(config.kcoin1) || KeyCheck(config.kcoin2) )	{coin_in=1;	}
		 //   if (KeyCheck(config.kreset))    {cpu_needs_reset(0);}

	BWVectorGenerator();
	if (gamenum == TACSCAN)
	{
		tacscan_sh_update();
	}
	else sega_sh_update();
}

struct MemoryWriteByte SegaWrite[] =
{
	//{ 0x0000,  0xc7ff, ProgRom },
	{ 0x0000,  0xffff, Vector_Write },
	//{ 0xe000,  0xefff, Vector_Write },
	{(UINT32)-1,	(UINT32)-1,		NULL}
};

struct MemoryReadByte SegaRead[] =
{
	{ 0xe000, 0xefff, VectorRam_r },
	{ 0xd000, 0xdfff, SoundRam },
	{ 0xf000, 0xffff, SoundRam },
	{(UINT32)-1,	(UINT32)-1,		NULL}
};

PORT_READ(SpacfuryPortRead)
PORT_ADDR(0x3f, 0x3f, sega_sh_r)
PORT_ADDR(0xbe, 0xbe, sega_mult_r)
PORT_ADDR(0xf8, 0xfc, sega_ports_r)
PORT_END

PORT_READ(G80SpinPortRead)
PORT_ADDR(0x3f, 0x3f, sega_sh_r)
PORT_ADDR(0xbe, 0xbe, sega_mult_r)
PORT_ADDR(0xf8, 0xfb, sega_ports_r)
PORT_ADDR(0xfc, 0xfc, sega_IN4_r)
PORT_END

PORT_READ(Elim2PortRead)
PORT_ADDR(0x3f, 0x3f, sega_sh_r)
PORT_ADDR(0xbe, 0xbe, sega_mult_r)
PORT_ADDR(0xf8, 0xfb, sega_ports_r)
PORT_ADDR(0xfc, 0xfc, elim_in2_r)
PORT_END

PORT_READ(Elim4PortRead)
PORT_ADDR(0x3f, 0x3f, sega_sh_r)
PORT_ADDR(0xbe, 0xbe, sega_mult_r)
PORT_ADDR(0xf8, 0xfb, sega_ports_r)
PORT_ADDR(0xfc, 0xfc, elim4_IN4_r)
PORT_END

PORT_WRITE(SpacfuryPortWrite)
PORT_ADDR(0x38, 0x38, sega_sh_speech_w)
PORT_ADDR(0x3e, 0x3e, spacfury1_sh_w)
PORT_ADDR(0x3f, 0x3f, spacfury2_sh_w)
PORT_ADDR(0xbd, 0xbd, sega_mult1_w)
PORT_ADDR(0xbe, 0xbe, sega_mult2_w)
PORT_ADDR(0xf8, 0xf8, sega_coin_counter_w) /* 0x80 = enable, 0x00 = disable */
PORT_ADDR(0xf9, 0xf9, sega_coin_counter_w) /* 0x80 = enable, 0x00 = disable */
PORT_END

PORT_WRITE(ElimPortWrite)
PORT_ADDR(0x38, 0x38, sega_sh_speech_w)
PORT_ADDR(0x3e, 0x3e, elim1_sh_w)
PORT_ADDR(0x3f, 0x3f, elim2_sh_w)
PORT_ADDR(0xbd, 0xbd, sega_mult1_w)
PORT_ADDR(0xbe, 0xbe, sega_mult2_w)
PORT_ADDR(0xf8, 0xf8, sega_switch_w)
PORT_ADDR(0xf9, 0xf9, sega_coin_counter_w) /* 0x80 = enable, 0x00 = disable */
PORT_END

PORT_WRITE(ZektorPortWrite)
PORT_ADDR(0x38, 0x38, sega_sh_speech_w)
PORT_ADDR(0x3c, 0x3c, Zektor_AY8910_w)
//PORT_ADDR(0x3d, 0x3d, Zektor_AY8910_w, NULL)
PORT_ADDR(0x3e, 0x3e, Zektor1_sh_w)
PORT_ADDR(0x3f, 0x3f, Zektor2_sh_w)
PORT_ADDR(0xbd, 0xbd, sega_mult1_w)
PORT_ADDR(0xbe, 0xbe, sega_mult2_w)
PORT_ADDR(0xf8, 0xf8, sega_switch_w)
PORT_ADDR(0xf9, 0xf9, sega_coin_counter_w) /* 0x80 = enable, 0x00 = disable */
PORT_END

PORT_WRITE(StarTrekPortWrite)
PORT_ADDR(0x38, 0x38, sega_sh_speech_w)
PORT_ADDR(0x3f, 0x3f, StarTrek_sh_w)
PORT_ADDR(0xbd, 0xbd, sega_mult1_w)
PORT_ADDR(0xbe, 0xbe, sega_mult2_w)
PORT_ADDR(0xf8, 0xf8, sega_switch_w)
PORT_ADDR(0xf9, 0xf9, sega_coin_counter_w) /* 0x80 = enable, 0x00 = disable */
PORT_END

PORT_WRITE(TacScanPortWrite)
PORT_ADDR(0x38, 0x38, sega_sh_speech_w)
PORT_ADDR(0x3f, 0x3f, TacScan_sh_w)
PORT_ADDR(0xbd, 0xbd, sega_mult1_w)
PORT_ADDR(0xbe, 0xbe, sega_mult2_w)
PORT_ADDR(0xf8, 0xf8, sega_switch_w)
PORT_ADDR(0xf9, 0xf9, sega_coin_counter_w) /* 0x80 = enable, 0x00 = disable */
PORT_END

int init_segag80()
{
	nodecrypt = 0;
	sega_rotate = 0;

	min_x = 512;
	min_y = 512;

	vectorram = (unsigned char*)malloc(0x1000);

	switch (gamenum)
	{
	case ZEKTOR:initz80N(SegaRead, SegaWrite, G80SpinPortRead, ZektorPortWrite, 0); NUM_SPEECH_SAMPLES = NUM_ZEKTOR_SPEECH; sega_security(82); break;
	case TACSCAN:sega_rotate = 1; initz80N(SegaRead, SegaWrite, G80SpinPortRead, TacScanPortWrite, 0); sega_security(76); break;
	case STARTREK:initz80N(SegaRead, SegaWrite, G80SpinPortRead, StarTrekPortWrite, 0); NUM_SPEECH_SAMPLES = NUM_STARTREK_SPEECH; sega_security(64); break;
	case SPACFURY:initz80N(SegaRead, SegaWrite, SpacfuryPortRead, SpacfuryPortWrite, 0); NUM_SPEECH_SAMPLES = NUM_SPACFURY_SPEECH; sega_security(0); Decrypt(0); nodecrypt = 1; break;
	case SPACFURA:initz80N(SegaRead, SegaWrite, SpacfuryPortRead, SpacfuryPortWrite, 0); NUM_SPEECH_SAMPLES = NUM_SPACFURY_SPEECH; sega_security(0); Decrypt(5); nodecrypt = 1; break;
	case SPACFURB:initz80N(SegaRead, SegaWrite, SpacfuryPortRead, SpacfuryPortWrite, 0); NUM_SPEECH_SAMPLES = NUM_SPACFURY_SPEECH; sega_security(0); Decrypt(6); nodecrypt = 1; break;
	case ELIM2:initz80N(SegaRead, SegaWrite, Elim2PortRead, ElimPortWrite, 0); sega_security(70); break;
	case ELIM2A:initz80N(SegaRead, SegaWrite, Elim2PortRead, ElimPortWrite, 0); sega_security(70); break;
	case ELIM2C:initz80N(SegaRead, SegaWrite, Elim2PortRead, ElimPortWrite, 0); sega_security(70); break;
	case ELIM4:initz80N(SegaRead, SegaWrite, Elim4PortRead, ElimPortWrite, 0); sega_security(76); break;
	case ELIM4P:initz80N(SegaRead, SegaWrite, Elim4PortRead, ElimPortWrite, 0); sega_security(76); break;
	}

	sega_sh_start();
	mz80reset();
	return 0;
}

void end_segag80()
{
	//sega_vh_stop ();
	free(GI[1]);
	free(vectorram);

	//save_sega_hi();
	sega_rotate = 0;
}