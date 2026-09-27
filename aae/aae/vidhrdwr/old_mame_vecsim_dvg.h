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

// Legacy Eric Smith VECSIM DVG simulator, kept as a selectable alternative
// to the MAME 0.111 engine in mame_late_avgdvg.cpp. All entry points carry
// a vecsim_ prefix so both engines can be linked at once; the asteroid
// driver picks one at init from config.dvg_engine ([main] dvg_engine=
// mame|vecsim). B/W DVG games only - the AVG side of this era was removed.

#include "aae_mame_driver.h"
#include "deftypes.h"

void vecsim_dvg_reset_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
int vecsim_dvg_done(void);
void vecsim_dvg_go_w(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);

/* Plain-call strobes (omegrace: GO from a port read, VGRST from a port write) */
void vecsim_dvg_go(void);
void vecsim_dvg_reset(void);

int vecsim_dvg_start_asteroid(void);
int vecsim_dvg_start(void);
int vecsim_dvg_end();
void vecsim_test_clear_busy();
void vecsim_set_screen_flipping(int val);
