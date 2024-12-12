
#include "allegro.h"
#include "alleggl.h"
#include <stdio.h>
#include <stdlib.h>

#include "aae_mame_driver.h"
#include "vector_fonts.h"
#include "rotation_table.h"
#include "colordefs.h"

#define FONT_SPACING 9.5
#define EOC  256

//EXCLM		33,3.5,2,3.5,6,3.5,0,3.5,1,EOC,
//PRCENT	37,0,0,7,6,1,6,1,5,6,0,6,1,EOC,
//APOST		39,3.5,6,3.5,5,EOC
//APOST		44,3.5,6,3.5,5,EOC
//DASH		45,1,3,6,3,EOC,
//PERIOD	46,3,0,4,0,EOC,
//SLASH		47,0,7,7,0,EOC,
//zero		48,0,0,7,0,7,0,7,6,7,6,0,6,0,6,0,0,EOC,
//ONE		49,3.5,0,3.5,6,EOC,
//TWO		50,7,0,0,0,0,0,0,3,0,3,7,3,7,3,7,6,7,6,0,6,EOC,
//THREE		51,0,0,7,0,7,0,7,3,7,3,4,3,4,3,7,6,7,6,0,6,EOC,
//four		52,5,0,5,6,5,6,0,3,0,3,7,3,EOC,
//FIVE		53,0,3,0,6,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,3,EOC,
//SIX		54,0,0,0,6,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,3,EOC,
//seven		55,2,0,7,6,7,6,0,6,EOC,
//EIGHT     56,0,0,0,6,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,6,EOC,
//NINE	    57,0,0,7,0,7,0,7,6,7,6,0,6,0,6,0,3,0,3,7,3,EOC,
//QUESTION	63,0,6,7,6,7,6,7,3,7,3,2,3,2,3,2,2,2,0,2,1,EOC,
//UNDRSCOR	95,1,0,6,0,EOC
//LEFTBK	123,2,0,1,0,1,0,0,1,0,1,0,5,0,5,1,6,1,6,2,6,EOC,
//RIGHTBK   125,5,0,6,0,6,0,7,1,7,1,7,5,7,5,6,6,6,6,5,6,EOC,
//UP		128,0,0,3.5,6,3.5,6,7,0,7,0,0,0,EOC,
//DOWN		129,0,6,3.5,0,3.5,0,7,6,0,6,7,6,EOC,
//RIGHT		130,0,0,0,6,0,6,7,3.5,7,3.5,0,0,EOC,
//LEFT		131,0,3.5,7,0,7,6,7,0,7,6,0,3.5,EOC,
//CPYRGHT	132,0,0,7,0,7,0,7,6,7,6,0,6,0,6,0,0,1,1,5,1,5,5,1,5,1,5,1,1,EOC,

//A 65,0,0,0,3, 0,6,7,6, 0,3,7,3, 0,0,7,0, 7,0,7,6, EOC,
//B	66,0,0,0,6,0,6,6,6,6,6,7,5,7,5,7,4,7,4,6,3,6,3,0,3,0,0,6,0,6,0,7,1,7,1,7,2,7,2,6,3,EOC,
//C	67,0,0,7,0,7,6,0,6,0,6,0,0,EOC,
//D	68,0,0,0,6,0,6,6,6,6,6,7,5,7,5,7,1,7,1,6,0,6,0,0,0, EOC,
//E	69,0,0,0,6,0,6,7,6,7,6,7,3,7,3,0,3,0,0,7,0,EOC,
//F	70,0,0,0,6,0,6,7,6,0,3,4,3,EOC,
//G	71,0,0,0,6,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,3,EOC,
//H	72,0,0,0,6,0,3,7,3,7,0,7,6,EOC,
//I	73,3.5,0,3.5,6,EOC,
//J	74,0,1,1,0,1,0,6,0,6,0,7,1,7,1,7,6,EOC,
//K	75,0,0,0,6,0,2,7,6,3,4,7,0,EOC,
//L	76,0,0,7,0,0,6,0,0,EOC,
//M	77,0,0,0,6,0,6,3,6,3,6,4,5,4,5,4,0,4,5,6,6,6,6,7,6,7,6,7,0,EOC,
//N	78,0,0,0,6,0,4,2,6,2,6,7,6,7,6,7,0,EOC,
//O	79,0,0,7,0,7,0,7,6,7,6,0,6,0,6,0,0,EOC,
//P	80,0,0,0,6,0,6,7,6,7,6,7,3,7,3,0,3,EOC,
//Q	81,0,0,0,6,0,6,7,6,7,6,7,2,7,2,6,0,6,0,0,0,5,2,7,0,EOC,
//R	82,0,0,0,6,0,4,2,6,2,6,7,6,EOC,
//S	83,0,0,7,0,7,0,7,3,7,3,0,3,0,3,0,6,0,6,7,6, EOC,
//T	84,3.5,0,3.5,6,0,6,7,6,EOC,
//U	85,0,0,7,0,7,0,7,6,0,6,0,0,EOC,
//V	86,0,6, 3.5,0,3.5,0,7,6,EOC,
//W	87,0,6,0,0,0,0,3.5,3,3.5,3,7,0,7,0,7,6,EOC,
//X	88,0,0,7,6,0,6,7,0,EOC,
//Y	89,0,6,3.5,3,3.5,3,3.5,0,3.5,3,7,6, EOC,
//Z	90,0,6,7,6,7,6,0,0,0,0,7,0,EOC,

