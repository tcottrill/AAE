// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E.(TM) Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain @ the M.A.M.E.(TM) Project and its
//     respective contributors under their original terms of distribution.
//   - Redistribution must preserve both this notice and the original MAME
//     copyright acknowledgement.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// Original Copyright:
//   This file is originally part of and copyright the M.A.M.E.(TM) Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------
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

/***************************************************************************

	Cinematronics vector hardware

	Special thanks to Neil Bradley, Zonn Moore, and Jeff Mitchell of the
	Retrocade Alliance

	Update:
	6/27/99 Jim Hernandez -- 1st Attempt at Fixing Drone Star Castle sound and
							 pitch adjustments.
	6/30/99 MLR added Rip Off, Solar Quest, Armor Attack (no samples yet)
	11/04/08 Jim Hernandez -- Fixed Drone Star Castle sound again. It was
							  broken for a long time due to some changes.

	Bugs: Sometimes the death explosion (small explosion) does not trigger.

***************************************************************************/


#include "cinematronics_driver.h"
#include "aae_mame_driver.h"
#include "mixer.h"
#include "ccpu.h"
#include "ay8910.h"
#include "z80fmly.h"
#include "demon_fifo.h"

#ifdef _MSC_VER
#pragma warning( disable : 4018 4244)
#endif

/*************************************
 *
 *  Macros
 *
 *************************************/

#define RISING_EDGE(bit, changed, val)	(((changed) & (bit)) && ((val) & (bit)))
#define FALLING_EDGE(bit, changed, val)	(((changed) & (bit)) && !((val) & (bit)))

#define SOUNDVAL_RISING_EDGE(bit)		RISING_EDGE(bit, bits_changed, sound_val)
#define SOUNDVAL_FALLING_EDGE(bit)		FALLING_EDGE(bit, bits_changed, sound_val)

#define SHIFTREG_RISING_EDGE(bit)		RISING_EDGE(bit, (last_shift ^ current_shift), current_shift)
#define SHIFTREG_FALLING_EDGE(bit)		FALLING_EDGE(bit, (last_shift ^ current_shift), current_shift)

#define SHIFTREG2_RISING_EDGE(bit)		RISING_EDGE(bit, (last_shift2 ^ current_shift), current_shift)
#define SHIFTREG2_FALLING_EDGE(bit)		FALLING_EDGE(bit, (last_shift2 ^ current_shift), current_shift)

static DemonSoundFifo demon_fifo;
static bool demon_board_active = false;
// Logged at shutdown: zero acks means the CTC hooks were never installed.
static unsigned demon_command_count = 0;
static unsigned demon_ack_count = 0;

void (*sound_write) (unsigned char, unsigned char) = nullptr;

static UINT32 current_shift = 0;
static UINT32 last_shift = 0;
static UINT32 last_shift16 = 0;
static UINT32 last_shift2 = 0;
static UINT32 current_pitch = 0x20000;
static UINT32 last_frame = 0;
// Solar Quest thrust fade state.
static int solarq_target_volume = 0;
static int solarq_current_volume = 0;
// Speed Freak noise level at the previous latch (crash trigger edge).
static int speedfrk_last_noise = 0;
// Tail Gunner OUT register and sound mux register (current and previous).
static UINT8 OldOutReg = 0;
static UINT8 XRreg = 0;
static UINT8 OldXRreg = 0x22;

static UINT8 sound_control;

void init_cinemat_snd(void (*snd_pointer)(UINT8, UINT8))
{
	sound_control = 0x9f;

	/* reset shift register values */

	current_shift = 0xffff;
	last_shift = 0xffff;
	last_shift16 = 0xffff;
	last_shift2 = 0xffff;
	current_pitch = 0x20000;

	/* the frame counter restarts at 0 for every game, so a stale last_frame
	   from the previous game froze the per-frame drone pitch (Star Castle)
	   and thrust fade (Solar Quest) until the new count caught up */
	last_frame = 0;
	solarq_target_volume = 0;
	solarq_current_volume = 0;
	speedfrk_last_noise = 0;
	/* Tail Gunner mux: same values as a first boot, so the first sound
	   after a restart is not swallowed by the previous game's state */
	OldOutReg = 0;
	XRreg = 0;
	OldXRreg = 0x22;

	sound_write = snd_pointer;
}

void cini_sound_control_w(int offset, int data)
{
	UINT8 oldval = sound_control;

	/* form an 8-bit value with the new bit */
	sound_control = (sound_control & ~(1 << offset)) | ((data & 1) << offset);

	/* if something changed, call the sound subroutine */
	if ((sound_control != oldval) && sound_write)
		(*sound_write)(sound_control, sound_control ^ oldval);
}

