#pragma once
#include "deftypes.h"


/*************************************
 *
 *  Constants
 *
 *************************************/

enum
{
	COLOR_BILEVEL,
	COLOR_16LEVEL,
	COLOR_64LEVEL,
	COLOR_RGB,
	COLOR_QB3
};

void vec_control_write(int data);
void cinemat_vector_callback(INT16 sx, INT16 sy, INT16 ex, INT16 ey, UINT8 shift);
void video_type_set(int type, int swap_xy);
int cinevid_up();