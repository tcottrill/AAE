//============================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
// 
// SOME CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.  
//============================================================================

/***************************************************************************

	pokyintf.c

	Interface the routines in pokey.c to MAME

	The pseudo-random number algorithm is (C) Keith Gerdes.
	Please contact him if you wish to use this same algorithm in your programs.
	His e-mail is mailto:kgerdes@worldnet.att.net

***************************************************************************/

#include "pokyintf.h"
#include "aae_mame_driver.h"
#include "cpu_control.h"
#include "samples.h"
#include "../rand.h"


#define MIN_SLICE 1	/* minimum update step for POKEY (226usec @ 44100Hz) */
#define TARGET_EMULATION_RATE 44100//44100	/* will be adapted to be a multiple of buffer_len */

static int buffer_len;
static int emulation_rate;
static int sample_pos;
static uint8 pokey_random[MAXPOKEYS];     /* The random number for each pokey */

static struct POKEYinterface* intfa;
static unsigned char* buffer;

int pokey_sh_start(struct POKEYinterface* iinterface)
{
	int i;
	intfa = iinterface;

	buffer_len = TARGET_EMULATION_RATE / driver[gamenum].fps;
	emulation_rate = buffer_len * driver[gamenum].fps;
	
	if ((buffer = (unsigned char*)malloc(buffer_len)) == 0)
	{
		free(buffer);
		return 1;
	}
	
	//stream = play_audio_stream(buffer_len, 8, 0, TARGET_EMULATION_RATE, config.pokeyvol, 128); //450  13500
	//stream = play_audio_stream(buffer_len, 8, 0, TARGET_EMULATION_RATE, 250, 128); //450  13500
	aae_stream_init(0, emulation_rate, buffer_len, intfa->volume);
	Pokey_sound_init(intfa->clock, emulation_rate, intfa->num, intfa->clip);
	wrlog("Pokey sound init: Emulation rate %d Buffer len %d Num Pokeys: %d",emulation_rate,buffer_len, intfa->num);

	for (i = 0; i < intfa->num; i++)
		/* Seed the values */
		pokey_random[i] = rand();
	pokey_cpu_reset();
	return 0;
}

void pokey_sh_stop(void)
{
	//free_audio_stream_buffer(stream);
	//stop_audio_stream(stream);
	aae_stop_stream(0);
	free(buffer);
}

/*****************************************************************************/
/* Module:  Read_pokey_regs()                                                */
/* Purpose: To return the values of the Pokey registers. Currently, only the */
/*          random number generator register is returned.                    */
/*                                                                           */
/* Author:  Keith Gerdes, Brad Oliver & Eric Smith                           */
/* Date:    August 8, 1997                                                   */
/*                                                                           */
/* Inputs:  addr - the address of the parameter to be changed                */
/*          chip - the pokey chip to read                                    */
/*                                                                           */
/* Outputs: Adjusts local globals, returns the register in question          */
/*                                                                           */
/*****************************************************************************/

int Read_pokey_regs(uint16 addr, uint8 chip)
{
	//wrlog("POKEY READ here address %x chip %x",addr,chip);
	switch (addr & 0x0f)
	{
	case POT0_C:
		if (intfa->pot0_r[chip]) return (*intfa->pot0_r[chip])(addr);
		break;
	case POT1_C:
		if (intfa->pot1_r[chip]) return (*intfa->pot1_r[chip])(addr);
		break;
	case POT2_C:
		if (intfa->pot2_r[chip]) return (*intfa->pot2_r[chip])(addr);
		break;
	case POT3_C:
		if (intfa->pot3_r[chip]) return (*intfa->pot3_r[chip])(addr);
		break;
	case POT4_C:
		if (intfa->pot4_r[chip]) return (*intfa->pot4_r[chip])(addr);
		break;
	case POT5_C:
		if (intfa->pot5_r[chip]) return (*intfa->pot5_r[chip])(addr);
		break;
	case POT6_C:
		if (intfa->pot6_r[chip]) return (*intfa->pot6_r[chip])(addr);
		break;
	case POT7_C:
		if (intfa->pot7_r[chip]) return (*intfa->pot7_r[chip])(addr);
		break;
	case ALLPOT_C:
		if (intfa->allpot_r[chip]) return (*intfa->allpot_r[chip])(addr);
		break;

	case RANDOM_C:
		// If the random number generator is enabled, get a new random number
		// The pokey apparently shifts the high nibble down on consecutive reads
		//if (rng[chip]) {
			//pokey_random[chip] = (pokey_random[chip]>>4) | (rand()&0xf0); return pokey_random[chip];
		return randintm(0xff);
		//	}
		//else
		//return pokey_random[chip]^ 0xff;
		break;
	default: wrlog("unhandled pokey read register %x chip %x", addr, chip);
#ifdef DEBUG
		if (errorlog) fprintf(errorlog, "Pokey #%d read from register %02x\n", chip, addr);
#endif

		wrlog("Pokey #%d read from register %02x\n", chip, addr);

		return 0;
		break;
	}

	return 0;
}

