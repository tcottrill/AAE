#ifndef AAEMAIN_H
#define AAEMAIN_H
//#define ALLEGRO_STATICLINK  1
//#define STATICLINK 1
#pragma warning(disable:4996 4102)


int mystrcmp(const char *s1, const char *s2);
void sort_games(void);
void run_game(void);
//void reset_to_default_keys();
//void ListDisplaySettings(void);
void SetGammaRamp(double gamma, double bright, double contrast);
void reset_for_new_game();
//void test_keys();
#endif