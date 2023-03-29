

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
#include "rand.h"
#include "fonts.h"

static struct POKEYinterface pokey_interface =
{
	4,			/* 4 chips */
	1,			/* 1 update per video frame (low quality) */
	1250000,
	128,	/* volume */
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

static UINT8 alpha_data=0;
static UINT8 alpha_rcvd=0;
static UINT8 alpha_xmtd=0;

static UINT8 gamma_data=0;
static UINT8 gamma_rcvd=0;
static UINT8 gamma_xmtd=0;

static UINT8 player_1;
int has_gamma_cpu=1;
int intcpu2=0;
static UINT8 alpha_irq_clock;
static UINT8 alpha_irq_clock_enable;
static UINT8 gamma_irq_clock;

//static UINT8 *ram_base;
//static UINT8 has_gamma_cpu;

static int ram_bank[2] = { 0x20200, 0x20800 };
static int ram_bank_sel=0;

static int rom_bank[4] = { 0x10000, 0x12000, 0x14000, 0x16000 };
static int rom_bank_sel=0;

static int reset1=0;
static int nmicpu1=0;
static int MHAVGDONE=1;
int noint=0;
int vec_cyc=0;
int mhticks=0;

int teller=0;

int pkenable[4];
int drawnok=0;



/*************************************
 *
 *  Interrupt handling
 *
 *************************************/

void mhavoc_interrupt( )
{
		
	/* clock the LS161 driving the alpha CPU IRQ */
	if (alpha_irq_clock_enable)
	{
		alpha_irq_clock++;
		if ((alpha_irq_clock & 0x0c) == 0x0c) //0x0c
		{    write_to_log("IRQ ALPHA CPU");
		    // intcpu1=1;
			//cpunum_set_input_line(machine, 0, 0, ASSERT_LINE);
			m6502zpint(1);
	
			alpha_irq_clock_enable = 0;
		}
	}

	/* clock the LS161 driving the gamma CPU IRQ */
	if (has_gamma_cpu)
	{
		gamma_irq_clock++;
		if (gamma_irq_clock & 0x08) //08
		{
		  write_to_log("IRQ GAMMA CPU");
		  intcpu2=1;

		}
		//cpunum_set_input_line(machine, 1, 0, (gamma_irq_clock & 0x08) ? ASSERT_LINE : CLEAR_LINE);
	}
}

WRITE_HANDLER( mhavoc_alpha_irq_ack_w )
{
	/* clear the line and reset the clock */
	//cpu_set_irq_line(0, 0, CLEAR_LINE);
	//write_to_log("Alpha IRQ ACK!",data);
	alpha_irq_clock = 0;
	alpha_irq_clock_enable = 1;
}


WRITE_HANDLER( mhavoc_gamma_irq_ack_w )
{
	/* clear the line and reset the clock */
	//cpu_set_irq_line(1, 0, CLEAR_LINE);
	//write_to_log("Gamma IRQ ACK!",data);
	gamma_irq_clock = 0;
}

READ_HANDLER(mh_quad_pokey_read)
{
	static int rng=0;
	int pokey_reg[4];
	
	int offset= address-0x2000;
    int pokey_num = (offset >> 3) & ~0x04;
    int control = (offset & 0x20) >> 2;
    pokey_reg[pokey_num] = (offset % 8) | control;
	//write_to_log("Pokey Read # %x  address %x",pokey_num,pokey_reg);
	 if (pokey_reg[pokey_num]==0x0a)
	            {	if (pkenable[pokey_num] != 0x00)
					{
						rng =  randintm(0xff);
						return rng;
					}
	                 else return rng ^ 0xff;
	             }
 
 return 0;
}

WRITE_HANDLER(mh_quad_pokey_write )
{
	int pokey_reg[4];
	int offset= address-0x2000;
    int pokey_num = (offset >> 3) & ~0x04;
    int control = (offset & 0x20) >> 2;
    pokey_reg[pokey_num] = (offset % 8) | control;
	if  (pokey_reg[pokey_num]==0x0f) {pkenable[pokey_num]=data;}
    switch (pokey_num) {
        case 0:
			Update_pokey_sound( pokey_reg[0],data,0,6);
           // pokey1_w (pokey_reg[0], data);//write_to_log("pokey write 0 %x",pokey_reg);
            break;
        case 1:
			Update_pokey_sound( pokey_reg[1],data,1,6);
            //pokey2_w (pokey_reg[1], data);//write_to_log("pokey write 1 %x",pokey_reg);
            break;
        case 2:
			Update_pokey_sound( pokey_reg[2],data,2,6);
            //pokey3_w (pokey_reg[2], data);//write_to_log("pokey write 2 %x",pokey_reg);
            break;
        case 3:
			Update_pokey_sound( pokey_reg[3],data,3,6);
            //pokey4_w (pokey_reg[3], data);//write_to_log("pokey write 3 %x",pokey_reg);
            break;
	}
}


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
{
	
	//if (MHAVGDONE==0) {return;}
	//else { 
	//total_length=0;
 	MH_generate_vector_list();
	//write_to_log("Total Frame list length %d" ,total_length );
	if (total_length > 1) {MHAVGDONE=0;mhticks=m6502zpGetElapsedTicks(0xff);write_to_log("Total LENGTH HERE %d ",total_length );} else {MHAVGDONE=1;}
//	}
	write_to_log("AVG RUN CALLED!@");
	
	

}
	
WRITE_HANDLER(mhavoc_out_0_w)
{
	if (data & 0x01) {write_to_log("LED ERROR BLINK"); }// glPrint(200, 580, 255,255,255,255,1,0,0,"O");}	

if (!(data & 0x08))
	{
		//write_to_log ("\t\t\t\t*** resetting gamma processor. ***\n");
		alpha_rcvd=0;
		alpha_xmtd=0;
		gamma_rcvd=0;
		gamma_xmtd=0;
		reset1=1;   //RESET GAMMA CPU
	}
	player_1 = (data >> 5) & 1;
	/* Emulate the roller light (Blinks on fatal errors) */
	set_aae_leds(data & 0x01,0,0);
	
	
	//gameImage[0][address]=data;
}

WRITE_HANDLER(mhavoc_out_1_w )
{
	// write_to_log("LED OUT WRITE");
	set_aae_leds(0,data & 0x01,0);
	set_aae_leds(0,0,(data & 0x02)>>1);
	gameImage[0][address]=data;
}
/* Simulates frequency and vector halt */
READ_HANDLER(mhavoc_port_0_r)
{
	int res;
    int ticks;
	float me;
	static int service=0;

	me = (((1775 * total_length)/ 1000000) * 1512);//* 1500); //2500 //1775
	ticks=m6502zpGetElapsedTicks(0);
	if (MHAVGDONE==0 )write_to_log("Total LENGTH HERE %f and TOTAL TICKS %d",me,ticks );
	if ( ticks > me && MHAVGDONE==0 ) {MHAVGDONE=1;total_length=0;}
	
    // write_to_log("IN0 READ");
	
	if (player_1) res = 0xff; //bf for 2 coins to start
	else res = 0xf0;
	
	/* Emulate the 2.4Khz source on bit 2 (divide 2.5Mhz by 1024) */
	  if (ticks & 0x400) //0x400
	  {res &=~0x02;}
	  else
	  { res|=0x02;}
	//MHAVGDONE=1;
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
	
	if (player_1==0)
	{
	//PORT_BIT ( 0x03, IP_ACTIVE_HIGH, IPT_SPECIAL )
	//PORT_BIT ( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT )
	//PORT_BIT ( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT )
	//PORT_BIT ( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_UP )
	if (key[KEY_F1])   bitclr(res,0x20);    //PORT_BITX( 0x20, IP_ACTIVE_LOW, IPT_SERVICE, "Diag Step/Coin C", KEYCODE_F1, IP_JOY_NONE )
	if (key[KEY_5])    bitclr(res,0x40);//PORT_BIT ( 0x40, IP_ACTIVE_LOW, IPT_COIN1 )	/* Left Coin Switch  */
	if (key[KEY_6])    bitclr(res,0x80);//PORT_BIT ( 0x80, IP_ACTIVE_LOW, IPT_COIN2 )	/* Right Coin */
	}
	
	if (player_1){
	if (KeyCheck(KEY_F2))  service^=1; 
    if (service && player_1) bitclr(res,0x80); 
	}
   	//return res;
	return (res & 0xff);
}

READ_HANDLER(mhavoc_port_1_r)
{
	
	int res=0xff;
	
	if (alpha_rcvd==1)
		res |=0x02;
	else
		res &=~0x02;

	if (alpha_xmtd==1)
		res |=0x01;
	else
		res &=~0x01;

	if (key[KEY_LCONTROL])    bitclr(res,0x80);
	if (key[KEY_ALT])         bitclr(res,0x40);

	return (res & 0xff);
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

READ_HANDLER(input_port_4_r)
{
	int c=0xff;//85
   // write_to_log("Dip Switch Read");
   return c;
}



WRITE_HANDLER(mhavoc_ram_banksel_w)
{
	data &= 0x01;
	ram_bank_sel=data;
//write_to_log("Alpha RAM Bank select: %02x",data);
}


WRITE_HANDLER(mhavoc_rom_banksel_w)
{
	static int lastbank=0;
	//static const offs_t bank[4] = { 0x10000, 0x12000, 0x14000, 0x16000 };
	data &= 0x03;
	rom_bank_sel=data;
	if (rom_bank_sel !=  lastbank){
	if (rom_bank_sel==3) {memcpy(gameImage[0]+0x2000,gameImage[0]+0x16000,0x2000);}
	if (rom_bank_sel==2) {memcpy(gameImage[0]+0x2000,gameImage[0]+0x14000,0x2000);}
	if (rom_bank_sel==1) {memcpy(gameImage[0]+0x2000,gameImage[0]+0x12000,0x2000);}
	if (rom_bank_sel==0) {memcpy(gameImage[0]+0x2000,gameImage[0]+0x10000,0x2000);}
	lastbank = rom_bank_sel;
	}
    gameImage[0][address]=data;
	//write_to_log("Alpha ROM Bank select: %02x",data);
	
}

////////////////////////////////////////////////////////////////////////////////////////////////

void MHPlayerRamWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite)
{
	if (ram_bank_sel == 0)
		gameImage[0][(address - 0x200)+20200] = data;
	else
		gameImage[0][(address - 0x200)+20800] = data;
}

UINT8 MHPlayerRamRead(UINT32 address, struct MemoryReadByte *pMemRead)
{
	if (gameImage[0][0x1780] == 0)
		return(gameImage[0][(address - 0x200)+20200]);
	else
		return(gameImage[0][(address - 0x200)+20800]);
}

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
		//write_to_log("pokey write 0 %x",pokey_reg);
		pokey1_w(pokey_reg, data);}
	else{
		//write_to_log("pokey write 1 %x",pokey_reg);
		pokey2_w(pokey_reg, data);}
}




