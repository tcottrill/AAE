//==============================================================================
// sys_str.h -- small portable string helpers.
//
// Exists because case-insensitive comparison is the one common string
// operation the C standard never specified: MSVC spells it _stricmp, POSIX
// spells it strcasecmp (<strings.h>), and neither is available on the other.
//==============================================================================
#pragma once

#ifdef _WIN32
#include <string.h>
#else
#include <strings.h>
#endif

inline int aae_stricmp(const char* a, const char* b)
{
#ifdef _WIN32
	return _stricmp(a, b);
#else
	return strcasecmp(a, b);
#endif
}

inline int aae_strnicmp(const char* a, const char* b, size_t n)
{
#ifdef _WIN32
	return _strnicmp(a, b, n);
#else
	return strncasecmp(a, b, n);
#endif
}

//------------------------------------------------------------------------------
// Bounded string copy with the destination size, replacing MSVC's strcpy_s /
// strncpy_s(..., _TRUNCATE).
//
// snprintf is the portable spelling that gets BOTH halves right: it truncates
// to the buffer AND always null-terminates. Plain strncpy does not - it leaves
// the destination unterminated on overflow, which is the exact bug the _s
// functions were introduced to avoid, so "just use strncpy" would be a
// regression rather than a port.
//------------------------------------------------------------------------------
#include <cstdio>

inline void aae_strcpy(char* dst, size_t dstSize, const char* src)
{
	if (!dst || dstSize == 0) return;
	snprintf(dst, dstSize, "%s", src ? src : "");
}

inline void aae_strncpy(char* dst, size_t dstSize, const char* src)
{
	aae_strcpy(dst, dstSize, src);
}
