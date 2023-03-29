#include "missile.h"

//Color to Black and White Conversion
//mono.rgb = (0.2125 * color.r) + (0.7154 * color.g) + (0.0721 * color.b);

extern char *gamename[];
extern gamenum;

unsigned char *RAM;
unsigned char *missile_videoram;
int missile_flipscreen=0x40;
int h_pos=0;
int v_pos=0;
int ctrld = 0;
/*
static int readbit( const unsigned char *src,int bitnum)
{
	return (src[bitnum / 8] >> (7 - bitnum % 8)) & 1;
}
*/
unsigned char mipalette[] =
{
	0x00,0x00,0x00,   /* black      */
	0xff,0xff,0xff,   /* darkpurple */
	0xd8,0x00,0x00,   /* darkred    */
	0xf8,0x64,0xd8,   /* pink       */
	0x00,0xd8,0x00,   /* darkgreen  */
	0x00,0xf8,0xd8,   /* darkcyan   */
	0xd8,0xd8,0x94,   /* darkyellow */
	0xd8,0xf8,0xd8,   /* darkwhite  */
	0xf8,0x94,0x44,   /* orange     */
	0x00,0x00,0xd8,   /* blue   */
	0xf8,0x00,0x00,   /* red    */
	0xff,0x00,0xff,   /* purple */
	0x00,0xf8,0x00,   /* green  */
	0x00,0xff,0xff,   /* cyan   */
	0xf8,0xf8,0x00,   /* yellow */
	0xff,0xff,0xff    /* white  */
};


struct roms mmissile[] =
{
 
  { "035820.02", 0x5000, 0x0800, ROM_LOAD_NORMAL },
  { "035821.02", 0x5800, 0x0800, ROM_LOAD_NORMAL },
  { "035822.02", 0x6000, 0x0800, ROM_LOAD_NORMAL },
  { "035823.02", 0x6800, 0x0800, ROM_LOAD_NORMAL },
  { "035824.02", 0x7000, 0x0800, ROM_LOAD_NORMAL },
  { "035825.02", 0x7800, 0x0800, ROM_LOAD_NORMAL },
  { "035825.02", 0xf800, 0x0800, ROM_LOAD_NORMAL },//reload
  {NULL, 0, 0, 0}
};

     

struct MemoryReadByte MissileRead[] =
{
	
	{ 0x4800, 0x4800, IN0_Read },
	{ 0x4900, 0x4900, IN1_Read },
	{ 0x4a00, 0x4a00, Missile_Dip1 },
	{ 0x4000, 0x400f, Missile_Dip2 },

	{(UINT32) -1,	(UINT32) -1,		NULL}
};

struct MemoryWriteByte MissileWrite[] =
{
	
	{ 0x4000, 0x400f, PokeyWriteA},
	{ 0x4800, 0x4800, IN0_Write },
	//{ 0x4c00, 0x4c00, NoWrite }, //Watchdog
	//{ 0x4d00, 0x4d00, NoWrite }, //IRQ ACK
	{ 0x5000, 0xffff, NoWrite }, //ROM
	{(UINT32) -1,	(UINT32) -1,		NULL}
};


void IN0_Write(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)
{           static data1=0;
            static data2=0;
			if ((~data >> 1) !=data1) {data1=~data >> 1;}
			if ((~data >> 2) !=data2) {data2=~data >> 2;}
	        set_aae_leds(data1,data2, 0 ); 
			ctrld = data & 1;
			write_to_log("Data %d",data);
			gameImage[address]=data;
}

void PokeyWriteA(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)
{	
	Update_pokey_sound( address,data,chip,gain);
	gameImage[address] = data;
	
}

UINT8 IN0_Read (UINT32 address , struct MemoryReadByte *psMemRead)
{
    if (ctrld)	// trackball 
  	    //return ((readinputport(5) << 4) & 0xf0) | (readinputport(4) & 0x0f);
	     return 0x80;
	else	/* buttons */
		return 0x03;//(readinputport(0));
}
UINT8 IN1_Read (UINT32 address , struct MemoryReadByte *psMemRead)
{
	return 0x60; //67
}

UINT8 Missile_Dip1 (UINT32 address , struct MemoryReadByte *psMemRead)
{
	return 0x02;
}

UINT8 Missile_Dip2 (UINT32 address , struct MemoryReadByte *psMemRead)
{
	write_to_log("POKEY READ %x",address);
	if (address==0x400a){return rand();}
	else
	return 0x84;
}



