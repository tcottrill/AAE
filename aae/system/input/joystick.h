#pragma once
#include "joystick_win32.h"

int install_joystick();
void remove_joystick();
int poll_joystick();

extern bool is_connected;
extern void letstryagain_joystick();
// -----------------------------------------------------------------------------
// Hotplug callback support
// -----------------------------------------------------------------------------

typedef void (*JoystickHotplugCallback)(int index, bool connected, const char* message);
void set_joystick_hotplug_callback(JoystickHotplugCallback callback);
extern JoystickHotplugCallback joystick_hotplug_callback;  // only extern here