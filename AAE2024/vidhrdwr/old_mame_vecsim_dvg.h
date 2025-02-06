#pragma once

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

#include "aae_mame_driver.h"
#include "deftypes.h"


void dvg_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
int dvg_done(void);
void dvg_go_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);

int dvg_start_asteroid(void);
int dvg_start(void);
int dvg_end();
