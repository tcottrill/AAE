#pragma once

#ifndef missile_H
#define missile_H


int init_missile();
void run_missile();
void end_missile();
extern void missile_interrupt();
void missile_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);
extern struct GfxDecodeInfo missile_gfxdecodeinfo[];


#endif