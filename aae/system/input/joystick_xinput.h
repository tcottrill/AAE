#pragma once

bool joystick_xinput_init();
void joystick_xinput_exit();
int joystick_xinput_poll();
bool joystick_xinput_available();


//using JoystickHotplugCallback = void(*)(int joystick_index, bool connected, const char* error_message);
//extern JoystickHotplugCallback joystick_hotplug_callback;
