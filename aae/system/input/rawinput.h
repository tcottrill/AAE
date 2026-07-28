//==============================================================================
// rawinput.h -- Usage Guide & API Reference
//==============================================================================
//
// OVERVIEW
// --------
// Windows Raw Input wrapper providing low-level keyboard and mouse access
// with three complementary consumption models:
//
//   1. CALLBACK (GLFW-style)  -- register functions that fire on key/button/move
//   2. POLLED STATE ARRAYS    -- read key[] and mouse_b directly each frame
//   3. QUERY FUNCTIONS        -- IsKeyDown(), GetMouseX(), etc.
//
// All three models read from the same internal state, which is updated on a
// dedicated worker thread. Raw WM_INPUT messages are enqueued on the message
// pump thread and batch-processed by the worker, keeping input handling off
// the rendering path.
//
// Designed for arcade / retro emulator use: the key defines and mouse_b
// bitmask are Allegro-4 compatible, so legacy game code that reads key[]
// and mouse_b works without changes.
//
//
// QUICK START
// -----------
//   #include "rawinput.h"
//
//   // During window creation (after CreateWindow / HWND is valid):
//   if (FAILED(RawInput_Initialize(hwnd))) {
//       MessageBox(hwnd, "Raw input init failed", "Error", MB_ICONERROR);
//       return -1;
//   }
//
//   // In your WndProc:
//   case WM_INPUT:
//       return RawInput_ProcessInput(hWnd, wParam, lParam);
//
//   // --- Polled access (in your frame loop) ---
//   if (key[KEY_ESC])          { quit(); }
//   if (key[KEY_LEFT])         { move_left(); }
//   if (mouse_b & 0x01)       { fire(); }     // left button
//
//   // --- Query functions ---
//   if (IsKeyDown(KEY_SPACE))  { jump(); }
//
//   // --- Relative mouse (mickeys) ---
//   int mx, my;
//   get_mouse_mickeys(&mx, &my);              // reads and resets deltas
//
//   // --- Window-relative mouse position ---
//   int wx, wy;
//   get_mouse_win(&wx, &wy);                  // uses GetCursorPos + ScreenToClient
//
//   // --- Callbacks (register once, fires on every event) ---
//   SetKeyCallback([](int key, int scancode, int action, int mods) {
//       // action: 1 = pressed, 0 = released
//       // mods: bitmask of RI_MOD_SHIFT | RI_MOD_CONTROL | RI_MOD_ALT | RI_MOD_SUPER
//   });
//
//   SetMouseButtonCallback([](int button, int action, int mods) {
//       // button: 0=left, 1=right, 2=middle
//       // action: 1=pressed, 0=released
//   });
//
//   SetCursorPositionCallback([](double xpos, double ypos) {
//       // accumulated raw position (not window coords -- see notes below)
//   });
//
//   // At shutdown:
//   RawInput_Shutdown();
//
//
// INITIALIZATION & SHUTDOWN
// -------------------------
//   HRESULT RawInput_Initialize(HWND hWnd)
//       Registers keyboard (HID usage 0x06) and mouse (HID usage 0x02) as
//       Raw Input devices with RIDEV_INPUTSINK (receives input even when
//       window is not focused). Zeros all state arrays. Starts the input
//       worker thread. Returns S_OK on success, E_FAIL if registration fails.
//
//       Safe to call multiple times -- if already initialized, automatically
//       shuts down the previous instance before reinitializing.
//
//   void RawInput_Shutdown()
//       Signals the worker thread to exit, joins it. Safe to call if not
//       initialized (no-ops). Does NOT unregister the Raw Input devices --
//       Windows handles that on process exit.
//
//
// WM_INPUT MESSAGE HANDLING
// -------------------------
//   LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam)
//       Call this from your WndProc on WM_INPUT. It:
//         1. Calls GetRawInputData() to extract the RAWINPUT struct
//         2. Pushes it onto the worker queue (under mutex)
//         3. Signals the worker thread
//         4. Returns DefWindowProc() result
//
//       If input is paused (via RawInput_SetPaused), the event is silently
//       discarded to DefWindowProc without queuing.
//
//       The worker thread batch-processes all queued events, updating key[],
//       lastkey[], mouse state, mouse_b, and firing callbacks.
//
//
// KEYBOARD STATE
// --------------
// Two parallel arrays track keyboard state:
//
//   unsigned char key[256]    -- Allegro-compatible keystate buffer
//       key[vkCode] = 1 when pressed, 0 when released.
//       Read directly in your frame loop: if (key[KEY_A]) { ... }
//       Indexed by virtual key code. Use the KEY_* defines for readability.
//
//   unsigned int lastkey[256] -- Hold counter (internal)
//       Increments each Raw Input message while a key is held. Useful for
//       auto-repeat / hold detection. Read via isKeyHeld(vkCode).
//       Returns 0 when released, 1+ when held (count of press messages).
//
// Key defines follow Allegro-4 naming mapped to Windows VK codes:
//   KEY_A..KEY_Z         (0x41..0x5A)
//   KEY_0..KEY_9         (0x30..0x39)
//   KEY_0_PAD..KEY_9_PAD (VK_NUMPAD0..VK_NUMPAD9)
//   KEY_F1..KEY_F12      (VK_F1..VK_F12)
//   KEY_ESC, KEY_ENTER, KEY_SPACE, KEY_TAB, KEY_BACKSPACE
//   KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN
//   KEY_INSERT, KEY_DEL, KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN
//   KEY_LSHIFT, KEY_RSHIFT, KEY_LCONTROL, KEY_RCONTROL
//   KEY_ALT (VK_LMENU), KEY_ALTGR (VK_RMENU)
//   KEY_LWIN, KEY_RWIN, KEY_CAPSLOCK, KEY_NUMLOCK, KEY_SCRLOCK
//   KEY_TILDE, KEY_MINUS, KEY_EQUALS, KEY_COMMA, KEY_STOP, KEY_SLASH
//   KEY_OPENBRACE, KEY_CLOSEBRACE, KEY_BACKSLASH, KEY_COLON, KEY_QUOTE
//   KEY_PRTSCR, KEY_PAUSE
//   KEY_MAX = 0xEF
//
// Left/right modifier disambiguation: VK_SHIFT is resolved to VK_LSHIFT
// or VK_RSHIFT via MapVirtualKey. VK_CONTROL and VK_MENU are resolved
// using the E0 escape flag. This means key[KEY_LSHIFT] and key[KEY_RSHIFT]
// work independently.
//
// Numpad vs. navigation: when NumLock is off, Insert/Home/PgUp/etc. are
// remapped to their numpad equivalents (VK_NUMPAD0, VK_NUMPAD7, etc.)
// based on the E0 flag. This matches standard Windows keyboard behavior.
//
//
// KEYBOARD QUERY FUNCTIONS
// ------------------------
//   int  isKeyHeld(INT vkCode)
//       Returns the hold counter from lastkey[]. 0 = not held, 1+ = held.
//       Useful for detecting key repeat (value increases each raw message
//       while the key remains down).
//
//   bool IsKeyDown(INT vkCode)
//       Returns true if key[vkCode] is nonzero (key is pressed).
//
//   bool IsKeyUp(INT vkCode)
//       Returns true if key[vkCode] is zero (key is released).
//
//
// KEYBOARD CALLBACK
// -----------------
//   typedef void (*KeyCallback)(int key, int scancode, int action, int mods);
//   void SetKeyCallback(KeyCallback callback);
//
//   Fires once per key press/release event on the worker thread.
//   Parameters:
//     key      -- Windows virtual key code (left/right disambiguated)
//     scancode -- hardware scan code
//     action   -- 1 = pressed, 0 = released
//     mods     -- bitmask: RI_MOD_SHIFT   (0x01)
//                          RI_MOD_CONTROL (0x02)
//                          RI_MOD_ALT     (0x04)
//                          RI_MOD_SUPER   (0x08)
//
//   Modifier state is sampled via GetAsyncKeyState() at callback time,
//   so it reflects the system-wide modifier state, not just raw input.
//
//   Pass nullptr to unregister the callback.
//
//
// MOUSE STATE
// -----------
// Internal mouse state tracks:
//   - Accumulated absolute position (x, y) -- sum of all raw deltas since init
//   - Per-frame relative deltas (dx, dy) -- accumulated between reads
//   - Wheel delta (dwheel)
//   - Button states (left, middle, right)
//
// IMPORTANT: The accumulated absolute position (GetMouseX/Y, SetMouseX/Y)
// is NOT the actual Windows cursor position. It is the running sum of all
// raw mouse deltas and can grow without bound. For actual window-relative
// cursor coordinates, use get_mouse_win() instead.
//
//
// MOUSE QUERY FUNCTIONS -- Relative Motion (Mickeys)
// ---------------------------------------------------
//   void get_mouse_mickeys(int* mx, int* my)
//       Returns accumulated relative motion since last call, scaled by the
//       mickey scale factor (default 1.0). Resets dx/dy to zero after read.
//       This is the primary mouse input function for game/emulator use.
//
//   void set_mouse_mickey_scale(float scale)
//       Sets the multiplier applied to raw deltas in get_mouse_mickeys().
//       Default is 1.0. Applied at read time, not accumulation time, so
//       changing it mid-frame does not lose precision.
//
//
// MOUSE QUERY FUNCTIONS -- Window Position
// -----------------------------------------
//   void get_mouse_win(int* mx, int* my)
//       Returns the actual Windows cursor position in client coordinates
//       via GetCursorPos() + ScreenToClient(). Independent of raw input
//       accumulation. Use this for UI/menu mouse interaction.
//
//
// MOUSE QUERY FUNCTIONS -- Accumulated Raw Position
// --------------------------------------------------
//   LONG GetMouseX()  / void SetMouseX(LONG x)
//   LONG GetMouseY()  / void SetMouseY(LONG y)
//   LONG GetMouseWheel()  / void SetMouseWheel(LONG w)
//       Get/set the accumulated raw position. This is the running sum of
//       all raw deltas, not the Windows cursor position. Useful if you want
//       to maintain your own coordinate space (e.g., for a virtual cursor).
//       SetMouse* can be used to reset or reposition the virtual origin.
//
//   LONG GetMouseXChange()
//   LONG GetMouseYChange()
//   LONG GetMouseWheelChange()
//       Returns current dx/dy/dwheel without resetting. Unlike
//       get_mouse_mickeys(), these do NOT zero the deltas after read
//       and do NOT apply the mickey scale factor.
//
//
// MOUSE BUTTON STATE
// ------------------
//   bool IsMouseLButtonDown() / IsMouseLButtonUp()
//   bool IsMouseRButtonDown() / IsMouseRButtonUp()
//   bool IsMouseMButtonDown() / IsMouseMButtonUp()
//       Query individual button state. Updates come from raw input events.
//
//   extern int mouse_b;
//       Allegro-compatible button bitmask, updated each raw input message:
//         bit 0 (0x01) = left button
//         bit 1 (0x02) = right button
//         bit 2 (0x04) = middle button
//       Usage: if (mouse_b & 0x01) { /* left held */ }
//
//   Mouse buttons are processed via bitmask flag tests (not a switch),
//   so simultaneous button state changes in a single RAWINPUT message
//   are all handled correctly.
//
//
// MOUSE BUTTON CALLBACK
// ---------------------
//   typedef void (*MouseButtonCallback)(int button, int action, int mods);
//   void SetMouseButtonCallback(MouseButtonCallback callback);
//
//   Fires on the worker thread when a mouse button changes state.
//   Parameters:
//     button -- 0=left, 1=right, 2=middle
//     action -- 1=pressed, 0=released
//     mods   -- same bitmask as key callback (RI_MOD_*)
//
//   Pass nullptr to unregister the callback.
//
//
// CURSOR POSITION CALLBACK
// ------------------------
//   typedef void (*CursorPositionCallback)(double xpos, double ypos);
//   void SetCursorPositionCallback(CursorPositionCallback callback);
//
//   Fires on the worker thread whenever raw mouse motion is received.
//   Receives the accumulated raw position (not window coords).
//   The mickey scale factor does NOT affect this callback -- it reports
//   the raw accumulated totals.
//
//   Pass nullptr to unregister the callback.
//
//
// PAUSE / RESUME
// --------------
//   void RawInput_SetPaused(bool paused)
//       When paused:
//         - Incoming WM_INPUT messages are discarded (not queued)
//         - All queued events are flushed
//         - key[] and lastkey[] are zeroed (prevents stuck keys)
//         - mouse_b and all button states are cleared
//         - Mouse deltas (dx, dy, dwheel) are zeroed
//       When resumed (paused=false):
//         - Processing resumes normally from a clean state
//
//       Designed for alt-tab / lost-focus handling. Call with true when
//       your window loses focus, false when it regains focus.
//
//       Calling SetPaused(true) when already paused is a safe no-op.
//       Only the transition from unpaused -> paused triggers the flush.
//
//
// MODIFIER FLAGS
// --------------
//   int GetModifierFlags()
//       Returns current modifier bitmask via GetAsyncKeyState():
//         RI_MOD_SHIFT   (0x01) -- either Shift key
//         RI_MOD_CONTROL (0x02) -- either Ctrl key
//         RI_MOD_ALT     (0x04) -- either Alt key
//         RI_MOD_SUPER   (0x08) -- either Win key
//
//       This reads system-wide key state, not the raw input buffer.
//       The same bitmask is passed to key and mouse button callbacks.
//
//
// UTILITIES
// ---------
//   void test_clr()
//       Zeros key[] and lastkey[]. Use to force-clear keyboard state
//       without going through the full pause/resume cycle. Does not
//       affect mouse state.
//
//   bset(p, m) / bclr(p, m)
//       Bitfield helper macros. bset sets bits, bclr clears bits.
//       Used internally for mouse_b; available for general use.
//
//   toUpper(ch)
//       Macro to convert lowercase ASCII to uppercase via bitmask.
//
//
// THREADING MODEL
// ---------------
// Input processing runs on a dedicated worker thread:
//
//   WndProc receives WM_INPUT
//     -> RawInput_ProcessInput() extracts RAWINPUT, pushes to queue
//     -> signals condition variable
//   Worker thread wakes
//     -> swaps queue to local copy (batch processing under lock)
//     -> processes each event: updates key[], lastkey[], mouse state
//     -> fires callbacks (on worker thread, NOT the main thread)
//     -> unlocks and waits for next signal
//
// IMPORTANT: Callbacks fire on the WORKER THREAD. If your callback
// touches rendering state or non-thread-safe data, you are responsible
// for synchronization (e.g., pushing events to a queue that the main
// thread drains each frame).
//
// The key[], lastkey[], and mouse state are written only by the worker
// thread and read by the main thread -- single-producer / single-consumer
// with natural-width writes, which is safe on x86 for the data sizes
// involved.
//
//
// INIT / SHUTDOWN SEQUENCE
// ------------------------
//   RawInput_Initialize(hWnd) -- Registers devices, zeros state, starts
//                                 worker thread. Returns S_OK or E_FAIL.
//                                 Safe to call again (auto-shuts down first).
//   RawInput_Shutdown()       -- Stops worker thread, joins. Safe to call
//                                 if not initialized (no-ops).
//
// Typical integration in a WndProc:
//
//   case WM_INPUT:
//       return RawInput_ProcessInput(hWnd, wParam, lParam);
//
//   case WM_ACTIVATEAPP:
//       RawInput_SetPaused(wParam == FALSE);  // pause on deactivate
//       break;
//
//
// DEPENDENCIES
// ------------
// Windows headers:  <windows.h> (Raw Input API, GetAsyncKeyState)
// C++ standard:     C++11 (thread, mutex, condition_variable, atomic, queue)
// Project headers:  "sys_log.h"
//
//==============================================================================

