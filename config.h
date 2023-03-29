#ifndef CONFIG_H
#define CONFIG_H

#include "aaemain.h"

void setup_video_config();
void setup_config(void);
void setup_game_config(void);
void my_set_config_int(char *section, char *name, int val, int path);
void my_set_config_float(char *section, char *name, float val,int path);
int my_get_config_int( char *section, char *name, int def);
char* my_get_config_string(char* section, char* name, char* def);

#endif