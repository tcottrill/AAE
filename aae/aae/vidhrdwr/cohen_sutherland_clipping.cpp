// C program to implement Cohen Sutherland algorithm
// for line clipping.

#include <stdio.h>
#include "sys_log.h"

// Defining region codes
const int INSIDE = 0; // 0000
const int LEFT = 1; // 0001
const int RIGHT = 2; // 0010
const int BOTTOM = 4; // 0100
const int TOP = 8; // 1000

// Defining x_max, y_max and x_min, y_min for
// clipping rectangle. Since diagonal points are
// enough to define a rectangle
int x_max;
int y_max;
int x_min;
int y_min;

void set_clip_rect(int xmin, int ymin, int xmax, int ymax)
{
	x_max = xmax;
	y_max = ymax;
	x_min = xmin;
	y_min = ymin;
}

// Function to compute region code for a point(x, y)
int computeCode(int x, int y)
{
	// initialized as being inside
	int code = INSIDE;

	if (x < x_min) // to the left of rectangle
		code |= LEFT;
	else if (x > x_max) // to the right of rectangle
		code |= RIGHT;
	if (y < y_min) // below the rectangle
		code |= BOTTOM;
	else if (y > y_max) // above the rectangle
		code |= TOP;

	return code;
}

// Implementing Cohen-Sutherland algorithm
// Clipping a line from P1 = (x2, y2) to P2 = (x2, y2)
int ClipLine(int* x1, int* y1, int* x2, int* y2)
{
	// Compute region codes for P1, P2
	int code1 = computeCode(*x1, *y1);
	int code2 = computeCode(*x2, *y2);

	// Initialize line as outside the rectangular window
	int accept = 0;

	while (true) 
	{
		if ((code1 == 0) && (code2 == 0))
		{
			// If both endpoints lie within rectangle
			accept = 1;
			break;
		}
		else if (code1 & code2) {
			// If both endpoints are outside rectangle,
			// in same region - DON"T DRAW ANY LINES, TODO: find a better way to do this. 
			//*x1 = -100;
			//*x2 = -100;
			//*y1 = -100;
			//*y2 = -100;
			accept = 0;
			break;
		}
		else {
			// Some segment of line lies within the
			// rectangle
			int code_out;
			int x = 0;
			int y = 0;

			// At least one endpoint is outside the
			// rectangle, pick it.
			if (code1 != 0)
				code_out = code1;
			else
				code_out = code2;

			// Find intersection point;
			// using formulas y = y1 + slope * (x - x1),
			// x = x1 + (1 / slope) * (y - y1)
			if (code_out & TOP) {
				x = *x1 + (*x2 - *x1) * (y_max - *y1) / (*y2 - *y1);
				y = y_max;
			}
			else if (code_out & BOTTOM) {
				x = *x1 + (*x2 - *x1) * (y_min - *y1) / (*y2 - *y1);
				y = y_min;
			}
			else if (code_out & RIGHT) {
				y = *y1 + (*y2 - *y1) * (x_max - *x1) / (*x2 - *x1);
				x = x_max;
			}
			else if (code_out & LEFT) {
				y = *y1 + (*y2 - *y1) * (x_min - *x1) / (*x2 - *x1);
				x = x_min;
			}
			// Now intersection point x, y is found
			// We replace point outside rectangle
			// by intersection point
			if (code_out == code1) {
				*x1 = x;
				*y1 = y;
				code1 = computeCode(*x1, *y1);
			}
			else {
				*x2 = x;
				*y2 = y;
				code2 = computeCode(*x2, *y2);
			}
		}
	}

	return accept;
}
