#pragma once

//OS Input configuration from M.A.M.E.(TM)

void os_init_input(void);
static int pseudo_to_key_code(int keycode);
int osd_key_invalid(int keycode);
int osd_key_pressed(int keycode);
int osd_key_pressed_memory(int keycode);
int osd_key_pressed_memory_repeat(int keycode, int speed);
int osd_read_key_immediate(void);
const char* osd_key_name(int keycode);
const char* osd_joy_name(int joycode);
void osd_poll_joysticks(void);
int osd_joy_pressed(int joycode);
void osd_analogjoy_read(int player, int* analog_x, int* analog_y);
void osd_trak_read(int player, int* deltax, int* deltay);

