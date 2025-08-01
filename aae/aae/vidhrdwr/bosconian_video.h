#pragma once


extern void bosco_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);
extern void bosco_vh_screenrefresh();
extern int bosco_vh_start(void);
extern void bosco_vh_interrupt(void);
extern void bosco_vh_stop(void);

