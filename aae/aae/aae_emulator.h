#pragma once

void emulator_init(int argc, char** argv);
void emulator_run();
void emulator_end();

// -----------------------------------------------------------------------------
// Game switching API (GUI-safe)
// -----------------------------------------------------------------------------
bool emulator_start_game(int newGameNum);
void emulator_stop_game();
bool emulator_is_game_running();
int  emulator_get_current_game();
bool emulator_apply_pending_switch();
void emulator_request_switch(int gameNum);

// -----------------------------------------------------------------------------
// Exit confirmation dialog API
// Called by glcode.cpp to know whether to draw the confirm overlay.
//   get_exit_confirm_status()    - returns 1 if dialog is active, 0 if not
//   get_exit_confirm_selection() - returns 0 for YES highlighted, 1 for NO highlighted
// -----------------------------------------------------------------------------
int  get_exit_confirm_status();
int  get_exit_confirm_selection();
// -----------------------------------------------------------------------------
// GUI mode detection
// Returns true when the currently running "game" is the GUI frontend driver.
// Use this anywhere you need to distinguish GUI context from actual gameplay,
// for example to decide whether to save settings to aae.ini vs a game ini.
// -----------------------------------------------------------------------------
bool emulator_is_gui_active();

// -----------------------------------------------------------------------------
// emulator_exit_now
// Ends the process from a point that cannot reach WinMain's shutdown sequence,
// which is where RawInput, the LED service, the render context and the log are
// torn down. Their worker threads are still running at that point, and a
// joinable std::thread destructor calls std::terminate, so the C++ static
// destructors must NOT run - that is what made the list-and-exit options abort
// with 0xC0000409 after doing their job correctly.
//
// Stops the LED service, closes the log, then _Exit(code). Anything else the
// caller wants flushed must already be on disk.
// -----------------------------------------------------------------------------
[[noreturn]] void emulator_exit_now(int code);



