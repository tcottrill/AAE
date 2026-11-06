#include "internal.h"
#include <algorithm>
#include <cerrno>
#include <clocale>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

namespace nw::detail {
static ssize_t pipeWrite(int fd, const void *bytes, std::size_t size) {
  // Clipboard receivers can disappear at any time. Do not change the host
  // application's SIGPIPE disposition or let a closed transfer kill it.
  sigset_t blocked, previous, pending;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &blocked, &previous);
  sigpending(&pending);
  bool wasPending = sigismember(&pending, SIGPIPE);
  ssize_t count = write(fd, bytes, size);
  int saved = errno;
  if (count < 0 && saved == EPIPE && !wasPending) {
    timespec zero{};
    while (sigtimedwait(&blocked, nullptr, &zero) < 0 && errno == EINTR) {
    }
  }
  pthread_sigmask(SIG_SETMASK, &previous, nullptr);
  errno = saved;
  return count;
}
static Event event(EventType type, NativeWindow *w = nullptr,
                   Seat *s = nullptr) {
  Event e;
  e.type = type;
  if (w)
    e.window = w->id;
  if (s) {
    e.seat = s->id;
    e.modifiers = s->modifiers;
  }
  return e;
}
static void destroySource(Seat &s) {
  if (s.source) {
    wl_data_source_destroy(s.source);
    s.source = nullptr;
  }
}
Transfer::~Transfer() {
  if (fd >= 0)
    close(fd);
}
void Core::emit(Event e) {
  // Only adjacent replaceable events coalesce; never reorder around keys/text.
  if (!events.empty() &&
      (e.type == EventType::PointerMotion || e.type == EventType::Configure)) {
    auto &last = events.back();
    if (last.type == e.type && last.window == e.window && last.seat == e.seat) {
      last = std::move(e);
      return;
    }
  }
  if (events.size() >= std::max<std::size_t>(32, options.maxQueuedEvents)) {
    // Overflow explicitly invalidates ALL delivered held state, instead of
    // silently dropping a release. Connection remains usable after draining.
    events.clear();
    for (auto &item : seats) {
      auto &s = *item.second;
      s.held.reset();
      s.repeatKey = 0;
      s.releaseCapture(true);
    }
    Event reset;
    reset.type = EventType::InputReset;
    reset.error = {ErrorCode::System,
                   "Event queue overflow; all input state reset"};
    events.push_back(std::move(reset));
  }
  events.push_back(std::move(e));
}
void Core::fail(ErrorCode code, const std::string &message) {
  if (!alive)
    return;
  alive = false;
  terminal = {code, message};
  for (auto &entry : seats)
    entry.second->reset();
  Event e;
  e.type = EventType::ConnectionError;
  e.error = terminal;
  emit(std::move(e));
  // Logger is intentionally not called inside protocol callbacks. create/pump
  // invoke it after dispatch has unwound.
}
NativeWindow *Core::find(wl_surface *surface, int *part) {
  for (auto &pair : windows) {
    auto *w = pair.second;
    if (w->surface == surface) {
      if (part)
        *part = -1;
      return w;
    }
    int p = w->decorationPart(surface);
    if (p >= 0) {
      if (part)
        *part = p;
      return w;
    }
  }
  return nullptr;
}
Seat *Core::selectSeat(SeatId id, NativeWindow *w) {
  if (id) {
    auto i = seats.find(id);
    return i == seats.end() ? nullptr : i->second.get();
  }
  for (auto &p : seats)
    if (p.second->keyWindow == w)
      return p.second.get();
  for (auto &p : seats)
    if (p.second->pointerWindow == w)
      return p.second.get();
  return seats.empty() ? nullptr : seats.begin()->second.get();
}
void NativeWindow::updateScale() {
  unsigned scale = scale120;
  if (!fractional) {
    std::vector<int> entered;
    for (auto id : outputs) {
      auto p = core->outputs.find(id);
      if (p != core->outputs.end())
        entered.push_back(p->second->value.scale);
    }
    scale = unsigned(integerScale(preferredScale, entered)) * 120;
  }
  auto old = state.framebufferSize;
  auto oldOutputs = state.enteredOutputs;
  double previous = state.scale;
  state.scale = double(scale) / 120.;
  state.framebufferSize = {pixelExtent(state.logicalSize.width, scale),
                           pixelExtent(state.logicalSize.height, scale)};
  state.enteredOutputs.assign(outputs.begin(), outputs.end());
  if (fractional && viewport) {
    wl_surface_set_buffer_scale(surface, 1);
    wp_viewport_set_destination(viewport, state.logicalSize.width,
                                state.logicalSize.height);
  } else
    wl_surface_set_buffer_scale(surface, int(scale / 120));
  if (old.width != state.framebufferSize.width ||
      old.height != state.framebufferSize.height || previous != state.scale ||
      oldOutputs != state.enteredOutputs) {
    auto e = event(EventType::Configure, this);
    e.state = state;
    core->emit(std::move(e));
    drawDecorations();
  }
  for (auto &item : core->seats)
    if (item.second->pointerWindow == this)
      item.second->applyCursor();
}
void NativeWindow::configured() {
  bool wasActive = state.activated;
  if (!state.fullscreen && !state.maximized &&
      (pendingFullscreen || pendingMaximized))
    windowedSize = state.logicalSize;
  if ((state.fullscreen || state.maximized) && !pendingFullscreen &&
      !pendingMaximized)
    state.logicalSize = windowedSize;
  int decorW = useClientDecoration && decorated && !pendingFullscreen ? 8 : 0;
  int decorH = useClientDecoration && decorated && !pendingFullscreen ? 34 : 0;
  state.logicalSize = {
      configuredExtent(suggestedW > decorW ? suggestedW - decorW : 0,
                       state.logicalSize.width),
      configuredExtent(suggestedH > decorH ? suggestedH - decorH : 0,
                       state.logicalSize.height)};
  state.fullscreen = pendingFullscreen;
  state.maximized = pendingMaximized;
  state.activated = pendingActivated;
  state.resizing = pendingResizing;
  state.bounds = {boundsW > decorW ? boundsW - decorW : 0,
                  boundsH > decorH ? boundsH - decorH : 0};
  // Only a size WE chose is clamped: a nonzero configure width/height is the
  // compositor's decision (interactive resize, tiling) and is taken as given.
  if (!state.fullscreen && !state.maximized && suggestedW <= 0 &&
      suggestedH <= 0)
    fitToBounds();
  state.configured = true;
  if (useClientDecoration && decorated && !state.fullscreen)
    xdg_surface_set_window_geometry(shell, -4, -30, state.logicalSize.width + 8,
                                    state.logicalSize.height + 34);
  else
    xdg_surface_set_window_geometry(shell, 0, 0, state.logicalSize.width,
                                    state.logicalSize.height);
  updateScale();
  drawDecorations();
  auto e = event(EventType::Configure, this);
  e.state = state;
  core->emit(std::move(e));
  if (wasActive != state.activated) {
    auto a = event(EventType::Activation, this);
    a.focused = state.activated;
    core->emit(std::move(a));
  }
}
// xdg_toplevel.configure_bounds: a windowed client choosing its own size should
// not exceed it, or the window ends up under a panel or partly off-screen (a
// Wayland client cannot reposition itself to fix that afterwards). Shrinks
// state.logicalSize to state.bounds, keeping its aspect ratio, never below the
// 64x64 minimum set at creation. No-op when no bounds were sent.
void NativeWindow::fitToBounds() {
  const int bw = state.bounds.width, bh = state.bounds.height;
  int w = state.logicalSize.width, h = state.logicalSize.height;
  if (bw <= 0 || bh <= 0 || w <= 0 || h <= 0 || (w <= bw && h <= bh))
    return;
  const double k = std::min(double(bw) / w, double(bh) / h);
  w = std::max(64, std::min(bw, int(std::floor(w * k))));
  h = std::max(64, std::min(bh, int(std::floor(h * k))));
  state.logicalSize = {w, h};
}
static void keyboardFocusLost(Seat &s) {
  auto focus = event(EventType::KeyboardFocus, s.keyWindow, &s);
  s.reset();
  s.keyWindow = nullptr;
  if (focus.window) {
    focus.focused = false;
    s.core->emit(std::move(focus));
  }
}
static void pointerFocusLost(Seat &s) {
  // Pointer focus is independent from keyboard focus. In particular, window
  // destruction/capability loss must not reset another window's held keys.
  auto buttons = std::move(s.held.buttons);
  s.held.buttons.clear();
  for (auto button : buttons) {
    auto release = event(EventType::PointerButton, s.pointerWindow, &s);
    release.code = button;
    release.pressed = false;
    s.core->emit(std::move(release));
  }
  s.releaseCapture(true);
  if (s.pointerWindow && s.pointerPart < 0) {
    auto focus = event(EventType::PointerFocus, s.pointerWindow, &s);
    focus.focused = false;
    s.core->emit(std::move(focus));
  }
  s.pointerWindow = nullptr;
  s.enterSerial = 0;
  s.pointerPart = -1;
  s.axis = {};
}
NativeWindow::~NativeWindow() {
  if (!core)
    return;
  for (auto &p : core->seats) {
    auto &s = *p.second;
    if (s.keyWindow == this) {
      keyboardFocusLost(s);
    }
    if (s.pointerWindow == this) {
      pointerFocusLost(s);
    }
    if (s.textWindow == this) {
      s.textWindow = nullptr;
      s.imeActive = false;
    }
    if (s.dragWindow == this)
      s.dragWindow = nullptr;
  }
  core->transfers.erase(
      std::remove_if(core->transfers.begin(), core->transfers.end(),
                     [this](const auto &t) { return t->window == id; }),
      core->transfers.end());
  core->windows.erase(id);
  for (auto &d : decorations)
    d.reset();
  if (inhibitor)
    zwp_idle_inhibitor_v1_destroy(inhibitor);
  if (serverDecoration)
    zxdg_toplevel_decoration_v1_destroy(serverDecoration);
  if (fractional)
    wp_fractional_scale_v1_destroy(fractional);
  if (viewport)
    wp_viewport_destroy(viewport);
  if (top)
    xdg_toplevel_destroy(top);
  if (shell)
    xdg_surface_destroy(shell);
  if (surface)
    wl_surface_destroy(surface);
}
void initWindow(NativeWindow &w) {
  static const wl_surface_listener surface = {
      [](void *p, wl_surface *, wl_output *output) {
        auto &w = *static_cast<NativeWindow *>(p);
        for (auto &entry : w.core->outputs)
          if (entry.second->proxy == output)
            w.outputs.insert(entry.first);
        w.updateScale();
      },
      [](void *p, wl_surface *, wl_output *output) {
        auto &w = *static_cast<NativeWindow *>(p);
        for (auto &entry : w.core->outputs)
          if (entry.second->proxy == output)
            w.outputs.erase(entry.first);
        w.updateScale();
      },
      [](void *p, wl_surface *, int32_t scale) {
        auto &w = *static_cast<NativeWindow *>(p);
        w.preferredScale = std::max(1, scale);
        w.updateScale();
      },
      [](void *, wl_surface *, uint32_t) {}};
  wl_surface_add_listener(w.surface, &surface, &w);
  static const xdg_surface_listener shell = {
      [](void *p, xdg_surface *surface, uint32_t serial) {
        auto &w = *static_cast<NativeWindow *>(p);
        xdg_surface_ack_configure(surface, serial);
        w.configured();
      }};
  xdg_surface_add_listener(w.shell, &shell, &w);
  static const xdg_toplevel_listener top = {
      [](void *p, xdg_toplevel *, int32_t width, int32_t height,
         wl_array *states) {
        auto &w = *static_cast<NativeWindow *>(p);
        w.suggestedW = width;
        w.suggestedH = height;
        w.pendingActivated = w.pendingFullscreen = w.pendingMaximized =
            w.pendingResizing = false;
        auto *a = static_cast<uint32_t *>(states->data);
        for (std::size_t i = 0; i < states->size / sizeof(uint32_t); ++i) {
          if (a[i] == XDG_TOPLEVEL_STATE_FULLSCREEN)
            w.pendingFullscreen = true;
          if (a[i] == XDG_TOPLEVEL_STATE_MAXIMIZED)
            w.pendingMaximized = true;
          if (a[i] == XDG_TOPLEVEL_STATE_ACTIVATED)
            w.pendingActivated = true;
          if (a[i] == XDG_TOPLEVEL_STATE_RESIZING)
            w.pendingResizing = true;
        }
      },
      [](void *p, xdg_toplevel *) {
        auto &w = *static_cast<NativeWindow *>(p);
        w.state.closed = true;
        w.core->emit(event(EventType::Close, &w));
      },
      [](void *p, xdg_toplevel *, int32_t width, int32_t height) {
        // configure_bounds (v4+). Sent before configure in the same sequence,
        // so configured() sees it; 0x0 means "unknown" per the protocol.
        auto &w = *static_cast<NativeWindow *>(p);
        w.boundsW = std::max(0, width);
        w.boundsH = std::max(0, height);
      },
      [](void *, xdg_toplevel *, wl_array *) {}};
  xdg_toplevel_add_listener(w.top, &top, &w);
  if (w.core->viewporter)
    w.viewport = wp_viewporter_get_viewport(w.core->viewporter, w.surface);
  if (w.core->fractional && w.viewport) {
    w.fractional = wp_fractional_scale_manager_v1_get_fractional_scale(
        w.core->fractional, w.surface);
    static const wp_fractional_scale_v1_listener listener = {
        [](void *p, wp_fractional_scale_v1 *, uint32_t scale) {
          auto &w = *static_cast<NativeWindow *>(p);
          if (scale) {
            w.scale120 = scale;
            w.updateScale();
          }
        }};
    wp_fractional_scale_v1_add_listener(w.fractional, &listener, &w);
  }
  if (w.core->decoration && w.decorated) {
    w.serverDecoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
        w.core->decoration, w.top);
    static const zxdg_toplevel_decoration_v1_listener listener = {
        [](void *p, zxdg_toplevel_decoration_v1 *, uint32_t mode) {
          auto &w = *static_cast<NativeWindow *>(p);
          w.useClientDecoration =
              mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
          w.drawDecorations();
        }};
    zxdg_toplevel_decoration_v1_add_listener(w.serverDecoration, &listener, &w);
    zxdg_toplevel_decoration_v1_set_mode(
        w.serverDecoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  }
}
void Seat::releaseCapture(bool revoked) {
  bool existed = locked || confined || relative;
  if (locked) {
    zwp_locked_pointer_v1_destroy(locked);
    locked = nullptr;
  }
  if (confined) {
    zwp_confined_pointer_v1_destroy(confined);
    confined = nullptr;
  }
  if (relative) {
    zwp_relative_pointer_v1_destroy(relative);
    relative = nullptr;
  }
  if (existed) {
    captureState = revoked ? CaptureState::Revoked : CaptureState::Inactive;
    auto e = event(EventType::CaptureChanged, captureWindow, this);
    e.capture = captureState;
    core->emit(std::move(e));
  }
  captureWindow = nullptr;
}
void Seat::reset() {
  auto heldBefore = held.reset();
  serial = 0;
  repeatKey = 0;
  modifiers = 0;
  if (compose)
    xkb_compose_state_reset(compose);
  if (keyWindow) {
    keyWindow->cursorVisible = true;
    for (auto &p : core->seats)
      if (p.second->pointerWindow == keyWindow)
        p.second->applyCursor();
  }
  for (unsigned key : heldBefore.first) {
    auto e = event(EventType::Key, keyWindow, this);
    e.code = key;
    e.pressed = false;
    core->emit(std::move(e));
  }
  for (unsigned button : heldBefore.second) {
    auto e = event(EventType::PointerButton, pointerWindow, this);
    e.code = button;
    e.pressed = false;
    core->emit(std::move(e));
  }
  core->emit(event(EventType::InputReset, keyWindow, this));
  releaseCapture(true);
}
Seat::~Seat() {
  releaseCapture(false);
  destroySource(*this);
  offers.clear();
  selection.reset();
  drag.reset();
  if (textInput)
    zwp_text_input_v3_destroy(textInput);
  if (dataDevice)
    wl_data_device_release(dataDevice);
  if (shapeDevice)
    wp_cursor_shape_device_v1_destroy(shapeDevice);
  if (cursorTheme)
    wl_cursor_theme_destroy(cursorTheme);
  if (cursorSurface)
    wl_surface_destroy(cursorSurface);
  if (compose)
    xkb_compose_state_unref(compose);
  if (xkb)
    xkb_state_unref(xkb);
  if (keymap)
    xkb_keymap_unref(keymap);
  if (keyboard)
    wl_keyboard_release(keyboard);
  if (pointer)
    wl_pointer_release(pointer);
  if (proxy)
    wl_seat_release(proxy);
}
void Seat::text(unsigned key, uint32_t time, bool repeated) {
  if (!xkb || !keyWindow || imeActive)
    return;
  auto sym = xkb_state_key_get_one_sym(xkb, key + 8);
  std::string text;
  if (compose) {
    xkb_compose_state_feed(compose, sym);
    auto status = xkb_compose_state_get_status(compose);
    if (status == XKB_COMPOSE_COMPOSING)
      return;
    if (status == XKB_COMPOSE_CANCELLED) {
      xkb_compose_state_reset(compose);
      return;
    }
    if (status == XKB_COMPOSE_COMPOSED) {
      int size = xkb_compose_state_get_utf8(compose, nullptr, 0);
      if (size > 0) {
        text.resize(std::size_t(size) + 1);
        xkb_compose_state_get_utf8(compose, text.data(), text.size());
        text.resize(size);
      }
      xkb_compose_state_reset(compose);
    }
  }
  if (text.empty()) {
    int size = xkb_state_key_get_utf8(xkb, key + 8, nullptr, 0);
    if (size > 0) {
      text.resize(std::size_t(size) + 1);
      xkb_state_key_get_utf8(xkb, key + 8, text.data(), text.size());
      text.resize(size);
    }
  }
  if (!text.empty() && validUtf8(text) &&
      static_cast<unsigned char>(text.front()) >= 32) {
    auto e = event(EventType::Text, keyWindow, this);
    e.text = std::move(text);
    e.timeMs = time;
    e.repeat = repeated;
    core->emit(std::move(e));
  }
}
void Seat::applyCursor() {
  if (!pointer || !pointerWindow || !enterSerial)
    return;
  auto &w = *pointerWindow;
  if (!w.cursorVisible && pointerPart < 0) {
    wl_pointer_set_cursor(pointer, enterSerial, nullptr, 0, 0);
    return;
  }
  CursorShape shape = w.cursor;
  if (pointerPart == 1 || pointerPart == 2)
    shape = CursorShape::ResizeHorizontal;
  if (pointerPart == 3)
    shape = CursorShape::ResizeVertical;
  if (pointerPart == 0)
    shape = CursorShape::Arrow;
  static const uint32_t shapes[] = {
      WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT,
      WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT,
      WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
      WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR,
      WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE,
      WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE,
      WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE,
      WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE,
      WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT,
      WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED};
  if (shapeDevice) {
    wp_cursor_shape_device_v1_set_shape(shapeDevice, enterSerial,
                                        shapes[int(shape)]);
    return;
  }
  int scale = std::max(1, int(std::ceil(w.state.scale)));
  if (cursorScale != scale || !cursorTheme) {
    if (cursorTheme)
      wl_cursor_theme_destroy(cursorTheme);
    cursorTheme = wl_cursor_theme_load(nullptr, 24 * scale, core->shm);
    cursorScale = scale;
  }
  if (!cursorTheme)
    return;
  static const char *names[] = {"left_ptr",
                                "xterm",
                                "hand2",
                                "crosshair",
                                "sb_h_double_arrow",
                                "sb_v_double_arrow",
                                "size_fdiag",
                                "size_bdiag",
                                "watch",
                                "not-allowed"};
  auto *cursor = wl_cursor_theme_get_cursor(cursorTheme, names[int(shape)]);
  if (!cursor)
    cursor = wl_cursor_theme_get_cursor(cursorTheme, "left_ptr");
  if (!cursor || !cursor->image_count)
    return;
  if (!cursorSurface)
    cursorSurface = wl_compositor_create_surface(core->compositor);
  auto *image = cursor->images[0];
  auto *buffer = wl_cursor_image_get_buffer(image);
  wl_surface_set_buffer_scale(cursorSurface, scale);
  wl_pointer_set_cursor(pointer, enterSerial, cursorSurface,
                        int(image->hotspot_x) / scale,
                        int(image->hotspot_y) / scale);
  wl_surface_attach(cursorSurface, buffer, 0, 0);
  wl_surface_damage_buffer(cursorSurface, 0, 0, INT_MAX, INT_MAX);
  wl_surface_commit(cursorSurface);
}
static void emitAxis(Seat &s) {
  if ((s.axis.dx || s.axis.dy || s.axis.wheelX || s.axis.wheelY) &&
      s.pointerWindow && s.pointerPart < 0) {
    s.axis.type = EventType::PointerAxis;
    s.axis.window = s.pointerWindow->id;
    s.axis.seat = s.id;
    s.core->emit(s.axis);
  }
  s.axis = {};
}
static void decorationButton(Seat &s, uint32_t serial) {
  auto *w = s.pointerWindow;
  if (!w || s.pointerPart < 0)
    return;
  if (s.pointerPart == 0) {
    int width = w->state.logicalSize.width + 8;
    if (s.y < 5) {
      xdg_toplevel_resize(w->top, s.proxy, serial,
                          XDG_TOPLEVEL_RESIZE_EDGE_TOP);
      return;
    }
    if (s.x > width - 30) {
      w->core->emit(event(EventType::Close, w));
      return;
    }
    if (s.x > width - 60) {
      if (w->state.maximized)
        xdg_toplevel_unset_maximized(w->top);
      else
        xdg_toplevel_set_maximized(w->top);
      return;
    }
    if (s.x > width - 90) {
      xdg_toplevel_set_minimized(w->top);
      return;
    }
    xdg_toplevel_move(w->top, s.proxy, serial);
  } else {
    uint32_t edge = s.pointerPart == 1   ? XDG_TOPLEVEL_RESIZE_EDGE_LEFT
                    : s.pointerPart == 2 ? XDG_TOPLEVEL_RESIZE_EDGE_RIGHT
                                         : XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    xdg_toplevel_resize(w->top, s.proxy, serial, edge);
  }
}
void initSeat(Seat &s) {
  static const wl_keyboard_listener keyboard = {
      [](void *p, wl_keyboard *, uint32_t format, int32_t fd, uint32_t size) {
        auto &s = *static_cast<Seat *>(p);
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0 ||
            size > 16 * 1024 * 1024) {
          close(fd);
          return;
        }
        void *map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (map == MAP_FAILED)
          return;
        xkb_keymap *keymap = nullptr;
        if (static_cast<char *>(map)[size - 1] == 0)
          keymap = xkb_keymap_new_from_string(
              s.core->xkb, static_cast<char *>(map), XKB_KEYMAP_FORMAT_TEXT_V1,
              XKB_KEYMAP_COMPILE_NO_FLAGS);
        munmap(map, size);
        if (!keymap)
          return;
        auto *state = xkb_state_new(keymap);
        if (!state) {
          xkb_keymap_unref(keymap);
          return;
        }
        s.reset();
        if (s.xkb)
          xkb_state_unref(s.xkb);
        if (s.keymap)
          xkb_keymap_unref(s.keymap);
        s.keymap = keymap;
        s.xkb = state;
      },
      [](void *p, wl_keyboard *, uint32_t, wl_surface *surface,
         wl_array *keys) {
        auto &s = *static_cast<Seat *>(p);
        s.keyWindow = s.core->find(surface);
        if (!s.keyWindow)
          return;
        auto e = event(EventType::KeyboardFocus, s.keyWindow, &s);
        e.focused = true;
        s.core->emit(e);
        auto *values = static_cast<uint32_t *>(keys->data);
        for (std::size_t i = 0; i < keys->size / sizeof(uint32_t); ++i) {
          s.held.keys.insert(values[i]);
          e = event(EventType::Key, s.keyWindow, &s);
          e.code = values[i];
          e.pressed = true;
          e.synchronized = true;
          if (s.xkb)
            e.symbol = xkb_state_key_get_one_sym(s.xkb, values[i] + 8);
          s.core->emit(e);
        }
      },
      [](void *p, wl_keyboard *, uint32_t, wl_surface *) {
        auto &s = *static_cast<Seat *>(p);
        keyboardFocusLost(s);
      },
      [](void *p, wl_keyboard *, uint32_t serial, uint32_t time, uint32_t key,
         uint32_t state) {
        auto &s = *static_cast<Seat *>(p);
        s.serial = serial;
        if (!s.keyWindow)
          return;
        bool down = state == WL_KEYBOARD_KEY_STATE_PRESSED;
        if (down)
          s.held.keys.insert(key);
        else
          s.held.keys.erase(key);
        auto e = event(EventType::Key, s.keyWindow, &s);
        e.code = key;
        e.timeMs = time;
        e.pressed = down;
        if (s.xkb)
          e.symbol = xkb_state_key_get_one_sym(s.xkb, key + 8);
        s.core->emit(e);
        if (down) {
          s.text(key, time, false);
          if (s.keymap && s.repeatRate > 0 &&
              xkb_keymap_key_repeats(s.keymap, key + 8)) {
            s.repeatKey = key;
            s.repeatTime = time;
            s.repeatOrigin = Clock::now();
            s.repeatAt =
                s.repeatOrigin + std::chrono::milliseconds(s.repeatDelay);
          }
        } else if (s.repeatKey == key)
          s.repeatKey = 0;
      },
      [](void *p, wl_keyboard *, uint32_t, uint32_t depressed, uint32_t latched,
         uint32_t locked, uint32_t group) {
        auto &s = *static_cast<Seat *>(p);
        if (!s.xkb)
          return;
        xkb_state_update_mask(s.xkb, depressed, latched, locked, 0, 0, group);
        s.modifiers = 0;
        const char *names[] = {XKB_MOD_NAME_SHIFT, XKB_MOD_NAME_CTRL,
                               XKB_MOD_NAME_ALT,   XKB_MOD_NAME_LOGO,
                               XKB_MOD_NAME_CAPS,  XKB_MOD_NAME_NUM};
        for (unsigned i = 0; i < 6; ++i)
          if (xkb_state_mod_name_is_active(s.xkb, names[i],
                                           XKB_STATE_MODS_EFFECTIVE) > 0)
            s.modifiers |= 1u << i;
        s.core->emit(event(EventType::Modifiers, s.keyWindow, &s));
      },
      [](void *p, wl_keyboard *, int32_t rate, int32_t delay) {
        auto &s = *static_cast<Seat *>(p);
        s.repeatRate = std::clamp(rate, 0, 1000);
        s.repeatDelay = std::clamp(delay, 0, 10000);
        if (!rate)
          s.repeatKey = 0;
      }};
  static const wl_pointer_listener pointer = {
      [](void *p, wl_pointer *, uint32_t serial, wl_surface *surface,
         wl_fixed_t x, wl_fixed_t y) {
        auto &s = *static_cast<Seat *>(p);
        s.enterSerial = serial;
        s.pointerWindow = s.core->find(surface, &s.pointerPart);
        s.x = wl_fixed_to_double(x);
        s.y = wl_fixed_to_double(y);
        if (s.pointerWindow && s.pointerPart < 0) {
          auto e = event(EventType::PointerFocus, s.pointerWindow, &s);
          e.focused = true;
          e.x = s.x;
          e.y = s.y;
          s.core->emit(e);
        }
        s.applyCursor();
      },
      [](void *p, wl_pointer *, uint32_t, wl_surface *) {
        auto &s = *static_cast<Seat *>(p);
        pointerFocusLost(s);
      },
      [](void *p, wl_pointer *, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
        auto &s = *static_cast<Seat *>(p);
        s.x = wl_fixed_to_double(x);
        s.y = wl_fixed_to_double(y);
        if (s.pointerWindow && s.pointerPart < 0) {
          auto e = event(EventType::PointerMotion, s.pointerWindow, &s);
          e.timeMs = time;
          e.x = s.x;
          e.y = s.y;
          s.core->emit(e);
        }
      },
      [](void *p, wl_pointer *, uint32_t serial, uint32_t time, uint32_t button,
         uint32_t state) {
        auto &s = *static_cast<Seat *>(p);
        s.serial = serial;
        if (!s.pointerWindow)
          return;
        bool down = state == WL_POINTER_BUTTON_STATE_PRESSED;
        if (s.pointerPart >= 0) {
          if (down && button == 272)
            decorationButton(s, serial);
          return;
        }
        if (down)
          s.held.buttons.insert(button);
        else
          s.held.buttons.erase(button);
        auto e = event(EventType::PointerButton, s.pointerWindow, &s);
        e.code = button;
        e.pressed = down;
        e.timeMs = time;
        e.x = s.x;
        e.y = s.y;
        s.core->emit(e);
      },
      [](void *p, wl_pointer *pointer, uint32_t time, uint32_t axis,
         wl_fixed_t value) {
        auto &s = *static_cast<Seat *>(p);
        s.axis.timeMs = time;
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
          s.axis.dy += wl_fixed_to_double(value);
        else
          s.axis.dx += wl_fixed_to_double(value);
        if (wl_pointer_get_version(pointer) < 5)
          emitAxis(s);
      },
      [](void *p, wl_pointer *) { emitAxis(*static_cast<Seat *>(p)); },
      [](void *p, wl_pointer *, uint32_t source) {
        static_cast<Seat *>(p)->axis.axisSource = source;
      },
      [](void *, wl_pointer *, uint32_t, uint32_t) {},
      [](void *p, wl_pointer *, uint32_t axis, int32_t discrete) {
        auto &s = *static_cast<Seat *>(p);
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
          s.axis.discreteY += discrete;
          s.axis.wheelY += discrete;
        } else {
          s.axis.discreteX += discrete;
          s.axis.wheelX += discrete;
        }
      },
      [](void *p, wl_pointer *, uint32_t axis, int32_t value) {
        auto &s = *static_cast<Seat *>(p);
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
          s.axis.wheelY += double(value) / 120;
        else
          s.axis.wheelX += double(value) / 120;
      },
      [](void *, wl_pointer *, uint32_t, uint32_t) {}};
  static const wl_seat_listener listener = {
      [](void *p, wl_seat *seat, uint32_t caps) {
        auto &s = *static_cast<Seat *>(p);
        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !s.keyboard) {
          s.keyboard = wl_seat_get_keyboard(seat);
          wl_keyboard_add_listener(s.keyboard, &keyboard, &s);
        } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && s.keyboard) {
          keyboardFocusLost(s);
          wl_keyboard_release(s.keyboard);
          s.keyboard = nullptr;
        }
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && !s.pointer) {
          s.pointer = wl_seat_get_pointer(seat);
          wl_pointer_add_listener(s.pointer, &pointer, &s);
          if (s.core->cursorShape)
            s.shapeDevice = wp_cursor_shape_manager_v1_get_pointer(
                s.core->cursorShape, s.pointer);
        } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && s.pointer) {
          pointerFocusLost(s);
          if (s.shapeDevice) {
            wp_cursor_shape_device_v1_destroy(s.shapeDevice);
            s.shapeDevice = nullptr;
          }
          wl_pointer_release(s.pointer);
          s.pointer = nullptr;
        }
      },
      [](void *, wl_seat *, const char *) {}};
  wl_seat_add_listener(s.proxy, &listener, &s);
  if (s.core->compose)
    s.compose =
        xkb_compose_state_new(s.core->compose, XKB_COMPOSE_STATE_NO_FLAGS);
}
void Core::setupSeat(Seat &s) {
  if (cursorShape && s.pointer && !s.shapeDevice)
    s.shapeDevice =
        wp_cursor_shape_manager_v1_get_pointer(cursorShape, s.pointer);
  if (dataManager && !s.dataDevice) {
    s.dataDevice = wl_data_device_manager_get_data_device(dataManager, s.proxy);
    static const wl_data_device_listener listener = {
        [](void *p, wl_data_device *, wl_data_offer *proxy) {
          auto &s = *static_cast<Seat *>(p);
          if (s.offers.size() >= 128) {
            wl_data_offer_destroy(proxy);
            s.core->fail(ErrorCode::Protocol, "Too many unclaimed data offers");
            return;
          }
          auto offer = std::make_shared<Offer>();
          offer->proxy = proxy;
          s.offers[proxy] = offer;
          static const wl_data_offer_listener listener = {
              [](void *p, wl_data_offer *, const char *mime) {
                auto *offer = static_cast<Offer *>(p);
                if (mime && offer->mimes.size() < 128)
                  offer->mimes.insert(mime);
              },
              [](void *, wl_data_offer *, uint32_t) {},
              [](void *, wl_data_offer *, uint32_t) {}};
          wl_data_offer_add_listener(proxy, &listener, offer.get());
        },
        [](void *p, wl_data_device *, uint32_t serial, wl_surface *surface,
           wl_fixed_t, wl_fixed_t, wl_data_offer *proxy) {
          auto &s = *static_cast<Seat *>(p);
          s.dragWindow = s.core->find(surface);
          auto it = s.offers.find(proxy);
          s.drag = it == s.offers.end() ? nullptr : it->second;
          if (it != s.offers.end())
            s.offers.erase(it);
          bool accepted =
              s.drag && s.dragWindow && s.drag->mimes.count("text/uri-list");
          if (proxy) {
            wl_data_offer_accept(proxy, serial,
                                 accepted ? "text/uri-list" : nullptr);
            if (wl_data_offer_get_version(proxy) >= 3)
              wl_data_offer_set_actions(
                  proxy, accepted ? WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY : 0,
                  accepted ? WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY : 0);
          }
        },
        [](void *p, wl_data_device *) {
          auto &s = *static_cast<Seat *>(p);
          s.drag.reset();
          s.dragWindow = nullptr;
        },
        [](void *, wl_data_device *, uint32_t, wl_fixed_t, wl_fixed_t) {},
        [](void *p, wl_data_device *) {
          auto &s = *static_cast<Seat *>(p);
          if (s.drag && s.dragWindow)
            s.core->startRead(s, s.dragWindow, s.drag, true);
          s.drag.reset();
          s.dragWindow = nullptr;
        },
        [](void *p, wl_data_device *, wl_data_offer *proxy) {
          auto &s = *static_cast<Seat *>(p);
          for (auto &t : s.core->transfers)
            if (!t->writing && !t->drop && t->seat == s.id) {
              if (t->fd >= 0)
                close(t->fd);
              t->fd = -1;
            }
          auto it = s.offers.find(proxy);
          s.selection = it == s.offers.end() ? nullptr : it->second;
          if (it != s.offers.end())
            s.offers.erase(it);
          auto e = event(EventType::ClipboardChanged, s.keyWindow, &s);
          s.core->emit(e);
        }};
    wl_data_device_add_listener(s.dataDevice, &listener, &s);
  }
  if (textInput && !s.textInput) {
    s.textInput = zwp_text_input_manager_v3_get_text_input(textInput, s.proxy);
    static const zwp_text_input_v3_listener listener = {
        [](void *p, zwp_text_input_v3 *input, wl_surface *surface) {
          auto &s = *static_cast<Seat *>(p);
          s.textWindow = s.core->find(surface);
          s.imeEnabled = s.textWindow && s.textWindow->textInputEnabled;
          if (s.imeEnabled) {
            auto &w = *s.textWindow;
            zwp_text_input_v3_enable(input);
            zwp_text_input_v3_set_surrounding_text(input, w.surrounding.c_str(),
                                                   w.textCursor, w.textAnchor);
            zwp_text_input_v3_commit(input);
            ++s.textCommitSerial;
            s.imeActive = true;
          }
        },
        [](void *p, zwp_text_input_v3 *, wl_surface *) {
          auto &s = *static_cast<Seat *>(p);
          if (s.textWindow) {
            auto e = event(EventType::Preedit, s.textWindow, &s);
            s.core->emit(e);
          }
          s.imeActive = false;
          s.textWindow = nullptr;
          s.gotCommit = s.gotPreedit = s.gotDelete = false;
        },
        [](void *p, zwp_text_input_v3 *, const char *text, int32_t begin,
           int32_t end) {
          auto &s = *static_cast<Seat *>(p);
          s.preedit = text ? text : "";
          s.preeditBegin = begin;
          s.preeditEnd = end;
          s.gotPreedit = true;
        },
        [](void *p, zwp_text_input_v3 *, const char *text) {
          auto &s = *static_cast<Seat *>(p);
          s.commit = text ? text : "";
          s.gotCommit = true;
        },
        [](void *p, zwp_text_input_v3 *, uint32_t before, uint32_t after) {
          auto &s = *static_cast<Seat *>(p);
          s.deleteBefore = before;
          s.deleteAfter = after;
          s.gotDelete = true;
        },
        [](void *p, zwp_text_input_v3 *, uint32_t serial) {
          auto &s = *static_cast<Seat *>(p);
          if (serial == s.textCommitSerial && s.textWindow && s.imeActive) {
            if (s.gotDelete) {
              auto e =
                  event(EventType::DeleteSurroundingText, s.textWindow, &s);
              e.beforeBytes = s.deleteBefore;
              e.afterBytes = s.deleteAfter;
              s.core->emit(e);
            }
            if (s.gotCommit && validUtf8(s.commit)) {
              auto e = event(EventType::Text, s.textWindow, &s);
              e.text = std::move(s.commit);
              s.core->emit(std::move(e));
            }
            if (s.gotPreedit && validUtf8(s.preedit)) {
              auto e = event(EventType::Preedit, s.textWindow, &s);
              e.text = std::move(s.preedit);
              e.cursorBegin = s.preeditBegin;
              e.cursorEnd = s.preeditEnd;
              s.core->emit(std::move(e));
            }
          }
          s.gotCommit = s.gotPreedit = s.gotDelete = false;
        }};
    zwp_text_input_v3_add_listener(s.textInput, &listener, &s);
  }
}
void Core::startRead(Seat &seat, NativeWindow *window,
                     std::shared_ptr<Offer> offer, bool drop) {
  if (!drop)
    for (auto &t : transfers)
      if (!t->writing && !t->drop && t->seat == seat.id) {
        if (t->fd >= 0)
          close(t->fd);
        t->fd = -1;
      }
  Event failure =
      event(drop ? EventType::Drop : EventType::Clipboard, window, &seat);
  if (!offer) {
    emit(std::move(failure));
    return;
  }
  const char *mime = nullptr;
  if (drop) {
    if (offer->mimes.count("text/uri-list"))
      mime = "text/uri-list";
  } else
    for (const char *candidate :
         {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain"})
      if (offer->mimes.count(candidate)) {
        mime = candidate;
        break;
      }
  if (!mime) {
    failure.error = {ErrorCode::Unsupported,
                     "Offer has no supported MIME type"};
    emit(std::move(failure));
    return;
  }
  if (transfers.size() >= 16) {
    failure.error = {ErrorCode::System, "Too many pending transfers"};
    emit(std::move(failure));
    return;
  }
  int fds[2];
  if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) < 0) {
    failure.error = {ErrorCode::System, "Cannot create transfer pipe"};
    emit(std::move(failure));
    return;
  }
  // The sender owns its FD and may use blocking writes; only our read end is
  // nonblocking.
  fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) & ~O_NONBLOCK);
  wl_data_offer_receive(offer->proxy, mime, fds[1]);
  close(fds[1]);
  auto t = std::make_unique<Transfer>();
  t->fd = fds[0];
  t->window = window ? window->id : 0;
  t->seat = seat.id;
  t->drop = drop;
  t->offer = std::move(offer);
  t->deadline = Clock::now() + options.transferTimeout;
  transfers.push_back(std::move(t));
}
void Core::transferReady(Transfer &t, short revents) {
  if (t.fd < 0)
    return;
  auto finish = [&](Error error) {
    if (!t.writing) {
      Event e;
      e.type = t.drop ? EventType::Drop : EventType::Clipboard;
      e.window = t.window;
      e.seat = t.seat;
      e.error = std::move(error);
      if (!e.error) {
        if (t.drop) {
          if (!parseUriList(t.bytes, e.paths))
            e.error = {ErrorCode::Invalid, "Invalid local file URI list"};
        } else if (validUtf8(t.bytes))
          e.text = std::move(t.bytes);
        else
          e.error = {ErrorCode::Invalid, "Clipboard is not valid UTF-8"};
      }
      emit(std::move(e));
      if (t.drop && t.offer && wl_data_offer_get_version(t.offer->proxy) >= 3 &&
          !error)
        wl_data_offer_finish(t.offer->proxy);
    }
    close(t.fd);
    t.fd = -1;
  };
  if (Clock::now() >= t.deadline) {
    finish({ErrorCode::Timeout, "Transfer deadline expired"});
    return;
  }
  if (t.writing) {
    if (revents & (POLLOUT | POLLHUP | POLLERR)) {
      while (t.offset < t.bytes.size()) {
        ssize_t n = pipeWrite(t.fd, t.bytes.data() + t.offset,
                              t.bytes.size() - t.offset);
        if (n > 0)
          t.offset += std::size_t(n);
        else if (n < 0 && errno == EINTR)
          continue;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
          return;
        else {
          finish({ErrorCode::System, "Clipboard write failed"});
          return;
        }
      }
      finish({});
    }
    return;
  }
  if (!(revents & (POLLIN | POLLHUP | POLLERR)))
    return;
  char buffer[16384];
  for (;;) {
    ssize_t n = read(t.fd, buffer, sizeof(buffer));
    if (n > 0) {
      if (t.bytes.size() + std::size_t(n) > options.maxTransferBytes) {
        finish({ErrorCode::Invalid, "Transfer exceeded byte limit"});
        return;
      }
      t.bytes.append(buffer, std::size_t(n));
    } else if (n == 0) {
      finish({});
      return;
    } else if (errno == EINTR)
      continue;
    else if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;
    else {
      finish({ErrorCode::System, "Transfer read failed"});
      return;
    }
  }
}
void Core::ticks() {
  for (auto &w : windows)
    if (w.second->decorationDirty)
      w.second->drawDecorations();
  auto now = Clock::now();
  for (auto &p : seats) {
    auto &s = *p.second;
    if (s.repeatKey && s.keyWindow && s.repeatRate > 0 && now >= s.repeatAt) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - s.repeatOrigin)
                         .count();
      auto e = event(EventType::Key, s.keyWindow, &s);
      e.code = s.repeatKey;
      e.pressed = e.repeat = true;
      e.timeMs = s.repeatTime + uint32_t(elapsed);
      if (s.xkb)
        e.symbol = xkb_state_key_get_one_sym(s.xkb, s.repeatKey + 8);
      emit(e);
      s.text(s.repeatKey, e.timeMs, true);
      s.repeatAt = now + std::chrono::microseconds(1000000 / s.repeatRate);
    }
  }
  for (auto &t : transfers)
    if (t->fd >= 0 && now >= t->deadline)
      transferReady(*t, 0);
  transfers.erase(std::remove_if(transfers.begin(), transfers.end(),
                                 [](const auto &t) { return t->fd < 0; }),
                  transfers.end());
}
bool Core::pump(int timeout) {
  if (!alive)
    return false;
  ticks();
  auto deadline = Clock::now() + std::chrono::milliseconds(clampWait(timeout));
  while (wl_display_prepare_read(display) != 0) {
    if (wl_display_dispatch_pending(display) < 0) {
      fail(ErrorCode::Connection, "Wayland pending dispatch failed");
      return false;
    }
  }
  bool needWrite = false;
  if (wl_display_flush(display) < 0) {
    if (errno == EAGAIN)
      needWrite = true;
    else {
      wl_display_cancel_read(display);
      fail(ErrorCode::Connection, "Wayland flush failed");
      return false;
    }
  }
  std::vector<pollfd> fds;
  fds.push_back({wl_display_get_fd(display),
                 short(POLLIN | (needWrite ? POLLOUT : 0)), 0});
  for (auto &t : transfers)
    fds.push_back({t->fd, short(t->writing ? POLLOUT : POLLIN), 0});
  auto timer = deadline;
  for (auto &t : transfers)
    timer = std::min(timer, t->deadline);
  for (auto &p : seats)
    if (p.second->repeatKey)
      timer = std::min(timer, p.second->repeatAt);
  int ready;
  do {
    int wait = int(std::max<long long>(
        0, std::chrono::duration_cast<std::chrono::milliseconds>(timer -
                                                                 Clock::now())
               .count()));
    ready = poll(fds.data(), fds.size(), wait);
  } while (ready < 0 && errno == EINTR && Clock::now() < timer);
  if (ready < 0) {
    wl_display_cancel_read(display);
    if (errno != EINTR)
      fail(ErrorCode::System, "Wayland poll failed");
    return alive;
  }
  if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
    wl_display_cancel_read(display);
    fail(ErrorCode::Connection, "Wayland display disconnected");
    return false;
  }
  if (fds[0].revents & POLLIN) {
    if (wl_display_read_events(display) < 0) {
      fail(ErrorCode::Connection, "Wayland read failed");
      return false;
    }
  } else
    wl_display_cancel_read(display);
  if ((fds[0].revents & POLLOUT) && wl_display_flush(display) < 0 &&
      errno != EAGAIN) {
    fail(ErrorCode::Connection, "Wayland flush failed");
    return false;
  }
  // dispatch can create transfers, so the polled list is deliberately processed
  // first and only then may callbacks mutate the owned transfer container.
  for (std::size_t i = 1; i < fds.size(); ++i)
    transferReady(*transfers[i - 1], fds[i].revents);
  if (wl_display_dispatch_pending(display) < 0) {
    fail(ErrorCode::Protocol, "Wayland protocol dispatch failed");
    return false;
  }
  ticks();
  return alive;
}
void initRegistry(Core &c) {
  static const wl_registry_listener listener = {
      [](void *p, wl_registry *registry, uint32_t name, const char *interface,
         uint32_t version) {
        auto &c = *static_cast<Core *>(p);
        auto bind = [&](const wl_interface *i, uint32_t max) {
          return wl_registry_bind(registry, name, i, std::min(version, max));
        };
        if (!strcmp(interface, wl_compositor_interface.name) && version >= 4)
          c.compositor =
              static_cast<wl_compositor *>(bind(&wl_compositor_interface, 6));
        else if (!strcmp(interface, wl_subcompositor_interface.name))
          c.subcompositor = static_cast<wl_subcompositor *>(
              bind(&wl_subcompositor_interface, 1));
        else if (!strcmp(interface, wl_shm_interface.name))
          c.shm = static_cast<wl_shm *>(bind(&wl_shm_interface, 1));
        else if (!strcmp(interface, xdg_wm_base_interface.name)) {
          c.shell = static_cast<xdg_wm_base *>(bind(&xdg_wm_base_interface, 6));
          static const xdg_wm_base_listener l = {
              [](void *, xdg_wm_base *shell, uint32_t serial) {
                xdg_wm_base_pong(shell, serial);
              }};
          xdg_wm_base_add_listener(c.shell, &l, &c);
        } else if (!strcmp(interface, wp_viewporter_interface.name))
          c.viewporter =
              static_cast<wp_viewporter *>(bind(&wp_viewporter_interface, 1));
        else if (!strcmp(interface,
                         wp_fractional_scale_manager_v1_interface.name))
          c.fractional = static_cast<wp_fractional_scale_manager_v1 *>(
              bind(&wp_fractional_scale_manager_v1_interface, 1));
        else if (!strcmp(interface, zxdg_decoration_manager_v1_interface.name))
          c.decoration = static_cast<zxdg_decoration_manager_v1 *>(
              bind(&zxdg_decoration_manager_v1_interface, 1));
        else if (!strcmp(interface,
                         zwp_relative_pointer_manager_v1_interface.name))
          c.relative = static_cast<zwp_relative_pointer_manager_v1 *>(
              bind(&zwp_relative_pointer_manager_v1_interface, 1));
        else if (!strcmp(interface, zwp_pointer_constraints_v1_interface.name))
          c.constraints = static_cast<zwp_pointer_constraints_v1 *>(
              bind(&zwp_pointer_constraints_v1_interface, 1));
        else if (!strcmp(interface, wp_cursor_shape_manager_v1_interface.name))
          c.cursorShape = static_cast<wp_cursor_shape_manager_v1 *>(
              bind(&wp_cursor_shape_manager_v1_interface, 1));
        else if (!strcmp(interface, zwp_text_input_manager_v3_interface.name))
          c.textInput = static_cast<zwp_text_input_manager_v3 *>(
              bind(&zwp_text_input_manager_v3_interface, 1));
        else if (!strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name))
          c.idle = static_cast<zwp_idle_inhibit_manager_v1 *>(
              bind(&zwp_idle_inhibit_manager_v1_interface, 1));
        else if (!strcmp(interface, wl_data_device_manager_interface.name))
          c.dataManager = static_cast<wl_data_device_manager *>(
              bind(&wl_data_device_manager_interface, 3));
        else if (!strcmp(interface, wl_seat_interface.name)) {
          if (version < 5)
            return;
          auto seat = std::make_unique<Seat>();
          seat->core = &c;
          seat->id = name;
          seat->proxy = static_cast<wl_seat *>(bind(&wl_seat_interface, 8));
          initSeat(*seat);
          c.setupSeat(*seat);
          c.seats[name] = std::move(seat);
        } else if (!strcmp(interface, wl_output_interface.name)) {
          auto output = std::make_unique<OutputData>();
          output->core = &c;
          output->value.id = name;
          output->proxy =
              static_cast<wl_output *>(bind(&wl_output_interface, 4));
          static const wl_output_listener listener = {
              [](void *p, wl_output *, int32_t, int32_t, int32_t, int32_t,
                 int32_t, const char *make, const char *model, int32_t) {
                auto &o = *static_cast<OutputData *>(p);
                if (o.value.description.empty())
                  o.value.description = std::string(make ? make : "") + " " +
                                        (model ? model : "");
              },
              [](void *p, wl_output *, uint32_t flags, int32_t w, int32_t h,
                 int32_t) {
                if (flags & WL_OUTPUT_MODE_CURRENT)
                  static_cast<OutputData *>(p)->value.pixels = {w, h};
              },
              [](void *p, wl_output *) {
                auto &o = *static_cast<OutputData *>(p);
                o.core->emit(event(EventType::OutputsChanged));
                for (auto &w : o.core->windows)
                  w.second->updateScale();
              },
              [](void *p, wl_output *, int32_t scale) {
                static_cast<OutputData *>(p)->value.scale = std::max(1, scale);
              },
              [](void *p, wl_output *, const char *name) {
                static_cast<OutputData *>(p)->value.name = name ? name : "";
              },
              [](void *p, wl_output *, const char *desc) {
                static_cast<OutputData *>(p)->value.description =
                    desc ? desc : "";
              }};
          wl_output_add_listener(output->proxy, &listener, output.get());
          c.outputs[name] = std::move(output);
        }
        for (auto &s : c.seats)
          c.setupSeat(*s.second);
      },
      [](void *p, wl_registry *, uint32_t name) {
        auto &c = *static_cast<Core *>(p);
        auto seat = c.seats.find(name);
        if (seat != c.seats.end()) {
          keyboardFocusLost(*seat->second);
          pointerFocusLost(*seat->second);
          c.seats.erase(seat);
        }
        auto output = c.outputs.find(name);
        if (output != c.outputs.end()) {
          auto *proxy = output->second->proxy;
          if (wl_output_get_version(proxy) >= 3)
            wl_output_release(proxy);
          else
            wl_output_destroy(proxy);
          c.outputs.erase(output);
          for (auto &w : c.windows) {
            w.second->outputs.erase(name);
            w.second->updateScale();
          }
          c.emit(event(EventType::OutputsChanged));
        }
      }};
  wl_registry_add_listener(c.registry, &listener, &c);
}
Core::~Core() {
  transfers.clear();
  seats.clear();
  for (auto &output : outputs) {
    auto *p = output.second->proxy;
    if (wl_output_get_version(p) >= 3)
      wl_output_release(p);
    else
      wl_output_destroy(p);
  }
  outputs.clear();
  if (compose)
    xkb_compose_table_unref(compose);
  if (xkb)
    xkb_context_unref(xkb);
  if (idle)
    zwp_idle_inhibit_manager_v1_destroy(idle);
  if (textInput)
    zwp_text_input_manager_v3_destroy(textInput);
  if (cursorShape)
    wp_cursor_shape_manager_v1_destroy(cursorShape);
  if (constraints)
    zwp_pointer_constraints_v1_destroy(constraints);
  if (relative)
    zwp_relative_pointer_manager_v1_destroy(relative);
  if (decoration)
    zxdg_decoration_manager_v1_destroy(decoration);
  if (fractional)
    wp_fractional_scale_manager_v1_destroy(fractional);
  if (viewporter)
    wp_viewporter_destroy(viewporter);
  if (dataManager)
    wl_data_device_manager_destroy(dataManager);
  if (shell)
    xdg_wm_base_destroy(shell);
  if (shm)
    wl_shm_destroy(shm);
  if (subcompositor)
    wl_subcompositor_destroy(subcompositor);
  if (compositor)
    wl_compositor_destroy(compositor);
  if (registry)
    wl_registry_destroy(registry);
  if (display)
    wl_display_disconnect(display);
}
} // namespace nw::detail

