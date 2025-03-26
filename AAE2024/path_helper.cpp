#include "path_helper.h"
#include <windows.h> // For MAX_PATH & GetCurrentDirectory
#include "utf8conv.h"
#include "log.h"

#pragma warning(disable:4996)
// This is a helper function to return the fullpath of a file

//Unicode Version
std::wstring getpathU(const char* dir, const char* file)
{
	std::wstring path;
	wchar_t temppath[MAX_PATH] = { 0 }; // Buffer to hold the path

	DWORD length = GetModuleFileNameW(NULL, temppath, MAX_PATH);

	if (length == 0)
	{
		// If the function fails, it returns 0
		wrlog("Failed to get the file path. Error: %ld\n", GetLastError());
	}

	// Find the last backslash and terminate the string there
	wchar_t* lastBackslash = wcsrchr(temppath, '\\');
	if (lastBackslash != NULL) {
		*lastBackslash = '\0'; // End the string at the last backslash
	}

	path.assign(temppath);

	if (dir)
	{
		path.append(win32::Utf8ToUtf16("\\"));
		path.append(win32::Utf8ToUtf16(dir));
	}
	if (file)
	{
		path.append(win32::Utf8ToUtf16("\\"));
		path.append(win32::Utf8ToUtf16(file));
	}

	return path;
}


// This is the non-wide version of this code. Need to update the entire program to unicode at some point
std::string getpathM(const char* dir, const char* file)
{
	std::string path;
	char temppath[MAX_PATH] = { 0 }; // Buffer to hold the path

	DWORD length = GetModuleFileNameA(NULL, temppath, MAX_PATH);

	if (length == 0)
	{
		// If the function fails, it returns 0
		wrlog("Failed to get the file path. Error: %ld\n", GetLastError());
	}

	// Find the last backslash and terminate the string there
	char* lastBackslash = strrchr(temppath, '\\');
	if (lastBackslash != NULL) {
		*lastBackslash = '\0'; // End the string at the last backslash
	}

	path.assign(temppath);

	if (dir)
	{
		path.append("\\");
		path.append(dir);
	}
	if (file)
	{
		path.append("\\");
		path.append(file);
	}

	return path;
}