UINT8 quadpokey_r(UINT32 address, struct MemoryReadByte* psMemRead)
{
	return quad_pokey_r(address);// -0x2000);
}
UINT8 pokey_1_r(UINT32 address, struct MemoryReadByte* psMemRead)
{
	return Read_pokey_regs(address, 0);
}
UINT8 pokey_2_r(UINT32 address, struct MemoryReadByte* psMemRead)
{
	return Read_pokey_regs(address, 1);
}
UINT8 pokey_3_r(UINT32 address, struct MemoryReadByte* psMemRead)
{
	return Read_pokey_regs(address, 2);
}
UINT8 pokey_4_r(UINT32 address, struct MemoryReadByte* psMemRead)
{
	return Read_pokey_regs(address, 3);
}
int pokey1_r(int offset)
{
	return Read_pokey_regs(offset, 0);
}

int pokey2_r(int offset)
{
	return Read_pokey_regs(offset, 1);
}

int pokey3_r(int offset)
{
	return Read_pokey_regs(offset, 2);
}

int pokey4_r(int offset)
{
	return Read_pokey_regs(offset, 3);
}

int quad_pokey_r(int offset)
{

	int pokey_num = (offset >> 3) & ~0x04;
	int control = (offset & 0x20) >> 2;
	int pokey_reg = (offset % 8) | control;

	//if (errorlog) fprintf (errorlog, "offset: %04x pokey_num: %02x pokey_reg: %02x\n", offset, pokey_num, pokey_reg);
	return Read_pokey_regs(pokey_reg, pokey_num);
}

void quadpokey_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	quad_pokey_w(address, data);
}

void pokey_1_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	pokey1_w((address & 0x0f), data);
}
void pokey_2_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	pokey2_w((address & 0x0f), data);
}
void pokey_3_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	pokey3_w((address & 0x0f), data);
}
void pokey_4_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite)
{
	pokey4_w((address & 0x0f), data);
}

void pokey1_w(int offset, int data)
{
	pokey_update();
	Update_pokey_sound(offset, data, 0, intfa->gain);
}

void pokey2_w(int offset, int data)
{
	pokey_update();
	Update_pokey_sound(offset, data, 1, intfa->gain);
}

void pokey3_w(int offset, int data)
{
	pokey_update();
	Update_pokey_sound(offset, data, 2, intfa->gain);
}

void pokey4_w(int offset, int data)
{
	pokey_update();
	Update_pokey_sound(offset, data, 3, intfa->gain);
}

void quad_pokey_w(int offset, int data)
{
		
	int pokey_num = (offset >> 3) & ~0x04;
	int control = (offset & 0x20) >> 2;
	int pokey_reg = (offset % 8) | control;
	// wrlog("Quad Pokey Write %x %x %x %x",pokey_num,control,pokey_reg,data);
	switch (pokey_num) {
	case 0:
		pokey1_w(pokey_reg, data);
		break;
	case 1:
		pokey2_w(pokey_reg, data);
		break;
	case 2:
		pokey3_w(pokey_reg, data);
		break;
	case 3:
		pokey4_w(pokey_reg, data);
		break;
	}
}

void pokey_update(void)  // Look at this when finishing adding Star Wars back.
{
	int totcycles, leftcycles, newpos;

	newpos = cpu_scale_by_cycles(buffer_len);
	
	if (newpos - sample_pos < MIN_SLICE)
		return;
	Pokey_process(buffer + sample_pos, newpos - sample_pos);

	sample_pos = newpos;
}

void pokey_sh_update(void)
{

	if (sample_pos < buffer_len) { Pokey_process(buffer + sample_pos, buffer_len - sample_pos); }
	
	aae_play_streamed_sample(0, buffer, buffer_len, emulation_rate, intfa->volume);

	sample_pos = 0;
}

void pokey_cpu_reset(void)
{
	sample_pos = 0;
}