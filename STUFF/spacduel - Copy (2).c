/* Black Widow Hardware */
#include "spacduel.h"
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
#include "sndhrdw/pokyintf.h"
#include "rand.h"

int ticks=0;
int toggle=0;
int vecwrite=0;
int cycles=0;

//extern uint8 rng[MAXPOKEYS];
//static uint8 pokey_random[MAXPOKEYS];     /* The random number for each pokey */

static struct POKEYinterface pokey_interface =
{
	2,			/* 4 chips */
	4,			/* updates per video frame */
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
static struct POKEYinterface pokey_interfaceSD =
{
	2,			/* # of chips */
	5,			/* updates per video frame */
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


void spacduel_int()
{
  static int count=0;
  count ++;
  if (count==31) {m6502zpint(1); count=0;} 

}


static void set_sd_colors(void)
{   
	int i,c=0;
	int *cmap;
	
	int colormapsd[]={  0,0,0,
		                0,0,255,
						0,255,0,
						0,255,255,
						255,0,0,
						255,0,255,
						255,255,0,
						255,255,255,
						255,0,0,
						255,255,255,
						0,0,255,
						0,255,0,
						255,0,0,
						255,0,0,
						255,0,255,
						255,255,255};
						

	cmap=colormapsd;
		
	for (i=0;i<16;i++)
	{
	   vec_colors[i].r=cmap[c];
       vec_colors[i].g=cmap[c+1];
       vec_colors[i].b=cmap[c+2];
	   c+=3;
	}
}
static void BW_generate_vector_list (void)
{

	int pc=0x2000;
	int sp;
	int stack [8];
    int flipword = 0;
	int scale=0;
	int statz   = 0;
	int sparkle = 0;
	int xflip   = 0;
    int gc =15;  //gamma correction factor
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
	int mod=0;
    
	int red,green,blue;

	sp = 0;
	statz = 0;
	color = 0;
	scale = 0;          
	

     firstwd = memrdwd (pc);
	 pc++;pc++;
	 secondwd = memrdwd (pc);
     	
	 if ((firstwd == 0) && (secondwd == 0))
	 {write_to_log("VGO with zeroed vector memory at %x\n",pc);return;}//write_to_log("VGO with zeroed vector memory at %x\n",pc); }


     pc=0x2000;

  	 cache_clear();
	while (!done)
	{
	   firstwd = memrdwd (pc);opcode = firstwd >> 13;pc++;pc++;	
	
	   if (opcode == VCTR) //Get the second word if it's a draw command
		{secondwd = memrdwd (pc);pc++;pc++;}
	
	   if ((opcode == STAT) && ((firstwd & 0x1000) != 0))opcode = SCAL;
	   switch (opcode)
		{
			case VCTR:x=twos_comp_val(secondwd, 13);y=twos_comp_val(firstwd, 13);z = (secondwd >> 12) & ~0x01;
			          goto DRAWCODE;break;    
			case SVEC:x=twos_comp_val(firstwd, 5)<<1;y=twos_comp_val(firstwd >> 8,5)<<1;z = ((firstwd >> 4) & 0x0e);

DRAWCODE:			if (z == 2){z = statz;}if (z) {z = (z << 4) | 0x1f;}
				    myx=x*(scale/2); myy=y*(scale/2);//For Scaling adjust
			        deltax = x * scale;deltay = y * scale;
					if (xflip) deltax = -deltax;
			        total_length+=vector_timer(myx, myy); 
			  	 		
				 if (vec_colors[color].r) red=z; else red=0;
				 if (vec_colors[color].g) green=z; else green=0;
				 if (vec_colors[color].b) blue=z; else blue=0;
				
				 if (toggle) {vec_colors[0].g=223;vec_colors[0].b=223;} //NEEDED TO FIX GRAVITAR TEST MODE
				 else {vec_colors[0].g=0;vec_colors[0].b=0;}

				 
              
                if ((currentx==(currentx)+deltax) && (currenty==(currenty)-deltay))
				 {add_color_point((currentx>>VEC_SHIFT),currenty>>VEC_SHIFT, red,green,blue);}

	              add_color_line((currentx>>VEC_SHIFT), (currenty>>VEC_SHIFT), (currentx+deltax)>>VEC_SHIFT, (currenty-deltay)>>VEC_SHIFT, red,green,blue);
				  add_color_point((currentx>>VEC_SHIFT),currenty>>VEC_SHIFT,  red,green,blue);
				  add_color_point((currentx+deltax)>>VEC_SHIFT,(currenty-deltay)>>VEC_SHIFT,  red,green,blue);

			  
				currentx += deltax;currenty -= deltay;
				break;

			case STAT: statz = (firstwd >> 4) & 0x000f;color = (firstwd) & 0x000f;break;
			case SCAL:
				b = ((firstwd >> 8) & 0x07)+8;
				l = (~firstwd) & 0xff;
				scale = (l << VEC_SHIFT) >> b;		
			    scale=scale*2; //Double the scale for 1024x768 resolution
				break;

			case CNTR: d = firstwd & 0xff;currentx = 512 << VEC_SHIFT;currenty = 512 << VEC_SHIFT;break; 
			case RTSL:

				if (sp == 0)
				{	write_to_log("*** Vector generator stack underflow! ***");
					done = 1;sp = MAXSTACK - 1;}
				else {sp--; pc = stack [sp];	}
				break;

			case HALT: done = 1;break;
			case JMPL: a=0x2000+((firstwd & 0x1fff)<<1);if (a == 0x2000){done=1;}else{pc=a;}break;/* if a = 0x0000, treat as HALT */
			case JSRL: a=0x2000+((firstwd & 0x1fff)<<1) ;
				       if (a == 0x2000){done = 1;}
					   else {stack [sp] = pc;
					
					   if (sp == (MAXSTACK - 1))
					   {write_to_log ("** Stack overflow! **");done = 1;sp = 0;}
					   else{sp++;pc=a;}
			            }break;
				

			default: write_to_log("Error in AVG engine");
			}	
		}
	 		
}

READ_HANDLER(SDRand)
{
    unsigned int r=0;

	r=Read_pokey_regs (address, 1);
	// if (rng[chip]) { pokey_random[chip] = (pokey_random[chip]>>4) | (rand()&0xf0);}
			
		return  r;// rand() & 0xff;//pokey_random[chip];
}

WRITE_HANDLER(AVGgo)
{
	if (vecwrite==0) {return;}
	else { 
 	BW_generate_vector_list();
	if (total_length ) {vecwrite=0;ticks=m6502zpGetElapsedTicks(0xff);} else {vecwrite=1;}
	}
}

READ_HANDLER(IN0read)
{
    int val=0xff;
    int ticks=0;
    float me;
	
	me = (((1775 * total_length)/ 1000000) * 1512); //1775 //1512
	ticks=m6502zpGetElapsedTicks(0);
	//write_to_log("Total LENGTH HERE %f and TOTAL TICKS %d",me,ticks );
	
	if ( ticks > me && vecwrite==0 ) {vecwrite=1;total_length=0;}
	
	//val = val | ((ticks >> 1) & 0x80); //3Khz clock
	if (ticks  & 0x100)           {bitclr(val, 0x80);} 
	if (vecwrite==0)              {bitclr(val, 0x40);} 
	if (key[config.ktestadv])     {bitclr(val, 0x20);}
	if (toggle)                   {bitclr(val, 0x10);}
	if (key[config.kcoin2])       {bitclr(val, 0x02);}
	if (key[config.kcoin1])       {bitclr(val, 0x01);}
  	if (KeyCheck(config.ktest))   {toggle^=1;}
	if (KeyCheck(config.kreset))  {m6502zpreset();}
	
	return val;
}

READ_HANDLER(GravControls)
{
  unsigned char c=0xff;
	
  if (key[config.kp1but3])    bitclr(c, 0x01); 
  if (key[config.kp1but1])    bitclr(c, 0x02);  
  if (key[config.kp1right])   bitclr(c, 0x04); 
  if (key[config.kp1left])    bitclr(c, 0x08); 
  if (key[config.kp1but2])    bitclr(c, 0x010); 
  
  return c;
}

READ_HANDLER(GravControls2)
{
  unsigned char c=0xff;

  if (key[config.kp2but2])	bitclr(c, 0x01); 
  if (key[config.kp2but1])	bitclr(c, 0x02); 
  if (key[config.kp2right])	bitclr(c, 0x04); 
  if (key[config.kp2left])	bitclr(c, 0x08); 
  if (key[config.kstart2])	bitclr(c, 0x10); 
  if (key[config.kstart1])	bitclr(c, 0x20); 
	
  return c;
}
READ_HANDLER(BWControls)
{
  unsigned char c=0xff;
	
  if (key[config.kp1right]) bitclr(c, 0x01); 
  if (key[config.kp1left])  bitclr(c, 0x02);  
  if (key[config.kp1down])  bitclr(c, 0x04); 
  if (key[config.kp1up])    bitclr(c, 0x08); 
  
  return c;
}

READ_HANDLER(BWControls2)
{
  unsigned char c=0xff;

  if (key[config.kp2right]) bitclr(c, 0x01); 
  if (key[config.kp2left])  bitclr(c, 0x02);
  if (key[config.kp2down])  bitclr(c, 0x04); 
  if (key[config.kp2up])    bitclr(c, 0x08); 
  if (key[config.kstart1])  bitclr(c, 0x20); 
  if (key[config.kstart2])  bitclr(c, 0x40);
	
  return c;
}

READ_HANDLER(SDControls)
{
  int c=0x00;

  switch (address & 0x07)
  {
	case 0: if (key[config.kp1but3])bitset(c,0x80);if (key[config.kp1but1]) bitset(c,0x40);break;
	case 1: if (key[config.kp2but3])bitset(c,0x80);if (key[config.kp2but1]) bitset(c,0x40);break;
	case 2: if (key[config.kp1left])bitset(c,0x80);if (key[config.kp1right])bitset(c,0x40);break;
	case 3: if (key[config.kp2left])bitset(c,0x80);if (key[config.kp2right])bitset(c,0x40);break; 
	case 4: if (key[config.kp1but2])bitset(c,0x80);if (key[config.kstart1]) bitset(c,0x40);break;
	case 5: if (key[config.kp2but2])bitset(c,0x80);break;
	case 6: if (key[config.kstart2])bitset(c,0x80);break;
	case 7: if (config.cocktail){bitset(c,0x80);}
			if (config.hack){bitset(c,0x40);} 
			break;
   }
  return c;
}

READ_HANDLER(DSW1)
{return z80dip1;}

READ_HANDLER(DSW2)
{return z80dip2;}

WRITE_HANDLER(avgdvg_reset_w)
{ write_to_log("AVG RESET");}

WRITE_HANDLER(intack)
{ gameImage[0][address]=data;}

WRITE_HANDLER(led_write)
{
  	set_aae_leds(~data & 0x10,~data & 0x20,0);
 }

void run_bwidow()
{
		UINT32 dwElapsedTicks = 0;
		UINT32 dwResult = 0;
		static int flip=0;
        int x;
		int passes=126;
		int cycles=1512000 / 60 / passes;

		ticks=m6502zpGetElapsedTicks(0); //Don't clear tick counter.
		//RUN CPU
		for (x=0; x < (passes+1); x++)
		{
			dwResult = m6502zpexec(cycles); 
			
			if (0x80000000 != dwResult)
			{	
				m6502zpGetContext(psCpu1);allegro_message("Invalid instruction at %.2x\n", psCpu1->m6502pc);
				exit(1);
			}
			spacduel_int();

		//m6502zpint(1);
		//do_sound();
		if (x==25)   {do_sound();}
		if (x==50)   {do_sound();}
		if (x==76)   {do_sound();}
		if (x==102)  {do_sound();}
		}
		do_sound();
 }

void run_spacduel()
{   
	    UINT32 dwElapsedTicks = 0;
        UINT32 dwResult = 0;
 	    static int flip=0;
		UINT8 x; //There is a reason for this being an unsigned short.
		int passes=168;
		int cycles=1512000 / 45 /passes;
		int cps=5;
	   	
        //write_to_log("------------------START FRAME ---------------------");
		ticks=m6502zpGetElapsedTicks(0); //Dont clear tick counter.

        //flip^=1;
		//if (flip) cps=5; else cps=6;
		//RUN CPU
		for (x=0; x < (passes+1); x++)
		{
		dwResult = m6502zpexec( cycles ); //cycles
		//write_to_log("actualcycles ran %d",m6502zpGetElapsedTicks(0));
		if (0x80000000 != dwResult)
		{	
			m6502zpGetContext(psCpu1);allegro_message("Invalid instruction at %.2x\n", psCpu1->m6502pc);
			exit(1);
		}
		//m6502zpint(1);
		spacduel_int();
		//if (x==passes/2)  {do_sound();}
		//if (x==passes/2)  {do_sound();}
		if (x==42)   {do_sound();}
		if (x==84)   {do_sound();}
		if (x==126)  {do_sound();}
		//if (x==134)  {do_sound();}
		}
		do_sound();
}



READ_HANDLER (SDPokey1Read)
{

static int rng=0;

	if (address==0x100a)
	{	if ((gameImage[0][0x100f] & 0x02) != 0x00)
				{
					rng =  randintm(0xff);
					gameImage[0][address]=rng;
					return rng;
				}
	else return rng ^ 0xff;
	}
 
 return 0;
}
READ_HANDLER (SDPokey2Read)
{
    int val=0;
	static int rng=0;
   
	if (address==0x140a)
	{
	 if ((gameImage[0][0x140f] & 0x02) != 0x00)
		{
			rng = randintm(0xff);
		    gameImage[0][address]=rng;
			return rng;
		}
	 else return rng ^ 0xff;
	}
	return 0;
}

void Pokey1SDWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)
{chip=0;Update_pokey_sound( address,data,chip,gain);if (address==0x100f) gameImage[0][address] = data;} //Dont overwrite dips

void Pokey2SDWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)
{chip=1;Update_pokey_sound( address,data,chip,gain); if (address==0x140f) gameImage[0][address] = data;} //Dont overwrite dips



