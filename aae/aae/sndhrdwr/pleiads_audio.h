#ifndef PLEIADS_AUDIO_H
#define PLEIADS_AUDIO_H

#include "aae_mame_driver.h"

// Lifecycle
int  pleiads_audio_sh_start(void);
void pleiads_audio_sh_stop(void);
void pleiads_audio_sh_update(void);

// Write Handlers (Visible to phoenix.cpp memory map)
WRITE_HANDLER_NS(pleiads_sound_control_a_w);
WRITE_HANDLER_NS(pleiads_sound_control_b_w);

// Internal helper called from phoenix.cpp pleiads_videoreg_w
void pleiads_audio_control_c_w(int data);

#endif // PLEIADS_AUDIO_H
