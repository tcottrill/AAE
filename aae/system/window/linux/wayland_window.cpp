//==============================================================================
// wayland_window.cpp -- see wayland_window.h.
//==============================================================================
#ifndef AAE_HAVE_WAYLAND
#error "wayland_window.cpp is only built when CMake finds the Wayland dependencies (AAE_HAVE_WAYLAND)"
#endif

// native_window FIRST. Its enums have an enumerator called None, and any header
// below that happens to reach <X11/X.h> would #define None to 0L underneath it.
// Nothing in this file uses Xlib, but include order is a cheaper guarantee than
// auditing every header for it.
#include <native_window/window.h>

#include "linux/wayland_window.h"

#include "sys_gl.h"
#include "sys_input.h"   // RawInput_SetPaused, on focus change
#include "sys_log.h"

// Vulkan surface support. VK_USE_PLATFORM_WAYLAND_KHR must be defined BEFORE
// vulkan.h to get VkWaylandSurfaceCreateInfoKHR; the wl_display/wl_surface it
// names are only forward-declared there, which is all we need because the real
// pointers come from native_window as void*. dlfcn for the runtime loader - AAE
// never links libvulkan (the same policy glx_present.cpp follows).
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#include <dlfcn.h>

#include <linux/input-event-codes.h>   // BTN_LEFT / BTN_RIGHT

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

// Declared here rather than included, for the same reason linux_window.cpp does
// it: opengl_renderer.h drags in GL and the whole renderer surface for one
// function.
void emulator_on_window_resize(int newW, int newH);

// Must match the Wayland app id to the installed .desktop file name: the
// compositor uses it to find the icon and group the taskbar entry. (Under X11
// the equivalent is WM_CLASS, which the launcher handles.)
static const char* kAppId = "io.github.tcottrill.AAE";

// Same wording as LinuxWindow::UpdateTitle() and winmain.cpp's
// UpdateWindowTitle(), so the three platforms read identically.
static const char* TitleFor(bool captured)
{
	return captured ? "AAE - Mouse Captured (F9 to release)" : "AAE";
}

//------------------------------------------------------------------------------
// All native_window state lives here so wayland_window.h stays free of its
// headers.
//------------------------------------------------------------------------------
struct WaylandWindow::Impl {
	std::unique_ptr<nw::Context> ctx;
	std::unique_ptr<nw::Window>  win;
	nw::WindowId                 winId = 0;

	// PIXELS (framebuffer), not logical units - see the note in the header.
	int   clientW  = 0;
	int   clientH  = 0;
	float dpiScale = 1.0f;

	// Last pointer position, LOGICAL, as the compositor reported it. GetMousePos
	// scales it to pixels on the way out.
	double ptrX = 0.0;
	double ptrY = 0.0;
	bool   pointerInside = false;

	bool clipEnabled = false;
	// Starts true, as the X11 backend does: a Wayland compositor is free to
	// withhold keyboard focus until the user clicks, and starting false would
	// leave the pointer visible and unconfined from the first frame.
	bool focused = true;

	// What the window is doing vs. what we last asked for. `fullscreen` is the
	// REQUESTED state and is what WindowSetup.borderlessFullscreen mirrors
	// (menu.cpp reads that); the compositor's answer arrives later in a
	// Configure. `fullscreenPending` holds off reconciling against Configure
	// events that were already in flight when the request went out - those still
	// say "windowed" and would otherwise flip the flag straight back.
	bool fullscreen        = false;
	bool fullscreenPending = false;
	std::chrono::steady_clock::time_point fullscreenAsked;

	// Cursor visibility as we last TOLD the library: -1 unknown, 0 hidden, 1
	// shown. A cache so that hide/show only costs a Wayland request on a real
	// transition. It is invalidated on focus changes, because the library
	// restores visibility itself when keyboard focus is lost.
	int cursorShown = -1;

	// Whether a Confined capture request is outstanding or active. The library's
	// constraints are ONE-SHOT: once the compositor revokes one (focus loss, the
	// pointer leaving, the user pressing a shell key) it is gone for good and the
	// next request has to be made by us - at the next focus gain, pointer enter
	// or click.
	bool captureRequested   = false;
	bool captureUnsupported = false;

