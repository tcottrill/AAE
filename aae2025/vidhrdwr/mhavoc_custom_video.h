#pragma once

#include "deftypes.h"

void mhavoc_video_init(int scale);
void mhavoc_video_update(void);
void avg_set_flip_x(int flip);
void avg_set_flip_y(int flip);
void mhavoc_colorram_w(UINT32 address, UINT8 data, struct MemoryWriteByte* pMemWrite);

