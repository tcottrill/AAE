

/*
     Memory Map for Major Havoc

    Alpha Processor
                     D  D  D  D  D  D  D  D
    Hex Address      7  6  5  4  3  2  1  0                    Function
    --------------------------------------------------------------------------------
    0000-01FF     |  D  D  D  D  D  D  D  D   | R/W  | Program RAM (1/2K)
    0200-07FF     |  D  D  D  D  D  D  D  D   | R/W  | Paged Program RAM (3K)
    0800-09FF     |  D  D  D  D  D  D  D  D   | R/W  | Program RAM (1/2K)
                  |                           |      |
    1000          |  D  D  D  D  D  D  D  D   |  R   | Gamma Commuication Read Port
                  |                           |      |
    1200          |  D                        |  R   | Right Coin (Player 1=0)
    1200          |     D                     |  R   | Left Coin  (Player 1=0)
    1200          |        D                  |  R   | Aux. Coin  (Player 1=0)
    1200          |           D               |  R   | Diagnostic Step
    1200          |  D                        |  R   | Self Test Switch (Player 1=1)
    1200          |     D                     |  R   | Cabinet Switch (Player 1=1)
    1200          |        D                  |  R   | Aux. Coin Switch (Player 1=1)
    1200          |           D               |  R   | Diagnostic Step
    1200          |              D            |  R   | Gammma Rcvd Flag
    1200          |                 D         |  R   | Gamma Xmtd Flag
    1200          |                    D      |  R   | 2.4 KHz
    1200          |                       D   |  R   | Vector Generator Halt Flag
                  |                           |      |
    1400-141F     |              D  D  D  D   |  W   | ColorRAM
                  |                           |      |
    1600          |  D                        |  W   | Invert X
    1600          |     D                     |  W   | Invert Y
    1600          |        D                  |  W   | Player 1
    1600          |           D               |  W   | Not Used
    1600          |              D            |  W   | Gamma Proc. Reset
    1600          |                 D         |  W   | Beta Proc. Reset
    1600          |                    D      |  W   | Not Used
    1600          |                       D   |  W   | Roller Controller Light
                  |                           |      |
    1640          |                           |  W   | Vector Generator Go
    1680          |                           |  W   | Watchdog Clear
    16C0          |                           |  W   | Vector Generator Reset
                  |                           |      |
    1700          |                           |  W   | IRQ Acknowledge
    1740          |                    D  D   |  W   | Program ROM Page Select
    1780          |                       D   |  W   | Program RAM Page Select
    17C0          |  D  D  D  D  D  D  D  D   |  W   | Gamma Comm. Write Port
                  |                           |      |
    1800-1FFF     |  D  D  D  D  D  D  D  D   | R/W  | Shared Beta RAM(not used)
                  |                           |      |
    2000-3FFF     |  D  D  D  D  D  D  D  D   |  R   | Paged Program ROM (32K)
    4000-4FFF     |  D  D  D  D  D  D  D  D   | R/W  | Vector Generator RAM (4K)
    5000-5FFF     |  D  D  D  D  D  D  D  D   |  R   | Vector Generator ROM (4K)
    6000-7FFF     |  D  D  D  D  D  D  D  D   |  R   | Paged Vector ROM (32K)
    8000-FFFF     |  D  D  D  D  D  D  D  D   |  R   | Program ROM (32K)
    -------------------------------------------------------------------------------

    Gamma Processor

                     D  D  D  D  D  D  D  D
    Hex Address      7  6  5  4  3  2  1  0                    Function
    --------------------------------------------------------------------------------
    0000-07FF     |  D  D  D  D  D  D  D  D   | R/W  | Program RAM (2K)
    2000-203F     |  D  D  D  D  D  D  D  D   | R/W  | Quad-Pokey I/O
                  |                           |      |
    2800          |  D                        |  R   | Fire 1 Switch
    2800          |     D                     |  R   | Shield 1 Switch
    2800          |        D                  |  R   | Fire 2 Switch
    2800          |           D               |  R   | Shield 2 Switch
    2800          |              D            |  R   | Not Used
    2800          |                 D         |  R   | Speech Chip Ready
    2800          |                    D      |  R   | Alpha Rcvd Flag
    2800          |                       D   |  R   | Alpha Xmtd Flag
                  |                           |      |
    3000          |  D  D  D  D  D  D  D  D   |  R   | Alpha Comm. Read Port
                  |                           |      |
    3800-3803     |  D  D  D  D  D  D  D  D   |  R   | Roller Controller Input
                  |                           |      |
    4000          |                           |  W   | IRQ Acknowledge
    4800          |                    D      |  W   | Left Coin Counter
    4800          |                       D   |  W   | Right Coin Counter
                  |                           |      |
    5000          |  D  D  D  D  D  D  D  D   |  W   | Alpha Comm. Write Port
                  |                           |      |
    5800          |  D  D  D  D  D  D  D  D   |  W   | Speech Data Write / Write Strobe Clear
    5900          |                           |  W   | Speech Write Strobe Set
                    |                           |      |
    6000-61FF     |  D  D  D  D  D  D  D  D   | R/W  | EEROM
    8000-BFFF     |  D  D  D  D  D  D  D  D   |  R   | Program ROM (16K)
    -----------------------------------------------------------------------------



    MAJOR HAVOC DIP SWITCH SETTINGS

    $=Default

    DIP Switch at position 13/14S

                                      1    2    3    4    5    6    7    8
    STARTING LIVES                  _________________________________________
    Free Play   1 Coin   2 Coin     |    |    |    |    |    |    |    |    |
        2         3         5      $|Off |Off |    |    |    |    |    |    |
        3         4         4       | On | On |    |    |    |    |    |    |
        4         5         6       | On |Off |    |    |    |    |    |    |
        5         6         7       |Off | On |    |    |    |    |    |    |
    GAME DIFFICULTY                 |    |    |    |    |    |    |    |    |
    Hard                            |    |    | On | On |    |    |    |    |
    Medium                         $|    |    |Off |Off |    |    |    |    |
    Easy                            |    |    |Off | On |    |    |    |    |
    Demo                            |    |    | On |Off |    |    |    |    |
    BONUS LIFE                      |    |    |    |    |    |    |    |    |
    50,000                          |    |    |    |    | On | On |    |    |
    100,000                        $|    |    |    |    |Off |Off |    |    |
    200,000                         |    |    |    |    |Off | On |    |    |
    No Bonus Life                   |    |    |    |    | On |Off |    |    |
    ATTRACT MODE SOUND              |    |    |    |    |    |    |    |    |
    Silence                         |    |    |    |    |    |    | On |    |
    Sound                          $|    |    |    |    |    |    |Off |    |
    ADAPTIVE DIFFICULTY             |    |    |    |    |    |    |    |    |
    No                              |    |    |    |    |    |    |    | On |
    Yes                            $|    |    |    |    |    |    |    |Off |
                                    -----------------------------------------

        DIP Switch at position 8S

                                      1    2    3    4    5    6    7    8
                                    _________________________________________
    Free Play                       |    |    |    |    |    |    | On |Off |
    1 Coin for 1 Game               |    |    |    |    |    |    |Off |Off |
    1 Coin for 2 Games              |    |    |    |    |    |    | On | On |
    2 Coins for 1 Game             $|    |    |    |    |    |    |Off | On |
    RIGHT COIN MECHANISM            |    |    |    |    |    |    |    |    |
    x1                             $|    |    |    |    |Off |Off |    |    |
    x4                              |    |    |    |    |Off | On |    |    |
    x5                              |    |    |    |    | On |Off |    |    |
    x6                              |    |    |    |    | On | On |    |    |
    LEFT COIN MECHANISM             |    |    |    |    |    |    |    |    |
    x1                             $|    |    |    |Off |    |    |    |    |
    x2                              |    |    |    | On |    |    |    |    |
    BONUS COIN ADDER                |    |    |    |    |    |    |    |    |
    No Bonus Coins                 $|Off |Off |Off |    |    |    |    |    |
    Every 4, add 1                  |Off | On |Off |    |    |    |    |    |
    Every 4, add 2                  |Off | On | On |    |    |    |    |    |
    Every 5, add 1                  | On |Off |Off |    |    |    |    |    |
    Every 3, add 1                  | On |Off | On |    |    |    |    |    |
                                    -----------------------------------------

        2 COIN MINIMUM OPTION: Short pin 6 @13N to ground.

***************************************************************************/

