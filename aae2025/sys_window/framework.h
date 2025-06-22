#pragma once

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include "glew.h"
#include "wglew.h"
#include "log.h"

void CaptureMouseToWindow(HWND hwnd);
void ReleaseMouseFromWindow();
extern void scare_mouse();
extern void show_mouse();
void osMessage(const char* caption, const char* fmt, ...);
extern void allegro_message(const char *title, const char *message);
extern HWND win_get_window();
extern int SCREEN_W;
extern int SCREEN_H;

