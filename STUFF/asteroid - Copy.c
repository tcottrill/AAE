/* Asteroid Emu */
#include "asteroid.h"
#include "aaemain.h"
#include "globals.h"
#include "samples.h"
#include "vector.h"
#include "glcode.h"
#include "dips.h"
#include "keysets.h"
#include "input.h"
#include "cpuintfaae.h"
#include "pokey.h"
#include "earom.h"

extern char *gamename[];
extern cyclesRemaining=0;
extern uint8 rng[MAXPOKEYS];
int SCRFLIP;
int dvggo=0;
UINT8 buffer[0x100];
//int astdelux_bank;
int check=0;
int psound=0;
int set=0;
int vec_done=0;

int check_hi(void)
{ 
    int num=0;
    if (gamenum==ASTEROID) {num=0x1d;} else num = 0x1c;

	if (memcmp(&gameImage[0][num],"\x00\x00",2) == 0 && memcmp(&gameImage[0][0x0050],"\x00\x00",2) == 0 &&
			memcmp(&gameImage[0][0x0000],"\x10",1) == 0)	{
	   		
        load_hi_aae(num, 0x35);
	
		return 1;
	}
	else return 0;	// we can't load the hi scores yet 
}
void  save_hi()
{
	int num;
	if (gamenum==ASTEROID) {num=0x1d;}else num = 0x1c;
    save_hi_aae(num, 0x35);
}
void set_ast_colors(void)
{
	int i=0;
	
	vec_colors[0].r=0;
	vec_colors[0].g=0;
	vec_colors[0].b=0;

	for (i=1;i<17;i++)
	 {
	  vec_colors[i].r=i*16;
      vec_colors[i].g=i*16;
      vec_colors[i].b=i*16;
	 }
	
}



////////////   SOUND ROUTINE FOR FIRST SET OF SOUNDS  /////////////////////////
WRITE_HANDLER(Sounds2)
{
    static int mfire = 0;
	static int vsfire = 0;
	static int saucer = 0;
	static int lastsaucer = 0;
	static int lastthrust = 0;
	static int saucertoggle=0;
   
	//int sound;
	int mfire2;
	int vsfire2;

gameImage[0][address] = data;

	switch (address)
	{
	case 0x3c00:  
		    if (data&0x80 && saucertoggle==0) 
				{
					if (gameImage[0][0x3c02]&0x80)
				    sample_start(4,9,1);
					else
					sample_start(4,8,1);
				    saucertoggle=1; break;
				}
			if (!(data&0x80) && saucertoggle==1)
			{	saucertoggle=0;
			    sample_stop(4);
			}       
		      break;	
	case 0x3c01:  
	              vsfire2 = data & 0x80;
				if (vsfire2!=vsfire)
					{
						sample_stop(3);
						if (vsfire2) sample_start(3,1,0);
					}
					vsfire=vsfire2;
					break;
			        	
	case 0x3c03:  if ((data&0x80) && !(lastthrust&0x80)) sample_start(5,2,1);
			      if (!(data&0x80) && (lastthrust&0x80)){sample_end(5);}
			      lastthrust=data;
			      break;
			           
	case 0x3c04:    mfire2 = data & 0x80;
					if (mfire2 != mfire)
					{
						 sample_stop(2);
						if (mfire2) sample_start(2,0,0);
					}
					mfire = mfire2;
					break;
	case 0x3c05:
		         if (data & 0x80)
				{   if (sample_playing(6)==0) {
				    sample_start(6,10,0);
				}   break;
					}
	default: break;
	}
}

////////////////// END SOUND ROUTINE FOR SECOND SET OF SOUNDS ////////////////////////

WRITE_HANDLER(Explosions)
{   
    static int explosion = -1;
	int explosion2;
	int sound = -1;

	if (data & 0x3c)
	{
		explosion2 = data >> 6;
		if (explosion2 != explosion)
		{
			
			switch (explosion2)
			{
				case 0:
				case 1:
				    sample_start(7,3,0);
					break;
				case 2:
					 sample_start(7,4,0);
					break;
				case 3:
					sample_start(7,5,0);
					break;
			   
				default: sample_start(7,4,0);
			}
		}
		explosion = explosion2;
	}
	else explosion = -1;
}

