//==============================================================================
// wayland_window.h -- WaylandWindow, the native Wayland implementation of
// ISystemWindow, and WaylandPresentSurface, its IPresentSurface.
//
// Sibling of linux_window.h (X11). linux_main.cpp picks ONE of the two at
// startup ([main] display, -wayland / -x11, or automatically) and falls back to
// X11 when the Wayland attempt fails, so the X11 backend stays the proven path
// and this one is strictly additive.
//
// All the protocol work - xdg-shell, decorations, scale, cursor shape, pointer
// constraints - lives in the shared native_window library
// (3rdparty/native_window, see native_window_PINNED.txt). This class is only
// the AAE adapter: it maps ISystemWindow onto that library and keeps the
// WindowSetup globals and emulator callbacks, which the library deliberately
// knows nothing about.
//
// NO Xlib, and no Wayland or native_window headers, IN THIS HEADER. The handles
// live behind an opaque Impl in the .cpp, for the same reason linux_window.h
// hides Xlib: <X11/Xlib.h> #defines None, and native_window has an enumerator
// called None, so whichever order a translation unit happens to include them in
// decides whether it compiles.
//
// Input does NOT come from here either. As with the X11 backend, keyboard,
// mouse buttons and pad all come from evdev, because evdev is what provides the
// per-device identity the multi-HID player routing needs. The Wayland key,
// text, axis and relative-motion events are deliberately dropped: feeding the
// same physical key in through both paths would fire every action twice. What
// is taken from Wayland is window management only - configure/resize, close,
// focus, and the click that re-arms pointer capture.
//
// Units. Wayland sizes windows in LOGICAL units and renders at logical * scale
// pixels. WindowSetup.windowWidth/windowHeight, as given to Create(), are
// LOGICAL. Everything the renderer reads - WindowSetup.clientWidth/clientHeight,
// ClientWidth()/ClientHeight(), GetDrawableSize(), GetMousePos() - is PIXELS,
// the same unit the Win32 and X11 backends report, because the viewport, the
// Vulkan extent and glReadPixels all work in pixels.
//==============================================================================
#pragma once

#include "sys_window.h"

#include <string>

class WaylandWindow;

//------------------------------------------------------------------------------
// Presentation. Same split as glx_present: window logic in WaylandWindow, the
// swap and the Vulkan surface here.
//------------------------------------------------------------------------------
class WaylandPresentSurface : public IPresentSurface {
public:
	// Called by WaylandWindow::Create once the window exists.
	void Attach(WaylandWindow* owner) { m_owner = owner; }

	void SwapBuffers() override;
	void GetDrawableSize(int* w, int* h) const override;

	const char* const* RequiredVkInstanceExtensions(uint32_t* count) const override;
	bool CreateVkSurface(void* instance, void* outSurface) override;

private:
	WaylandWindow* m_owner = nullptr;
};

class WaylandWindow : public ISystemWindow {
public:
	~WaylandWindow() override;

	// Connect to the compositor WITHOUT creating a window. Split out of Create()
	// because linux_main.cpp needs the output's size to compute the default
	// window before it creates one, and that must not depend on an X server -
	// the whole point of this backend is a session that has none.
	//
	// On failure returns false and, if given, fills *error with the reason (the
	// caller logs it and falls back to X11). Safe to call more than once.
	bool Connect(std::string* error = nullptr);

	// The first output the compositor reported: size in PIXELS, integer scale
	// and, optionally, its name. False when no output (or no usable mode) has
	// been reported - Wayland can legitimately say nothing here, e.g. a nested
	// compositor that has not mapped an output yet. Valid after Connect().
	bool QueryFirstOutput(int* pixelW, int* pixelH, int* scale,
	                      std::string* name = nullptr) const;

	// windowWidth/windowHeight in `setup` are LOGICAL. Calls Connect() itself if
	// linux_main.cpp has not.
	bool Create(const WindowSetup& setup) override;
	void Destroy() override;
	bool PumpEvents() override;

	int   ClientWidth()  const override;   // PIXELS - see the note at the top
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
	friend class WaylandPresentSurface;

	// Rewrites the title to reflect the current capture state, mirroring
	// LinuxWindow::UpdateTitle() and winmain.cpp's UpdateWindowTitle(). This is
	// the only on-screen hint that F9 releases the pointer.
	void UpdateTitle();

	// For WaylandPresentSurface. The handles are borrowed: they belong to the
	// native_window Context/Window and die with them.
	void  FramebufferSize(int* w, int* h) const;
	void* NativeDisplay() const;
	void* NativeSurface() const;

	struct Impl;
	Impl* m_impl = nullptr;
	WaylandPresentSurface m_presentSurface;
};
