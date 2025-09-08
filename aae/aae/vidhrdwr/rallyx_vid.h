#pragma once

#pragma once

extern void rallyx_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);
extern void rallyx_vh_screenrefresh();
extern int  rallyx_vh_start(void);

// NEW: optional stop to free the playfield dirty buffer
extern void rallyx_vh_stop(void);

// NEW: playfield (videoram2/colorram2) write handlers that mark tiles dirty
void rallyx_videoram2_w(int offset, int data);
void rallyx_colorram2_w(int offset, int data);