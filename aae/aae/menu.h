#pragma once

#ifndef MENU_H
#define MENU_H

// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E.™ Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain © the M.A.M.E.™ Project and its
//     respective contributors under their original terms of distribution.
//   - Modifications, enhancements, and new code are © 2025 Tim Cottrill and
//     released under the GNU General Public License v3 (GPLv3) or later.
//   - Redistribution must preserve both this notice and the original MAME
//     copyright acknowledgement.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// Original Copyright:
//   This file is originally part of and copyright the M.A.M.E.™ Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------

int get_menu_status();
void set_menu_status(int on);
int get_menu_level();
void set_menu_level_top();
void do_the_menu();
void do_root_menu();
void do_video_menu();
int do_joystick_menu(int type);
int do_gamejoy_menu(int type);
void do_keyboard_menu(int type);
void do_gamekey_menu(int type);
int do_mouse_menu();
void do_dipswitch_menu();
void setup_video_menu();
void setup_sound_menu();
void setup_mouse_menu();
void save_video_menu();
void save_sound_menu();
void check_sound_menu();
void check_video_menu();
void save_mouse_menu();
void change_menu_level(int dir);
void change_menu_item(int dir);
void select_menu_item();
//void change_menu();
int change_key();
void do_sound_menu();
void reset_menu();
void set_points_lines();

#endif 