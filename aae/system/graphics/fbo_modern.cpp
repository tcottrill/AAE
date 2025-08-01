#include "fbo_modern.h"
#include "sys_log.h"

///////////////////////////////////////////////////////////////////////////////
// Check FBO completeness (core profile)
///////////////////////////////////////////////////////////////////////////////
int multifbo::CheckFBOStatus()
{
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	switch (status) {
	case GL_FRAMEBUFFER_COMPLETE:
		LOG_INFO("Framebuffer complete.");
		break;
	case GL_FRAMEBUFFER_UNDEFINED:
		LOG_INFO("Framebuffer undefined.");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
		LOG_INFO("Framebuffer incomplete attachment.");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
		LOG_INFO("Framebuffer missing attachment.");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
		LOG_INFO("Framebuffer incomplete draw buffer.");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
		LOG_INFO("Framebuffer incomplete read buffer.");
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED:
		LOG_INFO("Framebuffer unsupported.");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
		LOG_INFO("Framebuffer incomplete multisample.");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
		LOG_INFO("Framebuffer incomplete layer targets.");
		break;
	default:
		LOG_INFO("Framebuffer unknown error: 0x%X", status);
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
		iMagFilter = GL_NEAREST;
		break;
	case fboFilter::FB_LINEAR:
		iMinFilter = GL_LINEAR;
		iMagFilter = GL_LINEAR;
		break;
	case fboFilter::FB_MIPMAP:
		iMinFilter = GL_LINEAR_MIPMAP_LINEAR;
		iMagFilter = GL_LINEAR;
		break;
	default:
		iMinFilter = iMagFilter = GL_LINEAR;
		break;
	}

	if (numSamples > 0) {
		GLint maxSamples = 0;
		glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
		if (numSamples > maxSamples) {
			LOG_INFO("Requested MSAA samples %d exceeds max supported %d", numSamples, maxSamples);
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
		CheckFBOStatus();
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	else {
		glGenTextures(1, &colorTexID0);
		glBindTexture(GL_TEXTURE_2D, colorTexID0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iMinFilter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iMagFilter);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, iWidth, iHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

		if (iMinFilter == GL_LINEAR_MIPMAP_LINEAR) {
			glGenerateMipmap(GL_TEXTURE_2D);
		}
		glBindTexture(GL_TEXTURE_2D, 0);

		if (numBuffers > 1) {
			glGenTextures(1, &colorTexID1);
			glBindTexture(GL_TEXTURE_2D, colorTexID1);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iMinFilter);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, iMagFilter);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, iWidth, iHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			if (iMinFilter == GL_LINEAR_MIPMAP_LINEAR) {
				glGenerateMipmap(GL_TEXTURE_2D);
			}
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		glGenTextures(1, &depthTexID);
		glBindTexture(GL_TEXTURE_2D, depthTexID);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, iWidth, iHeight, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, nullptr);
		glBindTexture(GL_TEXTURE_2D, 0);

		glGenFramebuffers(1, &frameBufID);
		glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexID0, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexID, 0);
		CheckFBOStatus();
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

void multifbo::BindTexture(short index, short textureUnit)
{
	if (textureUnit < 0) textureUnit = 0;
	if (textureUnit > 31) textureUnit = 31;

	GLuint texID = 0;
	switch (index) {
	case -1: texID = depthTexID; break;
	case 0: texID = colorTexID0; break;
	case 1: texID = colorTexID1; break;
	default: return;
	}

	glActiveTexture(GL_TEXTURE0 + textureUnit);
	glBindTexture(GL_TEXTURE_2D, texID);
	if (iMinFilter == GL_LINEAR_MIPMAP_LINEAR)
		glGenerateMipmap(GL_TEXTURE_2D);
}

void multifbo::Begin()
{
	glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
	glViewport(0, 0, iWidth, iHeight);
}

void multifbo::End()
{
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

void multifbo::Resize(int newWidth, int newHeight)
{
	if (newWidth == iWidth && newHeight == iHeight)
		return;

	// Save config
	const int oldWidth = iWidth;
	const int oldHeight = iHeight;

	iWidth = newWidth;
	iHeight = newHeight;

	// Clean up current resources
	this->~multifbo(); // Manually call destructor
	new (this) multifbo(newWidth, newHeight, numBuffers, numSamples,
		(iMinFilter == GL_LINEAR_MIPMAP_LINEAR) ? fboFilter::FB_MIPMAP :
		(iMinFilter == GL_LINEAR) ? fboFilter::FB_LINEAR :
		fboFilter::FB_NEAREST);

	LOG_INFO("FBO resized from %dx%d to %dx%d", oldWidth, oldHeight, newWidth, newHeight);
}