	// Logging is bounded: these events fire for the life of the session and a
	// line per occurrence would drown the log in the first minute.
	int  focusLogCount     = 0;
	int  captureLogCount   = 0;
	int  configureLogCount = 0;
	bool deferLogged       = false;
	bool setMousePosLogged = false;

	void publishFullscreen(bool v)
	{
		fullscreen = v;
		// Not bookkeeping: menu.cpp reads borderlessFullscreen to DISPLAY the
		// FULLSCREEN item and to persist [window] fullscreen on save.
		GetWindowSetup().borderlessFullscreen = v;
	}
};

WaylandWindow::~WaylandWindow()
{
	Destroy();
}

bool WaylandWindow::Connect(std::string* error)
{
	if (m_impl && m_impl->ctx) return true;
	if (!m_impl) m_impl = new Impl();

	nw::ContextOptions opts;   // empty displayName: the library reads WAYLAND_DISPLAY
	nw::Error err;
	m_impl->ctx = nw::Context::create(opts, err,
		[](const nw::Error& e) {
			// Terminal connection errors are reported through here after the
			// dispatch that caused them has unwound.
			LOG_WARN("native_window: %s", e.message.c_str());
		});

	if (!m_impl->ctx) {
		// Naming WAYLAND_DISPLAY matters for the same reason naming DISPLAY does
		// in the X11 backend: a missing or wrong socket is by far the most common
		// cause, and "cannot connect" alone sends people looking at the GPU.
		const char* disp = getenv("WAYLAND_DISPLAY");
		std::string reason = err.message.empty() ? std::string("unknown error") : err.message;
		reason += " (WAYLAND_DISPLAY=";
		reason += (disp && *disp) ? disp : "<unset>";
		reason += ")";
		if (error) *error = reason;
		delete m_impl; m_impl = nullptr;
		return false;
	}

	const char* disp = getenv("WAYLAND_DISPLAY");
	LOG_INFO("WaylandWindow: connected to the compositor (WAYLAND_DISPLAY=%s)",
	         (disp && *disp) ? disp : "<unset>");
	return true;
}

bool WaylandWindow::QueryFirstOutput(int* pixelW, int* pixelH, int* scale,
                                     std::string* name) const
{
	if (!m_impl || !m_impl->ctx) return false;

	for (const nw::Output& o : m_impl->ctx->outputs()) {
		if (o.pixels.width <= 0 || o.pixels.height <= 0) continue;
		if (pixelW) *pixelW = o.pixels.width;
		if (pixelH) *pixelH = o.pixels.height;
		if (scale)  *scale  = o.scale > 0 ? o.scale : 1;
		if (name)   *name   = o.name;
		return true;
	}
	return false;
}

