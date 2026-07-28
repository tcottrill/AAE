//Note: Updated bad mouse handling code. 8/5/25
// Updated 2/28/26 to support Lost Focus and Pausing Input.

#include "rawinput_win32.h"
#include "sys_log.h"
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdio>   // snprintf
#include <cstring>  // strstr
#include <new>      // std::nothrow

/* Forces RAWINPUTDEVICE and related Win32 APIs to be visible.
 * Only compatible with WIndows XP and above. */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501

 // GLFW-style modifier flags -- use the RI_MOD_* values from the header
 // to keep internal and public modifier naming consistent.
 // (RI_MOD_SHIFT=0x01, RI_MOD_CONTROL=0x02, RI_MOD_ALT=0x04, RI_MOD_SUPER=0x08)

HWND windowHandle;
unsigned char key[256];
unsigned int lastkey[256];
int mouse_b;

// This defaults to 1x for AAE - To be removed..
static float g_mouseScale = 1.0f;

// --------------------
// Thread management
// --------------------
static std::thread inputThread;
static std::mutex inputQueueMutex;
static std::condition_variable inputCV;
static std::atomic<bool> inputThreadExit{ false };
static std::atomic<bool> inputThreadRun{ false };
static std::queue<RAWINPUT> inputQueue;

// --------------------
// Forward declarations
// --------------------
static void RawInput_ProcessInternal(const RAWINPUT& input);
static void input_thread_func();

// -----------------------------------------------------------------------------
// set_mouse_mickey_scale
// Description:
//   Sets the scaling multiplier applied to relative mouse motion (mickeys).
// -----------------------------------------------------------------------------
void set_mouse_mickey_scale(float scale) {
	g_mouseScale = scale;
}

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

// --------------------
// Per-device mouse state (multi-mouse). The merged m_mouseStateRaw above
// still accumulates everything, so all legacy single-mouse callers are
// unaffected. Writers: worker thread; readers: main thread -- the small
// mutex covers the accumulate and the read-and-reset.
// --------------------
struct RI_MOUSE_DEV
{
	HANDLE handle;
	long dx, dy, dwheel;
	int  buttons;          // bit0=left, bit1=right, bit2=middle
	int  seen;             // 1 once the device has actually sent input --
	                       // distinguishes real mice from phantom mouse
	                       // collections on composite keyboards
	char name[64];         // friendly display name
	char path[260];        // full raw device path -- the STABLE identity
	                       // (contains the physical USB port chain); used
	                       // to persist player assignments across reboots
};
static RI_MOUSE_DEV g_mice[RI_MAX_MICE];
static int g_numMice = 0;
static std::mutex g_miceMutex;

// --------------------
// Per-device keyboard state (multi-keyboard). Same model as the mice: the
// merged key[] above is still written for every event, so all legacy
// consumers (menus, hotkeys, UI) are unaffected. Key bytes are written by
// the worker thread and read by the main thread (natural-width writes, same
// policy as the merged key[]); the mutex guards slot lookup/registration.
// --------------------
struct RI_KBD_DEV
{
	HANDLE handle;
	int  seen;                 // 1 once the device has actually sent a key
	unsigned char key[256];    // VK-indexed, same fixups as the merged key[]
	char name[64];             // friendly display name
	char path[260];            // full raw device path (stable identity)
};
static RI_KBD_DEV g_kbds[RI_MAX_KBDS];
static int g_numKbds = 0;
static std::mutex g_kbdsMutex;

// Derive a short, stable display name from the Raw Input device path,
// e.g. "\\?\HID#VID_046D&PID_C077#..." -> "HID#VID_046D&PID_C077".
static void ri_short_device_name(HANDLE hDevice, char* out, size_t outlen)
{
	char path[256] = { 0 };
	UINT sz = sizeof(path) - 1;
	out[0] = 0;

	if (GetRawInputDeviceInfoA(hDevice, RIDI_DEVICENAME, path, &sz) == (UINT)-1 || !path[0])
	{
		snprintf(out, outlen, "MOUSE");
		return;
	}

	const char* p = path;
	if (p[0] == '\\' && p[1] == '\\' && p[2] == '?' && p[3] == '\\')
		p += 4;

	// copy up to (and including) the second '#'-separated segment
	size_t o = 0;
	int hashes = 0;
	for (; *p && o < outlen - 1; p++)
	{
		if (*p == '#' && ++hashes >= 2) break;
		out[o++] = *p;
	}
	out[o] = 0;
	if (!out[0]) snprintf(out, outlen, "MOUSE");
}

// True for the Windows-generated Terminal Services / RDP redirected input
// devices ("\\?\Root#RDP_MOU#0000#...", RDP_KBD for keyboards). They exist
// on most systems even without a remote session and never carry local
// input, so they are excluded from the device list.
static bool ri_is_rdp_device(HANDLE hDevice)
{
	char path[256] = { 0 };
	UINT sz = sizeof(path) - 1;
	if (GetRawInputDeviceInfoA(hDevice, RIDI_DEVICENAME, path, &sz) == (UINT)-1)
		return false;
	return strstr(path, "RDP_MOU") != nullptr ||
	       strstr(path, "RDP_KBD") != nullptr ||
	       strstr(path, "Root#RDP") != nullptr;
}

