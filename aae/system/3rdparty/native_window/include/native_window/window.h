#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// All operations, including destruction, belong to the creating thread. Context
// must outlive its Windows; graphics bindings must be destroyed before Window.
// Protocol callbacks only enqueue owned events; no application code is called.
namespace nw {
using WindowId = std::uint64_t;
using SeatId = std::uint32_t;
using OutputId = std::uint32_t;
enum class ErrorCode {
  None,
  Invalid,
  Unsupported,
  Connection,
  Protocol,
  System,
  Timeout
};
struct Error {
  ErrorCode code = ErrorCode::None;
  std::string message;
  explicit operator bool() const { return code != ErrorCode::None; }
};
enum class RequestResult { Accepted, Unsupported, Invalid, Failed };
struct Size {
  int width = 0, height = 0;
};
struct WindowState {
  Size logicalSize{960, 540}, framebufferSize{960, 540};
  double scale = 1.0;
  bool configured = false, fullscreen = false, maximized = false;
  bool activated = false, resizing = false, closed = false;
  std::vector<OutputId> enteredOutputs;
  // xdg_toplevel.configure_bounds as CONTENT size (client decorations already
  // subtracted): the largest a windowed client should make itself so it stays
  // inside the usable work area (panels/docks excluded). 0x0 when the
  // compositor has not sent bounds (xdg_wm_base < 4, or it chose not to).
  Size bounds;
};
struct Output {
  OutputId id = 0;
  std::string name, description;
  Size pixels;
  int scale = 1;
};
struct Capabilities {
  bool fractionalScale = false, serverDecorations = false,
       clientDecorations = true;
  bool cursorShape = false, relativePointer = false, pointerConstraints = false;
  bool clipboard = false, fileDrop = false, textInput = false,
       idleInhibit = false;
  bool globalPosition = false, pointerWarp = false;
};
enum class CursorShape {
  Arrow,
  Text,
  Hand,
  Crosshair,
  ResizeHorizontal,
  ResizeVertical,
  ResizeNWSE,
  ResizeNESW,
  Wait,
  Forbidden
};
enum class CaptureMode { None, Confined, Locked };
enum class CaptureState { Inactive, Pending, Active, Revoked };
enum class EventType {
  Configure,
  Close,
  OutputsChanged,
  KeyboardFocus,
  PointerFocus,
  Activation,
  Key,
  Modifiers,
  Text,
  Preedit,
  DeleteSurroundingText,
  PointerMotion,
  PointerButton,
  PointerAxis,
  RelativeMotion,
  InputReset,
  CaptureChanged,
  ClipboardChanged,
  Clipboard,
  Drop,
  ConnectionError
};
enum Modifier : std::uint32_t {
  Shift = 1,
  Control = 2,
  Alt = 4,
  Super = 8,
  CapsLock = 16,
  NumLock = 32
};
// Key.code is an evdev physical key (no +8), symbol is an XKB keysym. Pointer
// buttons use Linux BTN_* codes. Positions are logical CONTENT coordinates.
// timeMs is the compositor's wrapping millisecond timestamp; repeat events use
// the last compositor time plus elapsed steady-clock time. Axis values retain
// Wayland units, with discrete wheel steps in discreteX/Y when supplied.
struct Event {
  EventType type = EventType::Configure;
  WindowId window = 0;
  SeatId seat = 0;
  std::uint32_t timeMs = 0, code = 0, symbol = 0, modifiers = 0;
  bool pressed = false, repeat = false, focused = false, synchronized = false;
  double x = 0, y = 0, dx = 0, dy = 0;
  double wheelX = 0, wheelY = 0; // wheel steps, including value120 fractions
  int discreteX = 0, discreteY = 0;
  std::uint32_t axisSource = 0;
  int cursorBegin = 0, cursorEnd = 0;
  std::uint32_t beforeBytes = 0, afterBytes = 0;
  WindowState state;
  CaptureState capture = CaptureState::Inactive;
  std::string text;
  std::vector<std::string> paths;
  Error error;
};
struct ContextOptions {
  std::string displayName; // empty uses WAYLAND_DISPLAY
  std::chrono::milliseconds connectTimeout{3000}, transferTimeout{5000};
  std::size_t maxTransferBytes = 8 * 1024 * 1024, maxQueuedEvents = 4096;
};
struct WindowOptions {
  Size size{960, 540};
  std::string title = "Native window", appId = "native-window";
  bool decorated = true;
};
using Logger = std::function<void(const Error &)>;
class Window;
class Context {
public:
  static std::unique_ptr<Context> create(const ContextOptions &, Error &,
                                         Logger = {});
  ~Context();
  Context(const Context &) = delete;
  Context &operator=(const Context &) = delete;
  // Never waits longer than timeout (clamped to [0, 1000] ms). false is a
  // terminal connection error, emitted once; subsequent calls return false.
  bool pump(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
  std::vector<Event> takeEvents();
  Capabilities capabilities() const;
  std::vector<Output> outputs() const;
  std::unique_ptr<Window> createWindow(const WindowOptions &, Error &);
  bool connected() const;
  // Borrowed wl_display*. Never disconnect or dispatch it outside Context.
  void *nativeDisplay() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  explicit Context(std::unique_ptr<Impl>);
  friend class Window;
};
class Window {
public:
  ~Window();
  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;
  WindowId id() const;
  WindowState state() const;
  RequestResult requestSize(Size);
  RequestResult requestFullscreen(bool enabled, OutputId outputHint = 0);
  RequestResult requestMaximized(bool enabled);
  RequestResult minimize();
  RequestResult setTitle(const std::string &utf8);
  RequestResult setCursor(CursorShape shape, bool visible);
  RequestResult requestPointerCapture(CaptureMode, SeatId seat = 0);
  RequestResult setClipboardText(const std::string &, SeatId seat = 0);
  RequestResult requestClipboardText(SeatId seat = 0);
  RequestResult setIdleInhibited(bool);
  // text-input-v3: offsets are UTF-8 byte offsets; unsupported is explicit.
  RequestResult setTextInput(bool enabled, const std::string &surrounding = {},
                             int cursor = 0, int anchor = 0);
  RequestResult setCaretRectangle(int x, int y, int width, int height);
  // Borrowed wl_surface*, valid until Window destruction. Its buffer is owned
  // by the graphics binding. Creation completes the initial configure/ack;
  // EGL/Vulkan must be torn down before this Window. No application position.
  void *nativeSurface() const;
  void *nativeDisplay() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  explicit Window(std::unique_ptr<Impl>);
  friend class Context;
};
} // namespace nw
