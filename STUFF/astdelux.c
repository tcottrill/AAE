/* Asteroid Deluxe Emu */
//#define ALLEGRO_STATICLINK =1

#include "astdelux.h"
#include "aaemain.h"
#include "globals.h"
#include "samples.h"
#include "vector.h"
#include "glcode.h"
#include "dips.h"
#include "keysets.h"
#include "input.h"
#include "cpuintf.h"
#include "pokey.h"
#include "earom.h"


extern char *gamename[];
extern cyclesRemaining;
char *deluxesamples[]={"astdelux.zip","thrust.wav","explode1.wav","explode2.wav","explode3.wav","NULL"};
extern uint8 rng[MAXPOKEYS];
int advggo;
int SCRFLIP;
int astdelux_bank;
UINT8 buffer[0x100];

void amyreset()
{
    gameImage[0x2002] =  0x0;
	gameImage[0x2007] =  0x0;
    gameImage[0x2802] =  0x0f;
	memset(gameImage,0,0x3ff);
	memset(gameImage+0x4000, 0, 0x7ff);
	psCpu1->irqPending=0;
	psCpu1->m6502pc=0;
	psCpu1->m6502af=0;
	psCpu1->m6502x=0;
    psCpu1->m6502y=0;
	advggo=0;
	astdelux_bank = 0x2;
	m6502zpreset();
}