// ---------------------------------------------------------------------------
// Friendly device names (algorithm adapted from MAME's
// rawinput_device_improve_name): resolve the registry DeviceDesc for a raw
// device path so the UI can say "Ultimarc I-PAC" or "HID-compliant mouse"
// instead of a VID/PID string. Falls back to the short VID/PID form when
// the registry walk comes up empty.
// ---------------------------------------------------------------------------

// DeviceDesc values look like "@input.inf,%hid...%;HID-compliant mouse":
// everything before the final ';' is an inf reference -- trim it.
static const char* ri_trim_desc_prefix(const char* desc)
{
	const char* semi = strrchr(desc, ';');
	return semi ? semi + 1 : desc;
}

// "\\?\HID#VID_2516&PID_007F#instance#{guid}" ->
// "SYSTEM\CurrentControlSet\Enum\HID\VID_2516&PID_007F\instance"
static bool ri_device_regpath(const char* rawname, char* out, size_t outlen)
{
	if (strncmp(rawname, "\\\\?\\", 4) != 0 && strncmp(rawname, "\\??\\", 4) != 0)
		return false;

	static const char base[] = "SYSTEM\\CurrentControlSet\\Enum\\";
	size_t o = sizeof(base) - 1;
	if (o >= outlen) return false;
	memcpy(out, base, o);

	for (const char* p = rawname + 4; *p && o < outlen - 1; p++)
		out[o++] = (*p == '#') ? '\\' : *p;
	out[o] = 0;

	// drop the trailing "{DeviceClasses-guid}" chunk
	char* last = strrchr(out, '\\');
	if (!last) return false;
	*last = 0;
	return true;
}

static bool ri_reg_query_string(HKEY key, const char* value, char* out, DWORD outlen)
{
	DWORD type = 0, len = outlen - 1;
	if (RegQueryValueExA(key, value, nullptr, &type, (LPBYTE)out, &len) != ERROR_SUCCESS)
		return false;
	if (type != REG_SZ && type != REG_EXPAND_SZ)
		return false;
	out[(len < outlen) ? len : outlen - 1] = 0;
	return out[0] != 0;
}

