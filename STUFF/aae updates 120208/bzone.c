/* Battlezone Emu */

#include "bzone.h"
#include "globals.h"
#include "samples.h"
#include "vector.h"
#include "input.h"
#include "cpuintfaae.h"
#include "pokey.h"
#include "earom.h"
#include "sndhrdw/pokyintf.h"

static int soundEnable = 1;

static int roll=RBDACCTR;	/* start centered */
static int pitch=RBDACCTR;
static int input_select; 	/* 0 is roll_data, 1 is pitch_data */

int ledstate=0;
int vector_off_bz=0;
int bzgo=0;


static struct POKEYinterface pokey_interface =
{
	1,			/* 4 chips */
	1512000,
	255,	/* volume */
	6,
	NO_CLIP,
	/* The 8 pot handlers */
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0},
	/* The allpot handler */
	{ DSRead_1 }, //Dip here
};



 void BZSaveScore(void)
{
	
	FILE *fp;
      int i;
   
      fp = fopen("hi\\bzone.aae", "w");
      if (fp == NULL) {
        write_to_log("Error - I couldn't write Battlezone High Score");
      }
    for (i= 0; i < 60; i++)
		
	{fprintf(fp, "%c", gameImage[0][(0x300+i)]);}//allegro_message("Writing %x",earom[i&0xff]);
     fclose(fp);   
}
 int BZLoadScore(void)
{

FILE *fp;
char c;
int i=0;
fp = fopen("hi\\bzone.aae","r");
  // check if the hi score table has already been initialized 
	if (memcmp(&gameImage[0][0x0300],"\x05\x00\x00",3) == 0 &&
			memcmp(&gameImage[0][0x0339],"\x22\x28\x38",3) == 0)
	{
   if (fp != NULL) 
   {
      do {
         c = getc(fp);    /* get one character from the file */
         gameImage[0][(0x300+i)]=c;         /* display it on the monitor       */
        i++;
	  } while (i< 60);    /* repeat until EOF (end of file)  */
   fclose(fp);
   }
      
   return 1;
	}
 else return 0;
}


static void set_bz_colors(void)
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
////////////////////////////VECTOR GENERATOR//////////////////////////////////


static void bz_gen_vector_list (void)
{
	int pc=0x2000;
	int sp=0;
	int stack [8];
   	int scale=0;
	int statz   = 0;
	int xflip   = 0;
    int gc =16;  //gamma correction factor
	int color= 0;
	int currentx, currenty=0;
	int done    = 0;
	int firstwd, secondwd;
	int opcode;
  	int x, y, z=0, b, l, d, a;
	int deltax, deltay=0;
		
	float sy=0;
	float ey=0;
	float sx=0;
	float ex=0;
	int nocache=0;
	float adj=0;
	int mx;
	int my;
	int ywindow=0;
	int clip=726;
	
	total_length=0;
	

	while (!done)
	{
		firstwd = memrdwd (pc);
		opcode = firstwd >> 13;
       	pc++;pc++;
		
	 if ((firstwd == 0) && (secondwd == 0))
	 {;}//write_to_log("VGO with zeroed vector memory at %x\n",pc); }
		 
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
                
			
				
DRAWCODE:			
				if (z == 2)	z = statz;
				if (z)	z = (z << 4) | 0x1f;
				deltax = x * scale;
				mx = x * (scale /2);
				if (xflip) deltax = -deltax;

				deltay = y * scale;
				my = y * (scale / 2);
				total_length+=vector_timer(mx, my); 
				deltay=-deltay;


			 if (z>-1)
			  
			 {  	
				    ey= ((currenty-deltay)>>VEC_SHIFT);   
				    sy= currenty>>VEC_SHIFT;
					sx= (currentx>>VEC_SHIFT);
					ex= (currentx+deltax)>>VEC_SHIFT;
					
					if ( !testsw && gamenum!=REDBARON ){
					//Line color 0 clipping
					if (sy> clip && ey > clip && color==0){nocache=1;}
					if (sy> clip && sy > ey && color==0){sx=((clip-sy)*((ex-sx)/(ey-sy)))+sx;sy=clip;}
					if (ey> clip && ey > sy && color==0){ex=((clip-ey)*((sx-ex)/(sy-ey)))+ex;ey=clip;}
				  
					}
                   				    
					gc=z+1;
					gc=z/16;
					if (nocache==0){
									cache_line(sx, sy,ex,ey, gc,config.gain,0);
					                cache_point(sx, sy,gc,config.gain,0,0);
					                cache_point(ex, ey,gc,config.gain,0,0);
					                }
               }
				currentx += deltax;
				currenty -= deltay;
			    nocache=0;
				break;

			case STAT:
				    color = (firstwd) & 0x000f;
					statz = (firstwd >> 4) & 0x000f;
					break;

			case SCAL:
				b = ((firstwd >> 8) & 0x07)+8;//8
				l = (~firstwd) & 0xff;
				scale = (l << VEC_SHIFT) >> b;		/* ASG 080497 */
			  					
				scale*=2;
				
				
				
				break;

			case CNTR:
				d = firstwd & 0xff;
				currentx = 512 << VEC_SHIFT;  
				currenty = 512 << VEC_SHIFT;  
				break;

			case RTSL:
				if (sp == 0)
				{done = 1;sp = MAXSTACK - 1;write_to_log("AVG Stack Under error");}
				else {sp--;pc = stack [sp];}
				break;

			case HALT:
				done = 1;
				break;

			case JMPL:
				a = 0x2000 + ((firstwd & 0x1fff) <<1) ;
				if (a == 0x2000) //HALT
				{done = 1;}
				else
				{pc = a;}
				break;

			case JSRL:
				a = 0x2000 + ((firstwd & 0x1fff) <<1) ;
				if (a == 0x2000) //HALT
				{done = 1;}
				else
				{	stack [sp] = pc;
					if (sp == (MAXSTACK - 1))
					{allegro_message ("*** Vector generator stack overflow! ***");done = 1;sp = 0;}
					else {sp++;pc = a;}
			    }
				break;

			default: allegro_message ("Error in Analog Vector Engine");
			}	
		}

	
}


