#pragma once

// -----------------------------------------------------------------------------
// File: FBO.h
//
// Description:
//   Encapsulates an OpenGL framebuffer object (FBO) with support for up to
//   four color attachments. Provides a reusable and modern C++ interface for
//   creating, binding, and managing FBOs and their associated 2D textures.
//
//   Supports traditional EXT_framebuffer_object API and is compatible with
//   legacy OpenGL 2.x codebases.
//
// Valid Inputs:
//   - Width and height in pixels
//   - Number of color attachments (1 to 4)
//
// Behavior:
//   - Automatically generates textures and attaches them to the FBO
//   - Configures each texture with GL_RGB8 format and GL_LINEAR filtering
//   - Logs framebuffer completeness status with detailed error messages
//
//   FBO::unbind();     // Unbinds any FBO (returns to default framebuffer)
// 
// Example:
//   FBO myFbo(1024.0f, 1024.0f, 2);
//   myFbo.bind();
//   ... render ...
//   FBO::unbind();
//   GLuint tex0 = myFbo.getTexture(0);
//
// -----------------------------------------------------------------------------



#include "sys_gl.h"
#include <vector>

class FBO {
public:
    FBO(float width, float height, int numAttachments = 1);
    ~FBO();

    void bind();
    static void unbind();         // static preferred for global clarity

    GLuint getFramebuffer() const { return fbo; }
    GLuint getTexture(int index) const;

    int getAttachmentCount() const { return (int)textures.size(); }
    float getWidth() const { return w; }
    float getHeight() const { return h; }

    // Framebuffer completeness check
    static int CHECK_FRAMEBUFFER_STATUS();

private:
    GLuint fbo;
    std::vector<GLuint> textures;
    float w, h;
};
