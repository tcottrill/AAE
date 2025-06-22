//Some code for this taken from
//https://blog.molecular-matters.com/2011/09/05/properly-handling-keyboard-input/
//

#include "rawinput.h"
#include "log.h"

/* Forces RAWINPUTDEVICE and related Win32 APIs to be visible.
 * Only compatible with WIndows XP and above. */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501

char buf[256];
HWND windowHandle;
unsigned char key[256];
unsigned int lastkey[256];
int mouse_b;

struct DXTI_MOUSE_STATE
{
	long x, y, wheel; //current position
	long dx, dy, dwheel; //change in position
	bool left, middle, right; //buttons
};

enum DXTI_MOUSE_BUTTON_STATE //named state of mouse buttons
{
	UP = FALSE,
	DOWN = TRUE,
};
struct DXTI_MOUSE_STATE m_mouseStateRaw;

HRESULT RawInput_Initialize(HWND hWnd)
{
	RAWINPUTDEVICE Rid[2];

	Rid[0].usUsagePage = 0x01;
	Rid[0].usUsage = 0x02;
	Rid[0].dwFlags = 0;			//RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE | RIDEV_INPUTSINK;
	Rid[0].hwndTarget = hWnd;

	Rid[1].usUsagePage = 0x01;
	Rid[1].usUsage = 0x06;
	Rid[1].dwFlags = 0;
	Rid[1].hwndTarget = hWnd;

	ZeroMemory(key, sizeof(key));
	ZeroMemory(lastkey, sizeof(lastkey));
	// ZeroMemory(&m_mouseState, sizeof(m_mouseState));
	ZeroMemory(&m_mouseStateRaw, sizeof(m_mouseStateRaw));

	ShowCursor(TRUE);
	windowHandle = hWnd;
	if (FALSE == RegisterRawInputDevices(Rid, 2, sizeof(Rid[0]))) //registers both mouse and keyboard
		return E_FAIL;

	return S_OK;
}

LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
	RAWINPUT input;
	UINT size = sizeof(input);

	GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));

	if (input.header.dwType == RIM_TYPEKEYBOARD) {
		auto& kbd = input.data.keyboard;
		UINT virtualKey = kbd.VKey;
		UINT scanCode = kbd.MakeCode;
		UINT flags = kbd.Flags;

		if (virtualKey == 255) return 0;

		if (virtualKey == VK_SHIFT)
			virtualKey = MapVirtualKey(scanCode, MAPVK_VSC_TO_VK_EX);
		else if (virtualKey == VK_NUMLOCK)
			scanCode |= 0x100;

		// e0 and e1 are escape sequences used for certain special keys, such as PRINT and PAUSE/BREAK.
		// see http://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html
		const bool isE0 = (flags & RI_KEY_E0);
		const bool isE1 = (flags & RI_KEY_E1);

		if (isE1 && virtualKey == VK_PAUSE)
			scanCode = 0x45;
		else if (isE1)
			scanCode = MapVirtualKey(virtualKey, MAPVK_VK_TO_VSC);

		switch (virtualKey) {
		case VK_CONTROL: virtualKey = isE0 ? VK_RCONTROL : VK_LCONTROL; break;
		case VK_MENU: virtualKey = isE0 ? VK_RMENU : VK_LMENU; break;
		case VK_RETURN: if (isE0) virtualKey = VK_SEPARATOR; break;
		case VK_INSERT: if (!isE0) virtualKey = VK_NUMPAD0; break;
		case VK_DELETE: if (!isE0) virtualKey = VK_DECIMAL; break;
		case VK_HOME: if (!isE0) virtualKey = VK_NUMPAD7; break;
		case VK_END: if (!isE0) virtualKey = VK_NUMPAD1; break;
		case VK_PRIOR: if (!isE0) virtualKey = VK_NUMPAD9; break;
		case VK_NEXT: if (!isE0) virtualKey = VK_NUMPAD3; break;
		case VK_LEFT: if (!isE0) virtualKey = VK_NUMPAD4; break;
		case VK_RIGHT: if (!isE0) virtualKey = VK_NUMPAD6; break;
		case VK_UP: if (!isE0) virtualKey = VK_NUMPAD8; break;
		case VK_DOWN: if (!isE0) virtualKey = VK_NUMPAD2; break;
		case VK_CLEAR: if (!isE0) virtualKey = VK_NUMPAD5; break;
		}

		if (kbd.Flags & RI_KEY_BREAK) {
			key[virtualKey] = 0;
			lastkey[virtualKey] = 0;
		}
		else {
			key[virtualKey] = 1;
			lastkey[virtualKey] = (lastkey[virtualKey] + 1) % 0xFFFFFFFF;
			if (lastkey[virtualKey] == 0) lastkey[virtualKey] = 1;
		}
	}
	else if (input.header.dwType == RIM_TYPEMOUSE)
	{
		m_mouseStateRaw.dx = input.data.mouse.lLastX;
		m_mouseStateRaw.dy = input.data.mouse.lLastY;

		m_mouseStateRaw.x += m_mouseStateRaw.dx;
		m_mouseStateRaw.y += m_mouseStateRaw.dy;

		switch (input.data.mouse.usButtonFlags)
		{
		case RI_MOUSE_LEFT_BUTTON_DOWN:
			m_mouseStateRaw.left = DOWN;
			break;
		case RI_MOUSE_LEFT_BUTTON_UP:
			m_mouseStateRaw.left = UP;
			break;
		case RI_MOUSE_RIGHT_BUTTON_DOWN:
			m_mouseStateRaw.right = DOWN;
			break;
		case RI_MOUSE_RIGHT_BUTTON_UP:
			m_mouseStateRaw.right = UP;
			break;
		case RI_MOUSE_MIDDLE_BUTTON_DOWN:
			m_mouseStateRaw.middle = DOWN;
			break;
		case RI_MOUSE_MIDDLE_BUTTON_UP:
			m_mouseStateRaw.middle = UP;
			break;
		case RI_MOUSE_WHEEL:
			m_mouseStateRaw.dwheel += input.data.mouse.usButtonData;
			break;
		}

		// Allegro Mouse button support.
		if (m_mouseStateRaw.left)   bset(mouse_b, 0x01); else  bclr(mouse_b, 0x01);
		if (m_mouseStateRaw.right)  bset(mouse_b, 0x02); else  bclr(mouse_b, 0x02);
		if (m_mouseStateRaw.middle) bset(mouse_b, 0x04); else  bclr(mouse_b, 0x04);
	}

	return 0;
	//DefWindowProc(hWnd, WM_INPUT, wParam, lParam);
}

