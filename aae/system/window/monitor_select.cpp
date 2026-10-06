// Vendored copy. Master: C:\Source2026\shared\monitor_select - keep in sync.
// -----------------------------------------------------------------------------
// monitor_select.cpp - see monitor_select.h
// -----------------------------------------------------------------------------
#include "monitor_select.h"

#include <algorithm>
#include <cstdio>

namespace monsel {

namespace {

std::string WideToUtf8(const wchar_t* w)
{
	if (!w || !*w) return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1) return std::string();
	std::string s(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
	s.resize(static_cast<size_t>(n) - 1);
	return s;
}

bool EqualNoCase(const std::string& a, const std::string& b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i) {
		char x = a[i], y = b[i];
		if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
		if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
		if (x != y) return false;
	}
	return true;
}

struct DisplayIds {
	std::wstring gdiName;      // source GDI name, e.g. \\.\DISPLAY1
	std::string  friendlyName;
	std::string  deviceId;
};

// QueryDisplayConfig for every active path. Any failure -> empty vector.
std::vector<DisplayIds> QueryDisplayIds()
{
	std::vector<DisplayIds> out;

	std::vector<DISPLAYCONFIG_PATH_INFO> paths;
	std::vector<DISPLAYCONFIG_MODE_INFO> modes;
	UINT32 numPaths = 0, numModes = 0;
	LONG rc = ERROR_INSUFFICIENT_BUFFER;

	for (int attempt = 0; attempt < 4 && rc == ERROR_INSUFFICIENT_BUFFER; ++attempt)
	{
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPaths, &numModes) != ERROR_SUCCESS)
			return out;
		paths.assign(numPaths, DISPLAYCONFIG_PATH_INFO{});
		modes.assign(numModes, DISPLAYCONFIG_MODE_INFO{});
		rc = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPaths, paths.data(), &numModes, modes.data(), nullptr);
	}
	if (rc != ERROR_SUCCESS)
		return out;
	paths.resize(numPaths);

	for (const DISPLAYCONFIG_PATH_INFO& p : paths)
	{
		DISPLAYCONFIG_SOURCE_DEVICE_NAME src = {};
		src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		src.header.size = sizeof(src);
		src.header.adapterId = p.sourceInfo.adapterId;
		src.header.id = p.sourceInfo.id;
		if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS)
			continue;

		// Cloned displays: several targets on one source - first one wins.
		bool dup = false;
		for (const DisplayIds& d : out)
			if (d.gdiName == src.viewGdiDeviceName) { dup = true; break; }
		if (dup) continue;

		DisplayIds ids;
		ids.gdiName = src.viewGdiDeviceName;

		DISPLAYCONFIG_TARGET_DEVICE_NAME tgt = {};
		tgt.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
		tgt.header.size = sizeof(tgt);
		tgt.header.adapterId = p.targetInfo.adapterId;
		tgt.header.id = p.targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&tgt.header) == ERROR_SUCCESS)
		{
			ids.friendlyName = WideToUtf8(tgt.monitorFriendlyDeviceName);
			ids.deviceId = WideToUtf8(tgt.monitorDevicePath);
		}
		out.push_back(ids);
	}
	return out;
}

struct EnumCtx {
	std::vector<MonitorDesc>* list;
	std::vector<DisplayIds>*  ids;
};

BOOL CALLBACK EnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM lp)
{
	EnumCtx* ctx = reinterpret_cast<EnumCtx*>(lp);
	MONITORINFOEXW mi = {};
	mi.cbSize = sizeof(mi);
	if (!GetMonitorInfoW(hMon, &mi))
		return TRUE;

	MonitorDesc d;
	d.handle = hMon;
	d.monitorRect = mi.rcMonitor;
	d.workRect = mi.rcWork;
	d.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
	d.gdiName = WideToUtf8(mi.szDevice);
	for (const DisplayIds& id : *ctx->ids)
	{
		if (_wcsicmp(id.gdiName.c_str(), mi.szDevice) == 0)
		{
			d.friendlyName = id.friendlyName;
			d.deviceId = id.deviceId;
			break;
		}
	}
	ctx->list->push_back(d);
	return TRUE;
}

} // namespace