static void cinemat_shift(UINT8 sound_val, UINT8 bits_changed, UINT8 A1, UINT8 CLK)
{
	// See if we're latching a shift

	if ((bits_changed & CLK) && (0 == (sound_val & CLK)))
	{
		current_shift <<= 1;
		if (sound_val & A1)
			current_shift |= 1;
	}
}

void ripoff_sound(UINT8 sound_val, UINT8 bits_changed)
{
	UINT8 shift_diff, current_bg_sound;
	static UINT8 last_bg_sound;

	cinemat_shift(sound_val, bits_changed, 0x01, 0x02);

	// Now see if it's time to act upon the shifted data

	if ((bits_changed & 0x04) && (0 == (sound_val & 0x04)))
	{
		// Yep. Falling edge! Find out what has changed.

		shift_diff = current_shift ^ last_shift;

		current_bg_sound = ((current_shift & 0x1) << 2) | (current_shift & 0x2) | ((current_shift & 0x4) >> 2);
		if (current_bg_sound != last_bg_sound) // use another background sound ?
		{
			shift_diff |= 0x08;
			sample_stop(5);
			last_bg_sound = current_bg_sound;
		}

		if (shift_diff & 0x08)
		{
			if (current_shift & 0x08)
				sample_stop(5);
			else
			{
				sample_start(5, 5 + last_bg_sound, 1);	// Background
				sample_set_volume(5, 150);
			}
		}

		if ((shift_diff & 0x10) && (0 == (current_shift & 0x10)))
			sample_start(2, 2, 0);	// Beep

		if (shift_diff & 0x20)
		{
			if (current_shift & 0x20)
				sample_stop(1);	// Stop it!
			else
				sample_start(1, 1, 1);	// Motor
		}

		last_shift = current_shift;
	}

	if ((bits_changed & 0x08) && (0 == (sound_val & 0x08)))
		sample_start(4, 4, 0);			// Torpedo

	if ((bits_changed & 0x10) && (0 == (sound_val & 0x10)))
		sample_start(0, 0, 0);			// Laser

	if ((bits_changed & 0x80) && (0 == (sound_val & 0x80)))
	{
		sample_start(3, 3, 0);
	}			// Explosion
}

void null_sound(UINT8 sound_val, UINT8 bits_changed)
{
	//do nothing ;(
}

void starcas_sound(UINT8 sound_val, UINT8 bits_changed)
{
	UINT32 target_pitch;
	UINT8 shift_diff;

	cinemat_shift(sound_val, bits_changed, 0x80, 0x10);

	// Now see if it's time to act upon the shifted data

	if ((bits_changed & 0x01) && (0 == (sound_val & 0x01)))
	{
		// Yep. Falling edge! Find out what has changed.

		shift_diff = current_shift ^ last_shift;

		if ((shift_diff & 1) && (0 == (current_shift & 1)))
			sample_start(2, 2, 0);	// Castle fire

		if ((shift_diff & 2) && (0 == (current_shift & 2)))
			sample_start(5, 5, 0);	// Shield hit

		if (shift_diff & 0x04)
		{
			if (current_shift & 0x04)
				sample_start(6, 6, 1);	// Star sound
			else
				sample_stop(6);	// Stop it!
		}

		if (shift_diff & 0x08)
		{
			if (current_shift & 0x08)
				sample_stop(7);	// Stop it!
			else
				sample_start(7, 7, 1);	// Thrust sound
		}

		if (shift_diff & 0x10)
		{
			if (current_shift & 0x10)
				sample_stop(4);
			else
				sample_start(4, 4, 1);	// Drone
		}

		// Latch the drone pitch

		target_pitch = (current_shift & 0x60) >> 3;
		target_pitch |= ((current_shift & 0x40) >> 5);
		target_pitch |= ((current_shift & 0x80) >> 7);

		// target_pitch = (current_shift & 0x60) >> 3;
		// is the the target drone pitch to rise and stop at.

		target_pitch = 0x10000 + (target_pitch << 12);

		// 0x10000 is lowest value the pitch will drop to
		// Star Castle drone sound

		static int last_pitch = 0;

		if ((UINT32)cpu_getcurrentframe() > last_frame)
		{
			if (current_pitch > target_pitch)
				current_pitch -= 300;
			if (current_pitch < target_pitch)
				current_pitch += 200;
			
			if (current_pitch != (UINT32)last_pitch)
			{
				last_pitch = current_pitch;
			}
			sample_set_freq(4, current_pitch);
		
			last_frame = cpu_getcurrentframe();
		}

		last_shift = current_shift;
	}

	if ((bits_changed & 0x08) && (0 == (sound_val & 0x08)))
		sample_start(3, 3, 0);			// Player fire

	if ((bits_changed & 0x04) && (0 == (sound_val & 0x04)))
		sample_start(1, 1, 0);			// Soft explosion

	if ((bits_changed & 0x02) && (0 == (sound_val & 0x02)))
		sample_start(0, 0, 0);			// Loud explosion
}



