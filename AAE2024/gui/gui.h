#ifndef GUI_H
#define GUI_H

#include "aaemain.h"

int init_gui();
void gui_animate(int reset);
void StartGame();
void run_gui();
void gui_loop();
void gui_input(int from, int to);
void shot_animate(int i);
void end_gui();

#endif
