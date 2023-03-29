

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
	4,			/* 4 chips */
	1,			/* 1 update per video frame (low quality) */
	1512000,
	255,	/* volume */
	USE_CLIP,
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

static UINT8 alpha_data=0;
static UINT8 alpha_rcvd=0;
static UINT8 alpha_xmtd=0;

static UINT8 gamma_data=0;
static UINT8 gamma_rcvd=0;
static UINT8 gamma_xmtd=0;

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
static unsigned char vectorbank;
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
{write_to_log("AVG RUN CALLED!@");MH_generate_vector_list();MHAVGDONE=1;}
	
WRITE_HANDLER(mhavoc_out_0_w)
{
	UINT32 dwResult = 0;

	if (!(data & 0x08))
	{
		write_to_log ( "\t\t\t\t*** resetting gamma processor. ***");
		reset1=1; //m6502zpreset(); //CPU1?
		
		alpha_rcvd=0;
		alpha_xmtd=0;
		gamma_rcvd=0;
		gamma_xmtd=0;
		noint=1;m6502zpReleaseTimeslice();
		
	}
	//player_1 = data & 0x20;
	/* Emulate the roller light (Blinks on fatal errors) */
	set_aae_leds(data & 0x01,0,0);
	if (data & 0x01) write_to_log("LED ERROR BLINK");

}

WRITE_HANDLER(mhavoc_out_1_w )
{
	set_aae_leds(0,data & 0x01,0);
	set_aae_leds(0,0,(data & 0x02)>>1);
}
/* Simulates frequency and vector halt */
READ_HANDLER(mhavoc_port_0_r)
{
	int res;
    int ticks;
	
	ticks=m6502zpGetElapsedTicks(0);
    
	res = 0xd0;//0xf7;//readinputport(0);
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

	if (gamma_rcvd==1)
		res |=0x08;
	else
		res &=~0x08;

	if (gamma_xmtd==1)
		res |=0x04;
	else
		res &=~0x04;

	return (res & 0xff);
}

READ_HANDLER(mhavoc_port_1_r)
{
	int res;

	res=0xdf;//readinputport(1);

	if (alpha_rcvd==1)
		res |=0x02;
	else
		res &=~0x02;

	if (alpha_xmtd==1)
		res |=0x01;
	else
		res &=~0x01;

	return (res & 0xff);
}
READ_HANDLER(input_port_2_r)
{
	int c = 0x72;
  
	return c;
}

READ_HANDLER(input_port_4_r)
{
	int c=0x85;

   return c;
}


WRITE_HANDLER(mhavoc_ram_banksel_w)
{
	//static const offs_t bank[2] = { 0x20200, 0x20800 };

	data &= 0x01;
	ram_bank_sel=data;
	//cpu_setbank(1, &ram_base[bank[data]]);
write_to_log("Alpha RAM Bank select: %02x",data);
}


WRITE_HANDLER(mhavoc_rom_banksel_w)
{
	//static const offs_t bank[4] = { 0x10000, 0x12000, 0x14000, 0x16000 };
	data &= 0x03;
	rom_bank_sel=data;
	//cpu_setbank(2, &ram_base[bank[data]]);
/*	logerror("Alpha ROM select: %02x\n",data);*/
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
  gameImage[0][ram_bank[ram_bank_sel]+(address-0x200)]=data;
 // write_to_log("Alpha Mem Write: address %x data %x",ram_bank[ram_bank_sel]+(address-0x200),data);
}


READ_HANDLER( MRA_BANK1_R)
{
	write_to_log("Alpha Banked Rom Read: address %x data %x",ram_bank[ram_bank_sel]+(address-0x200));
	return gameImage[0][ram_bank[ram_bank_sel]+(address-0x200)];
}
READ_HANDLER( mh_quad_pokey_r )
{
return quad_pokey_r (address&0xff);
}


WRITE_HANDLER(  mh_quad_pokey_w)
{
  quad_pokey_w (address&0xff, data);
}

