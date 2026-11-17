//==========================================================================
// process_result.cpp - see process_result.h.
//==========================================================================
#include "process_result.h"
#include "sys_log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace
{
	// Bounds. The frontend captures a limited amount of stderr, so everything
	// here is deliberately small.
	constexpr size_t kMaxDetailLines   = 40;
	constexpr size_t kMaxDetailBytes   = 8 * 1024;
	constexpr size_t kMaxLineChars     = 512;    // one summary / detail line
	constexpr size_t kMaxNameChars     = 200;    // one sample name
	constexpr size_t kMaxWarnPayload   = 1000;   // YAMFE_WARNING payload
	constexpr size_t kMaxSamples       = 256;    // stored sample entries
	constexpr size_t kMaxMessageChars  = 2048;
	constexpr size_t kMaxMessagesSeen  = 64;

	struct SampleEntry
	{
		ProcresSampleKind kind;
		std::string       text;   // sanitized "name" or "name (reason)"
		std::string       name;   // sanitized name, for de-duplication
	};

	struct State
	{
		std::mutex mtx;

		std::atomic<bool> frontend{ false };

		int         fatalCode = 0;
		std::string summary;
		std::vector<std::string> details;
		size_t      detailBytes = 0;
		size_t      detailDropped = 0;

		std::vector<SampleEntry> samples;
		size_t      sampleOverflow = 0;

		std::vector<std::string> messagesSeen;

		bool        finished = false;
		int         finishCode = 0;

		ProcresSink sink = nullptr;
	};

	// Leaked on purpose: nothing may depend on static destruction order, and
	// the entry points call procres_finish() long before the process ends.
	State& S()
	{
		static State* s = new State();
		return *s;
	}

	std::string vformat(const char* fmt, va_list ap)
	{
		if (!fmt) return std::string();
		va_list ap2;
		va_copy(ap2, ap);
		const int n = vsnprintf(nullptr, 0, fmt, ap2);
		va_end(ap2);
		if (n <= 0) return std::string();
		std::string out((size_t)n + 1, '\0');
		vsnprintf(&out[0], out.size(), fmt, ap);
		out.resize((size_t)n);
		return out;
	}

	// Largest cut point <= max that does not split a UTF-8 sequence.
	size_t utf8_cut(const std::string& s, size_t max)
	{
		if (s.size() <= max) return s.size();
		size_t cut = max;
		while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80)
			--cut;
		return cut;
	}

	// One line of text: control characters and newlines become spaces, runs of
	// them collapse, the ends are trimmed, and the length is bounded.
	std::string sanitize(const std::string& in, size_t maxChars)
	{
		std::string out;
		out.reserve(in.size() < maxChars ? in.size() : maxChars);
		bool lastSpace = true;   // also trims leading blanks
		for (unsigned char c : in)
		{
			const bool ctl = (c < 0x20 || c == 0x7F);
			if (ctl || c == ' ')
			{
				if (!lastSpace) out.push_back(' ');
				lastSpace = true;
			}
			else
			{
				out.push_back((char)c);
				lastSpace = false;
			}
		}
		while (!out.empty() && out.back() == ' ') out.pop_back();
		if (out.size() > maxChars)
		{
			out.resize(utf8_cut(out, maxChars));
			while (!out.empty() && out.back() == ' ') out.pop_back();
		}
		return out;
	}

	// ----- transport -------------------------------------------------------

#ifdef _WIN32
	void platform_write(const char* data, size_t len)
	{
		HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
		if (!h || h == INVALID_HANDLE_VALUE) return;

		// A real console wants UTF-16; WriteFile would send our UTF-8 through
		// the console code page and garble non-ASCII names.
		DWORD mode = 0;
		if (GetConsoleMode(h, &mode))
		{
			const int wn = MultiByteToWideChar(CP_UTF8, 0, data, (int)len, nullptr, 0);
			if (wn > 0)
			{
				std::wstring w((size_t)wn, L'\0');
				MultiByteToWideChar(CP_UTF8, 0, data, (int)len, &w[0], wn);
				size_t done = 0;
				while (done < w.size())
				{
					DWORD wrote = 0;
					if (!WriteConsoleW(h, w.data() + done, (DWORD)(w.size() - done), &wrote, nullptr) || wrote == 0)
						break;
					done += wrote;
				}
			}
			return;
		}

		// Redirected handle (pipe or file): raw bytes, handling partial writes.
		// The handle is inherited and is never closed here.
		size_t done = 0;
		while (done < len)
		{
			size_t chunk = len - done;
			if (chunk > (1u << 20)) chunk = (1u << 20);
			DWORD wrote = 0;
			if (!WriteFile(h, data + done, (DWORD)chunk, &wrote, nullptr) || wrote == 0)
				break;
			done += wrote;
		}
	}