/*************************************
 *
 *	Speed Freak
 *
 *************************************/


// Speed Freak sample/channel numbers (speedfrk_samples[] order).
enum { SPEEDFRK_OFFROAD = 0, SPEEDFRK_ENGINE = 1, SPEEDFRK_HORN = 2, SPEEDFRK_CRASH = 3 };
// engine.wav's fundamental, and the constant that maps the engine counter
// to a tone: tone = CLOCK / (4096 - value). Tune CLOCK by ear; 135000 puts
// the in-game RPM range (values ~250..3150) at about 35..143 Hz.
static const double SPEEDFRK_ENGINE_WAV_HZ = 73.6;
static const double SPEEDFRK_ENGINE_CLOCK = 135000.0;

void speedfrk_sound(UINT8 sound_val, UINT8 bits_changed)
{
	/* Once per frame the game drops bit 0x02, clocks a 16-bit word into the
	   shift register (16 falling edges of 0x08, inverted data on 0x04) and
	   raises 0x02 again to latch it (measured from gameplay traces). */
	if (SOUNDVAL_FALLING_EDGE(0x08))
		current_shift = ((current_shift >> 1) & 0x7fff) | ((((~sound_val) >> 2) & 1) << 15);

	if (SOUNDVAL_RISING_EDGE(0x02))
	{
		/* high 12 bits: engine counter, counts from the value up to $FFF, so
		   the engine tone is proportional to 1/(4096 - value). $FFF = off. */
		const int engine = (current_shift >> 4) & 0xfff;
		if (engine >= 0xfff)
		{
			if (sample_playing(SPEEDFRK_ENGINE)) sample_stop(SPEEDFRK_ENGINE);
		}
		else
		{
			if (!sample_playing(SPEEDFRK_ENGINE))
				sample_start(SPEEDFRK_ENGINE, SPEEDFRK_ENGINE, 1);
			const double tone = SPEEDFRK_ENGINE_CLOCK / (4096.0 - engine);
			const int base = sample_get_freq(SPEEDFRK_ENGINE);
			if (base > 0)
				sample_set_freq(SPEEDFRK_ENGINE, (int)(base * tone / SPEEDFRK_ENGINE_WAV_HZ));
		}

		/* low 4 bits: noise volume. The game ramps it to 15 and decays it
		   over ~0.7 s on a crash; crash.wav carries that envelope. */
		const int noise = current_shift & 0x0f;
		if (noise && !speedfrk_last_noise)
			sample_start(SPEEDFRK_CRASH, SPEEDFRK_CRASH, 0);
		speedfrk_last_noise = noise;
	}

	/* horn - 0=on, 1=off. The game pulses bit 0x80 low for ~0.5 s during
	   play; unconfirmed against schematics. */
	if (SOUNDVAL_FALLING_EDGE(0x80))
		sample_start(SPEEDFRK_HORN, SPEEDFRK_HORN, 1);
	if (SOUNDVAL_RISING_EDGE(0x80))
		sample_stop(SPEEDFRK_HORN);

	/* off-road - 1=on, 0=off */
	if (SOUNDVAL_RISING_EDGE(0x10))
		sample_start(0, 0, 1);
	if (SOUNDVAL_FALLING_EDGE(0x10))
		sample_stop(0);

	/* start LED is controlled by bit 0x02 */
	set_led_status(0 , ~sound_val & 0x02);
}


/*************************************
 *
 *	Armor Attack
 *
 *************************************/

