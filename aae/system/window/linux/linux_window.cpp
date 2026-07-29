//==============================================================================
// linux_window.cpp -- see linux_window.h.
//==============================================================================
#include "linux/linux_window.h"

#include "sys_gl.h"
#include "sys_log.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>   // XVisualInfo, for the GLX-matched visual
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/cursorfont.h>

#include <cstdlib>
#include <cstring>

//------------------------------------------------------------------------------
// All Xlib state lives here so linux_window.h stays free of <X11/Xlib.h>.
//------------------------------------------------------------------------------
struct LinuxWindow::Impl {
	Display* dpy    = nullptr;
	Window   win    = 0;
	int      screen = 0;

	Atom wmDeleteWindow  = 0;
	Atom netWmState      = 0;
	Atom netWmStateFull  = 0;

	Cursor blankCursor   = 0;   // 1x1 transparent, created once (see below)
	bool   cursorVisible = true;
	bool   clipEnabled   = false;
	bool   clipActive    = false;   // whether a pointer grab is currently held
	bool   fullscreen    = false;
	bool   focused       = true;

	int clientW = 0;
	int clientH = 0;
	float dpiScale = 1.0f;
};

//------------------------------------------------------------------------------
// A 1x1 fully transparent cursor. X11 has no ShowCursor counter, so hiding is
// done by defining this cursor on the window and showing by undefining it.
// Created ONCE at Create() time - making one per SetCursorVisible call leaks an
// X server resource on every toggle, and callers do toggle per-frame.
//------------------------------------------------------------------------------
static Cursor make_blank_cursor(Display* dpy, Window win)
{
	char zero[8] = { 0 };
	Pixmap pm = XCreateBitmapFromData(dpy, win, zero, 1, 1);
	XColor black{};
	Cursor c = XCreatePixmapCursor(dpy, pm, pm, &black, &black, 0, 0);
	XFreePixmap(dpy, pm);
	return c;
}

LinuxWindow::~LinuxWindow()
{
	Destroy();
}

