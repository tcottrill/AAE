//==============================================================================
// linux_window.h -- LinuxWindow, the X11 implementation of ISystemWindow.
//
// Mirrors win32/win32_window.h method for method so the two read as siblings.
//
// NO Xlib IN THIS HEADER. <X11/Xlib.h> #defines Bool, None, Status, Success
// and KeyPress; any header that pulls it in poisons every translation unit
// downstream, and `None` in particular collides with ordinary enum names to
// produce errors nowhere near their cause. The Xlib handles live behind an
// opaque Impl in the .cpp.
//
// Input does NOT come from here. X11 events are used only for window
// management (resize, focus, close); keyboard, mouse and pad all come from
// evdev, because evdev is what provides the per-device identity the multi-HID
// player routing needs and X11 cannot.
//==============================================================================
#pragma once

#include "sys_window.h"
#include "linux/glx_present.h"

class LinuxWindow : public ISystemWindow {
public:
	~LinuxWindow() override;

	bool Create(const WindowSetup& setup) override;
	void Destroy() override;
	bool PumpEvents() override;

	int   ClientWidth()  const override;
	int   ClientHeight() const override;
	float DpiScale()     const override;

	void ToggleBorderlessFullscreen() override;
	void RestoreViewport() override;

	void SetCursorVisible(bool visible) override;
	void EnableCursorClip(bool enable) override;
	void ForceCursorClipUpdate() override;
	void SetMousePos(int x, int y) override;
	void GetMousePos(int* x, int* y) const override;

	IPresentSurface* Presentation() override { return &m_presentSurface; }

private:
	// Rewrites the WM title to reflect the current capture state, mirroring
	// winmain.cpp's UpdateWindowTitle(). This is the only on-screen hint that
	// F9 releases the pointer, so it has to track the state, not be set once.
	void UpdateTitle();

	struct Impl;
	Impl* m_impl = nullptr;
	GlxPresentSurface m_presentSurface;
};