void armora_sound(UINT8 sound_val, UINT8 bits_changed)
{
	/* on the rising edge of bit 0x10, clock bit 0x80 into the shift register */
	if (SOUNDVAL_RISING_EDGE(0x10))
		current_shift = ((current_shift >> 1) & 0x7f) | (sound_val & 0x80);

	/* execute on the rising edge of bit 0x01 */
	if (SOUNDVAL_RISING_EDGE(0x01))
	{
		/* bits 0-4 control the tank sound speed */

		/* lo explosion - falling edge */
		if (SHIFTREG_FALLING_EDGE(0x10))
			sample_start(0, 0, 0);

		/* jeep fire - falling edge */
		if (SHIFTREG_FALLING_EDGE(0x20))
			sample_start(1, 1, 0);

		/* hi explosion - falling edge */
		if (SHIFTREG_FALLING_EDGE(0x40))
			sample_start(2, 2, 0);

		/* tank fire - falling edge */
		if (SHIFTREG_FALLING_EDGE(0x80))
			sample_start(3, 3, 0);

		/* remember the previous value */
		last_shift = current_shift;
	}

	/* tank sound - 0=on, 1=off */
	/* still not totally correct - should be multiple speeds based on remaining bits in shift reg */
	if (SOUNDVAL_FALLING_EDGE(0x02))
		sample_start(4, 4, 1);
	if (SOUNDVAL_RISING_EDGE(0x02))
		sample_stop(4);

	/* beep sound - 0=on, 1=off */
	if (SOUNDVAL_FALLING_EDGE(0x04))
		sample_start(5, 5, 1);
	if (SOUNDVAL_RISING_EDGE(0x04))
		sample_stop(5);

	/* chopper sound - 0=on, 1=off */
	if (SOUNDVAL_FALLING_EDGE(0x08))
		sample_start(6, 6, 1);
	if (SOUNDVAL_RISING_EDGE(0x08))
		sample_stop(6);
}

void solarq_sound(UINT8 sound_val, UINT8 bits_changed)
{
	UINT32 shift_diff, shift_diff16;

	cinemat_shift(sound_val, bits_changed, 0x80, 0x10);

	if ((bits_changed & 0x01) && (0 == (sound_val & 0x01)))
	{
		shift_diff16 = current_shift ^ last_shift16;

		if ((shift_diff16 & 0x1) && (current_shift & 0x1))
		{
			switch (current_shift & 0xffff)
			{
			case 0xceb3:
				sample_start(7, 7, 0);	// Hyperspace
				break;
			case 0x13f3:
				sample_start(7, 8, 0);	// Extra
				break;
			case 0xfdf3:
				sample_start(7, 9, 0);	// Phase
				break;
			case 0x7bf3:
				sample_start(7, 10, 0);	// Enemy fire
				break;
			default:
				LOG_INFO("Unknown sound starting with: %x\n", current_shift & 0xffff);
				break;
			}
		}

		last_shift16 = current_shift;
	}

	// Now see if it's time to act upon the shifted data

	if ((bits_changed & 0x02) && (0 == (sound_val & 0x02)))
	{
		// Yep. Falling edge! Find out what has changed.

		shift_diff = current_shift ^ last_shift;

		if ((shift_diff & 0x01) && (0 == (current_shift & 0x01)))
			sample_start(0, 0, 0);	// loud expl.

		if ((shift_diff & 0x02) && (0 == (current_shift & 0x02)))
			sample_start(1, 1, 0);	// soft expl.

		if (shift_diff & 0x04) // thrust
		{
			if (current_shift & 0x04)
			{
				solarq_target_volume = 0;
				// Release: fade to zero on the software-mixer path. The
				// volume-ramp block below only handles the attack; the
				// mixer's own fade takes the stop to silence.
				sample_end_mixer(2);
			}
			else
			{
				// Full channel level: MAIN VOLUME is applied once at the
				// backend master, and SAMPLE VOLUME scales this channel as
				// part of the sample group (mixer_groups.h).
				solarq_target_volume = 255;
				solarq_current_volume = 0;
				sample_start_mixer(2, 2, 1);
				// The attack ramp below walks the volume up from silence;
				// without this the first frame plays at the channel's
				// previous level.
				sample_set_volume(2, 0);
			}
		}

		if (sample_playing(2) && (last_frame < (UINT32)cpu_getcurrentframe()))
		{
			if (solarq_current_volume > solarq_target_volume)
				solarq_current_volume -= 20;
			if (solarq_current_volume < solarq_target_volume)
				solarq_current_volume += 20;
			if (solarq_current_volume > 0)
				sample_set_volume(2, solarq_current_volume);
			else
				sample_end_mixer(2);
			last_frame = cpu_getcurrentframe();
		}

		if ((shift_diff & 0x08) && (0 == (current_shift & 0x08)))
			sample_start(3, 3, 0);	// Fire

		if ((shift_diff & 0x10) && (0 == (current_shift & 0x10)))
			sample_start(4, 4, 0);	// Capture

		if (shift_diff & 0x20)
		{
			// Nuke: looped on the software-mixer path, faded out on release
			// like thrust above.
			if (current_shift & 0x20)
				sample_start_mixer(6, 6, 1);
			else
				sample_end_mixer(6);
		}

		if ((shift_diff & 0x40) && (0 == (current_shift & 0x40)))
			sample_start(5, 5, 0);	// Photon

		last_shift = current_shift;
	}
}