bool LinuxWindow::Create(const WindowSetup& setup)
{
	m_impl = new Impl();

	m_impl->dpy = XOpenDisplay(nullptr);
	if (!m_impl->dpy) {
		// Naming DISPLAY in the message matters: under WSL and over SSH a
		// missing or wrong DISPLAY is far and away the most common cause, and
		// "cannot open display" alone sends people looking at the GPU.
		const char* disp = getenv("DISPLAY");
		LOG_ERROR("XOpenDisplay failed (DISPLAY=%s). No X server reachable.",
		          disp ? disp : "<unset>");
		delete m_impl; m_impl = nullptr;
		return false;
	}

	m_impl->screen = DefaultScreen(m_impl->dpy);

	int w = setup.windowWidth  > 0 ? setup.windowWidth  : 1024;
	int h = setup.windowHeight > 0 ? setup.windowHeight : 768;

	int x = 0, y = 0;
	if (setup.centerWindow) {
		x = (DisplayWidth (m_impl->dpy, m_impl->screen) - w) / 2;
		y = (DisplayHeight(m_impl->dpy, m_impl->screen) - h) / 2;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
	}

	// ORDER MATTERS. The GL framebuffer config must be chosen FIRST, and this
	// window created with ITS visual - GLX requires the drawable's visual to be
	// compatible with the context's FBConfig. Creating the window with
	// CopyFromParent and picking an FBConfig afterwards yields a window that
	// makes a context, reports no error, swaps buffers happily, and displays
	// nothing whatsoever.
	XVisualInfo* vi = static_cast<XVisualInfo*>(
		sys_gl_choose_x11_visual(m_impl->dpy, m_impl->screen, /*multisample=*/0));
	if (!vi) {
		LOG_ERROR("LinuxWindow: no GLX-compatible visual available");
		XCloseDisplay(m_impl->dpy);
		delete m_impl; m_impl = nullptr;
		return false;
	}

	XSetWindowAttributes swa{};
	swa.background_pixel = BlackPixel(m_impl->dpy, m_impl->screen);
	// A colormap for the CHOSEN visual. Without one, XCreateWindow fails with
	// BadMatch whenever that visual differs from the root's.
	swa.colormap = XCreateColormap(m_impl->dpy,
	                               RootWindow(m_impl->dpy, m_impl->screen),
	                               vi->visual, AllocNone);
	swa.border_pixel = 0;
	// StructureNotify gives resize/map, FocusChange drives the pointer-grab
	// release below. Key/Button masks are deliberately absent - input comes
	// from evdev, not X11.
	swa.event_mask = StructureNotifyMask | FocusChangeMask | ExposureMask;

	m_impl->win = XCreateWindow(
		m_impl->dpy, RootWindow(m_impl->dpy, m_impl->screen),
		x, y, (unsigned)w, (unsigned)h, 0,
		vi->depth, InputOutput, vi->visual,
		CWBackPixel | CWBorderPixel | CWColormap | CWEventMask, &swa);

	XFree(vi);

	if (!m_impl->win) {
		LOG_ERROR("XCreateWindow failed");
		XCloseDisplay(m_impl->dpy);
		delete m_impl; m_impl = nullptr;
		return false;
	}

	XStoreName(m_impl->dpy, m_impl->win, "AAE");

	m_impl->wmDeleteWindow = XInternAtom(m_impl->dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(m_impl->dpy, m_impl->win, &m_impl->wmDeleteWindow, 1);

	m_impl->netWmState     = XInternAtom(m_impl->dpy, "_NET_WM_STATE", False);
	m_impl->netWmStateFull = XInternAtom(m_impl->dpy, "_NET_WM_STATE_FULLSCREEN", False);

	m_impl->blankCursor = make_blank_cursor(m_impl->dpy, m_impl->win);

	XMapWindow(m_impl->dpy, m_impl->win);
	// XSync, not XFlush: XMapWindow is asynchronous, and everything after this
	// (the GLX context, the pointer grab) assumes the window is actually
	// viewable. XFlush only pushes the request out; XSync waits for the server
	// to have processed it.
	XSync(m_impl->dpy, False);

	// Report what the SERVER thinks, not what we asked for. A window that is
	// created and mapped without error can still be unviewable or zero-sized,
	// and that looks identical to "no window appeared" from the outside.
	{
		XWindowAttributes wa{};
		if (XGetWindowAttributes(m_impl->dpy, m_impl->win, &wa)) {
			const char* st = (wa.map_state == IsViewable)   ? "IsViewable"
			               : (wa.map_state == IsUnviewable) ? "IsUnviewable"
			                                                : "IsUnmapped";
			LOG_INFO("LinuxWindow: server reports map_state=%s geometry=%dx%d at %d,%d",
			         st, wa.width, wa.height, wa.x, wa.y);
		} else {
			LOG_ERROR("LinuxWindow: XGetWindowAttributes failed after mapping");
		}
	}

	m_impl->clientW = w;
	m_impl->clientH = h;

	// The renderer reads its viewport and FBO size from WindowSetup, NOT from
	// ISystemWindow - opengl_renderer.cpp does glViewport(0, 0, ws.clientWidth,
	// ws.clientHeight) and sizes screen_rect from the same pair. They default
	// to 0, so leaving them unset renders a perfectly working black window.
	// winmain.cpp keeps them current from WM_SIZE; ConfigureNotify is the
	// counterpart here.
	GetWindowSetup().clientWidth  = w;
	GetWindowSetup().clientHeight = h;

	// X11 has no per-monitor DPI API. Xft.dpi in the resource database is what
	// desktop environments actually set, so read that and fall back to 1.0.
	if (char* rm = XResourceManagerString(m_impl->dpy)) {
		XrmInitialize();
		if (XrmDatabase db = XrmGetStringDatabase(rm)) {
			char* type = nullptr;
			XrmValue val{};
			if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &val) && val.addr) {
				float dpi = (float)atof(val.addr);
				if (dpi > 0.0f) m_impl->dpiScale = dpi / 96.0f;
			}
			XrmDestroyDatabase(db);
		}
	}

	// Hand the target to both consumers before anyone tries to make a context.
	m_presentSurface.Attach(m_impl->dpy, (unsigned long)m_impl->win);
	sys_gl_set_x11_target(m_impl->dpy, (unsigned long)m_impl->win);

	LOG_INFO("LinuxWindow: %dx%d at %d,%d (dpi scale %.2f)", w, h, x, y, m_impl->dpiScale);

	if (setup.useFullscreen || setup.borderlessFullscreen)
		ToggleBorderlessFullscreen();

	return true;
}