//A 97,0,0,0,3, 0,6,7,6, 0,3,7,3, 0,0,7,0, 7,0,7,6, EOC,
//B	98,0,0,0,6,0,6,6,6,6,6,7,5,7,5,7,4,7,4,6,3,6,3,0,3,0,0,6,0,6,0,7,1,7,1,7,2,7,2,6,3,EOC,
//C	99,0,0,7,0,7,6,0,6,0,6,0,0,EOC,
//D	100,0,0,0,6,0,6,6,6,6,6,7,5,7,5,7,1,7,1,6,0,6,0,0,0, EOC,
//E	101,0,0,0,6,0,6,7,6,7,6,7,3,7,3,0,3,0,0,7,0,EOC,
//F	102,0,0,0,6,0,6,7,6,0,3,4,3,EOC,
//G	103,0,0,0,6,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,3,EOC,
//H	104,0,0,0,6,0,3,7,3,7,0,7,6,EOC,
//I	105,3.5,0,3.5,6,EOC,
//J	106,0,1,1,0,1,0,6,0,6,0,7,1,7,1,7,6,EOC,
//K	107,0,0,0,6,0,2,7,6,3,4,7,0,EOC,
//L	108,0,0,7,0,0,6,0,0,EOC,
//M	109,0,0,0,6,0,6,3,6,3,6,4,5,4,5,4,0,4,5,6,6,6,6,7,6,7,6,7,0,EOC,
//N	110,0,0,0,6,0,4,2,6,2,6,7,6,7,6,7,0,EOC,
//O	111,0,0,7,0,7,0,7,6,7,6,0,6,0,6,0,0,EOC,
//P	112,0,0,0,6,0,6,7,6,7,6,7,3,7,3,0,3,EOC,
//Q	113,0,0,0,6,0,6,7,6,7,6,7,2,7,2,6,0,6,0,0,0,5,2,7,0,EOC,
//R	114,0,0,0,6,0,4,2,6,2,6,7,6,EOC,
//S	115,0,0,7,0,7,0,7,3,7,3,0,3,0,3,0,6,0,6,7,6, EOC,
//T	116,3.5,0,3.5,6,0,6,7,6,EOC,
//U	117,0,0,7,0,7,0,7,6,0,6,0,0,EOC,
//V	118,0,6, 3.5,0,3.5,0,7,6,EOC,
//W	119,0,6,0,0,0,0,3.5,3,3.5,3,7,0,7,0,7,6,EOC,
//X	120,0,0,7,6,0,6,7,0,EOC,
//Y	121,0,6,3.5,3,3.5,3,3.5,0,3.5,3,7,6, EOC,
//Z	122,0,6,7,6,7,6,0,0,0,0,7,0,EOC,

//GLuint base = 0;
int fstart[257];
static int lastx = 0;
static float lastscale = 1.0;
int fangle = 0;

float xcenter;
float ycenter;

