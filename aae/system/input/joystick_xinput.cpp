#ifndef _M_X64
#define _M_X64 1
#endif
#include <windows.h>
#include "joystick.h"
#include "joystick_xinput.h"
#include <Xinput.h>
#include <cstring>
#include <winerror.h>
#include "sys_log.h"

#pragma comment(lib, "Xinput9_1_0.lib") // Support for Windows 7
//#pragma comment(lib, "xinput.lib")

//static constexpr int RECONNECT_COOLDOWN = 60; // e.g. 1 second at 60 FPS

static constexpr int MAX_XINPUT_CONTROLLERS = 4;

static bool was_connected[MAX_XINPUT_CONTROLLERS] = {};
static bool notify_connect[MAX_XINPUT_CONTROLLERS] = {};
static bool notify_disconnect[MAX_XINPUT_CONTROLLERS] = {};

bool is_connected = 0;

static int reconnect_delay[MAX_XINPUT_CONTROLLERS] = {};
static DWORD last_packet_number[MAX_XINPUT_CONTROLLERS] = {};
static int stale_frame_count[MAX_XINPUT_CONTROLLERS] = {};
static constexpr int STALE_FRAME_THRESHOLD = 120; // 2 seconds at 60 FPS


static bool xinput_active = false;
//JoystickHotplugCallback joystick_hotplug_callback = nullptr;
static bool controller_connected[MAX_XINPUT_CONTROLLERS] = {};
//static XINPUT_STATE last_state[4] = {};

enum XInputConnectionState {
    XINPUT_DISCONNECTED = 0,
    XINPUT_CONNECTED = 1
};

//static XInputConnectionState xinput_state[MAX_XINPUT_CONTROLLERS] = {};

void letstryagain_joystick()
{

    DWORD dwResults;
    XINPUT_STATE statea;

    // Try controller 0
    dwResults = XInputGetState(0, &statea);

    if (dwResults == ERROR_SUCCESS) {
        LOG_ERROR("Joystick Connected in lib");
    }
    else {
        LOG_ERROR("Joystick disconnected in lib");
    }
}


static int xinput_map_button(WORD buttons, WORD mask) {
    return (buttons & mask) ? 1 : 0;
}

bool joystick_xinput_available() {
    XINPUT_STATE state = {};
    return (XInputGetState(0, &state) == ERROR_SUCCESS);
}

bool joystick_xinput_init() {
    xinput_active = false;
    for (DWORD i = 0; i < MAX_XINPUT_CONTROLLERS; ++i) {
        XINPUT_STATE state = {};
        if (XInputGetState(i, &state) == ERROR_SUCCESS) {
            JOYSTICK_INFO& j = joy[i];
            j.flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_SIGNED;

            j.num_sticks = 2;
            j.stick[0].flags = j.stick[1].flags = j.flags;
            j.stick[0].num_axis = j.stick[1].num_axis = 2;
            j.stick[0].axis[0].name = "Left X";
            j.stick[0].axis[1].name = "Left Y";
            j.stick[0].name = "Left Stick";
            j.stick[1].axis[0].name = "Right X";
            j.stick[1].axis[1].name = "Right Y";
            j.stick[1].name = "Right Stick";

            j.num_buttons = 10;
            static const char* names[] = {
                "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LStick", "RStick"
            };
            for (int b = 0; b < j.num_buttons; ++b)
                j.button[b].name = names[b];

            num_joysticks = i + 1;
            xinput_active = true;
            controller_connected[i] = true;
        }
    }
    return xinput_active;
}

