//==============================================================================
// linux_window.cpp -- see linux_window.h.
//==============================================================================
#include "linux/linux_window.h"

#include "sys_gl.h"
#include "sys_input.h"   // RawInput_SetPaused, on focus change
#include "sys_log.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>   // XVisualInfo, for the GLX-matched visual
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/cursorfont.h>

#include <cstdlib>
#include <cstring>

// opengl_renderer.h, declared here rather than included: that header drags in GL
// and the whole renderer surface for one function (same reasoning as
// linux_main.cpp, which forward-declares it for the same reason).
void emulator_on_window_resize(int newW, int newH);

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
	bool   clipPending   = false;   // grab wanted but not held - retry each frame
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
	// release below. KEY masks are deliberately absent - keyboard input comes
	// from evdev, not X11.
	//
	// ButtonPress is the one exception, and it is window MANAGEMENT, not game
	// input: it re-arms capture when the user clicks back into the window after
	// releasing with F9, exactly as WM_LBUTTONDOWN does in winmain.cpp. It has
	// to come from X11 rather than evdev precisely because X11 delivers it only
	// when the click lands on OUR window - an evdev button would re-capture on
	// any click anywhere on the desktop.
	swa.event_mask = StructureNotifyMask | FocusChangeMask | ExposureMask |
	                 ButtonPressMask;

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

	// Match winmain.cpp: the configured capture state arrives in WindowSetup, so
	// seed from there rather than defaulting to off. linux_main.cpp calls
	// EnableCursorClip() once the GL context exists to actually apply it.
	m_impl->clipEnabled = setup.cursorClipEnabled;
	UpdateTitle();

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
		{
			const int newW = ev.xconfigure.width;
			const int newH = ev.xconfigure.height;
			// ConfigureNotify also fires for pure MOVES, so only react to an
			// actual size change - emulator_on_window_resize below rebuilds the
			// screen rect and is not free.
			const bool sizeChanged = (newW != m_impl->clientW) || (newH != m_impl->clientH);

			m_impl->clientW = newW;
			m_impl->clientH = newH;
			// Keep WindowSetup in step - the renderer's viewport comes from
			// there, not from this class. See the note in Create().
			GetWindowSetup().clientWidth  = newW;
			GetWindowSetup().clientHeight = newH;

			// THE resize hook, and it was missing: this is what WM_SIZE calls on
			// Windows (winmain.cpp), and without it ALT+ENTER left the game
			// drawn at the old size and off-centre.
			//
			// Raster games happened to adapt anyway - Layout_Render re-reads
			// clientWidth/Height every frame - but the VECTOR path draws through
			// screen_rect, which holds absolute pixel coordinates computed once
			// and only ever re-fitted here. That is also what re-centres the
			// image: UpdateScreenRect recomputes the centring offsets from the
			// new client size.
			//
			// Safe before GL exists: emulator_on_window_resize returns
			// immediately while screen_rect is null.
			if (sizeChanged && newW > 0 && newH > 0)
				emulator_on_window_resize(newW, newH);
			break;
		}

		case FocusIn:
			m_impl->focused = true;
			// Re-apply the confine, and the cursor visibility that goes with it.
			// Unconditional: when capture is OFF this is what puts the pointer
			// back on screen, which is just as much part of "focus regained".
			ForceCursorClipUpdate();
			RawInput_SetPaused(false);
			break;

		case FocusOut:
			m_impl->focused = false;
			// Pause input, as winmain.cpp does on WM_ACTIVATEAPP.
			//
			// This matters MORE here than it does on Windows. evdev is a
			// global tap: it keeps delivering events from the physical
			// devices no matter which window the desktop considers focused,
			// so without this an alt-tabbed AAE carries on eating every
			// keystroke meant for the other application. Pausing also clears
			// the key state, which is what stops a key held at the moment of
			// the switch from being stuck down on return.
			RawInput_SetPaused(true);
			// CRITICAL: drop the pointer grab. A grab held while unfocused
			// confines the pointer for the WHOLE desktop, not just this
			// window - the X11 equivalent of a stuck ClipCursor, and much
			// harder for a user to escape from.
			//
			// Routed through ForceCursorClipUpdate rather than ungrabbing
			// inline: with `focused` now false it ungrabs exactly as before,
			// and it ALSO restores the cursor - which an inline ungrab does
			// not, now that hiding is a window attribute rather than a
			// property of the grab. Alt-tabbing away used to be able to leave
			// an invisible pointer over our own window.
			ForceCursorClipUpdate();
			break;

		case ButtonPress:
			// Clicking back into the game view re-arms capture, mirroring
			// WM_LBUTTONDOWN/WM_RBUTTONDOWN in winmain.cpp. Left and right only:
			// the middle button and the wheel (buttons 4/5) must not re-capture.
			//
			// Not game input - see the event-mask note in Create().
			if ((ev.xbutton.button == Button1 || ev.xbutton.button == Button3) &&
			    !m_impl->clipEnabled) {
				EnableCursorClip(true);
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

	// Retry a grab that was asked for but could not be taken yet.
	//
	// THIS is what fixes capture-at-startup. linux_main.cpp calls
	// EnableCursorClip() immediately after Create() returns, and at that moment
	// the window is still IsUnmapped: XMapWindow is asynchronous with respect to
	// the WINDOW MANAGER, and the XSync in Create() only waits for our own
	// requests, not for the WM's reparent+map (the same caveat the map report
	// above is written around). XGrabPointer on a window that is not viewable
	// fails with GrabNotViewable, so the very first capture of every session was
	// silently dropped - measured, not assumed.
	//
	// Retrying here rather than sleeping for the WM keeps it general: it also
	// covers AlreadyGrabbed, which is what happens when another client holds a
	// grab (a menu is open, a WM is mid-drag) at the moment we ask.
	if (m_impl->clipPending)
		ForceCursorClipUpdate();

	return keepRunning;
}