float fontdata[] = {
32,EOC,
33,3.5,2,3.5,6,3.5,0,3.5,1,EOC,
37,0,0,7,6,1,6,1,5,6,0,6,1,EOC,
39,3.5,6,3.5,5,EOC,

40, 2, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 5, 0, 5, 1, 6, 1, 6, 2, 6, EOC,
41, 5, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 5, 7, 5, 6, 6, 6, 6, 5, 6, EOC,

44,3.5,6,3.5,5,EOC,
45,1,3,6,3,EOC,
46,3,0,4,0,EOC,
47,0,7,7,0,EOC,
48,0,0,7,0,7,0,7,6,7,6,0,6,0,6,0,0,EOC,
49,3.5,0,3.5,6,EOC,
50,7,0,0,0,0,0,0,3,0,3,7,3,7,3,7,6,7,6,0,6,EOC,
51,0,0,7,0,7,0,7,3,7,3,4,3,4,3,7,6,7,6,0,6,EOC,
52,5,0,5,6,5,6,0,3,0,3,7,3,EOC,
53,0,3,0,6,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,3,EOC,
54,0,0,0,6,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,3,EOC,
55,2,0,7,6,7,6,0,6,EOC,
56,0,0,0,6,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,6,EOC,
57,0,0,7,0,7,0,7,6,7,6,0,6,0,6,0,3,0,3,7,3,EOC,

60, 0, 3, 7, 0, 7, 0, 7, 7, 7,7,0,3, EOC, //<
62,0,0,7,3,7,3,0,7,0,7,0,0,EOC, //>

63,0,6,7,6,7,6,7,3,7,3,2,3,2,3,2,2,2,0,2,1,EOC,
95,1,0,6,0,EOC,
123,2,0,1,0,1,0,0,1,0,1,0,5,0,5,1,6,1,6,2,6,EOC,
125,5,0,6,0,6,0,7,1,7,1,7,5,7,5,6,6,6,6,5,6,EOC,
128,0,0,3.5,6,3.5,6,7,0,7,0,0,0,EOC,
129,0,6,3.5,0,3.5,0,7,6,0,6,7,6,EOC,
130,0,0,0,6,0,6,7,3.5,7,3.5,0,0,EOC,
131,0,3.5,7,0,7,6,7,0,7,6,0,3.5,EOC,
132,0,0,7,0,7,0,7,6,7,6,0,6,0,6,0,0,1,1,5,1,5,5,1,5,1,5,1,1,EOC,
65,0,0,0,3, 0,6,7,6, 0,3,7,3, 0,0,7,0, 7,0,7,6, EOC,
66,0,0,0,6,0,6,6,6,6,6,7,5,7,5,7,4,7,4,6,3,6,3,0,3,0,0,6,0,6,0,7,1,7,1,7,2,7,2,6,3,EOC,
67,0,0,7,0,7,6,0,6,0,6,0,0,EOC,
68,0,0,0,6,0,6,6,6,6,6,7,5,7,5,7,1,7,1,6,0,6,0,0,0, EOC,
69,0,0,0,6,0,6,7,6,7,6,7,3,7,3,0,3,0,0,7,0,EOC,
70,0,0,0,6,0,6,7,6,0,3,4,3,EOC,
71,0,0,0,6,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,3,EOC,
72,0,0,0,6,0,3,7,3,7,0,7,6,EOC,
73,3.5,0,3.5,6,EOC,
74,0,1,1,0,1,0,6,0,6,0,7,1,7,1,7,6,EOC,
75,0,0,0,6,0,2,7,6,3,4,7,0,EOC,
76,0,0,7,0,0,6,0,0,EOC,
77,0,0,0,6,0,6,3,6,3,6,4,5,4,5,4,0,4,5,6,6,6,6,7,6,7,6,7,0,EOC,
78,0,0,0,6,0,4,2,6,2,6,7,6,7,6,7,0,EOC,
79,0,0,7,0,7,0,7,6,7,6,0,6,0,6,0,0,EOC,
80,0,0,0,6,0,6,7,6,7,6,7,3,7,3,0,3,EOC,
81,0,0,0,6,0,6,7,6,7,6,7,2,7,2,6,0,6,0,0,0,5,2,7,0,EOC,
82,0,0,0,6,0,4,2,6,2,6,7,6,EOC,
83,0,0,7,0,7,0,7,3,7,3,0,3,0,3,0,6,0,6,7,6, EOC,
84,3.5,0,3.5,6,0,6,7,6,EOC,
85,0,0,7,0,7,0,7,6,0,6,0,0,EOC,
86,0,6, 3.5,0,3.5,0,7,6,EOC,
87,0,6,0,0,0,0,3.5,3,3.5,3,7,0,7,0,7,6,EOC,
88,0,0,7,6,0,6,7,0,EOC,
89,0,6,3.5,3,3.5,3,3.5,0,3.5,3,7,6, EOC,
90,0,6,7,6,7,6,0,0,0,0,7,0,EOC,

91, 2, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 5, 0, 5, 1, 6, 1, 6, 2, 6, EOC,
93, 5, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 5, 7, 5, 6, 6, 6, 6, 5, 6, EOC,

97, 0,0 ,0,3, 0,6, 7,6, 0,3, 7,3, 0,0, 7,0, 7,0, 7,6, EOC,
98,0,0,0,6,0,6,6,6,6,6,7,5,7,5,7,4,7,4,6,3,6,3,0,3,0,0,6,0,6,0,7,1,7,1,7,2,7,2,6,3,EOC,
99,0,0,7,0,7,6,0,6,0,6,0,0,EOC,
100,0,0,0,6,0,6,6,6,6,6,7,5,7,5,7,1,7,1,6,0,6,0,0,0, EOC,
101,0,0,0,6,0,6,7,6,7,6,7,3,7,3,0,3,0,0,7,0,EOC,
102,0,0,0,6,0,6,7,6,0,3,4,3,EOC,
103,0,0,0,6,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,3,EOC,
104,0,0,0,6,0,3,7,3,7,0,7,6,EOC,
105,3.5,0,3.5,6,EOC,
106,0,1,1,0,1,0,6,0,6,0,7,1,7,1,7,6,EOC,
107,0,0,0,6,0,2,7,6,3,4,7,0,EOC,
108,0,0,7,0,0,6,0,0,EOC,
109,0,0,0,6,0,6,3,6,3,6,4,5,4,5,4,0,4,5,6,6,6,6,7,6,7,6,7,0,EOC,
110,0,0,0,6,0,4,2,6,2,6,7,6,7,6,7,0,EOC,
111,0,0,7,0,7,0,7,6,7,6,0,6,0,6,0,0,EOC,
112,0,0,0,6,0,6,7,6,7,6,7,3,7,3,0,3,EOC,
113,0,0,0,6,0,6,7,6,7,6,7,2,7,2,6,0,6,0,0,0,5,2,7,0,EOC,
114,0,0,0,6,0,4,2,6,2,6,7,6,EOC,
115,0,0,7,0,7,0,7,3,7,3,0,3,0,3,0,6,0,6,7,6, EOC,
116,3.5,0,3.5,6,0,6,7,6,EOC,
117,0,0,7,0,7,0,7,6,0,6,0,0,EOC,
118,0,6, 3.5,0,3.5,0,7,6,EOC,
119,0,6,0,0,0,0,3.5,3,3.5,3,7,0,7,0,7,6,EOC,
120,0,0,7,6,0,6,7,0,EOC,
121,0,6,3.5,3,3.5,3,3.5,0,3.5,3,7,6, EOC,
122,0,6,7,6,7,6,0,0,0,0,7,0,EOC,
//123, 2, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 5, 0, 5, 1, 6, 1, 6, 2, 6, EOC, //<|
//125, 5, 0, 6, 0, 6, 0, 7, 1, 7, 1, 7, 5, 7, 5, 6, 6, 6, 6, 5, 6, EOC, // |>
 -5,-5 };

