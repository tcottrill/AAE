
// Copyright Tim Cottrill 2025
// Release notes:
// First revision 3/25/25

/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non - commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain.We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors.We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to < https://unlicense.org/>
*/

#include "path_helper.h"
#include "sys_log.h"

#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>   // GetModuleFileNameW - see exe_dir() below
#include "utf8conv.h"
#else
#include <unistd.h>    // readlink
#include <limits.h>    // PATH_MAX
#endif

// -----------------------------------------------------------------------------
// exe_dir
//
// The ONE genuinely platform-specific operation in this file: ask the OS where
// the running executable lives. There is no portable way to do it - Windows has
// GetModuleFileName, Linux has the /proc/self/exe symlink - so it is isolated
// here and everything above it is std::filesystem.
//
// This also replaces the hand-rolled backslash searching the two functions
// below used to do (wcsrchr/strrchr for '\\'), which found nothing on Linux
// because a backslash is an ordinary filename character there.
// -----------------------------------------------------------------------------
static std::filesystem::path exe_dir()
{
#ifdef _WIN32
	wchar_t buf[MAX_PATH] = { 0 };
	DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	if (len == 0 || len == MAX_PATH)
	{
		LOG_INFO("Failed to get the executable path. Error: %lu", GetLastError());
		return {};
	}
	return std::filesystem::path(buf).parent_path();
#else
	char buf[PATH_MAX] = { 0 };
	ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (len <= 0)
	{
		LOG_INFO("Failed to get the executable path from /proc/self/exe");
		return {};
	}
	buf[len] = '\0';
	return std::filesystem::path(buf).parent_path();
#endif
}

// This is a helper function to return the fullpath of a file in Unicode

//Unicode Version
std::wstring getpathU(const char* dir, const char* file)
{
	std::filesystem::path path = exe_dir();

	// operator/= inserts the platform's preferred separator, so callers get
	// backslashes on Windows and forward slashes on Linux without this file
	// having to know which.
	if (dir)  path /= dir;
	if (file) path /= file;

	std::wstring result = path.wstring();
	LOG_INFO("getpathU returning path: %ls", result.c_str());
	return result;
}


// This is the non-wide version of this code.
std::string getpathM(const char* dir, const char* file)
{
	std::filesystem::path path = exe_dir();

	if (dir)  path /= dir;
	if (file) path /= file;

	std::string result = path.string();
	LOG_INFO("getpathM returning path: %s", result.c_str());
	return result;
}


