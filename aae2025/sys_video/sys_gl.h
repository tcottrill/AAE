/*
  sys_gl.h / sys_gl.cpp - OpenGL Context Creation and Utility (Win32 + GLEW)

  This module provides OpenGL context setup and utility functions for Win32
  applications using WGL and GLEW. It supports both legacy OpenGL 2.1 and
  modern OpenGL 3.x/4.x contexts (compatibility profile), and includes
  diagnostic tools and capability checks.

  Features:
	- Single entry point to create an OpenGL context:
		InitOpenGLContext(bool forceLegacyGL2)
		  • forceLegacyGL2 = true: creates OpenGL 2.1 compatibility context
		  • forceLegacyGL2 = false: creates highest supported OpenGL 4.x/3.x context
	- GLEW initialization and extension checks
	- Sets up orthographic 2D projection (ViewOrtho)
	- Handles window resizing (OnResize)
	- Double-buffered rendering support (GLSwapBuffers)
	- VSync enable/disable via WGL extension (osWaitVsync)
	- Full OpenGL error checking with file/line info (CheckGLError)
	- Capability reporting for debugging and compatibility logging (ReportOpenGLCapabilities)

  Dependencies:
	- GLEW and WGLEW
	- Win32 API (HDC, HWND)
	- External: win_get_window() for HWND
	- Logging macros: LOG_INFO, LOG_ERROR

  Usage:
	1. Call InitOpenGLContext(true) for legacy OpenGL 2.1
	   or InitOpenGLContext(false) to use the highest OpenGL 3/4 version.
	2. Use ViewOrtho() for 2D rendering setup.
	3. Use OnResize() to adjust the viewport on window resize.
	4. Call GLSwapBuffers() after rendering.
	5. Optionally call osWaitVsync(true/false) to toggle vsync.
	6. Use check_gl_error() macro to log any OpenGL errors.
	7. Call OpenGLShutDown() to clean up resources on exit.

  License:
	Released into the public domain under The Unlicense.
	See http://unlicense.org/ for more details.
*/

#pragma once

#ifndef GL_BASICS_H
#define GL_BASICS_H

#include <string>
#include "framework.h"
#include "glew.h"
#include "wglew.h"

void ViewOrtho(int width, int height);
bool InitOpenGLContext(bool forceLegacyGL2, bool enableMultisample = false, bool useCoreProfile = false);
void OpenGLShutDown();
void GLSwapBuffers();
void osWaitVsync(bool enable);
void OnResize(GLsizei width, GLsizei height);

void CheckGLErrorEx(const char* label = nullptr, const char* file = nullptr, int line = 0);
void ReportOpenGLCapabilities();

#define check_gl_error()          CheckGLErrorEx(nullptr, __FILE__, __LINE__)
#define check_gl_error_named(x)   CheckGLErrorEx(x, __FILE__, __LINE__)
#define check_gl_error_simple()   CheckGLErrorEx()

#endif
