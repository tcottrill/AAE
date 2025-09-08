#pragma once
#pragma once

// -----------------------------------------------------------------------------
// TiledEffect - Fast tiling of a small texture over a larger surface (GL 2.1).
// Uses your shader_util.h and sys_gl for GLEW + error logging.
// Draws a single quad; scales UVs so the texture repeats in hardware.
//
// Usage:
//   TiledEffect_Init();
//   // bind your target FBO (or default FB) sized to surfaceW x surfaceH
//   TiledEffect_Draw(scanrezTex /*GLuint*/, surfaceW, surfaceH, 1.0f);
//   ...
//   TiledEffect_Shutdown();
// -----------------------------------------------------------------------------

#include "sys_gl.h"       // your GLEW setup, CheckGLErrorEx helpers
#include "shader_util.h"  // CompileShader(), logging

#ifdef __cplusplus
extern "C" {
#endif

	bool TiledEffect_Init(void);
	void TiledEffect_Shutdown(void);

	// Draws over [0..surfaceW, 0..surfaceH] in the *current* framebuffer.
	// 'tex' must be a valid GL_TEXTURE_2D handle (e.g., your 12x4 PNG).
	// 'opacity' is clamped to [0..1].
	void TiledEffect_Draw(GLuint tex, int surfaceW, int surfaceH, float opacity);

#ifdef __cplusplus
}
#endif
