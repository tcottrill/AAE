#pragma once

#ifndef NEWVECTOR_H
#define NEWVECTOR_H



#define MIN_LINES 5
#define MAX_LINES 6000
#define LINE_END -5000


#define MAKE_RGB(r,g,b) 	((((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff))
#define MAKE_ARGB(a,r,g,b)	(MAKE_RGB(r,g,b) | (((a) & 0xff) << 24))
#define RGB_ALPHA(rgb)		(((rgb) >> 24) & 0xff)
#define RGB_RED(rgb)		(((rgb) >> 16) & 0xff)
#define RGB_GREEN(rgb)		(((rgb) >> 8) & 0xff)
#define RGB_BLUE(rgb)		((rgb) & 0xff)
//BW Vector Functions
#define RGB_ABW(v)          (MAKE_RGB(v,v,v) | (((v) & 0xff) << 24))
#define RGB_BW(v)           (MAKE_RGB(v,v,v))
#define RGB_BLACK			(MAKE_RGB(0,0,0))
#define RGB_WHITE			(MAKE_RGB(255,255,255))


#ifdef __cplusplus
extern "C" {
#endif

extern void Add_Line(int sx, int sy, int ex, int ey, int color);
extern void Add_TexPoint(int x, int y, int size, int color);
extern void Add_Point(int x, int y,int color);
extern void Reset_Vector_Draw();
extern void Render_Vectors();
extern void Set_Vector_Blendmode(int mode);
extern void Cache_Draw();


#ifdef __cplusplus
};
#endif

#endif