void test_clr()
{
	char buf[256];
	SecureZeroMemory(buf, 256);
	SecureZeroMemory(key, 256);
	//SecureZeroMemory(lastkey, 256);
}

// Function to get the window size
void getWindowSize(int* width, int* height) {
	RECT rect;
	GetClientRect(windowHandle, &rect);
	*width = rect.right - rect.left;
	*height = rect.bottom - rect.top;
}

void get_mouse_win(int* mickeyx, int* mickeyy)
{
	POINT cursor_pos;
	GetCursorPos(&cursor_pos);
	ScreenToClient(windowHandle, (LPPOINT)&cursor_pos);
	*mickeyx = cursor_pos.x;
	*mickeyy = cursor_pos.y;
}

void get_mouse_mickeys(int* mickeyx, int* mickeyy)
{
	static RECT windowRect;

	int temp_x = m_mouseStateRaw.dx;
	int temp_y = m_mouseStateRaw.dy;

	/*
	// Get current window dimensions
	GetClientRect(ihwnd, &windowRect);
	int width = windowRect.right - windowRect.left;
	int height = windowRect.bottom - windowRect.top;

	// Scale the raw input to the window size
	int scaledX = (temp_x * 4096) / width;  //4096
	int scaledY = temp_y * 4096 / height;   // 4096
	*/

	m_mouseStateRaw.dx -= temp_x;
	m_mouseStateRaw.dy -= temp_y;

	*mickeyx = temp_x * 2;
	*mickeyy = temp_y * 2;
}

//keyboard state checks
int isKeyHeld(INT vkCode) { return lastkey[vkCode]; }
bool IsKeyDown(INT vkCode) { return key[vkCode & 0xff] & 0x80 ? TRUE : FALSE; }
bool IsKeyUp(INT vkCode) { return  key[vkCode & 0xff] & 0x80 ? FALSE : TRUE; }

//summed mouse state checks/sets;
//use as convenience, ie. keeping track of movements without needing to maintain separate data set
//naming is left to C style for compatibility

void get_mouse_mickeys(int* mickeyx, int* mickeyy);
LONG GetMouseX() { return m_mouseStateRaw.x; }
LONG GetMouseY() { return m_mouseStateRaw.y; }
LONG GetMouseWheel() { return m_mouseStateRaw.wheel; }
void SetMouseX(LONG x) { m_mouseStateRaw.x = x; }
void SetMouseY(LONG y) { m_mouseStateRaw.y = y; }
void SetMouseWheel(LONG wheel) { m_mouseStateRaw.wheel = wheel; }

//relative mouse state changes
LONG GetMouseXChange() { return m_mouseStateRaw.dx; }
LONG GetMouseYChange() { return m_mouseStateRaw.dy; }
LONG GetMouseWheelChange() { return m_mouseStateRaw.dwheel; }

//mouse button state checks
bool IsMouseLButtonDown() { return (m_mouseStateRaw.left == DOWN) ? TRUE : FALSE; }
bool IsMouseLButtonUp() { return (m_mouseStateRaw.left == UP) ? TRUE : FALSE; }
bool IsMouseRButtonDown() { return (m_mouseStateRaw.right == DOWN) ? TRUE : FALSE; }
bool IsMouseRButtonUp() { return (m_mouseStateRaw.right == UP) ? TRUE : FALSE; }
bool IsMouseMButtonDown() { return (m_mouseStateRaw.middle == DOWN) ? TRUE : FALSE; }
bool IsMouseMButtonUp() { return (m_mouseStateRaw.middle == UP) ? TRUE : FALSE; }