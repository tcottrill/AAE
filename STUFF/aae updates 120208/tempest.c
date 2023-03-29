/* Tempest Emu */

#include "tempest.h"
#include "globals.h"
#include "vector.h"
#include "glcode.h"
#include "input.h"
#include "cpuintfaae.h"
#include "sndhrdw/pokyintf.h"
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
int tempprot=1;

#define MAXSTACK 8
static char *tbuffer;

static struct POKEYinterface pokey_interface =
{
	2,			/* 4 chips */
	1512000,
	255,	/* volume */
	6, //POKEY_DEFAULT_GAIN/2
	USE_CLIP,
	/* The 8 pot handlers */
	/* The 8 pot handlers */
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	/* The allpot handler */
	{ 0, 0, },
};


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
   
    cache_clear();
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

READ_HANDLER (Pokey1Read)
{

static int rng=0;

	if (address==0x60ca)
	{	if ((gameImage[0][0x60cf] & 0x02) != 0x00)
				{
					rng = (rand () >> 12) & 0xff;
					gameImage[0][address]=rng;
					return rng;
				}
	else return rng;
	}
  // if (address==0x60c8) return z80dip1; 
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
    int val=0;
	static int rng=0;
   
	//if (address==0x60d8) return z80dip2;
	if (address==0x60da)
	{
	 if ((gameImage[0][0x60df] & 0x02) != 0x00)
		{
			rng = (rand () >> 12) & 0xff;
		    gameImage[0][address]=rng;
			return rng;
		}
	 else return rng;
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
	if (clearme==1) return;
	if (clearme==0){tempest_vector_list();clearme=1;}
}

READ_HANDLER(TempestIN0read)
{
    int val=0x7f;//0x7f
    //int tticks;
	
	//tticks= get_elapsed_ticks(0);//m6502zpGetElapsedTicks(0);
	val = val | ((get_eterna_ticks(0) >> 1) & 0x80); //3Khz clock
    //if ( get_hertz_counter())             {bitset(val, 0x80);} 
	if (clearme)                          {bitclr(val, 0x40);}
	if (key[config.ktestadv])             {bitclr(val, 0x20);} 
  	if (KeyCheck(config.ktest))           {temptest^=1;}
	
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
{clearme=0;}//AVGRESET

WRITE_HANDLER(coin_write)
{if ((data & 0x08)){flipscreen=1;}else {flipscreen=0;}}

WRITE_HANDLER(WSpecial1)
{
	 gameImage[0][address]=0;
}

WRITE_HANDLER(WSpecial2)
{
	gameImage[0][address]=0;
}

WRITE_HANDLER(WSpecial3)
{
	gameImage[0][address]=0;
}
///////////////////////  MAIN LOOP /////////////////////////////////////
void set_tempest_video()
{
	clearme=0;
}



READ_HANDLER (TempestDSW1)
{if (gamenum==25){return 0;}else {return z80dip1;}}

READ_HANDLER (TempestDSW2)
{if (gamenum==25){return 0;}else {return z80dip2;}}
//////////////////////////////////////////////////////////////////////////

MEM_READ(TempestRead)
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
 MEM_ADDR(0x60c0, 0x60cf, pokey_1_w)
 MEM_ADDR(0x60d0, 0x60df, pokey_2_w)
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



void run_tempest()
{
	if (KeyCheck(config.kreset))          {cpu_needs_reset(0);total_length=0;clearme=0;}
	 //gameImage[0][0x48]=0x7;
    // gameImage[0][0x33a]=0;
	pokey_sh_update();
	
}
/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_tempest(void)
{  
   	init6502Z(TempestRead,TempestWrite,0);
    remove_mouse(); //Reset mouse to ensure cursor is removed and to regain exclusive control
    install_mouse();
    turnamt=config.mouse1xs;
	cache_clear();
	total_length=0;clearme=0;
	
	if (gamenum==TEMPESTM) {  tbuffer=malloc(0x10000);
	                          memcpy(tbuffer,gameImage[0],0x10000);
							  INMENU=1;
	                        } //Save the menu code so we can overwrite it.
	
	if (gamenum==TEMPTUBE ||
	    gamenum==TEMPEST1 ||
	    gamenum==TEMPEST2 ||
		gamenum==TEMPEST3 ||
		gamenum==VBREAK   ||
	    gamenum==TEMPEST )
	{
		LoadEarom();
		//LEVEL SELECTION HACK   (Does NOT Work on Protos) 
	    if (config.hack){gameImage[0][0x90cd]=0xea;gameImage[0][0x90ce]=0xea;}
	}
	
	
    difficulty = gameImage[0][0x60d0]; //SET Difficulty, hack really but it works.
	
	pokey_sh_start (&pokey_interface);
	   
	return 0;   
}

void end_tempest()
{
    if (gamenum==TEMPTUBE ||
	    gamenum==TEMPEST1 ||
	    gamenum==TEMPEST2 ||
		gamenum==TEMPEST3 ||
		gamenum==VBREAK   ||
	    gamenum==TEMPEST )
	{
		SaveEarom();
	}
	pokey_sh_stop();
	
}