void spacewar_sound(UINT8 sound_val, UINT8 bits_changed)
{
	// Explosion

	if (bits_changed & 0x01)
	{
		if (sound_val & 0x01)
		{
			if (rand() & 1)
				sample_start(0, 0, 0);
			else
				sample_start(0, 6, 0);
		}
	}
	// Fire sound

	if ((sound_val & 0x02) && (bits_changed & 0x02))
	{
		if (rand() & 1)
			sample_start(1, 1, 0);
		else
			sample_start(1, 7, 0);
	}

	// Player 1 thrust

	if (bits_changed & 0x04)
	{
		if (sound_val & 0x04)
			sample_stop(3);
		else
			sample_start(3, 3, 1);
	}

	// Player 2 thrust

	if (bits_changed & 0x08)
	{
		if (sound_val & 0x08)
			sample_stop(4);
		else
			sample_start(4, 4, 1);
	}

	// Sound board shutoff (or enable)

	if (bits_changed & 0x10)
	{
		// This is a toggle bit. If sound is enabled, shut everything off.

		if (sound_val & 0x10)
		{
			int i;

			for (i = 0; i < 5; i++)
			{
				if (i != 2)
					sample_stop(i);
			}

			sample_start(2, 5, 0);	// Pop when board is shut off
		}
		else
			sample_start(2, 2, 1);	// Otherwise play idle sound
	}
}

void warrior_sound(UINT8 sound_val, UINT8 bits_changed)
{
	if ((bits_changed & 0x10) && (0 == (sound_val & 0x10)))
	{
		sample_start(0, 0, 0);			// appear
	}

	if ((bits_changed & 0x08) && (0 == (sound_val & 0x08)))
		sample_start(3, 3, 0);			// fall

	if ((bits_changed & 0x04) && (0 == (sound_val & 0x04)))
		sample_start(4, 4, 0);			// explosion (kill)

	if (bits_changed & 0x02)
	{
		if ((sound_val & 0x02) == 0)
			sample_start(2, 2, 1);			// hi level
		else
			sample_stop(2);
	}

	if (bits_changed & 0x01)
	{
		if ((sound_val & 0x01) == 0)
			sample_start(1, 1, 1);			// normal level
		else
			sample_stop(1);
	}
}

void tailg_sound(UINT8 sound_val, UINT8 bits_changed)
{
	/*logerror ("Error %d soundval %d bitschanged\n",sound_val,bits_changed);*/

	UINT8   outReg;
	UINT8   outDiff;		/* changed bits */
	UINT8	outLow;			/* changed bits that have just gone low */
	UINT8	xrDiff;			/* changed bits */
	UINT8   xrHigh;			/* changed bits that have just gone high */
	UINT8	xrLow;			/* changed bits that have just gone low */
	UINT8	mask;

	/* outReg = New value of the CCPU's OUT register. */
	outReg = sound_val;

	outDiff = outReg ^ OldOutReg;		/* get bits that have changed state */
	outLow = outDiff & ~outReg;			/* get bits that have just gone low */

	//logerror ("xrDiff %d XRreg %d \n",xrDiff,XRreg);

	if (outLow & 0x10)
	{
		mask = 0x01 << (outReg & 0x07);	/* get address of bit as a mask */

		if (outReg & 0x08)
			XRreg |= mask;		/* If DATA set, set bit */
		else
			XRreg &= ~mask;		/* else reset bit */

		/* check for new MUX sounds */

		xrDiff = XRreg ^ OldXRreg;	/* get diff's */
		xrLow = xrDiff & ~XRreg;	/* get bits that just went low */
		xrHigh = xrDiff & XRreg;	/* get bits that just went high */

		/*
		HYPERSPACE	0x20
		BOUNCE	    0x10
		SHIELD		0x08
		LASER		0x04
		RUMBLE	    0x02
		EXPLOSION   0x01
		*/

		if (xrLow & 0x20)
			sample_start(0, 0, 0);

		if (xrLow & 0x10)
			sample_start(4, 4, 0);

		if (xrLow & 0x08)
			sample_start(3, 3, 1);

		if (xrHigh & 0x08)
			sample_stop(3);

		/* laser - 0=on: loops while held (MAME start(2, 2, true)); the
		   release below lets the current pass finish instead of cutting */
		if (xrLow & 0x04)
			sample_start(2, 2, 1);

		if (xrHigh & 0x04)
			sample_end(2);

		if (xrLow & 0x02)
			sample_start(5, 5, 1);

		if (xrHigh & 0x02)
			sample_stop(5);

		if (xrLow & 0x01)
			sample_start(1, 1, 0);

		OldXRreg = XRreg;
	}

	OldOutReg = outReg;	/* save new OUT register */
}

