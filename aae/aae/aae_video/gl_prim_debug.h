#pragma once

#include "framework.h"

void glCircle(float cx, float cy, float r, int num_segments);
void glPoint(float x, float y);
void glLine(float sx, float sy, float ex, float ey);
void glRectfromCenter(float x, float y, float size);
void glRect(float xmin, float xmax, float ymin, float ymax);

// Changed 'angle' to float (assumes degrees) to allow for smooth rotation
void glRectR(float x, float y, float size, float angle);