void set_astdlx_colors()
{   
	
	int i,c=0;
	float *cmap;

	float colormapbw[]={0,0,0,.1,.1,.1,.2,.2,.2,.3,.3,.3,.4,.4,.4,.5,.5,.5,
       .55,.55,.55,.65,.65,.65,.65,.65,.65,.7,.7,.7,.75,.75,.75,.8,.8,
       .8,.85,.85,.85,.95,.95,.95,1,1,1,1,1,1};
	cmap=colormapbw;
	/*
	if (config.hack){
	float colormaphck[]={0,0,0,.1,.1,.1,.2,.2,.2,
		                 0,.55,.55,0,.6,.6,0,.7,.7,
                      0,.75,.75,.43,.80,.80,.9,.10,.10,
			           .45,.90,.90,.65,1.0,1.0,.7,1,1,
					   .7,1,1,1,1,0,.7,1,1
					   ,.7,1,1};
	cmap=colormaphck;
	}
	*/	
	for (i=0;i<16;i++)
	{vec_colors[i].r=cmap[c];vec_colors[i].g=cmap[c+1];vec_colors[i].b=cmap[c+2];c+=3;}
}
static void dxdvg_generate_vector_list(void)
{
		 		
	    int  pc = 0x4000;
		int sp = 0;
		int stack [4];
		int scale = 0;
		int done = 0;
        UINT16 firstwd,secondwd = 0;
	    UINT16 opcode;
        int  x, y;
        int temp;
		int z;
		int a;
        float deltax, deltay;
        float currentx, currenty = 0;
	
	    currentx= 0;
	    currenty=0;
		while (!done)
	{
	    
	  	firstwd = memrdwd(pc);
		opcode = firstwd & 0xf000;
	  	pc++;pc++; 
	    
		switch (opcode)
		{
    		case 0xf000: 
		       			
			    // compute raw X and Y values //
				z = (firstwd & 0xf0) >> 4;
				y = firstwd & 0x0300;
				x = (firstwd & 0x03) << 8;
				//Scale Y best we can			
			    x=x*32;
				y=y*32;
				//Check Sign Values and adjust as necessary
				if (firstwd & 0x0400)
					{y = -y;}
				if (firstwd & 0x04)
					{x = -x;}
			    //Invert Drawing if in Cocktal mode and Player 2 selected
				if (!testsw){if (SCRFLIP && config.cocktail){x=-x;y=-y;}}	
				
				temp = 2 + ((firstwd >> 2) & 0x02) + ((firstwd >>11) & 0x01);
	  			temp = ((scale + temp) & 0x0f);
				if (temp > 9) {temp = -1;}
				
			   deltax = x >> (9-temp);
			   deltay = y >> (9-temp);	
			
			   deltax=deltax/32.0;
			   deltay=deltay/32.0;		
			   
			   if (z >3 )
				{
				  	glColor3f(vec_colors[z].r,vec_colors[z].g,vec_colors[z].b);	
								
					if ((currentx==(currentx)+deltax) && (currenty==(currenty)-deltay))
				    {  if (config.overlay) {cache_point(currentx, currenty ,z,0,0,1);cache_txt(currentx,currenty,30);}
					   else {cache_txt(currentx,currenty,config.fire_point_size);}
					}
				    else
					{
					  cache_line(currentx, currenty, currentx+deltax, currenty-deltay, z,0,0);
					  cache_point(currentx, currenty ,z,0,0,0);
					  cache_point(currentx+deltax, currenty-deltay ,z,0,0,0);
					}
				}
			 		        
			    currentx += deltax;
				currenty -= deltay;
				deltax,deltay=0;
				break;
				
			case 0:done=1;break;
			case 0x1000:
			case 0x2000:
			case 0x3000:
			case 0x4000:
			case 0x5000:
			case 0x6000:
			case 0x7000:
			case 0x8000:
			case 0x9000:
	  		
			     // Get Second Word
				 secondwd =  memrdwd(pc);
			     pc++;
		         pc++;
			
				 // compute raw X and Y values and intensity //
				z = secondwd >> 12;
				y = firstwd & 0x03ff;
				x = secondwd & 0x03ff;
			    //Scale Y best we can			
				x=x*32;
				y=y*32;
				
				//Check Sign Values and adjust as necessary
				if (firstwd & 0x0400)
					{y = -y;}
				if (secondwd & 0x400)
					{x=-x;}
				//Invert Drawing if in Cocktal mode and Player 2 selected
				if (!testsw){	
							 if (SCRFLIP && config.cocktail)
				 				{x=-x;y=-y;}
							}
							// Do overall scaling
			
				temp = scale + (opcode >> 12);
				temp = temp & 0x0f;
	  		
				if (temp > 9)
				{temp = -1;}
	  		   			   
			   deltax = x >> (9-temp);
			   deltay = y >> (9-temp);	
			   deltax=deltax/32.0;
			   deltay=deltay/32.0;	
			  
			   if (z >3)
				{
				  glColor3f(vec_colors[z].r,vec_colors[z].g,vec_colors[z].b);	
								
					if ((currentx==(currentx)+deltax) && (currenty==(currenty)-deltay))
				  {  
					  
					  if (z==4) cache_point(currentx, currenty ,z,0,0,.5);
					  if (z!=4){if (config.overlay) {cache_txt(currentx,currenty,30); cache_point(currentx, currenty ,z,0,0,1);}
					  else {cache_txt(currentx,currenty,config.fire_point_size);} 
					  }
					}
										
					cache_line(currentx, currenty, currentx+deltax, currenty-deltay, z,0,0);
					cache_point(currentx, currenty ,z,0,0,0);
					cache_point(currentx+deltax, currenty-deltay ,z,0,0,0);
				}
		    	 
				currentx += deltax;
				currenty -= deltay;
                deltax,deltay=0;
			    break;

			case 0xa000:
			     
				secondwd =  memrdwd(pc);
			    pc++;
		        pc++;
				x = twos_comp_val (secondwd, 12);
				y = twos_comp_val (firstwd, 12);
               	 //Scale Y drawing as best we can
				//Invert the screen drawing if cocktail and Player 2 selected
				if (!testsw){ 
					         if (SCRFLIP && config.cocktail)
								{ x=1024-x;y=1024-y;}
				            }
				scale = (secondwd >> 12) & 0x0f;
			    currenty = 1130-y;
				currentx = x ;
			    
				break;

			case 0xb000:
                done = 1; 
				break;

			case 0xc000:
				
				 a = 0x4000 + ((firstwd & 0x0fff) << 1) ;
			     stack [sp] = pc;
				if (sp == 4)
	    			{done = 1;sp = 0;}
				else
			    sp=sp+1;
				pc = a;
				
				break;

			case 0xd000:
				 sp=sp-1;
			     pc = stack [sp] ;
				 break;
				
			case 0xe000:
				 a = 0x4000 + ((firstwd & 0x0fff)  << 1);
				 pc = a;
				 break;
         }
	}
   
	   
}
WRITE_HANDLER(DeluxeLedWrite)
{   //Thanks to whoever decoded this for MAME!!!
	static int led0=0;
	static int led1=0;
	     
	if (address&0xff) {led1=(data&0x80)?0:1;}
	else {led0=(data&0x80)?0:1;}
    
    set_aae_leds(led0,led1,0);
}
	
WRITE_HANDLER(DxVectorGeneratorInternal)
{advggo=1;}

WRITE_HANDLER(DeluxeSwapRam)
{
	int astdelux_newbank=0;
   
    astdelux_newbank = (data >> 7) & 1; 
	SCRFLIP=gameImage[0x1e]; 
	if (astdelux_bank != astdelux_newbank )
	
	{
		astdelux_bank = astdelux_newbank;
		memcpy(buffer, gameImage + 0x200, 0x100);
		memcpy(gameImage + 0x200, gameImage + 0x300, 0x100);
		memcpy(gameImage + 0x300, buffer, 0x100);
	}

}
READ_HANDLER(RandRead)
{  
	static uint8 pokey_random[4];     
 
	if (rng[chip]) { pokey_random[chip] = (pokey_random[chip]>>4) | (rand()&0xf0); }
    return pokey_random[chip];
}