#include "mhavoc.h"
#include "globals.h"
#include "samples.h"
#include "vector.h"
#include "glcode.h"
#include "dips.h"
#include "keysets.h"
#include "input.h"
#include "cpuintfaae.h"
#include "sndhrdw/pokyintf.h"
#include "earom.h"

static struct POKEYinterface pokey_interface =
{
	2,			/* 4 chips */
	1,			/* 1 update per video frame (low quality) */
	1512000,
	255,	/* volume */
	NO_CLIP,
	/* The 8 pot handlers */
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	{ 0, 0, 0, 0 },
	/* The allpot handler */
	{ 0, 0, 0, 0 },
};


static UINT8 player_1;

static UINT8 alpha_irq_clock;
static UINT8 alpha_irq_clock_enable;
static UINT8 gamma_irq_clock;

static UINT8 *ram_base;
static UINT8 has_gamma_cpu;

static int ram_bank[2] = { 0x20200, 0x20800 };
static int ram_bank_sel=0;

static int rom_bank[4] = { 0x10000, 0x12000, 0x14000, 0x16000 };
static int rom_bank_sel=0;

static int reset1=0;
static int nmicpu1=0;
static int MHAVGDONE=1;
int noint=0;

int mhavoc_sh_start(void)
{
	int rv;

    rv = pokey_sh_start (&pokey_interface);
    return rv;
    //return(tms5220_sh_start (&tms5220_interface));
}


