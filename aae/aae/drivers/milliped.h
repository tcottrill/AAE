#pragma once

extern void milliped_interrupt(int dummy);

extern void milliped_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);
int  init_milliped();
void run_milliped();
void end_milliped();
void milliped_interrupt();


extern struct GfxDecodeInfo milliped_gfxdecodeinfo[];