#pragma once

#include "deftypes.h"


void init_cinemat_snd(void (*snd_pointer)(UINT8, UINT8));

void cini_sound_control_w(int offset, int data);
void ripoff_sound(UINT8 sound_val, UINT8 bits_changed);
void cinemat_shift(UINT8 sound_val, UINT8 bits_changed, UINT8 A1, UINT8 CLK);
void ripoff_sound(UINT8 sound_val, UINT8 bits_changed);
void armora_sound(UINT8 sound_val, UINT8 bits_changed);
void null_sound(UINT8 sound_val, UINT8 bits_changed);
void starcas_sound(UINT8 sound_val, UINT8 bits_changed);
void solarq_sound(UINT8 sound_val, UINT8 bits_changed);
void spacewar_sound(UINT8 sound_val, UINT8 bits_changed);
void warrior_sound(UINT8 sound_val, UINT8 bits_changed);
void tailg_sound(UINT8 sound_val, UINT8 bits_changed);
void starhawk_sound(UINT8 sound_val, UINT8 bits_changed);
void barrier_sound(UINT8 sound_val, UINT8 bits_changed);
void sundance_sound(UINT8 sound_val, UINT8 bits_changed);
void demon_sound(UINT8 sound_val, UINT8 bits_changed);
void boxingb_sound(UINT8 sound_val, UINT8 bits_changed);
void wotwc_sound(UINT8 sound_val, UINT8 bits_changed);