# Reusing the native window backend

This directory is the single shared implementation. It is independently
buildable and contains no player or emulator dependency. Keep consumers pointed
at this source (or a pinned release of it); do not copy window.cpp into each app.
UltraHLE and AAE have not been modified by the player integration.

## Build and ownership

Add the directory with CMake `add_subdirectory`, then link `native_window::core`.
For OpenGL also include this directory's `egl.cmake` and link
`native_window::egl`. A consumer supplies its own OpenGL loader/rendering code;
the EGL binding exposes `getProcAddress` without requiring GLEW or GLX.

Create one `nw::Context` and one or more `nw::Window` instances on the owner
thread. For each OpenGL window, create `nw::EglBinding` with the renderer's
required version and profile. The player requests `{3,3,true}`; an emulator may
request a compatibility profile. Unsupported driver requirements return errors
rather than silently downgrading.

Pump the context, consume events by window/seat ID, call that window's EGL
`makeCurrent` before rendering, and then `present`. This also resizes the drawable
before drawing. Destroy each binding before its window and all windows before
the context. Multiple EGL bindings share display initialization safely; a window
cannot have two simultaneous EGL bindings. Methods are owner-thread-only in this
version. Worker threads must send commands through the application's existing
queue to that thread; no shared cross-thread command API is implemented yet.

Window geometry and input positions are logical content coordinates. Renderers
use `state().framebufferSize`. Do not multiply scale twice: the player adapter
converts input/UI to pixel coordinates because its existing UI already scales
fonts and widgets. Another UI may instead stay in logical coordinates and apply
one framebuffer transform.

## AAE adapter mapping

| Existing operation | Shared implementation |
| --- | --- |
| Create / Destroy / PumpEvents | Context/window lifetime, pump and event dispatch |
| ClientWidth / ClientHeight / DpiScale | Logical content size and reported scale |
| IPresentSurface::GetDrawableSize | Framebuffer size |
| SwapBuffers | EGL present, with makeCurrent before rendering |
| ToggleBorderlessFullscreen | Request fullscreen; observe configure state |
| SetCursorVisible | setCursor with explicit visibility |
| EnableCursorClip | Request confined capture and observe pending/active/revoked events |
| Relative mouse mode | Locked capture plus RelativeMotion events |
| SetMousePos / desktop coordinates | Unsupported on this API; adapt call sites to relative input or application-local coordinates |
| RestoreViewport | Renderer operation using current framebuffer size |

Keep WindowSetup globals and emulator callbacks in AAE's adapter, not this
library. Preserve `EvdevKeyToAae` for physical key mapping: shared Key.code uses
evdev-style codes, with no XKB +8 offset. Key.symbol is a semantic XKB symbol;
Text is UTF-8. Treat synchronized focus-enter keys as held-state synchronization,
not fresh actions. Reset held state on InputReset and focus loss.

Keep raw evdev/gamepad/device identity handling separate and optional. Select one
keyboard/mouse source for each logical input path so raw events and Wayland
events do not double-fire. Before the AAE backport, correct evdev SYN_DROPPED
recovery and audit device removal/focus gating. The player needs no raw-device
permissions.

## UltraHLE adapter mapping

Translate Key, Text, focus and pointer events into the existing UI/action and
controller interfaces. Replace X11 key-symbol includes in that adapter with
xkbcommon key symbols or explicit action mapping; do not add Xlib to the shared
core. Emulator hotkeys belong in the adapter. Its graphics layer requests the
OpenGL profile it actually needs. Existing Pulse audio remains a separate module.

## Desktop and future rendering constraints

Window placement belongs to the compositor. Saved output names are hints for
fullscreen requests, not a guarantee of ordinary window placement or Monitor 1.
Use reported capabilities and observed state rather than assuming a request was
granted. Client decorations, cursor scaling and protocol negotiation stay here.

The native display/surface handles are borrowed and form the later Vulkan
integration boundary. Vulkan instance/surface/swapchain ownership will belong
to that renderer; no Vulkan implementation is included yet. Do not force Vulkan
presentation into EGL's present operation.

The core exposes text-input-v3 composition events; the current player adapter
uses XKB compose and reports that IME preedit UI is not implemented. Backports
must either implement preedit/delete/surrounding-text behavior or keep IME mode
disabled. `stb_truetype` handles decoration fonts. If application XML parsing is
added, reuse AAE's TinyXML2; protocol XML is currently build-time scanner input.

Unit tests are headless. Compositor behavior, mixed-DPI output moves, capture,
multiple visible windows, and renderer presentation need the user's UI testing
on the actual KDE/GNOME/SteamOS target.
