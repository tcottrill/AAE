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
#include "log.h"
FILE *stream;

namespace Log
{
	int open(const char *filename)
	{
		errno_t err;

		if ((err = fopen_s(&stream, filename, "w")) != NULL) return 0; /* Opened succesfully. */

		return -1; /* Failed. */
	}

	int write(const char *format, ...)
	{
		va_list ptr; /* get an arg pointer */
		int status = -1;

		if (stream != NULL)
		{
			/* initialize ptr to point to the first argument after the format string */
			va_start(ptr, format);

			/* Write to logfile. */
			status = vfprintf(stream, format, ptr); // Write passed text.
			fprintf(stream, "\n"); // New line..

			va_end(ptr);

			/* Flush file. */
			fflush(stream);
		}

		return status;
	}


	void close()
	{
		WRLOG("Closing log, ending program.");
		if (stream != NULL)
		{
			fflush(stream);
			fclose(stream);
		}
	}

}