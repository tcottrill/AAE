
#include "newfbo.h"
#include "log.h"

#pragma warning( disable : 4996 4244)

///////////////////////////////////////////////////////////////////////////////
// check FBO completeness
///////////////////////////////////////////////////////////////////////////////

int multifbo::checkFboStatus()
{
	GLenum status;
	status = glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
	//LOG_INFO("%x", status);
	switch (status) {
	case GL_FRAMEBUFFER_COMPLETE_EXT:
		LOG_INFO("Framebuffer Complete! A-OK");
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
		LOG_INFO("framebuffer GL_FRAMEBUFFER_UNSUPPORTED_EXT");
		/* you gotta choose different formats */
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
		LOG_INFO("framebuffer INCOMPLETE_ATTACHMENT");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
		LOG_INFO("framebuffer FRAMEBUFFER_MISSING_ATTACHMENT");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
		LOG_INFO("framebuffer FRAMEBUFFER_DIMENSIONS");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
		LOG_INFO("framebuffer INCOMPLETE_FORMATS");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
		LOG_INFO("framebuffer INCOMPLETE_DRAW_BUFFER");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
		LOG_INFO("framebuffer INCOMPLETE_READ_BUFFER");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
		LOG_INFO("framebuffer INCOMPLETE_MULTISAMPLE");
		break;
	case GL_FRAMEBUFFER_BINDING_EXT:
		LOG_INFO("framebuffer BINDING_ Error");
		break;
	}
	return status;
}


multifbo::multifbo(int width, int height, int num_buffers, int num_samples, fboFilter filter)
{
	iWidth = width;
	iHeight = height;
	numSamples = num_samples; //Num samples is the number of MSAA samples requested.
	numBuffers = num_buffers; //Num buffers is number of color buffers requested.
	
	switch (filter)
	{
	case fboFilter::FB_NEAREST: iMinFilter = GL_NEAREST; iMaxFilter = GL_NEAREST; break;
	case fboFilter::FB_LINEAR:  iMinFilter = GL_LINEAR; iMaxFilter = GL_LINEAR; break;
	case fboFilter::FB_MIPMAP:  iMinFilter = GL_LINEAR_MIPMAP_LINEAR; iMaxFilter = GL_LINEAR; break;
	default: GL_LINEAR;
	}

	if (num_samples)
	{
		GLint maxSamples;
		glGetIntegerv(GL_MAX_SAMPLES_EXT, &maxSamples);

		if (num_samples > maxSamples)
		{
			LOG_INFO("Warning! NumSamples passed to FBO exceeds cards ability. %d is Max, you specified %d", maxSamples, num_samples);
		}

		// multi sampled color buffer
		glGenRenderbuffers(1, &colorBufID);
		glBindRenderbuffer(GL_RENDERBUFFER, colorBufID);
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_buffers, GL_RGBA, iWidth, iHeight);

		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		// multi sampled depth buffer
		glGenRenderbuffers(1, &depthBufID);
		glBindRenderbuffer(GL_RENDERBUFFER, depthBufID);
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_buffers, GL_DEPTH_COMPONENT24, iWidth, iHeight);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		checkFboStatus();
		
		// create fbo for multi sampled content and attach buffers
		glGenFramebuffers(1, &frameBufID);
		glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorBufID); // Multisample Color
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_RENDERBUFFER, depthBufID); // Multisample Depth
		glBindFramebuffer(GL_FRAMEBUFFER, 0); //Unbind
	}

	else
	{
		// create color texture
		glGenTextures(1, &colorTexID0);
		glBindTexture(GL_TEXTURE_2D, colorTexID0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iMinFilter);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iMaxFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, iWidth, iHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		// unbind
		glBindTexture(GL_TEXTURE_2D, 0);

		if (num_buffers > 1)

		// create color texture
		glGenTextures(1, &colorTexID1);
		glBindTexture(GL_TEXTURE_2D, colorTexID0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iMinFilter);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iMaxFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, iWidth, iHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		// unbind
		glBindTexture(GL_TEXTURE_2D, 0);
		/*
		// create depth texture
		glGenTextures(1, &depthTexID);
		glBindTexture(GL_TEXTURE_2D, depthTexID);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_INTENSITY);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, iWidth, iHeight, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, NULL);

		// unbind
		glBindTexture(GL_TEXTURE_2D, 0);
		*/
		// create final fbo and attach textures
		glGenFramebuffers(1, &frameBufID);
		glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexID0, 0);  //ColorTex
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexID, 0);  //Depth

		checkFboStatus();

		// unbind
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
};


void multifbo::Bind(short buff, short num)
{
	switch (buff)
	{
	case -1:
		glBindTexture(GL_TEXTURE_2D, depthBufID);
		break;
	case 0:
		glBindTexture(GL_TEXTURE_2D, colorTexID0);
		break;
	case 1:
		glBindTexture(GL_TEXTURE_2D, colorTexID1);
		break;

		if (iMinFilter == GL_LINEAR_MIPMAP_LINEAR)
		{
			glGenerateMipmap(GL_TEXTURE_2D);
		}
	}

};

void multifbo::Check() {};

multifbo::~multifbo()
{
	if (numSamples)
	{
		glDeleteFramebuffers(1, &frameBufID);
		glDeleteRenderbuffers(1, &colorBufID);
		glDeleteRenderbuffers(1, &depthBufID);
		colorBufID = depthBufID = frameBufID = 0;
	}
	else
	{
		glDeleteTextures(1, &colorTexID0);
		if (numBuffers > 1)
		glDeleteTextures(1, &colorTexID1);
		glDeleteFramebuffers(1, &frameBufID);
		colorTexID0 = colorTexID0 = 0;
	}
};


void multifbo::Use() {
	glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
	glPushAttrib(GL_VIEWPORT_BIT);
	glViewport(0, 0, iWidth, iHeight);
}


void multifbo::UnUse() {
	glPopAttrib();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void multifbo::SwitchTo() {
	glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
	glViewport(0, 0, iWidth, iHeight);
}


void multifbo::BindToShader(short buff, short num)
{
	if (num < 0)
		num = 0;
	if (num > 16)
		num = 16;
	glActiveTexture(GL_TEXTURE0 + num);
	switch (buff) {
	case -1:
		glBindTexture(GL_TEXTURE_2D, depthBufID);
		break;
	case 0:
		glBindTexture(GL_TEXTURE_2D, colorTexID0);
		break;
	case 1:
		glBindTexture(GL_TEXTURE_2D, colorTexID1);
		break;

		if (iMinFilter == GL_LINEAR_MIPMAP_LINEAR)
		{
			glGenerateMipmap(GL_TEXTURE_2D);
		}
	}
}