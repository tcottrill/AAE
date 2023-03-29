/* Tempest Emu */

#include "tempest.h"
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

int difficulty=2;
int clearme;
int tavggo=0;
int turn=0x0f;
int turnamt=0;
int flipscreen=0;
int tticks=0;
int INMENU=0;
int temptest=0;

#define MAXSTACK 8
char *tbuffer;



static void tempest_vector_list (void)
{
   	int pc=0x2000;
	int sp=0;
	int stack [MAXSTACK];
    int flipword = 0;
	int scale=0;
	int statz   = 0;
	static int  sparkle =0;
	int xflip   = 0;
   	static int color;
	int currentx, currenty=0;
	int done    = 0;
	int firstwd, secondwd;
	int opcode;
	int x, y, z=0, b, l, d, a;
	int deltax, deltay=0;
    int red,green,blue;
   

	while (!done )
	{
	 	firstwd = memrdwd (pc);
		opcode = firstwd >> 13;
		pc++;pc++;
		secondwd = memrdwd (pc);
        if ((firstwd == 0) && (secondwd == 0))
		{;}//write_to_log("VGO with zeroed vector memory\n");
		 
		
     	if ((opcode == STAT) && ((firstwd & 0x1000) != 0))	opcode = SCAL;
		if (opcode == VCTR)	{ secondwd = memrdwd (pc);pc++;pc++;}
		
        switch (opcode)
		{
			case VCTR:x=twos_comp_val(secondwd, 13);y=twos_comp_val(firstwd, 13);z = (secondwd >> 12) & ~0x01;
			          goto DRAWCODE;break;    
			case SVEC:x=twos_comp_val(firstwd, 5)<<1;y=twos_comp_val(firstwd >> 8,5)<<1;z = ((firstwd >> 4) & 0x0e);
				

DRAWCODE:		
				
				if (z == 2){z = statz;}if (z) {z = (z << 4) | 0x1f;}
				 deltax = x * scale;deltay = y * scale;
				 total_length+=vector_timer(deltax, deltay); 
				 
			     if (flipscreen) {deltay = -deltay;deltax = -deltax;}
			     if (sparkle) {color = rand() & 0x07;}
			  	 		
				 if (vec_colors[color].r) red=z; else red=0;
				 if (vec_colors[color].g) green=z; else green=0;
				 if (vec_colors[color].b) blue=z; else blue=0;
				
                 if ((currentx==(currentx)+deltax) && (currenty==(currenty)-deltay))
				      {add_color_point((currentx>>VEC_SHIFT),currenty>>VEC_SHIFT, red,green,blue);
				       
				 }

	             add_color_line((currentx>>VEC_SHIFT), (currenty>>VEC_SHIFT), (currentx+deltax)>>VEC_SHIFT, (currenty-deltay)>>VEC_SHIFT, red,green,blue);
				 add_color_point((currentx>>VEC_SHIFT),currenty>>VEC_SHIFT,  red,green,blue);
				 add_color_point((currentx+deltax)>>VEC_SHIFT,(currenty-deltay)>>VEC_SHIFT,  red,green,blue);
						  
				 currentx += deltax;currenty -= deltay;
				 break;

			case STAT:
				color = (firstwd) & 0x000f;
				statz = (firstwd >> 4) & 0x000f;
				sparkle = !(firstwd & 0x0800);
				break;

			case SCAL:
				b = ((firstwd >> 8) & 0x07)+8;
				l = (~firstwd) & 0xff;
				scale = (l << VEC_SHIFT) >> b;		/* ASG 080497 */
				
				break;

			case CNTR:
				d = firstwd & 0xff;
				currentx = 512 << VEC_SHIFT;  
				currenty = 512 << VEC_SHIFT;  
              	break;

			case RTSL:

				if (sp == 0)
				{done = 1;sp = MAXSTACK - 1;}
				else
				sp--;
	            pc = stack [sp];
				break;

			case HALT: done = 1;break;

			case JMPL:
				a = 0x2000 + ((firstwd & 0x1fff) << 1) ;
				if (a == 0x2000) done = 1; //HALT
				else pc = a;
				break;

			case JSRL:
				a = 0x2000 + ((firstwd & 0x1fff) << 1) ;
				if (a == 0x2000) done = 1;
				else
				{
					stack [sp] = pc;
					if (sp == (MAXSTACK - 1))
					{
						write_to_log ("*** Vector generator stack overflow! ***");
						done = 1;
						sp = 0;
					}
					else sp++;pc = a;
				}
				break;

			default: allegro_message ("Error in AVG engine");
			}	
		}
}


