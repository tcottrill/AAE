//RawInput.h
//Original code by Jay Tennant 6/30/10
//performs initialization on raw input for testing purposes
//to replace DXTestInput.h, which used DirectInput
//Modified 2/12 aae:
// Added: Allergo 4 Compatible KeyCodes
// Added: Allegro 4 Compatible Mouse Operations
// Added: Advanced key decoding from https://blog.molecular-matters.com/2011/09/05/properly-handling-keyboard-input/
// This code does not currently handle multiple mice/keyboards.
// Cleaned up and modernized 6/15/2025 - TC
// Currently the mouse return values are doubled, that is for a special application, you will want to change:
// *mickeyx = temp_x * 2;
// *mickeyy = temp_y * 2;
// to
// *mickeyx = temp_x;
// *mickeyy = temp_y;
#pragma once

#include <windows.h>

#define bset(p,m) ((p) |= (m))
#define bclr(p,m) ((p) &= ~(m))

#define KEY_A                 0x41  //65
#define KEY_B                 0x42
#define KEY_C                 0x43
#define KEY_D                 0x44
#define KEY_E                 0x45
#define KEY_F                 0x46
#define KEY_G                 0x47
#define KEY_H                 0x48
#define KEY_I                 0x49
#define KEY_J                 0x4a
#define KEY_K                 0x4b
#define KEY_L                 0x4c
#define KEY_M                 0x4d
#define KEY_N                 0x4e
#define KEY_O                 0x4f
#define KEY_P                 0x50
#define KEY_Q                 0x51
#define KEY_R                 0x52
#define KEY_S                 0x53
#define KEY_T                 0x54
#define KEY_U                 0x55
#define KEY_V                 0x56
#define KEY_W                 0x57
#define KEY_X                 0x58
#define KEY_Y                 0x59
#define KEY_Z                 0x5a
#define KEY_0                 0x30  //48
#define KEY_1                 0x31
#define KEY_2                 0x32
#define KEY_3                 0x33
#define KEY_4                 0x34
#define KEY_5                 0x35
#define KEY_6                 0x36
#define KEY_7                 0x37
#define KEY_8                 0x38
#define KEY_9                 0x39
#define KEY_0_PAD             VK_NUMPAD0
#define KEY_1_PAD             VK_NUMPAD1
#define KEY_2_PAD             VK_NUMPAD2
#define KEY_3_PAD             VK_NUMPAD3
#define KEY_4_PAD             VK_NUMPAD4
#define KEY_5_PAD             VK_NUMPAD5
#define KEY_6_PAD             VK_NUMPAD6
#define KEY_7_PAD             VK_NUMPAD7
#define KEY_8_PAD             VK_NUMPAD8
#define KEY_9_PAD             VK_NUMPAD9
#define KEY_F1                VK_F1
#define KEY_F2                VK_F2
#define KEY_F3                VK_F3
#define KEY_F4                VK_F4
#define KEY_F5                VK_F5
#define KEY_F6                VK_F6
#define KEY_F7                VK_F7
#define KEY_F8                VK_F8
#define KEY_F9                VK_F9
#define KEY_F10               VK_F10
#define KEY_F11               VK_F11
#define KEY_F12               VK_F12
#define KEY_ESC               VK_ESCAPE
#define KEY_TILDE             0xc0
#define KEY_MINUS             0xbd
#define KEY_EQUALS            0xbb
#define KEY_BACKSPACE         VK_BACK
#define KEY_TAB               VK_TAB
#define KEY_OPENBRACE         0xdb
#define KEY_CLOSEBRACE        0xdd
#define KEY_ENTER             VK_RETURN
#define KEY_COLON             0xba
#define KEY_QUOTE             0xde
#define KEY_BACKSLASH         0xdc
#define KEY_BACKSLASH2        0xdc
#define KEY_COMMA             0xbc
#define KEY_STOP              0xbe
#define KEY_SLASH             0xbf
#define KEY_SPACE             VK_SPACE
#define KEY_INSERT            VK_INSERT
#define KEY_DEL               VK_DELETE
#define KEY_HOME              VK_HOME
#define KEY_END               VK_END
#define KEY_PGUP              VK_PRIOR
#define KEY_PGDN              VK_NEXT
#define KEY_LEFT              VK_LEFT
#define KEY_RIGHT             VK_RIGHT
#define KEY_UP                VK_UP
#define KEY_DOWN              VK_DOWN
#define KEY_SLASH_PAD         VK_DIVIDE
#define KEY_ASTERISK          VK_MULTIPLY
#define KEY_MINUS_PAD         VK_SUBTRACT
#define KEY_PLUS_PAD          VK_ADD
#define KEY_DEL_PAD           VK_DECIMAL
#define KEY_ENTER_PAD         VK_SEPARATOR
#define KEY_PRTSCR            VK_SNAPSHOT
#define KEY_PAUSE             VK_PAUSE
#define KEY_ABNT_C1           0xc1
#define KEY_YEN               125
#define KEY_KANA              VK_KANA
#define KEY_CONVERT           121
#define KEY_NOCONVERT         123
#define KEY_AT                145
#define KEY_CIRCUMFLEX        144
#define KEY_COLON2            146
#define KEY_KANJI             148
#define KEY_EQUALS_PAD        0x00
#define KEY_BACKQUOTE         192
#define KEY_SEMICOLON         0xba
#define KEY_LSHIFT            VK_LSHIFT
#define KEY_RSHIFT            VK_RSHIFT
#define KEY_LCONTROL          VK_LCONTROL
#define KEY_RCONTROL          VK_RCONTROL
#define KEY_ALT               VK_LMENU
#define KEY_LMENU             VK_LMENU
#define KEY_RMENU             VK_RMENU
#define KEY_ALTGR             VK_RMENU
#define KEY_LWIN              VK_LWIN
#define KEY_RWIN              VK_RWIN
#define KEY_MENU              VK_MENU
#define KEY_SCRLOCK           VK_SCROLL
#define KEY_NUMLOCK           VK_NUMLOCK
#define KEY_CAPSLOCK          VK_CAPITAL

#define KEY_MAX               0xEF  //127 Not!

#define toUpper(ch) ((ch >= 'a' && ch <='z') ? ch & 0x5f : ch)
#define RI_MOUSE_HWHEEL 0x0800

//Allegro compatible C style keystate buffers.
extern int mouse_b;
extern unsigned char key[256];
//registers a mouse and keyboard for raw input;
HRESULT RawInput_Initialize(HWND hWnd);

//processes WM_INPUT messages
LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam);

//keyboard state checks
int isKeyHeld(INT vkCode);
bool IsKeyDown(INT vkCode);
bool IsKeyUp(INT vkCode);

//summed mouse state checks/sets;
//use as convenience, ie. keeping track of movements without needing to maintain separate data set
//Added for Allegro Code Compatibility
void get_mouse_win(int* mickeyx, int* mickeyy);
void get_mouse_mickeys(int* mickeyx, int* mickeyy);
LONG GetMouseX();
LONG GetMouseY();
LONG GetMouseWheel();
void SetMouseX(LONG x);
void SetMouseY(LONG y);
void SetMouseWheel(LONG wheel);

//relative mouse state changes
LONG GetMouseXChange();
LONG GetMouseYChange();
LONG GetMouseWheelChange();

//mouse button state checks
bool IsMouseLButtonDown();
bool IsMouseLButtonUp();
bool IsMouseRButtonDown();
bool IsMouseRButtonUp();
bool IsMouseMButtonDown();
bool IsMouseMButtonUp();

void test_clr();