WRITE_HANDLER(Heartbeat)
{
 if (data & 0x10)
	{
		if (data & 0x0f){sample_start(8,7,0);}
		else {sample_start(8,6,0);}
	}      
}
///////////////////////////////////////////////////////////////////////////////
WRITE_HANDLER(DeluxeLedWrite)
{   //Thanks to whoever decoded this for MAME!!!
	static int led0=0;
	static int led1=0;
	     
	if (address&0xff) {led1=(data&0x80)?0:1;}
	else {led0=(data&0x80)?0:1;}
    
    set_aae_leds(led0,led1,0);
}
WRITE_HANDLER(DeluxeSwapRam)
{
	static int astdelux_bank = 0;
	int astdelux_newbank;
	unsigned char *RAM = gameImage[0];


	astdelux_newbank = (data >> 7) & 1;
	SCRFLIP=gameImage[0][0x1e];
	if (astdelux_bank != astdelux_newbank) {
		/* Perform bankswitching on page 2 and page 3 */
		int temp;
		int i;

		astdelux_bank = astdelux_newbank;
		for (i = 0; i < 0x100; i++) {
			temp = RAM[0x200 + i];
			RAM[0x200 + i] = RAM[0x300 + i];
			RAM[0x300 + i] = temp;
		}
	}
	/*int astdelux_newbank=0;
   
    astdelux_newbank = (data >> 7) & 1; 
	SCRFLIP=gameImage[0][0x1e]; 
	if (astdelux_bank != astdelux_newbank )
	
	{
		astdelux_bank = astdelux_newbank;
		memcpy(buffer, gameImage[0] + 0x200, 0x100);
		memcpy(gameImage[0] + 0x200, gameImage[0] + 0x300, 0x100);
		memcpy(gameImage[0] + 0x300, buffer, 0x100);
	}
  */
}
////////////  CALL ASTEROIDS SWAPRAM  ////////////////////////////////////////////
WRITE_HANDLER(AsteroidsSwapRam)
{
	static int asteroid_bank = 0;
	int asteroid_newbank;
	asteroid_newbank = (data >> 2) & 1;
	SCRFLIP=gameImage[0][0x18];
	if (asteroid_bank != asteroid_newbank) {
		/* Perform bankswitching on page 2 and page 3 */
		asteroid_bank = asteroid_newbank;
		memcpy(buffer, gameImage[0] + 0x200, 0x100);
		memcpy(gameImage[0] + 0x200, gameImage[0] + 0x300, 0x100);
    	memcpy(gameImage[0] + 0x300, buffer, 0x100);
	}
	set_aae_leds ((~data & 0x02), (~data & 0x01),0 );
}
/////////// END ASTEROIDS SWAPRAM ///////////////////////////////////////////////////

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
	{ sample_adjust(4, PLAYMODE_PLAY); }
	//{sample_end(0);}
	if ((data&0x80) && !(lastthrust&0x80)){sample_start(4,0,1);}
	lastthrust=data;
}