void tempm_reset()
{
memcpy(gameImage[0],tbuffer,0x10000);  
m6502zpreset();
INMENU=1;
}

static void SwitchGame(void)
{  
	int a;
	int b;
	int oldgamenum=TEMPESTM;
	
	if (!INMENU){return;}

	INMENU=0;
	oldgamenum=gamenum;
	a =(gameImage[0][0x51])+1;
	switch(a)
	{
	case 1: b=0x10000;gamenum=ALIENST;break;
	case 2: b=0x20000;gamenum=VBREAK;break;
	case 3: b=0x30000;gamenum=VORTEX;break;
	case 4: b=0x40000;gamenum=TEMPTUBE;break;
	case 5: b=0x50000;gamenum=TEMPEST1;break;
	case 6: b=0x60000;gamenum=TEMPEST2;break;
	case 7: b=0x70000;gamenum=TEMPEST;break;
	default: write_to_log("Hey unhandled game number!");
	}
	//write_to_log("A value is - %d",a);
	setup_video_config();
	gamenum=oldgamenum; //Reset back
	memset(gameImage[0],0x10000,0);
	memcpy(gameImage[0],gameImage[0]+ b,0x10000);  
	m6502zpreset();
   
}	

READ_HANDLER (TempestDSW1)
{if (gamenum==25){return 0;}else {return z80dip1;}}

READ_HANDLER (TempestDSW2)
{if (gamenum==25){return 0;}else {return z80dip2;}}

READ_HANDLER (Pokey1Read)
{
int number;

	if (address==0x60ca)
	{	if ((gameImage[0][0x60cf] & 0x03) != 0x00)
				{
					number = (rand () >> 12) & 0xff;
					gameImage[0][address]=number;
					return number;
				}
	else return 0xff;
	}
 
 get_mouse_mickeys(&mickeyx, &mickeyy);
 position_mouse(SCREEN_W/2, SCREEN_H/2);     
   if (config.mouse1x_invert){mickeyx=-mickeyx;}
   if (mickeyx > 1 )         {turn=turn-turnamt;if (turn <0) turn=0x0f;}
   if (mickeyx < -1 )        {turn=turn+turnamt;if (turn > 0x0f) turn=0;}
   if (key[config.kp1right]) {turn=turn-1;if (turn <0) turn=0x0f;}
   if (key[config.kp1left])  {turn=turn+1;if (turn > 0x0f) turn=0; }
   if (config.cocktail)      {return turn | 0x10;} //Cocktail flip bit, inverted config.
   return turn;
}
READ_HANDLER (Pokey2Read)
{
	int number;
    int val=0;
   
	if (address==0x60da)
	{
	 if ((gameImage[0][0x60cf] & 0x03) != 0x00)
		{
			number = (rand () >> 12) & 0xff;
		    gameImage[0][address]=number;
			return number;
		}
	 else return 0xff;
	}
	if (key[config.kstart1])  { val = val | 0x20;} //START1
	if (key[config.kstart2])  { val = val | 0x40;}  //START2
	if (key[config.kp1but1]  || (mouse_b & 1)) { val = val | 0x10; if (gamenum==TEMPESTM){SwitchGame(); }}//FIRE
	if (key[config.kp1but2]  || (mouse_b & 2)) { val = val | 0x08;} //SUPERZAPPER
	if (gamenum==TEMPESTM){	if (key[config.kp1but3]) {tempm_reset();}}
    return val | difficulty;
}

WRITE_HANDLER(TempGo)
{   
	if (clearme==1) {return;}
	else { 
 	tempest_vector_list();
	if (total_length ) {clearme=1;tticks=m6502zpGetElapsedTicks(0xff);} else {clearme=0;}
	}
	//if (clearme==0){tempest_vector_list();clearme=1;}
}