int missile_IN0_r(int offset)
{
	if (ctrld)	// trackball 
  	    //return ((readinputport(5) << 4) & 0xf0) | (readinputport(4) & 0x0f);
	     return 0x80;
	else	/* buttons */
		return 0x33;//(readinputport(0));
}


/*
void missile_w(int address, int data)
{
	int pc, opcode;
	//extern unsigned char *RAM;

    m6502zpGetContext(psCpu1);	
	pc = psCpu1->m6502pc;
	opcode = gameImage[pc];

	address += 0x640;

	// 3 different ways to write to video ram - the third is caught by the core memory handler 
	if (opcode == 0x81)
	{
		// 	STA ($00,X)
		missile_video_w (address, data);
		return;
	}
	if (address <= 0x3fff)
	{
		missile_video_mult_w (address, data);
		return;
	}

	 $4c00 - watchdog 
	if (address == 0x4c00)
	{
		write_to_log("Watchdog Write");
		//watchdog_reset_w (address, data);
		return;
	}

	 $4800 - various IO 
	if (address == 0x4800)
	{
		if (RAM[address] != data)
		{
			if (missile_flipscreen != (!(data & 0x40)))
				missile_flip_screen ();
			missile_flipscreen = !(data & 0x40);
			//coin_counter_w (0, data & 0x20);
			//coin_counter_w (1, data & 0x10);
			//coin_counter_w (2, data & 0x08);
			set_aae_leds(~data >> 1,~data >> 2, 0 ); 
			ctrld = data & 1;
			RAM[address] = data;
			gameImage[address]=data;
		}
		return;
	}

	 $4d00 - IRQ acknowledge 
	if (address == 0x4d00)
	{
		return;
	}

	// $4000 - $400f - Pokey 
	if (address >= 0x4000 && address <= 0x400f)
	{
		pokey1_w (address, data);
		return;
	}

	// $4b00 - $4b07 - color RAM 
	if (address >= 0x4b00 && address <= 0x4b07)
	{
		int r,g,b;


		r = 0xff * ((~data >> 3) & 1);
		g = 0xff * ((~data >> 2) & 1);
		b = 0xff * ((~data >> 1) & 1);

		//palette_change_color(address - 0x4b00,r,g,b);

		return;
	}

	
} 



int missile_r (int address)
{
	int pc, opcode;
	//extern unsigned char *RAM;

    write_to_log("Read - address %x",address);
	m6502zpGetContext(psCpu1);	
	pc = psCpu1->m6502pc;
	opcode = RAM[pc];

	address += 0x1900;

	if (opcode == 0xa1)
	{
		// 	LDA ($00,X)  
		return (missile_video_r(address));
	}

	if (address >= 0x5000)
		return RAM[address];

	if (address == 0x4800)
		return (missile_IN0_r(0));
	if (address == 0x4900)
		return 0x67;//(readinputport (1));//e7
	if (address == 0x4a00)
		return 0x02;//(readinputport (2));

	if ((address >= 0x4000) && (address <= 0x400f))
		return (pokey1_r (address & 0x0f));

	 write_to_log ("possible unmapped read, offset: %04x\n", address);
	return RAM[address];
}




int missile_video_r (int address)
{
	return (missile_videoram[address] & 0xe0);
}



void missile_flip_screen (void) //Redraw entire bitmap
{
	int x, y;
	//int temp;

	for (y = 0; y < 115; y ++)
	{
		for (x = 0; x < 256; x ++)
		{
			//temp = Machine->scrbitmap->line[y][x];
			//Machine->scrbitmap->line[y][x] = Machine->scrbitmap->line[230-y][255-x];
			//Machine->scrbitmap->line[230-y][255-x] = temp;
		}
	}
	// flip the middle line 
	for (x = 0; x < 128; x ++)
	{
		//temp = Machine->scrbitmap->line[115][x];
		//Machine->scrbitmap->line[115][x] = Machine->scrbitmap->line[115][255-x];
		//Machine->scrbitmap->line[115][255-x] = temp;
	}
	//osd_mark_dirty (0,0,255,230,0);
}


void missile_blit_w (int offset)
{
	int x, y;
	int bottom;
	int color;

	 The top 25 lines ($0000 -> $18ff) aren't used or drawn *
	y = (offset >> 8) - 25;
	x = offset & 0xff;
	if( y < 231 - 32)
		bottom = 1;
	else
		bottom = 0;

	if (Machine->orientation & ORIENTATION_SWAP_XY)
	{
		int tmp;

		tmp = x;
		x = y;
		y = tmp;
	}

	


 void missile_video_w (int address,int data)
{
	 $0640 - $4fff
	int wbyte, wbit;
	extern unsigned char *RAM;

	if (address < 0xf800)
	{
		missile_videoram[address] = data;
		missile_blit_w (address);
	}
	else
	{
		missile_videoram[address] = (missile_videoram[address] & 0x20) | data;
		missile_blit_w (address);
		wbyte = ((address - 0xf800) >> 2) & 0xfffe;
		wbit = (address - 0xf800) % 8;
		if(data & 0x20)
			RAM[0x401 + wbyte] |= (1 << wbit);
		else
			RAM[0x401 + wbyte] &= ((1 << wbit) ^ 0xff);
	}
}
 void missile_video2_w (int offset, int data)
{
	 $5000 - $ffff 
	offset += 0x5000;
	missile_video_w (offset, data);
}

void missile_video_mult_w (int address, int data)
{
	
		$1900 - $3fff

		2-bit color writes in 4-byte blocks.
		The 2 color bits are in x000x000.

		Note that the address range is smaller because 1 byte covers 4 screen pixels.
	

	data = (data & 0x80) + ((data & 8) << 3);
	address = address << 2;

	 If this is the bottom 8 lines of the screen, set the 3rd color bit 
	if (address >= 0xf800) data |= 0x20;

	missile_videoram[address]     = data;
	missile_videoram[address + 1] = data;
	missile_videoram[address + 2] = data;
	missile_videoram[address + 3] = data;

	missile_blit_w (address);
	missile_blit_w (address + 1);
	missile_blit_w (address + 2);
	missile_blit_w (address + 3);
}



 void missile_video_3rd_bit_w(int address, int data)
{
	int i;
	//extern unsigned char *RAM;


	address += 0x400;
	 This is needed to make the scrolling text work properly 
	RAM[address] = data;

	address = ((address - 0x401) << 2) + 0xf800;
	for (i=0; i<8; i++)
	{
		if (data & (1 << i))
			missile_videoram[address + i] |= 0x20;
		else
			missile_videoram[address + i] &= 0xc0;
		missile_blit_w (address + i);
	}
}



*/