READ_HANDLER(Alpha_ReadBank)
{
	write_to_log("Alpha reading from Rom Bank %x",rom_bank[rom_bank_sel]);
return gameImage[0][rom_bank[rom_bank_sel]  + (address-0x2000) ];
}

WRITE_HANDLER( mhavoc_gamma_irq_ack_w )
{
	/* clear the line and reset the clock */
	//cpu_set_irq_line(1, 0, CLEAR_LINE);
	write_to_log("Gamma IRQ ACK!",data);
	gamma_irq_clock = 0;
}

/////////////////////////////////////////GAMMA CPU ///////////////////////////////////////////
/* Read from the gamma processor */
READ_HANDLER(mhavoc_gamma_r)
{
	write_to_log(  "Now reading from gamma processor data sent by alpha: %02x", gamma_data);
	alpha_rcvd=1;
	gamma_xmtd=0;
	return gamma_data;
}

/* Read from the alpha processor */
READ_HANDLER(mhavoc_alpha_r)
{
	write_to_log(  "Now Reading from alpha processor data sent by gamma: %02x", alpha_data);
	gamma_rcvd=1;
	alpha_xmtd=0;
	return alpha_data;
}
/* Write to the alpha processor */
WRITE_HANDLER( mhavoc_alpha_w)
{
	write_to_log( "Now Writing to alpha processor Return from Gamma: %02x", data);
	alpha_rcvd=0;
	gamma_xmtd=1;
	gamma_data = data;
	 m6502zpReleaseTimeslice();
}

/* Write to the gamma processor */
WRITE_HANDLER( mhavoc_gamma_w)
{
	write_to_log(  "Now writing to gamma processor from Alpha: %02x", data);
	gamma_rcvd=0;
	alpha_xmtd=1;
	alpha_data = data;
	
	
	nmicpu1=1;noint=1;
    m6502zpReleaseTimeslice();
	
	//cpu_cause_interrupt (1, INT_NMI);
    //m6502zpint(1);write_to_log("interrupt, processor 2 (gamma)");
	/* the sound CPU needs to reply in 250ms (according to Neil Bradley) */
	//timer_set (TIME_IN_USEC(250), 0, 0);
    
}


READ_HANDLER(mhavoc_gammaram_r)
{
	//write_to_log("Ram read from Gamma CPU %x",address);
	return gameImage[1][(address-0x800) & 0x7ff];
}

WRITE_HANDLER( mhavoc_gammaram_w)
{
	//write_to_log("Ram write from Gamma CPU address:%x data:%x",address,data);
	gameImage[1][(address-0x800) & 0x7ff] = data;
}
WRITE_HANDLER(mhavoc_colorram_w)
{
   int bit3 = (~data >> 3) & 1;
   int bit2 = (~data >> 2) & 1;
   int bit1 = (~data >> 1) & 1;
   int bit0 = (~data >> 0) & 1;
   int r = bit3 * 0xee + bit2 * 0x11;
   int g = bit1 * 0xee;
   int b = bit0 * 0xee;

   vec_colors[address-0x1400].r=r;
   vec_colors[address-0x1400].g=g;
   vec_colors[address-0x1400].b=b;
   write_to_log("Mhavoc Writing to color ram");
//RIPPED THE ABOVE CODE FROM MAME. I COULD NEVER HAVE FIGURED IT OUT THIS NICE
}



void set_mh_colors(void)
{   
	int i,c=0;
	float *cmap;
	
	float colormapmh[]={0,0,0,
		                0,0,1,
						0,1,0,
						0,1,1,
						1,0,0,
						1,0,1,
						1,1,0,
						1,1,1,
						1,0,0,
						1,1,1,
						0,0,1,
						0,1,0,
						1,0,0,
						1,0,0,
						1,0,1,
						1,1,1};

	cmap=colormapmh;
		
	for (i=0;i<16;i++)
	{
	   vec_colors[i].r=cmap[c];
       vec_colors[i].g=cmap[c+1];
       vec_colors[i].b=cmap[c+2];
	   c+=3;
	}
}




