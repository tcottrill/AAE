#pragma once

#include "framework.h"

enum class fboFilter
{
	FB_NEAREST,
	FB_LINEAR,
	FB_MIPMAP
};

class multifbo
{
public:
	multifbo(int width, int height, int num_buffers, int num_samples, fboFilter filter);
	~multifbo();

	void Bind(short buff, short unit);
	void BindToShader(short buff, short unit);
	void Begin();
	void End();
	void BindFBOOnly();

	int checkFboStatus();

	GLuint GetTexture(int index) const;

private:
	GLuint frameBufID = 0;
	GLuint colorTexID0 = 0;
	GLuint colorTexID1 = 0;
	GLuint depthTexID = 0;

	GLuint colorBufID = 0; // Multisample renderbuffer
	GLuint depthBufID = 0;

	int numSamples = 0;
	int numBuffers = 0;
	int iMinFilter = GL_NEAREST;
	int iMaxFilter = GL_NEAREST;

	GLsizei iWidth = 1024;
	GLsizei iHeight = 768;
};
