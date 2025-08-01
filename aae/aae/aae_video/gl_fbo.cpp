#include "sys_gl.h"
#include "gl_fbo.h"
#include "sys_log.h"
#include "aae_mame_driver.h"
#include "iniFile.h"
#include <array>
#include <initializer_list>

#pragma warning(disable : 4305 4244)

GLuint fbo1, fbo2, fbo3, fbo4, fbo_raster;
GLuint img1a, img1b, img1c, img2a, img2b, img3a, img3b, img4a, img5a;

float width = 1024.0f, height = 1024.0f;
const float width2 = 512.0f, height2 = 512.0f;
const float width3 = 256.0f, height3 = 256.0f;

int CHECK_FRAMEBUFFER_STATUS()
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

static GLuint create_texture(float w, float h, GLenum format = GL_RGB8, GLenum internal_format = GL_RGBA)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, format, (GLsizei)w, (GLsizei)h, 0, internal_format, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    return tex;
}

static void create_fbo(GLuint& fbo, std::initializer_list<std::pair<GLuint*, std::array<float, 2>>> attachments)
{
    glGenFramebuffersEXT(1, &fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);

    int attachment = 0;
    for (const auto& texInfo : attachments) {
        GLuint* tex = texInfo.first;
        const auto& dim = texInfo.second;
        *tex = create_texture(dim[0], dim[1]);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT + attachment, GL_TEXTURE_2D, *tex, 0);
        attachment++;
    }

    CHECK_FRAMEBUFFER_STATUS();
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}

void fbo_init()
{
    float raster_scale = get_config_float("main", "raster_scale", 3.0f);

    create_fbo(fbo1, {
        { &img1a, { width, height } },
        { &img1b, { width, height } },
        { &img1c, { width, height } }
        });

    create_fbo(fbo2, {
        { &img2a, { width2, height2 } },
        { &img2b, { width2, height2 } }
        });

    create_fbo(fbo3, {
        { &img3a, { width3, height3 } },
        { &img3b, { width3, height3 } }
        });

    create_fbo(fbo4, {
        { &img4a, { width, height } }
        });

    std::array<float, 2> raster_dims = {
        static_cast<float>(Machine->gamedrv->screen_width) * raster_scale,
        static_cast<float>(Machine->gamedrv->screen_height) * raster_scale
    };

    create_fbo(fbo_raster, {
        { &img5a, raster_dims }
        });
}