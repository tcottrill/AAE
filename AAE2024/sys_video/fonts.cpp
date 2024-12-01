#include "stdio.h" //Required for vsprintf
#include "fonts.h"
#include "glcode.h"
#include "gl_texturing.h" //For font_tex[1]  ??? why not have it here?

GLuint base = 0;
GLuint font_tex[2];

float fwidth[] = { 9,8,8,18,18,22,19,5,8,8,12,20,5,9,5,10,19,13,17,16,20,16,18,15,18,17,5,5,20,20,20,17,24,17,18,16,19,16,
				16,18,17,7,14,16,13,23,19,19,18,19,18,18,16,18,19,27,17,17,18,8,10,8,24,12,5,15,15,13,15,13,10,15,14,6,
				8,14,6,21,14,15,15,15,11,14,11,14,16,22,14,14,14,12,12,12,20,9,9,9,9,19,9,24,12,12,12,32,9,10,29,9,9,9,
				9,4,12,9,9,14,12,24,12,9,9,10,22,9,9,9,8,6,9,16,16,25,19,6,11,11,14,17,8,8,8,8,16,16,16,16,16,16,16,16,
				16,16,8,8,17,17,17,12,22,16,16,14,17,14,11,22,17,6,9,16,11,23,17,22,14,22,16,14,13,16,17,25,17,14,15,11,
				10,11,17,14,8,16,16,11,16,16,8,16,14,6,9,12,6,20,14,16,16,16,8,12,9,14,14,20,12,14,11,11,6,11,17,14,14,
				14,6,16,11,28,14,14,8,28,14,9,28,14,14,14,14,6,6,11,11,14,14,28,8,28,12,9,26,14,11,14 };

void BuildFont(void)						// Build Our Font Display List
{
	float	cx = 0;										// Holds Our X Character Coord
	float	cy = 0;										// Holds Our Y Character Coord
	int loop;

	// Creating 256 Display Lists
	base = glGenLists(256);
	
	make_single_bitmap(&font_tex[1], "font.png", "aae.zip", 0);

	glBindTexture(GL_TEXTURE_2D, font_tex[1]);			    // Select Our Font Texture

	for (loop = 0; loop < 256; loop++)					    // Loop Through All 256 Lists
	{
		cx = (float)(loop % 16) / 16.0f;					// X Position Of Current Character
		cy = (float)(loop / 16) / 16.0f;					// Y Position Of Current Character

		glNewList(base + loop, GL_COMPILE);				// Start Building A List
		glBegin(GL_QUADS);							// Use A Quad For Each Character
		//	glColor4f(.1f,.1f,.1f,1.0f);
		glTexCoord2f(cx, 1 - cy - 0.0625f);			// Texture Coord (Bottom Left)
		glVertex2i(0, 0);						// Vertex Coord (Bottom Left)
		//glColor4f(.1f,.1f,.1f,1.0f);
		glTexCoord2f(cx + 0.0625f, 1 - cy - 0.0625f);	// Texture Coord (Bottom Right)
		glVertex2i(31, 0);						// Vertex Coord (Bottom Right)
		//	glColor4f(1.0f,1.0f,1.0f,1.0f);
		glTexCoord2f(cx + 0.0625f, 1 - cy);			// Texture Coord (Top Right)
		glVertex2i(31, 31);						// Vertex Coord (Top Right)
		//	glColor4f(1.0f,1.0f,1.0f,1.0f);
		glTexCoord2f(cx, 1 - cy);					// Texture Coord (Top Left)
		glVertex2i(0, 31);						// Vertex Coord (Top Left)
		glEnd();									// Done Building Our Quad (Character)
		glTranslated(((fwidth[loop])), 0, 0);
		glEndList();									// Done Building The Display List
	}													// Loop Until All 256 Are Built
	glBindTexture(GL_TEXTURE_2D, 0);
}

void KillFont(void)									    // Delete The Font From Memory
{
	glDeleteLists(base, 256);							// Delete All 256 Display Lists
	glDeleteTextures(1, &font_tex[1]);
}

void glPrint(float x, int y, int r, int g, int b, int alpha, float scale, float angle, int set, const char* fmt, ...)	// Where The Printing Happens
{
	char		text[256] = "";								// Holds Our String
	va_list		ap;										// Pointer To List Of Arguments

	if (fmt == NULL)									// If There's No Text
		return;											// Do Nothing

	va_start(ap, fmt);									// Parses The String For Variables
	vsprintf(text, fmt, ap);						// And Converts Symbols To Actual Numbers
	va_end(ap);											// Results Are Stored In Text

	if (set > 1) { set = 1; }
	glPushMatrix();
	//glLoadIdentity();
	glColor4ub(r, g, b, alpha);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, font_tex[1]);			 // Select Our Font Texture
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE); //PROPER

	glTranslated(x, y, 0);								 // Position The Text (0,0 - Bottom Left)
	glScalef(scale, scale, 0);
	glRotatef(angle, 0.0, 0.0, 1.0); //Check for rotation
	glListBase(base - 32 + (128 * set));                       // Choose The Font Set (0 or 1)
	glCallLists(strlen(text), GL_UNSIGNED_BYTE, text); // Write The Text To The Screen
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4ub(255, 255, 255, 255);
	glPopMatrix();
	//glLoadIdentity();
	glDisable(GL_TEXTURE_2D);
}

void glPrint_centered(int y, const char* string, int r, int g, int b, int alpha, float scale, float angle, int set)	// Where The Printing Happens
{
	float total = 0;
	float x = 512; //Center of Screen
	//x=512;

	total = get_string_pitch(string, scale, set);
	//wrlog("total %f");
	total = total / 2;
	//total = 512 - total;
	x -= total;
	// x+=20;
	// x = (int) 512-((float) total / (int) 2);
	 //x+=20;
	glPrint(x, y, r, g, b, alpha, scale, angle, set, string);
}

float get_string_pitch(const char* string, float scale, int set)
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
		total = (total + fwidth[(c - 32)]);
		//total=total+1;//(1 pixel spacing)
	}
	//total+=fwidth[(string[0]-32)]/2;
	return total * scale;
}
/*
int parse_font_vars(char  *filename)
{
FILE *varfile;
int i=0;

if ((varfile = fopen(filename, "r")) == NULL) { allegro_message("Failed to load Font INI File");return -1; } // Failed.

 for(i=0;i<256; i++)
{ fscanf(varfile,"%f,",&fwidth[i]); }

 fclose(varfile);

 return 1;
}

*/