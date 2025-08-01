#include "sys_log.h"

#include <cstdarg>  // <-- Required for va_start, va_list, va_end
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sstream>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <windows.h>
#include <io.h>
#include <fcntl.h>

static WORD levelToColor(Log::Level level) {
	switch (level) {
	case Log::Level::Debug: return FOREGROUND_INTENSITY;
	case Log::Level::Info:  return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; // bright white
	case Log::Level::Error: return FOREGROUND_RED | FOREGROUND_INTENSITY;
	default:                return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	}
}


static const char* baseName(const char* path) {
	const char* slash = strrchr(path, '\\');
	return slash ? slash + 1 : path;
}

namespace {
	FILE* stream = nullptr;
	std::mutex configMutex;
	Log::Level currentLogLevel = Log::Level::Debug;
	bool consoleOutput = true;

	std::mutex queueMutex;
	std::condition_variable queueCV;
	std::queue<std::string> messageQueue;
	std::thread logThread;
	std::atomic<bool> running = false;

	static std::atomic<bool> consoleOutputEnabled = false;
	static std::once_flag consoleInitFlag;

	const char* levelToString(Log::Level level) {
		switch (level) {
		case Log::Level::Debug: return "DEBUG";
		case Log::Level::Info:  return "INFO ";
		case Log::Level::Error: return "ERROR";
		default:                return "UNKNOWN";
		}
	}

	std::string currentTimeString() {
		std::time_t now = std::time(nullptr);
		struct tm timeInfo;
		localtime_s(&timeInfo, &now);
		char buffer[32];
		std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
		return buffer;
	}

	void logWorker() {
		while (running.load() || !messageQueue.empty()) {
			std::unique_lock<std::mutex> lock(queueMutex);
			queueCV.wait(lock, [] {
				return !messageQueue.empty() || !running.load();
				});

			while (!messageQueue.empty()) {
				std::string msg = std::move(messageQueue.front());
				messageQueue.pop();
				lock.unlock();

				// Write to file
				if (stream) {
					std::fputs(msg.c_str(), stream);
					std::fflush(stream);
				}

				// Optional console output
				if (consoleOutput) {
					std::cout << msg;
				}

				lock.lock();
			}
		}
	}

	void enqueueMessage(std::string&& msg) {
		{
			std::lock_guard<std::mutex> lock(queueMutex);
			messageQueue.push(std::move(msg));
		}
		queueCV.notify_one();
	}
}

namespace Log {
	bool open(const std::string& filename) {
		std::lock_guard<std::mutex> lock(configMutex);

		if (stream) {
			std::fclose(stream);
			stream = nullptr;
		}

		errno_t err = fopen_s(&stream, filename.c_str(), "w");
		if (err != 0 || !stream)
			return false;

		running = true;
		logThread = std::thread(logWorker);
		return true;
	}

	void close() {
		{
			std::lock_guard<std::mutex> lock(configMutex);
			if (!running) return;
			running = false;
		}

		queueCV.notify_all();
		if (logThread.joinable())
			logThread.join();

		// Flush any remaining messages
		std::lock_guard<std::mutex> lock(queueMutex);
		while (!messageQueue.empty()) {
			const std::string& msg = messageQueue.front();
			if (stream)
				std::fputs(msg.c_str(), stream);
			if (consoleOutput)
				std::cout << msg;
			messageQueue.pop();
		}

		if (stream) {
			std::fflush(stream);
			std::fclose(stream);
			stream = nullptr;
		}
	}

	void setLevel(Level level) {
		std::lock_guard<std::mutex> lock(configMutex);
		currentLogLevel = level;
	}

	void setConsoleOutputEnabled(bool enabled) {
		std::lock_guard<std::mutex> lock(configMutex);
		consoleOutput = enabled;

		if (enabled) {
			std::call_once(consoleInitFlag, []() {
				AllocConsole();
				freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
				freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

				// Optional: set text mode
				_setmode(_fileno(stdout), _O_TEXT);
				_setmode(_fileno(stderr), _O_TEXT);

				std::cout.clear();
				std::cerr.clear();
				});
		}
	}

	void write(Level level, const char* file, const char* function, int line, const char* format, ...) {
		if (level < currentLogLevel)
			return;

		constexpr size_t kMaxMessageSize = 2048;
		char formatted[kMaxMessageSize];

		va_list args;
		va_start(args, format);
		vsnprintf_s(formatted, kMaxMessageSize, _TRUNCATE, format, args);
		va_end(args);

		std::ostringstream oss;
#ifdef LOG_WITH_TIMESTAMP
		oss << "[" << currentTimeString() << "] ";
#endif
		oss << levelToString(level) << " ("
			<< baseName(file) << ":" << line << " " << function << ") - "
			<< formatted << "\n";

		enqueueMessage(std::move(oss.str()));

		if (consoleOutputEnabled) {
			HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
			CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
			WORD originalAttributes = 0;

			if (GetConsoleScreenBufferInfo(hConsole, &consoleInfo)) {
				originalAttributes = consoleInfo.wAttributes;
				SetConsoleTextAttribute(hConsole, levelToColor(level));
			}

			std::cout << oss.str();

			if (originalAttributes)
				SetConsoleTextAttribute(hConsole, originalAttributes);
		}
	}
}