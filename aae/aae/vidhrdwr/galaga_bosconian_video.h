#pragma once


extern void bosco_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);
extern void bosco_vh_screenrefresh();//struct osd_bitmap* bitmap)
extern int bosco_vh_start(void);

