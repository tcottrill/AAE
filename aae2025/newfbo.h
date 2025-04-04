#pragma once

#include "sys_gl.h"


enum class fboFilter
{
	FB_NEAREST,
	FB_LINEAR,
	FB_MIPMAP
};

class multifbo
{

public:

	GLuint frameBufID = 0;
	multifbo(int width, int height, int num_buffers, int num_samples, fboFilter filter);
	~multifbo();
	
	void Bind(short buff, short num);
	void Check();
	void Use();
	void UnUse();
	void SwitchTo();
	void BindToShader(short buff, short num);
	int checkFboStatus();


private:

	int numSamples = 0;
	int numBuffers = 0;
	int iMinFilter=GL_NEAREST;
	int iMaxFilter=GL_NEAREST;
	GLuint colorTexID0 = 0;
	GLuint colorTexID1 = 0;
	GLuint depthTexID = 0;
	GLuint colorBufID = 0;
	GLuint depthBufID = 0;
	
	GLsizei iWidth = 1024;
	GLsizei iHeight = 768;
};