READ_HANDLER (GVPokey1Read)
{

static int rng=0;

	if (address==0x600a)
	{	if ((gameImage[0][0x600f] & 0x02) != 0x00)
				{
					rng =  randintm(0xff);
					//gameImage[0][address]=rng;
					return rng;
				}
	else return rng ^ 0xff;
	}
 
 return 0;
}
READ_HANDLER (GVPokey2Read)
{
    int val=0;
	static int rng=0;
   
	if (address==0x680a)
	{
	 if ((gameImage[0][0x680f] & 0x02) != 0x00)
		{
			rng = randintm(0xff);
		    //gameImage[0][address]=rng;
			return rng;
		}
	 else return rng ^ 0xff;
	}
	return 0;
}

void Pokey1GVWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)
{chip=0;Update_pokey_sound( address,data,chip,gain); if (address&0x000f) gameImage[0][address] = data;} //Dont overwrite dips

void Pokey2GVWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)
{chip=1;Update_pokey_sound( address,data,chip,gain); if (address&0x000f) gameImage[0][address] = data;} //Dont overwrite dips


MEM_READ(BwidowRead)
 MEM_ADDR(0x7800, 0x7800, IN0read)
 MEM_ADDR(0x6008, 0x6008, DSW1)
 MEM_ADDR(0x6808, 0x6808, DSW2)
 MEM_ADDR(0x600a, 0x600a, GVPokey1Read)
 MEM_ADDR(0x680a, 0x680a, GVPokey2Read)
 MEM_ADDR(0x7000, 0x7000, EaromRead)
 MEM_ADDR(0x8000, 0x8000, BWControls)
 MEM_ADDR(0x8800, 0x8800, BWControls2)
