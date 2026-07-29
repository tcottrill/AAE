//==============================================================================
// glx_present.h -- IPresentSurface over GLX.
//
// Separate from linux_window.cpp on purpose. Phase 3a split presentation from
// windowing so a headless backend could honestly return nullptr instead of
// stubbing a swapchain it has no concept of; the same split is what will let a
// future Wayland or Vulkan backend reuse the window logic unchanged. It also
// keeps GL and Xlib headers out of linux_window.h.
//==============================================================================
#pragma once

#include "sys_window.h"

class GlxPresentSurface : public IPresentSurface {
public:
	// Called by LinuxWindow::Create once the X11 window exists. Typed
	// void*/unsigned long so this header needs no Xlib include - see the note
	// on sys_gl_set_x11_target() in sys_gl.h for why that matters.
	void Attach(void* display, unsigned long window);

	void SwapBuffers() override;
	void GetDrawableSize(int* w, int* h) const override;

	const char* const* RequiredVkInstanceExtensions(uint32_t* count) const override;
	bool CreateVkSurface(void* instance, void* outSurface) override;

private:
	void*         m_display = nullptr;
	unsigned long m_window  = 0;
};