// Machine->drv->vector_width, Machine->drv->vector_height
/*
float getxscale(float val)
{
	float s = (float)Machine->drv->vector_width / 1024.0;
	//if (s > 1.0)
		//s = (float) 1024.0 / (float)Machine->drv->vector_width;
	return 1.0f * val;
	//return val * ((float)Machine->drv->vector_width / 1024.0);
}

float getyscale(float val)
{
	//return val * ((float)Machine->drv->vector_height / 768.0);
	float s = (float)Machine->drv->vector_height / 768.0;
	//if (s > 1.0)
		//s = (float) 768.0 / (float)Machine->drv->vector_height;
	return val * 1.0f ;
}

float get_scaleh()
{
	float s = (float)Machine->drv->vector_height / 768.0;
	//if (s > 1.0)
	//	s = (float) 768.0 / (float)Machine->drv->vector_height;
	return 1.0f;
}

float get_scalew()
{
	//float s = (float)Machine->drv->vector_width / 1024.0;
	//if (s > 1.0)
	float	s = (float) 1024.0 / (float)Machine->drv->vector_width;
	return 1.0f;
}
*/


void font_init(void)
{
	int a = 0;
	int d = 0;

	for (a = 0; a < 257; a++)
	{
		fstart[a] = 32;
	}  //Set all default values to nothing
	a = 0;
	while (fontdata[a] > -1)
	{
		d = (int)fontdata[a];
		if (d > 31 && d < 255) //We have identified a character.
		{
			fstart[d] = a; //This is the start of a new character.
		}
		a++;
	}
}

void font_remove(void)									    // Delete The Font From Memory
{
}