WRITE_HANDLER(mhavoc_colorram_w)
{
	int i = (data & 4) ? 0x0f : 0x08;
	int r = (data & 8) ? 0x00 : i;
	int g = (data & 2) ? 0x00 : i;
	int b = (data & 1) ? 0x00 : i;
    
   vec_colors[address-0x1400].r=r;
   vec_colors[address-0x1400].g=g;
   vec_colors[address-0x1400].b=b;
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
    int scalef=0; 
  
	sp = 0;
	statz = 0;
	color = 0;
	scale = 0;          
	
    
     firstwd = memrdwd (pc);
	 pc++;pc++;
	 secondwd = memrdwd (pc);
     total_length=0;
	 if ((firstwd == 0) && (secondwd == 0))
	 {write_to_log("VGO with zeroed vector memory at %x\n",pc);return;}
	 if ( firstwd == 0xafe2) {write_to_log("EMPTY FRAME");total_length=1;return;}

      pc=0x4000;

  	cache_clear();
	
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
				
				z = (secondwd >> 12) & 0x0e;

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
			
			
			deltax = x * scale;
			if (xflip) deltax = -deltax;
			deltay = y * scale;
			myx= x*scalef;///2
			myy= y*scalef;///2
			total_length+=vector_timer(myx, myy); 
			if (sparkle) {color =  16 + ((rand() >> 8) & 15);}
			// if (z>0)
			// {      
				
					if (vec_colors[color].r) red=z; else red=0;
					if (vec_colors[color].g) green=z; else green=0;
					if (vec_colors[color].b) blue=z; else blue=0;

                    ey= ((currenty-deltay)>>VEC_SHIFT);   
				    sy= currenty>>VEC_SHIFT;
					sx= (currentx>>VEC_SHIFT);
					ex= (currentx+deltax)>>VEC_SHIFT;
					clip=330; //328 //510 on title screen
                   
					if (ywindow==1)
					{
						//Hack for Title Screen
						if (gameImage[0][0x0d]==0x38 && gameImage[0][0x0e]==0xfe && gameImage[0][0x0f]==0x58){clip=512;}
						//Y-Window clipping
						if (sy < clip && ey < clip ){draw=0;}else {draw=1;}
	                  
						if (ey < clip && ey < sy ){
													ex=((clip-ey)*((ex-sx)/(ey-sy)))+ex;ey=clip;
												  }
							
						if (sy< clip && sy < ey ) {
													sx=((clip-sy)*((sx-ex)/(sy-ey)))+sx;sy=clip;
												  }
					}
                   				    
					
					//if (currentx==ex && currenty==ey){add_color_point(sx, sy,red,green,blue);}
					if (draw) {    
						        add_color_line(sx, sy,ex,ey,red,green,blue);
								add_color_point(sx, sy,red,green,blue);
								add_color_point(ex, ey,red,green,blue);
							  }
			 //}
					               
					               

				currentx += deltax;
				currenty -= deltay;
			  	total_length+=vector_timer(myx, myy);   
				break;

			case STAT:
					
                    color = firstwd & 0x0f;
					statz = (firstwd >> 4) & 0x0f;
				    sparkle = firstwd & 0x0800;
					xflip = firstwd & 0x0400;
					vectorbank = 0x18000 + ((firstwd >> 8) & 3) * 0x2000;
					
					if (lastbank!=vectorbank){lastbank=vectorbank;
					//write_to_log("Vector Bank Switch %x",0x18000 + ((firstwd >> 8) & 3) * 0x2000);
					memcpy(gameImage[0]+0x6000,gameImage[0]+vectorbank,0x2000);
					}
					break;

			case SCAL:
				b = ((firstwd >> 8) & 0x07)+8;
				l = (~firstwd) & 0xff;
				scale = (l << VEC_SHIFT) >> b;	
				scalef=scale;
				scale=scale*3; //Triple the scale for 1024x768 resolution
				
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
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////GAMMA CPU ///////////////////////////////////////////

/* Read from the gamma processor */
READ_HANDLER(mhavoc_gamma_r)
{
	//write_to_log(  "Gamma: Now reading Alpha Data: %02x", gamma_data);
	alpha_rcvd=1;
	gamma_xmtd=0;
	return gamma_data;
}

/* Read from the alpha processor */
READ_HANDLER(mhavoc_alpha_r)
{
	//write_to_log(  "Now Reading from data from Gamma Processor: %02x", alpha_data);
	gamma_rcvd=1;
	alpha_xmtd=0;
	return alpha_data;
}
/* Write to the alpha processor */
WRITE_HANDLER( mhavoc_alpha_w)
{
	//write_to_log( "Now Writing to Gamma Processor: %02x", data);
	alpha_rcvd=0;
	gamma_xmtd=1;
	gamma_data = data;
	//gameImage[0][address]=data;
	
	//noint=1;
}

/* Write to the gamma processor */
WRITE_HANDLER( mhavoc_gamma_w)
{
	//write_to_log(  "Gamma: Writing to Alpha: %02x", data);
	gamma_rcvd=0;
	alpha_xmtd=1;
	alpha_data = data;
	nmicpu1=1;
	//noint=1;
	//write_to_log("--------------------------RELEASING CPU SLICE TO GAMMA");
    teller=1;
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////

MEM_READ(AlphaRead)
//MEM_ADDR(0x0000, 0x01ff, MRA_RAM)		/* 0.5K Program Ram */
MEM_ADDR( 0x0200, 0x07ff, MRA_BANK1_R)			/* 3K Paged Program RAM	*/
//MEM_ADDR( 0x0800, 0x09ff, MRA_RAM)			/* 0.5K Program RAM */
 MEM_ADDR( 0x1000, 0x1000, mhavoc_gamma_r)		/* Gamma Read Port */
 MEM_ADDR( 0x1200, 0x1200, mhavoc_port_0_r )	/* Alpha Input Port 0 */
//MEM_ADDR(0x1800, 0x1FFF, MRA_RAM)				/* Shared Beta Ram */
// MEM_ADDR(0x2000, 0x3fff, Alpha_ReadBank )			/* Paged Program ROM (32K) */
//MEM_ADDR(0x4000, 0x4fff, MRA_RAM ) /* Vector RAM	(4K) */
// MEM_ADDR(0x5000, 0x5fff, VectorRom )			/* Vector ROM (4K) */
//MEM_ADDR(0x6000, 0x7fff, VectorRom )			/* Paged Vector ROM (32K) */
//MEM_ADDR( 0x8000, 0xffff, MRA_ROM )			/* Program ROM (32K) */
MEM_END


/* Main Board Writemem */
MEM_WRITE(AlphaWrite)
//MEM_ADDR(0x0000, 0x01ff, MWA_RAM )			 /* 0.5K Program Ram */
MEM_ADDR( 0x0200, 0x07ff, MWA_BANK1_W )			 /* 3K Paged Program RAM */
//MEM_ADDR(0x0800, 0x09ff, MWA_RAM )			 /* 0.5K Program RAM */
MEM_ADDR( 0x1200, 0x1200, NoWrite )			     /* don't care */
MEM_ADDR( 0x1400, 0x141f, mhavoc_colorram_w )	 /* ColorRAM */
MEM_ADDR( 0x1600, 0x1600, mhavoc_out_0_w )		 /* Control Signals */
MEM_ADDR( 0x1640, 0x1640, avg_go )			     /* Vector Generator GO */
MEM_ADDR( 0x1680, 0x1680, NoWrite )			     /* Watchdog Clear */
 MEM_ADDR( 0x16c0, 0x16c0, avgdvg_reset_w )		 /* Vector Generator Reset */
MEM_ADDR(0x1700, 0x1700, mhavoc_alpha_irq_ack_w) /* IRQ ack */
MEM_ADDR( 0x1740, 0x1740, mhavoc_rom_banksel_w ) /* Program ROM Page Select */
MEM_ADDR(0x1780, 0x1780,  mhavoc_ram_banksel_w ) /* Program RAM Page Select */
 MEM_ADDR( 0x17c0, 0x17c0, mhavoc_gamma_w )		 /* Gamma Communication Write Port */
//MEM_ADDR( 0x1800, 0x1fff, MWA_RAM )			 /* Shared Beta Ram (Not Used)*/
MEM_ADDR( 0x2000, 0x3fff, NoWrite )			     /* (ROM)Major Havoc writes here.*/
//MEM_ADDR( 0x4000, 0x4fff, MWA_RAM,             /* Vector Generator RAM	*/
MEM_ADDR( 0x6000, 0x7fff, NoWrite )              /* ROM */
MEM_END


MEM_READ(GammaRead)
 //MEM_ADDR(0x0000, 0x07ff, MRA_RAM)			/* Program RAM (2K)	*/
MEM_ADDR(0x0800, 0x1fff, mhavoc_gammaram_r)	    /* wraps to 0x000-0x7ff */
 MEM_ADDR(0x2000, 0x203f, mh_quad_pokey_read)	/* Quad Pokey read	*/
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

MEM_ADDR(0x2000, 0x203f, mh_quad_pokey_write)	/* Quad Pokey write	*/
MEM_ADDR(0x4000, 0x4000,mhavoc_gamma_irq_ack_w)	/* IRQ Acknowledge	*/
MEM_ADDR(0x4800, 0x4800, mhavoc_out_1_w)		/* Coin Counters 	*/
MEM_ADDR(0x5000, 0x5000, mhavoc_alpha_w)		/* Alpha Comm. Write Port */
//MEM_ADDR(0x6000, 0x61ff, MWA_RAM          	/* EEROM		*/
 MEM_ADDR(0x08000, 0xffff, NoWrite)
MEM_END

void run_mhavoc()
{   
	int x;
	int pass=0;
	int fivek=0;
	int addnum=600;//(5000/60);   //300 //666
    int cycles;
	int norun=0;
	int cycles1 = (2500000 /30);
	int cycles2 = (1250000 /30);
	int addon=0;
    int testcyc=0;

    int passes=100;

	UINT32 m6502NmiTicks = 0;
	UINT32 dwElapsedTicks = 0;
	UINT32 dwResult = 0;
	//dwElapsedTicks = m6502zpGetElapsedTicks(0xff);
    
    drawnok=0;
        write_to_log("------------FRAME START --------------"); 
		
		
		for (x=0; x < passes; x++) //160
		{
			    cycles=0;
				testcyc=m6502zpGetElapsedTicks(0);
				m6502zpSetContext(&cpu1);	// Set CPU #1's information
				
				dwResult = m6502zpexec((int) cycles1 / passes);		// 1500 Instructions for 1.5MHZ CPU
				if (addon) {dwResult = m6502zpexec(300);addon=0;}
				if (0x80000000 != dwResult)
					{
						m6502zpGetContext(&cpu1);
						allegro_message("Invalid instruction at %.2x on CPU 0", cpu1.m6502pc);
					}


				if (teller){ //I have released my timeslice.
					addon=1;
				//write_to_log("Cycles start at release %d cycles at release %d", (cycles1 / 64) *x,m6502zpGetElapsedTicks(0)-testcyc);	
                m6502zpexec(20);//Run 250us worth of cyces.
                 /*              
				m6502zpGetContext(&cpu1); //Close CPU1
				m6502zpSetContext(&cpu2); //Start CPU2
                m6502zpnmi(); write_to_log("NMI Taken, Gamma CPU"); //Take an NMI
                //do{
	               m6502zpexec(40);cycles++;
	            //  } while (gamma_xmtd == 0 && cycles < 20);
                m6502zpGetContext(&cpu2); //Close CPU2
                m6502zpSetContext(&cpu1); //Open CPU1
               // dwResult = m6502zpexec(208); //Run some extra cycles to pickup the data
				//write_to_log("Cycles ran this int %d",m6502zpGetElapsedTicks(0));
				*/
				//m6502zpnmi();
				teller=0;
				}
				//if (noint==0) {m6502zpint(1);} else {noint=0;}
              //  pass+=( (int) cycles1 / 125);
				//if (pass > fivek) {
				   mhavoc_interrupt();
				 // write_to_log("Interrupt call Pass %d 5k %d",pass,fivek);
				  // fivek+=addnum;
				//}
				m6502zpGetContext(&cpu1);	// Get CPU #1's state info
 //----------------------------------------------------------------------------------------------------------------------   		
				m6502zpSetContext(&cpu2);	// Set CPU #2's information
			    if (nmicpu1)  {m6502zpnmi(); nmicpu1=0;write_to_log("NMI Taken, Gamma CPU");}
				if (intcpu2) {m6502zpint(1);intcpu2=0;write_to_log("INT Taken, Gamma CPU");} 
			    if (reset1) {m6502zpreset(); reset1=0;write_to_log("Reset, Gamma CPU");}
				
				dwResult =m6502zpexec((int) cycles2 / passes );		// 1500 Instructions for 1.5MHZ CPU
				if (0x80000000 != dwResult)
					{
						m6502zpGetContext(&cpu2);
						allegro_message("Invalid instruction at %.2x on CPU 2", cpu2.m6502pc);
					}
			   
             	m6502zpGetContext(&cpu2);	// Get CPU #2's state info
		if (x==passes/4)  do_sound();//mhavoc_sh_update();		
		if (x==passes/3)  do_sound();	
		if (x==passes/2)  do_sound();
		if (x==passes-1)  do_sound();
		}
		
       // mhavoc_sh_update();
		//MH_generate_vector_list();
		
}

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_mhavoc(void)
{
  
    
   mhinit6502(AlphaRead,AlphaWrite,0);
   mhinit6502(GammaRead,GammaWrite,1);
    write_to_log("CPU init complete");
	total_length=1;

	alpha_irq_clock=0;
    alpha_irq_clock_enable=1;
    gamma_irq_clock=0;
	MHAVGDONE=0;
	pkenable[0]=0;
	pkenable[1]=0;
	pkenable[2]=0;
	pkenable[3]=0;
	
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
	
      
	  memcpy(gameImage[0]+0x6000,gameImage[0]+0x18000,0x2000);
    
	BUFFER_SIZE = 44100 /50 / 4;// driver[gamenum].fps
	

    Pokey_sound_init(1250000, 44100,4,6 ); // INIT POKEY 1373300 1512000
	soundbuffer =  malloc(BUFFER_SIZE);
	memset(soundbuffer, 0, BUFFER_SIZE);
	stream = play_audio_stream(BUFFER_SIZE, 8, FALSE, 44100, config.pokeyvol, 128);
	//mhavoc_sh_start();
    cache_clear();
	
	return 0;
}

void end_mhavoc()
{
	free_audio_stream_buffer(stream);
	stop_audio_stream(stream);
	free (soundbuffer);
 //mhavoc_sh_stop();
}

//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////