void mhavoc_sh_stop(void)
{
    pokey_sh_stop ();
   // tms5220_sh_stop ();
}

void mhavoc_sh_update()
{
    pokey_sh_update ();
   // tms5220_sh_update();
}




WRITE_HANDLER(avgdvg_reset_w)
{ write_to_log("AVGDVG RESET @@@@@@@@@@@@@@@@@@@@");}

WRITE_HANDLER(avg_go)
{write_to_log("AVG RUN CALLED!@");MH_generate_vector_list();MHAVGDONE=0;}
	
WRITE_HANDLER(mhavoc_out_0_w)
{
	UINT32 dwResult = 0;

	//player_1 = data & 0x20;
	/* Emulate the roller light (Blinks on fatal errors) */
	set_aae_leds(data & 0x01,0,0);
	if (data & 0x01) write_to_log("LED ERROR BLINK");
}

WRITE_HANDLER(mhavoc_out_1_w )
{
	 write_to_log("LED OUT WRITE");
	set_aae_leds(0,data & 0x01,0);
	set_aae_leds(0,0,(data & 0x02)>>1);
}
/* Simulates frequency and vector halt */
READ_HANDLER(alphaone_port_0_r)
{
	int res;
    int ticks;
	
	ticks=m6502zpGetElapsedTicks(0);
   // write_to_log("IN0 READ");
	res = 0xf7;//0xf7;//readinputport(0);
	//if (player_1)
		//res = 0x03;//(res & 0x3f) | (readinputport (5) & 0xc0);

	/* Emulate the 2.4Khz source on bit 2 (divide 2.5Mhz by 1024) */
	  if (ticks & 0x400)
	  res &=~0x02;
	  else
	  res|=0x02;
   
	if (MHAVGDONE)
		res |=0x01;
	else
		res &=~0x01;

	if (key[KEY_LCONTROL])    bitclr(res,0x80);
	return res;
}

READ_HANDLER(alphaone_port_1_r)
{
	static int service=0;

	int res;
   // write_to_log("IN1 Read");
	res=0xff;//fc;//readinputport(1);
	
  if (key[KEY_5])    bitclr(res,0x80); 
  if (key[KEY_6])    bitclr(res,0x40); 
  
  if (KeyCheck(KEY_F2))  service^=1; 
  if (service) bitclr(res,0x10); 
  
	return res;
}
READ_HANDLER(input_port_2_r)
{
	 static unsigned char c=0x80;

  if (key[KEY_RIGHT])	c-=6;

  if (key[KEY_LEFT])	c+=6;
 
  if (c<0) c=0;
  if (c>0xff) c=0xff;
  return c;
}

