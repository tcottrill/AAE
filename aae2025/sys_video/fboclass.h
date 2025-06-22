#pragma once

#include "sys_gl.h"
#include "log.h"
#include <vector>
#include <stdexcept>

class OpenGLFBO
{
public:
    OpenGLFBO(int width, int height, int numAttachments, bool enableMipmaps = false, int msaaSamples = 0)
        : width(width), height(height), numAttachments(numAttachments), enableMipmaps(enableMipmaps), msaaSamples(msaaSamples)
    {
        createFBO();
    }

    ~OpenGLFBO() {
        destroyFBO();
    }

    void bind(GLenum buffer = GL_COLOR_ATTACHMENT0) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glDrawBuffer(buffer);
    }

    void unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

    void blitTo(OpenGLFBO& targetFBO, GLenum bufferMask = GL_COLOR_BUFFER_BIT, GLenum filter = GL_NEAREST) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, targetFBO.fbo);

        for (int i = 0; i < numAttachments; ++i) {
            glReadBuffer(GL_COLOR_ATTACHMENT0 + i);
            glDrawBuffer(GL_COLOR_ATTACHMENT0 + i);
            glBlitFramebuffer(0, 0, width, height, 0, 0, targetFBO.width, targetFBO.height, bufferMask, filter);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void resolveMSAAToTextures() {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        for (int i = 0; i < numAttachments; ++i) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFBO);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, resolveTextures[i], 0);

            glReadBuffer(GL_COLOR_ATTACHMENT0 + i);
            glDrawBuffer(GL_COLOR_ATTACHMENT0 + i);
            glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

            // Explicitly generate mipmaps after resolving MSAA
            if (enableMipmaps) {
                glBindTexture(GL_TEXTURE_2D, resolveTextures[i]);
                glGenerateMipmap(GL_TEXTURE_2D);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void resize(int newWidth, int newHeight) {
        if (newWidth != width || newHeight != height) {
            width = newWidth;
            height = newHeight;
            destroyFBO();
            createFBO();
        }
    }

private:
    int width, height;
    int numAttachments;
    bool enableMipmaps;
    int msaaSamples;
    GLuint fbo;
    GLuint resolveFBO;
    std::vector<GLuint> colorAttachments;
    std::vector<GLuint> resolveTextures;
    std::vector<GLenum> drawBuffers;

    void createFBO() {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        colorAttachments.resize(numAttachments);
        glGenTextures(numAttachments, colorAttachments.data());

        resolveTextures.resize(numAttachments);
        glGenTextures(numAttachments, resolveTextures.data());

        for (int i = 0; i < numAttachments; ++i) {
            if (msaaSamples > 0) {
                glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, colorAttachments[i]);
                glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, msaaSamples, GL_RGBA8, width, height, GL_TRUE);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D_MULTISAMPLE, colorAttachments[i], 0);

                glBindTexture(GL_TEXTURE_2D, resolveTextures[i]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, enableMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                if (enableMipmaps) {
                    glGenerateMipmap(GL_TEXTURE_2D);
                }
            }
            else {
                glBindTexture(GL_TEXTURE_2D, colorAttachments[i]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, enableMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, colorAttachments[i], 0);
                if (enableMipmaps) {
                    glGenerateMipmap(GL_TEXTURE_2D);
                }
            }
        }

        drawBuffers.resize(numAttachments);
        for (int i = 0; i < numAttachments; ++i) {
            drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
        }
        glDrawBuffers(numAttachments, drawBuffers.data());

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("Framebuffer is not complete!");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void destroyFBO() {
        glDeleteTextures(colorAttachments.size(), colorAttachments.data());
        glDeleteTextures(resolveTextures.size(), resolveTextures.data());
        glDeleteFramebuffers(1, &fbo);
    }
};
