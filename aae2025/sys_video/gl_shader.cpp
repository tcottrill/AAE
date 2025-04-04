#include "aae_mame_driver.h"
#include "config.h"
#include "log.h"
#include "glcode.h"


//Shader Handles
GLuint fragBlur;
GLuint fragMulti;
GLuint MotionBlur;



#define STRINGIFY(A)  #A
#include "../Shaders/Texturing.vs"
#include "../Shaders/MultiTexture.fs"
#include "../Shaders/blur.fs"
#include "../Shaders/vertex.vs"
#include "../Shaders/motion.fs"
#include "../Shaders/motion.vs"



////////////////////////////////////////////////////////////////////////////////
//  BEGIN SHADER CODE  /////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void WriteShaderError(GLhandleARB obj, const char* shaderName)
{
	int errorLength = 0;
	int written = 0;
	char* message;

	glGetObjectParameterivARB(obj, GL_OBJECT_INFO_LOG_LENGTH_ARB, &errorLength);
	if (errorLength > 0)
	{
		message = (char*)malloc(errorLength);
		glGetInfoLogARB(obj, errorLength, &written, message);
		wrlog("Shader compile error in %s: %s\n", shaderName, message);
		free(message);
	}
}

/******************** Shaders Function *******************************/
int setupShaders(const char* vert, const char* frag) {
	GLuint v = 0;
	GLuint f = 0;
	GLuint p = -1;
	int param;
	GLint linked;

	v = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(v, 1, (const GLchar**)&vert, 0);
	glCompileShader(v);
	glGetShaderiv(v, GL_COMPILE_STATUS, &param);
	if (!param)
	{
		WriteShaderError(v, vert);
	}

	f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(f, 1, (const GLchar**)&frag, 0);
	glCompileShader(f);
	if (!param)
	{
		WriteShaderError(f, frag);
	}
	p = glCreateProgram();
	glAttachShader(p, f);
	glAttachShader(p, v);
	glLinkProgram(p);

	glGetProgramiv(p, GL_LINK_STATUS, &linked);
	if (linked)
	{
		wrlog("Shader Program linked ok Vert: %s. Frag: %s", vert, frag);
	}

	return p;
}

int init_shader()
{
	wrlog("Shader Init Start");
	fragBlur = setupShaders(vertText, fragText);
	fragMulti = setupShaders(texvertText, texfragText);
	MotionBlur = setupShaders(motionvs, motionfs);

	return 1;
}
////////////////////////////////////////////////////////////////////////////////
//  END SHADER CODE  /////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

