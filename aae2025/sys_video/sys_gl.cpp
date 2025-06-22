/*
BMFont example implementation with Kerning, for C++ and OpenGL 2.0

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#include <windows.h>
#include "sys_gl.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "log.h"
#include <iostream> // for cerr
//For CheckOpenGLError StringStream
#include <sstream>
//For simple shader code
#include <vector>
#include <string>
#include <fstream>

// Required OpenGL Libraries, placed here to make it easier
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")

//OpenGL Globals for context
HDC hDC;
HGLRC hRC;

#pragma warning (disable : 4996)
constexpr auto  PI = 3.14159265358979323846;

// Code below from https://blog.nobel-joergensen.com/2013/01/29/debugging-opengl-using-glgeterror/

void CheckGLError(const char* file, int line) {
	GLenum err(glGetError());

	while (err != GL_NO_ERROR) {
		std::string error;

		switch (err) {
		case GL_INVALID_OPERATION:      error = "INVALID_OPERATION";      break;
		case GL_INVALID_ENUM:           error = "INVALID_ENUM";           break;
		case GL_INVALID_VALUE:          error = "INVALID_VALUE";          break;
		case GL_OUT_OF_MEMORY:          error = "OUT_OF_MEMORY";          break;
		case GL_INVALID_FRAMEBUFFER_OPERATION:  error = "INVALID_FRAMEBUFFER_OPERATION";  break;
		}

		LOG_INFO("OpenGL Error: %s in %s file at line %d ", error.c_str(), file, line);
		err = glGetError();
	}
}

void CheckGLError2() {
	GLenum err(glGetError());

	while (err != GL_NO_ERROR) {
		std::string error;

		err = glGetError();

		switch (err) {
		case GL_INVALID_OPERATION:      error = "INVALID_OPERATION";      break;
		case GL_INVALID_ENUM:           error = "INVALID_ENUM";           break;
		case GL_INVALID_VALUE:          error = "INVALID_VALUE";          break;
		case GL_OUT_OF_MEMORY:          error = "OUT_OF_MEMORY";          break;
		case GL_INVALID_FRAMEBUFFER_OPERATION:  error = "INVALID_FRAMEBUFFER_OPERATION";  break;
		}

		if (err != GL_NO_ERROR) LOG_INFO("OpenGL Error: %s ", error.c_str());
	}
}

void CheckGLVersionSupport()
{
	const char* version;
	int major, minor;

	version = (char*)glGetString(GL_VERSION);
	sscanf(version, "%d.%d", &major, &minor);
	LOG_INFO("OpenGl Version supported %d.%d", major, minor);

	if (major < 2)
	{
		//MessageBox(NULL, "This program WILL not work, your supported opengl version is less then 2.0", "OpenGL version warning", MB_ICONERROR | MB_OK);
	}
}

//========================================================================
// Setup and OpenGl Ortho 2D Window, Depreciated OGL 1.0-2.0
//========================================================================
void ViewOrtho(int width, int height)
{
	glViewport(0, 0, width, height);             // Set Up An Ortho View
	glMatrixMode(GL_PROJECTION);			  // Select Projection
	glLoadIdentity();						  // Reset The Matrix
	glOrtho(0.0f, width - 1, height - 1, 0.0f, -1.0f, 1.0f);	  // Select Ortho 2D Mode DirectX style(640x480)
	glMatrixMode(GL_MODELVIEW);				  // Select Modelview Matrix
	glLoadIdentity();						  // Reset The Matrix
}

//========================================================================
// Resize OpenGL Window
//========================================================================
void OnResize(GLsizei width, GLsizei height)
{
	if (height == 0)                // Prevent A Divide By Zero By
		height = 1;                    // Making Height Equal One
	SCREEN_W = width;             // Screen Width
	SCREEN_H = height;            // Screen Height
	ViewOrtho(width, height);
}

//========================================================================
// Enable Opengl Code, currently depreciated 2.0 only
//========================================================================
int OpenGL2Enable()
{
	PIXELFORMATDESCRIPTOR pfd;
	int format;

	// get the device context (DC)
	hDC = GetDC(win_get_window());

	// set the pixel format for the DC
	ZeroMemory(&pfd, sizeof(pfd));
	pfd.nSize = sizeof(pfd);
	//pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iLayerType = PFD_MAIN_PLANE;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	//pfd.cDepthBits = 16;
	
	format = ChoosePixelFormat(hDC, &pfd);
	SetPixelFormat(hDC, format, &pfd);

	// create and enable the render context (RC)
	hRC = wglCreateContext(hDC);
	wglMakeCurrent(hDC, hRC);

	//Enable GLEW
	glewExperimental = true;
	GLenum error = glewInit(); // Enable GLEW
	if (error != GLEW_OK) // If GLEW fails
	{
		LOG_INFO("Glew Init Failed");	return false;
	}

	LOG_INFO("OpenGL %s, GLSL %s\n", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
	return true;
}

//========================================================================
// Enable Opengl 3/4 Code.
//========================================================================
int OpenGL3Enable()
{
	hDC = GetDC(win_get_window());

	PIXELFORMATDESCRIPTOR pfd; // Create a new PIXELFORMATDESCRIPTOR (PFD)
	memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR)); // Clear our  PFD
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR); // Set the size of the PFD to the size of the class
	pfd.dwFlags = PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW; // Enable double buffering, opengl support and drawing to a window
	pfd.iPixelType = PFD_TYPE_RGBA; // Set our application to use RGBA pixels
	pfd.cColorBits = 32; // Give us 32 bits of color information (the higher, the more colors)
	pfd.cDepthBits = 32; // Give us 32 bits of depth information (the higher, the more depth levels)
	pfd.iLayerType = PFD_MAIN_PLANE; // Set the layer of the PFD

	int nPixelFormat = ChoosePixelFormat(hDC, &pfd); // Check if our PFD is valid and get a pixel format back
	if (nPixelFormat == 0) // If it fails
		return false;

	int bResult = SetPixelFormat(hDC, nPixelFormat, &pfd); // Try and set the pixel format based on our PFD
	if (!bResult) // If it fails
		return false;

	HGLRC tempOpenGLContext = wglCreateContext(hDC); // Create an OpenGL 2.1 context for our device context
	wglMakeCurrent(hDC, tempOpenGLContext); // Make the OpenGL 2.1 context current and active

	glewExperimental = true;
	GLenum error = glewInit(); // Enable GLEW
	if (error != GLEW_OK) // If GLEW fails
	{
		LOG_INFO("Glew Init Failed");	return false;
	}

	const int attributes[] = {
		WGL_CONTEXT_MAJOR_VERSION_ARB, 3, // Set the MAJOR version of OpenGL to 3
		WGL_CONTEXT_MINOR_VERSION_ARB, 2, // Set the MINOR version of OpenGL to 2
		WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB, // Set our OpenGL context to be forward compatible
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB , //Set the compatibility
		0
	};

	if (wglewIsSupported("WGL_ARB_create_context") == 1) { // If the OpenGL 3.x context creation extension is available
		hRC = wglCreateContextAttribsARB(hDC, NULL, attributes); // Create and OpenGL 3.x context based on the given attributes
		wglMakeCurrent(NULL, NULL); // Remove the temporary context from being active
		wglDeleteContext(tempOpenGLContext); // Delete the temporary OpenGL 2.1 context
		wglMakeCurrent(hDC, hRC); // Make our OpenGL 3.0 context current
	}
	else
	{
		hRC = tempOpenGLContext; // If we didn't have support for OpenGL 3.x and up, use the OpenGL 2.1 context
	}

	int glVersion[2] = { -1, -1 }; // Set some default values for the version
	glGetIntegerv(GL_MAJOR_VERSION, &glVersion[0]); // Get back the OpenGL MAJOR version we are using
	glGetIntegerv(GL_MINOR_VERSION, &glVersion[1]); // Get back the OpenGL MAJOR version we are using

	LOG_INFO("Gl version %x:%x", glVersion[0], glVersion[1]);
	LOG_INFO("OpenGL %s, GLSL %s\n", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));

	if (GLEW_EXT_framebuffer_multisample)
	{
		//Example of checking extensions
	}

	return true; // We have successfully created a context, return true
};

//========================================================================
// Disable and release the OpenGL Context
//========================================================================
void OpenGLShutDown()
{
	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(hRC);
	ReleaseDC(win_get_window(), hDC);
}

//========================================================================
// Swap GL Buffers
//========================================================================
void GLSwapBuffers()
{
	SwapBuffers(hDC);
}

//========================================================================
// Enable / Disable vSync
//========================================================================
void osWaitVsync(bool enable)
{
	if (enable == TRUE)
	{
		wglSwapIntervalEXT(1);
	}
	else
	{
		wglSwapIntervalEXT(0);
	}
}

// Really simple shader loader copied from the OpenGL examples on the web
GLuint LoadShaders(const char* vertex_file, const char* fragment_file, bool file_or_char)
{
	// Create the shaders
	GLuint VertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	GLuint FragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);
	std::string VertexShaderCode;
	std::string FragmentShaderCode;
	// If these are file names, read the data in.
	// Otherwise, use these directly.
	if (file_or_char)
	{
		// Read the Vertex Shader code from the file

		std::ifstream VertexShaderStream(vertex_file, std::ios::in);
		if (VertexShaderStream.is_open()) {
			std::string Line = "";
			while (getline(VertexShaderStream, Line))
				VertexShaderCode += "\n" + Line;
			VertexShaderStream.close();
		}
		else {
			LOG_INFO("Impossible to open %s. Are you in the right directory ? Don't forget to read the FAQ !\n", vertex_file);
			return 0;
		}

		// Read the Fragment Shader code from the file

		std::ifstream FragmentShaderStream(fragment_file, std::ios::in);
		if (FragmentShaderStream.is_open()) {
			std::string Line = "";
			while (getline(FragmentShaderStream, Line))
				FragmentShaderCode += "\n" + Line;
			FragmentShaderStream.close();
		}
		else {
			LOG_INFO("Impossible to open %s. Are you in the right directory ? Don't forget to read the FAQ !\n", fragment_file);
			return 0;
		}
	}
	else
	{
		VertexShaderCode = vertex_file;
		FragmentShaderCode = fragment_file;
	}

	GLint Result = GL_FALSE;
	int InfoLogLength;

	// Compile Vertex Shader
	LOG_INFO("Compiling vertex shader");
	char const* VertexSourcePointer = VertexShaderCode.c_str();
	glShaderSource(VertexShaderID, 1, &VertexSourcePointer, NULL);
	glCompileShader(VertexShaderID);

	// Check Vertex Shader
	glGetShaderiv(VertexShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(VertexShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0) {
		std::vector<char> VertexShaderErrorMessage(InfoLogLength + 1);
		glGetShaderInfoLog(VertexShaderID, InfoLogLength, NULL, &VertexShaderErrorMessage[0]);
		LOG_INFO("%s\n", &VertexShaderErrorMessage[0]);
	}

	// Compile Fragment Shader
	LOG_INFO("Compiling fragment shader");
	char const* FragmentSourcePointer = FragmentShaderCode.c_str();
	glShaderSource(FragmentShaderID, 1, &FragmentSourcePointer, NULL);
	glCompileShader(FragmentShaderID);

	// Check Fragment Shader
	glGetShaderiv(FragmentShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(FragmentShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0) {
		std::vector<char> FragmentShaderErrorMessage(InfoLogLength + 1);
		glGetShaderInfoLog(FragmentShaderID, InfoLogLength, NULL, &FragmentShaderErrorMessage[0]);
		LOG_INFO("%s\n", &FragmentShaderErrorMessage[0]);
	}

	// Link the program
	LOG_INFO("Linking program\n");
	GLuint ProgramID = glCreateProgram();
	glAttachShader(ProgramID, VertexShaderID);
	glAttachShader(ProgramID, FragmentShaderID);
	glLinkProgram(ProgramID);

	// Check the program
	glGetProgramiv(ProgramID, GL_LINK_STATUS, &Result);
	glGetProgramiv(ProgramID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if (InfoLogLength > 0) {
		std::vector<char> ProgramErrorMessage(InfoLogLength + 1);
		glGetProgramInfoLog(ProgramID, InfoLogLength, NULL, &ProgramErrorMessage[0]);
		LOG_INFO("%s\n", &ProgramErrorMessage[0]);
	}

	glDetachShader(ProgramID, VertexShaderID);
	glDetachShader(ProgramID, FragmentShaderID);

	glDeleteShader(VertexShaderID);
	glDeleteShader(FragmentShaderID);

	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR)
	{
		LOG_INFO("GL Error in shader processing  %d", err);
		//Process/log the error.
	}

	return ProgramID;
}

bool ShaderIsValid(GLuint program) {
	glValidateProgram(program);
	int params = -1;
	glGetProgramiv(program, GL_VALIDATE_STATUS, &params);
	LOG_INFO("program %i GL_VALIDATE_STATUS = %i\n", program, params);
	if (GL_TRUE != params) {
		//_print_programme_info_log(programme);
		return false;
	}
	return true;
}