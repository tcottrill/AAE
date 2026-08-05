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

#include <dlfcn.h>       // XFixes is loaded at runtime, not linked - see Impl

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

	// Frames left to re-assert hide+grab after the window becomes VIEWABLE,
	// and whether the one-shot priming warp has been done. See the priming
	// block in PumpEvents for why once is not enough.
	int  cursorPrimeFrames = 0;
	bool cursorWarped      = false;
	bool cursorEnterSeen   = false;   // an EnterNotify has arrived at least once
	int  focusLogCount     = 0;       // caps the FocusIn/FocusOut logging

	// XFixes, loaded at runtime rather than linked.
	//
	// This is what actually hides the pointer under gamescope. A window cursor
	// attribute is advice about OUR window; gamescope composites its own
	// pointer overlay and had gone on drawing an arrow over a window that was
	// created blank, with a blank grab cursor, and with the grab held - the log
	// said cursorVisible=0 clipActive=1 while an arrow was plainly on screen.
	// XFixesHideCursor is a DISPLAY-level request, which is the level the thing
	// drawing the arrow operates at.
	//
	// dlopen'd because the runtime library is present everywhere that matters
	// but the -dev package often is not, and this must not become a hard build
	// dependency for a fallback path. Same approach the Vulkan loader uses.
	void* xfixesLib = nullptr;
	int  (*pQueryExtension)(Display*, int*, int*) = nullptr;
	int  (*pQueryVersion)(Display*, int*, int*)   = nullptr;
	void (*pHideCursor)(Display*, Window)         = nullptr;
	void (*pShowCursor)(Display*, Window)         = nullptr;
	bool xfixesReady  = false;
	bool xfixesHidden = false;

	// Hide/show are REFERENCE COUNTED per client by the server: N hides need N
	// shows. The priming loop calls applyCursor every frame, so this must only
	// act on a transition - counting up ten times and back once would leave the
	// pointer hidden for good, including after F9.
	void applyXfixes(bool hide) {
		if (!xfixesReady || hide == xfixesHidden) return;
		if (hide) pHideCursor(dpy, win);
		else      pShowCursor(dpy, win);
		xfixesHidden = hide;
	}

	// XTEST, also loaded at runtime: the "wiggle the mouse for them" fallback.
	//
	// XWarpPointer is not enough under XWayland - Wayland gives ordinary
	// clients no way to move the pointer, so a warp is close to a no-op there
	// and the log showed it changing nothing. XTEST is different: it injects
	// motion through the server's input path, which is the same road real
	// device input travels, so the compositor processes it as genuine movement
	// and finally sends the pointer-enter that makes a cursor change legal.
	//
	// One pixel out and one pixel back, so the pointer ends where it started.
	void* xtestLib = nullptr;
	int (*pFakeRelativeMotion)(Display*, int, int, unsigned long) = nullptr;
	bool xtestReady = false;

	void wigglePointer() {
		if (!xtestReady) return;
		pFakeRelativeMotion(dpy, 1, 0, 0);
		pFakeRelativeMotion(dpy, -1, 0, 0);
		XFlush(dpy);
	}

	// The ROOT window's cursor, which is a different lever from our own
	// window's. A window that defines no cursor of its own inherits its
	// parent's, up to the root, so setting it there covers the case where the
	// pointer is not being treated as "inside" our surface at all - which is
	// exactly the state XWayland leaves us in before it grants pointer focus.
	//
	// MUST be restored. Unlike our own window, the root outlives this process,
	// so a blank cursor left on it is a desktop with no pointer. Destroy()
	// restores it, and a hard crash is the one case that would leave it set -
	// recoverable by any app that sets its own cursor, but worth knowing.
	Window root          = 0;
	bool   rootCursorSet = false;

	void applyRootCursor(bool hide) {
		if (!dpy || !root || hide == rootCursorSet) return;
		if (hide) XDefineCursor(dpy, root, blankCursor);
		else      XUndefineCursor(dpy, root);
		rootCursorSet = hide;
	}

	int clientW = 0;
	int clientH = 0;
	float dpiScale = 1.0f;

	// Issues the cursor attribute UNCONDITIONALLY, unlike SetCursorVisible,
	// which early-outs when the state already matches. The priming retry needs
	// to re-send a request the server has already been given, so it cannot go
	// through the state-guarded path.
	void applyCursor() {
		if (!dpy || !win) return;
		if (cursorVisible) XUndefineCursor(dpy, win);
		else               XDefineCursor(dpy, win, blankCursor);
		applyRootCursor(!cursorVisible);
		applyXfixes(!cursorVisible);
		XFlush(dpy);
	}
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
	// EnterWindowMask is here for XWayland, and it is the piece that was
	// missing. Under a Wayland compositor the pointer image belongs to the
	// compositor, and wl_pointer.set_cursor is only legal with the serial from
	// a wl_pointer.enter - so XDefineCursor, the CWCursor attribute below, the
	// grab cursor and even XFixesHideCursor are ALL silently dropped until the
	// pointer has entered our surface. EnterNotify is how we find out that the
	// serial exists, and the handler re-sends the cursor at that instant.
	// Without it the first legal moment came from the user physically moving
	// the mouse, which is precisely the reported symptom.
	swa.event_mask = StructureNotifyMask | FocusChangeMask | ExposureMask |
	                 ButtonPressMask | EnterWindowMask | LeaveWindowMask;

	// The blank cursor is created HERE, from the root drawable, so it can be
	// handed to XCreateWindow as the window's own cursor attribute.
	//
	// This is what makes hiding immune to startup timing, and it is the third
	// attempt at this bug. Applying it afterwards with XDefineCursor is a
	// request the server acts on when it next evaluates the pointer over this
	// window, and on SteamOS/gamescope that did not happen until the mouse was
	// physically moved: nothing else here generates a pointer event, because
	// the window selects neither EnterWindowMask nor PointerMotionMask and
	// motion otherwise arrives only through the grab we are still trying to
	// take. Worse, the one XDefineCursor we did issue went out while the window
	// was still IsUnmapped, and the state guard in SetCursorVisible then
	// suppressed every later attempt as redundant.
	//
	// A window CREATED with the attribute has never had any other cursor to
	// show, so there is no event to wait for and no state to re-assert.
	m_impl->root        = RootWindow(m_impl->dpy, m_impl->screen);
	m_impl->blankCursor = make_blank_cursor(m_impl->dpy, m_impl->root);
	swa.cursor          = m_impl->blankCursor;

	m_impl->win = XCreateWindow(
		m_impl->dpy, RootWindow(m_impl->dpy, m_impl->screen),
		x, y, (unsigned)w, (unsigned)h, 0,
		vi->depth, InputOutput, vi->visual,
		CWBackPixel | CWBorderPixel | CWColormap | CWEventMask | CWCursor, &swa);

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

	// The window was created WITH the blank cursor, so the state has to say so.
	// Left at its `true` default it would describe a cursor that is not on
	// screen, and SetCursorVisible(true) - what F9 does - would early-out as
	// "already visible" and never issue the XUndefineCursor, making the release
	// silently do nothing.
	m_impl->cursorVisible = false;

	// What are we actually running on? Four builds were spent assuming
	// gamescope because the symptom was reported from a Steam Machine, when the
	// session was in fact Plasma-on-Wayland in Desktop Mode - which is a
	// completely different cursor path. Panel geometry was the only clue, and
	// inferring the environment from the size of a taskbar is not a diagnostic.
	{
		int evb = 0, erb = 0;
		const bool xwayland = XQueryExtension(m_impl->dpy, "XWAYLAND", &evb, &erb, &erb);
		const char* sessionType = getenv("XDG_SESSION_TYPE");
		const char* desktop     = getenv("XDG_CURRENT_DESKTOP");
		LOG_INFO("LinuxWindow: display server = %s, XDG_SESSION_TYPE=%s, "
		         "XDG_CURRENT_DESKTOP=%s, WAYLAND_DISPLAY=%s",
		         xwayland ? "XWayland (cursor is the compositor's)" : "native X11",
		         sessionType ? sessionType : "(unset)",
		         desktop     ? desktop     : "(unset)",
		         getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(unset)");
	}

	// Bring up XFixes and hide the pointer at the DISPLAY level, before the
	// window is ever mapped. Everything tried before this operated on our
	// window's cursor attribute, which gamescope is free to ignore because it
	// draws its own pointer overlay - and did, even with the grab held.
	//
	// Soft dependency throughout: a missing library, a missing symbol or a
	// server without the extension all just leave xfixesReady false and fall
	// back to the window attribute, which is correct everywhere else.
	m_impl->xfixesLib = dlopen("libXfixes.so.3", RTLD_LAZY | RTLD_LOCAL);
	if (!m_impl->xfixesLib)
		m_impl->xfixesLib = dlopen("libXfixes.so", RTLD_LAZY | RTLD_LOCAL);

	if (m_impl->xfixesLib) {
		*(void**)(&m_impl->pQueryExtension) = dlsym(m_impl->xfixesLib, "XFixesQueryExtension");
		*(void**)(&m_impl->pQueryVersion)   = dlsym(m_impl->xfixesLib, "XFixesQueryVersion");
		*(void**)(&m_impl->pHideCursor)     = dlsym(m_impl->xfixesLib, "XFixesHideCursor");
		*(void**)(&m_impl->pShowCursor)     = dlsym(m_impl->xfixesLib, "XFixesShowCursor");

		int evBase = 0, errBase = 0, major = 0, minor = 0;
		if (m_impl->pQueryExtension && m_impl->pQueryVersion &&
		    m_impl->pHideCursor && m_impl->pShowCursor &&
		    m_impl->pQueryExtension(m_impl->dpy, &evBase, &errBase)) {
			// QueryVersion is not optional: it is what negotiates the version
			// with the server, and HideCursor needs XFixes 4 or later.
			m_impl->pQueryVersion(m_impl->dpy, &major, &minor);
			if (major > 4 || (major == 4 && minor >= 0)) {
				m_impl->xfixesReady = true;
				LOG_INFO("LinuxWindow: XFixes %d.%d available - using display-level cursor hiding",
				         major, minor);
			} else {
				LOG_INFO("LinuxWindow: XFixes %d.%d too old for HideCursor (need 4) - "
				         "falling back to the window cursor attribute", major, minor);
			}
		} else {
			LOG_INFO("LinuxWindow: XFixes present but unusable - "
			         "falling back to the window cursor attribute");
		}
	} else {
		LOG_INFO("LinuxWindow: libXfixes not found - "
		         "falling back to the window cursor attribute");
	}

	// XTEST, for the wiggle fallback in PumpEvents. Soft like XFixes: absent
	// library or symbol simply leaves xtestReady false.
	m_impl->xtestLib = dlopen("libXtst.so.6", RTLD_LAZY | RTLD_LOCAL);
	if (!m_impl->xtestLib)
		m_impl->xtestLib = dlopen("libXtst.so", RTLD_LAZY | RTLD_LOCAL);
	if (m_impl->xtestLib) {
		*(void**)(&m_impl->pFakeRelativeMotion) =
			dlsym(m_impl->xtestLib, "XTestFakeRelativeMotionEvent");
		m_impl->xtestReady = (m_impl->pFakeRelativeMotion != nullptr);
	}
	LOG_INFO("LinuxWindow: XTEST pointer nudge %s",
	         m_impl->xtestReady ? "available" : "NOT available");

	// The window was born blank; make the display-level state agree right now,
	// before the map, rather than waiting for a frame that will not run until
	// the whole emulator has finished initialising.
	m_impl->applyXfixes(true);

	UpdateTitle();

	m_impl->wmDeleteWindow = XInternAtom(m_impl->dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(m_impl->dpy, m_impl->win, &m_impl->wmDeleteWindow, 1);

	m_impl->netWmState     = XInternAtom(m_impl->dpy, "_NET_WM_STATE", False);
	m_impl->netWmStateFull = XInternAtom(m_impl->dpy, "_NET_WM_STATE_FULLSCREEN", False);

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

	// Set AFTER the fullscreen switch above, so the priming frames span the
	// window settling into its final size rather than being spent on the
	// windowed one it briefly was. These are VIEWABLE frames - PumpEvents does
	// not start counting until the server says the window is on screen, so the
	// budget cannot be burnt through while it is still unmapped.
	m_impl->cursorPrimeFrames = 10;

	return true;
}

void LinuxWindow::Destroy()
{
	if (!m_impl) return;

	if (m_impl->dpy) {
		if (m_impl->clipActive)  XUngrabPointer(m_impl->dpy, CurrentTime);
		// Balance the XFixes hide before the window goes away. The server drops
		// it when the client disconnects anyway, but not doing it leaves the
		// desktop without a pointer for however long teardown takes.
		m_impl->applyXfixes(false);
		// Before the cursor is freed, and before the display closes: hand the
		// desktop its pointer back. Leaving a blank cursor on the root is not
		// our window's problem to inherit - it is everyone's.
		m_impl->applyRootCursor(false);
		XFlush(m_impl->dpy);
		if (m_impl->blankCursor) XFreeCursor(m_impl->dpy, m_impl->blankCursor);
		if (m_impl->win)         XDestroyWindow(m_impl->dpy, m_impl->win);
		XCloseDisplay(m_impl->dpy);
	}

	if (m_impl->xfixesLib) dlclose(m_impl->xfixesLib);
	if (m_impl->xtestLib)  dlclose(m_impl->xtestLib);

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

		case EnterNotify:
			// The first moment a Wayland compositor will accept a cursor
			// change for this surface. Re-send unconditionally - going through
			// SetCursorVisible would early-out, because the state has said
			// "hidden" since before the window was even mapped.
			if (!m_impl->cursorEnterSeen) {
				m_impl->cursorEnterSeen = true;
				LOG_INFO("LinuxWindow: pointer entered - re-asserting cursor "
				         "(cursorVisible=%d)", m_impl->cursorVisible ? 1 : 0);
			}
			m_impl->applyCursor();
			break;

		case FocusIn:
			m_impl->focused = true;
			// Logged for the first few only. A compositor that withholds focus
			// until the pointer moves would leave `want` false in
			// ForceCursorClipUpdate, which shows the cursor and takes no grab -
			// indistinguishable from the priming having failed, so the log has
			// to be able to tell the two apart.
			if (m_impl->focusLogCount < 4) {
				++m_impl->focusLogCount;
				LOG_INFO("LinuxWindow: FocusIn (clipEnabled=%d)",
				         m_impl->clipEnabled ? 1 : 0);
			}
			// Re-apply the confine, and the cursor visibility that goes with it.
			// Unconditional: when capture is OFF this is what puts the pointer
			// back on screen, which is just as much part of "focus regained".
			ForceCursorClipUpdate();
			RawInput_SetPaused(false);
			break;

		case FocusOut:
			m_impl->focused = false;
			if (m_impl->focusLogCount < 4) {
				++m_impl->focusLogCount;
				LOG_INFO("LinuxWindow: FocusOut (capture released)");
			}
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

	// Capture priming: warp the pointer into the window, then re-assert the
	// whole hide-and-confine for a few VIEWABLE frames.
	//
	// A first attempt at this re-sent only the cursor attribute, for five
	// frames counted from the end of Create(). It changed nothing on SteamOS,
	// for two separate reasons:
	//
	// 1. Those frames were spent while the window was still IsUnmapped - the
	//    very asynchrony documented at the grab retry above. A cursor attribute
	//    set then has no window on screen to apply to, so all five were wasted
	//    before anything was visible. The count is now gated on IsViewable.
	//
	// 2. Re-sending the attribute could never restore CAPTURE, only hiding.
	//    Capture is the grab. Priming now runs the whole
	//    ForceCursorClipUpdate, so hide and grab are re-tried together.
	//
	// The warp is the actual mechanism, and it is what the user was doing by
	// hand. The window selects neither EnterWindowMask nor PointerMotionMask
	// (see the event_mask in Create) - pointer motion reaches us only through
	// the grab we are trying to take. So under gamescope there was no pointer
	// event for the compositor to act on and no way for us to provoke one, and
	// everything stayed stuck until the mouse was physically moved.
	// XWarpPointer generates that event ourselves, and lands the pointer inside
	// our window, which is also the only place the cursor attribute applies.
	//
	// Warping is safe here: capture at startup is unconditional by design, so
	// the pointer is about to be confined to this window regardless, and games
	// read mouse deltas rather than absolute position.
	if (m_impl->cursorPrimeFrames > 0) {
		XWindowAttributes wa{};
		if (XGetWindowAttributes(m_impl->dpy, m_impl->win, &wa) &&
		    wa.map_state == IsViewable) {

			if (!m_impl->cursorWarped) {
				m_impl->cursorWarped = true;

				// If the compositor has not handed us focus by the time the
				// window is on screen, ask for it once. ForceCursorClipUpdate
				// gates BOTH hiding and the grab on `focused`, so an unfocused
				// window spends every priming frame deciding it does not want
				// the pointer - and the FocusOut logged during startup shows
				// this is not hypothetical. Safe here because we are inside the
				// IsViewable check; XSetInputFocus on an unviewable window is a
				// BadMatch.
				if (!m_impl->focused) {
					XSetInputFocus(m_impl->dpy, m_impl->win, RevertToParent, CurrentTime);
					LOG_INFO("LinuxWindow: unfocused at priming - requested input focus");
				}

				XWarpPointer(m_impl->dpy, None, m_impl->win,
				             0, 0, 0, 0, wa.width / 2, wa.height / 2);
				XFlush(m_impl->dpy);

				// And a real one-pixel nudge through XTEST. The warp above is
				// the native-X11 answer and is ignored under XWayland; this is
				// the same idea expressed as INPUT rather than as a position
				// change, which is the only version a Wayland compositor acts
				// on. Harmless where the warp already worked - the pointer
				// ends exactly where it started.
				m_impl->wigglePointer();
				LOG_INFO("LinuxWindow: primed capture - pointer warped to %d,%d "
				         "(clipEnabled=%d focused=%d)",
				         wa.width / 2, wa.height / 2,
				         m_impl->clipEnabled ? 1 : 0, m_impl->focused ? 1 : 0);
			}

			--m_impl->cursorPrimeFrames;
			ForceCursorClipUpdate();

			// And re-send the attribute UNCONDITIONALLY. ForceCursorClipUpdate
			// goes through SetCursorVisible, which early-outs when the state
			// already matches - and it always does by this point, because the
			// hide was recorded back when the window was still unmapped. That
			// guard silently swallowed every priming attempt in the previous
			// build: the log showed "cursorVisible=0 clipActive=1" while an
			// arrow was still on screen, because no XDefineCursor had been sent
			// since the window became viewable.
			m_impl->applyCursor();

			// Compositor still has not sent a pointer-enter with two frames of
			// priming left? Nudge again. Under XWayland the enter is the only
			// thing that makes a cursor change legal, and one nudge during the
			// window's first viewable frame can land before the compositor is
			// ready to act on it.
			if (m_impl->cursorPrimeFrames <= 2 && !m_impl->cursorEnterSeen)
				m_impl->wigglePointer();

			if (m_impl->cursorPrimeFrames == 0)
				LOG_INFO("LinuxWindow: priming done - cursorVisible=%d clipActive=%d "
				         "enterSeen=%d",
				         m_impl->cursorVisible ? 1 : 0, m_impl->clipActive ? 1 : 0,
				         m_impl->cursorEnterSeen ? 1 : 0);
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

	m_impl->cursorVisible = visible;
	m_impl->applyCursor();
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
