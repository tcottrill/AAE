#ifndef __OSSTUFF__
#define __OSSTUFF__

//void force_all_kbdleds_off();
void ClientResize( int nWidth, int nHeight);
void GetDesktopResolution(int &horizontal, int &vertical);
void GetRefresh();
void LimitThreadAffinityToCurrentProc();
void SetProcessorAffinity();
void SetTopMost(const bool TopMost);
void setwindow();
void center_window();
void Set_ForeGround();



#endif