#else
	void platform_write(const char* data, size_t len)
	{
		// A closed pipe must not kill the process with SIGPIPE, and a failed
		// write must never change the exit code.
		void (*old)(int) = signal(SIGPIPE, SIG_IGN);
		size_t done = 0;
		int spins = 0;
		while (done < len)
		{
			const ssize_t n = write(2, data + done, len - done);
			if (n > 0) { done += (size_t)n; spins = 0; continue; }
			if (n < 0 && errno == EINTR) continue;
			if (n < 0 && errno == EAGAIN && ++spins < 1000) { usleep(1000); continue; }
			break;
		}
		if (old != SIG_ERR) signal(SIGPIPE, old);
	}
#endif

	void emit(State& s, const std::string& text)
	{
		if (text.empty()) return;
		if (s.sink) s.sink(text.data(), text.size());
		else        platform_write(text.data(), text.size());
	}

	bool ieq(const char* a, const char* b)
	{
		for (; *a && *b; ++a, ++b)
		{
			char x = *a, y = *b;
			if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
			if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
			if (x != y) return false;
		}
		return *a == *b;
	}

	bool ends_with_wav(const char* s)
	{
		const size_t n = strlen(s);
		if (n < 4) return false;
		return ieq(s + n - 4, ".wav");
	}

	// Add a detail line (state lock held).
	void add_detail_locked(State& s, const std::string& raw)
	{
		const std::string line = sanitize(raw, kMaxLineChars);
		if (line.empty()) return;
		if (s.details.size() >= kMaxDetailLines || s.detailBytes + line.size() + 1 > kMaxDetailBytes)
		{
			++s.detailDropped;
			return;
		}
		s.detailBytes += line.size() + 1;
		s.details.push_back(line);
	}
}

// ---------------------------------------------------------------------------

void procres_set_frontend_mode(bool on) { S().frontend = on; }
bool procres_frontend_mode()            { return S().frontend.load(); }

void procres_fatal(int code, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	const std::string msg = sanitize(vformat(fmt, ap), kMaxLineChars);
	va_end(ap);
	if (code == 0) code = AAE_EXIT_FATAL;   // a fatal always has a non-zero code

	bool log = false;
	{
		State& s = S();
		std::lock_guard<std::mutex> lk(s.mtx);
		if (s.fatalCode == 0)
		{
			s.fatalCode = code;
			s.summary   = msg;
		}
		else
		{
			add_detail_locked(s, msg);   // secondary: never replaces the primary
		}
		log = !s.finished;
	}
	// The logger may already be closed once finish has run.
	if (log && !msg.empty())
		LOG_ERROR("%s", msg.c_str());
}

void procres_detail(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	const std::string msg = vformat(fmt, ap);
	va_end(ap);

	State& s = S();
	std::lock_guard<std::mutex> lk(s.mtx);
	add_detail_locked(s, msg);
}

bool procres_has_fatal()
{
	State& s = S();
	std::lock_guard<std::mutex> lk(s.mtx);
	return s.fatalCode != 0;
}

int procres_fatal_code()
{
	State& s = S();
	std::lock_guard<std::mutex> lk(s.mtx);
	return s.fatalCode;
}

void procres_discard_recovered()
{
	State& s = S();
	std::lock_guard<std::mutex> lk(s.mtx);
	s.fatalCode = 0;
	s.summary.clear();
	s.details.clear();
	s.detailBytes = 0;
	s.detailDropped = 0;
}

void procres_begin_attempt()
{
	State& s = S();
	std::lock_guard<std::mutex> lk(s.mtx);
	s.samples.clear();
	s.sampleOverflow = 0;
}

void procres_optional_sample(ProcresSampleKind kind, const char* name, const char* reason)
{
	const std::string n = sanitize(name ? name : "", kMaxNameChars);
	if (n.empty()) return;
	std::string text = n;
	if (reason)
	{
		const std::string r = sanitize(reason, kMaxNameChars);
		if (!r.empty()) text += " (" + r + ")";
	}

	State& s = S();
	std::lock_guard<std::mutex> lk(s.mtx);
	for (const SampleEntry& e : s.samples)
		if (e.name == n) return;
	if (s.samples.size() >= kMaxSamples)
	{
		++s.sampleOverflow;
		return;
	}
	s.samples.push_back({ kind, text, n });
}

