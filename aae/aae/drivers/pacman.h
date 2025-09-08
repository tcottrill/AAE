#pragma once

#ifndef pacman_H
#define pacman_H

#pragma warning(disable:4996 4102)

extern void pacman_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);
int  init_pacman();
void run_pacman();
void end_pacman();
int init_mspacman();
void pacman_interrupt();


extern struct GfxDecodeInfo pacman_gfxdecodeinfo[];




#endif