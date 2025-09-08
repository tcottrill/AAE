#pragma once

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

#ifndef SEGAG80_H
#define SEGAG80_H

#include "deftypes.h"

void end_segag80(void);
int init_segag80(void);
void run_segag80(void);
void run_tacscan(void);
void sega_interrupt();

int init_spacfury();
int init_tacscan();
int init_zektor();
int init_startrek();
int init_elim2();
int init_elim4();





#endif