#pragma once

#ifndef MENU_H
#define MENU_H

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
void change_menu();
int change_key();
void do_sound_menu();
void reset_menu();
void set_points_lines();

#endif 