//==========================================================================
// Irem sound board (M52 sound-C class) - AAE port of MAME 0.57
// sndhrdw/irem.c. Used by Moon Patrol (and shareable with the other Irem
// Z80-era boards: m52/m57/m58/m62 all carry the same sound section).
//
//   M6803 @ 3.579545/4 MHz (CPU_M6803, on-chip ports/timer in the core)
//   2 x AY-3-8910 @ 3.579545/4 MHz, strobed from the 6803's port 1/2
//   2 x MSM5205 ADPCM @ 384 kHz; #0 in master mode driving the 6803 NMI,
//       #1 slaved to #0's VCLK
//
// Wiring stays faithful to MAME: the main CPU writes the sound command via
// irem_sound_cmd_w (bit 7 clear = latch data, bit 7 set = IRQ the 6803);
// AY #0 port A reads the latch, AY #0 port B write drives the MSM5205
// playmode/reset lines, the 6803's MOVX-space writes at $0801/$0802 feed
// the ADPCM data registers.
//
// THE CODE IS DERIVED FROM MAME and COPYRIGHT the MAME TEAM.
//==========================================================================
#pragma once
#ifndef IREM_SND_H
#define IREM_SND_H

#include "deftypes.h"
#include "cpu_control.h"

// Sound-command write handler for the MAIN CPU's memory map.
WRITE_HANDLER_PUBLIC(irem_sound_cmd_w);

// Sound CPU (CPU1) memory maps for the driver's AAE_CPU_ENTRY.
extern struct MemoryReadByte IremSoundRead[];
extern struct MemoryWriteByte IremSoundWrite[];

// The audio CPU entry values (clock etc.) live in the driver; use these:
#define IREM_AUDIO_CPU_CLOCK   (3579545 / 4)

// post_cpu_init callback for the sound CPU's AAE_CPU_ENTRY_EX slot: hooks
// the 6803's on-chip port 1/2 to the AY-8910 strobe glue.
void irem_sound_post_cpu_init(int cpunum);

// Lifecycle: call from the driver's init / per-frame run / end functions.
int  irem_sound_start(void);
void irem_sound_update(void);
void irem_sound_stop(void);

#endif // IREM_SND_H