WRITE_HANDLER(RedBaronSoundsWrite)
{
	static int lastValue = 0;
	
	input_select=(data & 0x01);

	if (data & 0x20)
	{soundEnable = 1;}
	if (lastValue == data) return;
	lastValue = data;

	/* Enable explosion output */
	if (data & 0xf0)
	{if(sample_playing(2)==0){sample_start (2, 0, 0);}}

    set_aae_leds (~data & 0x08,0,0 );


	/* Enable nosedive output */
	if (data & 0x02)
	{if(sample_playing(2)==0){sample_start (2, 2, 0);}}

	/* Enable firing output */
	if (data & 0x04)
	{   if(sample_playing(1)==0){sample_start (1, 1, 0);}}

}

READ_HANDLER(RBControls)
{
     int c=0x7f;//0x3f
	if (key[config.kstart1])  {bitclr(c,0x40);}
	if (key[config.kp1but1])  {bitset(c,0x80);}
    return c;
}

READ_HANDLER(RBJoyRead)
{
	int res=0;
	
	 if (key[config.kp1down]){pitch-=RESPONSE;}
	 if (key[config.kp1up])  {pitch+=RESPONSE;}
   
	 if (key[config.kp1left]) {roll-=RESPONSE;}
     if (key[config.kp1right]){roll+=RESPONSE;}

	
	if (roll<RBDACMIN) roll=RBDACMIN;
	if (roll>RBDACMAX) roll=RBDACMAX;

	
	if (pitch<RBDACMIN)	pitch=RBDACMIN;
	if (pitch>RBDACMAX)	pitch=RBDACMAX;
	
	if (input_select)
		return (roll);
	else
		return (pitch);
}



READ_HANDLER(BzoneControls)
{
    int c=0x80;

	if (key[config.kstart1])
	{return 0x20;}
	if (key[config.kstart2])
	{return 0x40;}
    if (key[config.kp1but2])
	{return 0x80;}
 	if (key[config.kp1but1])  //FIRE 
	{ c=0x10;}
	
if (config.hack)
   {
	
	if (key[config.kp1up] && key[config.kp1right])
	{return 0x08 | c;}
	if (key[config.kp1up] && key[config.kp1left])
	{return 0x02 | c;}
	if (key[config.kp1down] && key[config.kp1right])
	{ return 0x04|c;}
	if (key[config.kp1down] && key[config.kp1left])
	{return 0x01|c;}

	if (key[config.kp1up])
	{ return 0x0a|c;}
	if (key[config.kp1down])
    return 0x05|c;
	if (key[config.kp1left])
    return 0x06|c;
	if (key[config.kp1right])
    return 0x09|c;
	}
else
	{
	  if (key[config.kp1but3]) //Move forward Right
      {c = c | 0x02;}
	  if (key[config.kp1but4]) //Move backward Right
      {c = c | 0x01;}
	  if (key[config.kp1but5]) //Move forward Left
      {c = c | 0x08; }
	  if (key[config.kp1but6]) //Move backward Left
      {c = c | 0x04;}
	 
	}
return c;
}

WRITE_HANDLER(BZgo)
{  
 
	if (vector_off_bz==0) {return;}
	else { 
 	bz_gen_vector_list();
	if (total_length ) {vector_off_bz=0;get_video_ticks(0xff);} else {vector_off_bz=1;}
	}

}

READ_HANDLER(BzoneIN0read)
{
   	int val=0x7f;
	float me;

	me = (((1775 * total_length)/ 1000000) * 1512);
	//write_to_log("Total LENGTH HERE %f and TOTAL TICKS %d",me,ticks );
	if (get_video_ticks(0) > me && vector_off_bz==0 ) {vector_off_bz=1;}
	
	if (get_eterna_ticks(0) & 0x100)   {bitset(val, 0x80);} 
	if (vector_off_bz==0)              {bitclr(val, 0x40);} 
	if (key[config.ktestadv])		   {bitclr(val, 0x20);}
	if (testsw)						   {bitclr(val, 0x10);}
	if (key[config.kcoin2])			   {bitclr(val, 0x02);}
	if (key[config.kcoin1])			   {bitclr(val, 0x01);}
	
	return val;
}



