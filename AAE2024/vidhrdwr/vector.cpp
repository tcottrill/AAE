#include "vector.h"
#include "../sys_video/glcode.h"
#include "../aae_mame_driver.h"

//This is such horrible code, sorry. It was written long ago when I was just starting.

//Vector Video Variables

colors vec_colors[1024];

extern int gamenum;

int lpntr = 0;
int ppntr = 0;

static int oldppntr = 0;
static int oldlpntr = 0;

int dvglnptr = 0;
int dvgpntptr = 0;
int dvgtexptr = 0;

static int olddvglnptr = 0;
static int olddvgpntptr = 0;
static int olddvgtexptr = 0;

#define MIN_LINES 5
#define MAX_LINES 3000
#define LINE_END -5000

#define CLIP(v) ((v) <= 0 ? 0 : (v) >= 255 ? 255 : (v))

typedef struct
{
	float sx, sy, ex, ey;
	int r, g, b;
} llist;                      //Only one gamelist at a time
llist glines[MAX_LINES + 1];

typedef struct
{
	float sx, sy;
	int r, g, b;
} plist;                      //Only one gamelist at a time
plist gpoints[MAX_LINES + 1];

//VECTOR CACHE
float linecache[150000];
float pointcache[150000];
float texcache[8000];

void add_color_line(float sx, float sy, float ex, float ey, int r, int g, int b)
{
	//	wrlog("Adding color line %f %f %f %f color %d %d %d",sx,sy,ex,ey,r,g,b);
	glines[lpntr].sx = sx;
	glines[lpntr].sy = sy;
	glines[lpntr].ex = ex;
	glines[lpntr].ey = ey;
	glines[lpntr].r = r;
	glines[lpntr].g = g;
	glines[lpntr].b = b;
	if (lpntr < MAX_LINES) { lpntr++; }
}

void add_color_point(float sx, float sy, int r, int g, int b)
{
	gpoints[ppntr].sx = sx;
	gpoints[ppntr].sy = sy;
	gpoints[ppntr].r = r;
	gpoints[ppntr].g = g;
	gpoints[ppntr].b = b;
	if (ppntr < MAX_LINES) { ppntr++; }
}

void draw_color_vectors()
{
	int x = 0;
	int color = 1300;
	int lastcolor = -5;
	int all = 0;
	int gc = 0;

	gc = config.gain;
	glLineWidth(config.linewidth);
	glPointSize(config.pointsize);

	glBegin(GL_LINES);
	// wrlog("Drawing lines this frame %d Line Num %d max line",glines[x].sx,MAX_LINES);
	while (glines[x].sx != LINE_END && x < MAX_LINES)
	{
		color = (glines[x].r * 15) + (glines[x].g * 2) + (glines[x].b * 18); //Create a checksum
		//wrlog("Color determined to be %d",color);
		if (color == 0 && config.drawzero == 0) { color = -1; lastcolor = 0; }
		if (color != -1) {
			if (color != lastcolor) {
				glines[x].r += gc; glines[x].g += gc; glines[x].b += gc;
				//Clip color values
				glines[x].r = CLIP(glines[x].r);
				glines[x].g = CLIP(glines[x].g);
				glines[x].b = CLIP(glines[x].b);

				glColor3ub(glines[x].r, glines[x].g, glines[x].b);
				lastcolor = color; all++;//wrlog("color set %d %d %d",glines[x].r,glines[x].g,glines[x].b);
			}
			//wrlog("Line: %f %f %f %f color %d %d %d",glines[x].sx,glines[x].sy,glines[x].ex,glines[x].ey,glines[x].r,glines[x].g,glines[x].b);
			glVertex3f(glines[x].sx, glines[x].sy, 0);
			glVertex3f(glines[x].ex, glines[x].ey, 0);
		}
		x++;
	}
	glEnd();

	x = 0;
	lastcolor = -15;

	glBegin(GL_POINTS);
	while (gpoints[x].sx != LINE_END && x < MAX_LINES)
	{
		color = (gpoints[x].r * 27) + (gpoints[x].g * 3) + (gpoints[x].b * 5); //Create a checksum

		if (color == 0 && config.drawzero == 0) { color = -1; lastcolor = 0; }
		if (color != -1) {
			if (color != lastcolor) {
				gpoints[x].r += (gc / 2); gpoints[x].g += (gc / 2); gpoints[x].b += (gc / 2);//Add Gain
				//Clip color values

				gpoints[x].r = CLIP(gpoints[x].r);
				gpoints[x].g = CLIP(gpoints[x].g);
				gpoints[x].b = CLIP(gpoints[x].b);
				glColor3ub(gpoints[x].r, gpoints[x].g, gpoints[x].b);
				lastcolor = color; all++;
			}
			glVertex3f(gpoints[x].sx, gpoints[x].sy, 0);
		}
		x++;
	}
	glEnd();

	//wrlog("total color changes this frame %d",all);
}

