#pragma once


extern void galaxian_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);
extern void galaxian_vh_screenrefresh();//struct osd_bitmap* bitmap)
extern int galaxian_vh_start(void);

extern void galaxian_flipx_w(int offset, int data);
extern void galaxian_flipy_w(int offset, int data);
extern void galaxian_attributes_w(int offset, int data);
extern void galaxian_stars_w(int offset, int data);

extern void galaxian_vh_interrupt();


