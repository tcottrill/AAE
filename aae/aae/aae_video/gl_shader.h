#pragma once

#ifndef GL_SHADER_H
#define GL_SHADER_H

#include "sys_gl.h"
//#include <glm/glm.hpp>

// Shader program handles
extern GLuint fragBlur;
extern GLuint fragMulti;

#ifdef __cplusplus
extern "C" {
#endif

	int init_shader();
	void bind_shader(GLuint program);
	void unbind_shader();
	void delete_shader(GLuint* program);

	void set_uniform1i(GLuint program, const char* name, int value);
	void set_uniform1f(GLuint program, const char* name, float value);
	void set_uniform2f(GLuint program, const char* name, float x, float y);
	void set_uniform3f(GLuint program, const char* name, float x, float y, float z);
	void set_uniform4f(GLuint program, const char* name, float x, float y, float z, float w);
	//void set_uniform_mat4f(GLuint program, const char* name, const glm::mat4* matrix);

#ifdef __cplusplus
}
#endif

#endif

/*
* Usage Notes for me, just until I get the code updated.
* 
bind_shader(fragBlur);
set_uniform1f(fragBlur, "width", (float)textureWidth);
set_uniform1f(fragBlur, "height", (float)textureHeight);
// Draw...
unbind_shader();
*/