WRITE_HANDLER(mhavoc_ram_banksel_w)
{
	data &= 0x01;
	ram_bank_sel=data;

write_to_log("Alpha RAM Bank select: %02x",data);

}


WRITE_HANDLER(mhavoc_rom_banksel_w)
{
	static int lastbank=0;
	//static const offs_t bank[4] = { 0x10000, 0x12000, 0x14000, 0x16000 };
	data &= 0x03;
	rom_bank_sel=data;
	if (rom_bank_sel !=  lastbank){
	if (rom_bank_sel==1) {memcpy(gameImage[0]+0x2000,gameImage[0]+0x12000,0x2000);}
	if (rom_bank_sel==0) {memcpy(gameImage[0]+0x2000,gameImage[0]+0x10000,0x2000);}
	lastbank = rom_bank_sel;
	}

	write_to_log("Alpha ROM Bank select: %02x",data);
	
}

WRITE_HANDLER( mhavoc_alpha_irq_ack_w )
{
	/* clear the line and reset the clock */
	//cpu_set_irq_line(0, 0, CLEAR_LINE);
	write_to_log("Alpha IRQ ACK!",data);
	alpha_irq_clock = 0;
	alpha_irq_clock_enable = 1;
}
////////////////////////////////////////////////////////////////////////////////////////////////


WRITE_HANDLER( MWA_BANK1_W)
{
 	int bank;

	if (ram_bank_sel==0) {bank=0x20200;} else {bank = 0x20800;}
    bank=bank+(address -0x200);

  gameImage[0][bank]=data;
 // write_to_log("Alpha banked ram Write: address %x data %x",bank,data);
}


READ_HANDLER( MRA_BANK1_R)
{
	int bank;
	
	if (ram_bank_sel==0) {bank=0x20200;} else {bank = 0x20800;}
	bank=bank+(address -0x200);
	//write_to_log("Alpha Banked ram Read: address %x data %x",bank, gameImage[0][bank]);
	return gameImage[0][bank];
}

READ_HANDLER( dual_pokey_r )
{
	int offset= address-0x1020;

	int pokey_num = (offset >> 3) & 0x01;
	int control = (offset & 0x10) >> 1;
	int pokey_reg = (offset % 8) | control;

	if (pokey_num == 0){
		//write_to_log("pokey read 0 %x",pokey_reg);
		if (pokey_reg==0x0a) return rand();
		else return pokey1_r(pokey_reg);}
	else{
		//write_to_log("pokey read 1 %x",pokey_reg);
		return pokey2_r(pokey_reg);}
}


WRITE_HANDLER( dual_pokey_w )
{
	int offset= address-0x1020;

	int pokey_num = (offset >> 3) & 0x01;
	int control = (offset & 0x10) >> 1;
	int pokey_reg = (offset % 8) | control;

	if (pokey_num == 0){
		write_to_log("pokey write 0 %x",pokey_reg);
		pokey1_w(pokey_reg, data);}
	else{
		write_to_log("pokey write 1 %x",pokey_reg);
		pokey2_w(pokey_reg, data);}
}




WRITE_HANDLER(mhavoc_colorram_w)
{
	int i = (data & 4) ? 0x0f : 0x08;
	int r = (data & 8) ? 0x00 : i;
	int g = (data & 2) ? 0x00 : i;
	int b = (data & 1) ? 0x00 : i;

   vec_colors[address-0x10e0].r=r;
   vec_colors[address-0x10e0].g=g;
   vec_colors[address-0x10e0].b=b;
}

