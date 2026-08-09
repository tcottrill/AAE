#pragma once
//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
//
// CONTROLLER GUIDE -- a one-page vector-drawn help screen for couch/gamepad
// users: traced Xbox controller with button callouts, the chord table, and
// the game-list controls. Shown once when a gamepad-class device is first
// detected (config.controller_help_shown, [main] controller_help_shown in
// aae.ini), and on demand from the game-list GUI (Y) or the CONTROLLER HELP
// menu entry.
//
// controller_help_active() is the input gate, same contract as
// first_run_notice_active(): while non-zero, input handlers swallow their
// input so the dismissing press does not also fire whatever it is bound to.

int  controller_help_active();
void controller_help_open();
void do_the_controller_help();   // per-frame: trigger check, draw, dismissal

// True while the guide is up over a real game (not the GUI frontend).
// video_loop() ORs this into its menu pause handling so the game stays
// frozen (and audio muted) under the overlay, exactly like the menu.
int  controller_help_wants_pause();
