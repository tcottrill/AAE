/* Black Widow Hardware */
#include "spacduel.h"
#include "samples.h"
#include "vector.h"
#include "input.h"
#include "cpuintfaae.h"
#include "earom.h"
#include "sndhrdw/pokyintf.h"

static int ticks=0;
static int ticks2=0;
static int toggle=0;
static int vecwrite=0;
static int cycles=0;

#define TIME_IN_HZ(hz)        (1.0 / (double)(hz))
//#define TIME_IN_CYCLES(c,cpu) ((double)(c) * cycles_to_sec[cpu])
#define TIME_IN_SEC(s)        ((double)(s))
#define TIME_IN_MSEC(ms)      ((double)(ms) * (1.0 / 1000.0))
#define TIME_IN_USEC(us)      ((double)(us) * (1.0 / 1000000.0))
#define TIME_IN_NSEC(us)      ((double)(us) * (1.0 / 1000000000.0))

static struct POKEYinterface pokey_interface =
{
	2,			/* 2 chips */
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
	{ DSRead_1, DSRead_2 },
};


static void set_sd_colors(void)
{   
	int i,c=0;
	int *cmap;
	
	int colormapsd[]={  0,255,255, //should be zero
		                0,0,255,
						0,255,0,
						0,255,255, //???
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
    //int gc =15;  //gamma correction factor
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
	//int tk=20;
	//int mod=0;

    
	int red,green,blue;

	sp = 0;
	statz = 0;
	color = 0;
	scale = 0;          
	

     firstwd = memrdwd (pc);
	 pc++;pc++;
	 secondwd = memrdwd (pc);
     	
	 if ((firstwd == 0) && (secondwd == 0))
	 {write_to_log("VGO with zeroed vector memory at %x\n",pc);return;}


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
			case VCTR:x=twos_comp_val(secondwd, 13);y=twos_comp_val(firstwd, 13);z = (secondwd >> 12) & 0x0e;
			          goto DRAWCODE;break;    
			case SVEC:x=twos_comp_val(firstwd, 5)<<1;y=twos_comp_val(firstwd >> 8,5)<<1;z = ((firstwd >> 4) & 0x0e);

DRAWCODE:		  
				    if (z ==2){z =statz; }
				  	if (z) {z = (z << 4) | 0x1f;}
				    myx=x*(scale/2); myy=y*(scale/2);//For Scaling adjust
			        deltax = x * scale;deltay = y * scale;
					if (xflip) deltax = -deltax;
			        total_length+=vector_timer(myx, myy); 
			  	 		
					if (vec_colors[color].r) red=z; else red=0;
					if (vec_colors[color].g) green=z; else green=0;
					if (vec_colors[color].b) blue=z; else blue=0;
					
					if (toggle) {
							if (z && color==0)
							          {
                                       green=z; blue=z;
									  //vec_colors[0].g=z;vec_colors[0].b=z; 
									  write_to_log("color is %x z is %x",color,z);
							      }
					             } //NEEDED TO FIX GRAVITAR TEST MODE
					else {
						   vec_colors[0].g=0;vec_colors[0].b=0;
					     }
                   
				 
              
                if ((currentx==(currentx)+deltax) && (currenty==(currenty)-deltay))
				 {add_color_point((currentx>>VEC_SHIFT),currenty>>VEC_SHIFT, red,green,blue);}

	              add_color_line((currentx>>VEC_SHIFT), (currenty>>VEC_SHIFT), (currentx+deltax)>>VEC_SHIFT, (currenty-deltay)>>VEC_SHIFT, red,green,blue);
				  add_color_point((currentx>>VEC_SHIFT),currenty>>VEC_SHIFT,  red,green,blue);
				  add_color_point((currentx+deltax)>>VEC_SHIFT,(currenty-deltay)>>VEC_SHIFT,  red,green,blue);

			  
				currentx += deltax;currenty -= deltay;
				break;

			case STAT: statz = (firstwd >> 4) & 0x000f;  
				       color = (firstwd) & 0x000f;
					   break;
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



WRITE_HANDLER(AVGgo)
{
	if (vecwrite==0) {return;}
	else { 
 			BW_generate_vector_list();
			if (total_length ) {
								 vecwrite=0;
								 ticks2 = get_video_ticks(0xff);//m6502zpGetElapsedTicks2(0xff);//0xff
								
								} else {vecwrite=1;}
	}
}

READ_HANDLER(IN0read)
{
    int val=0x7f;
    //static int flip=0;
	//int newticks=0;
    float me;
	
		me = (((1775 * total_length)/ 1000000) * 1500); //1775 // ((TIME_IN_NSEC(1512) * total_length)) * 1512000;
	   // ticks2 = get_video_ticks(0);//m6502zpGetElapsedTicks2(0);
		//write_to_log("Total LENGTH HERE %f and TOTAL TICKS %d",me,ticks2 );
	 
	if ( get_video_ticks(0) > me && vecwrite==0 ) {vecwrite=1;total_length=0;}
	
	//ticks=get_eterna_ticks(0);//m6502zpGetElapsedTicks(0);
	//val = val | ((get_eterna_ticks(0) >> 1) & 0x80); //3Khz clock
	//if (get_eterna_ticks(0) & 0x100)    {bitset(val, 0x80);} 
	if ( get_hertz_counter())     {bitset(val, 0x80);} 
	if (vecwrite==0)              {bitclr(val, 0x40);} 
	if (key[config.ktestadv])     {bitclr(val, 0x20);}
	if (toggle)                   {bitclr(val, 0x10);}
	if (key[config.kcoin2])       {bitclr(val, 0x02);}
	if (key[config.kcoin1])       {bitclr(val, 0x01);}
  	if (KeyCheck(config.ktest))   {toggle^=1;}
	if (KeyCheck(config.kreset))  {cpu_needs_reset(0);}//m6502zpreset();}
	
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
	   
		pokey_sh_update();
 }

void run_spacduel()
{   
	write_to_log("frame end");
	pokey_sh_update();
	
}



MEM_READ(BwidowRead)
 MEM_ADDR(0x7800, 0x7800, IN0read)
 //MEM_ADDR(0x6008, 0x6008, DSW1)
 //MEM_ADDR(0x6808, 0x6808, DSW2)
 MEM_ADDR(0x600a, 0x600f, pokey_1_r)
 MEM_ADDR(0x680a, 0x680f, pokey_2_r)
 MEM_ADDR(0x7000, 0x7000, EaromRead)
 MEM_ADDR(0x8000, 0x8000, BWControls)
 MEM_ADDR(0x8800, 0x8800, BWControls2)
MEM_END

MEM_WRITE(BwidowWrite)
 MEM_ADDR(0x2800, 0x5fff, NoWrite)
 MEM_ADDR(0x6000, 0x67ff, pokey_1_w)
 MEM_ADDR(0x6800, 0x6fff, pokey_2_w)
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
 //MEM_ADDR(0x6008, 0x6008, DSW1)
 //MEM_ADDR(0x6808, 0x6808, DSW2)
 MEM_ADDR(0x6000, 0x600f, pokey_1_r)
 MEM_ADDR(0x6800, 0x680f, pokey_2_r)
 MEM_ADDR(0x7000, 0x7000, EaromRead)
 MEM_ADDR(0x8000, 0x8000, GravControls)
 MEM_ADDR(0x8800, 0x8800, GravControls2)
MEM_END

MEM_WRITE(GravWrite)
 MEM_ADDR(0x2800, 0x5fff, NoWrite)
 MEM_ADDR(0x6000, 0x67ff, pokey_1_w)
 MEM_ADDR(0x6800, 0x6fff, pokey_2_w)
 MEM_ADDR(0x8800, 0x8800, led_write)
 MEM_ADDR(0x8840, 0x8840, AVGgo)
 MEM_ADDR(0x8900, 0x8900, EaromCtrl)
 MEM_ADDR(0x8940, 0x897f, EaromWrite)
 MEM_ADDR(0x8980, 0x89ed, NoWrite)
 MEM_ADDR(0x9000, 0xffff, NoWrite)
MEM_END

MEM_READ(SpaceDuelRead)
 MEM_ADDR(0x800, 0x800,   IN0read)
 MEM_ADDR(0x1000, 0x100f, pokey_1_r)
 MEM_ADDR(0x1400, 0x140f, pokey_2_r)
 //MEM_ADDR(0x1008, 0x1008, DSW1)
 //MEM_ADDR(0x1408, 0x1408, DSW2)
 MEM_ADDR(0x0a00, 0x0a00, EaromRead)
 MEM_ADDR(0x0900, 0x0907, SDControls)
MEM_END

MEM_WRITE(SpaceDuelWrite)
 MEM_ADDR(0x1000, 0x100f, pokey_1_w)
 MEM_ADDR(0x1400, 0x140f, pokey_2_w)
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
   
   switch(gamenum)
    {                                                                                                              
	    case SPACDUEL: init6502Z(SpaceDuelRead,SpaceDuelWrite,0);break;
        case GRAVITAR: init6502Z(GravRead,GravWrite,0);break;
        case GRAVP:    init6502Z(GravRead,GravWrite,0);break;
        case BWIDOW:   init6502Z(BwidowRead,BwidowWrite,0);break;
	    case LUNARBAT: init6502Z(GravRead,GravWrite,0);break;
	    case LUNARBA1: init6502Z(GravRead,GravWrite,0);break;  
      }
   
	set_sd_colors();
	cache_clear();
	pokey_sh_start (&pokey_interface);
	LoadEarom();
    total_length=0;vecwrite=0;

	return 0;   
	   
}
void end_spacduel()
{
	//Close and tidy up
	pokey_sh_stop();
	SaveEarom();
}
//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////


