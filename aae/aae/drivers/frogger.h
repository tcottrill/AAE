
extern void frogger_vh_convert_color_prom(unsigned char* palette, unsigned char* colortable, const unsigned char* color_prom);
int  init_frogger();
void run_frogger();
void end_frogger();
void frogger_interrupt();
void frogger_sound_interrupt(void);

extern struct GfxDecodeInfo frogger_gfxdecodeinfo[];