// Authors:
//   - Jay Tennant (original implementation)
//   - TC (Allegro Compatibility, Full Keyboard Key Support, Modernization and GLFW style callback system)
// -----------------------------------------------------------------------------


#pragma once

#include <windows.h>

#define bset(p,m) ((p) |= (m))
#define bclr(p,m) ((p) &= ~(m))

// ---------------------------------------------------------------------------
// AaeKey - AAE's canonical physical key codes.
//
// Values are historical Windows VK codes and MUST NOT change: they are what
// gets written to default.cfg / per-game .cfg via writeword() in inptport.cpp,
// and what menu.cpp's key_names[] table is indexed by.
//
// This is an UNSCOPED enum on purpose - the values implicitly convert to int,
// so key[AAEKEY_A], IsKeyDown(AAEKEY_ESC) and the cfg write path all work with
// no casts. It is an enum rather than macros because macros leak into every
// downstream header; <linux/input-event-codes.h> defines KEY_A as 30, and the
// two cannot coexist in one translation unit.
//
// Backends translate their native codes to these (Win32: 1:1 with VK codes;
// Linux: an evdev -> AaeKey table in the evdev backend).
//
// Several enumerators deliberately share a value (AAEKEY_TILDE/BACKQUOTE,
// AAEKEY_COLON/SEMICOLON, AAEKEY_BACKSLASH/BACKSLASH2, AAEKEY_ALT/LMENU,
// AAEKEY_AT/SCRLOCK, AAEKEY_CIRCUMFLEX/NUMLOCK). These duplicates are
// pre-existing and intentional - do not "fix" them.
// ---------------------------------------------------------------------------
enum AaeKey {
    AAEKEY_A = 0x41, AAEKEY_B = 0x42, AAEKEY_C = 0x43, AAEKEY_D = 0x44,
    AAEKEY_E = 0x45, AAEKEY_F = 0x46, AAEKEY_G = 0x47, AAEKEY_H = 0x48,
    AAEKEY_I = 0x49, AAEKEY_J = 0x4a, AAEKEY_K = 0x4b, AAEKEY_L = 0x4c,
    AAEKEY_M = 0x4d, AAEKEY_N = 0x4e, AAEKEY_O = 0x4f, AAEKEY_P = 0x50,
    AAEKEY_Q = 0x51, AAEKEY_R = 0x52, AAEKEY_S = 0x53, AAEKEY_T = 0x54,
    AAEKEY_U = 0x55, AAEKEY_V = 0x56, AAEKEY_W = 0x57, AAEKEY_X = 0x58,
    AAEKEY_Y = 0x59, AAEKEY_Z = 0x5a,

