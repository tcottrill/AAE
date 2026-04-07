#ifndef namco_h
#define namco_h

struct namco_interface
{
	int samplerate; /* sample rate */
	int voices;     /* number of voices */
	int volume;     /* playback volume */
	int region;     /* memory region; -1 to use RAM (pointed to by namco_wavedata) */
	int stereo;     /* set to 1 to indicate stereo */
};

int  namco_sh_start(struct namco_interface* intf);
void namco_sh_stop(void);
void namco_sh_update(void);
void namco_update(short* buffer, int len);
void doupdate(void);

/* compatibility wrapper for older Pengo-style callers */
void namco_sound_w(int offset, int data);

/* newer handlers */
void pengo_sound_enable_w(int offset, int data);
void pengo_sound_w(int offset, int data);

void mappy_sound_enable_w(int offset, int data);
void mappy_sound_w(int offset, int data);

/* globals expected by drivers */
extern unsigned char* namco_soundregs;
extern unsigned char* namco_wavedata;

#define pengo_soundregs namco_soundregs
#define mappy_soundregs namco_soundregs

#endif