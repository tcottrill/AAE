#pragma once

// -----------------------------------------------------------------------------
// This file is part of the AAE (Another Arcade Emulator) project.
// Portions are derived from the M.A.M.E.(TM) Project and remain under their
// original copyright terms. See the accompanying source files for full
// license details.
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
//OS Input configuration from M.A.M.E.(TM)

#include "osdepend.h"

void os_init_input(void);
static int pseudo_to_key_code(int keycode);
// osd_key_pressed_for: multi-keyboard — routes the check to the player's
// assigned device. Declared in osdepend.h along with the rest of the
// OSD input contract.