    AAEKEY_0 = 0x30, AAEKEY_1 = 0x31, AAEKEY_2 = 0x32, AAEKEY_3 = 0x33,
    AAEKEY_4 = 0x34, AAEKEY_5 = 0x35, AAEKEY_6 = 0x36, AAEKEY_7 = 0x37,
    AAEKEY_8 = 0x38, AAEKEY_9 = 0x39,

    AAEKEY_0_PAD = 0x60, AAEKEY_1_PAD = 0x61, AAEKEY_2_PAD = 0x62,
    AAEKEY_3_PAD = 0x63, AAEKEY_4_PAD = 0x64, AAEKEY_5_PAD = 0x65,
    AAEKEY_6_PAD = 0x66, AAEKEY_7_PAD = 0x67, AAEKEY_8_PAD = 0x68,
    AAEKEY_9_PAD = 0x69,

    AAEKEY_F1 = 0x70, AAEKEY_F2 = 0x71, AAEKEY_F3  = 0x72, AAEKEY_F4  = 0x73,
    AAEKEY_F5 = 0x74, AAEKEY_F6 = 0x75, AAEKEY_F7  = 0x76, AAEKEY_F8  = 0x77,
    AAEKEY_F9 = 0x78, AAEKEY_F10 = 0x79, AAEKEY_F11 = 0x7a, AAEKEY_F12 = 0x7b,

