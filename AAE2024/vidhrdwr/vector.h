#ifndef __VECTORV__
#define __VECTORV__

#include "aae_mame_driver.h"
#include "colordefs.h"

extern int translucency;  // translucent vectors  

int VECTOR_UPDATE();
int VECTOR_START ();

void vector_set_shift(int shift);
void vector_clear_list();
void vector_reset_list();
void vector_add_point(int x, int y, unsigned int color, int intensity);
void vector_add_clip(int minx, int miny, int maxx, int maxy);
void vector_set_flicker(float _flicker);
float vector_get_flicker(void);
void vector_set_beam(float _beam);
float vector_get_beam(void);


#endif