bool WaylandWindow::Create(const WindowSetup& setup)
{
	std::string why;
	if (!Connect(&why)) {
		LOG_ERROR("WaylandWindow: cannot connect: %s", why.c_str());
		return false;
	}
	Impl& s = *m_impl;

	// What the compositor offers decides what cursor capture can do, so it goes
	// in the log up front. Four builds of the X11 backend were spent assuming
	// the environment; this makes it a fact on the first line instead.
	const nw::Capabilities caps = s.ctx->capabilities();
	LOG_INFO("WaylandWindow: capabilities: serverDecorations=%d cursorShape=%d "
	         "pointerConstraints=%d relativePointer=%d fractionalScale=%d",
	         caps.serverDecorations ? 1 : 0, caps.cursorShape ? 1 : 0,
	         caps.pointerConstraints ? 1 : 0, caps.relativePointer ? 1 : 0,
	         caps.fractionalScale ? 1 : 0);
	if (!caps.pointerConstraints)
		LOG_WARN("WaylandWindow: compositor has no pointer-constraints protocol - "
		         "the cursor will be hidden but cannot be confined to the window");

	const std::vector<nw::Output> outs = s.ctx->outputs();
	if (outs.empty())
		LOG_INFO("WaylandWindow: compositor reported no outputs");
	for (const nw::Output& o : outs)
		LOG_INFO("WaylandWindow: output %s%s%s: %dx%d px, scale %d",
		         o.name.empty() ? "(unnamed)" : "'", o.name.c_str(),
		         o.name.empty() ? "" : "'", o.pixels.width, o.pixels.height, o.scale);

	nw::WindowOptions opts;
	opts.size.width  = setup.windowWidth  > 0 ? setup.windowWidth  : 1024;
	opts.size.height = setup.windowHeight > 0 ? setup.windowHeight : 768;
	opts.title       = TitleFor(setup.cursorClipEnabled);
	opts.appId       = kAppId;
	opts.decorated   = true;
	LOG_INFO("WaylandWindow: requesting %dx%d logical, app id %s",
	         opts.size.width, opts.size.height, opts.appId.c_str());

	nw::Error err;
	s.win = s.ctx->createWindow(opts, err);
	if (!s.win) {
		LOG_ERROR("WaylandWindow: createWindow failed: %s",
		          err.message.empty() ? "unknown error" : err.message.c_str());
		Destroy();
		return false;
	}
	s.winId = s.win->id();

	// Work-area fit. The default size above was a GUESS (output size minus a
	// margin, see SizeFromWayland) because a Wayland client cannot ask where the
	// panels are before it has a window. The initial configure may have carried
	// xdg_toplevel.configure_bounds, which IS the usable area for this window,
	// panels and decorations already excluded. The library has already shrunk
	// anything larger than it; for an AUTO size, also grow/shrink to the largest
	// window at the game's aspect that fits it - what GetCenteredAspectWindowSetup
	// does with the work area on Windows. An explicit screenw/screenh is left as
	// the library clamped it.
	{
		const nw::WindowState pre = s.win->state();
		const nw::Size b = pre.bounds;
		if (b.width > 0 && b.height > 0) {
			LOG_INFO("WaylandWindow: compositor bounds %dx%d logical (usable area)",
			         b.width, b.height);
			if (!setup.explicitWindowSize && setup.aspectRatio > 0.0f) {
				int fitH = b.height;
				int fitW = (int)lroundf(fitH * setup.aspectRatio);
				if (fitW > b.width) {
					fitW = b.width;
					fitH = (int)lroundf(fitW / setup.aspectRatio);
				}
				if (fitW >= 64 && fitH >= 64 && !pre.fullscreen && !pre.maximized &&
				    (fitW != pre.logicalSize.width || fitH != pre.logicalSize.height)) {
					const nw::RequestResult r = s.win->requestSize({fitW, fitH});
					LOG_INFO("WaylandWindow: default window refit to bounds: %dx%d -> %dx%d "
					         "logical (result=%d)", pre.logicalSize.width,
					         pre.logicalSize.height, fitW, fitH, static_cast<int>(r));
				}
			} else if (pre.logicalSize.width != opts.size.width ||
			           pre.logicalSize.height != opts.size.height) {
				LOG_WARN("WaylandWindow: requested %dx%d does not fit the usable area - "
				         "clamped to %dx%d logical", opts.size.width, opts.size.height,
				         pre.logicalSize.width, pre.logicalSize.height);
			}
		} else {
			LOG_INFO("WaylandWindow: compositor sent no configure_bounds - keeping the "
			         "margin-based default");
		}
	}

	// createWindow does not return until the first configure has been
	// acknowledged, so the state is already real. The renderer reads its
	// viewport and FBO size from WindowSetup, NOT from ISystemWindow (see the
	// note in LinuxWindow::Create) and wants PIXELS: the Vulkan extent and
	// glViewport are in framebuffer pixels, which differ from the logical size
	// whenever the scale is not 1.
	const nw::WindowState st = s.win->state();
	s.clientW  = st.framebufferSize.width;
	s.clientH  = st.framebufferSize.height;
	s.dpiScale = static_cast<float>(st.scale);
	GetWindowSetup().clientWidth  = s.clientW;
	GetWindowSetup().clientHeight = s.clientH;

	// Seed from WindowSetup like the other backends; linux_main.cpp calls
	// EnableCursorClip() once the renderer exists to actually apply it.
	s.clipEnabled = setup.cursorClipEnabled;

	// Hide the pointer now, before any frame exists. A Wayland client can only
	// change the pointer image in response to a wl_pointer.enter, and the library
	// applies THIS flag at that very instant - which is what the X11 backend's
	// whole stack of workarounds (blank window cursor, XFixes, root cursor, XTEST
	// nudges, priming frames) existed to approximate. The enter cannot be
	// delivered before the first PumpEvents, so setting the flag here is early
	// enough.
	SetCursorVisible(!(s.clipEnabled && s.focused));

	m_presentSurface.Attach(this);
	// The EGL context (OpenGL renderer only) is made on this window; Vulkan takes
	// the handles through Presentation() instead.
	sys_gl_set_wayland_target(s.win.get());

	LOG_INFO("WaylandWindow: created: logical %dx%d, framebuffer %dx%d, scale %.3f",
	         st.logicalSize.width, st.logicalSize.height,
	         st.framebufferSize.width, st.framebufferSize.height, st.scale);

	if (setup.useFullscreen || setup.borderlessFullscreen)
		ToggleBorderlessFullscreen();

	return true;
}

