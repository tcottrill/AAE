#ifndef PHOENIX_AUDIO_H
#define PHOENIX_AUDIO_H

#include "aae_mame_driver.h"

// Lifecycle
int  phoenix_audio_sh_start(void);
void phoenix_audio_sh_stop(void);
void phoenix_audio_sh_update(void);

// Write Handlers (Visible to phoenix.cpp memory map)
WRITE_HANDLER_NS(phoenix_sound_control_a_w);
WRITE_HANDLER_NS(phoenix_sound_control_b_w);

#endif // PHOENIX_AUDIO_H
