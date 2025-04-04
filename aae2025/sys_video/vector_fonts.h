#ifndef VECTOR_FONTS_H
#define VECTOR_FONTS_H
/*
class spoint
{
public:
	float x;
	float y;
	spoint(float x, float y) : x(x), y(y) {}
	spoint() : x(0), y(0) { }
};

spoint RotateAroundPoint(float x, float y, float cx, float cy, float cosTheta, float sinTheta);
*/
int get_string_len();
float v_get_string_pitch(const char* string, float scale, int set);
void fprintc(int y, unsigned int color, float scale, const char* string);
void fprint(float x, int y, unsigned int color, float scale, const char* fmt, ...);
void font_remove(void);
void font_init(void);
void fontmode_start();
void fontmode_end();

#endif