void LinuxWindow::Destroy()
{
	if (!m_impl) return;

	if (m_impl->dpy) {
		if (m_impl->clipActive)  XUngrabPointer(m_impl->dpy, CurrentTime);
		if (m_impl->blankCursor) XFreeCursor(m_impl->dpy, m_impl->blankCursor);
		if (m_impl->win)         XDestroyWindow(m_impl->dpy, m_impl->win);
		XCloseDisplay(m_impl->dpy);
	}

	delete m_impl;
	m_impl = nullptr;
}

bool LinuxWindow::PumpEvents()
{
	if (!m_impl || !m_impl->dpy) return false;

	bool keepRunning = true;

	// One-shot: report the map state once the WM has had a chance to respond.
	// XMapWindow is asynchronous with respect to the WINDOW MANAGER (XSync only
	// waits for our own requests), so checking immediately after mapping can
	// legitimately still read IsUnmapped.
	static bool s_reportedMap = false;
	if (!s_reportedMap) {
		XWindowAttributes wa{};
		if (XGetWindowAttributes(m_impl->dpy, m_impl->win, &wa) &&
		    wa.map_state == IsViewable) {
			s_reportedMap = true;
			LOG_INFO("LinuxWindow: window is now IsViewable (%dx%d at %d,%d)",
			         wa.width, wa.height, wa.x, wa.y);
		}
	}

	while (XPending(m_impl->dpy)) {
		XEvent ev;
		XNextEvent(m_impl->dpy, &ev);

		switch (ev.type) {
		case ConfigureNotify:
			m_impl->clientW = ev.xconfigure.width;
			m_impl->clientH = ev.xconfigure.height;
			// Keep WindowSetup in step - the renderer's viewport comes from
			// there, not from this class. See the note in Create().
			GetWindowSetup().clientWidth  = m_impl->clientW;
			GetWindowSetup().clientHeight = m_impl->clientH;
			break;

		case FocusIn:
			m_impl->focused = true;
			// Re-apply the confine if the user asked for one.
			if (m_impl->clipEnabled) ForceCursorClipUpdate();
			break;

		case FocusOut:
			m_impl->focused = false;
			// CRITICAL: drop the pointer grab. A grab held while unfocused
			// confines the pointer for the WHOLE desktop, not just this
			// window - the X11 equivalent of a stuck ClipCursor, and much
			// harder for a user to escape from.
			if (m_impl->clipActive) {
				XUngrabPointer(m_impl->dpy, CurrentTime);
				m_impl->clipActive = false;
			}
			break;

		case ClientMessage:
			if ((Atom)ev.xclient.data.l[0] == m_impl->wmDeleteWindow) {
				LOG_INFO("LinuxWindow: WM_DELETE_WINDOW - quitting");
				keepRunning = false;
			}
			break;

		default:
			break;
		}
	}

	return keepRunning;
}

int   LinuxWindow::ClientWidth()  const { return m_impl ? m_impl->clientW  : 0; }
int   LinuxWindow::ClientHeight() const { return m_impl ? m_impl->clientH  : 0; }
float LinuxWindow::DpiScale()     const { return m_impl ? m_impl->dpiScale : 1.0f; }