// USB-tree fallback for HID nodes without a DeviceDesc: find the USB device
// whose ParentIdPrefix prefixes our instance id and use its DeviceDesc.
static bool ri_name_from_usb_tree(const char* regpath, char* out, size_t outlen)
{
	// instance id = final chunk of the HID regpath, e.g. "7&2f4c...&0&0000"
	const char* parentid = strrchr(regpath, '\\');
	if (!parentid) return false;
	parentid++;

	HKEY usbkey;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Enum\\USB",
	                  0, KEY_READ, &usbkey) != ERROR_SUCCESS)
		return false;

	bool found = false;
	char vidkeyname[128];
	for (DWORD i = 0; !found; i++)
	{
		DWORD len = sizeof(vidkeyname);
		if (RegEnumKeyExA(usbkey, i, vidkeyname, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
			break;

		HKEY vidkey;
		if (RegOpenKeyExA(usbkey, vidkeyname, 0, KEY_READ, &vidkey) != ERROR_SUCCESS)
			continue;

		char instname[128];
		for (DWORD j = 0; !found; j++)
		{
			len = sizeof(instname);
			if (RegEnumKeyExA(vidkey, j, instname, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
				break;

			HKEY instkey;
			if (RegOpenKeyExA(vidkey, instname, 0, KEY_READ, &instkey) != ERROR_SUCCESS)
				continue;

			char prefix[128];
			if (ri_reg_query_string(instkey, "ParentIdPrefix", prefix, sizeof(prefix)) &&
			    strncmp(parentid, prefix, strlen(prefix)) == 0)
			{
				char desc[256];
				if (ri_reg_query_string(instkey, "DeviceDesc", desc, sizeof(desc)))
				{
					snprintf(out, outlen, "%s", ri_trim_desc_prefix(desc));
					found = true;
				}
			}
			RegCloseKey(instkey);
		}
		RegCloseKey(vidkey);
	}
	RegCloseKey(usbkey);
	return found;
}

// Resolve the best display name for a device: registry DeviceDesc first,
// USB-tree fallback for bare HID nodes, VID/PID short form as a last resort.
static void ri_friendly_device_name(HANDLE hDevice, char* out, size_t outlen)
{
	char path[256] = { 0 };
	UINT sz = sizeof(path) - 1;
	out[0] = 0;

	if (GetRawInputDeviceInfoA(hDevice, RIDI_DEVICENAME, path, &sz) == (UINT)-1 || !path[0])
	{
		snprintf(out, outlen, "UNKNOWN DEVICE");
		return;
	}

	char regpath[512];
	if (ri_device_regpath(path, regpath, sizeof(regpath)))
	{
		HKEY key;
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regpath, 0, KEY_READ, &key) == ERROR_SUCCESS)
		{
			char desc[256];
			bool got = ri_reg_query_string(key, "DeviceDesc", desc, sizeof(desc));
			RegCloseKey(key);
			if (got)
			{
				snprintf(out, outlen, "%s", ri_trim_desc_prefix(desc));
				return;
			}
		}

		if (strstr(regpath, "HID") && ri_name_from_usb_tree(regpath, out, outlen) && out[0])
			return;
	}

	ri_short_device_name(hDevice, out, outlen);
}

// Store device paths in an ini-safe form: the ini parser strips everything
// after ';' or '#' as an inline comment, and raw device paths are full of
// '#' ("\\?\HID#VID_...#instance#{guid}") -- a raw path saved to the ini
// came back truncated and never matched again ("assigned device not
// attached" after every restart). Mapping every non-alphanumeric character
// to '_' keeps the string unique per device instance and survives any
// round-trip. Both the stored table entry and the persisted config value
// use this form, so comparisons are always sanitized-vs-sanitized.
static void ri_sanitize_path(char* path)
{
	for (char* p = path; *p; p++)
	{
		const char c = *p;
		const bool ok = (c >= '0' && c <= '9') ||
		                (c >= 'A' && c <= 'Z') ||
		                (c >= 'a' && c <= 'z');
		if (!ok) *p = '_';
	}
}

// Find (or register) the per-device slot for hDevice. Called with
// g_miceMutex held. Returns -1 for injected input (hDevice == NULL),
// for RDP virtual devices, or when the table is full.
static int ri_mouse_slot(HANDLE hDevice)
{
	if (!hDevice) return -1;

	for (int i = 0; i < g_numMice; i++)
		if (g_mice[i].handle == hDevice) return i;

	if (g_numMice >= RI_MAX_MICE) return -1;
	if (ri_is_rdp_device(hDevice)) return -1;

	int i = g_numMice;
	ZeroMemory(&g_mice[i], sizeof(g_mice[i]));
	g_mice[i].handle = hDevice;
	ri_friendly_device_name(hDevice, g_mice[i].name, sizeof(g_mice[i].name));

	UINT psz = sizeof(g_mice[i].path) - 1;
	if (GetRawInputDeviceInfoA(hDevice, RIDI_DEVICENAME, g_mice[i].path, &psz) == (UINT)-1)
		g_mice[i].path[0] = 0;
	ri_sanitize_path(g_mice[i].path);

	g_numMice++;
	LOG_INFO("RawInput: mouse %d registered: %s", i + 1, g_mice[i].name);
	return i;
}

// Keyboard twin of ri_mouse_slot. Called with g_kbdsMutex held.
static int ri_kbd_slot(HANDLE hDevice)
{
	if (!hDevice) return -1;

	for (int i = 0; i < g_numKbds; i++)
		if (g_kbds[i].handle == hDevice) return i;

	if (g_numKbds >= RI_MAX_KBDS) return -1;
	if (ri_is_rdp_device(hDevice)) return -1;

	int i = g_numKbds;
	ZeroMemory(&g_kbds[i], sizeof(g_kbds[i]));
	g_kbds[i].handle = hDevice;
	ri_friendly_device_name(hDevice, g_kbds[i].name, sizeof(g_kbds[i].name));

	UINT psz = sizeof(g_kbds[i].path) - 1;
	if (GetRawInputDeviceInfoA(hDevice, RIDI_DEVICENAME, g_kbds[i].path, &psz) == (UINT)-1)
		g_kbds[i].path[0] = 0;
	ri_sanitize_path(g_kbds[i].path);

	g_numKbds++;
	LOG_INFO("RawInput: keyboard %d registered: %s", i + 1, g_kbds[i].name);
	return i;
}

// Pre-register every mouse and keyboard present at init so counts/names are
// available to the menu before any of them produce input.
static void ri_enumerate_devices(void)
{
	UINT count = 0;
	if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0)
		return;

	RAWINPUTDEVICELIST* list = new (std::nothrow) RAWINPUTDEVICELIST[count];
	if (!list) return;

	count = GetRawInputDeviceList(list, &count, sizeof(RAWINPUTDEVICELIST));
	if (count != (UINT)-1)
	{
		for (UINT i = 0; i < count; i++)
		{
			if (list[i].dwType == RIM_TYPEMOUSE)
			{
				std::lock_guard<std::mutex> lock(g_miceMutex);
				ri_mouse_slot(list[i].hDevice);
			}
			else if (list[i].dwType == RIM_TYPEKEYBOARD)
			{
				std::lock_guard<std::mutex> lock(g_kbdsMutex);
				ri_kbd_slot(list[i].hDevice);
			}
		}
	}
	delete[] list;
}

