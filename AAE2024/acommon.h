#pragma once
#ifndef ACOMMON_H
#define ACOMMON_H

void set_aae_leds(int a, int b, int c);
void NoWrite(UINT32 address, UINT8 data, struct MemoryWriteByte* psMemWrite);
unsigned char randRead(unsigned int address, struct MemoryReadByte* psMemRead);

void center_window(void);
void Set_ForeGround(void);

void video_loop(void);
void return_to_menu(void);
void sanity_check_config(void);
void setup_ambient(int style);

void do_AYsound(void);
void playstreamedsample(int channel, signed char* data, int len, int vol);
void AllowAccessibilityShortcutKeys(int bAllowKeys);
int vector_timer(int deltax, int deltay);
#endif