    AAEKEY_ESC        = 0x1b,
    AAEKEY_TILDE      = 0xc0,
    AAEKEY_MINUS      = 0xbd,
    AAEKEY_EQUALS     = 0xbb,
    AAEKEY_BACKSPACE  = 0x08,
    AAEKEY_TAB        = 0x09,
    AAEKEY_OPENBRACE  = 0xdb,
    AAEKEY_CLOSEBRACE = 0xdd,
    AAEKEY_ENTER      = 0x0d,
    AAEKEY_COLON      = 0xba,
    AAEKEY_QUOTE      = 0xde,
    AAEKEY_BACKSLASH  = 0xdc,
    AAEKEY_BACKSLASH2 = 0xdc,
    AAEKEY_COMMA      = 0xbc,
    AAEKEY_STOP       = 0xbe,
    AAEKEY_SLASH      = 0xbf,
    AAEKEY_SPACE      = 0x20,

    AAEKEY_INSERT = 0x2d, AAEKEY_DEL  = 0x2e, AAEKEY_HOME = 0x24,
    AAEKEY_END    = 0x23, AAEKEY_PGUP = 0x21, AAEKEY_PGDN = 0x22,
    AAEKEY_LEFT   = 0x25, AAEKEY_RIGHT = 0x27,
    AAEKEY_UP     = 0x26, AAEKEY_DOWN  = 0x28,