// GLFW style callbacks
static MouseButtonCallback g_mouseButtonCallback = nullptr;
static CursorPositionCallback g_cursorPositionCallback = nullptr;
static KeyCallback g_keyCallback = nullptr;

static std::atomic<bool> isInputPaused{ false };

// Track whether we have been initialized (thread is running)
static std::atomic<bool> s_initialized{ false };

// -----------------------------------------------------------------------------
// RawInput_SetPaused
// Description:
//  Pauses raw input processing and flushes current states so keys/buttons
//  don't get stuck down when losing focus.
// -----------------------------------------------------------------------------
void RawInput_SetPaused(bool paused) {
	bool wasPaused = isInputPaused.exchange(paused);

	if (paused && !wasPaused) {
		std::lock_guard<std::mutex> lock(inputQueueMutex);

		// Clear any queued raw inputs
		std::queue<RAWINPUT> emptyQueue;
		std::swap(inputQueue, emptyQueue);

		// Clear keyboard states to prevent stuck keys
		SecureZeroMemory(key, sizeof(key));
		SecureZeroMemory(lastkey, sizeof(lastkey));

		// Clear mouse button states
		mouse_b = 0;
		m_mouseStateRaw.left = UP;
		m_mouseStateRaw.right = UP;
		m_mouseStateRaw.middle = UP;

		// Clear relative motion deltas so the camera doesn't jump on return
		m_mouseStateRaw.dx = 0;
		m_mouseStateRaw.dy = 0;
		m_mouseStateRaw.dwheel = 0;

		// Clear per-device deltas and buttons too
		{
			std::lock_guard<std::mutex> mlock(g_miceMutex);
			for (int i = 0; i < g_numMice; i++)
			{
				g_mice[i].dx = g_mice[i].dy = g_mice[i].dwheel = 0;
				g_mice[i].buttons = 0;
			}
		}

		// Clear per-device keyboard state (prevents stuck keys)
		{
			std::lock_guard<std::mutex> klock(g_kbdsMutex);
			for (int i = 0; i < g_numKbds; i++)
				SecureZeroMemory(g_kbds[i].key, sizeof(g_kbds[i].key));
		}
	}
}

// -----------------------------------------------------------------------------
// GetModifierFlags
// Description:
//   Returns a bitmask of currently-held modifier keys via GetAsyncKeyState().
//   Uses RI_MOD_SHIFT/CONTROL/ALT/SUPER flags.
// -----------------------------------------------------------------------------
int GetModifierFlags()
{
	int mods = 0;
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
		mods |= RI_MOD_SHIFT;
	if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
		mods |= RI_MOD_CONTROL;
	if (GetAsyncKeyState(VK_MENU) & 0x8000)
		mods |= RI_MOD_ALT;
	if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000))
		mods |= RI_MOD_SUPER;
	return mods;
}

// -----------------------------------------------------------------------------
// SetKeyCallback
// Description:
//   Registers a callback function for keyboard key events.
// -----------------------------------------------------------------------------
void SetKeyCallback(KeyCallback callback) {
	g_keyCallback = callback;
}

// -----------------------------------------------------------------------------
// RawInput_Initialize
// Description:
//   Initializes raw input devices (keyboard and mouse) for the given window.
// -----------------------------------------------------------------------------
HRESULT RawInput_Initialize(HWND hWnd)
{
	// Guard against double-init: shut down previous instance first
	if (s_initialized.load()) {
		LOG_INFO("RawInput_Initialize: already initialized, shutting down previous instance");
		RawInput_Shutdown();
	}

	RAWINPUTDEVICE Rid[2]{};

	Rid[0].usUsagePage = 0x01;
	Rid[0].usUsage = 0x02;
	Rid[0].dwFlags = RIDEV_INPUTSINK;			//RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE | RIDEV_INPUTSINK;
	Rid[0].hwndTarget = hWnd;

	Rid[1].usUsagePage = 0x01;
	Rid[1].usUsage = 0x06;
	Rid[1].dwFlags = RIDEV_INPUTSINK;
	Rid[1].hwndTarget = hWnd;

	ZeroMemory(key, sizeof(key));
	ZeroMemory(lastkey, sizeof(lastkey));
	ZeroMemory(&m_mouseStateRaw, sizeof(m_mouseStateRaw));

	{
		std::lock_guard<std::mutex> lock(g_miceMutex);
		ZeroMemory(g_mice, sizeof(g_mice));
		g_numMice = 0;
	}
	{
		std::lock_guard<std::mutex> lock(g_kbdsMutex);
		ZeroMemory(g_kbds, sizeof(g_kbds));
		g_numKbds = 0;
	}

	windowHandle = hWnd;
	if (FALSE == RegisterRawInputDevices(Rid, 2, sizeof(Rid[0]))) //registers both mouse and keyboard
		return E_FAIL;

	ri_enumerate_devices();
	LOG_INFO("RawInput: %d mouse device(s), %d keyboard device(s) present",
		RawInput_GetMouseCount(), RawInput_GetKeyboardCount());

	inputThreadExit = false;
	inputThreadRun = false;
	inputThread = std::thread(input_thread_func);
	s_initialized = true;
	LOG_INFO("RawInput thread: started");
	return S_OK;
}

