#ifndef aae_mame_pokey_h
#define aae_mame_pokey_h

#pragma once

/*****************************************************************************/
/*                                                                           */
/* Module:  POKEY Chip Simulator Includes, V2.4                              */
/* Purpose: To emulate the sound generation hardware of the Atari POKEY chip.*/
/* Author:  Ron Fries                                                        */
/*                                                                           */
/* Revision History:                                                         */
/*                                                                           */
/* 09/22/96 - Ron Fries - Initial Release                                    */
/* 04/06/97 - Brad Oliver - Some cross-platform modifications. Added         */
/*                          big/little endian #defines, removed <dos.h>,     */
/*                          conditional defines for TRUE/FALSE               */
/* 01/19/98 - Ron Fries - Changed signed/unsigned sample support to a        */
/*                        compile-time option.  Defaults to unsigned -       */
/*                        define SIGNED_SAMPLES to create signed.            */
/* 03/22/98 - Ron Fries - Added 'filter' support to channels 1 & 2.          */
/*                                                                           */
/*****************************************************************************/
/*                                                                           */
/*                 License Information and Copyright Notice                  */
/*                 ========================================                  */
/*                                                                           */
/* PokeySound is Copyright(c) 1996-1998 by Ron Fries                         */
/*                                                                           */
/* This library is free software; you can redistribute it and/or modify it   */
/* under the terms of version 2 of the GNU Library General Public License    */
/* as published by the Free Software Foundation.                             */
/*                                                                           */
/* This library is distributed in the hope that it will be useful, but       */
/* WITHOUT ANY WARRANTY; without even the implied warranty of                */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library */
/* General Public License for more details.                                  */
/* To obtain a copy of the GNU Library General Public License, write to the  */
/* Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.   */
/*                                                                           */
/* Any permitted reproduction of these routines, in whole or in part, must   */
/* bear this legend.                                                         */
/*                                                                           */
/*****************************************************************************/

//= ========================================================================== =
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

#include "aae_mame_driver.h"
#include "deftypes.h"

#define POKEY_DEFAULT_GAIN 16
#define POKEY_DEBUG 0
#define NO_CLIP		0
#define USE_CLIP	1

// THIS IS ONLY DEFINED WHEN USING A 16 BIT AUDIO OUTPUT BACKEND.
//#define POKEY_16BIT_AUDIO 1
//

/* define some data types to keep it platform independent */

#define int8  char
#define int16 short
#define int32 int
#define uint8  unsigned int8
#define uint16 unsigned int16
#define uint32 unsigned int32

/* CONSTANT DEFINITIONS */

/* POKEY WRITE LOGICALS */
/* Note: only 0x00 - 0x09 are emulated by POKEYSND */
#define AUDF1_C     0x00
#define AUDC1_C     0x01
#define AUDF2_C     0x02
#define AUDC2_C     0x03
#define AUDF3_C     0x04
#define AUDC3_C     0x05
#define AUDF4_C     0x06
#define AUDC4_C     0x07
#define AUDCTL_C    0x08
#define STIMER_C    0x09
#define SKREST_C    0x0A
#define POTGO_C     0x0B
#define SEROUT_C    0x0D
#define IRQEN_C     0x0E
#define SKCTL_C     0x0F

/* POKEY READ LOGICALS */
#define POT0_C      0x00
#define POT1_C      0x01
#define POT2_C      0x02
#define POT3_C      0x03
#define POT4_C      0x04
#define POT5_C      0x05
#define POT6_C      0x06
#define POT7_C      0x07
#define ALLPOT_C    0x08
#define KBCODE_C    0x09
#define RANDOM_C    0x0A
#define SERIN_C     0x0D
#define IRQST_C     0x0E
#define SKSTAT_C    0x0F


/* As an alternative to using the exact frequencies, selecting a playback
   frequency that is an exact division of the main clock provides a higher
   quality output due to less aliasing.  For best results, a value of
   1787520 MHz is used for the main clock.  With this value, both the
   64 kHz and 15 kHz clocks are evenly divisible.  Selecting a playback
   frequency that is also a division of the clock provides the best
   results.  The best options are FREQ_64 divided by either 2, 3, or 4.
   The best selection is based on a trade off between performance and
   sound quality.

   Of course, using a main clock frequency that is not exact will affect
   the pitch of the output.  With these numbers, the pitch will be low
   by 0.127%.  (More than likely, an actual unit will vary by this much!) */

#define FREQ_17_EXACT     1789790  /* exact 1.79 MHz clock freq */
#define FREQ_17_APPROX    1787520  /* approximate 1.79 MHz clock freq */
#define MAXPOKEYS         4        /* max number of emulated chips */



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

int pokey_sh_start(struct POKEYinterface* interfacea);
void pokey_sh_update(void);
void pokey_sh_stop(void);

int Read_pokey_regs(uint16 addr, uint8 chip);

int pokey1_r(int offset);
int pokey2_r(int offset);
int pokey3_r(int offset);
int pokey4_r(int offset);
int quad_pokey_r(int offset);

void pokey1_w(int offset, int data);
void pokey2_w(int offset, int data);
void pokey3_w(int offset, int data);
void pokey4_w(int offset, int data);
void quad_pokey_w(int offset, int data);

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

#ifndef POKEY_16BIT_AUDIO 
int Pokey_sound_init(uint32 freq17, uint16 playback_freq, uint8 num_pokeys, uint8 use_clip);
#else
int Pokey_sound_init (uint32 freq17, uint16 playback_freq, uint8 num_pokeys);
#endif
void Update_pokey_sound(uint16 addr, uint8 val, uint8 chip, uint8 gain);
#ifndef POKEY_16BIT_AUDIO 
#else
void Pokey_process(short* buffer, uint16 n);
#endif
void Pokey_process(unsigned char* buffer, uint16 n);
void pokey_sound_stop();

#endif
