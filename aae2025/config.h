#ifndef CONFIG_H
#define CONFIG_H

void setup_video_config();
void setup_config(void);
void setup_game_config(void);
void my_set_config_int(const char* section, const char* name, int val, int path);
void my_set_config_float(const char* section, const char* name, float val, int path);
int my_get_config_int(const char* section, const char* name, int def);
char* my_get_config_string(const char* section, const char* name, const char* def);

#endif