// -----------------------------------------------------------------------------
// RawInput_ProcessInput - now only enqueues input and signals worker
// -----------------------------------------------------------------------------
LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam) {
	// If input is paused, discard the event to Windows Default Proc
	if (isInputPaused.load()) {
		return DefWindowProc(hWnd, WM_INPUT, wParam, lParam);
	}

	RAWINPUT input;
	UINT size = sizeof(input);
	GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));

	{
		std::lock_guard<std::mutex> lock(inputQueueMutex);
		inputQueue.push(input);
		inputThreadRun = true;
	}
	inputCV.notify_one();

	return DefWindowProc(hWnd, WM_INPUT, wParam, lParam);
}

// -----------------------------------------------------------------------------
// Input Thread Function - processes queued events in batches
// -----------------------------------------------------------------------------
static void input_thread_func() {
	std::unique_lock<std::mutex> lock(inputQueueMutex);
	while (!inputThreadExit) {
		inputCV.wait(lock, [] { return inputThreadExit || inputThreadRun.load(); });
		if (inputThreadExit) break;

		// Swap queue to local for batch processing
		std::queue<RAWINPUT> localQueue;
		std::swap(localQueue, inputQueue);
		inputThreadRun = false;
		lock.unlock();

		while (!localQueue.empty()) {
			RawInput_ProcessInternal(localQueue.front());
			localQueue.pop();
		}

		lock.lock();
	}
	LOG_INFO("RawInput thread: exiting");
}

// -----------------------------------------------------------------------------
// Shutdown helper
// -----------------------------------------------------------------------------
void RawInput_Shutdown() {
	if (!s_initialized.load()) return;

	{
		std::lock_guard<std::mutex> lock(inputQueueMutex);
		inputThreadExit = true;
	}
	inputCV.notify_one();
	if (inputThread.joinable()) inputThread.join();
	s_initialized = false;
	LOG_INFO("RawInput_Shutdown: complete");
}

