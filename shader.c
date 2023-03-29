
#include "shader.h"

extern  vertFinalShader;
extern  fragFinalShader;
extern  vertShader;
extern  fragShader;

extern  varFinalNoise;
extern  varFinalTime;
extern  varFinalImage;
extern  varSky;
extern  progWave;
extern  progFinal;
extern  varTime;
extern  varNoise;
extern  varViewPos;
extern  varSunPos;
/*




extern  varSky;



*/

// Compile GLSL shaders and link them into programs
int CompileShaders(void)
{
	int param;

	/* Read the text files */
	const char *vertText = ReadTextFile("media/wave.vp");
	const char *fragText = ReadTextFile("media/wave.fp");
	const char *vertFinalText = ReadTextFile("media/final.vp");
	const char *fragFinalText = ReadTextFile("media/final.fp");
		
	/* If we failed to read any of the files, return error */
	if(vertText == 0 || fragText == 0 || vertFinalText == 0 || fragFinalText == 0)
	{
		return FALSE;
	}

	
	/* Create OpenGL shader objects for all shaders */
	vertShader = glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
	fragShader = glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
	vertFinalShader = glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
	fragFinalShader = glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
		
	/* Assign source to each shader */
	glShaderSourceARB(vertShader, 1, &vertText, 0);
	glShaderSourceARB(fragShader, 1, &fragText, 0);
	glShaderSourceARB(vertFinalShader, 1, &vertFinalText, 0);
	glShaderSourceARB(fragFinalShader, 1, &fragFinalText, 0);
	
	/* Compile each shader */
	glCompileShaderARB(vertShader);

	glGetObjectParameterivARB(vertShader, GL_COMPILE_STATUS, &param);
	if(param != 1)
	{
		WriteShaderError(vertShader, "media/wave.vp");	
		return FALSE;
	}

	glCompileShaderARB(fragShader);

	glGetObjectParameterivARB(fragShader, GL_COMPILE_STATUS, &param);
	if(param != 1)
	{
		WriteShaderError(fragShader, "media/wave.fp");	
		return FALSE;
	}

	glCompileShaderARB(vertFinalShader);

	glGetObjectParameterivARB(vertFinalShader, GL_COMPILE_STATUS, &param);
	if(param != 1)
	{
		WriteShaderError(vertFinalShader, "media/final.vp");	
		return FALSE;
	}

	glCompileShaderARB(fragFinalShader);

	glGetObjectParameterivARB(fragFinalShader, GL_COMPILE_STATUS, &param);
	if(param != 1)
	{
		WriteShaderError(fragFinalShader, "media/final.fp");	
		return FALSE;
	}


	/* Create the GLSL programs */
	progWave = glCreateProgramObjectARB();
	progFinal = glCreateProgramObjectARB();
		
	/* Bind the shaders to the proper program */
	glAttachObjectARB(progWave, vertShader);
	glAttachObjectARB(progWave, fragShader);
	glAttachObjectARB(progFinal, vertFinalShader);
	glAttachObjectARB(progFinal, fragFinalShader);
	
	/* Link the programs into bindable shaders */
	glLinkProgramARB(progWave);
	glLinkProgramARB(progFinal);
	
	glGetObjectParameterivARB(progWave, GL_LINK_STATUS, &param);
	if(param != 1)
	{
		WriteShaderError(progWave, "media/wave.vp/fp");	
		return FALSE;
	}
	glGetObjectParameterivARB(progFinal, GL_LINK_STATUS, &param);
	if(param != 1)
	{
		WriteShaderError(progFinal, "media/final.vp/fp");	
		return FALSE;
	}



	varTime = glGetUniformLocationARB(progWave, "time");

	if(varTime == -1)
	{
		fprintf(stderr, "Error: Shader does not contain 'time' uniform.\n");
		return FALSE;
	}

/*	varImage = glGetUniformLocationARB(progWave, "image");

	if(varImage == -1)
	{
		fprintf(stderr, "Error: Shader does not contain 'image' uniform.\n");
		return FALSE;
	}*/

	varNoise = glGetUniformLocationARB(progWave, "noiseTex");

	/*if(varNoise == -1)
	{
		fprintf(stderr, "Error: Shader does not contain 'noiseTex' uniform.\n");
		return FALSE;
	}*/

	varSky = glGetUniformLocationARB(progWave, "sky");

	if(varSky == -1)
	{
		fprintf(stderr, "Error: Shader does not contain 'sky' uniform.\n");
		return FALSE;
	}

	varViewPos = glGetUniformLocationARB(progWave, "viewPos");

	if(varViewPos == -1)
	{
		fprintf(stderr, "Error: Shader does not contain 'viewPos' uniform.\n");
		return FALSE;
	}

	varSunPos = glGetUniformLocationARB(progWave, "sunPos");

	if(varSunPos == -1)
	{
		fprintf(stderr, "Error: Shader does not contain 'sunPos' uniform.\n");
		return FALSE;
	}



	varFinalTime = glGetUniformLocationARB(progFinal, "time");

	if(varFinalTime == -1)
	{
		fprintf(stderr, "Error: Shader does not contain 'time' uniform.\n");
		return FALSE;
	}

	varFinalNoise = glGetUniformLocationARB(progFinal, "noiseTex");

	if(varFinalNoise == -1)
	{
		fprintf(stderr, "Error: Shader does not contain 'noiseTex' uniform.\n");
		return FALSE;
	}

	varFinalImage = glGetUniformLocationARB(progFinal, "image");

	if(varFinalImage == -1)
	{
		fprintf(stderr, "Error: Shader does not contain 'image' uniform.\n");
		return FALSE;
	}
	
	/* Return success */
	return TRUE;
}


void WriteShaderError(GLhandleARB obj, const char *shaderName)
{
	int errorLength = 0;
	int written = 0;
	char *message;

	glGetObjectParameterivARB(obj, GL_OBJECT_INFO_LOG_LENGTH_ARB, &errorLength);
	if(errorLength > 0)
	{
		message = (char *)malloc(errorLength);
		glGetInfoLogARB(obj, errorLength, &written, message);
		fprintf(stderr, "Shader compile error in %s: %s\n", shaderName, message);
		free(message);
	}
}

/*
	ReadTextFile

	Read a text file into a buffer.
*/
char *ReadTextFile(const char *fileName)
{
	FILE *file = 0;
	char *ret = 0;
	int length = 0;

	/* Open file */
	file = fopen(fileName, "rb");

	/* If we failed to open the file, print an error and return a null pointer */
	if(!file)
	{
		fprintf(stderr, "Error: Failed to open file %s\n", fileName);
		return ret;
	}

	/* Determine length of file */
	fseek(file, 0, SEEK_END);
	length = ftell(file);

	/* Create a buffer for the text and read the data */
	fseek(file, 0, SEEK_SET);

	ret = (char *)malloc(length + 1);

	fread(ret, 1, length, file);

	/* Null-terminate the string */
	ret[length] = 0;

	/* Return pointer to data */
	return ret;
}
