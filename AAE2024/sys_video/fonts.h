#ifndef FONTS_H
#define FONTS_H

float get_string_pitch(const char* string, float scale, int set);
void glPrint_centered(int y, const char* string, int r, int g, int b, int alpha, float scale, float angle, int set);
void glPrint(float x, int y, int r, int g, int b, int alpha, float scale, float angle, int set, const char* fmt, ...);
void KillFont(void);
void BuildFont(void);
//int parse_font_vars(char *filename);

#endif