MEM_END

MEM_WRITE(BwidowWrite)
 MEM_ADDR(0x2800, 0x5fff, NoWrite)
 MEM_ADDR(0x6000, 0x67ff, Pokey1GVWrite)
 MEM_ADDR(0x6800, 0x6fff, Pokey2GVWrite)
 MEM_ADDR(0x8800, 0x8800, led_write)
 MEM_ADDR(0x8840, 0x8840, AVGgo)
 MEM_ADDR(0x88c0, 0x88c0, intack)
 MEM_ADDR(0x8900, 0x8900, EaromCtrl)
 MEM_ADDR(0x8940, 0x897f, EaromWrite)
 MEM_ADDR(0x8980, 0x89ed, NoWrite)
 MEM_ADDR(0x9000, 0xffff, NoWrite)
MEM_END

MEM_READ(GravRead)
 MEM_ADDR(0x7800, 0x7800, IN0read)
 MEM_ADDR(0x6008, 0x6008, DSW1)
 MEM_ADDR(0x6808, 0x6808, DSW2)
 MEM_ADDR(0x600a, 0x600a, GVPokey1Read)
 MEM_ADDR(0x680a, 0x680a, GVPokey2Read)
 MEM_ADDR(0x7000, 0x7000, EaromRead)
 MEM_ADDR(0x8000, 0x8000, GravControls)
 MEM_ADDR(0x8800, 0x8800, GravControls2)
