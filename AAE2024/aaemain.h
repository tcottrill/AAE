#ifndef AAEMAIN_H
#define AAEMAIN_H

#pragma warning(disable:4996 4102)

//============================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM. 
//============================================================================


int mystrcmp(const char* s1, const char* s2);
void sort_games(void);
void run_game(void);
//void reset_to_default_keys();
//void ListDisplaySettings(void);
void SetGammaRamp(double gamma, double bright, double contrast);
void reset_for_new_game(int new_gamenum, int in_giu);
//void test_keys();
#endif