    AAEKEY_SLASH_PAD  = 0x6f,
    AAEKEY_ASTERISK   = 0x6a,
    AAEKEY_MINUS_PAD  = 0x6d,
    AAEKEY_PLUS_PAD   = 0x6b,
    AAEKEY_DEL_PAD    = 0x6e,
    AAEKEY_ENTER_PAD  = 0x6c,
    AAEKEY_PRTSCR     = 0x2c,
    AAEKEY_PAUSE      = 0x13,

    AAEKEY_ABNT_C1    = 0xc1,
    AAEKEY_YEN        = 0x7d,
    AAEKEY_KANA       = 0x15,
    AAEKEY_CONVERT    = 0x79,
    AAEKEY_NOCONVERT  = 0x7b,
    AAEKEY_AT         = 0x91,
    AAEKEY_CIRCUMFLEX = 0x90,
    AAEKEY_COLON2     = 0x92,
    AAEKEY_KANJI      = 0x94,
    AAEKEY_EQUALS_PAD = 0x00,
    AAEKEY_BACKQUOTE  = 0xc0,
    AAEKEY_SEMICOLON  = 0xba,

    AAEKEY_LSHIFT   = 0xa0, AAEKEY_RSHIFT   = 0xa1,
    AAEKEY_LCONTROL = 0xa2, AAEKEY_RCONTROL = 0xa3,
    AAEKEY_ALT      = 0xa4, AAEKEY_LMENU    = 0xa4,
    AAEKEY_RMENU    = 0xa5, AAEKEY_ALTGR    = 0xa5,
    AAEKEY_LWIN     = 0x5b, AAEKEY_RWIN     = 0x5c,
    AAEKEY_MENU     = 0x12,
    AAEKEY_SCRLOCK  = 0x91,
    AAEKEY_NUMLOCK  = 0x90,
    AAEKEY_CAPSLOCK = 0x14,