WRITE_HANDLER(Thrust)
{
	static int lastthrust = 0;
	if (!(data&0x80) && (lastthrust&0x80)) 
	{ sample_adjust(0, PLAYMODE_PLAY); }
	//{sample_end(0);}
	if ((data&0x80) && !(lastthrust&0x80)){sample_start(0,0,1);}
	lastthrust=data;
}

WRITE_HANDLER(DxExplosions)
{
  	if (data == 0x3d)  {sample_start(1,1,0);}
	if (data == 0xfd)  {sample_start(2,2,0);}
	if (data == 0xbd)  {sample_start(3,3,0);}
}
///////////////////////////////////////////////////////////////////////////////////////////
READ_HANDLER(DxAstPIA1Read)
{
	static int lastret=1;

	if (KeyCheck(config.ktest))       {testsw^=1;amyreset();}
	if (KeyCheck(config.kreset))      {amyreset();}//m6502zpreset();}

	switch (address) 
	{
	 case 0x2001: if (lastret==1){lastret=0;return 0x80;}else{lastret=1;return 0x7f;}break;
	 case 0x2002: if (advggo == 0){return 0x7f;} else {return 0x80;}break;
	 case 0x2003: if (key[config.kp1but3])return 0x80;break; /*Shield */
     case 0x2004: if (key[config.kp1but1])return 0x80;break; /* Fire */
     case 0x2007: if (testsw) return 0x80;break; /* Self Test */
    }

   return 0;
}
READ_HANDLER(DxAstPIA2Read)
{
	switch(address)
	{      
     case 0x2400: if (key[config.kcoin1])  return 0x80;break; // Coin in
     case 0x2403: if (key[config.kstart1]) return 0x80;break;  // 1 Player start 
     case 0x2404: if (key[config.kstart2]) return 0x80;break; // 2 Player start 
	 case 0x2405: if (key[config.kp1but2]) return 0x80;break; //thrust
     case 0x2406: if (key[config.kp1right])return 0x80;break; // Rotate right 
     case 0x2407: if (key[config.kp1left]) return 0x80;break; // Rotate left 
	}
    return 0;
}



void ExecAstDelux()
{
	UINT32 dwResult = 0;
	int can=0;
	int sec_time=0;
	int x=0;
	double starttime=0;
	double gametime=0;
	double millsec = (double) 1000 / gamefps;
	UINT32 m6502NmiTicks = 0;
	UINT32 dwElapsedTicks = 0;
	m6502zpreset();
	m6502zpexec(100);
	dwElapsedTicks = m6502zpGetElapsedTicks(0xff);
 	
	m6502zpreset();
    while (!done)
	{	
		write_to_log("A done is now %d",done);
		for (x=0; x<4; x++)
		{
				dwResult = m6502zpexec(6250); //6250
				if (0x80000000 != dwResult)
				{
				m6502zpGetContext(psCpu1);
				allegro_message("Invalid instruction at %.2x\n", psCpu1->m6502pc);
				exit(1);
				}
			if (!testsw ){m6502zpnmi();
			write_to_log("B done is now %d",done);
			do_sound();
		 }
		
		}
        write_to_log("C done is now %d",done);	
		 set_render();
		 dxdvg_generate_vector_list(); 			
		 cache_end();
         draw_points();
		 draw_lines();
		 glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);		
		 draw_texs();
		 complete_render();
		write_to_log("D done is now %d",done);
        //Normal rendering.       
        
        if (config.bezel) {final_render(0,820,0,670,100,200);}
	    else{final_render(0,1024,0,965,0,0);}
       
		if (config.overlay) {blit_fsa_texub(&art_tex[3],5,255,255,255,255,0);}
		if (config.artwork) {blit_fsa_texub(&art_tex[0],1, 230,230,230,200,0);}
		if (config.bezel) {blit_fsa_texub(&art_tex[1],0, 255,255,255,255,0);blit_fsa_texub(&art_tex[2],1, 255,255,255,255,0);}//blit_fs_tex(&art_tex[1],1,0);}//blit_fs_tex(&art_tex[2],0,0);}

		advggo=0;
		msg_loop(); //Call main message loop 
	    write_to_log("E done is now %d",done);
		video_loop(); //Common video stuff handling
		write_to_log("F done is now %d",done);      
		while (((double)(gametime)-(double)starttime)  < (double)millsec)
		{if ((double)((double)gametime- (double) starttime) < (double) (millsec-4) )
		{Sleep(1);}
		(double) gametime=TimerGetTimeMS();}
         fps_count = 1000 / ((double)(gametime)-(double)starttime);
		(double) starttime=TimerGetTimeMS();
		allegro_gl_flip();
	  }
}



