#include "allegro.h"
#include "winalleg.h"


#ifndef MENU_H
#define MENU_H

void do_the_menu(void);
void do_root_menu(void);
void do_video_menu(void);
void do_joystick_menu(void);
void do_keyboard_menu(void);
void do_mouse_menu(void);
void do_settings_menu(void);
void setup_video_menu(void);
void setup_sound_menu(void);
void setup_mouse_menu(void);

void save_video_menu(void);
void save_sound_menu();
void check_sound_menu();
void check_video_menu();
void save_mouse_menu();
void change_menu_level(int dir);
void change_menu_item(int dir);
void select_menu_item(void);
void change_menu(void);
void change_key(void);
void do_sound_menu(void);
void reset_menu();
void set_points_lines();

#endif 