    AAEKEY_MAX = 0xef
};

// Public modifier masks 
enum RI_Modifiers {
    RI_MOD_SHIFT = 0x01,
    RI_MOD_CONTROL = 0x02,
    RI_MOD_ALT = 0x04,
    RI_MOD_SUPER = 0x08
};

#define toUpper(ch) ((ch >= 'a' && ch <='z') ? ch & 0x5f : ch)
#define RI_MOUSE_HWHEEL 0x0800 

int GetModifierFlags();

// -----------------------------------------------------------------------------
// Keyboard Callback Support
// -----------------------------------------------------------------------------
typedef void (*KeyCallback)(int key, int scancode, int action, int mods);

// -----------------------------------------------------------------------------
// Registers a key callback (GLFW-style)
// -----------------------------------------------------------------------------
void SetKeyCallback(KeyCallback callback);

// -----------------------------------------------------------------------------
// Callback Type Definitions
// -----------------------------------------------------------------------------
typedef void (*MouseButtonCallback)(int button, int action, int mods);
typedef void (*CursorPositionCallback)(double xpos, double ypos);

// -----------------------------------------------------------------------------
// Set callback functions
// -----------------------------------------------------------------------------
void SetMouseButtonCallback(MouseButtonCallback callback);
void SetCursorPositionCallback(CursorPositionCallback callback);

// -----------------------------------------------------------------------------
// Sets the scaling factor for raw mouse mickeys (default is 1.0)
// -----------------------------------------------------------------------------
void set_mouse_mickey_scale(float scale);

// -----------------------------------------------------------------------------
// Multiple mice (per-device Raw Input)
//
// Every RAWINPUT mouse event carries the source device handle; state is
// tracked per device in addition to the merged legacy state above. Devices
// are enumerated at init and registered on the fly if they first appear at
// runtime (hot-plug).
//
//   RawInput_GetMouseCount()    -- number of distinct mice seen
//   RawInput_GetMouseName(i)    -- short display name ("HID#VID_046D&PID_C077")
//   get_mouse_mickeys_ex(i,...) -- per-device relative motion, read-and-reset,
//                                  scaled by the mickey scale. i = -1 reads the
//                                  MERGED stream (all mice), identical to the
//                                  legacy get_mouse_mickeys().
//   RawInput_GetMouseButtons(i) -- bit0=left, bit1=right, bit2=middle;
//                                  i = -1 returns the merged mouse_b.
//
// All existing single-mouse functions keep working on the merged stream.
// -----------------------------------------------------------------------------
#define RI_MAX_MICE 8