MEM_END

MEM_WRITE(GravWrite)
 MEM_ADDR(0x2800, 0x5fff, NoWrite)
 MEM_ADDR(0x6000, 0x67ff, Pokey1GVWrite)
 MEM_ADDR(0x6800, 0x6fff, Pokey2GVWrite)
 MEM_ADDR(0x8800, 0x8800, led_write)
 MEM_ADDR(0x8840, 0x8840, AVGgo)
 MEM_ADDR(0x8900, 0x8900, EaromCtrl)
 MEM_ADDR(0x8940, 0x897f, EaromWrite)
 MEM_ADDR(0x8980, 0x89ed, NoWrite)
 MEM_ADDR(0x9000, 0xffff, NoWrite)
MEM_END

MEM_READ(SpaceDuelRead)
 MEM_ADDR(0x800, 0x800,   IN0read)
 MEM_ADDR(0x100a, 0x100a, SDPokey1Read)
 MEM_ADDR(0x140a, 0x140a, SDPokey2Read)
 MEM_ADDR(0x1008, 0x1008, DSW1)
 MEM_ADDR(0x1408, 0x1408, DSW2)
 MEM_ADDR(0x0a00, 0x0a00, EaromRead)
 MEM_ADDR(0x0900, 0x0907, SDControls)
