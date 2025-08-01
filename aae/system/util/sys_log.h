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

// Logging macros — safe, fast, and non-blocking.
#define LogOpen(f) Log::open(f)
#define LogClose() Log::close()
#define LOG_DEBUG(fmt, ...) Log::write(Log::Level::Debug, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  Log::write(Log::Level::Info,  __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Log::write(Log::Level::Error, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)