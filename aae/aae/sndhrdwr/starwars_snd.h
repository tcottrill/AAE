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

#ifndef STARWARS_SND_H
#define STARWARS_SND_H


#include "deftypes.h"


int  starwars_sh_start(void);
void starwars_sh_stop(void);
void starwars_sh_update(void);

// Create/wire the sound board's 6532 RIOT. Call once, after timer_init() has
// run for this driver load (same rule as any other timer_alloc() caller -
// see starwars_start_irq_timer() in drivers/starwars.cpp). `cpu` is the CPU
// index the RIOT is clocked from (CPU 1, the audio 6809); `clock` is its
// frequency in Hz (1512000 = MASTER_CLOCK/8, matching MAME 0.159).
void starwars_snd_init_riot(int cpu, UINT32 clock);

// Machine-reset hook: resets the RIOT itself plus the main<->sound latches
// and TMS5220 strobe-edge state. MAME resets the RIOT only on a machine
// reset, not on the soundrst (0x46e0) latch-clear - call this from the CPU0
// reset callback (starwars_machine_reset), not from soundrst().
void starwars_riot_reset(void);

UINT8 main_read_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 main_ready_flag_r(UINT32 address, struct MemoryReadByte* psMemRead);

void main_wr_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void soundrst(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);


UINT8 riot_r(UINT32 address, struct MemoryReadByte* psMemRead);
void  riot_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);

UINT8 sin_r(UINT32 address, struct MemoryReadByte* psMemRead);
void sout_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);


#endif