MEM_END

MEM_WRITE(SpaceDuelWrite)
 MEM_ADDR(0x1000, 0x100f, Pokey1SDWrite)
 MEM_ADDR(0x1400, 0x140f, Pokey2SDWrite)
 MEM_ADDR(0x0c80, 0x0c80, AVGgo)
 MEM_ADDR(0x0c00, 0x0c00, led_write)
 MEM_ADDR(0x0d00, 0x0d00, NoWrite)
 MEM_ADDR(0x0d80, 0x0d80, avgdvg_reset_w)
 MEM_ADDR(0x0e00, 0x0e00, NoWrite)
 MEM_ADDR(0x0f00, 0x0f3f, EaromWrite)
 MEM_ADDR(0x0e80, 0x0e80, EaromCtrl)
 MEM_ADDR(0x2800, 0x8fff, NoWrite)
 MEM_ADDR(0xf000, 0xffff, NoWrite)
 MEM_ADDR(0x0905, 0x0906, NoWrite)
MEM_END



/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_spacduel()
{
   	UINT32 m6502NmiTicks = 0;
	UINT32 dwElapsedTicks = 0;
    cycles = (1512000 /driver[gamenum].fps)/4;
	
   
   switch(gamenum){                                                                                                              
	    case SPACDUEL: init6502(SpaceDuelRead,SpaceDuelWrite,0);break;
        case GRAVITAR: init6502(GravRead,GravWrite,0);break;
        case GRAVP: init6502(GravRead,GravWrite,0);break;
        case BWIDOW: init6502(BwidowRead,BwidowWrite,0);break;
	    case LUNARBAT: init6502(GravRead,GravWrite,0);break;
	    case LUNARBA1: init6502(GravRead,GravWrite,0);break;  
      }
 
        BUFFER_SIZE = 44100 / driver[gamenum].fps / 5;

	if (gamenum==SPACDUEL) BUFFER_SIZE = 44100 / driver[gamenum].fps / 4; //6
	//else pokey_sh_start (&pokey_interface);

	chip=2;gain=6;
	set_sd_colors();
	cache_clear();
	
	//pokey_sh_start (&pokey_interface);
	Pokey_sound_init(1512000, 44100,chip,6 ); // INIT POKEY 1373300 1512000
	soundbuffer =  malloc(BUFFER_SIZE);
	memset(soundbuffer, 0, BUFFER_SIZE);
	stream = play_audio_stream(BUFFER_SIZE, 8, FALSE, 44100, config.pokeyvol, 128);

	m6502zpreset();
	m6502zpexec(100);
	dwElapsedTicks = m6502zpGetElapsedTicks(0xff);
    
	m6502zpreset();
	LoadEarom();


    total_length=0;vecwrite=0;

	return 0;   
	   
}
void end_spacduel()
{
	//Close and tidy up
	SaveEarom();
    free_audio_stream_buffer(stream);
	stop_audio_stream(stream);
	free (soundbuffer);
}
//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////