// -----------------------------------------------------------------------------
// RawInput_ProcessInput
// Description:
//   Processes WM_INPUT messages and updates internal key and mouse state.
// -----------------------------------------------------------------------------
//LRESULT RawInput_ProcessInput(HWND hWnd, WPARAM wParam, LPARAM lParam)
static void RawInput_ProcessInternal(const RAWINPUT& input)
{
	if (input.header.dwType == RIM_TYPEKEYBOARD) {
		const auto& kbd = input.data.keyboard;
		UINT virtualKey = kbd.VKey;
		UINT scanCode = kbd.MakeCode;
		UINT flags = kbd.Flags;

		if (virtualKey == 255) return;

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

		// per-device state (multi-keyboard): same fixed-up VK, so a device
		// table always agrees with the merged key[] for the same event
		{
			std::lock_guard<std::mutex> lock(g_kbdsMutex);
			int slot = ri_kbd_slot(input.header.hDevice);
			if (slot >= 0)
			{
				g_kbds[slot].seen = 1;
				g_kbds[slot].key[virtualKey & 0xff] = (kbd.Flags & RI_KEY_BREAK) ? 0 : 1;
			}
		}

		if (g_keyCallback) {
			int mods = GetModifierFlags();
			int action = (kbd.Flags & RI_KEY_BREAK) ? 0 : 1;
			g_keyCallback((int)virtualKey, (int)scanCode, action, mods);
		}
	}
	else if (input.header.dwType == RIM_TYPEMOUSE)
	{
		int mods = GetModifierFlags();

		//
		// Explicitly accumulate all WM_INPUT deltas per frame.
		m_mouseStateRaw.dx += input.data.mouse.lLastX;
		m_mouseStateRaw.dy += input.data.mouse.lLastY;

		// accumulated absolute position advances by THIS message's delta
		// (accumulating dx here compounded earlier deltas quadratically)
		m_mouseStateRaw.x += input.data.mouse.lLastX;
		m_mouseStateRaw.y += input.data.mouse.lLastY;

		// per-device accumulation (multi-mouse); injected input (NULL
		// device) only lands in the merged state above
		{
			std::lock_guard<std::mutex> lock(g_miceMutex);
			int slot = ri_mouse_slot(input.header.hDevice);
			if (slot >= 0)
			{
				RI_MOUSE_DEV& m = g_mice[slot];
				m.seen = 1;
				m.dx += input.data.mouse.lLastX;
				m.dy += input.data.mouse.lLastY;

				USHORT bf = input.data.mouse.usButtonFlags;
				if (bf & RI_MOUSE_LEFT_BUTTON_DOWN)   bset(m.buttons, 0x01);
				if (bf & RI_MOUSE_LEFT_BUTTON_UP)     bclr(m.buttons, 0x01);
				if (bf & RI_MOUSE_RIGHT_BUTTON_DOWN)  bset(m.buttons, 0x02);
				if (bf & RI_MOUSE_RIGHT_BUTTON_UP)    bclr(m.buttons, 0x02);
				if (bf & RI_MOUSE_MIDDLE_BUTTON_DOWN) bset(m.buttons, 0x04);
				if (bf & RI_MOUSE_MIDDLE_BUTTON_UP)   bclr(m.buttons, 0x04);
				if (bf & RI_MOUSE_WHEEL)              m.dwheel += (SHORT)input.data.mouse.usButtonData;
			}
		}

		if (g_cursorPositionCallback)
			g_cursorPositionCallback((double)m_mouseStateRaw.x, (double)m_mouseStateRaw.y);

		// usButtonFlags is a bitmask -- multiple flags can be set in one message.
		// Use if-chains instead of switch to handle simultaneous button events.
		USHORT btnFlags = input.data.mouse.usButtonFlags;

		if (btnFlags & RI_MOUSE_LEFT_BUTTON_DOWN) {
			m_mouseStateRaw.left = DOWN;
			if (g_mouseButtonCallback) g_mouseButtonCallback(0, 1, mods);
		}
		if (btnFlags & RI_MOUSE_LEFT_BUTTON_UP) {
			m_mouseStateRaw.left = UP;
			if (g_mouseButtonCallback) g_mouseButtonCallback(0, 0, mods);
		}

		if (btnFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) {
			m_mouseStateRaw.right = DOWN;
			if (g_mouseButtonCallback) g_mouseButtonCallback(1, 1, mods);
		}
		if (btnFlags & RI_MOUSE_RIGHT_BUTTON_UP) {
			m_mouseStateRaw.right = UP;
			if (g_mouseButtonCallback) g_mouseButtonCallback(1, 0, mods);
		}

		if (btnFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) {
			m_mouseStateRaw.middle = DOWN;
			if (g_mouseButtonCallback) g_mouseButtonCallback(2, 1, mods);
		}
		if (btnFlags & RI_MOUSE_MIDDLE_BUTTON_UP) {
			m_mouseStateRaw.middle = UP;
			if (g_mouseButtonCallback) g_mouseButtonCallback(2, 0, mods);
		}

		if (btnFlags & RI_MOUSE_WHEEL) {
			m_mouseStateRaw.dwheel += input.data.mouse.usButtonData;
		}

		// Allegro Mouse button support.
		if (m_mouseStateRaw.left)   bset(mouse_b, 0x01); else  bclr(mouse_b, 0x01);
		if (m_mouseStateRaw.right)  bset(mouse_b, 0x02); else  bclr(mouse_b, 0x02);
		if (m_mouseStateRaw.middle) bset(mouse_b, 0x04); else  bclr(mouse_b, 0x04);
	}
}

// -----------------------------------------------------------------------------
// SetMouseButtonCallback
// Description:
//   Registers a callback function for mouse button events.
// -----------------------------------------------------------------------------
void SetMouseButtonCallback(MouseButtonCallback callback) {
	g_mouseButtonCallback = callback;
}

// -----------------------------------------------------------------------------
// SetCursorPositionCallback
// Description:
//   Registers a callback function for mouse cursor position changes.
// -----------------------------------------------------------------------------
void SetCursorPositionCallback(CursorPositionCallback callback) {
	g_cursorPositionCallback = callback;
}

// -----------------------------------------------------------------------------
// test_clr
// Description:
//   Clears internal input state buffers.
// -----------------------------------------------------------------------------
void test_clr()
{
	SecureZeroMemory(key, sizeof(key));
	SecureZeroMemory(lastkey, sizeof(lastkey));
}

// Function to get the window size
// -----------------------------------------------------------------------------
// getWindowSize
// Description:
//   Retrieves the current size of the client area of the window.
// -----------------------------------------------------------------------------
void getWindowSize(int* width, int* height) {
	RECT rect;
	GetClientRect(windowHandle, &rect);
	*width = rect.right - rect.left;
	*height = rect.bottom - rect.top;
}

// -----------------------------------------------------------------------------
// get_mouse_win
// Description:
//   Retrieves the current mouse position in window (client) coordinates.
// -----------------------------------------------------------------------------
void get_mouse_win(int* mickeyx, int* mickeyy)
{
	POINT cursor_pos;
	GetCursorPos(&cursor_pos);
	ScreenToClient(windowHandle, (LPPOINT)&cursor_pos);
	*mickeyx = cursor_pos.x;
	*mickeyy = cursor_pos.y;
}

