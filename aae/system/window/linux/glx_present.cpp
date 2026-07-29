//==============================================================================
// glx_present.cpp -- see glx_present.h.
//==============================================================================
#include "linux/glx_present.h"

#include "sys_gl.h"
#include "sys_log.h"

#include <X11/Xlib.h>
#include <GL/glx.h>

void GlxPresentSurface::Attach(void* display, unsigned long window)
{
	m_display = display;
	m_window  = window;
}

void GlxPresentSurface::SwapBuffers()
{
	// Routed through sys_gl.cpp rather than calling glXSwapBuffers directly:
	// that file owns the GLX context and the display/window it was made
	// current on, and having two places that think they own the swap is how
	// you end up swapping a drawable the context is not bound to.
	GLSwapBuffers();
}

void GlxPresentSurface::GetDrawableSize(int* w, int* h) const
{
	if (w) *w = 0;
	if (h) *h = 0;
	if (!m_display || !m_window) return;

	Display* dpy = static_cast<Display*>(m_display);
	Window root;
	int x = 0, y = 0;
	unsigned int width = 0, height = 0, border = 0, depth = 0;

	if (XGetGeometry(dpy, static_cast<Drawable>(m_window), &root,
	                 &x, &y, &width, &height, &border, &depth)) {
		if (w) *w = static_cast<int>(width);
		if (h) *h = static_cast<int>(height);
	}
}

//------------------------------------------------------------------------------
// Vulkan: Phase 4.
//
// The Raspberry Pi needs it - Mesa v3d tops out near GL/GLES 3.1, below the
// renderer's "#version 330 core" shaders - but the Steam Machine's radeonsi
// does GL 4.6, so nothing in Phase 3c requires a Vulkan surface. Returning
// nothing here is honest; a caller checks the count and the bool.
//------------------------------------------------------------------------------
const char* const* GlxPresentSurface::RequiredVkInstanceExtensions(uint32_t* count) const
{
	if (count) *count = 0;
	return nullptr;
}

bool GlxPresentSurface::CreateVkSurface(void* /*instance*/, void* /*outSurface*/)
{
	LOG_ERROR("CreateVkSurface: Vulkan is Phase 4; this build has a GLX surface only");
	return false;
}
