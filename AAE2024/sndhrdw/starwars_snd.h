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


void starwars_snd_interrupt();

UINT8 main_read_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 main_ready_flag_r(UINT32 address, struct MemoryReadByte* psMemRead);

void main_wr_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void soundrst(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);


UINT8 m6532_r(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 sin_r(UINT32 address, struct MemoryReadByte* psMemRead);


void sout_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
void m6532_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);



#endif
