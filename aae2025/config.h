#ifndef CONFIG_H
#define CONFIG_H


typedef struct {
	char rompath[256];
	char samplepath[256];
	
	int kreset;
	int ktest;
	int ktestadv;
	int kpause;
	int ksnap;

	int drawzero;
	int widescreen;
	int overlay;
	int colordepth;
	int screenw;
	int screenh;
	int windowed;
	int language;
	int translucent;
	float translevel;
	int lives;

	int m_line;
	int m_point;
	int monitor;

	float linewidth;
	float pointsize;

	int gamma;
	int bright;
	int contrast;
	int gain;
	int fire_point_size;
	int explode_point_size;
	//int colorhack;
	int shotsize;
	int cocktail;
	int mainvol;
	int pokeyvol;
	int artwork;
	int bezel;
	int burnin;
	int artcrop;
	int vid_rotate;
	int vecglow;
	int vectrail;

	int psnoise;
	int hvnoise;
	int pshiss;
	int noisevol;
	int snappng;

	char* aspect;
	int prescale;
	int anisfilter;
	int priority;
	int forcesync;
	int dblbuffer;
	int showinfo; //Show readme info message
	char* exrompath; //optional path for roms
	int hack;
	int cheat;
	int debug;
	int debug_profile_code;
	int audio_force_resample;
	int kbleds;
	int samplerate;
}settings;

// This setting required c++ 17 to compile
inline settings config;

void setup_video_config();
void setup_config(void);
void setup_game_config(void);

void my_set_config_int(const char* section, const char* key, int val, int path);
void my_set_config_float(const char* section, const char* key, float val, int path);
void my_set_config_string(const char* section, const char* key, const char* val, int path);

#endif // CONFIG_H
