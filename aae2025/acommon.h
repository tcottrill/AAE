#pragma once
#ifndef ACOMMON_H
#define ACOMMON_H

void set_aae_leds(int a, int b, int c);
void video_loop(void);
void return_to_menu(void);
void sanity_check_config(void);
void setup_ambient(int style);
void AllowAccessibilityShortcutKeys(int bAllowKeys);
int vector_timer(int deltax, int deltay);
#endif