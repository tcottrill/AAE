
#include "gl_fbo.h"
#include "log.h"

// FBO Handles
GLuint fbo1;            
GLuint fbo2;
GLuint fbo3;
GLuint fbo4;

// Texture Handles
GLuint img1a;
GLuint img1b;
GLuint img1c;
GLuint img2a;
GLuint img2b;
GLuint img3a;
GLuint img3b;

GLuint img4a;
//Unused Depth Buffer Handles
//GLuint dep1;             // Our handle to the FBO
//GLuint dep2;
//GLuint dep3;
//GLuint dep4;


float width = 1024.0;		         // The height of the texture we'll be rendering to for FBO1
float height = 1024.0;		         // The width of the texture we'll be rendering to for FBO1

const float width2 = 512.0;		// The height of the texture we'll be rendering to for FBO2
const float height2 = 512.0;		// The width of the texture we'll be rendering to for FBO2

const float width3 = 256.0;		// The height of the texture we'll be rendering to for FBO3
const float height3 = 256.0;		// The width of the texture we'll be rendering to for FBO3


int CHECK_FRAMEBUFFER_STATUS()
{
	GLenum status;
	status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
	// wrlog("%x\n", status);
	switch (status) {
	case GL_FRAMEBUFFER_COMPLETE_EXT:
		wrlog("Framebuffer Complete! A-OK");   break;
	case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
		wrlog("framebuffer GL_FRAMEBUFFER_UNSUPPORTED_EXT\n");
		/* you gotta choose different formats */
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
		wrlog("framebuffer INCOMPLETE_ATTACHMENT\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
		wrlog("framebuffer FRAMEBUFFER_MISSING_ATTACHMENT\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
		wrlog("framebuffer FRAMEBUFFER_DIMENSIONS\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DUPLICATE_ATTACHMENT_EXT:
		wrlog("framebuffer INCOMPLETE_DUPLICATE_ATTACHMENT\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
		wrlog("framebuffer INCOMPLETE_FORMATS\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
		wrlog("framebuffer INCOMPLETE_DRAW_BUFFER\n");
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
		wrlog("framebuffer INCOMPLETE_READ_BUFFER\n");
		break;
	case GL_FRAMEBUFFER_BINDING_EXT:
		wrlog("framebuffer BINDING_EXT\n");
		break;

	}
	return status;
}



void fbo_init()
{
	glGenFramebuffersEXT(1, &fbo1);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo1);

	glGenTextures(1, &img1a); glBindTexture(GL_TEXTURE_2D, img1a);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	//glGenerateMipmapEXT(GL_TEXTURE_2D);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, img1a, 0);


	//Gen Texture 2
	glGenTextures(1, &img1b);
	glBindTexture(GL_TEXTURE_2D, img1b);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	// glGenerateMipmapEXT(GL_TEXTURE_2D);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_2D, img1b, 0);

	//Gen Texture 3
	glGenTextures(1, &img1c);
	glBindTexture(GL_TEXTURE_2D, img1c);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT2_EXT, GL_TEXTURE_2D, img1c, 0);

	CHECK_FRAMEBUFFER_STATUS(); //Check Framebuffer Status
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);	// Unbind the FBO for now
	//////////////////////////////////////////////////////////////////////////////////////////////////

	// Setup our FBO 2
	glGenFramebuffersEXT(1, &fbo2);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo2);

	glGenTextures(1, &img2a); glBindTexture(GL_TEXTURE_2D, img2a);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width2, height2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	// glGenerateMipmapEXT(GL_TEXTURE_2D);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);


	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, img2a, 0);

	//Gen Texture 2
	glGenTextures(1, &img2b); glBindTexture(GL_TEXTURE_2D, img2b);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width2, height2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	// glGenerateMipmapEXT(GL_TEXTURE_2D);
	// glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);


	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_2D, img2b, 0);

	CHECK_FRAMEBUFFER_STATUS(); //Check Framebuffer Status
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);	// Unbind the FBO for now

	//////////////////////////////////////////////////////////////////////////////////////////////////////

	// Setup our FBO 3
	glGenFramebuffersEXT(1, &fbo3);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo3);

	glGenTextures(1, &img3a); glBindTexture(GL_TEXTURE_2D, img3a);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width3, height3, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	//glGenerateMipmapEXT(GL_TEXTURE_2D);
	//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, img3a, 0);

	//Gen Texture 2
	glGenTextures(1, &img3b); glBindTexture(GL_TEXTURE_2D, img3b);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width3, height3, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	//glGenerateMipmapEXT(GL_TEXTURE_2D);
	//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	//glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_2D, img3b, 0);
	CHECK_FRAMEBUFFER_STATUS(); //Check Framebuffer Status
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);	// Unbind the FBO for now

	// Setup our FBO 4
	glGenFramebuffersEXT(1, &fbo4);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo4);

	glGenTextures(1, &img4a); glBindTexture(GL_TEXTURE_2D, img4a);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// And attach it to the FBO so we can render to it
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, img4a, 0);

	CHECK_FRAMEBUFFER_STATUS(); //Check Framebuffer Status
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);	// Unbind the FBO for now

}