void WaylandWindow::Destroy()
{
	if (!m_impl) return;

	// The GL binding and the Vulkan surface are normally gone by now (linux_main
	// tears the renderer down first), but the EGL binding holds a pointer to this
	// window, so make sure of it rather than rely on the call order.
	if (m_impl->win) sys_gl_set_wayland_target(nullptr);

	// Window before context. Dropping the window also releases any pointer
	// capture and the cursor; there is no root cursor or XFixes state to hand
	// back, which is the other half of why this path is so much shorter.
	m_impl->win.reset();
	m_impl->ctx.reset();

	delete m_impl;
	m_impl = nullptr;
}

bool WaylandWindow::PumpEvents()
{
	if (!m_impl || !m_impl->ctx || !m_impl->win) return false;
	Impl& s = *m_impl;

	bool keepRunning = true;

	// Non-blocking: the frame loop is paced elsewhere. false means the connection
	// is gone for good; the ConnectionError event below carries the reason.
	if (!s.ctx->pump(std::chrono::milliseconds(0)))
		keepRunning = false;

	for (const nw::Event& e : s.ctx->takeEvents()) {
		// Events for other windows cannot happen with one window; ConnectionError
		// and queue overflow carry window 0, which must still get through.
		if (e.window && e.window != s.winId) continue;

		switch (e.type) {
		case nw::EventType::Configure:
		{
			const int newW = e.state.framebufferSize.width;
			const int newH = e.state.framebufferSize.height;
			// Configure also fires for scale and output changes that leave the
			// pixel size alone, so only react to an actual size change -
			// emulator_on_window_resize rebuilds the screen rect and is not free.
			const bool sizeChanged = (newW != s.clientW) || (newH != s.clientH);

			s.clientW  = newW;
			s.clientH  = newH;
			s.dpiScale = static_cast<float>(e.state.scale);
			// Keep WindowSetup in step - the renderer's viewport comes from there,
			// not from this class. See the note in Create().
			GetWindowSetup().clientWidth  = newW;
			GetWindowSetup().clientHeight = newH;

			if (s.configureLogCount == 0 || (sizeChanged && s.configureLogCount < 8)) {
				++s.configureLogCount;
				LOG_INFO("WaylandWindow: configure: logical %dx%d, framebuffer %dx%d, "
				         "scale %.3f, fullscreen=%d",
				         e.state.logicalSize.width, e.state.logicalSize.height,
				         newW, newH, e.state.scale, e.state.fullscreen ? 1 : 0);
			}

			// Reconcile fullscreen with what the compositor says. The user can
			// also toggle it from the compositor (window menu, a shell shortcut)
			// without us asking, and a request can be refused.
			if (s.fullscreenPending) {
				if (e.state.fullscreen == s.fullscreen) s.fullscreenPending = false;
			} else if (e.state.fullscreen != s.fullscreen) {
				LOG_INFO("WaylandWindow: compositor changed fullscreen to %s",
				         e.state.fullscreen ? "on" : "off");
				s.publishFullscreen(e.state.fullscreen);
			}

			// THE resize hook. On X11 a ConfigureNotify is followed by whatever the
			// renderer needs; on Wayland a resize need NOT make a Vulkan swapchain
			// report OUT_OF_DATE, so without this call the swapchain keeps its old
			// extent. emulator_on_window_resize -> vkchain_on_window_resize ->
			// VK_RecreateSwapchain is what rebuilds it, and it is also what
			// recomputes the vector path's screen_rect and re-centres the image.
			//
			// Safe before the renderer exists: it returns immediately while
			// screen_rect is null.
			if (sizeChanged && newW > 0 && newH > 0)
				emulator_on_window_resize(newW, newH);
			break;
		}

		case nw::EventType::Close:
			LOG_INFO("WaylandWindow: close requested - quitting");
			keepRunning = false;
			break;

		case nw::EventType::ConnectionError:
			LOG_ERROR("WaylandWindow: connection lost: %s",
			          e.error.message.empty() ? "unknown error" : e.error.message.c_str());
			keepRunning = false;
			break;

		case nw::EventType::KeyboardFocus:
			s.focused = e.focused;
			if (s.focusLogCount < 8) {
				++s.focusLogCount;
				LOG_INFO("WaylandWindow: keyboard focus %s (clipEnabled=%d)",
				         e.focused ? "gained" : "lost", s.clipEnabled ? 1 : 0);
			}
			// Pause input, as winmain.cpp does on WM_ACTIVATEAPP and the X11
			// backend does on FocusOut.
			//
			// This matters MORE here than it does on Windows. evdev is a global
			// tap: it keeps delivering events from the physical devices no matter
			// which window the desktop considers focused, so without this an
			// unfocused AAE carries on eating every keystroke meant for another
			// application. Pausing also clears the key state, which is what stops
			// a key held at the moment of the switch from being stuck down on
			// return.
			RawInput_SetPaused(!e.focused);
			// The library restores cursor visibility itself when keyboard focus is
			// lost (a held-input reset), so the cached state can no longer be
			// trusted - forget it and re-assert. Routed through
			// ForceCursorClipUpdate, which also re-requests the confine that the
			// same reset revoked.
			s.cursorShown = -1;
			ForceCursorClipUpdate();
			break;

		case nw::EventType::PointerFocus:
			s.pointerInside = e.focused;
			if (e.focused) {
				s.ptrX = e.x;
				s.ptrY = e.y;
				// A confine can only be requested for a pointer that is INSIDE the
				// surface; a request made earlier (at startup, or after a revoke
				// caused by the pointer leaving) was deferred until now.
				ForceCursorClipUpdate();
			}
			break;

		case nw::EventType::PointerMotion:
			s.ptrX = e.x;
			s.ptrY = e.y;
			break;

		case nw::EventType::PointerButton:
			// Clicking back into the game view re-arms capture, mirroring
			// WM_LBUTTONDOWN/WM_RBUTTONDOWN in winmain.cpp and ButtonPress in the
			// X11 backend. Left and right only. Not game input - that comes from
			// evdev.
			if (e.pressed && (e.code == BTN_LEFT || e.code == BTN_RIGHT)) {
				if (!s.clipEnabled)
					EnableCursorClip(true);
				else if (!s.captureRequested)
					ForceCursorClipUpdate();   // re-arm a constraint the compositor revoked
			}
			break;

		case nw::EventType::CaptureChanged:
		{
			if (s.captureLogCount < 8) {
				++s.captureLogCount;
				const char* st = "inactive";
				switch (e.capture) {
				case nw::CaptureState::Pending: st = "pending (requested, not yet granted)"; break;
				case nw::CaptureState::Active:  st = "active (pointer confined)"; break;
				case nw::CaptureState::Revoked: st = "revoked by the compositor"; break;
				default: break;
				}
				LOG_INFO("WaylandWindow: pointer capture %s", st);
			}
			// Only a REVOKE is involuntary. Everything else follows a request we
			// made ourselves and has already updated captureRequested.
			if (e.capture == nw::CaptureState::Revoked)
				s.captureRequested = false;
			break;
		}

		case nw::EventType::InputReset:
			// Not input - evdev owns that - but the library has just restored
			// cursor visibility and dropped any capture as part of the reset, so
			// the cached state is stale. Forget it and re-assert immediately: on
			// a queue overflow the library clears the queue, emits the capture
			// revoke and THEN this reset, so by the time it arrives
			// captureRequested is already false and nothing else would re-confine
			// the pointer until the next focus change or click.
			s.cursorShown = -1;
			ForceCursorClipUpdate();
			break;

		// Deliberately ignored: Key, Text, Modifiers, PointerAxis,
		// RelativeMotion. Input stays on evdev; taking it from Wayland as well
		// would fire every action twice. Activation, OutputsChanged, Preedit,
		// DeleteSurroundingText, Clipboard*, Drop are features AAE does not use.
		default:
			break;
		}
	}

	// A fullscreen request the compositor never answered (refused outright, or
	// the answer matched the old state) would leave the published flag wrong
	// forever, because Configure events are what reconcile it. After a couple of
	// seconds stop waiting and believe the window.
	if (s.fullscreenPending &&
	    std::chrono::steady_clock::now() - s.fullscreenAsked > std::chrono::seconds(2)) {
		s.fullscreenPending = false;
		const bool actual = s.win->state().fullscreen;
		if (actual != s.fullscreen) {
			LOG_WARN("WaylandWindow: fullscreen %s was not honoured by the compositor",
			         s.fullscreen ? "on" : "off");
			s.publishFullscreen(actual);
		}
	}

	return keepRunning;
}