void procres_message(const char* title, const char* text)
{
	std::string body = sanitize(text ? text : "", kMaxMessageChars);
	if (body.empty()) return;

	State& s = S();
	std::string line;
	{
		std::lock_guard<std::mutex> lk(s.mtx);
		for (const std::string& seen : s.messagesSeen)
			if (seen == body) return;
		if (s.messagesSeen.size() < kMaxMessagesSeen)
			s.messagesSeen.push_back(body);

		const std::string t = sanitize(title ? title : "", kMaxLineChars);
		line = "AAE: ";
		if (!t.empty()) { line += t; line += ": "; }
		line += body;
		line += "\n";
	}
	emit(s, line);
}

int procres_finish()
{
	State& s = S();
	std::string out;
	{
		std::lock_guard<std::mutex> lk(s.mtx);
		if (s.finished) return s.finishCode;
		s.finished   = true;
		s.finishCode = s.fatalCode;

		if (s.fatalCode != 0)
		{
			out = s.summary.empty() ? std::string("AAE: fatal error") : s.summary;
			out += "\n";
			for (const std::string& d : s.details)
			{
				out += d;
				out += "\n";
			}
			if (s.detailDropped)
				out += "... and " + std::to_string(s.detailDropped) + " more\n";
		}
		else if (!s.samples.empty())
		{
			// Build "Missing optional samples: a, b; Unreadable optional
			// samples: c (reason)" as one bounded payload.
			static const char* const kLabel[2] = {
				"Missing optional samples: ", "Unreadable optional samples: " };
			std::string payload;
			size_t omitted = s.sampleOverflow;
			bool full = false;
			for (int kind = 0; kind < 2; ++kind)
			{
				bool groupStarted = false;
				for (const SampleEntry& e : s.samples)
				{
					if ((int)e.kind != kind) continue;
					if (full) { ++omitted; continue; }

					std::string add;
					if (!groupStarted) add = (payload.empty() ? "" : "; ") + std::string(kLabel[kind]) + e.text;
					else               add = ", " + e.text;

					// Keep room for the " (and N more)" suffix.
					if (!payload.empty() && payload.size() + add.size() + 24 > kMaxWarnPayload)
					{
						full = true;
						++omitted;
						continue;
					}
					payload += add;
					groupStarted = true;
				}
			}
			if (omitted)
				payload += " (and " + std::to_string(omitted) + " more)";
			out = "YAMFE_WARNING: " + payload + "\n";
		}
	}
	emit(s, out);
	return s.finishCode;
}

void procres_set_sink(ProcresSink sink)
{
	State& s = S();
	std::lock_guard<std::mutex> lk(s.mtx);
	s.sink = sink;
}

void procres_reset_for_test()
{
	State& s = S();
	std::lock_guard<std::mutex> lk(s.mtx);
	s.frontend = false;
	s.fatalCode = 0;
	s.summary.clear();
	s.details.clear();
	s.detailBytes = 0;
	s.detailDropped = 0;
	s.samples.clear();
	s.sampleOverflow = 0;
	s.messagesSeen.clear();
	s.finished = false;
	s.finishCode = 0;
}

// ---------------------------------------------------------------------------
// Command line helpers
// ---------------------------------------------------------------------------

bool aae_wavwrite_value_follows(const char* next)
{
	if (!next || !next[0] || next[0] == '-') return false;
	if (ends_with_wav(next)) return true;
	return strchr(next, '/') != nullptr || strchr(next, '\\') != nullptr;
}

int aae_option_value_count(int argc, char** argv, int i)
{
	// Options that always take the following token as their value.
	static const char* const kValueOpts[] = {
		"-renderer", "-prescale", "-aspect", "-width", "-height",
		"-monitor", "-monitorid", nullptr };

	if (!argv || i < 1 || i >= argc || !argv[i] || argv[i][0] != '-') return 0;
	if (i + 1 >= argc || !argv[i + 1]) return 0;
	const char* tok = argv[i];

	for (int k = 0; kValueOpts[k]; ++k)
		if (ieq(tok, kValueOpts[k])) return 1;

	if (ieq(tok, "-debug"))
		return (strcmp(argv[i + 1], "0") == 0 || strcmp(argv[i + 1], "1") == 0) ? 1 : 0;

	if (ieq(tok, "-wavwrite"))
		return aae_wavwrite_value_follows(argv[i + 1]) ? 1 : 0;

	return 0;
}

int aae_find_game_arg(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
	{
		const char* tok = argv[i];
		if (!tok || !tok[0]) continue;

		if (tok[0] == '-' || tok[0] == '/')
		{
			i += aae_option_value_count(argc, argv, i);
			continue;
		}
		return i;
	}
	return -1;
}

bool aae_cmdline_has(int argc, char** argv, const char* name)
{
	if (!name) return false;
	for (int i = 1; i < argc; ++i)
		if (argv[i] && ieq(argv[i], name)) return true;
	return false;
}
