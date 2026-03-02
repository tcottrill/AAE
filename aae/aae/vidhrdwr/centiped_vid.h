#pragma once


extern void centiped_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);
extern void centiped_vh_screenrefresh();//struct osd_bitmap* bitmap)
extern int centiped_vh_start(void);