static void ExecByCycleCount(void)
{
	
	/* UINT32 m6502clockticks; */
	
	double frametime= 16.666666;
	int toggle=0;
	int sec_time=0;
	static double fliptime=0;
	static double timepart=0;
	static double starttime=0;
	static double endtime=0;
	static double gametime=0;
	UINT32 m6502NmiTicks = 0;
	UINT32 dwElapsedTicks = 0;
	UINT32 dwResult;
	int loopcount=0;
	m6502zpreset();
	m6502zpexec(100);
	dwElapsedTicks = m6502zpGetElapsedTicks(0xff);
    
	
	m6502zpreset();

	while (!key[KEY_ESC])
	{ 
		
		fliptime = (double)starttime; 
		starttime=TimerGetTimeMS();
	    timepart=(double)starttime-(double)gametime;
		
		



         glClear(GL_COLOR_BUFFER_BIT );//| GL_DEPTH_BUFFER_BIT
		 glLoadIdentity();
		 //glViewport(0,0,256,256);
		 glDisable(GL_TEXTURE_2D);
		 glDisable(GL_POINT_SMOOTH);
		 glDisable(GL_BLEND);			
		 glClearColor(0.0f,0.0f,0.0f,0.0f);
		 glLineWidth(1);
	     glPointSize(1.0); //*scalef


	     dwResult=m6502zpexec(4250); //12500 //22992
				
		if (0x80000000 != dwResult)
		{
//			m6502zpGetContext(&psCpu1);
			allegro_message("Invalid instruction at %.2x\n", dwResult);
			exit(1);
		}
		m6502zpint(0);
		//m6502zpnmi();
		do_sound();
		
		msg_loop(); //Call main message loop
		
		
		    
			 
			
			   
				
                glEnable(GL_BLEND);
				//glEnable(GL_DEPTH_TEST);
				glBlendFunc(GL_SRC_ALPHA,GL_ONE);
				glBlendFuncSeparateEXT(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, game_tex[1]);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); //nearest!!
			        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				
				glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0,256, 256, 0); //RGBA8?
			
				glClear(GL_COLOR_BUFFER_BIT );
				
				glColor4f(1.0f,1.0f,1.0f,1.0f);
				/*
				//NOW DUMP IT AGAIN
			//	glBindTexture(GL_TEXTURE_2D,game_tex[1]); 
			//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); 
			 //   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTranslated(0,-32,0);	
				glBegin(GL_QUADS);
				glTexCoord2f(1,0);glVertex2i(0,512); //24
				glTexCoord2f(1,1);glVertex2i(0,0); //0,-24
				glTexCoord2f(0,1);glVertex2i(512,0); //-24
				glTexCoord2f(0,0);glVertex2i(512,512); //24 
				glEnd();
				
				glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0,512, 512, 0); //RGBA8?
				
				glClear(GL_COLOR_BUFFER_BIT );//Clear it AGAIN!!
				*/
				
				//glLoadIdentity();
				glViewport(0,0,SCREEN_W,SCREEN_H); //Reset for scaled? 1024/768 view
				
										// Enable Smooth Shading			
				
				//glEnable(GL_BLEND);
				////glBlendFunc(GL_SRC_ALPHA, GL_ONE); //Only Works for Space invaders
				//glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); 
				//if (background && config.artwork){glColor4f(1.0f,1.0f,1.0f,background_blend_val);} //THIS NEEDS ADJUSTMENT
				//else {glColor4f(1.0f,1.0f,1.0f,1.0f);}
		//!!!!!!!!!!		DrawBackground();
				glLoadIdentity();
				glEnable(GL_BLEND);
				
				
				glColor4f(1.0f,1.0f,1.0f,1.0f);
				//glBlendFunc(GL_SRC_ALPHA, GL_ONE); 
				//glBlendFunc (GL_ONE_MINUS_SRC_ALPHA, GL_ONE); 
 
				//glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); 
				glBlendFuncSeparateEXT(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
				
				glBindTexture(GL_TEXTURE_2D,game_tex[1]); 
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
			    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
               // glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
				//glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
				
				glTranslated(0,-23,0);							
				
				glBegin(GL_QUADS);
				glTexCoord2f(1,0);glVertex2i(30,768+24); //24
				glTexCoord2f(1,1);glVertex2i(30,-24); //0,-24
				glTexCoord2f(0,1);glVertex2i(994,-24); //-24
				glTexCoord2f(0,0);glVertex2i(994,768+24); //24 
				//glTexCoord2f(1,0);glVertex2i(994,+48); //24
				//glTexCoord2f(1,1);glVertex2i(994,768); //0,-24
				//glTexCoord2f(0,1);glVertex2i(30,768); //-24
				//glTexCoord2f(0,0);glVertex2i(30,+48); //24 
				glEnd();
				



			//	glPrint(100, 430, 255,255,255,255,1,0,0,"JOY RT/LFT 0: %d ",joya); 
			//	glPrint(100, 460, 255,255,255,255,1,0,0,"JOY UP/DN 0: %d ",joyb);
		  		//do_an_effect(0);	
			//	if (special) {DrawSpecial();}					
		    //    if (background && config.artwork){DrawBackground();}	
				//DrawBackground();
			
				video_loop(); //Common video stuff handling
			//	if (logic_counter>0){logic_counter=0;fps_count=frames;frames=0;}
				
		      gametime=TimerGetTimeMS();
				
		    while (((double)(gametime)-(double)starttime)- (double) (-timepart)  < (double)frametime)
		
		     {  
				 if ((double)(gametime-starttime) < 12 ){Sleep(1);}
		        gametime=TimerGetTimeMS();
		     };
			
           frames++;
		   gametime=TimerGetTimeMS();
		   
		   glDisable(GL_TEXTURE_2D);
		   glDisable(GL_BLEND);
		   glFlush();
		   allegro_gl_flip();
		   
	}
 
}

int init_missile(void)
{
   
   int EditDS = 0;
   int goodload=0;
//   int y;
   BITMAP *bmp;
 
  
   setup_game_config();
  
   set_aae_leds(0,0,0); 
   bmp= create_bitmap(256*scalef,256*scalef);  
   game_tex[1] = allegro_gl_make_texture(bmp);
   destroy_bitmap(bmp);
	
    gamefps=60;
           	
    init6502(MissileRead,MissileWrite,0x10000);
    goodload=load_roms(gamename[gamenum], mmissile);
    goodload=make_single_bitmap( &art_tex[0],"missile.png","missile.zip"); 
	
   
    chip=1;
	gain=6;
	BUFFER_SIZE=1750;
	Pokey_sound_init(1512000, 44100,chip ); // INIT POKEY
	soundbuffer =  malloc(BUFFER_SIZE);
	stream = play_audio_stream(BUFFER_SIZE, 8, FALSE, 44100, config.pokeyvol, 128);
    
    
    
    
    ExecByCycleCount();
    
  
    free_audio_stream_buffer(stream);
    set_aae_leds(0,0,0); 
    return_to_menu();
    exit(0);
}
