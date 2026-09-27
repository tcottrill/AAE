/*****************************************************************
machine\swmathbx.h

This file is Copyright 1997, Steve Baines.

Release 2.0 (5 August 1997)

See drivers\starwars.c for notes

******************************************************************/
#include "aae_mame_driver.h"

void swmathbox_init(void);
void swmathbox_timer_init(void);
void swmathbox_reset(void);

WRITE_HANDLER_NS(swmathbx_w);
WRITE_HANDLER_NS(starwars_out_w);
WRITE_HANDLER_NS(starwars_nstore_w);
WRITE_HANDLER_NS(starwars_adc_select_w);

READ_HANDLER_NS(starwars_input_1_r);
READ_HANDLER_NS(swmathbx_prng_r);
READ_HANDLER_NS(swmathbx_reh_r);
READ_HANDLER_NS(swmathbx_rel_r);
READ_HANDLER_NS(starwars_adc_r);

// X2212 NVRAM store/recall lines (256 x 4-bit chip). Defined in
// drivers/starwars.cpp, next to the SRAM/EEPROM arrays; pulsed from
// starwars_nstore_w (store) and starwars_out_w case 7 (recall) below.
// Both are edge-triggered on the line going active (non-zero), matching
// x2212_device::store()/recall() (WRITE_LINE_MEMBER) in MAME 0.159.
void x2212_store_line_w(int state);
void x2212_recall_line_w(int state);