void starhawk_sound(UINT8 sound_val, UINT8 bits_changed)
{
	/* explosion - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x01))
		sample_start(0, 0, 0);

	/* right laser - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x02))
		sample_start(1, 1, 0);

	/* left laser - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x04))
		sample_start(2, 2, 0);

	/* K - 0=on, 1=off */
	if (SOUNDVAL_FALLING_EDGE(0x08))
		sample_start(3, 3, 1);
	if (SOUNDVAL_RISING_EDGE(0x08))
		sample_stop(3);

	/* master - 0=on, 1=off */
	if (SOUNDVAL_FALLING_EDGE(0x10))
		sample_start(4, 4, 1);
	if (SOUNDVAL_RISING_EDGE(0x10))
		sample_stop(4);

	/* K exit - 1=on, 0=off */
	if (SOUNDVAL_RISING_EDGE(0x80))
		sample_start(3, 5, 1);
	if (SOUNDVAL_FALLING_EDGE(0x80))
		sample_stop(3);
}

void barrier_sound(UINT8 sound_val, UINT8 bits_changed)
{
	/* Player die - rising edge */
	if (SOUNDVAL_RISING_EDGE(0x01))
		sample_start(0, 0, 0);

	/* Player move - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x02))
		sample_start(1, 1, 0);

	/* Enemy move - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x04))
		sample_start(2, 2, 0);
}

void sundance_sound(UINT8 sound_val, UINT8 bits_changed)
{
	/* bong - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x01))
		sample_start(0, 0, 0);

	/* whoosh - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x02))
		sample_start(1, 1, 0);

	/* explosion - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x04))
		sample_start(2, 2, 0);

	/* ping - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x08))
		sample_start(3, 3, 0);

	/* ping - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x10))
		sample_start(4, 4, 0);

	/* hatch - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x80))
		sample_start(5, 5, 0);
}

void boxingb_sound(UINT8 sound_val, UINT8 bits_changed)
{
	/* on the rising edge of bit 0x10, clock bit 0x80 into the shift register */
	if (SOUNDVAL_RISING_EDGE(0x10))
		current_shift = ((current_shift >> 1) & 0x7fff) | ((sound_val << 8) & 0x8000);

	/* execute on the rising edge of bit 0x02 */
	if (SOUNDVAL_RISING_EDGE(0x02))
	{
		/* only the upper 8 bits matter */
		current_shift >>= 8;

		/* soft explosion - falling edge */
		if (SHIFTREG_FALLING_EDGE(0x80))
			sample_start(0, 0, 0);

		/* loud explosion - falling edge */
		if (SHIFTREG_FALLING_EDGE(0x40))
			sample_start(1, 1, 0);

		/* chirping birds - 0=on, 1=off */
		if (SHIFTREG_FALLING_EDGE(0x20))
			sample_start(2, 2, 0);
		if (SHIFTREG_RISING_EDGE(0x20))
			sample_stop(2);

		/* egg cracking - falling edge */
		if (SHIFTREG_FALLING_EDGE(0x10))
			sample_start(3, 3, 0);

		/* bug pushing A - rising edge */
		if (SHIFTREG_RISING_EDGE(0x08))
			sample_start(4, 4, 0);

		/* bug pushing B - rising edge */
		if (SHIFTREG_RISING_EDGE(0x04))
			sample_start(5, 5, 0);

		/* bug dying - falling edge */
		if (SHIFTREG_FALLING_EDGE(0x02))
			sample_start(6, 6, 0);

		/* beetle on screen - falling edge */
		if (SHIFTREG_FALLING_EDGE(0x01))
			sample_start(7, 7, 0);

		/* remember the previous value */
		last_shift = current_shift;
	}

	/* clock music data on the rising edge of bit 0x01 */
	if (SOUNDVAL_RISING_EDGE(0x01))
	{
		int freq, vol;

		/* start/stop the music sample on the high bit */
		if (SHIFTREG2_RISING_EDGE(0x8000))
			sample_start(8, 8, 1);
		if (SHIFTREG2_FALLING_EDGE(0x8000))
			sample_stop(8);

		/* set the frequency */
		freq = 56818.181818 / (4096 - (current_shift & 0xfff));
		sample_set_freq(8, 44100 * freq / 1050);

		/* set the volume */
		/* 4 levels: MAME set_volume(vol / 3.0) -> 0, 85, 170, 255 */
		vol = (~current_shift >> 12) & 3;
		sample_set_volume(8, vol * 255 / 3);
		
		/* cannon - falling edge */
		if (SHIFTREG2_RISING_EDGE(0x4000))
			sample_start(9, 9, 0);

		/* remember the previous value */
		last_shift2 = current_shift;
	}

	/* bounce - rising edge */
	if (SOUNDVAL_RISING_EDGE(0x04))
		sample_start(10, 10, 0);

	/* bell - falling edge */
	if (SOUNDVAL_RISING_EDGE(0x08))
		sample_start(11, 11, 0);
}

