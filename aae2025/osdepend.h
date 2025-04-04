#ifndef OSDEPEND_H
#define OSDEPEND_H

struct osd_bitmap
{
	int width, height;       /* width and height of the bitmap */
	int depth;		/* bits per pixel - ASG 980209 */
	void* privatebm; /* don't touch! - reserved for osdepend use */
	unsigned char **line; /* pointers to the start of each line */

};


#define    OSD_KEY_NONE		     0
#define    OSD_KEY_A             0x41
#define    OSD_KEY_B             0x42
#define    OSD_KEY_C             0x43
#define    OSD_KEY_D             0x44
#define    OSD_KEY_E             0x45
#define    OSD_KEY_F             0x46
#define    OSD_KEY_G             0x47
#define    OSD_KEY_H             0x48
#define    OSD_KEY_I             0x49
#define    OSD_KEY_J             0x4a
#define    OSD_KEY_K             0x4b
#define    OSD_KEY_L             0x4c
#define    OSD_KEY_M             0x4d
#define    OSD_KEY_N             0x4e
#define    OSD_KEY_O             0x4f
#define    OSD_KEY_P             0x50
#define    OSD_KEY_Q             0x51
#define    OSD_KEY_R             0x52
#define    OSD_KEY_S             0x53
#define    OSD_KEY_T             0x54
#define    OSD_KEY_U             0x55
#define    OSD_KEY_V             0x56
#define    OSD_KEY_W             0x57
#define    OSD_KEY_X             0x58
#define    OSD_KEY_Y             0x59
#define    OSD_KEY_Z             0x5a
#define    OSD_KEY_0             0x30
#define    OSD_KEY_1             0x31
#define    OSD_KEY_2             0x32
#define    OSD_KEY_3             0x33
#define    OSD_KEY_4             0x34
#define    OSD_KEY_5             0x35
#define    OSD_KEY_6             0x36
#define    OSD_KEY_7             0x37
#define    OSD_KEY_8             0x38
#define    OSD_KEY_9             0x39
#define    OSD_KEY_0_PAD         0x60
#define    OSD_KEY_1_PAD         0x61
#define    OSD_KEY_2_PAD         0x62
#define    OSD_KEY_3_PAD         0x63
#define    OSD_KEY_4_PAD         0x64
#define    OSD_KEY_5_PAD         0x65
#define    OSD_KEY_6_PAD         0x66
#define    OSD_KEY_7_PAD         0x67
#define    OSD_KEY_8_PAD         0x68
#define    OSD_KEY_9_PAD         0x69
#define    OSD_KEY_F1            0x70
#define    OSD_KEY_F2            0x71
#define    OSD_KEY_F3            0x72
#define    OSD_KEY_F4            0x73
#define    OSD_KEY_F5            0x74
#define    OSD_KEY_F6            0x75
#define    OSD_KEY_F7            0x76
#define    OSD_KEY_F8            0x77
#define    OSD_KEY_F9            0x78
#define    OSD_KEY_F10           0x79
#define    OSD_KEY_F11           0x7a
#define    OSD_KEY_F12           0x7b
#define    OSD_KEY_ESC           0x1b
#define    OSD_KEY_TILDE         0xc0
#define    OSD_KEY_MINUS         0xbd
#define    OSD_KEY_EQUALS        0xbb
#define    OSD_KEY_BACKSPACE     0x08
#define    OSD_KEY_TAB           0x09
#define    OSD_KEY_OPENBRACE     0xdb
#define    OSD_KEY_CLOSEBRACE    0xdd
#define    OSD_KEY_ENTER         0x0d
#define    OSD_KEY_COLON         0xba
#define    OSD_KEY_QUOTE         0xde
#define    OSD_KEY_BACKSLASH     0xdc
#define    OSD_KEY_BACKSLASH2    0xdc
#define    OSD_KEY_COMMA         0xbc 
#define    OSD_KEY_STOP          0xbe
#define    OSD_KEY_SLASH         0xbf
#define    OSD_KEY_SPACE         0x20
#define    OSD_KEY_INSERT        0x2d
#define    OSD_KEY_DEL           0x2e
#define    OSD_KEY_HOME          0x24
#define    OSD_KEY_END           0x23
#define    OSD_KEY_PGUP          0x21
#define    OSD_KEY_PGDN          0x22
#define    OSD_KEY_LEFT          0x25
#define    OSD_KEY_RIGHT         0x27
#define    OSD_KEY_UP            0x26
#define    OSD_KEY_DOWN          0x28
#define    OSD_KEY_SLASH_PAD     0x6f
#define    OSD_KEY_ASTERISK      0x6a
#define    OSD_KEY_MINUS_PAD     0x6d
#define    OSD_KEY_PLUS_PAD      0x6b
#define    OSD_KEY_DEL_PAD       0x6e
#define    OSD_KEY_ENTER_PAD     0x6c
#define    OSD_KEY_PRTSCR        0x2c
#define    OSD_KEY_PAUSE         0x13
#define    OSD_KEY_ABNT_C1       0xc1
//#define    OSD_KEY_YEN           0
//#define    OSD_KEY_KANA          0
//#define    OSD_KEY_CONVERT       0
//#define    OSD_KEY_NOCONVERT     0
//#define    OSD_KEY_AT            0
//#define    OSD_KEY_CIRCUMFLEX    0
//#define    OSD_KEY_COLON2        0
//#define    OSD_KEY_KANJI         0
//#define    OSD_KEY_EQUALS_PAD    0
//#define    OSD_KEY_BACKQUOTE     0
#define    OSD_KEY_SEMICOLON     0x3b
//#define    OSD_KEY_COMMAND       0
//#define    OSD_KEY_UNKNOWN1      0
//#define    OSD_KEY_UNKNOWN2      0
//#define    OSD_KEY_UNKNOWN3      0
//#define    OSD_KEY_UNKNOWN4      0
//#define    OSD_KEY_UNKNOWN5      0
//#define    OSD_KEY_UNKNOWN6      0
//#define    OSD_KEY_UNKNOWN7      0
//#define    OSD_KEY_UNKNOWN8      0
#define    OSD_KEY_LSHIFT       0xa0 
#define    OSD_KEY_RSHIFT       0xa1
#define    OSD_KEY_LCONTROL     0xa2 
#define    OSD_KEY_RCONTROL     0xa3
#define    OSD_KEY_ALT          0xa4 
#define    OSD_KEY_ALTGR        0xa5
#define    OSD_KEY_LWIN         0x5b 
#define    OSD_KEY_RWIN         0x5c 
#define    OSD_KEY_MENU         0xa4 
#define    OSD_KEY_SCRLOCK      0x91 
#define    OSD_KEY_NUMLOCK      0x90 
#define    OSD_KEY_CAPSLOCK     0x14 