int   WaylandWindow::ClientWidth()  const { return m_impl ? m_impl->clientW  : 0; }
int   WaylandWindow::ClientHeight() const { return m_impl ? m_impl->clientH  : 0; }
float WaylandWindow::DpiScale()     const { return m_impl ? m_impl->dpiScale : 1.0f; }

void WaylandWindow::ToggleBorderlessFullscreen()
{
	if (!m_impl || !m_impl->win) return;
	Impl& s = *m_impl;

	const bool next = !s.fullscreen;
	const nw::RequestResult r = s.win->requestFullscreen(next, 0);
	if (r != nw::RequestResult::Accepted) {
		LOG_WARN("WaylandWindow: fullscreen %s request not accepted (result=%d)",
		         next ? "on" : "off", static_cast<int>(r));
		return;
	}

	// Publish on REQUEST, as the Win32 and X11 versions do - menu.cpp reads
	// borderlessFullscreen immediately after the toggle. The compositor's answer
	// reconciles it in PumpEvents.
	s.publishFullscreen(next);
	s.fullscreenPending = true;
	s.fullscreenAsked   = std::chrono::steady_clock::now();

	LOG_INFO("WaylandWindow: fullscreen %s", next ? "on" : "off");
}

void WaylandWindow::RestoreViewport()
{
	if (m_impl && m_impl->fullscreen)
		ToggleBorderlessFullscreen();
}

