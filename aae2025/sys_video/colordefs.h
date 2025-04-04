
#ifndef __COLORDEF__
#define _COLORDEF_


typedef unsigned int rgb_t;


#ifndef MAKE_BGR
#define MAKE_BGR(r,g,b) ((((b) & 0xff) << 16) | (((g) & 0xff) << 8) | ((r) & 0xff))
#endif

#ifndef MAKE_RGB
#define MAKE_RGB(r,g,b) ((((b) & 0xff) << 16) | (((g) & 0xff) << 8) | ((r) & 0xff))
#endif

#ifndef MAKE_RGBA
#define MAKE_RGBA(r,g,b,a)  (r | (g << 8) | (b << 16) | (a << 24))
#endif
 
#ifndef RGB_BLUE
#define RGB_BLUE(rgba) (( (rgba)>>16 ) & 0xff )
#endif
 
#ifndef RGB_GREEN
#define RGB_GREEN(rgba) (( (rgba)>>8 ) & 0xff )
#endif
 
#ifndef RGB_RED
#define RGB_RED(rgba) ( rgba & 0xff )
#endif

#ifndef RGB_ALPHA
#define RGB_ALPHA(rgba) (( (rgba)>>24 ) & 0xff)
#endif

#define VECTOR_COLOR111(c) \
	MAKE_RGB((((c) >> 2) & 1) * 0xff, (((c) >> 1) & 1) * 0xff, (((c) >> 0) & 1) * 0xff)

#define VECTOR_COLOR222(c) \
	MAKE_RGB((((c) >> 4) & 3) * 0x55, (((c) >> 2) & 3) * 0x55, (((c) >> 0) & 3) * 0x55)

#define VECTOR_COLOR444(c) \
	MAKE_RGB((((c) >> 8) & 15) * 0x11, (((c) >> 4) & 15) * 0x11, (((c) >> 0) & 15) * 0x11)


// common colors 
#define RGB_BLACK			(MAKE_RGBA(0,0,0,255))
#define RGB_WHITE			(MAKE_RGBA(255,255,255,255))
#define RGB_YELLOW          (MAKE_RGBA((255,250,0,255))
#define RGB_PURPLE          (MAKE_RGBA((30,30,80,255))
#define RGB_PINK			(MAKE_RGBA( 255,0,255,255 ))
#define RGB_WHAT			(MAKE_RGBA( 255,128,255,255 ))

/*
__inline unsigned char Clamp(int n)
{
    n = n>255 ? 255 : n;
    return n<0 ? 0 : n;
}

*/

#endif
