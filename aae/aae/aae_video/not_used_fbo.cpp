#include "FBO.h"
#include "sys_log.h"

FBO::FBO(float width, float height, int numAttachments)
    : fbo(0), w(width), h(height)
{
    if (numAttachments < 1 || numAttachments > 4) {
        LOG_ERROR("Invalid attachment count: %d (must be 1 to 4)", numAttachments);
        numAttachments = 1;
    }

    glGenFramebuffersEXT(1, &fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);

    textures.resize(numAttachments);

    for (int i = 0; i < numAttachments; ++i) {
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, (GLsizei)width, (GLsizei)height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT + i, GL_TEXTURE_2D, tex, 0);
        textures[i] = tex;
    }

    CHECK_FRAMEBUFFER_STATUS();
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

FBO::~FBO()
{
    for (GLuint tex : textures) {
        glDeleteTextures(1, &tex);
    }
    if (fbo != 0) {
        glDeleteFramebuffersEXT(1, &fbo);
    }
}

void FBO::bind()
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
}

void FBO::unbind()
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

GLuint FBO::getTexture(int index) const
{
    if (index < 0 || index >= (int)textures.size()) {
        LOG_ERROR("Invalid texture index %d", index);
        return 0;
    }
    return textures[index];
}

int FBO::CHECK_FRAMEBUFFER_STATUS()
{
    GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    switch (status) {
    case GL_FRAMEBUFFER_COMPLETE_EXT:
        LOG_INFO("Framebuffer Complete! A-OK");
        break;
    case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
        LOG_ERROR("GL_FRAMEBUFFER_UNSUPPORTED_EXT");
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
        LOG_ERROR("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT");
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
        LOG_ERROR("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT");
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
        LOG_ERROR("GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT");
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
        LOG_ERROR("GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT");
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
        LOG_ERROR("GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT");
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
        LOG_ERROR("GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT");
        break;
    default:
        LOG_ERROR("Unknown Framebuffer Error: 0x%x", status);
        break;
    }
    return status;
}
