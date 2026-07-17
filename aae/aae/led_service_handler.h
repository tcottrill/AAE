
void osd_led_service_start();
void osd_led_service_stop();

void osd_set_leds(int state);
int osd_get_leds();
void set_led_status(int which, int on);
int get_led_status(int which);
void set_led_status_all(int led0, int led1, int led2);