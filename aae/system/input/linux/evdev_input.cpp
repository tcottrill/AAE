//==============================================================================
// evdev_input.cpp -- Linux input backend.
//
// TEMPORARY (Phase 3c Milestone A). Everything here is a stub so the aae target
// can LINK and the X11/GLX window can be tested on its own. Milestone B
// replaces every one of these with the real evdev implementation: per-device
// keyboards and mice with /dev/input/by-id/ identity, player routing, and
// gamepads with force-feedback rumble.
//
// The #warning below is deliberate. A silently-stubbed input layer is
// indistinguishable from broken hardware, and this must not reach a build
// anyone plays without it being obvious.
//==============================================================================
#warning "evdev_input.cpp is the Milestone A stub - NO INPUT WILL WORK"

#include "sys_input.h"
#include "joystick.h"
#include "sys_log.h"

#include <cstring>

//------------------------------------------------------------------------------
// State the contract says the backend DEFINES (sys_input.h declares them
// extern; a backend that omits them fails at link time, not compile time).
//------------------------------------------------------------------------------
unsigned char key[256] = { 0 };
int mouse_b = 0;

//------------------------------------------------------------------------------
// Joystick globals from joystick.h
//------------------------------------------------------------------------------
int num_joysticks = 0;
int _joystick_installed = 0;
JOYSTICK_INFO joy[MAX_JOYSTICKS] = {};

// --- keyboard ---------------------------------------------------------------
int  isKeyHeld(int)             { return 0; }
bool IsKeyDown(int)             { return false; }
bool IsKeyUp(int)               { return true; }
int  GetModifierFlags()         { return 0; }

int  RawInput_GetKeyboardCount()          { return 0; }
const char* RawInput_GetKeyboardName(int) { return ""; }
const char* RawInput_GetKeyboardPath(int) { return ""; }
int  RawInput_FindKeyboardByPath(const char*) { return -1; }
int  RawInput_KeyboardSeenInput(int)      { return 0; }
int  RawInput_IsKeyDownEx(int, int)       { return 0; }

// --- mouse ------------------------------------------------------------------
void get_mouse_win(int* x, int* y)      { if (x) *x = 0; if (y) *y = 0; }
void get_mouse_mickeys(int* x, int* y)  { if (x) *x = 0; if (y) *y = 0; }
void get_mouse_mickeys_ex(int, int* x, int* y) { if (x) *x = 0; if (y) *y = 0; }
void set_mouse_mickey_scale(float)      {}

int32_t GetMouseX()      { return 0; }
int32_t GetMouseY()      { return 0; }
int32_t GetMouseWheel()  { return 0; }
void SetMouseX(int32_t)      {}
void SetMouseY(int32_t)      {}
void SetMouseWheel(int32_t)  {}
int32_t GetMouseXChange()     { return 0; }
int32_t GetMouseYChange()     { return 0; }
int32_t GetMouseWheelChange() { return 0; }

bool IsMouseLButtonDown() { return false; }
bool IsMouseLButtonUp()   { return true; }
bool IsMouseRButtonDown() { return false; }
bool IsMouseRButtonUp()   { return true; }
bool IsMouseMButtonDown() { return false; }
bool IsMouseMButtonUp()   { return true; }

int  RawInput_GetMouseCount()          { return 0; }
const char* RawInput_GetMouseName(int) { return ""; }
const char* RawInput_GetMousePath(int) { return ""; }
int  RawInput_FindMouseByPath(const char*) { return -1; }
int  RawInput_GetMouseButtons(int)     { return 0; }
int  RawInput_MouseSeenInput(int)      { return 0; }

void RawInput_SetPaused(bool) {}
void RawInput_Shutdown()      {}

// --- callbacks --------------------------------------------------------------
void SetKeyCallback(KeyCallback)                   {}
void SetMouseButtonCallback(MouseButtonCallback)   {}
void SetCursorPositionCallback(CursorPositionCallback) {}

// --- joystick ---------------------------------------------------------------
int  install_joystick() { LOG_INFO("evdev stub: no joystick support yet (Milestone B)"); return -1; }
void remove_joystick()  {}
int  poll_joystick()    { return -1; }

bool joystick_set_rumble(int, float, float) { return false; }
void joystick_stop_rumble(int)              {}
void set_joystick_hotplug_callback(JoystickHotplugCallback) {}
bool joystick_check_combo(int, uint16_t)    { return false; }
bool joystick_using_xinput()                { return false; }
const char* joystick_driver_name()          { return "None (stub)"; }
void joystick_device_change()               {}
int  joystick_device_count()                { return 0; }
const char* joystick_get_display_name(int)  { return ""; }
int  joystick_is_connected(int)             { return 0; }
const char* joystick_get_id(int)            { return ""; }
int  joystick_find_by_id(const char*)       { return -1; }
bool joystick_any_connected()               { return false; }