std::vector<MonitorDesc> Enumerate()
{
	std::vector<MonitorDesc> list;
	std::vector<DisplayIds> ids = QueryDisplayIds();
	EnumCtx ctx = { &list, &ids };
	EnumDisplayMonitors(nullptr, nullptr, EnumProc, reinterpret_cast<LPARAM>(&ctx));

	if (list.empty())
	{
		MonitorDesc d;
		d.primary = true;
		d.number = 1;
		HMONITOR h = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
		MONITORINFOEXW mi = {};
		mi.cbSize = sizeof(mi);
		d.handle = h;
		if (h && GetMonitorInfoW(h, &mi))
		{
			d.monitorRect = mi.rcMonitor;
			d.workRect = mi.rcWork;
			d.gdiName = WideToUtf8(mi.szDevice);
		}
		else
		{
			d.monitorRect = RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
			d.workRect = d.monitorRect;
		}
		list.push_back(d);
		return list;
	}

	// Ensure exactly one primary comes first (defensive if no flag was seen).
	std::stable_sort(list.begin(), list.end(), [](const MonitorDesc& a, const MonitorDesc& b) {
		if (a.primary != b.primary) return a.primary;      // primary first
		if (a.monitorRect.left != b.monitorRect.left) return a.monitorRect.left < b.monitorRect.left;
		return a.monitorRect.top < b.monitorRect.top;
	});
	if (!list[0].primary)
		list[0].primary = true;   // no flagged primary: lowest-left monitor stands in
	for (size_t i = 0; i < list.size(); ++i)
		list[i].number = static_cast<int>(i) + 1;
	return list;
}

MonitorDesc Primary()
{
	return Enumerate().front();
}

MonitorDesc FromWindow(HWND hwnd)
{
	std::vector<MonitorDesc> all = Enumerate();
	HMONITOR h = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
	                  : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
	for (const MonitorDesc& d : all)
		if (d.handle == h)
			return d;
	return all.front();
}

MonitorDesc Select(int number, const std::string& deviceId, std::string* reason)
{
	std::vector<MonitorDesc> all = Enumerate();
	char buf[160];

	if (number <= 0) number = 1;

	if (!deviceId.empty())
	{
		for (const MonitorDesc& d : all)
		{
			if (!d.deviceId.empty() && EqualNoCase(d.deviceId, deviceId))
			{
				if (reason) *reason = "matched device id";
				return d;
			}
		}
		if (number >= 1 && number <= static_cast<int>(all.size()))
		{
			snprintf(buf, sizeof(buf), "device id not connected, using number %d", number);
			if (reason) *reason = buf;
			return all[static_cast<size_t>(number) - 1];
		}
		snprintf(buf, sizeof(buf), "device id not connected and number %d out of range (%d monitors), using primary",
		         number, static_cast<int>(all.size()));
		if (reason) *reason = buf;
		return all.front();
	}

	if (number <= static_cast<int>(all.size()))
	{
		snprintf(buf, sizeof(buf), "using number %d", number);
		if (reason) *reason = buf;
		return all[static_cast<size_t>(number) - 1];
	}
	snprintf(buf, sizeof(buf), "number %d out of range (%d monitors), using primary",
	         number, static_cast<int>(all.size()));
	if (reason) *reason = buf;
	return all.front();
}

bool IsRectVisible(const RECT& r)
{
	return MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != nullptr;
}

RECT CenterInWorkArea(int width, int height, const MonitorDesc& m)
{
	const RECT& w = m.workRect;
	int x = w.left + ((w.right - w.left) - width) / 2;
	int y = w.top + ((w.bottom - w.top) - height) / 2;
	// Keep the top-left on the monitor.
	if (x < m.monitorRect.left) x = m.monitorRect.left;
	if (y < m.monitorRect.top)  y = m.monitorRect.top;
	RECT r = { x, y, x + width, y + height };
	return r;
}

std::string DescribeAll()
{
	std::vector<MonitorDesc> all = Enumerate();
	std::string out;
	char buf[256];
	for (const MonitorDesc& d : all)
	{
		snprintf(buf, sizeof(buf),
		         "monitor %d%s: rect (%ld,%ld)-(%ld,%ld) work (%ld,%ld)-(%ld,%ld) gdi=",
		         d.number, d.primary ? " [primary]" : "",
		         d.monitorRect.left, d.monitorRect.top, d.monitorRect.right, d.monitorRect.bottom,
		         d.workRect.left, d.workRect.top, d.workRect.right, d.workRect.bottom);
		out += buf;
		out += d.gdiName;
		out += " name=\"" + d.friendlyName + "\" id=" + d.deviceId + "\n";
	}
	return out;
}

} // namespace monsel
