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
sndhrdw\starwars.c

STARWARS MACHINE FILE - sound-board <-> main-board communication, re-ported
from MAME 0.159's audio/starwars.c to sit on top of the new machine/riot6532
6532 RIOT device instead of AAE's old fake FIFO + hand-rolled timer.

See drivers\starwars.c for notes.

***************************************************************************/

#include "aae_mame_driver.h"
#include "c012294_interface.h"
#include "starwars_snd.h"
#include "riot6532.h"
#include "tms5220.h"
#include "cpu_control.h"

// -----------------------------------------------------------------------------
// The 6532 RIOT (audio CPU side, CPU 1). One instance per driver load, wired
// up by starwars_snd_init_riot() (called from init_starwars()/init_esb() in
// drivers/starwars.cpp, after timer_init() has run) and reset by
// starwars_riot_reset() (called from the CPU0 reset callback, matching MAME's
// starwars_state::machine_reset() - the RIOT resets on a machine reset, not
// on the soundrst latch-clear).
// -----------------------------------------------------------------------------
static riot6532_device g_riot;

// Main<->sound 1-byte latches (MAME's starwars_state::m_main_data / m_sound_data).
// AAE has no scheduler synchronize()/boost_interleave() to defer these writes
// to a safe point; the two 6809s are interleaved 100 slices/frame already, so
// the latches are applied immediately from the writing CPU's own memory
// handler, matching every other cross-CPU latch in AAE.
static UINT8 main_data = 0;
static UINT8 sound_data = 0;

// TMS5220 /RS,/WS edge-detect state (see r6532_porta_w below). AAE's TMS5220
// has no rsq_w/wsq_w strobe pins like MAME's tms5220_device, so the RIOT's
// port A output bits 0 (/WS) and 1 (/RS) are edge-detected here and turned
// into single tms5220_data_w()/tms5220_status_r() calls on the falling edge -
// the same instant the real strobe would latch the bus.
static UINT8 riot_porta_prev = 0;
static UINT8 tms_status_latch = 0;

// Counter/throttle for the "sound command overwritten before being read" log
// (MAME's logerror in main_callback). sound_overrun_logged gates the log
// message itself (so a game that legitimately pumps commands faster than the
// audio CPU drains them doesn't spam the log every frame), while
// sound_overrun_count tallies every overrun -- logged as a totalled at
// starwars_sh_stop() so a test run shows whether cpu_yield() is still
// losing any commands (it should be at or near zero; see main_wr_w below).
static bool sound_overrun_logged = false;
static UINT32 sound_overrun_count = 0;

static struct TMS5220interface tms5220_interface =
{
	672000,     /* clock speed = MASTER_CLOCK/2/9 (MAME 0.159), was 640000 */
	255,        /* volume */
	0           /* IRQ handler */
};


static struct POKEYinterface pokey_interface =
{
	4,			/* 4 chips */
	1512000,	/* MASTER_CLOCK/8 (MAME 0.159), was 1500000 */
	{ 64, 64, 64, 64 },	/* volume */
	/* The 8 pot handlers */
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	/* The allpot handler */
	{ 0, 0, 0, 0 },
};


int starwars_sh_start(void)
{
	int rv;

	rv = pokey_sh_start(&pokey_interface);
	if (rv) return rv;
	return(tms5220_sh_start(&tms5220_interface));
}


void starwars_sh_stop(void)
{
	if (sound_overrun_count > 0) {
		LOG_INFO("starwars_snd: %u sound command(s) overwritten before being read this session",
			sound_overrun_count);
	}

	pokey_sh_stop();
	tms5220_sh_stop();
}

void starwars_sh_update()
{
	pokey_sh_update();
	tms5220_sh_update();
}


/*************************************
 *
 *  RIOT port callbacks
 *
 *************************************/

/* Configured as follows:           */
/* d7 (in)  Main Ready Flag         */
/* d6 (in)  Sound Ready Flag        */
/* d5 (out) Mute Speech             */
/* d4 (in)  Not Sound Self Test     */
/* d3 (out) Hold Main CPU in Reset? */
/*          + enable delay circuit? */
/* d2 (in)  TMS5220 Not Ready       */
/* d1 (out) TMS5220 Not Read        */
/* d0 (out) TMS5220 Not Write       */
/* Note: bit 4 is always set to avoid sound self test */
static UINT8 r6532_porta_r()
{
	const UINT8 olddata = g_riot.porta_in_get();
	const int readyq = tms5220_ready_r() ? 0 : 1;   /* AAE's ready_r() is active-high ready; readyq is its complement */

	return (UINT8)((olddata & 0xc0) | 0x10 | (readyq << 2));
}

static void r6532_porta_w(UINT8 data)
{
	const UINT8 prev = riot_porta_prev;

	/* /WS (bit 0) falling edge: latch whatever is currently on the RIOT's
	   port B output register into the TMS5220's data input. */
	if ((prev & 0x01) && !(data & 0x01))
		tms5220_data_w(0, g_riot.portb_out_get());

	/* /RS (bit 1) falling edge: latch the TMS5220's status byte so the next
	   port B read (r6532_portb_r below) returns it. */
	if ((prev & 0x02) && !(data & 0x02))
		tms_status_latch = (UINT8)tms5220_status_r(0);

	riot_porta_prev = data;
}

