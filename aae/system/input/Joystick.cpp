#include "joystick.h"
#include "joystick_win32.h"
#include "joystick_xinput.h"
#include "sys_log.h"

int num_joysticks = 0;
int _joystick_installed = 0;
static int _joystick_xinput = 0;
//JOYSTICK_INFO joy[MAX_JOYSTICKS]{};
//extern JoystickHotplugCallback joystick_hotplug_callback = nullptr;

static void clear_joystick_vars()
{
    constexpr const char* unused = "unused";

    for (auto& j : joy) {
        j.flags = 0;
        j.num_sticks = 0;
        j.num_buttons = 0;

        for (auto& stick : j.stick) {
            stick.flags = 0;
            stick.num_axis = 0;
            stick.name = unused;

            for (auto& axis : stick.axis) {
                axis.pos = 0;
                axis.d1 = false;
                axis.d2 = false;
                axis.name = unused;
            }
        }

        for (auto& button : j.button) {
            button.b = false;
            button.name = unused;
        }
    }

    num_joysticks = 0;
}

int install_joystick()
{
    if (_joystick_installed)
        return 0;

    clear_joystick_vars();

    if (joystick_xinput_available()) {
        LOG_INFO("Using XInput joystick driver");
        if (!joystick_xinput_init()) {
            LOG_ERROR("Failed to initialize XInput joystick");
            return -1;
        }
        _joystick_xinput = 1;
    }
    else {
        LOG_INFO("Using Win32 joystick driver");
        if (!install_joystick_win32()) {
            LOG_ERROR("Failed to initialize Win32 joystick");
            return -1;
        }
        _joystick_xinput = 0;
    }

    poll_joystick();
    _joystick_installed = true;
    return 0;
}

void remove_joystick()
{
    if (joystick_xinput_available())
        joystick_xinput_exit();
    else
        remove_joystick_win32();

    _joystick_installed = false;
}

int poll_joystick()
{
    if (!_joystick_installed)
        return -1;

    static bool first_poll = true;

    if (_joystick_xinput) {
        if (first_poll) {
            LOG_INFO("Polling joystick using XInput");
            first_poll = false;
        }
        return joystick_xinput_poll();
    }
    else {
        if (first_poll) {
            LOG_INFO("Polling joystick using Win32");
            first_poll = false;
        }
         return poll_joystick_win32();
    }
}

JoystickHotplugCallback joystick_hotplug_callback = nullptr;

void set_joystick_hotplug_callback(JoystickHotplugCallback callback)
{
    joystick_hotplug_callback = callback;
}
