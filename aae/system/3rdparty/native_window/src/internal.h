#pragma once
#include "cursor-shape-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "helpers.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include <chrono>
#include <deque>
#include <map>
#include <native_window/window.h>
#include <set>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

namespace nw::detail {
using Clock = std::chrono::steady_clock;
struct Core;
struct NativeWindow;
struct Seat;
struct OutputData {
  Core *core;
  wl_output *proxy = nullptr;
  Output value;
};
struct ShmBuffer {
  wl_buffer *proxy = nullptr;
  void *memory = nullptr;
  std::size_t bytes = 0;
  bool released = false;
  ~ShmBuffer();
};
struct Decoration {
  wl_surface *surface = nullptr;
  wl_subsurface *sub = nullptr;
  std::vector<std::unique_ptr<ShmBuffer>> buffers;
  ~Decoration();
};
struct Offer {
  wl_data_offer *proxy = nullptr;
  std::set<std::string> mimes;
  ~Offer() {
    if (proxy)
      wl_data_offer_destroy(proxy);
  }
};
struct Transfer {
  int fd = -1;
  bool writing = false, drop = false;
  WindowId window = 0;
  SeatId seat = 0;
  std::string bytes;
  std::size_t offset = 0;
  Clock::time_point deadline;
  std::shared_ptr<Offer> offer;
  ~Transfer();
};
struct NativeWindow {
  Core *core = nullptr;
  WindowId id = 0;
  WindowState state;
  std::string title;
  wl_surface *surface = nullptr;
  xdg_surface *shell = nullptr;
  xdg_toplevel *top = nullptr;
  zxdg_toplevel_decoration_v1 *serverDecoration = nullptr;
  wp_viewport *viewport = nullptr;
  wp_fractional_scale_v1 *fractional = nullptr;
  zwp_idle_inhibitor_v1 *inhibitor = nullptr;
  std::set<OutputId> outputs;
  unsigned scale120 = 120;
  int preferredScale = 0;
  int suggestedW = 0, suggestedH = 0;
  // Raw xdg_toplevel.configure_bounds (window-geometry units, so including any
  // client-side decoration); 0 = not sent.
  int boundsW = 0, boundsH = 0;
  void fitToBounds();
  bool pendingFullscreen = false, pendingMaximized = false,
       pendingActivated = false, pendingResizing = false;
  bool decorated = true, useClientDecoration = true, cursorVisible = true,
       decorationDirty = false;
  Size windowedSize{960, 540};
  CursorShape cursor = CursorShape::Arrow;
  std::unique_ptr<Decoration> decorations[4];
  bool textInputEnabled = false;
  std::string surrounding;
  int textCursor = 0, textAnchor = 0;
  ~NativeWindow();
  void updateScale();
  void configured();
  void drawDecorations();
  int decorationPart(wl_surface *) const;
};
struct Seat {
  Core *core;
  SeatId id;
  wl_seat *proxy = nullptr;
  wl_keyboard *keyboard = nullptr;
  wl_pointer *pointer = nullptr;
  wl_data_device *dataDevice = nullptr;
  wl_data_source *source = nullptr;
  zwp_text_input_v3 *textInput = nullptr;
  wp_cursor_shape_device_v1 *shapeDevice = nullptr;
  zwp_relative_pointer_v1 *relative = nullptr;
  zwp_locked_pointer_v1 *locked = nullptr;
  zwp_confined_pointer_v1 *confined = nullptr;
  wl_surface *cursorSurface = nullptr;
  wl_cursor_theme *cursorTheme = nullptr;
  int cursorScale = 0;
  xkb_keymap *keymap = nullptr;
  xkb_state *xkb = nullptr;
  xkb_compose_state *compose = nullptr;
  NativeWindow *keyWindow = nullptr;
  NativeWindow *pointerWindow = nullptr;
  NativeWindow *textWindow = nullptr;
  NativeWindow *captureWindow = nullptr;
  detail::HeldInput held;
  std::uint32_t serial = 0, enterSerial = 0, modifiers = 0;
  int pointerPart = -1;
  double x = 0, y = 0;
  Event axis;
  int repeatRate = 25, repeatDelay = 600;
  unsigned repeatKey = 0;
  std::uint32_t repeatTime = 0;
  Clock::time_point repeatAt, repeatOrigin;
  CaptureState captureState = CaptureState::Inactive;
  std::shared_ptr<Offer> selection, drag;
  std::map<wl_data_offer *, std::shared_ptr<Offer>> offers;
  NativeWindow *dragWindow = nullptr;
  std::string clipboard;
  bool imeEnabled = false, imeActive = false;
  std::uint32_t textCommitSerial = 0;
  std::string preedit, commit;
  int preeditBegin = 0, preeditEnd = 0;
  std::uint32_t deleteBefore = 0, deleteAfter = 0;
  bool gotPreedit = false, gotCommit = false, gotDelete = false;
  ~Seat();
  void reset();
  void releaseCapture(bool revoked);
  void applyCursor();
  void text(unsigned key, std::uint32_t time, bool repeat);
};
struct Core {
  ContextOptions options;
  Logger logger;
  bool alive = true, discoverDone = false, initialDone = false;
  Error terminal;
  wl_display *display = nullptr;
  wl_registry *registry = nullptr;
  wl_compositor *compositor = nullptr;
  wl_subcompositor *subcompositor = nullptr;
  wl_shm *shm = nullptr;
  xdg_wm_base *shell = nullptr;
  wp_viewporter *viewporter = nullptr;
  wp_fractional_scale_manager_v1 *fractional = nullptr;
  zxdg_decoration_manager_v1 *decoration = nullptr;
  zwp_relative_pointer_manager_v1 *relative = nullptr;
  zwp_pointer_constraints_v1 *constraints = nullptr;
  wp_cursor_shape_manager_v1 *cursorShape = nullptr;
  zwp_text_input_manager_v3 *textInput = nullptr;
  zwp_idle_inhibit_manager_v1 *idle = nullptr;
  wl_data_device_manager *dataManager = nullptr;
  xkb_context *xkb = nullptr;
  xkb_compose_table *compose = nullptr;
  std::map<OutputId, std::unique_ptr<OutputData>> outputs;
  std::map<SeatId, std::unique_ptr<Seat>> seats;
  std::map<WindowId, NativeWindow *> windows;
  std::deque<Event> events;
  std::vector<std::unique_ptr<Transfer>> transfers;
  WindowId nextWindow = 1;
  ~Core();
  void emit(Event);
  void fail(ErrorCode, const std::string &);
  bool pump(int);
  NativeWindow *find(wl_surface *, int *part = nullptr);
  Seat *selectSeat(SeatId, NativeWindow *);
  void setupSeat(Seat &);
  void startRead(Seat &, NativeWindow *, std::shared_ptr<Offer>, bool);
  void transferReady(Transfer &, short);
  void ticks();
};
void initRegistry(Core &);
void initWindow(NativeWindow &);
void initSeat(Seat &);
} // namespace nw::detail
