//==========================================================================
// process_result.h - process exit codes and stderr diagnostics.
//
// AAE reports why it could not run to a launching frontend (Yamfe) the way
// MAME does: through the process exit code (MAME src/emu/main.h meanings) and
// a human-readable explanation on stderr. This module is the single place that
// state lives. It is platform neutral and depends on nothing but the logger,
// so aae_core, aae_headless and both entry points can share it.
//
// Life cycle
//   - Anything that decides the process cannot do its job calls
//     procres_fatal() (and procres_detail() for per-item lines). The FIRST
//     fatal wins; later ones are only kept as extra detail. Nothing here
//     exits the process: the caller unwinds normally.
//   - Optional audio problems are recorded with procres_optional_sample().
//   - The entry point ends with `return procres_finish();`, which writes the
//     diagnostics to stderr exactly once and yields the exit code.
//
// The module's state is separate from the emulator's per-game state
// (have_error and friends), so resets between games never erase it.
//==========================================================================
#pragma once

#include <cstddef>

// Process exit codes (MAME src/emu/main.h). 1 (validity checks) and 7-9 (ROM
// identification utilities) are deliberately unused by AAE.
enum AaeExitCode
{
	AAE_EXIT_OK              = 0,
	AAE_EXIT_MISSING_FILES   = 2,  // required files missing or unusable
	AAE_EXIT_FATAL           = 3,  // other fatal error
	AAE_EXIT_DEVICE          = 4,  // window / renderer / input init failure
	AAE_EXIT_NO_SUCH_GAME    = 5,  // explicitly requested game does not exist
	AAE_EXIT_INVALID_CONFIG  = 6   // invalid configuration or command line value
};

// Frontend mode: AAE was launched by another program that reads our exit code
// and stderr. It changes where notices go (stderr, never a modal dialog).
void procres_set_frontend_mode(bool on);
bool procres_frontend_mode();

// Record a fatal error. `code` is an AaeExitCode (non-zero). The first call
// supplies the exit code and the summary line; later calls are appended to the
// detail lines and never replace it. printf-style.
#if defined(__GNUC__) || defined(__clang__)
void procres_fatal(int code, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void procres_detail(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#else
void procres_fatal(int code, const char* fmt, ...);
void procres_detail(const char* fmt, ...);
#endif

// Detail line under the primary summary (for example one per missing ROM).
// Bounded: at most 40 lines / 8 KiB are kept, the rest become "... and N more".

// True once a fatal has been recorded, and its code (0 when none).
bool procres_has_fatal();
int  procres_fatal_code();

// Forget the fatal and its details. Only for a failure that was recovered from
// (an interactive game switch that fell back to the GUI): such a failure does
// not decide the process result.
void procres_discard_recovered();

// Start of a game start attempt: forgets that attempt's optional sample
// warnings. Never touches the fatal.
void procres_begin_attempt();

enum ProcresSampleKind
{
	PROCRES_SAMPLE_MISSING    = 0,   // not found anywhere
	PROCRES_SAMPLE_UNREADABLE = 1    // found but empty or not decodable
};

// Record an optional sample problem. Deduplicated by name; control characters
// and newlines are replaced by spaces. `reason` may be null.
void procres_optional_sample(ProcresSampleKind kind, const char* name, const char* reason);

// Frontend-mode transport for allegro_message style notices: writes
// "AAE: <title>: <text>" (newlines flattened) to stderr immediately.
// Identical text is written once.
void procres_message(const char* title, const char* text);

// Emit the final diagnostics to stderr, ONCE, and return the exit code.
//   fatal recorded  -> summary line, then the detail lines;
//   otherwise       -> at most one "YAMFE_WARNING: ..." line for optional
//                      samples (never emitted when a fatal exists).
// Idempotent: later calls return the same code and write nothing. Does not
// use the logger, so it is safe after the log has been closed.
int procres_finish();

// Test hooks. Redirects every write (procres_message and procres_finish) to
// `sink` instead of the real stderr; pass nullptr to restore. Reset clears all
// state including frontend mode and the "already finished" latch.
typedef void (*ProcresSink)(const char* data, size_t len);
void procres_set_sink(ProcresSink sink);
void procres_reset_for_test();

// ---------------------------------------------------------------------------
// Command line helpers shared by every argv parser.
// ---------------------------------------------------------------------------

// How many following tokens argv[i] consumes as its value: 1 when argv[i] is an
// option that takes a value here (always for -renderer, -prescale, -aspect,
// -width, -height, -monitor, -monitorid; only when the next token is "0"/"1"
// for -debug; only when it looks like a path for -wavwrite), else 0.
int aae_option_value_count(int argc, char** argv, int i);

// Index of the first non-option token (the game name), or -1. Values of
// options that take one (-renderer v, -prescale n, -aspect a:b, -width n,
// -height n, -monitor n, -monitorid id, plus the optional values of -debug and
// -wavwrite) are skipped. Options compare case-insensitively.
int aae_find_game_arg(int argc, char** argv);

// True if `name` (e.g. "-frontend") appears as an argument, case-insensitively.
bool aae_cmdline_has(int argc, char** argv, const char* name);

// The one definition of "does this token after -wavwrite look like its path":
// it does not start with '-' and either ends in .wav (any case) or contains a
// path separator.
bool aae_wavwrite_value_follows(const char* next);
