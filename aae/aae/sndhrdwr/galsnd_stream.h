#pragma once

// -----------------------------------------------------------------------------
// galsnd_stream.h — Galaxian-style streaming (tone + LFO motor only)
// Mono S16, integer FPS. Partial updates supported.
//
// Lifecycle:
//   int  galaxian_sh_start_stream(const MachineSound* msound);
//   void galaxian_sh_stop_stream(void);
//   void galaxian_sh_update_stream(void);
//   void galaxian_doupdate_stream(void);
//
// Register writes (use 'offset' as provided):
//   void galaxian_pitch_w(int data);                    // 0x7800
//   void galaxian_vol_w(int offset, int data);          // 0x6806..0x6807 (bit0/bit1)
//   void galaxian_background_enable_w(int offset, int data); // 0x6800..0x6802 (3 lines)
//   void galaxian_lfo_freq_w(int offset, int data);     // 0x6004..0x6007 (4-bit DAC)
//
// Tuning:
//   void galaxian_set_lfo_speed_scale(double scale);    // e.g., 1.00..1.25
// -----------------------------------------------------------------------------

#include <cstdint>

struct MachineSound;

// Lifecycle
int  galaxian_sh_start_stream(const MachineSound* msound);
void galaxian_sh_stop_stream(void);
void galaxian_sh_update_stream(void);
void galaxian_doupdate_stream(void);

// Tone
void galaxian_pitch_w(int data);                   // 0x7800
void galaxian_vol_w(int offset, int data);         // 0x6806..0x6807

// LFO / background
void galaxian_background_enable_w(int offset, int data); // 0x6800..0x6802
void galaxian_lfo_freq_w(int offset, int data);          // 0x6004..0x6007

// Tuning
void galaxian_set_lfo_speed_scale(double scale);
