#include "m8080.h"

extern int screen_support_red;
extern int screen_red;
extern int player2;
extern int lrt_voice;




void InvadersSoundPort1Write(UINT16 port, UINT8 data, struct z80PortWrite *pPW)
{
	 static UINT8 bSound = 0;
}

void InvadersSoundPort3Write(UINT16 port, UINT8 data, struct z80PortWrite *pPW)
{

	static unsigned char Sound = 0;

	if (data & 0x01 && ~Sound & 0x01)
		sample_start (0, 0, 1);

	if (~data & 0x01 && Sound & 0x01)
		sample_stop (0);

	if (data & 0x02 && ~Sound & 0x02)
		sample_start (1, 1, 0);

	if (~data & 0x02 && Sound & 0x02)
		sample_stop (1);

	if (data & 0x04 && ~Sound & 0x04)
		sample_start (2, 2, 0);

	if (~data & 0x04 && Sound & 0x04)
		sample_stop (2);

	if (data & 0x08 && ~Sound & 0x08)
		sample_start (3, 3, 0);

	if (~data & 0x08 && Sound & 0x08)
		sample_stop (3);

	if((data&0x04) && screen_support_red ) {screen_red=1;}
	if (!(data&0x04) && screen_support_red ) {screen_red=0;}
	Sound = data;
  
}

void InvadersSoundPort5Write(UINT16 port, UINT8 data, struct z80PortWrite *pPW)
{	
	static unsigned char Sound = 0;
    static lastp2=0;

	if (data & 0x01 && ~Sound & 0x01)
		sample_start (4, 4, 0);

	if (data & 0x02 && ~Sound & 0x02)
		sample_start (5, 5, 0);

	if (data & 0x04 && ~Sound & 0x04)
		sample_start (6, 6, 0);

	if (data & 0x08 && ~Sound & 0x08)
		sample_start (7, 7, 0);

	if (data & 0x10 && ~Sound & 0x10)
		sample_start (8, 8, 0);

	if (~data & 0x10 && Sound & 0x10)
		sample_stop (5);

	player2 =(data & 0x20);
	if (player2) player2=1;else player2=0;
	Sound = data;
	
}




void LrescueSoundPort3Write(UINT16 port, UINT8 data, struct z80PortWrite *pPW)
{
	 static UINT8 bSound = 0;
	static lastp2=0;
	
      if(data&0x01 && ~bSound&0x01) play_sample(game_sounds[3],config.mainvol,128,1000,1);
      if(~data&0x01 && bSound&0x01) stop_sample(game_sounds[3]);
      if(data&0x02 && ~bSound&0x02) play_sample(game_sounds[2],config.mainvol,128,1000,0);
     // if(~data&0x02 && bSound&0x02) stop_sample(game_sounds[1]);
      if(data&0x04 && ~bSound&0x04) play_sample(game_sounds[1],config.mainvol,128,1000,0);
     // if(~data&0x04 && bSound&0x04) stop_sample(game_sounds[2]);
      if(data&0x08 && ~bSound&0x08) play_sample(game_sounds[0],config.mainvol,128,1000,0);
    //  if(~data&0x08 && bSound&0x08) stop_sample(game_sounds[3]);
      if(data&0x10 && ~bSound&0x10) play_sample(game_sounds[5],config.mainvol,128,1000,0);
    //  if(~data&0x10 && bSound&0x10) stop_sample(game_sounds[9]);
	
	  bSound = data;
	if((data&0x04) && screen_support_red ) {screen_red=1;}
	if (!(data&0x04) && screen_support_red ) {screen_red=0;}
	
}

void LrescueSoundPort5Write(UINT16 port, UINT8 data, struct z80PortWrite *pPW)
{	  
	  static lastp2=0;
	  static UINT8 bSound = 0;

     //speaker_level_w(0, (data & 0x08) ? 1 : 0);	/* Bitstream tunes - endlevel and bonus1 */

      if(data&0x01 && ~bSound&0x01) play_sample(game_sounds[8],config.mainvol,128,1000,0);
      if(data&0x02 && ~bSound&0x02) play_sample(game_sounds[7],config.mainvol,128,1000,0);
      if(data&0x04 && ~bSound&0x04) play_sample(game_sounds[4],config.mainvol,128,1000,0);
	  if(data&0x08 && ~bSound&0x08) { if (voice_get_position(lrt_voice)==-1){ voice_start(lrt_voice);}
	                        	    }
      
	  if(data&0x10 && ~bSound&0x10) play_sample(game_sounds[6],config.mainvol,128,1000,0);
      if(~data&0x10 && bSound&0x10) stop_sample(game_sounds[6]);
	
	bSound = data;
	player2 =(data & 0x20);
	if (player2) player2=1;else player2=0;
	bSound = data;
}