// Your updated function
// -----------------------------------------------------------------------------
// get_mouse_mickeys
// Description:
//   Retrieves and resets relative mouse movement, scaled by the mouse mickey scale.
// -----------------------------------------------------------------------------
void get_mouse_mickeys(int* mickeyx, int* mickeyy)
{
	int temp_x = m_mouseStateRaw.dx;
	int temp_y = m_mouseStateRaw.dy;
	m_mouseStateRaw.dx = 0;
	m_mouseStateRaw.dy = 0;
	*mickeyx = static_cast<int>(temp_x * g_mouseScale);
	*mickeyy = static_cast<int>(temp_y * g_mouseScale);
}

// -----------------------------------------------------------------------------
// Multi-mouse accessors
// -----------------------------------------------------------------------------
int RawInput_GetMouseCount()
{
	std::lock_guard<std::mutex> lock(g_miceMutex);
	return g_numMice;
}

const char* RawInput_GetMouseName(int index)
{
	// name[] is written once at registration and never changes; safe to
	// return the pointer without holding the lock afterwards
	std::lock_guard<std::mutex> lock(g_miceMutex);
	if (index < 0 || index >= g_numMice) return "NONE";
	return g_mice[index].name;
}

// Per-device relative motion, read-and-reset, mickey-scaled.
// index = -1 reads the merged stream (identical to get_mouse_mickeys).
void get_mouse_mickeys_ex(int index, int* mickeyx, int* mickeyy)
{
	if (index < 0)
	{
		get_mouse_mickeys(mickeyx, mickeyy);
		return;
	}

	std::lock_guard<std::mutex> lock(g_miceMutex);
	if (index >= g_numMice)
	{
		*mickeyx = *mickeyy = 0;
		return;
	}
	long tx = g_mice[index].dx;
	long ty = g_mice[index].dy;
	g_mice[index].dx = 0;
	g_mice[index].dy = 0;
	*mickeyx = static_cast<int>(tx * g_mouseScale);
	*mickeyy = static_cast<int>(ty * g_mouseScale);
}

int RawInput_GetMouseButtons(int index)
{
	if (index < 0) return mouse_b;

	std::lock_guard<std::mutex> lock(g_miceMutex);
	if (index >= g_numMice) return 0;
	return g_mice[index].buttons;
}

// 1 once the device has actually produced input. Lets the UI distinguish
// real mice from phantom mouse collections that composite keyboards expose
// (which enumerate but never move).
int RawInput_MouseSeenInput(int index)
{
	std::lock_guard<std::mutex> lock(g_miceMutex);
	if (index < 0 || index >= g_numMice) return 0;
	return g_mice[index].seen;
}

// Full raw device path -- the stable identity used to persist assignments.
// Written once at registration; safe to hand out the pointer.
const char* RawInput_GetMousePath(int index)
{
	std::lock_guard<std::mutex> lock(g_miceMutex);
	if (index < 0 || index >= g_numMice) return "";
	return g_mice[index].path;
}

// Look up a device by its persisted path. Returns the CURRENT index (which
// may differ from the index at the time the assignment was saved -- Windows
// does not guarantee stable enumeration order), or -1 if the device is not
// attached right now.
int RawInput_FindMouseByPath(const char* path)
{
	if (!path || !path[0]) return -1;

	std::lock_guard<std::mutex> lock(g_miceMutex);
	for (int i = 0; i < g_numMice; i++)
		if (strcmp(g_mice[i].path, path) == 0) return i;
	return -1;
}

// -----------------------------------------------------------------------------
// Multi-keyboard accessors (twins of the mouse set above)
// -----------------------------------------------------------------------------
int RawInput_GetKeyboardCount()
{
	std::lock_guard<std::mutex> lock(g_kbdsMutex);
	return g_numKbds;
}

const char* RawInput_GetKeyboardName(int index)
{
	std::lock_guard<std::mutex> lock(g_kbdsMutex);
	if (index < 0 || index >= g_numKbds) return "NONE";
	return g_kbds[index].name;
}

const char* RawInput_GetKeyboardPath(int index)
{
	std::lock_guard<std::mutex> lock(g_kbdsMutex);
	if (index < 0 || index >= g_numKbds) return "";
	return g_kbds[index].path;
}

int RawInput_FindKeyboardByPath(const char* path)
{
	if (!path || !path[0]) return -1;

	std::lock_guard<std::mutex> lock(g_kbdsMutex);
	for (int i = 0; i < g_numKbds; i++)
		if (strcmp(g_kbds[i].path, path) == 0) return i;
	return -1;
}

int RawInput_KeyboardSeenInput(int index)
{
	std::lock_guard<std::mutex> lock(g_kbdsMutex);
	if (index < 0 || index >= g_numKbds) return 0;
	return g_kbds[index].seen;
}