int  RawInput_GetMouseCount();
const char* RawInput_GetMouseName(int index);
void get_mouse_mickeys_ex(int index, int* mickeyx, int* mickeyy);
int  RawInput_GetMouseButtons(int index);
int  RawInput_MouseSeenInput(int index);   // 1 once the device has sent input
                                           // (identifies real vs phantom mice;
                                           // RDP virtual devices are filtered)
const char* RawInput_GetMousePath(int index);      // stable device identity
int  RawInput_FindMouseByPath(const char* path);   // path -> current index, -1
                                                   // if not attached

// -----------------------------------------------------------------------------
// Multiple keyboards (per-device Raw Input)
//
// Same model as the mice: every RAWINPUT keyboard event carries the source
// device handle, and per-device key state is tracked alongside the merged
// legacy key[] (which all existing consumers keep using). Devices are
// enumerated at init and hot-plug registered; RDP virtual keyboards are
// filtered; friendly names resolve through the registry.
//
//   RawInput_IsKeyDownEx(i, vk) -- key state on keyboard i (VK-indexed, same
//                                  translation as the merged key[]); i = -1
//                                  reads the merged state.
// -----------------------------------------------------------------------------
#define RI_MAX_KBDS 8

int  RawInput_GetKeyboardCount();
const char* RawInput_GetKeyboardName(int index);
const char* RawInput_GetKeyboardPath(int index);
int  RawInput_FindKeyboardByPath(const char* path);
int  RawInput_KeyboardSeenInput(int index);
int  RawInput_IsKeyDownEx(int index, int vk);

// -----------------------------------------------------------------------------
// Allegro compatible C style keystate buffers.
// -----------------------------------------------------------------------------
extern int mouse_b;
extern unsigned char key[256];
// -----------------------------------------------------------------------------
// Registers a mouse and keyboard for raw input;
// Usage: if (FAILED(RawInput_Initialize(hwnd))) {
// MessageBox(hwnd, "Failed to initialize raw input", "Error", MB_ICONERROR);
// return -1;
// }
// -----------------------------------------------------------------------------
HRESULT RawInput_Initialize(HWND hWnd);

// -----------------------------------------------------------------------------
// Pauses/Resumes background input processing and clears stuck keys
// -----------------------------------------------------------------------------
void RawInput_SetPaused(bool paused);

// -----------------------------------------------------------------------------
// Processes WM_INPUT messages
// -----------------------------------------------------------------------------
LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam);

// -----------------------------------------------------------------------------
// NEW: Shuts down the raw input worker thread gracefully
// -----------------------------------------------------------------------------
void RawInput_Shutdown();

// -----------------------------------------------------------------------------
// Keyboard State Queries
// -----------------------------------------------------------------------------
int isKeyHeld(INT vkCode);
bool IsKeyDown(INT vkCode);
bool IsKeyUp(INT vkCode);

// -----------------------------------------------------------------------------
// Summed mouse state checks/sets;
// Use as convenience, ie. keeping track of movements without needing to maintain separate data set
// Added for Allegro Code Compatibility
// -----------------------------------------------------------------------------
void get_mouse_win(int *mickeyx, int *mickeyy);
void get_mouse_mickeys(int *mickeyx, int *mickeyy);
LONG GetMouseX();
LONG GetMouseY();
LONG GetMouseWheel();
void SetMouseX(LONG x);
void SetMouseY(LONG y);
void SetMouseWheel(LONG wheel);

// -----------------------------------------------------------------------------
// Relative mouse state changes
// -----------------------------------------------------------------------------
LONG GetMouseXChange();
LONG GetMouseYChange();
LONG GetMouseWheelChange();

// -----------------------------------------------------------------------------
// Mouse button state checks
// -----------------------------------------------------------------------------
bool IsMouseLButtonDown();
bool IsMouseLButtonUp();
bool IsMouseRButtonDown();
bool IsMouseRButtonUp();
bool IsMouseMButtonDown();
bool IsMouseMButtonUp();

// -----------------------------------------------------------------------------
// Utilities
// -----------------------------------------------------------------------------
void test_clr();