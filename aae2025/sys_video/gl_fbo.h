#ifndef GL_FBO_H
#define GL_FBO_H

#include "sys_gl.h"

// FBO Handle
extern GLuint fbo1;
extern GLuint fbo2;
extern GLuint fbo3;
extern GLuint fbo4;

extern GLuint img1a;
extern GLuint img1b;
extern GLuint img1c;
// Our handle to a texture
extern GLuint img2a;
extern GLuint img2b;
extern GLuint img3a;
extern GLuint img3b;
extern GLuint img4a;

extern GLuint fbo_raster;
extern GLuint img5a;

extern float width;		// The hight of the texture we'll be rendering to
extern float height;		// The width of the texture we'll be rendering to

extern const float width2;		// The hight of the texture we'll be rendering to
extern const float height2;		// The width of the texture we'll be rendering to

extern const float width3;		// The hight of the texture we'll be rendering to
extern const float height3;		// The width of the texture we'll be rendering to


extern void fbo_init();
extern int CHECK_FRAMEBUFFER_STATUS();

#endif