// Key state on a specific keyboard (VK-indexed, same fixups as the merged
// key[]). index = -1 reads the merged state. Lock-free read of a single
// byte written by the worker thread -- same policy as the merged key[].
int RawInput_IsKeyDownEx(int index, int vk)
{
	if (index < 0) return key[vk & 0xff];
	if (index >= g_numKbds) return 0;
	return g_kbds[index].key[vk & 0xff];
}
//keyboard state checks
// -----------------------------------------------------------------------------
// isKeyHeld
// Description:
//   Returns non-zero if the given key has been pressed (held count).
// -----------------------------------------------------------------------------
int isKeyHeld(int vkCode) { return lastkey[vkCode]; }
// -----------------------------------------------------------------------------
// IsKeyDown
// Description:
//   Returns true if the specified key is currently pressed.
// -----------------------------------------------------------------------------
bool IsKeyDown(int vkCode) { return key[vkCode & 0xff] ? true : false; }
// -----------------------------------------------------------------------------
// IsKeyUp
// Description:
//   Returns true if the specified key is currently released.
// -----------------------------------------------------------------------------
bool IsKeyUp(int vkCode) { return key[vkCode & 0xff] ? false : true; }

//summed mouse state checks/sets;
//use as convenience, ie. keeping track of movements without needing to maintain separate data set
//naming is left to C style for compatibility

// -----------------------------------------------------------------------------
// GetMouseX
// Description:
//   Returns the absolute X position of the mouse.
// -----------------------------------------------------------------------------
int32_t GetMouseX() { return m_mouseStateRaw.x; }
// -----------------------------------------------------------------------------
// GetMouseY
// Description:
//   Returns the absolute Y position of the mouse.
// -----------------------------------------------------------------------------
int32_t GetMouseY() { return m_mouseStateRaw.y; }
// -----------------------------------------------------------------------------
// GetMouseWheel
// Description:
//   Returns the absolute scroll wheel value.
// -----------------------------------------------------------------------------
int32_t GetMouseWheel() { return m_mouseStateRaw.wheel; }
// -----------------------------------------------------------------------------
// SetMouseX
// Description:
//   Sets the internal X position of the mouse.
// -----------------------------------------------------------------------------
void SetMouseX(int32_t x) { m_mouseStateRaw.x = x; }
// -----------------------------------------------------------------------------
// SetMouseY
// Description:
//   Sets the internal Y position of the mouse.
// -----------------------------------------------------------------------------
void SetMouseY(int32_t y) { m_mouseStateRaw.y = y; }
// -----------------------------------------------------------------------------
// SetMouseWheel
// Description:
//   Sets the internal scroll wheel value.
// -----------------------------------------------------------------------------
void SetMouseWheel(int32_t wheel) { m_mouseStateRaw.wheel = wheel; }

//relative mouse state changes
// -----------------------------------------------------------------------------
// GetMouseXChange
// Description:
//   Returns the change in mouse X position since last reset.
// -----------------------------------------------------------------------------
int32_t GetMouseXChange() { return m_mouseStateRaw.dx; }
// -----------------------------------------------------------------------------
// GetMouseYChange
// Description:
//   Returns the change in mouse Y position since last reset.
// -----------------------------------------------------------------------------
int32_t GetMouseYChange() { return m_mouseStateRaw.dy; }
// -----------------------------------------------------------------------------
// GetMouseWheelChange
// Description:
//   Returns the change in scroll wheel value since last reset.
// -----------------------------------------------------------------------------
int32_t GetMouseWheelChange() { return m_mouseStateRaw.dwheel; }

//mouse button state checks
// -----------------------------------------------------------------------------
// IsMouseLButtonDown
// Description:
//   Returns true if the left mouse button is currently down.
// -----------------------------------------------------------------------------
bool IsMouseLButtonDown() { return (m_mouseStateRaw.left == DOWN) ? TRUE : FALSE; }
// -----------------------------------------------------------------------------
// IsMouseLButtonUp
// Description:
//   Returns true if the left mouse button is currently up.
// -----------------------------------------------------------------------------
bool IsMouseLButtonUp() { return (m_mouseStateRaw.left == UP) ? TRUE : FALSE; }
// -----------------------------------------------------------------------------
// IsMouseRButtonDown
// Description:
//   Returns true if the right mouse button is currently down.
// -----------------------------------------------------------------------------
bool IsMouseRButtonDown() { return (m_mouseStateRaw.right == DOWN) ? TRUE : FALSE; }
// -----------------------------------------------------------------------------
// IsMouseRButtonUp
// Description:
//   Returns true if the right mouse button is currently up.
// -----------------------------------------------------------------------------
bool IsMouseRButtonUp() { return (m_mouseStateRaw.right == UP) ? TRUE : FALSE; }
// -----------------------------------------------------------------------------
// IsMouseMButtonDown
// Description:
//   Returns true if the middle mouse button is currently down.
// -----------------------------------------------------------------------------
bool IsMouseMButtonDown() { return (m_mouseStateRaw.middle == DOWN) ? TRUE : FALSE; }
// -----------------------------------------------------------------------------
// IsMouseMButtonUp
// Description:
//   Returns true if the middle mouse button is currently up.
// -----------------------------------------------------------------------------
bool IsMouseMButtonUp() { return (m_mouseStateRaw.middle == UP) ? TRUE : FALSE; }