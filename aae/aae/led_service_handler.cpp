#ifdef _WIN32
#include "framework.h"   // Win32 window helpers
#endif
#include "led_service_handler.h"  // osd_set_leds() and the set_led_status surface
#include "sys_log.h"
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Platform-neutral LED bookkeeping.
//
// These latch the driver-facing LED indices and fold them into the mask that
// osd_set_leds() takes. Nothing about it is platform-specific, so it lives
// OUTSIDE the #ifdef: it used to exist twice -- a real copy in the Win32
// branch and a do-nothing copy in the other -- which meant a non-Windows build
// discarded LED state before the osd_ layer was ever reached. Nothing
// observable changes today, because the Linux osd_set_leds() -- now in the
// evdev backend, see the note below the bookkeeping -- is still a stub; this
// matters once the Linux osd_ layer is real, and it means only that layer has
// to be written rather than this bookkeeping as well.
//
// LED mapping: 0 -> NumLock, 1 -> CapsLock, 2 -> ScrollLock.
// ---------------------------------------------------------------------------
static int g_led0_numlock    = 0;
static int g_led1_capslock   = 0;
static int g_led2_scrolllock = 0;

static int led_mask()
{
	int mask = 0;
	if (g_led0_numlock)    mask |= (1 << 0);
	if (g_led1_capslock)   mask |= (1 << 1);
	if (g_led2_scrolllock) mask |= (1 << 2);
	return mask;
}

// which: 0..2 as above. Any other index is ignored -- omegrace.cpp asks for
// index 3, which has no keyboard indicator to map onto.
void set_led_status(int which, int on)
{
	const int v = (on != 0) ? 1 : 0;

	if      (which == 0) g_led0_numlock    = v;
	else if (which == 1) g_led1_capslock   = v;
	else if (which == 2) g_led2_scrolllock = v;
	else return;

	osd_set_leds(led_mask());
}

// Currently latched state for one LED, or 0 for an invalid index.
int get_led_status(int which)
{
	if (which == 0) return g_led0_numlock;
	if (which == 1) return g_led1_capslock;
	if (which == 2) return g_led2_scrolllock;
	return 0;
}

void set_led_status_all(int led0, int led1, int led2)
{
	g_led0_numlock    = (led0 != 0) ? 1 : 0;
	g_led1_capslock   = (led1 != 0) ? 1 : 0;
	g_led2_scrolllock = (led2 != 0) ? 1 : 0;

	osd_set_leds(led_mask());
}

// The Linux osd_led_* implementations live in the evdev backend
// (aae/system/input/linux/evdev_input.cpp), where the open device fds are.
//
// Deliberately "Linux" and not "non-Windows": this file no longer carries a
// catch-all stub arm, so any third target -- osdepend.h marks Teensy STUB --
// must bring its own osd_led_* definitions or take an undefined reference.

#ifdef _WIN32   // ========================================= Win32 implementation
// This header provides IOCTL_KEYBOARD_SET_INDICATORS and KEYBOARD_INDICATOR_PARAMETERS.
#include <ntddkbd.h>
#include <setupapi.h>
#pragma comment(lib, "setupapi.lib")


// Keyboard device interface GUID (GUID_DEVINTERFACE_KEYBOARD)
// {884b96c3-56ef-11d1-bc8c-00a0c91405dd}
static const GUID kGuidKeyboardInterface =
{
	0x884b96c3, 0x56ef, 0x11d1,
	{ 0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd }
};

static HANDLE g_kbdLedHandles[64];
static int g_kbdLedHandleCount = 0;
static HANDLE g_ledEvent = NULL;         // signals "LED state changed"
static volatile LONG g_ledLastRequestedMask = -1;

// Bitmask enum for keyboard LEDs
enum LedBitMask {
	LED_NUMLOCK = 1 << 0,
	LED_CAPSLOCK = 1 << 1,
	LED_SCROLLLOCK = 1 << 2,
};

static volatile LONG g_ledDesiredMask = 0;     // desired LED mask (LedBitMask)
static volatile LONG g_ledServiceRunning = 0;  // 0/1
static HANDLE g_ledThread = NULL;

static void Win32_CloseKeyboardLedTargets()
{
	for (int i = 0; i < g_kbdLedHandleCount; ++i)
	{
		if (g_kbdLedHandles[i] && g_kbdLedHandles[i] != INVALID_HANDLE_VALUE)
			CloseHandle(g_kbdLedHandles[i]);
		g_kbdLedHandles[i] = NULL;
	}
	g_kbdLedHandleCount = 0;
}