void WaylandWindow::SetCursorVisible(bool visible)
{
	if (!m_impl || !m_impl->win) return;

	const int v = visible ? 1 : 0;
	if (v == m_impl->cursorShown) return;

	m_impl->cursorShown = v;
	// The library remembers this on the window and applies it the moment the
	// pointer enters, so it is correct even when called before any enter has
	// happened - which is the whole difference from the X11 path.
	m_impl->win->setCursor(nw::CursorShape::Arrow, visible);
}

void WaylandWindow::UpdateTitle()
{
	if (!m_impl || !m_impl->win) return;
	m_impl->win->setTitle(TitleFor(m_impl->clipEnabled));
}

void WaylandWindow::EnableCursorClip(bool enable)
{
	if (!m_impl) return;
	m_impl->clipEnabled = enable;

	// Keep the shared WindowSetup in step, as the other backends do. This is NOT
	// bookkeeping for its own sake: msg_loop()'s F9 handler reads
	// cursorClipEnabled to decide which way to toggle, so without this the flag
	// would never change and F9 would only ever capture, never release.
	GetWindowSetup().cursorClipEnabled = enable;

	UpdateTitle();
	ForceCursorClipUpdate();
}

void WaylandWindow::ForceCursorClipUpdate()
{
	if (!m_impl || !m_impl->win) return;
	Impl& s = *m_impl;

	// Same condition winmain.cpp's UpdateCursorState() and the X11 backend use
	// for BOTH halves: hide and confine together, show and release together.
	const bool want = s.clipEnabled && s.focused;

	// Visibility first, and independently of the confine, as in the X11 backend:
	// it must not inherit the confine's failure modes. SetCursorVisible early-outs
	// when already in the requested state, so calling this repeatedly costs
	// nothing.
	SetCursorVisible(!want);

	// Requests only on TRANSITIONS. The library's constraints are one-shot, so a
	// per-frame re-request (the X11 grab-retry pattern) would churn Wayland
	// objects for nothing; the events that matter - focus, pointer enter, a click
	// - each call back into here.
	if (want && !s.captureRequested && !s.captureUnsupported) {
		const nw::RequestResult r = s.win->requestPointerCapture(nw::CaptureMode::Confined);
		if (r == nw::RequestResult::Accepted) {
			s.captureRequested = true;
			s.deferLogged = false;
			if (s.captureLogCount < 8)
				LOG_INFO("WaylandWindow: confine requested (the compositor grants it asynchronously)");
		} else if (r == nw::RequestResult::Unsupported) {
			s.captureUnsupported = true;
			LOG_WARN("WaylandWindow: confine request unsupported - the cursor is hidden "
			         "but not confined");
		} else if (!s.deferLogged) {
			// Invalid/Failed: the pointer is not over the window (yet), so there is
			// nothing to confine. Logged once per deferral; PointerFocus retries.
			s.deferLogged = true;
			LOG_INFO("WaylandWindow: confine deferred (result=%d) - pointer is not over "
			         "the window; retrying when it enters", static_cast<int>(r));
		}
	} else if (!want && s.captureRequested) {
		s.win->requestPointerCapture(nw::CaptureMode::None);
		s.captureRequested = false;
	}
}