static UINT8 r6532_portb_r()
{
	return tms_status_latch;
}

static void r6532_irq_cb(int state)
{
	if (m_cpu_6809[1])
		m_cpu_6809[1]->irq_line(state != 0);
}


/*************************************
 *
 *  RIOT creation / reset
 *
 *************************************/

void starwars_snd_init_riot(int cpu, UINT32 clock)
{
	g_riot.init(cpu, clock);
	g_riot.set_in_pa_cb(r6532_porta_r);
	g_riot.set_out_pa_cb(r6532_porta_w);
	g_riot.set_in_pb_cb(r6532_portb_r);
	g_riot.set_irq_cb(r6532_irq_cb);

	starwars_riot_reset();
}

void starwars_riot_reset()
{
	g_riot.reset();
	riot_porta_prev = 0;
	tms_status_latch = 0;
	main_data = 0;
	sound_data = 0;
	sound_overrun_logged = false;
}


/*************************************
 *
 *  Sound CPU to/from main CPU
 *
 *************************************/

UINT8 sin_r(UINT32 /*address*/, struct MemoryReadByte* /*psMemRead*/)
{
	g_riot.porta_in_set(0x00, 0x80);
	return sound_data;
}


void sout_w(UINT32 /*address*/, UINT8 data, struct MemoryWriteByte* /*psMemWrite*/)
{
	/* MAME defers this through scheduler().synchronize(). That IS needed
	   here, not just a nicety: the main CPU's command-send routine at ROM
	   $BCE9 only waits ~210 cycles (LDB #$0E / TST $4401 / BPL / DECB / BNE)
	   for this reply flag before giving up and stomping the latch with 0.
	   AAE's 100-division scheduler slices are ~504 cycles at 30fps, so
	   without synchronize() the sound CPU can be interleaved too far behind
	   to clear the flag inside that window and a reply gets silently lost
	   (see main_wr_w's overrun counter below, and the mirror-image problem
	   there). cpu_yield() ends THIS CPU's (the sound CPU's) timeslice right
	   here so the main CPU resumes immediately and observes the flag before
	   its 210-cycle timeout, matching what scheduler().synchronize() buys
	   MAME. */
	g_riot.porta_in_set(0x40, 0x40);
	main_data = data;
	cpu_yield();
}



/*************************************
 *
 *  Main CPU to/from sound CPU
 *
 *************************************/

UINT8 main_read_r(UINT32 /*address*/, struct MemoryReadByte* /*psMemRead*/)
{
	g_riot.porta_in_set(0x00, 0x40);
	return main_data;
}


UINT8 main_ready_flag_r(UINT32 /*address*/, struct MemoryReadByte* /*psMemRead*/)
{
	return (UINT8)(g_riot.porta_in_get() & 0xc0);    /* only upper two flag bits mapped */
}


void main_wr_w(UINT32 /*address*/, UINT8 data, struct MemoryWriteByte* /*psMemWrite*/)
{
	if (g_riot.porta_in_get() & 0x80)
	{
		sound_overrun_count++;
		if (!sound_overrun_logged)
		{
			LOG_INFO("starwars_snd: sound data not read %x", sound_data);
			sound_overrun_logged = true;
		}
	}
	else
	{
		sound_overrun_logged = false;
	}

	/* MAME defers this through scheduler().synchronize(). This one is the
	   command path the ROM comment at the top of this file documents: main
	   CPU routine $BCE9 polls $4401 bit 7 for ~210 cycles waiting for the
	   PREVIOUS command to be read, then gives up and overwrites it with 0
	   regardless. cpu_yield() here ends the MAIN CPU's timeslice the instant
	   it writes a new command, so the sound CPU (interleaved right behind it
	   by cpu_run()) gets to run and clear the flag well inside that 210-cycle
	   window instead of however many cycles are left in the writer's current
	   ~504-cycle slice. Without this, commands sent in the same slice as a
	   still-unread previous one time out and are lost -- the overrun counter
	   above (and its total logged from starwars_sh_stop) is how a test run
	   shows whether that is still happening. */
	g_riot.porta_in_set(0x80, 0x80);
	sound_data = data;
	cpu_yield();
}


void soundrst(UINT32 /*address*/, UINT8 /*data*/, struct MemoryWriteByte* /*psMemWrite*/)
{
	g_riot.porta_in_set(0x00, 0xc0);

	/* Reset the sound CPU immediately. cpu_reset() is safe to call from
	   inside another CPU's memory handler (main CPU writes 0x46e0) - it
	   switches active_cpu to the target and restores it afterwards. Note:
	   MAME 0.159 resets only the sound CPU here, not the RIOT itself - the
	   RIOT resets on a machine reset (starwars_riot_reset(), called from
	   starwars_machine_reset in drivers/starwars.cpp), not from soundrst. */
	cpu_reset(1);
}


/*************************************
 *
 *  RIOT register access (memory map at 0x1080-0x109f)
 *
 *************************************/

UINT8 riot_r(UINT32 address, struct MemoryReadByte* /*psMemRead*/)
{
	return g_riot.reg_r((UINT8)(address & 0x1f));
}

void riot_w(UINT32 address, UINT8 data, struct MemoryWriteByte* /*psMemWrite*/)
{
	g_riot.reg_w((UINT8)(address & 0x1f), data);
}
