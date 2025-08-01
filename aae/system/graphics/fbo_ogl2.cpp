#include "fbo_ogl2.h"
#include "sys_log.h"

#pragma warning(disable : 4996 4244)

///////////////////////////////////////////////////////////////////////////////
// Check FBO completeness
///////////////////////////////////////////////////////////////////////////////
int multifbo::checkFboStatus()
{
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
	switch (status) {
	case GL_FRAMEBUFFER_COMPLETE_EXT:
		LOG_INFO("Framebuffer Complete! A-OK");
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
		LOG_INFO("Framebuffer Unsupported");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
		LOG_INFO("Framebuffer Incomplete Attachment");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
		LOG_INFO("Framebuffer Missing Attachment");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
		LOG_INFO("Framebuffer Incomplete Dimensions");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
		LOG_INFO("Framebuffer Incomplete Formats");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
		LOG_INFO("Framebuffer Incomplete Draw Buffer");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
		LOG_INFO("Framebuffer Incomplete Read Buffer");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
		LOG_INFO("Framebuffer Incomplete Multisample");
		break;
	case GL_FRAMEBUFFER_BINDING_EXT:
		LOG_INFO("Framebuffer Binding Error");
		break;
	default:
		LOG_INFO("Framebuffer Unknown Error: %x", status);
		break;
	}
	return status;
}

multifbo::multifbo(int width, int height, int num_buffers, int num_samples, fboFilter filter)
{
	iWidth = width;
	iHeight = height;
	numSamples = num_samples;
	numBuffers = num_buffers;

	switch (filter) {
	case fboFilter::FB_NEAREST:
		iMinFilter = GL_NEAREST;
		iMaxFilter = GL_NEAREST;
		break;
	case fboFilter::FB_LINEAR:
		iMinFilter = GL_LINEAR;
		iMaxFilter = GL_LINEAR;
		break;
	case fboFilter::FB_MIPMAP:
		iMinFilter = GL_LINEAR_MIPMAP_LINEAR;
		iMaxFilter = GL_LINEAR;
		break;
	default:
		iMinFilter = iMaxFilter = GL_LINEAR;
		break;
	}

	if (numSamples > 0) {
		GLint maxSamples;
		glGetIntegerv(GL_MAX_SAMPLES_EXT, &maxSamples);

		if (numSamples > maxSamples) {
			LOG_INFO("Warning: Requested MSAA %d exceeds max supported %d", numSamples, maxSamples);
		}

		glGenRenderbuffers(1, &colorBufID);
		glBindRenderbuffer(GL_RENDERBUFFER, colorBufID);
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, numSamples, GL_RGBA8, iWidth, iHeight);

		glGenRenderbuffers(1, &depthBufID);
		glBindRenderbuffer(GL_RENDERBUFFER, depthBufID);
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, numSamples, GL_DEPTH_COMPONENT24, iWidth, iHeight);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		glGenFramebuffers(1, &frameBufID);
		glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorBufID);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBufID);
		checkFboStatus();
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	else {
		glGenTextures(1, &colorTexID0);
		glBindTexture(GL_TEXTURE_2D, colorTexID0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iMaxFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iMinFilter);
		if (iMinFilter == GL_LINEAR_MIPMAP_LINEAR)
			glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, iWidth, iHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);

		if (numBuffers > 1) {
			glGenTextures(1, &colorTexID1);
			glBindTexture(GL_TEXTURE_2D, colorTexID1);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iMaxFilter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iMinFilter);
			if (iMinFilter == GL_LINEAR_MIPMAP_LINEAR)
				glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, iWidth, iHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		glGenTextures(1, &depthTexID);
		glBindTexture(GL_TEXTURE_2D, depthTexID);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, iWidth, iHeight, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);

		glGenFramebuffers(1, &frameBufID);
		glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexID0, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexID, 0);
		checkFboStatus();
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
}
multifbo::~multifbo()
{
	if (numSamples > 0) {
		glDeleteFramebuffers(1, &frameBufID);
		glDeleteRenderbuffers(1, &colorBufID);
		glDeleteRenderbuffers(1, &depthBufID);
		colorBufID = depthBufID = frameBufID = 0;
	}
	else {
		glDeleteTextures(1, &colorTexID0);
		if (numBuffers > 1)
			glDeleteTextures(1, &colorTexID1);
		glDeleteTextures(1, &depthTexID);
		glDeleteFramebuffers(1, &frameBufID);
		colorTexID0 = colorTexID1 = depthTexID = frameBufID = 0;
	}
}

void multifbo::Bind(short buff, short unit)
{
	GLuint texID = 0;
	switch (buff) {
	case -1: texID = depthTexID; break;
	case 0:  texID = colorTexID0; break;
	case 1:  texID = colorTexID1; break;
	}
	glBindTexture(GL_TEXTURE_2D, texID);
	if (iMinFilter == GL_LINEAR_MIPMAP_LINEAR)
		glGenerateMipmap(GL_TEXTURE_2D);
}

void multifbo::BindToShader(short buff, short unit)
{
	if (unit < 0) unit = 0;
	if (unit > 16) unit = 16;

	glActiveTexture(GL_TEXTURE0 + unit);

	GLuint texID = 0;
	switch (buff) {
	case -1: texID = depthTexID; break;
	case 0:  texID = colorTexID0; break;
	case 1:  texID = colorTexID1; break;
	}
	glBindTexture(GL_TEXTURE_2D, texID);
	if (iMinFilter == GL_LINEAR_MIPMAP_LINEAR)
		glGenerateMipmap(GL_TEXTURE_2D);
}

void multifbo::Begin()
{
	glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
	glPushAttrib(GL_VIEWPORT_BIT);
	glViewport(0, 0, iWidth, iHeight);
}

void multifbo::End()
{
	glPopAttrib();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void multifbo::BindFBOOnly()
{
	glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
	glViewport(0, 0, iWidth, iHeight);
}

GLuint multifbo::GetTexture(int index) const
{
	switch (index) {
	case 0: return colorTexID0;
	case 1: return colorTexID1;
	default: return 0;
	}
}