WRITE_HANDLER (BzoneSounds)
{
	static int lastValue = 0;
    
	set_aae_leds (~data & 0x40,0,0 );

	// Enable/disable all sound output 
	if (data & 0x20)
	{soundEnable = 1; if (!sample_playing(3)){sample_start(3,2,1);}}
	else {soundEnable = 0;}
    
	// If sound is off, don't bother playing samples 
	if (!soundEnable) {sample_stop(3);return;}

	if (lastValue == data) return;
	lastValue = data;

	// Enable explosion output 
	if (data & 0x01)
	{
		if (data & 0x02) {sample_start(1,4,0);}else {sample_start(1,5,0);}
	}
	
	// Enable shell output 
	if (data & 0x04)
	{   if (data & 0x08) { sample_start(2,0,0);} // loud shell 
		else {sample_start(2,1,0);} // soft shell 
	}

	// Enable engine output
	if (data & 0x80)
	{	if (data & 0x10) sample_set_freq(3,33000); // Fast rumble
		else sample_set_freq(3,22050); // Slow rumble
	}
}



READ_HANDLER(BZRand)
{
	return rand() & 0xff;
}
///////////////////////  MAIN LOOP /////////////////////////////////////
void run_bzone()
{
    //BZSaveScore();
	if (KeyCheck(config.ktest))   {testsw^=1;cpu_needs_reset(0);}
	if (KeyCheck(config.kreset))  {cpu_needs_reset(0);}
	if (testsw) cpu_disable_interrupts(0); else cpu_disable_interrupts(1);

	 if (!paused && soundEnable) {pokey_sh_update();}	
}

MEM_READ(BzoneRead) 
 MEM_ADDR(0x0800, 0x0800, BzoneIN0read)
 MEM_ADDR(0x1800, 0x1800, MathboxStatusRead)
 MEM_ADDR(0x1810, 0x1810, MathboxLowbitRead)
 MEM_ADDR(0x1818, 0x1818, MathboxHighbitRead)
 MEM_ADDR(0x182a, 0x182a, BZRand)
 MEM_ADDR( 0x1828, 0x1828, BzoneControls)
MEM_END

MEM_WRITE(BzoneWrite)
 MEM_ADDR(0x1000, 0x1000, NoWrite)
 MEM_ADDR(0x1200, 0x1200, BZgo)
 MEM_ADDR(0x1400, 0x1400, NoWrite)
 MEM_ADDR(0x1600, 0x1600, NoWrite)
 MEM_ADDR(0x1820, 0x182f, pokey_1_w)
 MEM_ADDR(0x1860, 0x187f, MathboxGo)
 MEM_ADDR(0x1840, 0x1840, BzoneSounds)
 MEM_ADDR(0x3000, 0xffff, NoWrite)
MEM_END

MEM_READ(RedBaronRead)
 MEM_ADDR(0x0800, 0x0800, BzoneIN0read)
//	MEM_ADDR( 0x0a00, 0x0a00, RBDSW1)
//	MEM_ADDR( 0x0c00, 0x0c00, RBDSW2)
 MEM_ADDR(0x181a, 0x181a, randRead)
 MEM_ADDR(0x1818, 0x1818, RBJoyRead)
 MEM_ADDR(0x1800, 0x1800, MathboxStatusRead)
 MEM_ADDR(0x1804, 0x1804, MathboxLowbitRead)
 MEM_ADDR(0x1806, 0x1806, MathboxHighbitRead)
 MEM_ADDR(0x1802, 0x1802, RBControls)
 MEM_ADDR(0x1820, 0x185f, EaromRead)
MEM_END

MEM_WRITE(RedBaronWrite)
 //MEM_ADDR( 0x0a00, 0x0a00, NoWrite)
 //MEM_ADDR( 0x0c00, 0x0c00, NoWrite)
 MEM_ADDR(0x1808, 0x1808, RedBaronSoundsWrite)
 MEM_ADDR(0x1810, 0x181f, pokey_1_w)
 MEM_ADDR(0x180c, 0x180c, EaromCtrl)
 MEM_ADDR(0x1820, 0x185f, EaromWrite)
 MEM_ADDR(0x1860, 0x187f, MathboxGo)
 MEM_ADDR(0x1200, 0x1200, BZgo)
 MEM_ADDR(0x5000, 0x7fff, NoWrite)
 MEM_ADDR(0x3000, 0x37ff, NoWrite)
MEM_END

//////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_bzone()
{
	
	if (gamenum==REDBARON){init6502Z(RedBaronRead,RedBaronWrite,0);} 
	else { init6502Z(BzoneRead,BzoneWrite,0);}
  	
	set_bz_colors();
	setup_ambient(VECTOR);
	//LoadEarom();
    total_length=0;vector_off_bz=0;	
	
	pokey_sh_start(&pokey_interface);
	return 0;
}	
void end_bzone()
{
    pokey_sh_stop();
	
}

//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////


