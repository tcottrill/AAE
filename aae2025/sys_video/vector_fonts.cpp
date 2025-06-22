// Simple Vector Font
// 2025 TC

#include "sys_gl.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <cstdarg>
#include <cstring>

#include "aae_mame_driver.h"
#include "vector_fonts.h"
#include "rotation_table.h"
#include "colordefs.h"

#define FONT_SPACING 9.5f
#define EOC 256


static int fstart[257];
static int lastx = 0;
static float lastscale = 1.0f;
static int fangle = 0;
static float xcenter = 0.0f;
static float ycenter = 0.0f;


//EXCLM		33,3.5,2,3.5,6,3.5,0,3.5,1,EOC,
//PRCENT	37,0,0,7,6,1,6,1,5,6,0,6,1,EOC,
//APOST		39,3.5,6,3.5,5,EOC
//APOST		44,3.5,6,3.5,5,EOC
//DASH		45,1,3,6,3,EOC,
//PERIOD	46,3,0,4,0,EOC,
//SLASH		47,0,7,7,0,EOC,
//Zero		48,0,0,7,0,7,0,7,6,7,6,0,6,0,6,0,0,EOC,
//ONE		49,3.5,0,3.5,6,EOC,
//TWO		50,7,0,0,0,0,0,0,3,0,3,7,3,7,3,7,6,7,6,0,6,EOC,
//THREE		51,0,0,7,0,7,0,7,3,7,3,4,3,4,3,7,6,7,6,0,6,EOC,
//four		52,5,0,5,6,5,6,0,3,0,3,7,3,EOC,
//FIVE		53,0,3,0,6,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,3,EOC,
//SIX		54,0,0,0,6,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,3,EOC,
//SEVEN		55,2,0,7,6,7,6,0,6,EOC,
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

float fontdata[] = {
    // Space
    32,EOC,
    // 
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
    60,0,3,7,0,7,0,7,7,7,7,0,3,EOC,
    62,0,0,7,3,7,3,0,7,0,7,0,0,EOC,
    63,0,6,7,6,7,6,7,3,7,3,2,3,2,3,2,2,2,0,2,1,EOC,
    65,0,0,0,3,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,6,EOC,
    66,0,0,0,6,0,6,6,6,6,6,7,5,7,5,7,4,7,4,6,3,6,3,0,3,0,0,6,0,6,0,7,1,7,1,7,2,7,2,6,3,EOC,
    67,0,0,7,0,7,6,0,6,0,6,0,0,EOC,
    68,0,0,0,6,0,6,6,6,6,6,7,5,7,5,7,1,7,1,6,0,6,0,0,0,EOC,
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
    83,0,0,7,0,7,0,7,3,7,3,0,3,0,3,0,6,0,6,7,6,EOC,
    84,3.5,0,3.5,6,0,6,7,6,EOC,
    85,0,0,7,0,7,0,7,6,0,6,0,0,EOC,
    86,0,6,3.5,0,3.5,0,7,6,EOC,
    87,0,6,0,0,0,0,3.5,3,3.5,3,7,0,7,0,7,6,EOC,
    88,0,0,7,6,0,6,7,0,EOC,
    89,0,6,3.5,3,3.5,3,3.5,0,3.5,3,7,6,EOC,
    90,0,6,7,6,7,6,0,0,0,0,7,0,EOC,
    91,2,0,1,0,1,0,0,1,0,1,0,5,0,5,1,6,1,6,2,6,EOC,
    93,5,0,6,0,6,0,7,1,7,1,7,5,7,5,6,6,6,6,5,6,EOC,
    95,1,0,6,0,EOC,
    97,0,0,0,3,0,6,7,6,0,3,7,3,0,0,7,0,7,0,7,6,EOC,
    98,0,0,0,6,0,6,6,6,6,6,7,5,7,5,7,4,7,4,6,3,6,3,0,3,0,0,6,0,6,0,7,1,7,1,7,2,7,2,6,3,EOC,
    99,0,0,7,0,7,6,0,6,0,6,0,0,EOC,
    100,0,0,0,6,0,6,6,6,6,6,7,5,7,5,7,1,7,1,6,0,6,0,0,0,EOC,
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
    115,0,0,7,0,7,0,7,3,7,3,0,3,0,3,0,6,0,6,7,6,EOC,
    116,3.5,0,3.5,6,0,6,7,6,EOC,
    117,0,0,7,0,7,0,7,6,0,6,0,0,EOC,
    118,0,6,3.5,0,3.5,0,7,6,EOC,
    119,0,6,0,0,0,0,3.5,3,3.5,3,7,0,7,0,7,6,EOC,
    120,0,0,7,6,0,6,7,0,EOC,
    121,0,6,3.5,3,3.5,3,3.5,0,3.5,3,7,6,EOC,
    122,0,6,7,6,7,6,0,0,0,0,7,0,EOC,
    123,2,0,1,0,1,0,0,1,0,1,0,5,0,5,1,6,1,6,2,6,EOC,
    125,5,0,6,0,6,0,7,1,7,1,7,5,7,5,6,6,6,6,5,6,EOC,
    128,0,0,3.5,6,3.5,6,7,0,7,0,0,0,EOC,
    129,0,6,3.5,0,3.5,0,7,6,0,6,7,6,EOC,
    130,0,0,0,6,0,6,7,3.5,7,3.5,0,0,EOC,
    131,0,3.5,7,0,7,6,7,0,7,6,0,3.5,EOC,
    132,0,0,7,0,7,0,7,6,7,6,0,6,0,6,0,0,1,1,5,1,5,5,1,5,1,5,1,1,EOC,
};

