#pragma once
// ===========================================================================
// win32_window.h - Win32 implementation of ISystemWindow / IPresentSurface.
//
// PRIVATE to the Win32 backend, same rule as win32_private.h: only files
// under system/window/ construct or downcast to these types. Everything
// else reaches the singleton through GetSystemWindow() (sys_window.h).
//
// Method bodies live in winmain.cpp, not a separate win32_window.cpp: they
// are re-homed free functions that used to live there (ToggleBorderlessFull-
// screen, EnableCursorClip, ForceCursorClipUpdate, SetMousePos, GetMousePos,
// RestoreWindowViewport) plus the g_hWnd/g_windowedFallback* state they
// close over. Keeping them in winmain.cpp means that state stays file-local
// (no new externs, no linkage changes) instead of being split across an
// extra translation unit purely to match a class-per-file convention.
// ===========================================================================
#include "sys_window.h"

// Presents the Win32/OpenGL backbuffer. CreateVkSurface() is implemented in
// winmain.cpp (Phase 4a Plan 2): runtime-loaded vkCreateWin32SurfaceKHR.
class Win32PresentSurface : public IPresentSurface {
public:
	void SwapBuffers() override;
	void GetDrawableSize(int* w, int* h) const override;
	const char* const* RequiredVkInstanceExtensions(uint32_t* count) const override;
	bool CreateVkSurface(void* instance, void* outSurface) override;
};

// The Win32 ISystemWindow. Methods re-home the bodies that used to live as
// free functions in winmain.cpp (declared via framework.h); the window
// creation/message-pump plumbing (WndProc, ini/cmdline parsing, monitor
// resolution) stays in winmain.cpp, which now calls through this interface
// for the operations it exposes.
class Win32Window : public ISystemWindow {
public:
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

	IPresentSurface* Presentation() override;

private:
	Win32PresentSurface m_presentSurface;
};