/*************************************
 *
 *  War of the Worlds (color)
 *
 *************************************/

void wotwc_sound(UINT8 sound_val, UINT8 bits_changed)
{
	UINT32 target_pitch;

	/* on the rising edge of bit 0x10, clock bit 0x80 into the shift register */
	if (SOUNDVAL_RISING_EDGE(0x10))
		current_shift = ((current_shift >> 1) & 0x7f) | (sound_val & 0x80);

	/* execute on the rising edge of bit 0x01 */
	if (SOUNDVAL_RISING_EDGE(0x01))
	{
		/* fireball - falling edge */
		if (SHIFTREG_FALLING_EDGE(0x80))
			sample_start(0, 0, 0);

		/* shield hit - falling edge */
		if (SHIFTREG_FALLING_EDGE(0x40))
			sample_start(1, 1, 0);

		/* star sound - 0=off, 1=on */
		if (SHIFTREG_RISING_EDGE(0x20))
			sample_start(2, 2, 1);
		if (SHIFTREG_FALLING_EDGE(0x20))
			sample_stop(2);

		/* thrust sound - 1=off, 0=on*/
		if (SHIFTREG_FALLING_EDGE(0x10))
			sample_start(3, 3, 1);
		if (SHIFTREG_RISING_EDGE(0x10))
			sample_stop(3);

		/* drone - 1=off, 0=on */
		if (SHIFTREG_FALLING_EDGE(0x08))
			sample_start(4, 4, 1);
		if (SHIFTREG_RISING_EDGE(0x08))
			sample_stop(4);

		/* latch the drone pitch */
		target_pitch = (current_shift & 7) + ((current_shift & 2) << 2);
		target_pitch = 0x10000 + (target_pitch << 12);

		// once per frame slide the pitch toward the target
		if ((UINT32)cpu_getcurrentframe() > last_frame)
		{
			if (current_pitch > target_pitch)
				current_pitch -= 300;
			if (current_pitch < target_pitch)
				current_pitch += 200;
			sample_set_freq(4, current_pitch);
			last_frame = cpu_getcurrentframe();
		}
		 
		 /* remember the previous value */
		last_shift = current_shift;
	}

	/* loud explosion - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x02))
		sample_start(5, 5, 0);

	/* soft explosion - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x04))
		sample_start(6, 6, 0);

	/* player fire - falling edge */
	if (SOUNDVAL_FALLING_EDGE(0x08))
		sample_start(7, 7, 0);
}

/*************************************
*
*	Demon
*
*************************************/

void demon_sound(UINT8 sound_val, UINT8 bits_changed)
{
	// CPUs are interleaved by the scheduler; never execute the Z80 reentrantly.
	if ((bits_changed & 0x10) && !(sound_val & 0x10)) ++demon_command_count;
	demon_fifo.write_command(sound_val, bits_changed);
}

// QB3: OUT port 4 bypasses the sound latch and clocks the low nibble of the
// CCPU A register into the FIFO (MAME qb3_sound_w).
void qb3_sound_w(int rega)
{
	demon_sound(~rega & 0x0f, 0x10);
}

static UINT8 demon_porta_r() { return demon_fifo.read_port_a(); }
static UINT8 demon_portb_r() { return demon_fifo.read_port_b(); }
static void demon_portb_w(UINT8 data)
{
	if ((data ^ demon_fifo.read_port_b()) & 4)
		ay8910_set_mute((data & 4) != 0);
	demon_fifo.write_port_b(data);
}