int   LinuxWindow::ClientWidth()  const { return m_impl ? m_impl->clientW  : 0; }
int   LinuxWindow::ClientHeight() const { return m_impl ? m_impl->clientH  : 0; }
float LinuxWindow::DpiScale()     const { return m_impl ? m_impl->dpiScale : 1.0f; }

void LinuxWindow::ToggleBorderlessFullscreen()
{
	if (!m_impl || !m_impl->dpy) return;

	m_impl->fullscreen = !m_impl->fullscreen;

	// Publish to the shared WindowSetup, as the Win32 version does. Not
	// bookkeeping: menu.cpp reads borderlessFullscreen to DISPLAY the FULLSCREEN
	// item, to decide which arrow keys are live on it, and to persist
	// [window] fullscreen on save. Left unset, the Linux Video menu always
	// showed "NO" and always saved 0, whatever the window was actually doing.
	GetWindowSetup().borderlessFullscreen = m_impl->fullscreen;

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

void LinuxWindow::UpdateTitle()
{
	if (!m_impl || !m_impl->dpy || !m_impl->win) return;

	// Same wording as winmain.cpp so the two platforms read identically.
	const char* title = m_impl->clipEnabled
		? "AAE - Mouse Captured (F9 to release)"
		: "AAE";

	XStoreName(m_impl->dpy, m_impl->win, title);
	XFlush(m_impl->dpy);
}

void LinuxWindow::EnableCursorClip(bool enable)
{
	if (!m_impl) return;
	m_impl->clipEnabled = enable;

	// Keep the shared WindowSetup in step, as Win32Window::EnableCursorClip
	// does. This is NOT bookkeeping for its own sake: msg_loop()'s F9 handler
	// reads cursorClipEnabled to decide which way to toggle, so without this the
	// flag would never change and F9 would only ever capture, never release.
	GetWindowSetup().cursorClipEnabled = enable;

	UpdateTitle();
	ForceCursorClipUpdate();
}

void LinuxWindow::ForceCursorClipUpdate()
{
	if (!m_impl || !m_impl->dpy) return;

	// Same condition winmain.cpp's UpdateCursorState() uses for BOTH halves:
	// hide and confine together, show and release together.
	const bool want = m_impl->clipEnabled && m_impl->focused;

	// Visibility FIRST, and independently of the grab.
	//
	// Hiding used to ride entirely on the grab cursor passed to XGrabPointer,
	// on the reasoning that a grab cursor cannot drift out of step with the
	// grab. True, but it made visibility inherit every one of the grab's
	// failure modes - and the grab genuinely does fail, both at startup (see
	// PumpEvents) and any time another client already holds one. The pointer
	// stayed a visible arrow until something unrelated happened to re-trigger
	// this function.
	//
	// The window's own cursor attribute has no such dependency: the server
	// applies it whenever the pointer is over our window, grab or no grab. That
	// is also the closer analogue of ShowCursor(FALSE), which likewise owes
	// nothing to ClipCursor. SetCursorVisible early-outs when already in the
	// requested state, so calling it on every retry costs no X traffic.
	SetCursorVisible(!want);

	if (want && !m_impl->clipActive) {
		// confine_to = our window is the closest X11 has to ClipCursor.
		// owner_events=True so the app still receives its own events normally.
		// blankCursor stays as the grab cursor so the pointer is hidden for the
		// whole confined area even where the window attribute would not reach.
		int r = XGrabPointer(m_impl->dpy, m_impl->win, True,
		                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		                     GrabModeAsync, GrabModeAsync,
		                     m_impl->win, m_impl->blankCursor, CurrentTime);
		if (r == GrabSuccess) {
			m_impl->clipActive = true;
			if (m_impl->clipPending)
				LOG_INFO("LinuxWindow: pointer grab acquired on retry");
		} else if (!m_impl->clipPending) {
			// Logged once per pending run, not once per frame - PumpEvents
			// retries this every frame until it takes.
			LOG_INFO("LinuxWindow: XGrabPointer failed (%d); cursor hidden but "
			         "not yet confined - retrying", r);
		}
	} else if (!want && m_impl->clipActive) {
		XUngrabPointer(m_impl->dpy, CurrentTime);
		m_impl->clipActive = false;
	}

	// Drives the retry in PumpEvents. Also self-clearing: once capture is no
	// longer wanted (F9, focus lost) the retry stops on its own.
	m_impl->clipPending = want && !m_impl->clipActive;
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
