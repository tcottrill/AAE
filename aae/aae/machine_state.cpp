// -----------------------------------------------------------------------------
// machine_state.cpp
//
// Storage for the single global `Machine` pointer (declared `extern` in
// aae_mame_driver.h) that cpu_control.cpp, memory.cpp and nearly every game
// driver dereference. This used to live inside aae_emulator.cpp, which is an
// executable-side (OSD) translation unit. Linking aae_core alone therefore
// failed with an unresolved external for `Machine`, even though nothing in
// the emulation core actually needs anything else from aae_emulator.cpp.
//
// Pulling just these two lines into their own aae_core translation unit gives
// any core-only consumer (the headless/Teensy target included) a complete
// link without dragging in run_game()/emulator_run() and the OSD subsystems
// they interleave with (GL, artwork, audio, NVRAM, window aspect). This file
// exists on its own, rather than folding into memory.cpp or driver_registry.cpp,
// so that intent stays obvious from the file listing alone.
// -----------------------------------------------------------------------------
#include "aae_mame_driver.h"

static struct RunningMachine machine;
struct RunningMachine* Machine = &machine;
