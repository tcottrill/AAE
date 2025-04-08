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

/*****************************************************************
machine\swmathbx.h

This file is Copyright 1997, Steve Baines.

Release 2.0 (5 August 1997)

See drivers\starwars.c for notes

******************************************************************/
#include "aae_mame_driver.h"

void translate_proms(void);
void run_mbox(void);

/* Read handlers */
UINT8 reh(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 rel(UINT32 address, struct MemoryReadByte* psMemRead);
UINT8 prng(UINT32 address, struct MemoryReadByte* psMemRead);


/* Write handlers */

void prngclr();
void swmathbx(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite);