/////////////////////////////VECTOR GENERATOR//////////////////////////////////
static void MH_generate_vector_list (void)
{

	int pc=0x4000;
	int sp;
	int stack [8];
    int flipword = 0;
	int scale=0;
	int statz   = 0;
	int sparkle = 0;
	int xflip   = 0;
    float gc =1.0;  //gamma correction factor
	int color   = 0;
	int myx=0;
    int myy=0;
	int currentx, currenty=0;
	int done    = 0;
	int firstwd, secondwd;
	int opcode;
  
	int x, y, z=0, b, l, d, a;
	int deltax, deltay=0;
	
	int vectorbank=0x18000;
	static int lastbank=0;
	int red,green,blue;
	int ywindow=1;
	int clip=0;
	float sy=0;
	float ey=0;
	float sx=0;
	float ex=0;
	int nocache=0;
	int draw=1;

	sp = 0;
	statz = 0;
	color = 0;
	scale = 0;          
	
    
     firstwd = memrdwd (pc);
	 pc++;pc++;
	 secondwd = memrdwd (pc);
     
	 if ((firstwd == 0) && (secondwd == 0))
	 {write_to_log("VGO with zeroed vector memory at %x\n",pc);return;}


      pc=0x4000;

  	
	while (!done)
	{
		
		firstwd = memrdwd (pc);
		opcode = firstwd >> 13;
       	pc++;pc++;
	
	if (opcode == VCTR) //Get the second word if it's a draw command
		{secondwd = memrdwd (pc);pc++;pc++;}
	
		if ((opcode == STAT) && ((firstwd & 0x1000) != 0))
			opcode = SCAL;

		switch (opcode)
		{
			case VCTR:
			    
				x = twos_comp_val (secondwd, 13);
				y = twos_comp_val (firstwd, 13);
				
				z = (secondwd >> 12) & ~0x01;

				goto DRAWCODE;
				
				/* z is the maximum DAC output, and      */
				/* the 8 bit value from STAT does some   */
				/* fine tuning. STATs of 128 should give */
				/* highest intensity. */
				break;

			case SVEC:
				x = twos_comp_val (firstwd, 5) << 1;
				y = twos_comp_val (firstwd >> 8, 5) << 1;
				z = ((firstwd >> 4) & 0x0e);

DRAWCODE:	if (z == 2){z = statz;}if (z) {z = (z << 4) | 0x1f;}
			
			myx=x*(scale/2);///2
			myy=y*(scale/2);///2
			
			deltax = x * scale;
			if (xflip) deltax = -deltax;
			deltay = y * scale;
			
			 if (z>0)
			 {      
				
				 if (vec_colors[color].r) red=z; else red=0;
				 if (vec_colors[color].g) green=z; else green=0;
				 if (vec_colors[color].b) blue=z; else blue=0;

                    ey= ((currenty-deltay)>>VEC_SHIFT);   
				    sy= currenty>>VEC_SHIFT;
					sx= (currentx>>VEC_SHIFT);
					ex= (currentx+deltax)>>VEC_SHIFT;
					clip=275;
					if (ywindow==1)
					{
					//Line color 0 clipping
					
					if (sy < clip && ey < clip ){draw=0;}else {draw=1;}

					if (ey < clip && ey < sy ){
						                        ex=((clip-ey)*((ex-sx)/(ey-sy)))+ex;ey=clip;
					                         }
						
					if (sy< clip && sy < ey ){
						                      sx=((clip-sy)*((sx-ex)/(sy-ey)))+sx;sy=clip;
					                         }
									
				  
					}
                   				    
					
					
					if (draw) {
						            add_color_line(sx, sy,ex,ey,red,green,blue);}
									add_color_point(sx, sy,red,green,blue);
									add_color_point(ex, ey,red,green,blue);
									}
					               
					               

				currentx += deltax;
				currenty -= deltay;
			    
				total_length+=vector_timer(myx, myy);   
				break;

			case STAT:
					
                    color = (firstwd) & 0x000f;
					statz = (firstwd >> 4) & 0x000f;
				    sparkle = firstwd & 0x0800;
					xflip = firstwd & 0x0400;
					vectorbank = 0x18000 + ((firstwd >> 8) & 3) * 0x2000;
					
					if (lastbank!=vectorbank){lastbank=vectorbank;
					write_to_log("Vector Bank Switch %x",0x18000 + ((firstwd >> 8) & 3) * 0x2000);
					memcpy(gameImage[0]+0x6000,gameImage[0]+vectorbank,0x2000);
					}
					break;

			case SCAL:
				b = ((firstwd >> 8) & 0x07)+8;
				l = (~firstwd) & 0xff;
				scale = (l << VEC_SHIFT) >> b;		
			    scale=scale*2; //Double the scale for 1024x768 resolution
				
				if (firstwd & 0x0800)
					{				
						if (ywindow == 0) { ywindow = 1;}	else {ywindow = 0;}
					}
				
				break;

			case CNTR:
				d = firstwd & 0xff;
				currentx = 500 << VEC_SHIFT;  
				currenty = 512 << VEC_SHIFT;  

				break;

			case RTSL:

				if (sp == 0)
				{	write_to_log("*** Vector generator stack underflow! ***");
					done = 1;
					sp = MAXSTACK - 1;
				}
				else
				{sp--;
   				 pc = stack [sp];
				}
				break;

			case HALT:
				done = 1;
				break;

			case JMPL:
				a = 0x4000 + ((firstwd & 0x1fff) <<1) ;
				/* if a = 0x0000, treat as HALT */
				if (a == 0x4000)
				{done = 1;}
				else
				{pc = a;}
				break;

			case JSRL:
				a = 0x4000 + ((firstwd & 0x1fff) <<1) ;
				/* if a = 0x0000, treat as HALT */
				if (a == 0x4000)
				{done = 1;}
				else
				{   stack [sp] = pc;
					if (sp == (MAXSTACK - 1))
					{
						write_to_log ("\n*** Stack overflow! ***\n");
						done = 1;
						sp = 0;
					}
					else
					{
						sp++;
                        pc = a;
					}
			}
				break;

			default: write_to_log("Error in AVG engine");
			}	
		}

	
}

