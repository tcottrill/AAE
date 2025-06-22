#ifndef FONTS_H
#define FONTS_H

#ifdef __cplusplus
extern "C" {
#endif

	void BuildFont(void);
	void KillFont(void);
	void glPrint(float x, float y, int r, int g, int b, int alpha, float scale, float angle, int set, const char* fmt, ...);
	void glPrint_centered(float y, const char* string, int r, int g, int b, int alpha, float scale, float angle, int set);
	float get_string_pitch(const char* string, float scale, int set);

#ifdef __cplusplus
}
#endif

#endif // FONTS_H