void joystick_xinput_exit() {
    xinput_active = false;
    num_joysticks = 0;
}
/*
int joystick_xinput_poll() {
    if (!xinput_active)
        return -1;

    for (DWORD i = 0; i < num_joysticks; ++i) {
        // Declare XINPUT_STATE before use
        XINPUT_STATE state{};
        bool was_connected = controller_connected[i];
        bool is_connected = (XInputGetState(i, &state) == ERROR_SUCCESS);

        if (is_connected && !was_connected) {
            controller_connected[i] = true;
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, true, nullptr);
        }
        else if (!is_connected && was_connected) {
            controller_connected[i] = false;
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, false, "Controller disconnected");
            continue;
        }

        if (!is_connected)
            continue;

        JOYSTICK_INFO& j = joy[i];

        auto scale = [](SHORT v) -> int {
            float fv = (v < 0 ? (v / 32768.0f) : (v / 32767.0f));
            return static_cast<int>(fv * 128);
            };

        // Left stick
        j.stick[0].axis[0].pos = scale(state.Gamepad.sThumbLX);
        j.stick[0].axis[1].pos = scale(state.Gamepad.sThumbLY);
        j.stick[0].axis[0].d1 = (j.stick[0].axis[0].pos < -64);
        j.stick[0].axis[0].d2 = (j.stick[0].axis[0].pos > 64);
        j.stick[0].axis[1].d1 = (j.stick[0].axis[1].pos < -64);
        j.stick[0].axis[1].d2 = (j.stick[0].axis[1].pos > 64);

        // Right stick
        j.stick[1].axis[0].pos = scale(state.Gamepad.sThumbRX);
        j.stick[1].axis[1].pos = scale(state.Gamepad.sThumbRY);
        j.stick[1].axis[0].d1 = (j.stick[1].axis[0].pos < -64);
        j.stick[1].axis[0].d2 = (j.stick[1].axis[0].pos > 64);
        j.stick[1].axis[1].d1 = (j.stick[1].axis[1].pos < -64);
        j.stick[1].axis[1].d2 = (j.stick[1].axis[1].pos > 64);

        // Buttons
        j.button[0].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_A);
        j.button[1].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_B);
        j.button[2].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_X);
        j.button[3].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_Y);
        j.button[4].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER);
        j.button[5].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER);
        j.button[6].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_BACK);
        j.button[7].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_START);
        j.button[8].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB);
        j.button[9].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB);
    }

    return 0;
}


int joystick_xinput_poll()
{
    if (!xinput_active)
        return -1;

    for (DWORD i = 0; i < MAX_JOYSTICKS; ++i)
    {
        XINPUT_STATE state = {};
        bool is_connected = (XInputGetState(i, &state) == ERROR_SUCCESS);
        bool was_connected = controller_connected[i];

        if (is_connected && !was_connected) {
            controller_connected[i] = true;
            last_state[i] = state;

            LOG_INFO("XInput controller %u connected", i);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, true, nullptr);
        }
        else if (!is_connected && was_connected) {
            controller_connected[i] = false;

            LOG_INFO("XInput controller %u disconnected", i);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, false, "Controller disconnected");
            continue;
        }

        if (!is_connected)
            continue;

        // Process joystick input (same as before)
        JOYSTICK_INFO& j = joy[i];

        auto scale = [](SHORT v) -> int {
            float fv = (v < 0 ? (v / 32768.0f) : (v / 32767.0f));
            return static_cast<int>(fv * 128);
            };

        j.stick[0].axis[0].pos = scale(state.Gamepad.sThumbLX);
        j.stick[0].axis[1].pos = scale(state.Gamepad.sThumbLY);
        j.stick[0].axis[0].d1 = (j.stick[0].axis[0].pos < -64);
        j.stick[0].axis[0].d2 = (j.stick[0].axis[0].pos > 64);
        j.stick[0].axis[1].d1 = (j.stick[0].axis[1].pos < -64);
        j.stick[0].axis[1].d2 = (j.stick[0].axis[1].pos > 64);

        j.stick[1].axis[0].pos = scale(state.Gamepad.sThumbRX);
        j.stick[1].axis[1].pos = scale(state.Gamepad.sThumbRY);
        j.stick[1].axis[0].d1 = (j.stick[1].axis[0].pos < -64);
        j.stick[1].axis[0].d2 = (j.stick[1].axis[0].pos > 64);
        j.stick[1].axis[1].d1 = (j.stick[1].axis[1].pos < -64);
        j.stick[1].axis[1].d2 = (j.stick[1].axis[1].pos > 64);

        // Buttons
        j.button[0].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_A);
        j.button[1].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_B);
        j.button[2].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_X);
        j.button[3].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_Y);
        j.button[4].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER);
        j.button[5].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER);
        j.button[6].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_BACK);
        j.button[7].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_START);
        j.button[8].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB);
        j.button[9].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB);
    }

    return 0;
}


int joystick_xinput_poll()
{
    bool any_connected = false;
    for (DWORD i = 0; i < MAX_XINPUT_CONTROLLERS; ++i)
    {
        XINPUT_STATE state = {};
        bool is_connected = (XInputGetState(i, &state) == ERROR_SUCCESS);
        bool was_connected = (xinput_state[i] == XINPUT_CONNECTED);

        if (!is_connected && was_connected) {
            xinput_state[i] = XINPUT_DISCONNECTED;
            LOG_INFO("XInput controller %u disconnected", i);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, false, "Controller disconnected");
            continue;
        }

        if (is_connected && !was_connected) {
            xinput_state[i] = XINPUT_CONNECTED;
            LOG_INFO("XInput controller %u connected", i);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, true, nullptr);
        }

        if (!is_connected)
            continue;


        // Process joystick input (same as before)
        JOYSTICK_INFO& j = joy[i];

        auto scale = [](SHORT v) -> int {
            float fv = (v < 0 ? (v / 32768.0f) : (v / 32767.0f));
            return static_cast<int>(fv * 128);
            };

        j.stick[0].axis[0].pos = scale(state.Gamepad.sThumbLX);
        j.stick[0].axis[1].pos = scale(state.Gamepad.sThumbLY);
        j.stick[0].axis[0].d1 = (j.stick[0].axis[0].pos < -64);
        j.stick[0].axis[0].d2 = (j.stick[0].axis[0].pos > 64);
        j.stick[0].axis[1].d1 = (j.stick[0].axis[1].pos < -64);
        j.stick[0].axis[1].d2 = (j.stick[0].axis[1].pos > 64);

        j.stick[1].axis[0].pos = scale(state.Gamepad.sThumbRX);
        j.stick[1].axis[1].pos = scale(state.Gamepad.sThumbRY);
        j.stick[1].axis[0].d1 = (j.stick[1].axis[0].pos < -64);
        j.stick[1].axis[0].d2 = (j.stick[1].axis[0].pos > 64);
        j.stick[1].axis[1].d1 = (j.stick[1].axis[1].pos < -64);
        j.stick[1].axis[1].d2 = (j.stick[1].axis[1].pos > 64);

        // Buttons
        j.button[0].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_A);
        j.button[1].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_B);
        j.button[2].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_X);
        j.button[3].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_Y);
        j.button[4].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER);
        j.button[5].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER);
        j.button[6].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_BACK);
        j.button[7].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_START);
        j.button[8].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB);
        j.button[9].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB);
    }

    xinput_active = any_connected;
    return 0;
}


int joystick_xinput_poll()
{
    if (!xinput_active)
        return -1;

    for (DWORD i = 0; i < MAX_XINPUT_CONTROLLERS; ++i)
    {
        XINPUT_STATE state = {};
        bool is_connected = (XInputGetState(i, &state) == ERROR_SUCCESS);
        bool was_connected = controller_connected[i];

       // LOG_INFO("XInput %u: is_connected %d", i, is_connected);
      //  LOG_INFO("XInput %u: was_connected %d", i, was_connected);

        // If controller state hasn't changed for a while, mark as stale
        if (is_connected)
        {
            if (state.dwPacketNumber != last_packet_number[i])
            {
                last_packet_number[i] = state.dwPacketNumber;
                stale_frame_count[i] = 0;
            }
            else
            {
                stale_frame_count[i]++;
                if (stale_frame_count[i] >= STALE_FRAME_THRESHOLD)
                {
                    if (was_connected)
                    {
                        controller_connected[i] = false;
                        LOG_INFO("XInput controller %u disconnected (stale)", i);
                        if (joystick_hotplug_callback)
                            joystick_hotplug_callback(i, false, "Controller timeout");
                    }
                    continue;
                }
            }
        }

        if (is_connected && !was_connected)
        {
            controller_connected[i] = true;
            last_packet_number[i] = state.dwPacketNumber;
            stale_frame_count[i] = 0;

            LOG_INFO("XInput controller %u connected", i);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, true, nullptr);
        }
        else if (!is_connected && was_connected)
        {
            controller_connected[i] = false;
            stale_frame_count[i] = 0;

            LOG_INFO("XInput controller %u disconnected", i);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, false, "Controller unplugged");
            continue;
        }

        if (!is_connected)
            continue;

        // Process joystick input
        JOYSTICK_INFO& j = joy[i];

        auto scale = [](SHORT v) -> int {
            float fv = (v < 0 ? (v / 32768.0f) : (v / 32767.0f));
            return static_cast<int>(fv * 128);
            };

        j.stick[0].axis[0].pos = scale(state.Gamepad.sThumbLX);
        j.stick[0].axis[1].pos = scale(state.Gamepad.sThumbLY);
        j.stick[0].axis[0].d1 = (j.stick[0].axis[0].pos < -64);
        j.stick[0].axis[0].d2 = (j.stick[0].axis[0].pos > 64);
        j.stick[0].axis[1].d1 = (j.stick[0].axis[1].pos < -64);
        j.stick[0].axis[1].d2 = (j.stick[0].axis[1].pos > 64);

        j.stick[1].axis[0].pos = scale(state.Gamepad.sThumbRX);
        j.stick[1].axis[1].pos = scale(state.Gamepad.sThumbRY);
        j.stick[1].axis[0].d1 = (j.stick[1].axis[0].pos < -64);
        j.stick[1].axis[0].d2 = (j.stick[1].axis[0].pos > 64);
        j.stick[1].axis[1].d1 = (j.stick[1].axis[1].pos < -64);
        j.stick[1].axis[1].d2 = (j.stick[1].axis[1].pos > 64);

        j.button[0].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_A);
        j.button[1].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_B);
        j.button[2].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_X);
        j.button[3].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_Y);
        j.button[4].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER);
        j.button[5].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER);
        j.button[6].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_BACK);
        j.button[7].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_START);
        j.button[8].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB);
        j.button[9].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB);
    }

    return 0;
}
*/
/*
int joystick_xinput_poll()
{
    if (!xinput_active)
        return -1;

    for (DWORD i = 0; i < MAX_XINPUT_CONTROLLERS; ++i)
    {
        XINPUT_STATE state = {};
        bool is_connected = (XInputGetState(i, &state) == ERROR_SUCCESS);
        bool was_connected = controller_connected[i];

        LOG_INFO("XInput %u: is_connected %d", i, is_connected);
        LOG_INFO("XInput %u: was_connected %d", i, was_connected);

        // Apply reconnect delay after disconnection
        if (reconnect_delay[i] > 0)
            reconnect_delay[i]--;

        // Check stale input
        if (is_connected)
        {
            if (state.dwPacketNumber != last_packet_number[i])
            {
                last_packet_number[i] = state.dwPacketNumber;
                stale_frame_count[i] = 0;
            }
            else
            {
                stale_frame_count[i]++;
                if (stale_frame_count[i] >= STALE_FRAME_THRESHOLD)
                {
                    if (was_connected)
                    {
                        controller_connected[i] = false;
                        reconnect_delay[i] = RECONNECT_COOLDOWN;
                        LOG_INFO("XInput controller %u disconnected (stale)", i);
                        if (joystick_hotplug_callback)
                            joystick_hotplug_callback(i, false, "Controller timeout");
                    }
                    continue;
                }
            }
        }

        // Handle new connection
        if (is_connected && !was_connected && reconnect_delay[i] == 0)
        {
            controller_connected[i] = true;
            last_packet_number[i] = state.dwPacketNumber;
            stale_frame_count[i] = 0;

            LOG_INFO("XInput controller %u connected", i);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, true, nullptr);
        }
        else if (!is_connected && was_connected)
        {
            controller_connected[i] = false;
            reconnect_delay[i] = RECONNECT_COOLDOWN;
            stale_frame_count[i] = 0;

            LOG_INFO("XInput controller %u disconnected", i);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, false, "Controller unplugged");
            continue;
        }

        if (!is_connected || reconnect_delay[i] > 0)
            continue;

        // Input processing...
        JOYSTICK_INFO& j = joy[i];

        auto scale = [](SHORT v) -> int {
            float fv = (v < 0 ? (v / 32768.0f) : (v / 32767.0f));
            return static_cast<int>(fv * 128);
            };

        j.stick[0].axis[0].pos = scale(state.Gamepad.sThumbLX);
        j.stick[0].axis[1].pos = scale(state.Gamepad.sThumbLY);
        j.stick[0].axis[0].d1 = (j.stick[0].axis[0].pos < -64);
        j.stick[0].axis[0].d2 = (j.stick[0].axis[0].pos > 64);
        j.stick[0].axis[1].d1 = (j.stick[0].axis[1].pos < -64);
        j.stick[0].axis[1].d2 = (j.stick[0].axis[1].pos > 64);

        j.stick[1].axis[0].pos = scale(state.Gamepad.sThumbRX);
        j.stick[1].axis[1].pos = scale(state.Gamepad.sThumbRY);
        j.stick[1].axis[0].d1 = (j.stick[1].axis[0].pos < -64);
        j.stick[1].axis[0].d2 = (j.stick[1].axis[0].pos > 64);
        j.stick[1].axis[1].d1 = (j.stick[1].axis[1].pos < -64);
        j.stick[1].axis[1].d2 = (j.stick[1].axis[1].pos > 64);

        j.button[0].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_A);
        j.button[1].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_B);
        j.button[2].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_X);
        j.button[3].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_Y);
        j.button[4].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER);
        j.button[5].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER);
        j.button[6].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_BACK);
        j.button[7].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_START);
        j.button[8].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB);
        j.button[9].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB);
    }

    return 0;
}

int joystick_xinput_poll()
{
    if (!xinput_active)
        return -1;

    for (DWORD i = 0; i < MAX_XINPUT_CONTROLLERS; ++i)
    {
        XINPUT_STATE state = {};
        bool is_connected = (XInputGetState(i, &state) == ERROR_SUCCESS);

        // Fire only on connect event
        if (is_connected && !was_connected[i] && !notify_connect[i]) {
            was_connected[i] = true;
            notify_connect[i] = true;
            notify_disconnect[i] = false;

            LOG_INFO("XInput controller %u connected", i);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, true, nullptr);
        }

        // Fire only on disconnect event
        else if (!is_connected && was_connected[i] && !notify_disconnect[i]) {
            was_connected[i] = false;
            notify_disconnect[i] = true;
            notify_connect[i] = false;

            LOG_INFO("XInput controller %u disconnected", i);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, false, "Controller disconnected");
        }

        // If not connected, skip input processing
        if (!is_connected)
            continue;

        // Process input
        JOYSTICK_INFO& j = joy[i];

        auto scale = [](SHORT v) -> int {
            float fv = (v < 0 ? (v / 32768.0f) : (v / 32767.0f));
            return static_cast<int>(fv * 128);
            };

        j.stick[0].axis[0].pos = scale(state.Gamepad.sThumbLX);
        j.stick[0].axis[1].pos = scale(state.Gamepad.sThumbLY);
        j.stick[0].axis[0].d1 = (j.stick[0].axis[0].pos < -64);
        j.stick[0].axis[0].d2 = (j.stick[0].axis[0].pos > 64);
        j.stick[0].axis[1].d1 = (j.stick[0].axis[1].pos < -64);
        j.stick[0].axis[1].d2 = (j.stick[0].axis[1].pos > 64);

        j.stick[1].axis[0].pos = scale(state.Gamepad.sThumbRX);
        j.stick[1].axis[1].pos = scale(state.Gamepad.sThumbRY);
        j.stick[1].axis[0].d1 = (j.stick[1].axis[0].pos < -64);
        j.stick[1].axis[0].d2 = (j.stick[1].axis[0].pos > 64);
        j.stick[1].axis[1].d1 = (j.stick[1].axis[1].pos < -64);
        j.stick[1].axis[1].d2 = (j.stick[1].axis[1].pos > 64);

        j.button[0].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_A);
        j.button[1].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_B);
        j.button[2].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_X);
        j.button[3].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_Y);
        j.button[4].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER);
        j.button[5].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER);
        j.button[6].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_BACK);
        j.button[7].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_START);
        j.button[8].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB);
        j.button[9].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB);
    }

    return 0;
}

int joystick_xinput_poll()
{
    if (!xinput_active)
        return -1;

    static bool notified_connected[MAX_XINPUT_CONTROLLERS] = {};
    static bool notified_disconnected[MAX_XINPUT_CONTROLLERS] = {};

    for (DWORD i = 0; i < MAX_XINPUT_CONTROLLERS; ++i)
    {
        XINPUT_STATE state = {};
        bool is_connected = (XInputGetState(i, &state) == ERROR_SUCCESS);
        bool was_connected = controller_connected[i];

        // Handle stale input (no activity for N frames)
        if (is_connected)
        {
            if (state.dwPacketNumber != last_packet_number[i])
            {
                last_packet_number[i] = state.dwPacketNumber;
                stale_frame_count[i] = 0;
            }
            else
            {
                stale_frame_count[i]++;
                if (stale_frame_count[i] >= STALE_FRAME_THRESHOLD)
                {
                    if (was_connected)
                    {
                        controller_connected[i] = false;

                        if (!notified_disconnected[i])
                        {
                            LOG_INFO("XInput controller %u disconnected (stale)", i);
                            if (joystick_hotplug_callback)
                                joystick_hotplug_callback(i, false, "Controller timeout");
                            notified_disconnected[i] = true;
                            notified_connected[i] = false;
                        }
                    }
                    continue;
                }
            }
        }

        // Handle hardware unplug event
        if (!is_connected && was_connected)
        {
            controller_connected[i] = false;
            stale_frame_count[i] = 0;

            if (!notified_disconnected[i])
            {
                LOG_INFO("XInput controller %u disconnected", i);
                if (joystick_hotplug_callback)
                    joystick_hotplug_callback(i, false, "Controller unplugged");
                notified_disconnected[i] = true;
                notified_connected[i] = false;
            }
            continue;
        }

        // Handle plug-in event
        if (is_connected && !was_connected)
        {
            controller_connected[i] = true;
            last_packet_number[i] = state.dwPacketNumber;
            stale_frame_count[i] = 0;

            if (!notified_connected[i])
            {
                LOG_INFO("XInput controller %u connected", i);
                if (joystick_hotplug_callback)
                    joystick_hotplug_callback(i, true, nullptr);
                notified_connected[i] = true;
                notified_disconnected[i] = false;
            }
        }

        if (!is_connected)
            continue;

        // Process joystick input
        JOYSTICK_INFO& j = joy[i];

        auto scale = [](SHORT v) -> int {
            float fv = (v < 0 ? (v / 32768.0f) : (v / 32767.0f));
            return static_cast<int>(fv * 128);
            };

        j.stick[0].axis[0].pos = scale(state.Gamepad.sThumbLX);
        j.stick[0].axis[1].pos = scale(state.Gamepad.sThumbLY);
        j.stick[0].axis[0].d1 = (j.stick[0].axis[0].pos < -64);
        j.stick[0].axis[0].d2 = (j.stick[0].axis[0].pos > 64);
        j.stick[0].axis[1].d1 = (j.stick[0].axis[1].pos < -64);
        j.stick[0].axis[1].d2 = (j.stick[0].axis[1].pos > 64);

        j.stick[1].axis[0].pos = scale(state.Gamepad.sThumbRX);
        j.stick[1].axis[1].pos = scale(state.Gamepad.sThumbRY);
        j.stick[1].axis[0].d1 = (j.stick[1].axis[0].pos < -64);
        j.stick[1].axis[0].d2 = (j.stick[1].axis[0].pos > 64);
        j.stick[1].axis[1].d1 = (j.stick[1].axis[1].pos < -64);
        j.stick[1].axis[1].d2 = (j.stick[1].axis[1].pos > 64);

        j.button[0].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_A);
        j.button[1].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_B);
        j.button[2].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_X);
        j.button[3].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_Y);
        j.button[4].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER);
        j.button[5].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER);
        j.button[6].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_BACK);
        j.button[7].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_START);
        j.button[8].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB);
        j.button[9].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB);
    }

    return 0;
}
*/
/*
int joystick_xinput_poll()
{
    if (!xinput_active)
        return -1;

    static bool notified_connected[MAX_XINPUT_CONTROLLERS] = {};
    static bool notified_disconnected[MAX_XINPUT_CONTROLLERS] = {};

    for (DWORD i = 0; i < MAX_XINPUT_CONTROLLERS; ++i)
    {
        XINPUT_STATE state = {};
        bool is_connected = (XInputGetState(i, &state) == ERROR_SUCCESS);
        bool was_connected = controller_connected[i];

        if (is_connected)
        {
            if (state.dwPacketNumber != last_packet_number[i])
            {
                last_packet_number[i] = state.dwPacketNumber;
                stale_frame_count[i] = 0;
            }
            else
            {
                stale_frame_count[i]++;
            }

            if (stale_frame_count[i] < STALE_FRAME_THRESHOLD)
            {
                controller_connected[i] = true;

                if (!notified_connected[i])
                {
                    LOG_INFO("XInput controller %u connected", i);
                    if (joystick_hotplug_callback)
                        joystick_hotplug_callback(i, true, nullptr);
                    notified_connected[i] = true;
                    notified_disconnected[i] = false;
                }
            }
            else
            {
                if (!notified_disconnected[i])
                {
                    controller_connected[i] = false;
                    LOG_INFO("XInput controller %u disconnected (stale)", i);
                    if (joystick_hotplug_callback)
                        joystick_hotplug_callback(i, false, "Controller timeout");
                    notified_disconnected[i] = true;
                    notified_connected[i] = false;
                }
                continue;
            }
        }
        else // XInputGetState failed
        {
            stale_frame_count[i] = 0;

            if (controller_connected[i])
            {
                controller_connected[i] = false;

                if (!notified_disconnected[i])
                {
                    LOG_INFO("XInput controller %u disconnected", i);
                    if (joystick_hotplug_callback)
                        joystick_hotplug_callback(i, false, "Controller unplugged");
                    notified_disconnected[i] = true;
                    notified_connected[i] = false;
                }
            }

            continue;
        }

        // Process joystick input
        JOYSTICK_INFO& j = joy[i];

        auto scale = [](SHORT v) -> int {
            float fv = (v < 0 ? (v / 32768.0f) : (v / 32767.0f));
            return static_cast<int>(fv * 128);
            };

        j.stick[0].axis[0].pos = scale(state.Gamepad.sThumbLX);
        j.stick[0].axis[1].pos = scale(state.Gamepad.sThumbLY);
        j.stick[0].axis[0].d1 = (j.stick[0].axis[0].pos < -64);
        j.stick[0].axis[0].d2 = (j.stick[0].axis[0].pos > 64);
        j.stick[0].axis[1].d1 = (j.stick[0].axis[1].pos < -64);
        j.stick[0].axis[1].d2 = (j.stick[0].axis[1].pos > 64);

        j.stick[1].axis[0].pos = scale(state.Gamepad.sThumbRX);
        j.stick[1].axis[1].pos = scale(state.Gamepad.sThumbRY);
        j.stick[1].axis[0].d1 = (j.stick[1].axis[0].pos < -64);
        j.stick[1].axis[0].d2 = (j.stick[1].axis[0].pos > 64);
        j.stick[1].axis[1].d1 = (j.stick[1].axis[1].pos < -64);
        j.stick[1].axis[1].d2 = (j.stick[1].axis[1].pos > 64);

        j.button[0].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_A);
        j.button[1].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_B);
        j.button[2].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_X);
        j.button[3].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_Y);
        j.button[4].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER);
        j.button[5].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER);
        j.button[6].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_BACK);
        j.button[7].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_START);
        j.button[8].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB);
        j.button[9].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB);
    }

    return 0;
}



int joystick_xinput_poll()
{
    if (!xinput_active)
        return -1;

    for (DWORD i = 0; i < MAX_XINPUT_CONTROLLERS; ++i)
    {
        XINPUT_STATE state = {};
        bool is_connected = (XInputGetState(i, &state) == ERROR_SUCCESS);
        bool was_connected = controller_connected[i];

        // LOG_INFO("XInput %u: is_connected %d", i, is_connected);
       //  LOG_INFO("XInput %u: was_connected %d", i, was_connected);

         // If controller state hasn't changed for a while, mark as stale
        if (is_connected)
        {
            if (state.dwPacketNumber != last_packet_number[i])
            {
                last_packet_number[i] = state.dwPacketNumber;
                stale_frame_count[i] = 0;
            }
            else
            {
                stale_frame_count[i]++;
                if (stale_frame_count[i] >= STALE_FRAME_THRESHOLD)
                {
                    if (was_connected)
                    {
                        controller_connected[i] = false;

                        LOG_INFO("XInput controller %u disconnected (stale)", i);
                        if (joystick_hotplug_callback)
                            joystick_hotplug_callback(i, false, "Controller timeout");
                    }
                    continue;
                }
            }
        }

        if (is_connected && !was_connected)
        {
            controller_connected[i] = true;
            last_packet_number[i] = state.dwPacketNumber;
            stale_frame_count[i] = 0;

            LOG_INFO("XInput controller %u connected", i);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, true, nullptr);
        }
        else if (!is_connected && was_connected)
        {
            controller_connected[i] = false;
            stale_frame_count[i] = 0;

            LOG_INFO("XInput controller %u disconnected", i);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, false, "Controller unplugged");
            continue;
        }

        if (!is_connected)
            continue;

        // Process joystick input
        JOYSTICK_INFO& j = joy[i];

        auto scale = [](SHORT v) -> int {
            float fv = (v < 0 ? (v / 32768.0f) : (v / 32767.0f));
            return static_cast<int>(fv * 128);
            };

        j.stick[0].axis[0].pos = scale(state.Gamepad.sThumbLX);
        j.stick[0].axis[1].pos = scale(state.Gamepad.sThumbLY);
        j.stick[0].axis[0].d1 = (j.stick[0].axis[0].pos < -64);
        j.stick[0].axis[0].d2 = (j.stick[0].axis[0].pos > 64);
        j.stick[0].axis[1].d1 = (j.stick[0].axis[1].pos < -64);
        j.stick[0].axis[1].d2 = (j.stick[0].axis[1].pos > 64);

        j.stick[1].axis[0].pos = scale(state.Gamepad.sThumbRX);
        j.stick[1].axis[1].pos = scale(state.Gamepad.sThumbRY);
        j.stick[1].axis[0].d1 = (j.stick[1].axis[0].pos < -64);
        j.stick[1].axis[0].d2 = (j.stick[1].axis[0].pos > 64);
        j.stick[1].axis[1].d1 = (j.stick[1].axis[1].pos < -64);
        j.stick[1].axis[1].d2 = (j.stick[1].axis[1].pos > 64);

        j.button[0].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_A);
        j.button[1].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_B);
        j.button[2].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_X);
        j.button[3].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_Y);
        j.button[4].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER);
        j.button[5].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER);
        j.button[6].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_BACK);
        j.button[7].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_START);
        j.button[8].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB);
        j.button[9].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB);
    }

    return 0;
}
*/
int joystick_xinput_poll() {
   // letstryagain_joystick();
    
    if (!xinput_active)
        return -1;

    for (int i = 0; i < num_joysticks; ++i) {
        // Declare XINPUT_STATE before use
        XINPUT_STATE state{};
        bool was_connected = controller_connected[i];
        is_connected = (XInputGetState(i, &state) == ERROR_SUCCESS);

       // LOG_INFO("Controller %d is_connected %d", i, is_connected);
             
        if (is_connected && !was_connected) {
            controller_connected[i] = true;
            LOG_INFO("Controller %d is_connected %d", i, controller_connected[i]);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, true, nullptr);
        }
        else if (!is_connected && was_connected) {
            controller_connected[i] = false;
            LOG_INFO("Controller %d is disconnected %d", i, controller_connected[i]);
            if (joystick_hotplug_callback)
                joystick_hotplug_callback(i, false, "Controller disconnected");
            continue;
        }

        if (!is_connected)
            continue;

        JOYSTICK_INFO& j = joy[i];

        auto scale = [](SHORT v) -> int {
            float fv = (v < 0 ? (v / 32768.0f) : (v / 32767.0f));
            return static_cast<int>(fv * 128);
            };

        // Left stick (flip Y)
        j.stick[0].axis[0].pos = scale(state.Gamepad.sThumbLX);
        j.stick[0].axis[1].pos = -scale(state.Gamepad.sThumbLY);
        j.stick[0].axis[0].d1 = (j.stick[0].axis[0].pos < -64);
        j.stick[0].axis[0].d2 = (j.stick[0].axis[0].pos > 64);
        j.stick[0].axis[1].d1 = (j.stick[0].axis[1].pos < -64);
        j.stick[0].axis[1].d2 = (j.stick[0].axis[1].pos > 64);

        // Right stick (flip Y)
        j.stick[1].axis[0].pos = scale(state.Gamepad.sThumbRX);
        j.stick[1].axis[1].pos = -scale(state.Gamepad.sThumbRY);
        j.stick[1].axis[0].d1 = (j.stick[1].axis[0].pos < -64);
        j.stick[1].axis[0].d2 = (j.stick[1].axis[0].pos > 64);
        j.stick[1].axis[1].d1 = (j.stick[1].axis[1].pos < -64);
        j.stick[1].axis[1].d2 = (j.stick[1].axis[1].pos > 64);

        // Buttons
        j.button[0].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_A);
        j.button[1].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_B);
        j.button[2].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_X);
        j.button[3].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_Y);
        j.button[4].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_SHOULDER);
        j.button[5].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_SHOULDER);
        j.button[6].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_BACK);
        j.button[7].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_START);
        j.button[8].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_LEFT_THUMB);
        j.button[9].b = xinput_map_button(state.Gamepad.wButtons, XINPUT_GAMEPAD_RIGHT_THUMB);
    }

    return 0;
}