#define    OSD_MAX_KEY          199

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
#define OSD_KEY_FAST_EXIT			200
#define OSD_KEY_CANCEL				201
#define OSD_KEY_RESET_MACHINE		202
#define OSD_KEY_CONFIGURE			203
#define OSD_KEY_ON_SCREEN_DISPLAY	204
#define OSD_KEY_SHOW_GFX			205
#define OSD_KEY_FRAMESKIP_INC	    206
#define OSD_KEY_FRAMESKIP_DEC		207
#define OSD_KEY_THROTTLE			208
#define OSD_KEY_SHOW_FPS			209
#define OSD_KEY_SHOW_PROFILE		210
#define OSD_KEY_SHOW_TOTAL_COLORS	211
#define OSD_KEY_SNAPSHOT			212
#define OSD_KEY_CHEAT_TOGGLE		213
#define OSD_KEY_DEBUGGER			214
#define OSD_KEY_UI_LEFT				215
#define OSD_KEY_UI_RIGHT			216
#define OSD_KEY_UI_UP				217
#define OSD_KEY_UI_DOWN				218
#define OSD_KEY_UI_SELECT			219
#define OSD_KEY_ANY					220
#define OSD_KEY_CHAT_ENABLE         221

#define OSD_MAX_PSEUDO				222//151

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
//int scale_by_cycles(int val, int clock);

//int osd_key_pressed(int keycode);
//int osd_key_pressed_memory(int keycode);
//int osd_key_pressed_memory_repeat(int keycode,int speed);
//int osd_read_key_immediate(void);
/* the following two should return pseudo key codes if translate != 0 */
//int osd_read_keyrepeat(void);
//int osd_key_invalid(int keycode);
//const char *osd_joy_name(int joycode);
//const char *osd_key_name(int keycode);
//void osd_poll_joysticks(void);
//int osd_joy_pressed(int joycode);
//void msdos_init_input (void);

//void osd_trak_read(int player,int *deltax,int *deltay);

/* return values in the range -128 .. 128 (yes, 128, not 127) */
//void osd_analogjoy_read(int player,int *analog_x, int *analog_y);

//int osd_update_vectors(int *x_res, int *y_res, int step);
//void osd_draw_to(int x, int y, int col);



/* file handling routines 
#define OSD_FILETYPE_ROM 1
#define OSD_FILETYPE_SAMPLE 2
#define OSD_FILETYPE_HIGHSCORE 3
#define OSD_FILETYPE_CONFIG 4
#define OSD_FILETYPE_INPUTLOG 5
#define OSD_FILETYPE_STATE 6
#define OSD_FILETYPE_ARTWORK 7
#define OSD_FILETYPE_MEMCARD 8
#define OSD_FILETYPE_SCREENSHOT 9
*/

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

#endif
