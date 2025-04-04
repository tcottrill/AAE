#ifndef pokyintf_h
#define pokyintf_h

#include "pokey.h"

#define NO_CLIP   0
#define USE_CLIP  1

#define POKEY_DEFAULT_GAIN 16

struct POKEYinterface
{
	int num;	/* total number of pokeys in the machine */
	int clock;
	int volume;
	int gain;
	int clip;				/* determines if pokey.c will clip the sample range */
	/* Handlers for reading the pot values. Some Atari games use ALLPOT to return */
	/* dipswitch settings and other things */
	int (*pot0_r[MAXPOKEYS])(int offset);
	int (*pot1_r[MAXPOKEYS])(int offset);
	int (*pot2_r[MAXPOKEYS])(int offset);
	int (*pot3_r[MAXPOKEYS])(int offset);
	int (*pot4_r[MAXPOKEYS])(int offset);
	int (*pot5_r[MAXPOKEYS])(int offset);
	int (*pot6_r[MAXPOKEYS])(int offset);
	int (*pot7_r[MAXPOKEYS])(int offset);
	int (*allpot_r[MAXPOKEYS])(int offset);
};

int pokey_sh_start (struct POKEYinterface *intfa);
void pokey_sh_stop (void);
int Read_pokey_regs(uint16 addr, uint8 chip);
int pokey1_r (int offset);
int pokey2_r (int offset);
int pokey3_r (int offset);
int pokey4_r (int offset);
int quad_pokey_r (int offset);

void pokey1_w (int offset,int data);
void pokey2_w (int offset,int data);
void pokey3_w (int offset,int data);
void pokey4_w (int offset,int data);
void quad_pokey_w (int offset,int data);


// Read definitions for AAE
UINT8 pokey_1_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 pokey_2_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 pokey_3_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 pokey_4_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 quadpokey_r(UINT32 address, struct MemoryReadByte* psMemRead);
// Write definitions for AAE
void pokey_1_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void pokey_2_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void pokey_3_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void pokey_4_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void quadpokey_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);

void pokey_sh_update (void);

#endif