static UINT8 MHControls (UINT32 address, struct MemoryReadByte *psMemRead)
{
  unsigned char c=0xff;
	
  if (key[config.kp1right])    bitclr(c, 0x01); 
  if (key[config.kp1left])    bitclr(c, 0x02);  
  if (key[config.kp1down])   bitclr(c, 0x04); 
  if (key[config.kp1up])    bitclr(c, 0x08); 
  
  return c;
}



static UINT8 VectorRom (UINT32 address, struct MemoryReadByte *psMemRead)
{
 if (vectorbank==0) return gameImage[0][address];
 else return gameImage[0][(address-0x6000)+vectorbank];
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
    int clip_y=0;
	int x, y, z=0, b, l, d, a;
	int deltax, deltay=0;
	int tk=20;
	float mod=.90;

	sp = 0;
	statz = 0;
	color = 0;
	scale = 0;          
	
    
     firstwd = memrdwd (pc);
	 pc++;pc++;
	 secondwd = memrdwd (pc);
     
	 if ((firstwd == 0) && (secondwd == 0))
	 {return;}//write_to_log("VGO with zeroed vector memory at %x\n",pc);return;}


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

DRAWCODE:			if (z == 2)
						z = statz;
					if (z)
						z = (z << 4) | 0x1f;
				
			myx=x*(scale/2);///2
			myy=y*(scale/2);///2
			
			deltax = x * scale;
			if (xflip) deltax = -deltax;
			deltay = y * scale;
			
			 if (z>-1)
			 {      
				    //gc= (z - 1)+32;
				   // if (gc>255) gc=255;
					//gc=z*1.05;
				    gc=(z+statz)*1.1;
					if (gc>255) gc=255;
					gc=gc/255;
			        if (gc>1.0) gc=1.0;
					
				  
				 vec_colors[0].g=0.0;vec_colors[0].b=0.0;

	        	if ((currentx==(currentx)+deltax) && (currenty==(currenty)-deltay))
					
				{add_color_point((currentx>>VEC_SHIFT),currenty>>VEC_SHIFT,255,255,255);}

	             add_color_line((currentx>>VEC_SHIFT), (currenty>>VEC_SHIFT), (currentx+deltax)>>VEC_SHIFT, (currenty-deltay)>>VEC_SHIFT,255,255,255);
				 add_color_point((currentx>>VEC_SHIFT),currenty>>VEC_SHIFT,  255,255,255);
				 add_color_point((currentx+deltax)>>VEC_SHIFT,(currenty-deltay)>>VEC_SHIFT,255,255,255);
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
					write_to_log("Vector Bank Switch %x",0x18000 + ((firstwd >> 8) & 3) * 0x2000);
					break;

			case SCAL:
				b = ((firstwd >> 8) & 0x07)+8;
				l = (~firstwd) & 0xff;
				scale = (l << VEC_SHIFT) >> b;		
			    scale=scale*2; //Double the scale for 1024x768 resolution
				break;

			case CNTR:
				d = firstwd & 0xff;
				currentx = 512 << VEC_SHIFT;  
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
				{
					stack [sp] = pc;
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
//////////////////////////////////////////////////////////////////////////
UINT8 MHDSW1 (UINT32 address, struct MemoryReadByte *psMemRead)
{return z80dip1;
}
UINT8 MHDSW2 (UINT32 address, struct MemoryReadByte *psMemRead)
{return z80dip2;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
MEM_READ(AlphaRead)
//MEM_ADDR(0x0000, 0x01ff, MRA_RAM)		/* 0.5K Program Ram */
MEM_ADDR( 0x0200, 0x07ff, MRA_BANK1_R)			/* 3K Paged Program RAM	*/
//MEM_ADDR( 0x0800, 0x09ff, MRA_RAM)			/* 0.5K Program RAM */
 MEM_ADDR( 0x1000, 0x1000, mhavoc_gamma_r)		/* Gamma Read Port */
 MEM_ADDR( 0x1200, 0x1200, mhavoc_port_0_r )	/* Alpha Input Port 0 */
//MEM_ADDR(0x1800, 0x1FFF, MRA_RAM)				/* Shared Beta Ram */
 MEM_ADDR(0x2000, 0x3fff, Alpha_ReadBank )			/* Paged Program ROM (32K) */
//MEM_ADDR(0x4000, 0x4fff, MRA_RAM ) /* Vector RAM	(4K) */
// MEM_ADDR(0x5000, 0x5fff, VectorRom )			/* Vector ROM (4K) */
MEM_ADDR(0x6000, 0x7fff, VectorRom )			/* Paged Vector ROM (32K) */
//MEM_ADDR( 0x8000, 0xffff, MRA_ROM )			/* Program ROM (32K) */
MEM_END


/* Main Board Writemem */
MEM_WRITE(AlphaWrite)
//MEM_ADDR(0x0000, 0x01ff, MWA_RAM )			/* 0.5K Program Ram */
MEM_ADDR( 0x0200, 0x07ff, MWA_BANK1_W )			/* 3K Paged Program RAM */
//MEM_ADDR(0x0800, 0x09ff, MWA_RAM )			/* 0.5K Program RAM */
MEM_ADDR( 0x1200, 0x1200, NoWrite )			/* don't care */
MEM_ADDR( 0x1400, 0x141f, mhavoc_colorram_w )	/* ColorRAM */
MEM_ADDR( 0x1600, 0x1600, mhavoc_out_0_w )		/* Control Signals */
MEM_ADDR( 0x1640, 0x1640, avg_go )			/* Vector Generator GO */
MEM_ADDR( 0x1680, 0x1680, NoWrite )			/* Watchdog Clear */
 MEM_ADDR( 0x16c0, 0x16c0, avgdvg_reset_w )		/* Vector Generator Reset */
MEM_ADDR(0x1700, 0x1700, mhavoc_alpha_irq_ack_w)        /* IRQ ack */
MEM_ADDR( 0x1740, 0x1740, mhavoc_rom_banksel_w )      /* Program ROM Page Select */
MEM_ADDR(0x1780, 0x1780,  mhavoc_ram_banksel_w )      /* Program RAM Page Select */
 MEM_ADDR( 0x17c0, 0x17c0, mhavoc_gamma_w )		/* Gamma Communication Write Port */
//MEM_ADDR( 0x1800, 0x1fff, MWA_RAM )			/* Shared Beta Ram */
MEM_ADDR( 0x2000, 0x3fff, NoWrite )			/* Major Havoc writes here.*/
//MEM_ADDR( 0x4000, 0x4fff, MWA_RAM, &vectorram, &vectorram_size )/* Vector Generator RAM	*/
MEM_ADDR( 0x6000, 0xffff, NoWrite )
MEM_END


MEM_READ(GammaRead)
 //MEM_ADDR(0x0000, 0x07ff, MRA_RAM)			/* Program RAM (2K)	*/
 MEM_ADDR(0x0800, 0x1fff, mhavoc_gammaram_r)	/* wraps to 0x000-0x7ff */
 MEM_ADDR(0x2000, 0x203f, mh_quad_pokey_r)		    /* Quad Pokey read	*/
 MEM_ADDR(0x2800, 0x2800, mhavoc_port_1_r)	    /* Gamma Input Port	*/
 MEM_ADDR(0x3000, 0x3000, mhavoc_alpha_r)		/* Alpha Comm. Read Port*/
 MEM_ADDR(0x3800, 0x3803, input_port_2_r)		/* Roller Controller Input*/
 MEM_ADDR(0x4000, 0x4000, input_port_4_r)		/* DSW at 8S */
 //MEM_ADDR(0x6000, 0x61ff, MRA_RAM )			/* EEROM		*/
 //MEM_ADDR(0x8000, 0xffff, MRA_ROM )			/* Program ROM (16K)	*/
MEM_END

MEM_WRITE(GammaWrite)
//MEM_ADDR(0x0000, 0x07ff, MWA_RAM )			/* Program RAM (2K)	*/
MEM_ADDR(0x0800, 0x1fff, mhavoc_gammaram_w)	    /* wraps to 0x000-0x7ff */

MEM_ADDR(0x2000, 0x203f, mh_quad_pokey_w)		    /* Quad Pokey write	*/
MEM_ADDR(0x4000, 0x4000,mhavoc_gamma_irq_ack_w)	/* IRQ Acknowledge	*/
MEM_ADDR(0x4800, 0x4800, mhavoc_out_1_w)		/* Coin Counters 	*/
MEM_ADDR(0x5000, 0x5000, mhavoc_alpha_w)		/* Alpha Comm. Write Port */
//MEM_ADDR(0x6000, 0x61ff, MWA_RAM, &nvram, &nvram_size)	/* EEROM		*/
 MEM_ADDR(0x08000, 0xffff, NoWrite)
MEM_END




void run_mhavoc()
{   
	int x;
	int cycles1 = (2500000 /gamefps)/8;
	int cycles2 = (1512000 /gamefps)/6;
	
	UINT32 m6502NmiTicks = 0;
	UINT32 dwElapsedTicks = 0;
	UINT32 dwResult = 0;
	dwElapsedTicks = m6502zpGetElapsedTicks(0xff);
    
    
		
        write_to_log("------------FRAME START --------------"); 
		//total_length=0;vecwrite=0;hit=0; //reset vector timer
		//ticks=m6502zpGetElapsedTicks(1); //Clear tick counter.
		
		m6502zpGetElapsedTicks(1);
		for (x=0; x < 8; x++)
		{
			
				m6502zpSetContext(&cpu1);	// Set CPU #1's information
				
				if (x==6 && MHAVGDONE)  {MHAVGDONE=0;}

				dwResult = m6502zpexec(cycles1);		// 1500 Instructions for 1.5MHZ CPU
				if (0x80000000 != dwResult)
					{
						m6502zpGetContext(&cpu1);
						allegro_message("Invalid instruction at %.2x on CPU 0", cpu1.m6502pc);
					}
				write_to_log("Cycles ran this int %d",m6502zpGetElapsedTicks(0));
		
				if (noint==0) {m6502zpint(0);m6502zpexec(25);} else {noint=0;}

				m6502zpGetContext(&cpu1);	// Get CPU #1's state info
        ////////////////////////////////////////////////////////////////////////////////////////////////
				m6502zpSetContext(&cpu2);	// Set CPU #1's information
				
			    if (nmicpu1)  {m6502zpnmi(); nmicpu1=0;write_to_log("NMI Taken, Gamma CPU");}
			   // if (reset1) {m6502zpreset(); reset1=0;write_to_log("Reset, Gamma CPU");}
				
				dwResult =m6502zpexec(cycles2);		// 1500 Instructions for 1.5MHZ CPU
				if (0x80000000 != dwResult)
					{
						m6502zpGetContext(&cpu2);
						allegro_message("Invalid instruction at %.2x on CPU 2", cpu2.m6502pc);
					}
				
				m6502zpint(0);
			
		
            
			

				m6502zpGetContext(&cpu2);	// Get CPU #1's state info
		     
		}
        mhavoc_sh_update();
		MH_generate_vector_list();
	
}

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_mhavoc(void)
{
  
 
   
   
   mhinit6502(AlphaRead,AlphaWrite,0);
   mhinit6502(GammaRead,GammaWrite,1);
    write_to_log("CPU init complete");
    /*
	gameImage[0][0x1200]=0xf0;  
    
	 
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
   
    write_to_log("Game Start");
	mhavoc_sh_start();
    set_mh_colors();
	cache_clear();
	
	return 0;
}

void end_mhavoc()
{
 mhavoc_sh_stop();
}

//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////