void WaylandWindow::SetMousePos(int x, int y)
{
	(void)x; (void)y;
	// Wayland gives clients no way to move the pointer, and the library makes no
	// claim to. Nothing outside the backends calls this, so a no-op is honest;
	// logged once so a future caller finds out why it does nothing.
	if (m_impl && !m_impl->setMousePosLogged) {
		m_impl->setMousePosLogged = true;
		LOG_INFO("WaylandWindow: SetMousePos is not supported on Wayland - ignored");
	}
}

void WaylandWindow::GetMousePos(int* x, int* y) const
{
	if (x) *x = 0;
	if (y) *y = 0;
	if (!m_impl || !m_impl->win) return;

	// The compositor reports LOGICAL coordinates; every other backend returns
	// window-relative PIXELS, which is what callers compare against clientWidth.
	const double scale = m_impl->win->state().scale;
	if (x) *x = static_cast<int>(std::lround(m_impl->ptrX * scale));
	if (y) *y = static_cast<int>(std::lround(m_impl->ptrY * scale));
}

void WaylandWindow::FramebufferSize(int* w, int* h) const
{
	if (w) *w = 0;
	if (h) *h = 0;
	if (!m_impl || !m_impl->win) return;

	const nw::Size fb = m_impl->win->state().framebufferSize;
	if (w) *w = fb.width;
	if (h) *h = fb.height;
}

void* WaylandWindow::NativeDisplay() const
{
	return (m_impl && m_impl->win) ? m_impl->win->nativeDisplay() : nullptr;
}

