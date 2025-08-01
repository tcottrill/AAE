/*****************************************************************
machine\swmathbx.h

This file is Copyright 1997, Steve Baines.

Release 2.0 (5 August 1997)

See drivers\starwars.c for notes

******************************************************************/
#include "aae_mame_driver.h"

void swmathbox_init(void);
void swmathbox_reset(void);

WRITE_HANDLER_NS(swmathbx_w);
WRITE_HANDLER_NS(starwars_out_w);
WRITE_HANDLER_NS(starwars_adc_select_w);

READ_HANDLER_NS(starwars_input_1_r);
READ_HANDLER_NS(swmathbx_prng_r);
READ_HANDLER_NS(swmathbx_reh_r);
READ_HANDLER_NS(swmathbx_rel_r);
READ_HANDLER_NS(starwars_adc_r);