namespace nw {
struct Context::Impl : detail::Core {};
struct Window::Impl : detail::NativeWindow {};
Context::Context(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}
Context::~Context() = default;
Window::Window(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}
Window::~Window() = default;
std::unique_ptr<Context> Context::create(const ContextOptions &options,
                                         Error &error, Logger logger) {
  error = {};
  auto p = std::make_unique<Impl>();
  p->options = options;
  p->logger = std::move(logger);
  if (options.connectTimeout.count() <= 0 ||
      options.transferTimeout.count() <= 0 || options.maxTransferBytes == 0 ||
      options.maxQueuedEvents < 32) {
    error = {ErrorCode::Invalid, "Invalid context limits"};
    return {};
  }
  p->display = wl_display_connect(
      options.displayName.empty() ? nullptr : options.displayName.c_str());
  if (!p->display) {
    error = {ErrorCode::Connection, "Cannot connect to Wayland display"};
    return {};
  }
  p->registry = wl_display_get_registry(p->display);
  p->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!p->xkb) {
    error = {ErrorCode::System, "Cannot initialize XKB"};
    return {};
  }
  const char *locale = std::getenv("LC_ALL");
  if (!locale || !*locale)
    locale = std::getenv("LC_CTYPE");
  if (!locale || !*locale)
    locale = std::getenv("LANG");
  if (!locale || !*locale)
    locale = "C.UTF-8";
  p->compose = xkb_compose_table_new_from_locale(p->xkb, locale,
                                                 XKB_COMPOSE_COMPILE_NO_FLAGS);
  detail::initRegistry(*p);
  auto sync = [&](bool &done) {
    auto *callback = wl_display_sync(p->display);
    static const wl_callback_listener listener = {
        [](void *p, wl_callback *callback, uint32_t) {
          *static_cast<bool *>(p) = true;
          wl_callback_destroy(callback);
        }};
    wl_callback_add_listener(callback, &listener, &done);
  };
  sync(p->discoverDone);
  auto deadline = detail::Clock::now() + options.connectTimeout;
  while (!p->discoverDone && p->alive && detail::Clock::now() < deadline)
    p->pump(int(std::min<long long>(
        1000, std::chrono::duration_cast<std::chrono::milliseconds>(
                  deadline - detail::Clock::now())
                  .count())));
  if (p->alive && p->discoverDone) {
    sync(p->initialDone);
    while (!p->initialDone && p->alive && detail::Clock::now() < deadline)
      p->pump(int(std::min<long long>(
          1000, std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - detail::Clock::now())
                    .count())));
  }
  if (!p->alive) {
    error = p->terminal;
    return {};
  }
  if (!p->initialDone) {
    error = {ErrorCode::Timeout, "Wayland registry discovery timed out"};
    return {};
  }
  if (!p->compositor || !p->shell || !p->shm || !p->subcompositor) {
    error = {ErrorCode::Unsupported,
             "Compositor lacks required "
             "Wayland/xdg-shell/shared-memory/subsurface globals"};
    return {};
  }
  return std::unique_ptr<Context>(new Context(std::move(p)));
}
bool Context::pump(std::chrono::milliseconds timeout) {
  bool was = impl_->alive;
  bool result = impl_->pump(detail::clampWait(timeout.count()));
  if (was && !result && impl_->logger)
    impl_->logger(impl_->terminal);
  return result;
}
std::vector<Event> Context::takeEvents() {
  std::vector<Event> result;
  result.reserve(impl_->events.size());
  while (!impl_->events.empty()) {
    result.push_back(std::move(impl_->events.front()));
    impl_->events.pop_front();
  }
  return result;
}
Capabilities Context::capabilities() const {
  auto &c = *impl_;
  Capabilities caps;
  caps.fractionalScale = c.fractional && c.viewporter;
  caps.serverDecorations = c.decoration;
  caps.cursorShape = c.cursorShape;
  caps.relativePointer = c.relative;
  caps.pointerConstraints = c.constraints;
  caps.clipboard = c.dataManager;
  caps.fileDrop = c.dataManager;
  caps.textInput = c.textInput;
  caps.idleInhibit = c.idle;
  return caps;
}
std::vector<Output> Context::outputs() const {
  std::vector<Output> result;
  for (auto &p : impl_->outputs)
    result.push_back(p.second->value);
  return result;
}
bool Context::connected() const { return impl_->alive; }
void *Context::nativeDisplay() const { return impl_->display; }
std::unique_ptr<Window> Context::createWindow(const WindowOptions &options,
                                              Error &error) {
  error = {};
  if (!impl_->alive) {
    error = impl_->terminal;
    return {};
  }
  if (options.size.width <= 0 || options.size.height <= 0 ||
      options.size.width > 32768 || options.size.height > 32768 ||
      !detail::validUtf8(options.title) || !detail::validUtf8(options.appId)) {
    error = {ErrorCode::Invalid, "Invalid window options"};
    return {};
  }
  auto w = std::make_unique<Window::Impl>();
  w->core = impl_.get();
  w->id = impl_->nextWindow++;
  w->state.logicalSize = options.size;
  w->state.framebufferSize = options.size;
  w->windowedSize = options.size;
  w->title = options.title;
  w->decorated = options.decorated;
  w->surface = wl_compositor_create_surface(impl_->compositor);
  w->shell = xdg_wm_base_get_xdg_surface(impl_->shell, w->surface);
  w->top = xdg_surface_get_toplevel(w->shell);
  impl_->windows[w->id] = w.get();
  detail::initWindow(*w);
  xdg_toplevel_set_title(w->top, options.title.c_str());
  xdg_toplevel_set_app_id(w->top, options.appId.c_str());
  xdg_toplevel_set_min_size(w->top, 64, 64);
  wl_surface_commit(w->surface);
  auto deadline = detail::Clock::now() + impl_->options.connectTimeout;
  while (!w->state.configured && impl_->alive &&
         detail::Clock::now() < deadline)
    impl_->pump(int(std::min<long long>(
        1000, std::chrono::duration_cast<std::chrono::milliseconds>(
                  deadline - detail::Clock::now())
                  .count())));
  if (!impl_->alive) {
    error = impl_->terminal;
    return {};
  }
  if (!w->state.configured) {
    error = {ErrorCode::Timeout, "Initial window configure timed out"};
    return {};
  }
  return std::unique_ptr<Window>(new Window(std::move(w)));
}
WindowId Window::id() const { return impl_->id; }
WindowState Window::state() const { return impl_->state; }
void *Window::nativeSurface() const { return impl_->surface; }
void *Window::nativeDisplay() const { return impl_->core->display; }
RequestResult Window::requestSize(Size size) {
  auto &w = *impl_;
  if (!w.core->alive)
    return RequestResult::Failed;
  if (size.width <= 0 || size.height <= 0 || size.width > 32768 ||
      size.height > 32768)
    return RequestResult::Invalid;
  if (w.state.fullscreen || w.state.maximized)
    return RequestResult::Unsupported;
  w.state.logicalSize = size;
  w.fitToBounds();
  w.updateScale();
  w.drawDecorations();
  auto e = detail::event(EventType::Configure, &w);
  e.state = w.state;
  w.core->emit(e);
  return RequestResult::Accepted;
}
RequestResult Window::requestFullscreen(bool enabled, OutputId hint) {
  auto &w = *impl_;
  if (!w.core->alive)
    return RequestResult::Failed;
  wl_output *output = nullptr;
  if (hint) {
    auto p = w.core->outputs.find(hint);
    if (p != w.core->outputs.end())
      output = p->second->proxy;
  }
  if (enabled)
    xdg_toplevel_set_fullscreen(w.top, output);
  else
    xdg_toplevel_unset_fullscreen(w.top);
  return RequestResult::Accepted;
}
RequestResult Window::requestMaximized(bool enabled) {
  if (!impl_->core->alive)
    return RequestResult::Failed;
  if (enabled)
    xdg_toplevel_set_maximized(impl_->top);
  else
    xdg_toplevel_unset_maximized(impl_->top);
  return RequestResult::Accepted;
}
RequestResult Window::minimize() {
  if (!impl_->core->alive)
    return RequestResult::Failed;
  xdg_toplevel_set_minimized(impl_->top);
  return RequestResult::Accepted;
}
RequestResult Window::setTitle(const std::string &title) {
  if (!impl_->core->alive)
    return RequestResult::Failed;
  if (!detail::validUtf8(title) || title.size() > 4096)
    return RequestResult::Invalid;
  impl_->title = title;
  xdg_toplevel_set_title(impl_->top, title.c_str());
  impl_->drawDecorations();
  return RequestResult::Accepted;
}
RequestResult Window::setCursor(CursorShape shape, bool visible) {
  if (!impl_->core->alive)
    return RequestResult::Failed;
  if (int(shape) < 0 || int(shape) > int(CursorShape::Forbidden))
    return RequestResult::Invalid;
  impl_->cursor = shape;
  impl_->cursorVisible = visible;
  for (auto &p : impl_->core->seats)
    if (p.second->pointerWindow == impl_.get())
      p.second->applyCursor();
  return RequestResult::Accepted;
}
RequestResult Window::requestPointerCapture(CaptureMode mode, SeatId seatId) {
  auto &w = *impl_;
  if (!w.core->alive)
    return RequestResult::Failed;
  auto *s = w.core->selectSeat(seatId, &w);
  if (mode != CaptureMode::None && mode != CaptureMode::Locked &&
      mode != CaptureMode::Confined)
    return RequestResult::Invalid;
  if (mode == CaptureMode::None) {
    if (s && s->captureWindow == &w)
      s->releaseCapture(false);
    return RequestResult::Accepted;
  }
  if (!s || !s->pointer || s->pointerWindow != &w || s->pointerPart >= 0)
    return RequestResult::Invalid;
  s->releaseCapture(false);
  if (!w.core->constraints ||
      (mode == CaptureMode::Locked && !w.core->relative))
    return RequestResult::Unsupported;
  s->captureWindow = &w;
  s->captureState = CaptureState::Pending;
  if (mode == CaptureMode::Locked) {
    s->locked = zwp_pointer_constraints_v1_lock_pointer(
        w.core->constraints, w.surface, s->pointer, nullptr,
        ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
    static const zwp_locked_pointer_v1_listener l = {
        [](void *p, zwp_locked_pointer_v1 *) {
          auto &s = *static_cast<detail::Seat *>(p);
          s.captureState = CaptureState::Active;
          auto e =
              detail::event(EventType::CaptureChanged, s.captureWindow, &s);
          e.capture = s.captureState;
          s.core->emit(e);
        },
        [](void *p, zwp_locked_pointer_v1 *) {
          static_cast<detail::Seat *>(p)->releaseCapture(true);
        }};
    zwp_locked_pointer_v1_add_listener(s->locked, &l, s);
    s->relative = zwp_relative_pointer_manager_v1_get_relative_pointer(
        w.core->relative, s->pointer);
    static const zwp_relative_pointer_v1_listener r = {
        [](void *p, zwp_relative_pointer_v1 *, uint32_t hi, uint32_t lo,
           wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t, wl_fixed_t) {
          auto &s = *static_cast<detail::Seat *>(p);
          if (s.captureState != CaptureState::Active)
            return;
          auto e =
              detail::event(EventType::RelativeMotion, s.captureWindow, &s);
          e.timeMs = uint32_t(((uint64_t(hi) << 32) | lo) / 1000);
          e.dx = wl_fixed_to_double(dx);
          e.dy = wl_fixed_to_double(dy);
          s.core->emit(e);
        }};
    zwp_relative_pointer_v1_add_listener(s->relative, &r, s);
  } else if (mode == CaptureMode::Confined) {
    s->confined = zwp_pointer_constraints_v1_confine_pointer(
        w.core->constraints, w.surface, s->pointer, nullptr,
        ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
    static const zwp_confined_pointer_v1_listener l = {
        [](void *p, zwp_confined_pointer_v1 *) {
          auto &s = *static_cast<detail::Seat *>(p);
          s.captureState = CaptureState::Active;
          auto e =
              detail::event(EventType::CaptureChanged, s.captureWindow, &s);
          e.capture = s.captureState;
          s.core->emit(e);
        },
        [](void *p, zwp_confined_pointer_v1 *) {
          static_cast<detail::Seat *>(p)->releaseCapture(true);
        }};
    zwp_confined_pointer_v1_add_listener(s->confined, &l, s);
  } else
    return RequestResult::Invalid;
  auto e = detail::event(EventType::CaptureChanged, &w, s);
  e.capture = CaptureState::Pending;
  w.core->emit(e);
  return RequestResult::Accepted;
}
RequestResult Window::requestClipboardText(SeatId id) {
  auto &w = *impl_;
  if (!w.core->alive)
    return RequestResult::Failed;
  if (!w.core->dataManager)
    return RequestResult::Unsupported;
  auto *s = w.core->selectSeat(id, &w);
  if (!s)
    return RequestResult::Invalid;
  w.core->startRead(*s, &w, s->selection, false);
  return RequestResult::Accepted;
}
RequestResult Window::setClipboardText(const std::string &text, SeatId id) {
  auto &w = *impl_;
  if (!w.core->alive)
    return RequestResult::Failed;
  if (!w.core->dataManager)
    return RequestResult::Unsupported;
  if (text.size() > w.core->options.maxTransferBytes ||
      !detail::validUtf8(text))
    return RequestResult::Invalid;
  auto *s = w.core->selectSeat(id, &w);
  if (!s || !s->serial || !s->dataDevice ||
      (s->keyWindow != &w && s->pointerWindow != &w))
    return RequestResult::Invalid;
  detail::destroySource(*s);
  s->clipboard = text;
  s->source = wl_data_device_manager_create_data_source(w.core->dataManager);
  static const wl_data_source_listener listener = {
      [](void *, wl_data_source *, const char *) {},
      [](void *p, wl_data_source *, const char *mime, int32_t fd) {
        auto &s = *static_cast<detail::Seat *>(p);
        if (!mime ||
            (strcmp(mime, "text/plain;charset=utf-8") &&
             strcmp(mime, "UTF8_STRING") && strcmp(mime, "text/plain")) ||
            s.core->transfers.size() >= 16) {
          close(fd);
          return;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        auto t = std::make_unique<detail::Transfer>();
        t->fd = fd;
        t->writing = true;
        t->bytes = s.clipboard;
        t->deadline = detail::Clock::now() + s.core->options.transferTimeout;
        s.core->transfers.push_back(std::move(t));
      },
      [](void *p, wl_data_source *source) {
        auto &s = *static_cast<detail::Seat *>(p);
        wl_data_source_destroy(source);
        if (s.source == source)
          s.source = nullptr;
      },
      [](void *, wl_data_source *) {},
      [](void *, wl_data_source *) {},
      [](void *, wl_data_source *, uint32_t) {}};
  wl_data_source_add_listener(s->source, &listener, s);
  for (const char *mime :
       {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain"})
    wl_data_source_offer(s->source, mime);
  wl_data_device_set_selection(s->dataDevice, s->source, s->serial);
  return RequestResult::Accepted;
}
RequestResult Window::setIdleInhibited(bool enabled) {
  auto &w = *impl_;
  if (!w.core->alive)
    return RequestResult::Failed;
  if (!w.core->idle)
    return RequestResult::Unsupported;
  if (enabled && !w.inhibitor)
    w.inhibitor =
        zwp_idle_inhibit_manager_v1_create_inhibitor(w.core->idle, w.surface);
  else if (!enabled && w.inhibitor) {
    zwp_idle_inhibitor_v1_destroy(w.inhibitor);
    w.inhibitor = nullptr;
  }
  return RequestResult::Accepted;
}
RequestResult Window::setTextInput(bool enabled, const std::string &surrounding,
                                   int cursor, int anchor) {
  auto &w = *impl_;
  if (!w.core->alive)
    return RequestResult::Failed;
  if (!w.core->textInput)
    return RequestResult::Unsupported;
  if (!detail::validUtf8(surrounding) || surrounding.size() > 4000 ||
      cursor < 0 || anchor < 0 || std::size_t(cursor) > surrounding.size() ||
      std::size_t(anchor) > surrounding.size())
    return RequestResult::Invalid;
  if ((std::size_t(cursor) < surrounding.size() &&
       (static_cast<unsigned char>(surrounding[cursor]) & 0xc0) == 0x80) ||
      (std::size_t(anchor) < surrounding.size() &&
       (static_cast<unsigned char>(surrounding[anchor]) & 0xc0) == 0x80))
    return RequestResult::Invalid;
  w.textInputEnabled = enabled;
  w.surrounding = surrounding;
  w.textCursor = cursor;
  w.textAnchor = anchor;
  for (auto &p : w.core->seats) {
    auto &s = *p.second;
    if (s.keyWindow != &w && s.textWindow != &w)
      continue;
    s.imeEnabled = enabled;
    if (s.textWindow != &w)
      continue;
    if (enabled) {
      if (!s.imeActive)
        zwp_text_input_v3_enable(s.textInput);
      zwp_text_input_v3_set_surrounding_text(s.textInput, surrounding.c_str(),
                                             cursor, anchor);
    } else if (s.imeActive)
      zwp_text_input_v3_disable(s.textInput);
    zwp_text_input_v3_commit(s.textInput);
    ++s.textCommitSerial;
    s.imeActive = enabled;
  }
  return RequestResult::Accepted;
}
RequestResult Window::setCaretRectangle(int x, int y, int width, int height) {
  if (!impl_->core->alive)
    return RequestResult::Failed;
  if (!impl_->core->textInput)
    return RequestResult::Unsupported;
  if (width < 0 || height < 0)
    return RequestResult::Invalid;
  for (auto &p : impl_->core->seats) {
    auto &s = *p.second;
    if (s.textWindow == impl_.get()) {
      zwp_text_input_v3_set_cursor_rectangle(s.textInput, x, y, width, height);
      zwp_text_input_v3_commit(s.textInput);
      ++s.textCommitSerial;
    }
  }
  return RequestResult::Accepted;
}
} // namespace nw
