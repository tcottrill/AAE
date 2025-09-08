#pragma once

#ifndef GALAGA_H
#define GALAGA_H

#pragma warning(disable:4996 4102)

extern void galaga_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);
int  init_galaga();
void run_galaga();
void end_galaga();

void galagaint();
void galagaint2();
void galagaint3();


extern struct GfxDecodeInfo galaga_gfxdecodeinfo[];




#endif