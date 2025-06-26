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

#include <windows.h>
#include "sys_gl.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "log.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <fstream>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")

HDC hDC = nullptr;
HGLRC hRC = nullptr;

constexpr auto PI = 3.14159265358979323846;

void CheckGLErrorEx(const char* label, const char* file, int line) {
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR) {
		const char* errStr = "UNKNOWN_ERROR";
		switch (err) {
		case GL_INVALID_ENUM:                  errStr = "GL_INVALID_ENUM"; break;
		case GL_INVALID_VALUE:                 errStr = "GL_INVALID_VALUE"; break;
		case GL_INVALID_OPERATION:             errStr = "GL_INVALID_OPERATION"; break;
		case GL_STACK_OVERFLOW:                errStr = "GL_STACK_OVERFLOW"; break;
		case GL_STACK_UNDERFLOW:               errStr = "GL_STACK_UNDERFLOW"; break;
		case GL_OUT_OF_MEMORY:                 errStr = "GL_OUT_OF_MEMORY"; break;
		case GL_INVALID_FRAMEBUFFER_OPERATION: errStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
		}

		if (file && label) {
			LOG_ERROR("OpenGL Error [%s] (%#x) in '%s' at %s:%d", errStr, err, label, file, line);
		}
		else if (file) {
			LOG_ERROR("OpenGL Error [%s] (%#x) at %s:%d", errStr, err, file, line);
		}
		else if (label) {
			LOG_ERROR("OpenGL Error [%s] (%#x) in '%s'", errStr, err, label);
		}
		else {
			LOG_ERROR("OpenGL Error [%s] (%#x)", errStr, err);
		}
	}
}

