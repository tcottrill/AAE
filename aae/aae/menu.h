#pragma once

#ifndef MENU_H
#define MENU_H

// ----------------------------------------------------------------------
// Artwork Availability Flags
// ----------------------------------------------------------------------
// These flags must be set by your texture/artwork loading code after it
// attempts to load each piece of art. When a flag is 0, the corresponding
// menu item will be shown as disabled (grayed, "NOT LOADED") and will not
// accept user input. The menu re-queries these flags each time the Video
// submenu is opened, so they always reflect the current load state.
//
// Usage in your texture/artwork loader:
//
//   #include "menu.h"
//
//   // After attempting to load artwork:
//   g_artworkAvailable = (texture_loaded_ok) ? 1 : 0;
//   g_overlayAvailable = (overlay_loaded_ok) ? 1 : 0;
//   g_bezelAvailable   = (bezel_loaded_ok)   ? 1 : 0;
//   // artcrop depends on bezel being available:
//   g_artcropAvailable = g_bezelAvailable;
//
// These are plain ints (not bools) to stay consistent with the rest of
// the MAME-heritage codebase where int is used for boolean config fields.

extern int g_artworkAvailable;   // 1 = artwork texture loaded ok
extern int g_overlayAvailable;   // 1 = overlay texture loaded ok
extern int g_bezelAvailable;     // 1 = bezel texture loaded ok
extern int g_artcropAvailable;   // 1 = crop-bezel mode is usable (requires bezel)

// ----------------------------------------------------------------------
// Legacy-compatible interface (called from os_basic.cpp / winmain.cpp)
// ----------------------------------------------------------------------

int  get_menu_status();
void set_menu_status(int on);
int  get_menu_level();
void set_menu_level_top();
// Go back one menu level (nested submenus return to their parent, e.g.
// the monitor setups return to VIDEO SETUP; top-level submenus to root).
void menu_navigate_back();
void do_the_menu();
void change_menu_level(int dir);
void change_menu_item(int dir);
void select_menu_item();
void set_points_lines();

// ----------------------------------------------------------------------
// First-run notice
// ----------------------------------------------------------------------
// A one-shot acknowledgement panel drawn over whatever runs first -- the GUI
// frontend, or a game launched straight from the command line. It uses the
// same blue panel as the menu and waits for any key or joystick button, then
// clears aae.ini [main] first_run so it never appears again.
//
// first_run_notice_active() is what input handlers gate on: while it returns
// non-zero they must swallow their input so the dismissing press does not
// also toggle the menu, move the GUI selection, or exit.
int  first_run_notice_active();
void do_the_first_run_notice();

#endif // MENU_H
