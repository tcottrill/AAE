#pragma once

#pragma once

extern void centiped_interrupt(int dummy);

extern void centiped_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);
int  init_centiped();
void run_centiped();
void end_centiped();
void centiped_interrupt();


extern struct GfxDecodeInfo centiped_gfxdecodeinfo[];