READ_HANDLER(TempestIN0read)
{
    int val=0x7f;
    int tticks;
	 float me;
	static int clockticks=0;
	
	tticks=m6502zpGetElapsedTicks(0);

	val = val | ((tticks >> 1) & 0x80); //3Khz clock
	
	me = (((1775 * total_length)/ 1000000) * 1512);
	write_to_log("Total LENGTH HERE %f and TOTAL TICKS %d",me,tticks );

	
	//if ( tticks > me && clearme==1 ) {clearme=0;total_length=0;tticks=m6502zpGetElapsedTicks(0xff);}



	//if (tticks  & 0x100)                {bitclr(val, 0x80);} 
	if (clearme)                          {bitclr(val, 0x40);} 
	if (KeyCheck(config.ktestadv))        {bitclr(val, 0x20);} 
  	if (KeyCheck(config.ktest))           {temptest^=1;}
	if (KeyCheck(config.kreset))          {m6502zpreset();}
	if (key[config.kcoin1])               {bitclr(val, 0x01);}
	if (key[config.kcoin2])               {bitclr(val, 0x02);}
	if (temptest)                         {bitclr(val, 0x10);}

	return val;
}

WRITE_HANDLER(tempest_led_w)
{set_aae_leds( ~data & 0x02, ~data & 0x01,0);}

WRITE_HANDLER(colorram_w)
{
int bit3 = (~data >> 3) & 1;
int bit2 = (~data >> 2) & 1;
int bit1 = (~data >> 1) & 1;
int bit0 = (~data >> 0) & 1;
int r = bit1 * 0xee + bit0 * 0x11;
int g = bit3 * 0xee;
int b = bit2 * 0xee;

vec_colors[address-0x800].r=r;
vec_colors[address-0x800].g=g;
vec_colors[address-0x800].b=b;
//RIPPED THE ABOVE CODE FROM MAME. I COULD NEVER HAVE FIGURED IT OUT THIS NICE
}
WRITE_HANDLER(avg_reset_w)
{
clearme=0;
write_to_log("AVG RESET!!!!------------------------------------");
}//AVGRESET

WRITE_HANDLER(coin_write)
{if ((data & 0x08)){flipscreen=1;}else {flipscreen=0;}}

READ_HANDLER(RSpecial1)
{
	if (temptest) return gameImage[0][address];

	else return 0;
}


READ_HANDLER(RSpecial2)
{
	if (temptest) return gameImage[0][address];

	else return 0;
}

READ_HANDLER(RSpecial3)
{ 
	if (temptest) return gameImage[0][address];

	else return 0;

}

WRITE_HANDLER(WSpecial1)
{
	if (temptest) gameImage[0][address]=data;//write_to_log("Special Write1: Value %x Frame %d",data,frames);
}

WRITE_HANDLER(WSpecial2)
{
	if (temptest) gameImage[0][address]=data;//write_to_log("Special Write2: Value %x Frame %d",data,frames);
}

WRITE_HANDLER(WSpecial3)
{
	if (temptest) gameImage[0][address]=data;//write_to_log("Special Write3: Value %x Frame %d",data,frames);
}
///////////////////////  MAIN LOOP /////////////////////////////////////
void run_tempest()
{
		
	UINT32 dwResult = 0;
   	int x;
	int cycles = 1512000/60/4;
	UINT32 dwElapsedTicks = 0;
	//dwElapsedTicks = m6502zpGetElapsedTicks(0xff);
   
    write_to_log("BEGIN FRAME---------------------------------------");
	//tticks=m6502zpGetElapsedTicks(0xff); //Clear tick counter.
		
		for (x=0; x<4; x++)
		{
		        dwResult = m6502zpexec(cycles); 
				if (0x80000000 != dwResult)
				{
				m6502zpGetContext(psCpu1);
				allegro_message("Invalid instruction at %.2x\n", psCpu1->m6502pc);
				exit(1);
				}
		m6502zpint(0);
		do_sound();
		if (clearme) clearme=0;
		}
       
		
}

//////////////////////////////////////////////////////////////////////////

MEM_READ(TempestRead)
 MEM_ADDR(0x011b, 0x011b, RSpecial1)
 MEM_ADDR(0x011f, 0x011f, RSpecial2)
 MEM_ADDR(0x0455, 0x0455, RSpecial3)
 MEM_ADDR(0x0c00, 0x0c00, TempestIN0read)
 MEM_ADDR(0x0d00, 0x0d00, TempestDSW1)
 MEM_ADDR(0x0e00, 0x0e00, TempestDSW2)
 MEM_ADDR(0x60c0, 0x60cf, Pokey1Read)
 MEM_ADDR(0x60d0, 0x60df, Pokey2Read)
 MEM_ADDR(0x6040, 0x6040, MathboxStatusRead)
 MEM_ADDR(0x6050, 0x6050, EaromRead)
 MEM_ADDR(0x6060, 0x6060, MathboxLowbitRead)
 MEM_ADDR(0x6070, 0x6070, MathboxHighbitRead)
