#pragma once

#include "framework.h"

// -----------------------------------------------------------------------------
// multifbo
// Modern OpenGL 3.3+ core-compatible FBO wrapper
// Supports single or dual color buffers, optional multisampling, depth texture,
// and mipmap filtering. Intended for FBO rendering and shader binding.
//
// Valid inputs:
//   - width, height: dimensions of the FBO
//   - num_buffers: 1 or 2 color outputs
//   - num_samples: 0 = no MSAA, or a supported sample count
//   - filter: FB_NEAREST, FB_LINEAR, or FB_MIPMAP
//
// Behavior:
//   - When MSAA is used, renderbuffers are allocated instead of textures.
//   - When mipmap filter is used, glGenerateMipmap is called after rendering.
//   - BindTexture() allows shader access to color or depth textures.
// -----------------------------------------------------------------------------

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

	// Bind a texture to the given texture unit (color index 0, 1 or -1 for depth)
	void BindTexture(short index, short textureUnit = 0);

	// Begin rendering to this FBO (sets viewport)
	void Begin();

	// End rendering to FBO (unbinds framebuffer)
	void End();

	// Bind FBO only, without push/pop (for blitting or manual control)
	void BindFBOOnly();

	// Get color texture handle (0 or 1)
	GLuint GetTexture(int index) const;

	// Check FBO status (logs reason if incomplete)
	int CheckFBOStatus();

	// Resize FBO to a new width/height (recreates all attachments)
	void Resize(int newWidth, int newHeight);

private:
	GLuint frameBufID = 0;

	// Color targets (for non-multisample)
	GLuint colorTexID0 = 0;
	GLuint colorTexID1 = 0;

	// Depth texture (non-multisample)
	GLuint depthTexID = 0;

	// Multisample-only renderbuffers
	GLuint colorBufID = 0;
	GLuint depthBufID = 0;

	int numSamples = 0;
	int numBuffers = 0;

	int iMinFilter = GL_NEAREST;
	int iMagFilter = GL_NEAREST;

	GLsizei iWidth = 1024;
	GLsizei iHeight = 768;
};