static void Win32_EnumKeyboardLedTargets()
{
	Win32_CloseKeyboardLedTargets();

	// Enumerate keyboard device interfaces and open handles once.
	HDEVINFO devs = SetupDiGetClassDevs(&kGuidKeyboardInterface,
		NULL,
		NULL,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

	if (devs != INVALID_HANDLE_VALUE)
	{
		SP_DEVICE_INTERFACE_DATA ifData;
		memset(&ifData, 0, sizeof(ifData));
		ifData.cbSize = sizeof(ifData);

		for (DWORD idx = 0; ; ++idx)
		{
			if (!SetupDiEnumDeviceInterfaces(devs, NULL, &kGuidKeyboardInterface, idx, &ifData))
				break;

			DWORD reqSize = 0;
			SetupDiGetDeviceInterfaceDetail(devs, &ifData, NULL, 0, &reqSize, NULL);
			if (reqSize == 0)
				continue;

			PSP_DEVICE_INTERFACE_DETAIL_DATA detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(reqSize);
			if (!detail)
				continue;

			memset(detail, 0, reqSize);
			detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

			if (SetupDiGetDeviceInterfaceDetail(devs, &ifData, detail, reqSize, NULL, NULL))
			{
				HANDLE h = CreateFile(detail->DevicePath,
					GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL,
					OPEN_EXISTING,
					0,
					NULL);

				if (h == INVALID_HANDLE_VALUE)
				{
					h = CreateFile(detail->DevicePath,
						GENERIC_READ | GENERIC_WRITE,
						FILE_SHARE_READ | FILE_SHARE_WRITE,
						NULL,
						OPEN_EXISTING,
						0,
						NULL);
				}

				if (h != INVALID_HANDLE_VALUE)
				{
					if (g_kbdLedHandleCount < (int)(sizeof(g_kbdLedHandles) / sizeof(g_kbdLedHandles[0])))
					{
						// Log the device path: when a machine misbehaves this
						// is what identifies WHICH keyboard-class device we
						// are about to send indicator IOCTLs to (a keyboard
						// encoder like an I-PAC shows its VID here, e.g.
						// vid_d209 for Ultimarc).
						LOG_INFO("LED service: opened target %ls", detail->DevicePath);
						g_kbdLedHandles[g_kbdLedHandleCount++] = h;
					}
					else
					{
						CloseHandle(h);
					}
				}
			}

			free(detail);
		}

		SetupDiDestroyDeviceInfoList(devs);
	}

	// Fallback to \\.\KeyboardClassX if interface enumeration found nothing.
	if (g_kbdLedHandleCount == 0)
	{
		for (int i = 0; i < 32; ++i)
		{
			char path[64];
			sprintf_s(path, "\\\\.\\KeyboardClass%d", i);

			HANDLE h = CreateFileA(path,
				GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL,
				OPEN_EXISTING,
				0,
				NULL);

			if (h != INVALID_HANDLE_VALUE)
			{
				if (g_kbdLedHandleCount < (int)(sizeof(g_kbdLedHandles) / sizeof(g_kbdLedHandles[0])))
				{
					LOG_INFO("LED service: opened target %s (fallback path)", path);
					g_kbdLedHandles[g_kbdLedHandleCount++] = h;
				}
				else
					CloseHandle(h);
			}
		}
	}

	if (g_kbdLedHandleCount == 0)
		LOG_INFO("LED service: no keyboard LED targets could be opened; keyboard LEDs will not work (check OS/service configuration)");
}

// Apply LED mask via KeyboardClass IOCTL.
// Returns true if at least one device accepted the IOCTL.
// Apply LED mask via keyboard device interface enumeration first,
// then fall back to \\.\KeyboardClassX.
//
// Returns true if at least one device accepted the IOCTL.
static bool Win32_ApplyKeyboardLeds_IOCTL(int mask)
{
	// If we have no targets, enumerate now (rare path).
	if (g_kbdLedHandleCount == 0)
		Win32_EnumKeyboardLedTargets();

	USHORT flags = 0;
	if (mask & LED_NUMLOCK)    flags |= KEYBOARD_NUM_LOCK_ON;
	if (mask & LED_CAPSLOCK)   flags |= KEYBOARD_CAPS_LOCK_ON;
	if (mask & LED_SCROLLLOCK) flags |= KEYBOARD_SCROLL_LOCK_ON;

	KEYBOARD_INDICATOR_PARAMETERS kip;
	memset(&kip, 0, sizeof(kip));
	kip.UnitId = 0;
	kip.LedFlags = flags;

	bool any_ok = false;

	for (int i = 0; i < g_kbdLedHandleCount; ++i)
	{
		HANDLE h = g_kbdLedHandles[i];
		if (!h || h == INVALID_HANDLE_VALUE)
			continue;

		DWORD bytesReturned = 0;
		BOOL ok = DeviceIoControl(h,
			IOCTL_KEYBOARD_SET_INDICATORS,
			&kip, sizeof(kip),
			NULL, 0,
			&bytesReturned,
			NULL);

		if (ok)
		{
			any_ok = true;
		}
		else
		{
			// If handles go stale (device hotplug), we can re-enumerate later.
			static int spam = 0;
			if (spam++ < 6)
			{
				DWORD e = GetLastError();
				LOG_INFO("LED IOCTL failed on cached handle (err=%lu)", (unsigned long)e);
			}
		}
	}

	return any_ok;
}

// Worker thread: periodically re-apply LEDs (some devices reset LEDs on activity).
static DWORD WINAPI LedServiceThreadProc(LPVOID)
{
	// Keep this thread from competing with the emulation/render loop.
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);

	// Enumerate once on start.
	Win32_EnumKeyboardLedTargets();

	int lastApplied = -1;

	while (InterlockedCompareExchange(&g_ledServiceRunning, 1, 1) == 1)
	{
		// Wait until there is work. Use a long timeout so we can exit cleanly.
		DWORD w = WaitForSingleObject(g_ledEvent, 250);
		if (w == WAIT_TIMEOUT)
			continue;

		if (InterlockedCompareExchange(&g_ledServiceRunning, 1, 1) != 1)
			break;

		const int desired = (int)InterlockedCompareExchange(&g_ledDesiredMask, 0, 0);
		if (desired != lastApplied)
		{
			Win32_ApplyKeyboardLeds_IOCTL(desired);
			lastApplied = desired;
		}
	}

	Win32_CloseKeyboardLedTargets();
	return 0;
}