void LinuxWindow::ToggleBorderlessFullscreen()
{
	if (!m_impl || !m_impl->dpy) return;

	m_impl->fullscreen = !m_impl->fullscreen;

	// EWMH, not override-redirect. Override-redirect bypasses the window
	// manager entirely: it breaks alt-tab, ignores multi-monitor layout, and
	// misbehaves under compositors including gamescope. _NET_WM_STATE is what
	// the WM is there to handle.
	XEvent ev{};
	ev.type                 = ClientMessage;
	ev.xclient.window       = m_impl->win;
	ev.xclient.message_type = m_impl->netWmState;
	ev.xclient.format       = 32;
	ev.xclient.data.l[0]    = m_impl->fullscreen ? 1 : 0;   // _NET_WM_STATE_ADD/REMOVE
	ev.xclient.data.l[1]    = (long)m_impl->netWmStateFull;
	ev.xclient.data.l[2]    = 0;
	ev.xclient.data.l[3]    = 1;                            // source: application

	XSendEvent(m_impl->dpy, RootWindow(m_impl->dpy, m_impl->screen), False,
	           SubstructureNotifyMask | SubstructureRedirectMask, &ev);
	XFlush(m_impl->dpy);

	LOG_INFO("LinuxWindow: fullscreen %s", m_impl->fullscreen ? "on" : "off");
}

void LinuxWindow::RestoreViewport()
{
	if (m_impl && m_impl->fullscreen)
		ToggleBorderlessFullscreen();
}

void LinuxWindow::SetCursorVisible(bool visible)
{
	if (!m_impl || !m_impl->dpy) return;
	if (visible == m_impl->cursorVisible) return;

	if (visible) XUndefineCursor(m_impl->dpy, m_impl->win);
	else         XDefineCursor(m_impl->dpy, m_impl->win, m_impl->blankCursor);

	XFlush(m_impl->dpy);
	m_impl->cursorVisible = visible;
}

void LinuxWindow::EnableCursorClip(bool enable)
{
	if (!m_impl) return;
	m_impl->clipEnabled = enable;
	ForceCursorClipUpdate();
}

void LinuxWindow::ForceCursorClipUpdate()
{
	if (!m_impl || !m_impl->dpy) return;

	const bool want = m_impl->clipEnabled && m_impl->focused;

	if (want && !m_impl->clipActive) {
		// confine_to = our window is the closest X11 has to ClipCursor.
		// owner_events=True so the app still receives its own events normally.
		int r = XGrabPointer(m_impl->dpy, m_impl->win, True,
		                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		                     GrabModeAsync, GrabModeAsync,
		                     m_impl->win, None, CurrentTime);
		if (r == GrabSuccess) {
			m_impl->clipActive = true;
		} else {
			// Not fatal - another client may hold a grab. Say so rather than
			// leaving the caller to wonder why the pointer still escapes.
			LOG_INFO("LinuxWindow: XGrabPointer failed (%d); cursor not confined", r);
		}
	} else if (!want && m_impl->clipActive) {
		XUngrabPointer(m_impl->dpy, CurrentTime);
		m_impl->clipActive = false;
	}
}

void LinuxWindow::SetMousePos(int x, int y)
{
	if (!m_impl || !m_impl->dpy) return;
	// Window-relative, matching the Win32 side.
	XWarpPointer(m_impl->dpy, None, m_impl->win, 0, 0, 0, 0, x, y);
	XFlush(m_impl->dpy);
}

void LinuxWindow::GetMousePos(int* x, int* y) const
{
	if (x) *x = 0;
	if (y) *y = 0;
	if (!m_impl || !m_impl->dpy) return;

	Window root = 0, child = 0;
	int rootX = 0, rootY = 0, winX = 0, winY = 0;
	unsigned int mask = 0;

	if (XQueryPointer(m_impl->dpy, m_impl->win, &root, &child,
	                  &rootX, &rootY, &winX, &winY, &mask)) {
		if (x) *x = winX;
		if (y) *y = winY;
	}
}
