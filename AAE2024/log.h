/*
BMFont example implementation with Kerning, for c++ and OpenGL 2.0

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#pragma once

#ifndef LOG_H
#define LOG_H

#include <stdio.h>

#define LogOpen(f) Log::open((char *)(f))
#define LogClose() Log::close()
#define LOG_DEBUG(str,...) Log::write("%s: Function %s: Line# %d DEBUG " str, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(str,...) Log::write("%s: Function %s: Line# %d ERROR " str, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define WRLOG(str,...) Log::write( str, ##__VA_ARGS__)
#define wrlog(str,...) Log::write( str, ##__VA_ARGS__)
#define write_to_log(str,...) Log::write( str, ##__VA_ARGS__)

namespace Log
{
int open(const char *filename);
int write(const char *format, ...);
void close();


}
 
#endif