/*
	Asynchronous Logging System (Windows 11 / C++11+ Compatible)

	Description:
	------------
	This is a high-performance, thread-safe logging system designed for modern
	Windows systems (Windows 11+) using C++11 or newer. It supports asynchronous
	logging via a dedicated background thread, colored console output, log levels,
	formatted messages, timestamps, and source location tagging.

	Features:
	---------
	- C++11+ compatible (requires threads, atomics, variadic templates)
	- Asynchronous, non-blocking logging (background thread)
	- Log levels: DEBUG, INFO, ERROR, OFF
	- Optional timestamped log entries (define LOG_WITH_TIMESTAMP)
	- File, function, and line tracking
	- Optional colored console output (WinAPI)
	- Secure formatted output using vsnprintf_s
	- Public domain (Unlicense)

	Usage:
	------
		LogOpen("log.txt");               // Start the logging system
		LOG_INFO("App started");          // Log an info message
		LOG_DEBUG("x = %d", x);           // Log a debug message
		LOG_ERROR("Something failed");    // Log an error
		LogClose();                       // Shutdown and flush log

		Log::setLevel(Log::Level::Info);  // Set minimum log level
		Log::setConsoleOutputEnabled(true); // Enable colored console output

	Notes:
	------
	- Messages are queued and written asynchronously.
	- Console output is optional and color-coded per log level.
	- LOG_WITH_TIMESTAMP must be defined **before** including the header
	  to enable timestamp output.

	License:
	--------
	This is free and unencumbered software released into the public domain.
	For more information, please refer to <http://unlicense.org/>
*/

#pragma once

#include <string>

//#define LOG_WITH_TIMESTAMP

namespace Log {

    enum class Level {
        Debug = 0,
        Info = 1,
        Error = 2,
        Off = 3
    };

    // Initializes the logging system and starts the background thread.
    // Returns false if the log file could not be opened.
    bool open(const std::string& filename);

    // Flushes and shuts down logging system.
    void close();

    // Writes a formatted log message at the specified level with source info.
    void write(Level level, const char* file, const char* function, int line, const char* format, ...);

    // Change the minimum level of messages to log.
    void setLevel(Level level);

    // Enable or disable also writing messages to the console.
    void setConsoleOutputEnabled(bool enabled);
}

#define LogOpen(f) Log::open(f)
#define LogClose() Log::close()
// Logging macros  safe, fast, and non-blocking.
#define LOG_DEBUG(fmt, ...) Log::write(Log::Level::Debug, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  Log::write(Log::Level::Info,  __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Log::write(Log::Level::Info,  __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Log::write(Log::Level::Error, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)