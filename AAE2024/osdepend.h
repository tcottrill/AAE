#ifndef OSDEPEND_H
#define OSDEPEND_H

#ifdef __cplusplus
extern "C" {
#endif

	struct osd_bitmap
	{
		int width, height;       /* width and height of the bitmap */
		int depth;		/* bits per pixel - ASG 980209 */
		void* privatebm; /* don't touch! - reserved for osdepend use */
		unsigned char** line; /* pointers to the start of each line */
	};

#define    OSD_KEY_NONE		     0
#define    OSD_KEY_A             1
#define    OSD_KEY_B             2
#define    OSD_KEY_C             3
#define    OSD_KEY_D             4
#define    OSD_KEY_E             5
#define    OSD_KEY_F             6
#define    OSD_KEY_G             7
#define    OSD_KEY_H             8
#define    OSD_KEY_I             9
#define    OSD_KEY_J             10
#define    OSD_KEY_K             11
#define    OSD_KEY_L             12
#define    OSD_KEY_M             13
#define    OSD_KEY_N             14
#define    OSD_KEY_O             15
#define    OSD_KEY_P             16
#define    OSD_KEY_Q             17
#define    OSD_KEY_R             18
#define    OSD_KEY_S             19
#define    OSD_KEY_T             20
#define    OSD_KEY_U             21
#define    OSD_KEY_V             22
#define    OSD_KEY_W             23
#define    OSD_KEY_X             24
#define    OSD_KEY_Y             25
#define    OSD_KEY_Z             26
#define    OSD_KEY_0             27
#define    OSD_KEY_1             28
#define    OSD_KEY_2             29
#define    OSD_KEY_3             30
#define    OSD_KEY_4             31
#define    OSD_KEY_5             32
#define    OSD_KEY_6             33
#define    OSD_KEY_7             34
#define    OSD_KEY_8             35
#define    OSD_KEY_9             36
#define    OSD_KEY_0_PAD         37
#define    OSD_KEY_1_PAD         38
#define    OSD_KEY_2_PAD         39
#define    OSD_KEY_3_PAD         40
#define    OSD_KEY_4_PAD         41
#define    OSD_KEY_5_PAD         42
#define    OSD_KEY_6_PAD         43
#define    OSD_KEY_7_PAD         44
#define    OSD_KEY_8_PAD         45
#define    OSD_KEY_9_PAD         46
#define    OSD_KEY_F1            47
#define    OSD_KEY_F2            48
#define    OSD_KEY_F3            49
#define    OSD_KEY_F4            50
#define    OSD_KEY_F5            51
#define    OSD_KEY_F6            52
#define    OSD_KEY_F7            53
#define    OSD_KEY_F8            54
#define    OSD_KEY_F9            55
#define    OSD_KEY_F10           56
#define    OSD_KEY_F11           57
#define    OSD_KEY_F12           58
#define    OSD_KEY_ESC           59
#define    OSD_KEY_TILDE         60
#define    OSD_KEY_MINUS         61
#define    OSD_KEY_EQUALS        62
#define    OSD_KEY_BACKSPACE     63
#define    OSD_KEY_TAB           64
#define    OSD_KEY_OPENBRACE     65
#define    OSD_KEY_CLOSEBRACE    66
#define    OSD_KEY_ENTER         67
#define    OSD_KEY_COLON         68
#define    OSD_KEY_QUOTE         69
#define    OSD_KEY_BACKSLASH     70
#define    OSD_KEY_BACKSLASH2    71
#define    OSD_KEY_COMMA         72
#define    OSD_KEY_STOP          73
#define    OSD_KEY_SLASH         74
#define    OSD_KEY_SPACE         75
#define    OSD_KEY_INSERT        76
#define    OSD_KEY_DEL           77
#define    OSD_KEY_HOME          78
#define    OSD_KEY_END           79
#define    OSD_KEY_PGUP          80
#define    OSD_KEY_PGDN          81
#define    OSD_KEY_LEFT          82
#define    OSD_KEY_RIGHT         83
#define    OSD_KEY_UP            84
#define    OSD_KEY_DOWN          85
#define    OSD_KEY_SLASH_PAD     86
#define    OSD_KEY_ASTERISK      87
#define    OSD_KEY_MINUS_PAD     88
#define    OSD_KEY_PLUS_PAD      89
#define    OSD_KEY_DEL_PAD       90
#define    OSD_KEY_ENTER_PAD     91
#define    OSD_KEY_PRTSCR        92
#define    OSD_KEY_PAUSE         93
#define    OSD_KEY_ABNT_C1       94
#define    OSD_KEY_YEN           95
#define    OSD_KEY_KANA          96
#define    OSD_KEY_CONVERT       97
#define    OSD_KEY_NOCONVERT     98
#define    OSD_KEY_AT            99
#define    OSD_KEY_CIRCUMFLEX    100
#define    OSD_KEY_COLON2        101
#define    OSD_KEY_KANJI         102
#define    OSD_KEY_EQUALS_PAD    103  /* MacOS X */
#define    OSD_KEY_BACKQUOTE     104  /* MacOS X */
#define    OSD_KEY_SEMICOLON     105  /* MacOS X */
#define    OSD_KEY_COMMAND       106  /* MacOS X */
#define    OSD_KEY_UNKNOWN1      107
#define    OSD_KEY_UNKNOWN2      108
#define    OSD_KEY_UNKNOWN3      109
#define    OSD_KEY_UNKNOWN4      110
#define    OSD_KEY_UNKNOWN5      111
#define    OSD_KEY_UNKNOWN6      112
#define    OSD_KEY_UNKNOWN7      113
#define    OSD_KEY_UNKNOWN8      114

#define    OSD_KEY_LSHIFT        115
#define    OSD_KEY_RSHIFT        116
#define    OSD_KEY_LCONTROL      117
#define    OSD_KEY_RCONTROL      118
#define    OSD_KEY_ALT           119
#define    OSD_KEY_ALTGR         120
#define    OSD_KEY_LWIN          121
#define    OSD_KEY_RWIN          122
#define    OSD_KEY_MENU          123
#define    OSD_KEY_SCRLOCK       124
#define    OSD_KEY_NUMLOCK       125
#define    OSD_KEY_CAPSLOCK      126

#define    OSD_MAX_KEY          127

	/* 116 - 119 */

	/* The following are defined in Allegro */
	/* 120 KEY_RCONTROL */
	/* 121 KEY_ALTGR */
	/* 122 KEY_SLASH2 */
	/* 123 KEY_PAUSE */

	/*
	 * ASG 980730: these are pseudo-keys that the os-dependent code can
	 * map to whatever they see fit
	 * HJB 980812: added some more names and used higher values because
	 * there were some clashes with Allegro's scancodes (see above)
	 */
#define OSD_KEY_FAST_EXIT			128
#define OSD_KEY_CANCEL				129
#define OSD_KEY_RESET_MACHINE		130
#define OSD_KEY_CONFIGURE			133
#define OSD_KEY_ON_SCREEN_DISPLAY	134
#define OSD_KEY_SHOW_GFX			135
#define OSD_KEY_FRAMESKIP_INC		136
#define OSD_KEY_FRAMESKIP_DEC		137
#define OSD_KEY_THROTTLE			138
#define OSD_KEY_SHOW_FPS			139
#define OSD_KEY_SHOW_PROFILE		140
#define OSD_KEY_SHOW_TOTAL_COLORS	141
#define OSD_KEY_SNAPSHOT			142
#define OSD_KEY_CHEAT_TOGGLE		143
#define OSD_KEY_DEBUGGER			144
#define OSD_KEY_UI_LEFT				145
#define OSD_KEY_UI_RIGHT			146
#define OSD_KEY_UI_UP				147
#define OSD_KEY_UI_DOWN				148
#define OSD_KEY_UI_SELECT			149
#define OSD_KEY_ANY					150
#define OSD_KEY_CHAT_ENABLE         151

#define OSD_MAX_PSEUDO				151

#define OSD_JOY_LEFT    1
#define OSD_JOY_RIGHT   2
#define OSD_JOY_UP      3
#define OSD_JOY_DOWN    4
#define OSD_JOY_FIRE1   5
#define OSD_JOY_FIRE2   6
#define OSD_JOY_FIRE3   7
#define OSD_JOY_FIRE4   8
#define OSD_JOY_FIRE5   9
#define OSD_JOY_FIRE6   10
#define OSD_JOY_FIRE7   11
#define OSD_JOY_FIRE8   12
#define OSD_JOY_FIRE9   13
#define OSD_JOY_FIRE10  14
#define OSD_JOY_FIRE    15      /* any of the first joystick fire buttons */
#define OSD_JOY2_LEFT   16
#define OSD_JOY2_RIGHT  17
#define OSD_JOY2_UP     18
#define OSD_JOY2_DOWN   19
#define OSD_JOY2_FIRE1  20
#define OSD_JOY2_FIRE2  21
#define OSD_JOY2_FIRE3  22
#define OSD_JOY2_FIRE4  23
#define OSD_JOY2_FIRE5  24
#define OSD_JOY2_FIRE6  25
#define OSD_JOY2_FIRE7  26
#define OSD_JOY2_FIRE8  27
#define OSD_JOY2_FIRE9  28
#define OSD_JOY2_FIRE10 29
#define OSD_JOY2_FIRE   30      /* any of the second joystick fire buttons */
#define OSD_JOY3_LEFT   31
#define OSD_JOY3_RIGHT  32
#define OSD_JOY3_UP     33
#define OSD_JOY3_DOWN   34
#define OSD_JOY3_FIRE1  35
#define OSD_JOY3_FIRE2  36
#define OSD_JOY3_FIRE3  37
#define OSD_JOY3_FIRE4  38
#define OSD_JOY3_FIRE5  39
#define OSD_JOY3_FIRE6  40
#define OSD_JOY3_FIRE7  41
#define OSD_JOY3_FIRE8  42
#define OSD_JOY3_FIRE9  43
#define OSD_JOY3_FIRE10 44
#define OSD_JOY3_FIRE   45      /* any of the third joystick fire buttons */
#define OSD_JOY4_LEFT   46
#define OSD_JOY4_RIGHT  47
#define OSD_JOY4_UP     48
#define OSD_JOY4_DOWN   49
#define OSD_JOY4_FIRE1  50
#define OSD_JOY4_FIRE2  51
#define OSD_JOY4_FIRE3  52
#define OSD_JOY4_FIRE4  53
#define OSD_JOY4_FIRE5  54
#define OSD_JOY4_FIRE6  55
#define OSD_JOY4_FIRE7  56
#define OSD_JOY4_FIRE8  57
#define OSD_JOY4_FIRE9  58
#define OSD_JOY4_FIRE10 59
#define OSD_JOY4_FIRE   60      /* any of the fourth joystick fire buttons */
#define OSD_MAX_JOY     60

	 /* We support 4 players for each analog control */
#define OSD_MAX_JOY_ANALOG	4
#define X_AXIS          1
#define Y_AXIS          2

#define COIN_COUNTERS	4	/* total # of coin counters */
#define NUMVOICES 16 //SOUND - PUT SOMEWHERE ELSE
/*
extern int video_sync;

int osd_init(int argc,char **argv);
void osd_exit(void);
struct osd_bitmap *osd_create_bitmap(int width,int height);
void osd_clearbitmap(struct osd_bitmap *bitmap);
void osd_free_bitmap(struct osd_bitmap *bitmap);
*/
/* Create a display screen, or window, large enough to accomodate a bitmap */
/* of the given dimensions. Attributes are the ones defined in driver.h. */
/* palette is an array of 'totalcolors' R,G,B triplets. The function returns */
/* in *pens the pen values corresponding to the requested colors. */
/* Return a osd_bitmap pointer or 0 in case of error. */
/*
struct osd_bitmap *osd_create_display(int width,int height,int totalcolors,
		const unsigned char *palette,unsigned char *pens,int attributes);
int osd_set_display(int width,int height,int attributes);
void osd_close_display(void);
void osd_modify_pen(int pen,unsigned char red, unsigned char green, unsigned char blue);
void osd_get_pen(int pen,unsigned char *red, unsigned char *green, unsigned char *blue);
void osd_mark_dirty(int x1, int y1, int x2, int y2, int ui);
void osd_update_display(void);
*/
//void osd_update_audio(void);
//#define osd_fread_msbfirst osd_fread_swap
//#define osd_fwrite_msbfirst osd_fwrite_swap
//#define osd_fread_lsbfirst osd_fread
//#define osd_fwrite_lsbfirst osd_fwrite

//void osd_play_sample(int channel,signed char *data,int len,int freq,int volume,int loop);
//void osd_play_sample_16(int channel,signed short *data,int len,int freq,int volume,int loop);
//void osd_play_streamed_sample(int channel,signed char *data,int len,int freq,int volume);
//void osd_play_streamed_sample_16(int channel,signed short *data,int len,int freq,int volume);
//void osd_adjust_sample(int channel,int freq,int volume);
//void osd_stop_sample(int channel);
//void osd_restart_sample(int channel);
//int osd_get_sample_status(int channel);
//void osd_ym2203_write(int n, int r, int v);
//void osd_ym2203_update(void);
//void osd_set_mastervolume(int volume);
	int scale_by_cycles(int val, int clock);
	/*
	int osd_key_pressed(int keycode);
	int osd_key_pressed_memory(int keycode);
	int osd_key_pressed_memory_repeat(int keycode,int speed);
	int osd_read_key_immediate(void);
	// the following two should return pseudo key codes if translate != 0
	int osd_read_keyrepeat(void);
	int osd_key_invalid(int keycode);
	const char *osd_joy_name(int joycode);
	const char *osd_key_name(int keycode);
	void osd_poll_joysticks(void);
	int osd_joy_pressed(int joycode);
	void msdos_init_input (void);
	void osd_trak_read(int player,int *deltax,int *deltay);
	// return values in the range -128 .. 128 (yes, 128, not 127)
	void osd_analogjoy_read(int player,int *analog_x, int *analog_y);
	*/
	//int osd_update_vectors(int *x_res, int *y_res, int step);
	//void osd_draw_to(int x, int y, int col);

	/* gamename holds the driver name, filename is only used for ROMs and samples. */
	/* if 'write' is not 0, the file is opened for write. Otherwise it is opened */
	/* for read. */
	//void *osd_fopen (const char *gamename,const char *filename,int filetype,int write);

	//int osd_fread (void *file,void *buffer,int length);
	//int osd_fread_swap(void *file,void *buffer,int length);
	//int osd_fread_scatter(void *file,void *buffer,int length,int increment);
	//int osd_fwrite (void *file,const void *buffer,int length);
	//int osd_fseek (void *file,int offset,int whence);
	//unsigned int osd_fcrc (void *file);
	//void osd_fclose (void *file);

	/* control keyboard leds or other indicators */
	//void osd_led_w(int led,int on);
	int osd_get_leds(void);
	void osd_set_leds(int state);
	/* config */
	//void osd_set_config(int def_samplerate, int def_samplebits);
	//void osd_save_config(int frameskip, int samplerate, int samplebits);
	//int osd_get_config_samplerate(int def_samplerate);
	//int osd_get_config_samplebits(int def_samplebits);
	//int osd_get_config_frameskip(int def_frameskip);

#ifdef __cplusplus
}
#endif
#endif
