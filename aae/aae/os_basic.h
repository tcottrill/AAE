#ifndef __OSSTUFF__
#define __OSSTUFF__

//void force_all_kbdleds_off();
void ClientResize( int nWidth, int nHeight);
void GetDesktopResolution(int &horizontal, int &vertical);
void GetRefresh();
void LimitThreadAffinityToCurrentProc();
int osd_get_leds();
void SetProcessorAffinity();
void osd_set_leds(int state);

void SetTopMost(const bool TopMost);
void setwindow();
void center_window();
void Set_ForeGround();

#endif