void fontmode_start()
{
	//
	//Why is there settings for changing the screen in this code?
	//

	/*
	glPushMatrix();
	glMatrixMode(GL_PROJECTION);							// Select The Projection Matrix
	glLoadIdentity();
	// Reset The Projection Matrix
	glViewport(0, 0, 1024, 768);//Machine->drv->vector_width, Machine->drv->vector_height);
	glOrtho(1024, 0,768,0, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);								// Select The Modelview Matrix
	glLoadIdentity();
	*/
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	//
	// To Be fixed with driver changes/updates
	//

	//switch (Machine->gamedrv->flags)
	//{
	//case 0: fangle = 0; break;
	//case 5: fangle = 90; break;
	//case 3: fangle = 180; break;
	//case 6: fangle = 270; break;
	//}
	fangle = 0;
	/* compute the min/max values */
	int xmin = Machine->gamedrv->visible_area.min_x;
	int ymin = Machine->gamedrv->visible_area.min_y;
	int xmax = Machine->gamedrv->visible_area.max_x;
	int ymax = Machine->gamedrv->visible_area.max_y;

	/* determine the center points */
	xcenter = ((xmax + xmin) / 2);
	ycenter = ((ymax + ymin) / 2);
}

void fontmode_end()
{
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glColor4ub(255, 255, 255, 255);
	glPopMatrix();
	glLoadIdentity();
}

spoint RotateAroundPoint(float x, float y, float cx, float cy, float cosTheta, float sinTheta)
{
	spoint p;
	p.x = ((((x)-(cx)) * (cosTheta)) - (((y)-(cy)) * (sinTheta))) + (cx);
	p.y = ((((x)-(cx)) * (sinTheta)) + (((y)-(cy)) * (cosTheta))) + (cy);
	return p;
}


//Note to self:
// All of the printing and rotation is a complete test hack, please fix

void fprint(float x, int y, unsigned int color, float scale, const char* fmt, ...)	// Where The Printing Happens
{
	int a = 0;
	int b = 1;
	int i = 0;
	int len;

	spoint p0;
	spoint p1;

	float radians;
	float cosTheta;
	float sinTheta;

	char		text[EOC] = "";								// Holds Our String
	va_list		ap;										// Pointer To List Of Arguments

	if (fmt == NULL)									// If There's No Text
		return;											// Do Nothing

	va_start(ap, fmt);									// Parses The String For Variables
	vsprintf(text, fmt, ap);						// And Converts Symbols To Actual Numbers
	va_end(ap);											// Results Are Stored In Text

	glColor4ub(RGB_RED(color), RGB_GREEN(color), RGB_BLUE(color), 255);

	len = strnlen(text, 255);
	lastscale = scale;
	spoint center = { 580 , 325 }; // { xcenter, ycenter };/

	if (fangle)
	{
		radians = _dtorad[fangle];
		cosTheta = _cos[fangle];
		sinTheta = _sin[fangle];
	}
	
	for (i = 0; i < len; i++)
	{
		a = fstart[text[i]];
		a++;
		b = a + 1;
		while (fontdata[a] != EOC)
		{
			
			if (fangle)
			{
				p0 = RotateAroundPoint((fontdata[a] * scale) + x, (fontdata[b] * scale) + y,
					center.x, center.y, cosTheta, sinTheta);

				p1 = RotateAroundPoint((fontdata[a + 2] * scale) + x, (fontdata[b + 2] * scale) + y,
					center.x, center.y, cosTheta, sinTheta);
			}
			else
			{

				p0 = { (fontdata[a] * scale) + x, (fontdata[b] * scale) + y };
				p1 = { (fontdata[a + 2] * scale) + x, (fontdata[b + 2] * scale) + y };
			}
			//TODO: Change this to proper rendering.

			glBegin(GL_LINES);
			glVertex2f(p0.x, p0.y);// Top Left
			glVertex2f(p1.x, p1.y);// Top Left
			glEnd();

			glBegin(GL_POINTS);
			glVertex2f(p0.x, p0.y);// Top Left
			glVertex2f(p1.x, p1.y);// Top Left
			glEnd();

			a += 4; b += 4;
		}

		x += (FONT_SPACING * scale);
	}

	lastx = x;
}

int get_string_len()
{
	return lastx + (3.5 * lastscale);
}

void fprintc(int y, unsigned int color, float scale, const char* string)
{
	int len;
	float total = 0;
	float x = 1024 / 2;
	len = strnlen(string, 255);

	total = len * (FONT_SPACING * scale);
	total = total / 2;
	x -= total;
	fprint(x, y, color, scale, string);
}

float v_get_string_pitch(const char* string, float scale, int set)
{
	int c = 0;
	int i = 0;
	float total = 0;
	int len = 0;

	len = strlen(string);

	if (set == 1) c = 128;
	for (i = 0; i < len; i++) //<=
	{
		c = string[i];
	}
	//total+=fwidth[(string[0]-32)]/2;
	return total * scale;
}