WRITE_HANDLER(DxExplosions)
{
  	if (data == 0x3d)  {sample_start(1,1,0);}
	if (data == 0xfd)  {sample_start(2,2,0);}
	if (data == 0xbd)  {sample_start(3,3,0);}
}
static void dvg_vector_timer (int scale)
{total_length += scale;}
/////////////////////////////VECTOR GENERATOR//////////////////////////////////
void dvg_generate_vector_list(void)
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
        float deltax, deltay; //float
        float currentx, currenty = 0; //float
		int div =0;
        int bright=0;
		
	    currentx= 0;
	    currenty=0;
		while (!done)
	{
	   firstwd = memrdwd(pc);opcode = firstwd & 0xf000;pc++;pc++;
	   

	   switch (opcode)
		{
    	  case 0xf000: 
		       			
			    // compute raw X and Y values //
			    z = (firstwd & 0xf0) >> 4;
				y = firstwd & 0x0300;
				x = (firstwd & 0x03) << 8;
				//Scale Y best we can			
			    x=x*4;
				y=y*4;
				//Check Sign Values and adjust as necessary
				if (firstwd & 0x0400) {y = -y;}
				if (firstwd & 0x04)   {x = -x;}
			    //Invert Drawing if in Cocktal mode and Player 2 selected
				if (!testsw){
				if (SCRFLIP  && config.cocktail )
				 	{x=-x;y=-y;}
					 }	
				
			    temp = 2 + ((firstwd >> 2) & 0x02) + ((firstwd >>11) & 0x01);
	  		    temp = ((scale + temp) & 0x0f);
			    if (temp > 9) temp = -1;
				dvg_vector_timer(temp);
				
			    deltax = x >> (9-temp);
			    deltay = y >> (9-temp);
			    deltax=deltax*.25;
			    deltay=deltay*.25;
			   								
				if ((currentx==(currentx)+deltax) && (currenty==(currenty)-deltay))
				  { if (z ==7){cache_txt(currentx,currenty,config.explode_point_size);}else {cache_point(currentx, currenty ,z,config.gain,0,1.0);}}
				
                else
			    	{
					cache_line(currentx,currenty,currentx+deltax,currenty-deltay, z,config.gain,0);
					cache_point(currentx,currenty,z,config.gain,0,0);
					cache_point(currentx+deltax,currenty-deltay,z,config.gain,0,0);
					}

			 		        
			    currentx += deltax;
				currenty -= deltay;
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
				secondwd =  memrdwd(pc);pc++;pc++;
			    // compute raw X and Y values and intensity //
				z = secondwd >> 12;
				y = firstwd & 0x03ff;
				x = secondwd & 0x03ff;
			    //Scale Y best we can			
				x=x*4;y=y*4;
				
				//Check Sign Values and adjust as necessary
				if (firstwd & 0x0400)
					{y = -y;}
				if (secondwd & 0x400)
					{x=-x;}
				//Invert Drawing if in Cocktal mode and Player 2 selected
				if (!testsw){	
				if (SCRFLIP  && config.cocktail )
				 	{x=-x;y=-y;}}
				// Do overall scaling
				temp = scale + (opcode >> 12);temp = temp & 0x0f;
				if (temp > 9){temp = -1;}
				dvg_vector_timer(temp);
						   
			    deltax = x >> (9-temp);
			    deltay = y >> (9-temp);	
						  
				deltax=deltax*.25;
				deltay=deltay*.25;
             
			 	 if ((currentx==(currentx)+deltax) && (currenty==(currenty)-deltay))
				   { if (z>14) cache_txt(currentx,currenty,config.fire_point_size); else cache_point(currentx, currenty ,z,0,0,1.0);}
				   
						cache_line(currentx, currenty, currentx+deltax, currenty-deltay, z,config.gain,0);
						cache_point(currentx,currenty,z,config.gain,0,0);
						cache_point(currentx+deltax,currenty-deltay,z,config.gain,0,0);
				 
				currentx += deltax;currenty -= deltay;	
				break;
			
			case 0xa000:
			     
				secondwd =  memrdwd(pc);
			    pc++;
		        pc++;
				x = twos_comp_val (secondwd, 12);
				y = twos_comp_val (firstwd, 12);
               	//Invert the screen drawing if cocktail and Player 2 selected
				if (!testsw){
				if (SCRFLIP && config.cocktail)
				  {x=1024-x; y=1024-y;}}
				//Do overall draw scaling
				scale = (secondwd >> 12) & 0x0f;
			  	currenty = 1130-y; //y-100; 1130-y
				currentx = x;
				break;

			case 0xb000: done = 1;break;
           
			case 0xc000: a = 0x4000 + ((firstwd & 0x0fff) << 1) ;
				         stack [sp] = pc;
				         if (sp == 4){done = 1;sp = 0;}else {sp=sp+1;pc = a;}
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

//////////////// VECTOR GENERATOR TRIGGER ///////////////////////////////////////

WRITE_HANDLER (BWVectorGeneratorInternal)
{   

	if (vec_done==1) return;
	dvggo=1;vec_done=1;dvg_generate_vector_list();write_to_log("DVG GO CALLED THIS FRAME");set=1;
	if (total_length==0)  {dvggo=0;vec_done=0;write_to_log("DVG GO SCRATCHED ---------- THIS FRAME");set=0;}
	
	
}
///////////// END VECTOR GENERATOR TRIGGER  //////////////////////////////////////
/////////////////////END VECTOR GENERATOR///////////////////////////////////////

/////////////////////READ KEYS FROM PIA 1 //////////////////////////////////////
READ_HANDLER(AstPIA1ROCKRead)
{
	int res;
	int bitmask;

	res=0;
	bitmask = (1 << address & 0xf);

  switch (address) 
	{
	case 0x2000: return 0;break;	
	case 0x2001: return 0;break;	 
	case 0x2002: if (dvggo == 0){return 0x80;} else {return 0;}break;
    case 0x2003: if (key[config.kp1but3] || my_j[config.j1but3])return 0x80;else return 0; break; /*Shield */
    case 0x2004: if (key[config.kp1but1] || my_j[config.j1but1])return 0x80;else return 0; break;/* Fire */
    case 0x2005: return 0;break;
    case 0x2006: return 0;break;
    case 0x2007: if (testsw)return 0x80;break;/* Self Test */
    }
   return 0;
}

READ_HANDLER(AstPIA1Read)
{
    int ticks;
	static int lastret=1;
	int val=0;
    ticks=m6502zpGetElapsedTicks(0);
	if (address==0x2001) {if (ticks  & 0x100) val=0x80; }
	if (address==0x2002) {if (dvggo){if (ticks >(13 * total_length)) val = 0x80;vec_done=0;dvggo=0;total_length=0;write_to_log("DVG Release, ticks is %d Val is %d",ticks,(13 * total_length));} }
	if (address==0x2003) {if (key[config.kp1but3]) val = 0x80;}
	if (address==0x2004) {if (key[config.kp1but1]) val = 0x80;}
	if (address==0x2007) {if (testsw) val = 0x80;}
	
	// case 0x2001: if (ticks  & 0x100) return 0x80; else return 0; break;	 
		// if (lastret==1){lastret=0;return 0x80;}
	     //else {lastret=1;return 0;} break;
	// case 0x2002: if (dvggo == 0){return 0;}
				//  if (dvggo == 1){if (ticks >(16*total_length)) {return 0x80;}else {return 0;}} break;
	// case 0x2003: if (key[config.kp1but3] || my_j[config.j1but3])return 0x80;else return 0;break; //Shield 
   //  case 0x2004: if (key[config.kp1but1] || my_j[config.j1but1])return 0x80;else return 0;  break; // Fire 
   //  case 0x2007: if (testsw)return 0x80; else return 0;break; // Self Test 
    // default: return 0; 
	
  return val;
  }

READ_HANDLER(AstPIA2Read) 
{
	switch(address)
	{      
      case 0x2400: if (key[config.kcoin1] || my_j[config.j1but8])  return 0x80;break;/* Coin in */
	  case 0x2403: if (key[config.kstart1] || my_j[config.j1but7]) return 0x80;break;/* 1 Player start */
	  case 0x2404: if (key[config.kstart2] || my_j[config.j1but6]) return 0x80;break;/* 2 Player start */
      case 0x2405: if (key[config.kp1but2] || my_j[config.j1but2]) return 0x80;break;/* Thrust */
      case 0x2406: if (key[config.kp1right]|| my_j[config.j1right])return 0x80;break; /* Rotate right */
      case 0x2407: if (key[config.kp1left]|| my_j[config.j1left])  return 0x80;break;/* Rotate left */
     }
    return 0x7f;
}
//////////////////////////////////////////////////////////////////////////
////////////////////////////LOAD HISCORE//////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////  MAIN LOOP /////////////////////////////////////
void run_asteroids()
{
	double runtime=0;
	double fintime=0;
	int x;
	int ticks;
	int cycles=1512000/60/4;
		
	UINT32 dwResult = 0;	
	   
	if (!check){check=check_hi();}
	if (KeyCheck(config.ktest))      {testsw^=1;m6502zpreset();}//dvggo=0;}
	if (KeyCheck(config.kreset))     {m6502zpreset();dvggo=0;}
    ticks=m6502zpGetElapsedTicks(0xff);
	//dvggo=0;
	//total_length=0;
    write_to_log("------------------START FRAME ---------------------");
		for (x=0; x<4; x++)
		{
       
         //if (dvggo ) {dvg_generate_vector_list();}
         //gameImage[0][0x3400]=0;
		 
		 dwResult = m6502zpexec(cycles); //6250
		 
		 if (0x80000000 != dwResult)
		   {
			 m6502zpGetContext(psCpu1);
			 allegro_message("Invalid instruction at %.2x\n", psCpu1->m6502pc);
			 exit(1);
		   }
		 write_to_log("RUN");
		 if (!testsw ){m6502zpnmi();}
		 if (psound && !paused) {do_sound();}
		
		}
		
		write_to_log("total length is %d",total_length);
		if (set==0){write_to_log("-------------------------------------NO DVG CALL--------------------");}
		set=0;
		
		
		
}
/////////////////END MAIN LOOP/////////////////////////////////////////////
/*
static void check_joystick(void)
{ int x=0;
  int y=0;
   poll_joystick();

for (x=0; x < 4; x++){
	 
	  for (y=0; y < 4; y++){
     
	 if (joy[x].stick[y].axis[0].d1) {
	 glPrint(200, 700, 255,255,255,255,1,0,0," Joystick %d Stick %d axis 0 d1",x,y);
	 }
	 if (joy[x].stick[y].axis[0].d2) {
	 glPrint(200, 670, 255,255,255,255,1,0,0," Joystick %d Stick %d axis 0 d2",x,y);
	 }
	 if (joy[x].stick[y].axis[1].d1) {
	 glPrint(200, 640, 255,255,255,255,1,0,0," Joystick %d Stick %d axis 1 d1",x,y);
	 }
	 if (joy[x].stick[y].axis[1].d2) {
	 glPrint(200, 610, 255,255,255,255,1,0,0," Joystick %d Stick %d axis 1 d2",x,y);
	 }
	  }
     
	 if (joy[x].button[0].b) { glPrint(200, 580, 255,255,255,255,1,0,0," Joystick %d button 0",x);}
	 if (joy[x].button[1].b) { glPrint(200, 550, 255,255,255,255,1,0,0," Joystick %d button 1",x);}
	 if (joy[x].button[2].b) { glPrint(200, 520, 255,255,255,255,1,0,0," Joystick %d button 2",x);}
	 if (joy[x].button[3].b) { glPrint(200, 490, 255,255,255,255,1,0,0," Joystick %d button 3",x);}
	 if (joy[x].button[4].b) { glPrint(200, 460, 255,255,255,255,1,0,0," Joystick %d button 4",x);}
	 if (joy[x].button[5].b) { glPrint(200, 430, 255,255,255,255,1,0,0," Joystick %d button 5",x);}
	 if (joy[x].button[6].b) { glPrint(200, 400, 255,255,255,255,1,0,0," Joystick %d button 6",x);}
	 if (joy[x].button[7].b) { glPrint(200, 370, 255,255,255,255,1,0,0," Joystick %d button 7",x);}
	 if (joy[x].button[8].b) { glPrint(200, 340, 255,255,255,255,1,0,0," Joystick %d button 8",x);}
	 if (joy[x].button[9].b) { glPrint(200, 310, 255,255,255,255,1,0,0," Joystick %d button 9",x);}
	 if (joy[x].button[10].b) { glPrint(200, 280, 255,255,255,255,1,0,0," Joystick %d button 10",x);}
	
      }
}
*/


MEM_WRITE(AsteroidDeluxeWrite)
    MEM_ADDR(0x2400, 0x2407, NoWrite)
	MEM_ADDR(0x2000, 0x2007, NoWrite)
	MEM_ADDR(0x2c00, 0x2c09, Pokey0Write)
	MEM_ADDR(0x3000, 0x3000, BWVectorGeneratorInternal)
	MEM_ADDR(0x3c03, 0x3c03, Thrust)
	MEM_ADDR(0x3c04, 0x3c04, DeluxeSwapRam)
	MEM_ADDR(0x3600, 0x3600, DxExplosions)
	MEM_ADDR(0x3200, 0x323f, EaromWrite)
	MEM_ADDR(0x3a00, 0x3a00, EaromCtrl)
	MEM_ADDR(0x3c00, 0x3c01, DeluxeLedWrite)
	MEM_ADDR(0x4800, 0x7fff, NoWrite)
MEM_END

MEM_READ(AsteroidDeluxeRead)
	MEM_ADDR(0x2c40, 0x2c7f, EaromRead)
	MEM_ADDR(0x2c0a, 0x2c0a, RandRead)
	MEM_ADDR(0x2400, 0x2407, AstPIA2Read)
	MEM_ADDR(0x2000, 0x2007, AstPIA1Read)
MEM_END

MEM_READ(AsteroidRead)
	MEM_ADDR(0x2400, 0x2407, AstPIA2Read)
	MEM_ADDR(0x2000, 0x2007, AstPIA1Read)
MEM_END

MEM_READ(AsterRockRead)
	MEM_ADDR( 0x2400, 0x2407, AstPIA2Read)
	MEM_ADDR(0x2000, 0x2007, AstPIA1ROCKRead)
MEM_END

MEM_WRITE(AsteroidWrite)
	MEM_ADDR(0x3000,0x3000,BWVectorGeneratorInternal)
	MEM_ADDR(0x3200, 0x3200, AsteroidsSwapRam)
	MEM_ADDR(0x3600, 0x3600, Explosions)
	MEM_ADDR(0x3a00, 0x3a00, Heartbeat)
	MEM_ADDR(0x3c00, 0x3c05, Sounds2)
	MEM_ADDR(0x6800, 0x7fff, NoWrite) //Program Rom
	MEM_ADDR(0x5000, 0x57ff, NoWrite) //Vector Rom
MEM_END
/////////////////// MAIN() for program ///////////////////////////////////////////////////
void end_asteroids()
{
 save_hi();write_to_log("Save hi called!!");
}
void end_astdelux()
{
    cache_clear();
	SaveEarom();
  	free_audio_stream_buffer(stream);
	stop_audio_stream(stream);
	free(soundbuffer);
}
int init_asteroid(void)
{
   
	if (gamenum==ASTEROCK)
	{init6502(AsterRockRead, AsteroidWrite,0);}
	else
	{init6502(AsteroidRead, AsteroidWrite,0);}
	 
	  
   	gameImage[0][0x2002] =  0x0;
	gameImage[0][0x2007] =  0x0;
    gameImage[0][0x2801] =  0xff; // Just to clear 0 value
	 
    cache_clear();
	set_ast_colors();
	//setup_ambient(VECTOR);
	 
    
	if (config.bezel) {config.cocktail=0;}//Just to check for stupidity
	check=0;
	check=0;
    psound=0;
	dvggo=0;
	m6502zpreset();
	m6502zpexec(100);
	m6502zpreset();
    	
//	while (check_hi()==0)
	//{
	//m6502zpexec(6250);
	//m6502zpnmi();
	//}
	//run_asteroids();
    
   // save_hi();
    //save_dips();
  
    write_to_log("End init");
	return 0;   
}

int init_astdelux(void)
{
    if (config.bezel) {config.cocktail=0;}//Just to check for stupidity
	//astdelux_bank = 0x2;
	init6502(AsteroidDeluxeRead,AsteroidDeluxeWrite,0);
		
	gameImage[0][0x2000] =  0x7f;
	cache_clear();
	set_ast_colors();
	LoadEarom();
	m6502zpreset();
	m6502zpexec(100);
	m6502zpreset();
	check=1; //Disable Asteroids HiScore Loading
	psound=1; //Enable Pokey Sound Processing
	dvggo=0;
	/////////////Sound INIT///////////////////////////
	BUFFER_SIZE=44100/60/4;chip=1;gain=6; //425
	Pokey_sound_init(1512000, 44100,chip,0 ); // INIT POKEY
	soundbuffer =  malloc(BUFFER_SIZE);
	memset(soundbuffer, 0, BUFFER_SIZE);
	stream = play_audio_stream(BUFFER_SIZE, 8, FALSE, 44100, config.pokeyvol, 128);
	
	return 0;   
}