void ViewOrtho(int width, int height) {
	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0f, width - 1, height - 1, 0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void OnResize(GLsizei width, GLsizei height) {
	if (height == 0) height = 1;
	SCREEN_W = width;
	SCREEN_H = height;
	ViewOrtho(width, height);
}

bool InitOpenGLContext(bool forceLegacyGL2) {
	hDC = GetDC(win_get_window());

	PIXELFORMATDESCRIPTOR pfd = {};
	pfd.nSize = sizeof(pfd);
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 32;
	pfd.iLayerType = PFD_MAIN_PLANE;

	int pixelFormat = ChoosePixelFormat(hDC, &pfd);
	if (pixelFormat == 0 || !SetPixelFormat(hDC, pixelFormat, &pfd)) {
		LOG_ERROR("Failed to choose/set pixel format");
		return false;
	}

	// Step 1: Temporary OpenGL context
	HGLRC tempContext = wglCreateContext(hDC);
	if (!tempContext || !wglMakeCurrent(hDC, tempContext)) {
		LOG_ERROR("Failed to create/make current temporary OpenGL context");
		return false;
	}

	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK) {
		LOG_ERROR("GLEW init failed");
		return false;
	}

	if (forceLegacyGL2 || !wglewIsSupported("WGL_ARB_create_context")) {
		LOG_INFO("Using legacy OpenGL 2.1 context");
		hRC = tempContext;
	}
	else {
		// Step 2: Create modern context (request highest compatible 4.x fallback to 3.x)
		const int attribs[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
			WGL_CONTEXT_MINOR_VERSION_ARB, 5,
			WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
			0
		};

		hRC = wglCreateContextAttribsARB(hDC, 0, attribs);
		if (!hRC) {
			LOG_ERROR("wglCreateContextAttribsARB failed — falling back to OpenGL 2.1");
			hRC = tempContext;
		}
		else {
			wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(tempContext);
			wglMakeCurrent(hDC, hRC);
		}
	}

	LOG_INFO("OpenGL %s, GLSL %s", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
	ReportOpenGLCapabilities();
	return true;
}

void OpenGLShutDown() {
	wglMakeCurrent(nullptr, nullptr);
	if (hRC) {
		wglDeleteContext(hRC);
		hRC = nullptr;
	}
	if (hDC) {
		ReleaseDC(win_get_window(), hDC);
		hDC = nullptr;
	}
}

void GLSwapBuffers() {
	if (hDC) {
		SwapBuffers(hDC);
	}
	else {
		LOG_ERROR("GLSwapBuffers called with null device context");
	}
}

void osWaitVsync(bool enable) {
	if (wglSwapIntervalEXT) {
		wglSwapIntervalEXT(enable ? 1 : 0);
	}
	else {
		LOG_ERROR("wglSwapIntervalEXT not available");
	}
}

void ReportOpenGLCapabilities() {
	// Basic system info
	LOG_INFO("GL_VENDOR: %s", glGetString(GL_VENDOR));
	LOG_INFO("GL_RENDERER: %s", glGetString(GL_RENDERER));
	LOG_INFO("GL_VERSION: %s", glGetString(GL_VERSION));

	// GLSL version check
	const char* glslVersionStr = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
	if (glslVersionStr) {
		LOG_INFO("GL_SHADING_LANGUAGE_VERSION: %s", glslVersionStr);

		int major = 0, minor = 0;
		if (sscanf(glslVersionStr, "%d.%d", &major, &minor) == 2) {
			if (major >= 4) {
				LOG_INFO("GLSL 4.x or higher is supported");
			}
			else {
				LOG_ERROR("GLSL version is below 4.x — some shaders may be incompatible");
			}
		}
		else {
			LOG_ERROR("Unable to parse GLSL version string");
		}
	}
	else {
		LOG_ERROR("glGetString(GL_SHADING_LANGUAGE_VERSION) returned null");
	}

	// Feature support
	if (GLEW_EXT_framebuffer_multisample) {
		GLint maxSamples = 0;
		glGetIntegerv(GL_MAX_SAMPLES_EXT, &maxSamples);
		LOG_INFO("GL_EXT_framebuffer_multisample supported: max samples = %d", maxSamples);
	}
	else {
		LOG_ERROR("GL_EXT_framebuffer_multisample NOT supported");
	}

	if (GLEW_ARB_texture_non_power_of_two) {
		LOG_INFO("GL_ARB_texture_non_power_of_two supported: NPOT textures available.");
	}
	else {
		LOG_ERROR("GL_ARB_texture_non_power_of_two NOT supported");
	}

	if (GLEW_EXT_framebuffer_object) {
		LOG_INFO("GL_EXT_framebuffer_object supported: FBO rendering available.");
	}
	else {
		LOG_ERROR("GL_EXT_framebuffer_object NOT supported");
	}

	if (GLEW_ARB_texture_float) {
		LOG_INFO("GL_ARB_texture_float supported: High precision textures available.");
	}
	else {
		LOG_ERROR("GL_ARB_texture_float NOT supported");
	}

	if (GLEW_ARB_shader_objects && GLEW_ARB_shading_language_100) {
		LOG_INFO("GLSL (ARB_shader_objects) supported");
	}
	else {
		LOG_ERROR("GLSL support NOT available");
	}

	// Hardware limits
	GLint maxTexSize = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
	LOG_INFO("GL_MAX_TEXTURE_SIZE = %d", maxTexSize);

	GLint maxTexUnits = 0;
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTexUnits);
	LOG_INFO("GL_MAX_TEXTURE_IMAGE_UNITS = %d", maxTexUnits);

	GLint maxDrawBuffers = 0;
	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
	LOG_INFO("GL_MAX_DRAW_BUFFERS = %d", maxDrawBuffers);

	// VSync extension check
	if (wglewIsSupported("WGL_EXT_swap_control")) {
		LOG_INFO("WGL_EXT_swap_control supported: vsync control available");

		if (wglGetSwapIntervalEXT) {
			int swap = wglGetSwapIntervalEXT();
			LOG_INFO("Current vsync swap interval = %d", swap);
		}
		else {
			LOG_ERROR("wglGetSwapIntervalEXT function pointer not available");
		}
	}
	else {
		LOG_ERROR("WGL_EXT_swap_control NOT supported: vsync control unavailable");
	}
}