void* WaylandWindow::NativeSurface() const
{
	return (m_impl && m_impl->win) ? m_impl->win->nativeSurface() : nullptr;
}

//==============================================================================
// WaylandPresentSurface
//==============================================================================

void WaylandPresentSurface::SwapBuffers()
{
	// Diagnostic, as in GlxPresentSurface: a window that never presents looks the
	// same as a window that was never created.
	static int s_swapCount = 0;
	if (s_swapCount < 3) {
		++s_swapCount;
		LOG_INFO("WaylandPresentSurface::SwapBuffers #%d", s_swapCount);
	}

	// Routed through sys_gl.cpp rather than touching EGL here: that file owns the
	// binding, as it owns the GLX context, and two places that think they own the
	// swap is how you end up swapping a surface the context is not bound to.
	glchain_swap_buffers();
}

void WaylandPresentSurface::GetDrawableSize(int* w, int* h) const
{
	if (w) *w = 0;
	if (h) *h = 0;
	if (m_owner) m_owner->FramebufferSize(w, h);
}

//------------------------------------------------------------------------------
// Vulkan: the Wayland twin of GlxPresentSurface's Xlib implementation.
//
// The window core already applies the buffer scale / viewport to the surface, so
// the swapchain only has to be the right size. sys_vk.cpp takes its extent from
// GetDrawableSize() whenever the surface reports currentExtent == 0xFFFFFFFF -
// which is what Wayland WSI does, since the surface has no size of its own - and
// WaylandWindow::PumpEvents calls emulator_on_window_resize on every pixel-size
// change to rebuild it. Nothing else is needed here.
//------------------------------------------------------------------------------
const char* const* WaylandPresentSurface::RequiredVkInstanceExtensions(uint32_t* count) const
{
	static const char* kExtensions[] = { "VK_KHR_surface", "VK_KHR_wayland_surface" };
	if (count) *count = 2;
	return kExtensions;
}

bool WaylandPresentSurface::CreateVkSurface(void* instance, void* outSurface)
{
	void* display = m_owner ? m_owner->NativeDisplay() : nullptr;
	void* surface = m_owner ? m_owner->NativeSurface() : nullptr;
	if (!instance || !outSurface || !display || !surface)
	{
		LOG_ERROR("WaylandPresentSurface::CreateVkSurface: bad args or no window");
		return false;
	}

	// Runtime load, no link - same policy and same two names as the X11 side.
	void* loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
	if (!loader)
		loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
	if (!loader)
	{
		LOG_ERROR("WaylandPresentSurface::CreateVkSurface: libvulkan.so.1 not found (%s)",
		          dlerror() ? dlerror() : "no error text");
		return false;
	}

	PFN_vkGetInstanceProcAddr gipa =
		(PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
	PFN_vkCreateWaylandSurfaceKHR createWaylandSurface = gipa
		? (PFN_vkCreateWaylandSurfaceKHR)gipa((VkInstance)instance, "vkCreateWaylandSurfaceKHR")
		: nullptr;

	bool ok = false;
	if (createWaylandSurface)
	{
		// Both handles are BORROWED from native_window and valid until the window
		// is destroyed - which linux_main.cpp does only after Vulkan is torn down.
		VkWaylandSurfaceCreateInfoKHR sci{ VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR };
		sci.display = static_cast<struct wl_display*>(display);
		sci.surface = static_cast<struct wl_surface*>(surface);

		VkResult r = createWaylandSurface((VkInstance)instance, &sci, nullptr,
		                                  (VkSurfaceKHR*)outSurface);
		ok = (r == VK_SUCCESS);
		if (!ok)
			LOG_ERROR("WaylandPresentSurface::CreateVkSurface: vkCreateWaylandSurfaceKHR failed (VkResult=%d)", (int)r);
	}
	else
	{
		LOG_ERROR("WaylandPresentSurface::CreateVkSurface: vkCreateWaylandSurfaceKHR not available "
		          "(driver missing VK_KHR_wayland_surface?)");
	}

	// Refcounted: sys_vk.cpp holds its own handle on the loader for the session.
	dlclose(loader);
	return ok;
}
