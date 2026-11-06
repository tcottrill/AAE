# native_window

A standalone C++17 Wayland window/input library. `native_window::core` depends on
Wayland client headers 1.22+, Wayland cursor and xkbcommon. It has no player, emulator, toolkit, ImGui,
OpenGL, EGL, SDL, GLFW, Xlib or GLX dependency. The separate `native_window::egl`
target supplies graphics binding. Source compatibility is the initial contract;
this is not a stable binary ABI.

Build independently on Linux:

```sh
cmake -S shared/native_window -B build/native-window -DBUILD_TESTING=ON
cmake --build build/native-window
ctest --test-dir build/native-window --output-on-failure
```

The build compiles a renderer-free consumer in `examples/consumer.cpp`; tests
never launch it or create a desktop/GPU window. Tests cover scale rounding and
fallback, output-scale preference, URI/UTF-8 validation, event ordering and
coalescing, held-input reset, overflow notification, terminal errors, missing
display, silent-server discovery timeout, abrupt disconnect, transfer deadlines
and byte limits, receiver exit/SIGPIPE, and decoration rasterization. These are
headless checks, not evidence of compositor UI acceptance.

`Context::create` discovers mandatory globals with a bounded deadline. It requires
wl_compositor v4+, xdg-shell, wl_shm and wl_subcompositor. Creating a Window commits
an empty surface, waits for the first configure, and acknowledges it before
returning. The graphics binding can then attach its own buffers. Subsequent
configure callbacks acknowledge before publishing coherent logical/framebuffer
state. A zero suggested dimension preserves the selected content dimension;
windowed dimensions are retained across fullscreen/maximize transitions.

All methods and destruction belong to one owner thread. Destroy graphics bindings,
then windows, then context. Cross-thread command queues/wakeup are not exposed in
this initial API. `pump` uses prepare-read/flush/poll/read-or-cancel, handles write
backpressure and interrupted polls, and clamps each wait to 1000 milliseconds.
Disconnect is terminal and emits one ConnectionError. The optional diagnostic
logger is invoked after dispatch unwinds. No clipboard text, input text or file
paths are logged. Optional protocols are reported by `capabilities`; an Accepted
request means locally submitted, not a compositor grant.

Each window retains separate state and entered outputs; seats have independent
focus, held keys/buttons, modifiers, XKB layout/compose, repeat and capture state.
Physical keys are evdev codes, symbols are XKB keysyms, and text is UTF-8. Focus
enter keys are marked `synchronized` so adapters seed held state without firing
shortcuts. Focus loss/capability removal/seat removal resets held input and revokes
capture. Queue overflow clears the queue and emits an explicit InputReset; only
adjacent pointer motion/configure events coalesce across no intervening event.

Fractional-scale plus viewporter is used when available; otherwise preferred
surface integer scale wins, then the greatest entered-output scale, then 1.
Framebuffer extents use ceil(logical * scale). Content pointer coordinates stay
logical and exclude client decoration extents. Axis dx/dy retain Wayland units;
wheelX/Y retain normalized wheel steps, including value120 fractions. Relative
motion timestamps are converted from protocol microseconds to wrapping
milliseconds; capture remains Pending until the compositor activates it. No
pointer warping/global positioning/primary-monitor claim is made.

Server decoration is requested; otherwise four shared-memory subsurfaces draw
title/borders and close, maximize/restore, minimize controls. Valid pointer-button
serials drive move/resize. Decoration allocation is bounded; busy buffers are
retried after release. Rasterization uses the existing stb_truetype copy, with
its original notices preserved, and trusted system fonts. No FreeType/libpng
application dependency is introduced. There is no runtime XML parser. Protocol
bindings are generated at build time from the vendored Wayland-protocols 1.47 XML
snapshot (each file retains its copyright/license); no new XML library is used.

Cursor visibility is window-owned and survives adapter shape updates. Cursor
shape protocol is preferred; themed, correctly scaled cursor surfaces are the
fallback. Focus reset restores visibility. Capture uses one-shot constraints;
fresh application action is needed after revocation. Idle inhibition is optional.
Text-input-v3 exposes committed/preedit/delete events and caret/surrounding text
requests using UTF-8 byte offsets; XKB compose is suppressed while IME is enabled.
Adapters that lack preedit must report that limitation.

Clipboard UTF-8 text and local `file:` URI lists are asynchronous, bounded by
transfer count, bytes and deadline. Selection changes/new requests cancel old
clipboard reads. Clipboard ownership requires an input serial and does not
promise persistence after exit. URI validation rejects remote hosts, bad percent
encoding, embedded NUL, non-file schemes and query/fragment components; a drop
retains all validated local paths as byte strings. Transfers never execute paths.

Not included: raw evdev/gamepad routing, presentation clock conversion, a renderer,
Vulkan binding, file dialogs, desktop placement, a cross-thread event API or a
virtual-compositor acceptance harness. Optional protocol behavior, multi-seat
policy in each adapter, mixed DPI, IME, clipboard/drop interop and decorations
still require user UI acceptance on the target compositors.