MEM_WRITE(AsteroidDeluxeWrite)
	MEM_ADDR(0x2c00, 0x2c0f, Pokey0Write)
	MEM_ADDR(0x3000, 0x3000, DxVectorGeneratorInternal)
	MEM_ADDR(0x3c03, 0x3c03, Thrust)
	MEM_ADDR(0x3c04, 0x3c04, DeluxeSwapRam)
	MEM_ADDR(0x3600, 0x3600, DxExplosions)
	MEM_ADDR(0x3200, 0x323f, EaromWrite)
	MEM_ADDR(0x3a00, 0x3a00, EaromCtrl)
	MEM_ADDR(0x3c00, 0x3c01, DeluxeLedWrite)
	MEM_ADDR(0x4800, 0xffff, NoWrite)
MEM_END

MEM_READ(AsteroidDeluxeRead)
	MEM_ADDR(0x2c40, 0x2c7f, EaromRead)
	MEM_ADDR(0x2c0a, 0x2c0a, RandRead)
	MEM_ADDR(0x2400, 0x2407, DxAstPIA2Read)
	MEM_ADDR(0x2000, 0x2007, DxAstPIA1Read)
MEM_END

int init_astdelux(void)
{
    int goodloadr,goodloada;
    gamefps=60;
	//SET VARS
	SCRFLIP=0;
	advggo=0;
	astdelux_bank = 0x2;
	setup_game_config();
	 init_gl();
	init6502(AsteroidDeluxeRead,AsteroidDeluxeWrite,0x10000);
	
	goodloada=make_single_bitmap( &game_tex[0],"shote.png","asteroid.zip");
    if (goodloada==0){write_to_log("A REQUIRED texture was not found!");have_error=15;config.artwork=0;}
	
	if (gamenum==6) {
	                  goodloadr=load_roms(gamename[gamenum], astdelu1);
	                  if (config.artwork) {goodloada=make_single_bitmap( &art_tex[0],"astdelu1.png","astdelux.zip");}
	                }
   
	if (gamenum==7) {goodloadr=load_roms(gamename[gamenum], astdelux);
	                 if (config.artwork) {goodloada=make_single_bitmap( &art_tex[0],"astdelux.png","astdelux.zip");}
	                 }
    if (gamenum==59) {goodloadr=load_roms(gamename[gamenum], astdelu2);
	                  if (config.artwork) {goodloada=make_single_bitmap( &art_tex[0],"astdelux.png","astdelux.zip");}
	                  }

	if (config.overlay) {goodloada=make_single_bitmap( &art_tex[3],"astdelux_overlay.png","astdelux.zip");}
     if (config.bezel){
				                 goodloada=make_single_bitmap( &art_tex[1],"astdeluxmask.png","astdelux.zip");
			                     goodloada=make_single_bitmap( &art_tex[2],"astdelux_bezelF.png","astdelux.zip");
			                    }
	
	if (goodloadr==0) {write_to_log("Rom loading failure, exiting...");have_error=10; return 0;}
	if (goodloada==0) {write_to_log("A requested texture was not found!");have_error=15;config.artwork=0;}
	/////////////Sound INIT///////////////////////////
	BUFFER_SIZE=188;chip=1;gain=6; //425
	Pokey_sound_init(1512000, 44100,chip ); // INIT POKEY
	soundbuffer =  malloc(BUFFER_SIZE);
	stream = play_audio_stream(BUFFER_SIZE, 8, FALSE, 44100, config.pokeyvol, 128);
  
	load_samples(deluxesamples);
	       
 	gameImage[0x2002] =  0x0;
	gameImage[0x2007] =  0x0;
    gameImage[0x2802] =  0x0f;
	
	gameImage[0x2000] = (char) 0x7f;
	 cache_clear();
	
    dips = astdelux_dips;
	retrieve_dips();
    gamekeys = astdelux_keys; 
	retrieve_keys();	
	setup_ambient(VECTOR);
	set_astdlx_colors();
    LoadEarom();
	
	if (config.cocktail) {config.artwork=0;}
    write_to_log("Z done is now %d",done);
	ExecAstDelux();
    //EXIT
	cache_clear();
	SaveEarom();
	save_dips();	
   	free_audio_stream_buffer(stream);
	stop_audio_stream(stream);
	free(soundbuffer);
	return_to_menu();
	return 0;   
}
