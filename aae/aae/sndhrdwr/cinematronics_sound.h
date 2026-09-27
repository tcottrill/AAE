#pragma once

#include "deftypes.h"


void init_cinemat_snd(void (*snd_pointer)(UINT8, UINT8));
void cinemat_shift(UINT8 sound_val, UINT8 bits_changed, UINT8 A1, UINT8 CLK);

void cini_sound_control_w(int offset, int data);
void speedfrk_sound(UINT8 sound_val, UINT8 bits_changed);
void ripoff_sound(UINT8 sound_val, UINT8 bits_changed);
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
int demon_sound_start();
int qb3_sound_start();
void qb3_sound_w(int rega);
void demon_sound_update();
void demon_sound_stop();
void demon_sound_post_cpu_init(int cpunum);
extern struct MemoryReadByte DemonSoundRead[];
extern struct MemoryWriteByte DemonSoundWrite[];
extern struct z80PortRead DemonSoundPortRead[];
extern struct z80PortWrite DemonSoundPortWrite[];
void boxingb_sound(UINT8 sound_val, UINT8 bits_changed);
void wotwc_sound(UINT8 sound_val, UINT8 bits_changed);