////////////////////////////////////////////////////////////////////////////////////////////////////
MEM_READ(AlphaRead)
	/* 0.5K Program Ram */
//MEM_ADDR( 0x0000, 0x01ff, RAM0_READ)
	
MEM_ADDR( 0x0200, 0x07ff, MRA_BANK1_R)			/* 3K Paged Program RAM	*/

//MEM_ADDR( 0x0800, 0x09ff, RAM1_READ)
MEM_ADDR( 0x1020, 0x103f,  dual_pokey_r)
		/* 0.5K Program RAM */
	/* Gamma Read Port */
 MEM_ADDR(0x1040, 0x1040, alphaone_port_0_r )	/* Alpha Input Port 0 */
 MEM_ADDR(0x1060, 0x1060, alphaone_port_1_r)		/* Gamma Input Port	*/
 MEM_ADDR(0x1080, 0x1080, input_port_2_r)		/* Roller Controller Input*/
 
		/* Shared Beta Ram */
 //MEM_ADDR(0x2000, 0x3fff, Alpha_ReadBank )			/* Paged Program ROM (32K) */
        /* Vector RAM	(4K) */
		/* Vector ROM (4K) */
//MEM_ADDR(0x6000, 0x7fff, VectorRom )			/* Paged Vector ROM (32K) */
//MEM_ADDR(0x8000, 0xf000,  ROM_READ )      /* Program ROM (32K) */
MEM_END


/* Main Board Writemem */
MEM_WRITE(AlphaWrite)
/* 0.5K Program Ram */
MEM_ADDR( 0x0200, 0x07ff, MWA_BANK1_W )			/* 3K Paged Program RAM */
	/* 0.5K Program RAM */
MEM_ADDR(0x1020, 0x103f, dual_pokey_w)
MEM_ADDR(0x1040, 0x1040, NoWrite )			/* don't care */
MEM_ADDR(0x10a0, 0x10a0, mhavoc_out_0_w )		/* Control Signals */
MEM_ADDR(0x10a4, 0x10a4, avg_go)			/* Vector Generator GO */
MEM_ADDR(0x10a8, 0x10a8, NoWrite )			/* Watchdog Clear */
MEM_ADDR(0x10ac, 0x10ac, avgdvg_reset_w )		/* Vector Generator Reset */
MEM_ADDR(0x10b0, 0x10b0, mhavoc_alpha_irq_ack_w)        /* IRQ ack */
MEM_ADDR(0x10b4, 0x10b4, mhavoc_rom_banksel_w )      /* Program ROM Page Select */
MEM_ADDR(0x10b8, 0x10b8,  mhavoc_ram_banksel_w )      /* Program RAM Page Select */
	/* Gamma Communication Write Port */
