#pragma once


//============================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
// 
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.  
//============================================================================

#ifndef OS_INPUT_H
#define OS_INPUT_H

int osd_key_pressed(int keycode);
int osd_key_pressed_memory(int keycode);
int osd_key_pressed_memory_repeat(int keycode, int speed);
int osd_read_key_immediate(void);
/* the following two should return pseudo key codes if translate != 0 */
int osd_read_keyrepeat(void);

int osd_key_invalid(int keycode);
const char* osd_joy_name(int joycode);
const char* osd_key_name(int keycode);
void osd_poll_joysticks(void);
int osd_joy_pressed(int joycode);
void msdos_init_input(void);
void osd_trak_read(int player, int* deltax, int* deltay);
/* return values in the range -128 .. 128 (yes, 128, not 127) */
void osd_analogjoy_read(int player, int* analog_x, int* analog_y);

#endif