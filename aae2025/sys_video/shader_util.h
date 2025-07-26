#pragma once
// -----------------------------------------------------------------------------
// shader_util.h
//
// Description:
// General-purpose OpenGL shader utility for compiling and linking GLSL shaders.
// Provides reusable functions for:
// - Compiling individual shaders (vertex, fragment, etc.)
// - Linking shader programs
// - Logging success/failure via LOG_INFO and LOG_ERROR
//
// Usage:
// GLuint vs = CompileShader(GL_VERTEX_SHADER, vs_src, "VS label");
// GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fs_src, "FS label");
// GLuint program = LinkShaderProgram(vs, fs);
//
// On success, shaders are linked and deleted. On failure, logs show error source.
//
// License:
// This is free and unencumbered software released into the public domain
// under the Unlicense. For more information, see <http://unlicense.org/>
// -----------------------------------------------------------------------------

#pragma once
#include "sys_gl.h"
#include "log.h"

// -----------------------------------------------------------------------------
// Compiles a GLSL shader and logs success/failure
// -----------------------------------------------------------------------------
inline GLuint CompileShader(GLenum type, const char* src, const char* label = nullptr)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, nullptr);
	glCompileShader(shader);

	GLint success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

	if (!success) {
		char log[512];
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		LOG_ERROR("CompileShader FAILED (%s):\n%s", label ? label : "unnamed", log);
	}
	else {
		LOG_INFO("CompileShader OK (%s)", label ? label : "unnamed");
	}

	return shader;
}

// -----------------------------------------------------------------------------
// Links a GLSL program from vertex + fragment shaders
// -----------------------------------------------------------------------------
inline GLuint LinkShaderProgram(GLuint vertexShader, GLuint fragmentShader)
{
	GLuint program = glCreateProgram();
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);

	GLint success = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &success);

	if (!success) {
		char log[512];
		glGetProgramInfoLog(program, sizeof(log), nullptr, log);
		LOG_ERROR("LinkShaderProgram FAILED:\n%s", log);
	}
	else {
		LOG_INFO("LinkShaderProgram OK");
	}

	// Cleanup individual shaders after linking
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	return program;
}