MEM_END

MEM_WRITE(TempestWrite)
 MEM_ADDR(0x011b, 0x011b, WSpecial1)
 MEM_ADDR(0x011f, 0x011f, WSpecial2)
 MEM_ADDR(0x0455, 0x0455, WSpecial3)
 MEM_ADDR(0x0800, 0x080f, colorram_w)
 MEM_ADDR(0x60c0, 0x60cf, Pokey0Write)
 MEM_ADDR(0x60d0, 0x60df, Pokey1Write)
 MEM_ADDR(0x6080, 0x609f, MathboxGo)
 MEM_ADDR(0x4000, 0x4000, coin_write)
 MEM_ADDR(0x4800, 0x4800, TempGo)
 MEM_ADDR(0x3000, 0x3fff, NoWrite)
 MEM_ADDR(0x6000, 0x603f, EaromWrite)
 MEM_ADDR(0x6040, 0x6040, EaromCtrl)
 MEM_ADDR(0x5800, 0x5800, avg_reset_w)
 MEM_ADDR(0x60e0, 0x60e0, tempest_led_w)
 MEM_ADDR(0x9000, 0xffff, NoWrite)
 MEM_ADDR(0x3000, 0x57ff, NoWrite)
MEM_END

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_tempest(void)
{   write_to_log("Initing tempest");
   	init6502(TempestRead,TempestWrite,0);
    remove_mouse(); //Reset mouse to ensure cursor is removed and to regain exclusive control
    install_mouse();
    turnamt=config.mouse1xs;
	cache_clear();
	clearme=0;
	total_length=0;clearme=0;temptest=0;
	

	
	if (gamenum==TEMPESTM) {  tbuffer=malloc(0x10000);
	                          memcpy(tbuffer,gameImage[0],0x10000);
							  INMENU=1;
	                        } //Save the menu code so we can overwrite it.
	//spinvert = config.mouse1x_invert;
	/*
	switch(gamenum){
	case 20: goodload=load_roms(gamename[gamenum], tempestm);romver=0;break;
	case 21: goodload=load_roms(gamename[gamenum], tempest);romver=1;break;
	case 22: goodload=load_roms(gamename[gamenum], tempest1);romver=2;break;
	case 23: goodload=load_roms(gamename[gamenum], tempest2);romver=3;break;
	case 24: goodload=load_roms(gamename[gamenum], tempest3);romver=4;break;
	case 25: goodload=load_roms(gamename[gamenum], alienst);romver=5;break;
	case 26: goodload=load_roms(gamename[gamenum], vortex);romver=6;break;
	case 27: goodload=load_roms(gamename[gamenum], temptube);romver=7;break;
	case 28: goodload=load_roms(gamename[gamenum], vbreak);romver=8;break;
	default: write_to_log("Something seriously wrong, exiting...");return 0;
	}
    
	*/
	BUFFER_SIZE=44100/60/4;
	chip=2;
	gain=6;
    
	Pokey_sound_init(1512000, 44100,chip,0 ); // INIT POKEY
	soundbuffer =  malloc(BUFFER_SIZE);
	stream = play_audio_stream(BUFFER_SIZE, 8, FALSE, 44100, config.pokeyvol, 128);
	LoadEarom();
	
		
	//LEVEL SELECTION HACK   (Does NOT Work on Protos) 
	if (config.hack){gameImage[0][0x90cd]=0xea;gameImage[0][0x90ce]=0xea;}
    difficulty = gameImage[0][0x60d0]; //SET Difficulty, hack really but it works.
	setup_ambient(VECTOR);
	m6502zpreset();
	m6502zpexec(100);
	m6502zpreset();

	return 0;   
}

void end_tempest()
{
    SaveEarom();
	save_dips();
	free_audio_stream_buffer(stream);
	stop_audio_stream(stream);
	free (soundbuffer);
}