void draw_lines(void)
{
	int loop = 0;
	int gc;
	int mod = 0;
	int i = 0;
	int z = 0;
	float adj;
	int lpz = -1;
	int llz = -1;
	int r = 0;
	int g = 0;
	int b = 0;

	gc = config.gain;
	glLineWidth(config.linewidth);
	glPointSize(config.pointsize);
	for (i = 0; i < 16; i++) //16
	{
		glBegin(GL_LINES);
		while (linecache[loop] != -5000)
		{
			z = linecache[loop + 4];
			if (z == 0 && config.drawzero == 0) { z = -1; }
			if (z == i) {
				//Add gain to values
				r = vec_colors[z].r + gc; g = vec_colors[z].g + gc; b = vec_colors[z].b + gc;
				//Clip color values
				if (r > 255) { r = 255; }if (g > 255) { g = 255; }if (b > 255) { b = 255; }
				if (r < 0) { r = 0; }if (g < 0) { g = 0; }if (b < 0) { b = 0; }
				//Only set if changed, slight speed increase
				if (llz != i) { glColor3ub(r, g, b); llz = i; }
				//actual drawing
				glVertex3f(linecache[loop], linecache[loop + 1], 0);// Top Left
				glVertex3f(linecache[(loop + 2)], linecache[(loop + 3)], 0);
			}
			loop += 5;
		}
		glEnd(); loop = 0;

		glBegin(GL_POINTS);
		while (pointcache[loop] != -5000)
		{
			z = pointcache[loop + 2];
			if (z == 0 && config.drawzero == 0) { z = -1; }
			if (z == i) {
				adj = pointcache[loop + 5];
				r = vec_colors[z].r + gc; g = vec_colors[z].g + gc; b = vec_colors[z].b + gc;
				if (r > 255) { r = 255; }if (g > 255) { g = 255; }if (b > 255) { b = 255; }
				if (r < 0) { r = 0; }if (g < 0) { g = 0; }if (b < 0) { b = 0; }
				if (adj) { glPointSize(config.pointsize + adj); }
				if (lpz != i) { glColor3ub(r, g, b); lpz = i; }

				glVertex3f(pointcache[loop], pointcache[loop + 1], 0);// Top Left
			}
			loop += 6;
		}
		glEnd(); loop = 0;
	}
}
void draw_texs(void)
{
	int	loop = 0;
	int i = 255;

	glEnable(GL_TEXTURE_2D);
	glDisable(GL_DITHER);
	glBindTexture(GL_TEXTURE_2D, game_tex[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	//glBlendFunc(GL_ONE, GL_ONE);
   // glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);

	while (texcache[loop] != -5000)
	{
		i = texcache[loop + 3];
		glColor4ub(i, i, i, 255);
		glBegin(GL_QUADS);
		glTexCoord2f(0, 1); glVertex2f(texcache[loop] + texcache[loop + 2], texcache[loop + 1] + texcache[loop + 2]);
		glTexCoord2f(0, 0); glVertex2f(texcache[loop] - texcache[loop + 2], texcache[loop + 1] + texcache[loop + 2]);
		glTexCoord2f(1, 0); glVertex2f(texcache[loop] - texcache[loop + 2], texcache[loop + 1] - texcache[loop + 2]);
		glTexCoord2f(1, 1); glVertex2f(texcache[loop] + texcache[loop + 2], texcache[loop + 1] - texcache[loop + 2]);
		glEnd();

		loop += 4;
	}
	glDisable(GL_TEXTURE_2D);
}

void cache_line(float startx, float starty, float endx, float endy, int zvalue, float gc, float mod)
{
	if (dvglnptr > 144900) { wrlog("possible Line Cache overflow!"); cache_clear(); }

	linecache[dvglnptr] = startx; dvglnptr++;
	linecache[dvglnptr] = starty; dvglnptr++;
	linecache[dvglnptr] = endx; dvglnptr++;
	linecache[dvglnptr] = endy; dvglnptr++;
	linecache[dvglnptr] = zvalue; dvglnptr++;
}

void cache_point(float pointx, float pointy, int zvalue, float gc, float mod, float adj)
{
	if (dvgpntptr > 149900) { wrlog("possible POINT CACHE overflow!"); cache_clear(); }

	pointcache[dvgpntptr] = pointx; dvgpntptr++;
	pointcache[dvgpntptr] = pointy; dvgpntptr++;
	pointcache[dvgpntptr] = zvalue; dvgpntptr++;
	pointcache[dvgpntptr] = gc; dvgpntptr++;
	pointcache[dvgpntptr] = mod; dvgpntptr++;
	pointcache[dvgpntptr] = adj; dvgpntptr++;
}

void cache_txt(float pointx, float pointy, int size, int color)
{
	texcache[dvgtexptr] = pointx;
	dvgtexptr++;
	texcache[dvgtexptr] = pointy;
	dvgtexptr++;
	texcache[dvgtexptr] = size;
	dvgtexptr++;
	texcache[dvgtexptr] = color;
	dvgtexptr++;
}

void cache_end(void)
{
	//Color Line Empty frame catch
	if (ppntr > MAX_LINES)ppntr = MAX_LINES;
	if (lpntr > MAX_LINES)lpntr = MAX_LINES;

	if (ppntr > MIN_LINES) oldppntr = ppntr;
	if (lpntr > MIN_LINES) oldlpntr = lpntr;

	if (ppntr < MIN_LINES) { ppntr = oldppntr; gpoints[ppntr].sx = LINE_END; }
	else { gpoints[ppntr].sx = LINE_END; ppntr = 0; }
	if (lpntr < MIN_LINES) { lpntr = oldlpntr; glines[lpntr].sx = LINE_END; }
	else { glines[lpntr].sx = LINE_END; lpntr = 0; }

	if (dvglnptr < MIN_LINES && olddvglnptr > MIN_LINES) { dvglnptr = olddvglnptr; dvgpntptr = olddvgpntptr; dvgtexptr = olddvgtexptr; olddvglnptr = 0; olddvgpntptr = 0; olddvgtexptr = 0; wrlog("DRAW FROM CACHE!!!!!!!!!!!!!!!!!!!!!"); } //Is there no lines this frame?
	else { olddvglnptr = dvglnptr; olddvgtexptr = dvgtexptr; olddvgpntptr = dvgpntptr; }

	linecache[dvglnptr] = LINE_END;
	pointcache[dvgpntptr] = LINE_END;
	texcache[dvgtexptr] = LINE_END;

	dvglnptr = 0;
	dvgpntptr = 0;
	dvgtexptr = 0;
}

void cache_clear(void)
{
	int i = 0;
	for (i = 0; i < 10; i++)
	{
		linecache[i] = LINE_END;
		pointcache[i] = LINE_END;
		texcache[i] = LINE_END;
	}
	//AVG Pointers
	gpoints[0].sx = LINE_END;
	gpoints[1].sx = LINE_END;
	glines[0].sx = LINE_END;
	glines[1].sx = LINE_END;
	ppntr = 0;
	lpntr = 0;
	oldppntr = 0;
	oldlpntr = 0;
	//DVG Pointers
	dvglnptr = 0;
	dvgpntptr = 0;
	dvgtexptr = 0;
	olddvglnptr = 0;
	olddvgpntptr = 0;
	olddvgtexptr = 0;
}