
#include "osdepend.h"

// osd_led_service_start/stop and osd_set_leds/osd_get_leds are declared in
// osdepend.h as part of the OSD LED contract.
void set_led_status(int which, int on);
int get_led_status(int which);
void set_led_status_all(int led0, int led1, int led2);