void osd_led_service_start()
{
	if (InterlockedCompareExchange(&g_ledServiceRunning, 1, 0) != 0)
		return;

	// Create an auto-reset event (one wake per signal).
	if (!g_ledEvent)
		g_ledEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	if (!g_ledEvent)
	{
		InterlockedExchange(&g_ledServiceRunning, 0);
		return;
	}

	// Clear any cached targets so the thread will enumerate fresh.
	Win32_CloseKeyboardLedTargets();

	g_ledThread = CreateThread(NULL, 0, LedServiceThreadProc, NULL, 0, NULL);
	if (!g_ledThread)
	{
		InterlockedExchange(&g_ledServiceRunning, 0);
		CloseHandle(g_ledEvent);
		g_ledEvent = NULL;
	}
}

void osd_led_service_stop()
{
	if (InterlockedCompareExchange(&g_ledServiceRunning, 0, 1) != 1)
		return;

	// Wake the worker so it can exit promptly.
	if (g_ledEvent)
		SetEvent(g_ledEvent);

	if (g_ledThread)
	{
		WaitForSingleObject(g_ledThread, 2000);
		CloseHandle(g_ledThread);
		g_ledThread = NULL;
	}

	Win32_CloseKeyboardLedTargets();

	if (g_ledEvent)
	{
		CloseHandle(g_ledEvent);
		g_ledEvent = NULL;
	}
}

void osd_set_leds(int state)
{
	// Normalize to the 3-bit mask we support (Num/Caps/Scroll).
	state &= (LED_NUMLOCK | LED_CAPSLOCK | LED_SCROLLLOCK);

	// If the request hasn't changed, do nothing (no event, no IO).
	const LONG prev = InterlockedExchange(&g_ledLastRequestedMask, (LONG)state);
	if ((int)prev == state)
		return;

	// Store desired mask for the service thread.
	InterlockedExchange(&g_ledDesiredMask, (LONG)state);

	// Wake LED service thread to apply it (thread does the IOCTL).
	if (g_ledEvent)
		SetEvent(g_ledEvent);
}

int osd_get_leds()
{
	return (int)InterlockedCompareExchange(&g_ledDesiredMask, 0, 0);
}

#endif // _WIN32
