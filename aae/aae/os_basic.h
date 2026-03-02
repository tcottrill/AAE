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

void set_led_status(int which, int on);
int get_led_status(int which);
void set_led_status_all(int led0, int led1, int led2);

void SetTopMost(const bool TopMost);
void setwindow();
void center_window();
void Set_ForeGround();

void osd_led_service_start();
void osd_led_service_stop();

#endif