void font_init(void)
{
    for (int i = 0; i < 257; ++i)
        fstart[i] = 32;

    for (int a = 0; fontdata[a] > -1; ++a) {
        int d = static_cast<int>(fontdata[a]);
        if (d > 31 && d < 255)
            fstart[d] = a;
    }
}

void font_remove(void) {}

void fontmode_start()
{
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    fangle = 0;

    int xmin = Machine->gamedrv->visible_area.min_x;
    int ymin = Machine->gamedrv->visible_area.min_y;
    int xmax = Machine->gamedrv->visible_area.max_x;
    int ymax = Machine->gamedrv->visible_area.max_y;

    xcenter = (xmax + xmin) / 2.0f;
    ycenter = (ymax + ymin) / 2.0f;
}

void fontmode_end()
{
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4ub(255, 255, 255, 255);
}

void fprint(float x, int y, unsigned int color, float scale, const char* fmt, ...)
{
    char text[256] = "";
    va_list ap;
    if (!fmt) return;

    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    int len = strnlen(text, sizeof(text) - 1);
    lastscale = scale;

    std::vector<GLfloat> vertices;
    std::vector<GLubyte> colors;

    float cosTheta = 1.0f, sinTheta = 0.0f;
    Vec2 center = { xcenter, ycenter };

    if (fangle) {
        cosTheta = _cos[fangle];
        sinTheta = _sin[fangle];
    }

    for (int i = 0; i < len; ++i) {
        int a = fstart[static_cast<unsigned char>(text[i])] + 1;
        while (fontdata[a] != EOC) {
            Vec2 p0, p1;
            if (fangle) {
                p0 = RotateAroundPoint(fontdata[a] * scale + x, fontdata[a + 1] * scale + y, center.x, center.y, cosTheta, sinTheta);
                p1 = RotateAroundPoint(fontdata[a + 2] * scale + x, fontdata[a + 3] * scale + y, center.x, center.y, cosTheta, sinTheta);
            }
            else {
                p0 = { fontdata[a] * scale + x, fontdata[a + 1] * scale + y };
                p1 = { fontdata[a + 2] * scale + x, fontdata[a + 3] * scale + y };
            }
            vertices.push_back(p0.x); vertices.push_back(p0.y);
            vertices.push_back(p1.x); vertices.push_back(p1.y);
            for (int j = 0; j < 2; ++j) {
                colors.push_back(RGB_RED(color));
                colors.push_back(RGB_GREEN(color));
                colors.push_back(RGB_BLUE(color));
            }
            a += 4;
        }
        x += (FONT_SPACING * scale);
    }
    lastx = static_cast<int>(x);

    if (!vertices.empty()) {
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, vertices.data());
        glColorPointer(3, GL_UNSIGNED_BYTE, 0, colors.data());
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size() / 2));
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
    }
}

void fprintc(int y, unsigned int color, float scale, const char* string)
{
    if (!string) return;
    int len = static_cast<int>(strnlen(string, 255));
    float total = len * FONT_SPACING * scale;
    float x = (1024.0f - total) / 2.0f;
    fprint(x, y, color, scale, "%s", string);
}

int get_string_len()
{
    return static_cast<int>(lastx + (3.5f * lastscale));
}

float v_get_string_pitch(const char* string, float scale, int set)
{
    if (!string) return 0.0f;
    int len = static_cast<int>(strlen(string));
    return len * FONT_SPACING * scale;
}