MEM_ADDR(0x10e0, 0x10ff, mhavoc_colorram_w )	/* ColorRAM */
		/* Shared Beta Ram */
MEM_ADDR(0x2000, 0x3fff, NoWrite )			/* Major Havoc writes here.*/
 /* Vector Generator RAM	*/
MEM_ADDR(0x6000, 0xffff, NoWrite )
MEM_END




void run_mhavoc()
{   
	int x;
	int cycles1 = (2500000 /50)/8;
	int cycles2 = (1512000 /gamefps)/6;
	
	
	UINT32 dwElapsedTicks = 0;
	UINT32 dwResult = 0;
	dwElapsedTicks = m6502GetElapsedTicks(0xff);
    
    
		
        write_to_log("------------FRAME START --------------"); 
		//total_length=0;vecwrite=0;hit=0; //reset vector timer
		//ticks=m6502zpGetElapsedTicks(1); //Clear tick counter.
		MHAVGDONE=1;
		
		for (x=0; x < 8; x++)
		{
			
				m6502GetElapsedTicks(0xff);
				
				if (x==6 && MHAVGDONE)  {MHAVGDONE=0;}

				dwResult = m6502exec(cycles1);		// 1500 Instructions for 1.5MHZ CPU
				if (0x80000000 != dwResult)
					{
						m6502GetContext(&cpu1);
						allegro_message("Invalid instruction at %.2x on CPU 0", cpu1.m6502pc);
					}
				write_to_log("Cycles ran this int %d",m6502GetElapsedTicks(0));
		
				m6502int(1);
								
        ////////////////////////////////////////////////////////////////////////////////////////////////
				
		     
		}
		
        mhavoc_sh_update();
		
	
}

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_mhavoc(void)
{
  
    
   init6502B(AlphaRead,AlphaWrite,0);
  // mhinit6502(GammaRead,GammaWrite,1);
    write_to_log("CPU init complete");
/*
	gameImage[0][0x10a0] = 0xfc;  
    gameImage[0][0x8001] = 0xff;
    gameImage[0][0x8254] = 0x8f;
    gameImage[0][0x9120] = 0x12;
	gameImage[0][0x96d0] = 0x4c;
    gameImage[0][0xe509] = 0x98;
    gameImage[0][0xea91] = 0xc9;
	gameImage[0][0xec76] = 0x2c;
    gameImage[0][0xefff] = 0x85;
    gameImage[0][0xffff] = 0x85;
*/

	/*
	   m6502zpSetContext(&cpu2);	//
	   write_to_log("Test CPU 2A complete"); 
	   m6502zpreset();
	   m6502zpexec(100);
	   write_to_log("Test CPU 2B complete"); 
	   m6502zpreset();
	    write_to_log("Test CPU 2c complete"); 
	   m6502zpGetContext(&cpu2);	// Get CPU #2's state information
       write_to_log("Test CPU 2 complete");     
	
	
	m6502zpSetContext(&cpu1);	// Set CPU #1's information
	   write_to_log("Test CPU 1A complete"); 
	   m6502zpreset();
	   m6502zpexec(1500);
	    m6502zpreset();
	    write_to_log("Test CPU 1B complete"); 
	   m6502zpGetContext(&cpu1);	// Get CPU #1's state info
       write_to_log("Test CPU 1 complete");     
	  */
   
	  memcpy(gameImage[0]+0x6000,gameImage[0]+0x18000,0x2000);
      m6502reset();
	  m6502exec(50);
	  m6502reset();

	mhavoc_sh_start();
    cache_clear();
	
	return 0;
}

void end_mhavoc()
{
 mhavoc_sh_stop();
}

//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////