static void demon_ctc_interrupt(int state)
{
	if (!m_cpu_z80[CPU1]) return;
	// Demon opts into strict daisy priority. Keep Cosmic Chasm's existing
	// callback contract unchanged, including its legacy combined state.
	state = z80ctc_irq_state(0);
	if (state & Z80_INT_REQ) m_cpu_z80[CPU1]->mz80AssertInt();
	else m_cpu_z80[CPU1]->mz80ClearPendingInterrupt();
}

PORT_WRITE_HANDLER(demon_ctc_w) { z80ctc_0_w(port & 3, data); }

MEM_READ(DemonSoundRead)
MEM_ADDR(0x0000, 0x1fff, MRA_ROM)
MEM_ADDR(0x3000, 0x33ff, MRA_RAM)
MEM_ADDR(0x4000, 0x4001, ay8910_0_data_r)
MEM_ADDR(0x5000, 0x5001, ay8910_1_data_r)
MEM_ADDR(0x6000, 0x6001, ay8910_2_data_r)
MEM_END

MEM_WRITE(DemonSoundWrite)
MEM_ADDR(0x0000, 0x1fff, MWA_ROM)
MEM_ADDR(0x3000, 0x33ff, MWA_RAM)
MEM_ADDR(0x4002, 0x4002, ay8910_0_data_w)
MEM_ADDR(0x4003, 0x4003, ay8910_0_control_w)
MEM_ADDR(0x5002, 0x5002, ay8910_1_data_w)
MEM_ADDR(0x5003, 0x5003, ay8910_1_control_w)
MEM_ADDR(0x6002, 0x6002, ay8910_2_data_w)
MEM_ADDR(0x6003, 0x6003, ay8910_2_control_w)
MEM_ADDR(0x7000, 0x7000, MWA_NOP)
MEM_END

PORT_READ(DemonSoundPortRead)
PORT_END

PORT_WRITE(DemonSoundPortWrite)
PORT_ADDR(0x00, 0x03, demon_ctc_w)
PORT_ADDR(0x1c, 0x1f, demon_ctc_w)
PORT_END

int demon_sound_start()
{
	demon_fifo.reset();
	memset(Machine->memory_region[CPU1] + 0x3000, 0, 0x400);
	z80ctc_interface ctc = {};
	ctc.num = 1;
	ctc.baseclock[0] = 3579545;
	ctc.cpu[0] = CPU1;
	ctc.intr[0] = demon_ctc_interrupt;
	z80ctc_init(&ctc);
	AY8910Config ay = {};
	ay.num_chips = 3;
	ay.base_clock = 3579545;
	for (int i = 0; i < 3; ++i) ay.mixing_level[i] = 64;
	ay.port_a_read[0] = demon_porta_r;
	ay.port_b_read[0] = demon_portb_r;
	ay.port_b_write[0] = demon_portb_w;
	if (ay8910_sh_start(&ay) != 0) {
		LOG_ERROR("Demon: AY8910 sound initialization failed");
		return 1;
	}
	// AY0 channel A controls the analog filter; it is not an audible voice.
	ay8910_set_output_mask(0, 6);
	demon_command_count = demon_ack_count = 0;
	demon_board_active = true;
	return 0;
}

int qb3_sound_start()
{
	// Same board as Demon. MAME patches the sound ROM so command $0A (sent on
	// a cube rotate) does not make the sound program overwrite itself.
	Machine->memory_region[CPU1][0x11dc] = 0x09;
	return demon_sound_start();
}

void demon_sound_post_cpu_init(int cpunum)
{
	if (cpunum != CPU1 || !m_cpu_z80[CPU1]) return;
	m_cpu_z80[CPU1]->int_ack_fn = []() -> int { ++demon_ack_count; return z80ctc_interrupt(0); };
	m_cpu_z80[CPU1]->reti_hook = []() { z80ctc_reti(0); };
}

void demon_sound_update()
{
	if (demon_board_active) ay8910_sh_update();
}

void demon_sound_stop()
{
	if (!demon_board_active) return;
	LOG_INFO("Demon sound board: %u FIFO commands, %u CTC interrupts acknowledged",
		demon_command_count, demon_ack_count);
	z80ctc_reset(0);
	if (m_cpu_z80[CPU1]) {
		m_cpu_z80[CPU1]->int_ack_fn = nullptr;
		m_cpu_z80[CPU1]->reti_hook = nullptr;
	}
	ay8910_sh_stop();
	demon_fifo.reset();
	demon_board_active = false;
}
