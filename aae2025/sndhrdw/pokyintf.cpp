/***************************************************************************

	pokyintf.c

	Interface the routines in pokey.c to MAME

	The pseudo-random number algorithm is (C) Keith Gerdes.
	Please contact him if you wish to use this same algorithm in your programs.
	His e-mail is mailto:kgerdes@worldnet.att.net

***************************************************************************/


#include "deftypes.h"
#include "aae_mame_driver.h"
#include "pokyintf.h"
#include "pokey.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include "log.h"
#include <string.h>
#include "mixer.h"

#define MIN_SLICE 10	/* minimum update step for POKEY (226usec @ 44100Hz) */

static int buffer_len;
static int emulation_rate;
static int sample_pos;

static int channel;

extern uint8 rng[MAXPOKEYS];
static uint8 pokey_random[MAXPOKEYS];     /* The random number for each pokey */

static struct POKEYinterface* intf;
static short* buffer;

int pokey_sh_start(struct POKEYinterface* intfa)
{
	int i, res;

	intf = intfa;

	buffer_len = config.samplerate / driver[gamenum].fps;// Machine->sample_rate / Machine->drv->frames_per_second;
	emulation_rate = buffer_len * driver[gamenum].fps;
	sample_pos = 0;

	//channel = get_play_channels(1);
	wrlog("Pokey Stream Size start %d", (16 / 8) * buffer_len);
	wrlog("Pokey Stream Size that I am creating %d", 2 * buffer_len);
	if ((buffer = (short *)malloc(buffer_len * 2 )) == 0)
		//malloc((sample_bits/8)*stream_buffer_len[channel])) == 0)
	{
		wrlog("Pokey Buffer Error");
		return 1;
	}

	memset(buffer, 0, buffer_len);

	res = Pokey_sound_init(intf->clock, emulation_rate, intf->num);// , intf->clip);
	if (res)
	{
		free(buffer);
		return 1;
	}

	for (i = 0; i < intf->num; i++)
		/* Seed the values */
		pokey_random[i] = rand();

	stream_start(0, 0, 16, driver[gamenum].fps);
	
	return 0;
}

void pokey_sh_stop(void)
{
	pokey_sound_stop();
	stream_stop(0, 0);
	free(buffer);
}

static void update_pokeys(void)
{
	int newpos = 0;

	newpos = cpu_scale_by_cycles(buffer_len, intf->clock); /* get current position based on the timer */

	if (newpos - sample_pos < MIN_SLICE)
		return;
	Pokey_process(buffer + sample_pos, newpos - sample_pos);

	sample_pos = newpos;
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
	//wrlog("Pokey #%d read from register %02x\n", chip, addr);

	switch (addr & 0x0f)
	{
		
	case POT0_C:
		if (intf->pot0_r[chip]) return (*intf->pot0_r[chip])(addr);
		break;
	case POT1_C:
		if (intf->pot1_r[chip]) return (*intf->pot1_r[chip])(addr);
		break;
	case POT2_C:
		if (intf->pot2_r[chip]) return (*intf->pot2_r[chip])(addr);
		break;
	case POT3_C:
		if (intf->pot3_r[chip]) return (*intf->pot3_r[chip])(addr);
		break;
	case POT4_C:
		if (intf->pot4_r[chip]) return (*intf->pot4_r[chip])(addr);
		break;
	case POT5_C:
		if (intf->pot5_r[chip]) return (*intf->pot5_r[chip])(addr);
		break;
	case POT6_C:
		if (intf->pot6_r[chip]) return (*intf->pot6_r[chip])(addr);
		break;
	case POT7_C:
		if (intf->pot7_r[chip]) return (*intf->pot7_r[chip])(addr);
		break;
	case ALLPOT_C:
		if (intf->allpot_r[chip]) return (*intf->allpot_r[chip])(addr);
		break;
	case RANDOM_C:
		/* If the random number generator is enabled, get a new random number */
		/* The pokey apparently shifts the high nibble down on consecutive reads */
		if (rng[chip]) {
			pokey_random[chip] = (pokey_random[chip] >> 4) | (rand() & 0xf0);
		}
	//	wrlog( "RNG #%d data: %02x\n", chip, pokey_random[chip]);
		return pokey_random[chip];
		break;
	default:
		//wrlog( "Pokey #%d read from register %02x\n", chip, addr);
		return 0;
		break;
	}

	return 0;
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

	/*    if (errorlog) fprintf (errorlog, "offset: %04x pokey_num: %02x pokey_reg: %02x\n", offset, pokey_num, pokey_reg);*/
	return Read_pokey_regs(pokey_reg, pokey_num);
}

void pokey1_w(int offset, int data)
{
	//wrlog("Pokey 1 write  #%d to register %02x\n", data, offset);
	update_pokeys();
	Update_pokey_sound(offset, data, 0, intf->gain);
}

void pokey2_w(int offset, int data)
{
	update_pokeys();
	Update_pokey_sound(offset, data, 1, intf->gain);
}

void pokey3_w(int offset, int data)
{
	update_pokeys();
	Update_pokey_sound(offset, data, 2, intf->gain);
}

void pokey4_w(int offset, int data)
{
	update_pokeys();
	Update_pokey_sound(offset, data, 3, intf->gain);
}

void quad_pokey_w(int offset, int data)
{
	int pokey_num = (offset >> 3) & ~0x04;
	int control = (offset & 0x20) >> 2;
	int pokey_reg = (offset % 8) | control;

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

UINT8 quadpokey_r(UINT32 address, struct MemoryReadByte* psMemRead)
{
	return quad_pokey_r(address);
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

void pokey_sh_update(void)
{
	//if (Machine->sample_rate == 0) return;

	if (sample_pos < buffer_len)
		Pokey_process(buffer + sample_pos, buffer_len - sample_pos);
	sample_pos = 0;

	stream_update(0, buffer);
	//osd_play_streamed_sample(channel,(signed char *)buffer,buffer_len,emulation_rate,intf->volume);
}