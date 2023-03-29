
#include "quantum.h"
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

//#define BYTESWAP(x) ((((uint16_t)(x))>>8) | (((uint16_t)(x))<<8))
#define memrdwdf(address) ((vec_ram[pc+1]) | (vec_ram[pc]<<8)) 

static int itgone=0;
static int videoup=0;
unsigned char program_rom[0x14000];
unsigned char main_ram[0x4fff];
unsigned char vec_ram[0x1fff];
unsigned char nv_ram[0x200];  
static int turn1=0;
static int turn2=0; 
static int sample_pos = 0;
static int q_vid_ticks=0;

static void do_interrupt()
{
	int intres=0;
    static int x=0;

	x++;
	if (x==31) {
				intres=s68000interrupt(1,-1);
				s68000flushInterrupts();
				//write_to_log("INTERRUPT");
				x=0; 
					 
			   }

}

static int Qscale_by_cycles(int val, int clock)
{
	int k;
	int current=0;
	int max;
    float temp;

	 current+=s68000readOdometer();
	 current+=gammaticks;

     max= clock / 60;
	 temp=(  (float) current / (float) max);
     if (temp > 1) temp=.95;
	// write_to_log(" Current %d divided by MAX: %d is equal to value: %f",current,max,temp);
	 
	 temp= val * temp;
	 k=temp;
	 return k;
}

static void Q_pokey_update()
{
  int newpos=0;
	
  newpos = Qscale_by_cycles(BUFFER_SIZE, 6048000);	
  
  if (newpos - sample_pos < 10) 	return;
  Pokey_process (soundbuffer + sample_pos, newpos - sample_pos);
  sample_pos = newpos;
}


static void Q_pokey_do_update()
{
	if (sample_pos < BUFFER_SIZE) {Pokey_process (soundbuffer + sample_pos, BUFFER_SIZE - sample_pos);}
	aae_play_streamed_sample(0,soundbuffer,BUFFER_SIZE,44100,127);
	sample_pos = 0;
}


static void read_mouse()
{
 
   int amt1=0;
   int amt2=0;
  // write_to_log("Trackball READ --------------- Address: %x ",(address>>1)&0xf);

         
 //  if (config.mouse1x_invert){mickeyx=mickeyx;}
  // else {mickeyx=-mickeyx;}

  //mickeyx=-mickeyx;
   if (mickeyx > 0 ) amt1=1;
   if (mickeyx > 10 ) amt1=2;
  if (mickeyx > 100 ) amt1=3;
   if (mickeyx > 200 ) amt1=4;
  if (mickeyx > 300 ) amt1=5;

   if (mickeyx < 0 ) amt1=1;
   if (mickeyx < -10 ) amt1=2;
  if (mickeyx < -100 ) amt1=3;
   if (mickeyx < -200 ) amt1=4;
  if (mickeyx < -300 ) amt1=5;

   if (mickeyy > 0 ) amt2=1;
   if (mickeyy > 10 ) amt2=2;
   if (mickeyy > 100 ) amt2=3;
   if (mickeyy > 200 ) amt2=4;
  if (mickeyy > 300 ) amt2=5;

   if (mickeyy < 0 ) amt2=1;
   if (mickeyy < -10 ) amt2=2;
   
  if (mickeyy < -100 ) amt2=3;
   if (mickeyy < -200 ) amt2=4;
  if (mickeyy < -300 ) amt2=5;
   	
   if (key[config.kp1right]) {turn1=turn1+1;if (turn1 > 0x0f) turn1=0x00;}
   if (key[config.kp1left])  {turn1=turn1-1;if (turn1 <0) turn1=0x0f;}
   if (key[config.kp1up])    {turn2=turn2+1;if (turn2 > 0x0f) turn2=0;}
   if (key[config.kp1down])  {turn2=turn2-1;if (turn2 <0) turn2=0x0f;}

   if (mickeyx < 0 ) {turn1=turn1-amt1;if (turn1 <0) turn1=0x0f;}
   if (mickeyx > 0 ) {turn1=turn1+amt1;if (turn1 > 0x0f) turn1=0x00;}
  
   if (mickeyy > 0 ) {turn2=turn2-amt2;if (turn2 <0) turn2=0x0f;}
   if (mickeyy < 0 ) {turn2=turn2+amt2;if (turn2 > 0x0f) turn2=0;}

  
}

 void QSaveScore(void)
{
	char temppath[255]; 
	FILE *fp;
    int i;
   
	strcpy(temppath,"hi\\");
	strcat(temppath,driver[gamenum].name);
	strcat(temppath,".aae");
    //write_to_log("TEMPPATH SAVING is %s",temppath);
    fp = fopen(temppath, "w");
    if (fp == NULL) {write_to_log("Error - I couldn't write Quantum High Score");}
    for (i= 0; i < 0x200; i++) {fprintf(fp, "%c", nv_ram[i]);}fclose(fp);   
}
 int QLoadScore(void)
{
    FILE *fp;
    char c;
    int i=0;
	char temppath[255]; 
    strcpy(temppath,"hi\\");
	strcat(temppath,driver[gamenum].name);
	strcat(temppath,".aae");
   // write_to_log("TEMPPATH LOADING is %s",temppath);
    fp = fopen(temppath,"r");
  
    if (fp == NULL) {write_to_log("Error - I couldn't READ Quantum High Score");}
    if (fp != NULL) 
    {
      do {
         c = getc(fp);    /* get one character from the file */
        nv_ram[i]=c;         /* display it on the monitor       */
        i++;
	 } while (i< 0x200);    /* repeat until EOF (end of file)  */
    fclose(fp);
    return 1;
	}
 else return 0;
}

static void BW_generate_vector_list (void)
{
	int pc=0x0000;
	int sp;
	int stack [8];
    int flipword = 0;
	int scale=0;
	int statz   = 0;
	int xflip   = 0;
  	int color   = 0;
	int currentx, currenty=0;
	int done    = 0;
	int firstwd, secondwd;
	int opcode;
	int x, y, z=0, b, l, d, a;
	int deltax, deltay=0;
	int red,green,blue;

	sp = 0;
	statz = 0;
	color = 0;
	scale = 0;          
	total_length=0;

     firstwd = memrdwdf (pc);
	 pc++;pc++;
	 secondwd = memrdwdf (pc);
     	
	 if ((firstwd == 0) && (secondwd == 0))
	 {;}//write_to_log("VGO with zeroed vector memory at %x\n",pc);return;}//write_to_log("VGO with zeroed vector memory at %x\n",pc); }
     pc=0x0000;
	while (!done)
	{
	   firstwd = memrdwdf (pc);   opcode = firstwd >> 13;pc++;pc++;	
	   //write_to_log("FirstWord is  %x",firstwd);
	   if (opcode == VCTR) //Get the second word if it's a draw command
		{secondwd = memrdwdf (pc);pc++;pc++;}
	
	   if ((opcode == STAT) && ((firstwd & 0x1000) != 0))opcode = SCAL;
	   switch (opcode)
		{
			case VCTR:x=twos_comp_val(secondwd, 12);y=twos_comp_val(firstwd, 12);z = (secondwd >> 12) & ~0x01;
				
				
				      
				goto DRAWCODE;break;    
			case SVEC:x=twos_comp_val(firstwd, 5)<<1;y=twos_comp_val(firstwd >> 8,5)<<1;z = ((firstwd >> 4) & 0x0e);

DRAWCODE:			if (z == 2){z = statz;}if (z) {z = (z << 4) | 0x1f;}
				   
			        deltax = x * scale;
					deltay = y * scale;
					 total_length+=vector_timer(deltax, deltay); 
			         if (xflip) deltax = -deltax;
			        
			 if (z>0)
			 {     
                
			    //write_to_log("red %f green %f blue %f z %d",vec_colors[color].r,vec_colors[color].g,vec_colors[color].b,z);
				// glColor4ub(vec_colors[color].r,vec_colors[color].g,vec_colors[color].b,z);
				 if (vec_colors[color].r) red=z; else red=0;
				 if (vec_colors[color].g) green=z; else green=0;
				 if (vec_colors[color].b) blue=z; else blue=0;
				
	        	if ((currentx==(currentx)+deltax) && (currenty==(currenty)-deltay))
					 {add_color_point((currentx>>VEC_SHIFT),currenty>>VEC_SHIFT, red,green,blue);}
	
				else{	 
				     add_color_line((currentx>>VEC_SHIFT), (currenty>>VEC_SHIFT), (currentx+deltax)>>VEC_SHIFT, (currenty-deltay)>>VEC_SHIFT, red,green,blue);
				     add_color_point((currentx>>VEC_SHIFT),currenty>>VEC_SHIFT,  red,green,blue);
				     add_color_point((currentx+deltax)>>VEC_SHIFT,(currenty-deltay)>>VEC_SHIFT,  red,green,blue);
                    }
				}
				currentx += deltax;currenty -= deltay;
				
				break;

			case STAT: statz = (firstwd >> 4) & 0x000f;color = (firstwd) & 0x000f;break;
			case SCAL:
				b = ((firstwd >> 8) & 0x07)+8;
				l = (~firstwd) & 0xff;
				scale = (l << VEC_SHIFT) >> b;		
			    scale=scale; //Double the scale for 1024x768 resolution
				break;

			case CNTR: d = firstwd & 0xff;currentx = 512 << VEC_SHIFT;currenty = 512 << VEC_SHIFT;break; 
			case RTSL:

				if (sp == 0)
				{	write_to_log("AVG Stack Underflow, <quantum error.>");
					done = 1;sp = MAXSTACK - 1;}
				else {sp--; pc = stack [sp];	}
				break;

			case HALT: done = 1;break;
			case JMPL: a=0x0000+((firstwd & 0x1fff)<<1);if (a == 0x0000){done=1;}else{pc=a;}break;/* if a = 0x0000, treat as HALT */
			case JSRL: a=0x0000+((firstwd & 0x1fff)<<1) ;
				       if (a == 0x0000){done = 1;}
					   else {stack [sp] = pc;
					
					   if (sp == (MAXSTACK - 1))
					   {write_to_log ("AVG Stack Overflow <quantum error>");done = 1;sp = 0;}
					   else{sp++;pc=a;}
			            }break;
				

			default: write_to_log("Some sort of Error in AVG engine, default reached.");
			}	
		}

		
}


static void byteswap(unsigned char *mem, int length)
{
    int i, j;
    for(i = 0; i < (length / 2); i += 1)
    {
        j = mem[i*2+0];
        mem[i*2+0] = mem[i*2+1];
        mem[i*2+1] = j;
    }
}
void my_reset_handler(void) {write_to_log("_____RESET CALLED _____");}



void vec_ram_w(unsigned address, unsigned data)
{
	//write_to_log("Vector Ram Write Address: %x data %x  Datapart1 %x Data part2 %x",address,data,(data & 0xff00)>>8,(data & 0x00ff) );
    vec_ram[address-0x800000]=(data & 0xff00)>>8;
	vec_ram[(address+1)-0x800000]=data & 0x00ff;

}
unsigned vec_ram_r(unsigned address)
{
	int c;
	c=vec_ram[address-0x800000]<<8 | vec_ram[(address+1)-0x800000];
	//write_to_log("Vector Ram READ: Data Returned %x",(vec_ram[address-0x800000]<<8 | vec_ram[(address+1)-0x800000]) );
    return c;
}

unsigned quantum_trackball_r(unsigned address)
{ 
	/*
	static int turn1=0;
	static int turn2=0;
  
   get_mouse_mickeys(&mickeyx, &mickeyy);
 //  position_mouse(SCREEN_W/2, SCREEN_H/2);     
   if (config.mouse1x_invert){mickeyx=mickeyx;}
   else {mickeyx=-mickeyx;}
  
   if (key[config.kp1right]) {turn1=turn1+1;if (turn1 > 0x0f) turn1=0x00;}
   if (key[config.kp1left])  {turn1=turn1-1;if (turn1 <0) turn1=0x0f;}
   if (key[config.kp1up])    {turn2=turn2+1;if (turn2 > 0x0f) turn2=0;}
   if (key[config.kp1down])  {turn2=turn2-1;if (turn2 <0) turn2=0x0f;}

   if (mickeyx > 0 ) {turn1=turn1-1;if (turn1 <0) turn1=0x0f;}
   if (mickeyx < 0 ) {turn1=turn1+1;if (turn1 > 0x0f) turn1=0x00;}
  
   if (mickeyy > 0 ) {turn2=turn2-1;if (turn2 <0) turn2=0x0f;}
   if (mickeyy < 0 ) {turn2=turn2+1;if (turn2 > 0x0f) turn2=0;}
   */
   //write_to_log("mouse read %d %d",mickeyx,mickeyy);   
	read_mouse();
   return (turn1 << 4) | turn2;
}
unsigned quantum_switches_r(unsigned address)
{
 int c=0xfe;
 static int toggle=0;
 //float me;
	
		//me = (((1775 * total_length)/ 1000000) * 1500); //1775 // ((TIME_IN_NSEC(1512) * total_length)) * 1512000;
	   // ticks2 = get_video_ticks(0);//m6502zpGetElapsedTicks2(0);
		//write_to_log("Total LENGTH HERE %f and TOTAL TICKS %d",me,ticks2 );
	 
	//if (itgone) 	if (q_vid_ticks > me  ) {itgone=0;total_length=0;}
    



 if (key[config.kcoin1])   bitclr(c, 0x20); 
 if (key[config.kstart1])  bitclr(c, 0x04);
 if (key[config.kstart2])  bitclr(c, 0x08); 
 if (key[KEY_F2])    toggle^=1;  
 if (key[KEY_F3])   s68000reset();
 if (toggle)               bitclr(c, 0x80); 



// if (itgone)               bitset(c, 0x01);

 
 return c;
}

void quantum_led_write(unsigned address, unsigned data)
{  	
    if (data & 0xff)
	{
		/* bits 0 and 1 are coin counters */
		
		/* bit 3 = select second trackball for cocktail mode? */

		/* bits 4 and 5 are LED controls */
	    set_aae_leds( data & 0x10,data & 0x20,0);
		/* bits 6 and 7 flip screen */
		//vector_set_flip_x (data & 0x40);
		//vector_set_flip_y (data & 0x80);
	}
}
void MWA_NOP(unsigned address, unsigned data)
{ write_to_log("NOOP Write Address: %x Data: %x",address,data);}

void UN_READ(unsigned address)
{write_to_log("--------------------Unhandled Read, %x data: %x",address);}

void UN_WRITE(unsigned address, unsigned data)
{write_to_log("--------------------Unhandled Read, %x data: %x",address,data);}

unsigned MRA_NOP(unsigned address)
{return 0x00;}//write_to_log("WATCHDOG read");

void watchdog_reset_w(unsigned address, unsigned data)
{;}//write_to_log("watchdog reset WRITE");
unsigned quantum_snd_read(unsigned address)
{
	int a=0; //coin 1
    int b=0; //coin 2
	//write_to_log("---------------------------sound read-------------------------- ADDRESS %x",(address>>1)&0xf);
    
    switch(z80dip1)
    {
     case 0: a=0xff;break;
     case 1: break;
     case 2: a=0xff;b=0xff;break;
	 case 3: b=0xff;break;
   }

	address=(address>>1)&0xf;
	switch (address)
	{
  	 case 0: return 0x00;break; //1
	 case 1: return 0x00;break; //2
	 case 2: return 0x00;break; //3
	 case 3: return 0x00;break; //4
	 case 4: return 0x00;break; //5
	 case 5: return 0x00;break; //6
	 case 6: return a;break; //7 COINAGE 1 
	 case 7: return b;break; //8 COINAGE 2
	 case 8: return 0x00;break; //??
	 default: write_to_log("unhandled pokey read &x",address);
	}

return 0;
//if ( (address&0x0f) == 0x0a){return rand();}
}
void quantum_snd_write(unsigned address, unsigned data) 
{
	unsigned  data1;
	unsigned  data2;

	data1=(data & 0xff00)>>8;
	data2=data & 0x00ff;
	
   //write_to_log("SOUND ADDRESS CONVERSION %x", (address>>1)&0xf);
   if (address & 0x1) {
	                   Q_pokey_update();
	                   Update_pokey_sound( (address>>1)&0xf,data1,0,6);
                       }
   else {
	       Q_pokey_update();
	       Update_pokey_sound((address>>1)&0xf,data2,1,6);
        }
}
void quantum_colorram_w(unsigned address, unsigned data)
{ 
	int r,g,b;
	int bit0,bit1,bit2,bit3;
	
    address=(address&0xff)>>1;
    data=data&0x00ff;
	     
	bit3 = (~data >> 3) & 1;
	bit2 = (~data >> 2) & 1;
	bit1 = (~data >> 1) & 1;
	bit0 = (~data >> 0) & 1;

	g = bit1 * 0xaa + bit0 * 0x54; //54
	b = bit2 * 0xdf;
	r = bit3 * 0xe9; //ce

	if (r>255)r=255;
	// write_to_log("vec color set R %d G %d B %d",r,g,b);
	vec_colors[address].r=r;
    vec_colors[address].g=g;
    vec_colors[address].b=b;
		
}
void avgdvg_resetQ(unsigned address, unsigned data)
{
	write_to_log("AVG Reset");itgone=0;
}
void avgdvg_goQ(unsigned address, unsigned data)
{
	//if (itgone) return;
	if (itgone==0 && videoup==0)
	     {
			
             BW_generate_vector_list(); 
			 if (total_length ) {itgone=1;q_vid_ticks=0;} else {itgone=0;}
         }
	 videoup=1;
 }
/*
unsigned read_handler(unsigned address)
{;}
void write_handler(unsigned address, unsigned data)
{;}
*/

/*
	QUANTUM MEMORY MAP (per schem):

	000000-003FFF	ROM0
	004000-004FFF	ROM1
	008000-00BFFF	ROM2
	00C000-00FFFF	ROM3
	010000-013FFF	ROM4

	018000-01BFFF	RAM0
	01C000-01CFFF	RAM1

	940000			TRACKBALL
	948000			SWITCHES
	950000			COLORRAM
	958000			CONTROL (LED and coin control)
	960000-970000	RECALL (nvram read)
	968000			VGRST (vector reset)
	970000			VGGO (vector go)
	978000			WDCLR (watchdog)
	900000			NVRAM (nvram write)
	840000			I/OS (sound and dip switches)
	800000-801FFF	VMEM (vector display list)
	940000			I/O (shematic label really - covered above)
	900000			DTACK1
*/

 struct STARSCREAM_PROGRAMREGION quantum_fetch[] = {
      {0x000000, 0x013FFF, (unsigned )program_rom - 0x000000},
      {0x018000, 0x01CFFF, (unsigned) main_ram -0x018000},
   	  {0x900000, 0x9001ff, (unsigned )nv_ram - 0x900000},
	  {-1, -1, (unsigned ) NULL}
   };


  struct STARSCREAM_DATAREGION quantum_readbyte[] =
{
	{ 0x000000, 0x013fff, NULL, program_rom },
	{ 0x018000, 0x01cfff,  NULL,main_ram },
	{ 0x800000, 0x801fff, vec_ram_r,NULL},
	{ 0x840000, 0x84003f, quantum_snd_read,NULL },
	{ 0x900000, 0x9001ff, NULL, nv_ram},
	{ 0x940000, 0x940001,  quantum_trackball_r, NULL}, 
	{ 0x948000, 0x948001,  quantum_switches_r, NULL },
	//{ 0x950000, 0x95001f,  NULL, color_ram},
	{ 0x960000, 0x9601ff, UN_READ,NULL },
	{ 0x978000, 0x978001,  MRA_NOP,NULL },	
	//{ 0x000000, 0xfffff, UN_READ, NULL },
	 {-1, -1, NULL, NULL}
};

  
  struct STARSCREAM_DATAREGION quantum_readword[] =
{
	{ 0x000000, 0x013fff, NULL, program_rom },
	{ 0x018000, 0x01cfff,  NULL,main_ram },
	{ 0x800000, 0x801fff, vec_ram_r,NULL},
	{ 0x840000, 0x84003f, quantum_snd_read,NULL },
	{ 0x900000, 0x9001ff,  NULL, nv_ram},
	{ 0x940000, 0x940001,  quantum_trackball_r, NULL}, 
	{ 0x948000, 0x948001,  quantum_switches_r, NULL },
	//{ 0x950000, 0x95001f,  NULL, color_ram},
	{ 0x960000, 0x9601ff, UN_READ,NULL },
	{ 0x978000, 0x978001,  MRA_NOP,NULL },	
	//{ 0x000000, 0xfffff, UN_READ, NULL },
	 {-1, -1, NULL, NULL}
};

struct STARSCREAM_DATAREGION quantum_writebyte[] =
{
	{ 0x000000, 0x013fff, UN_WRITE,NULL },
	{ 0x018000, 0x01cfff,  NULL,main_ram },
	{ 0x800000, 0x801fff, vec_ram_w,NULL},
	{ 0x840000, 0x84003f,  quantum_snd_write,NULL },
	{ 0x900000, 0x9001ff,  NULL,nv_ram },
	{ 0x950000, 0x95001f,  quantum_colorram_w,NULL},
	{ 0x958000, 0x958001,  quantum_led_write,NULL },
	{ 0x960000, 0x960001,  MWA_NOP,NULL },	// enable NVRAM? 
	{ 0x968000, 0x968001, avgdvg_resetQ,NULL },
	//{ 0x970000, 0x970001,  avgdvg_goQ,NULL },
	//{ 0x970000, 0x970001, NULL,qavggo },
	//{ 0x978000, 0x978001,  watchdog_reset_w,NULL },
	// the following is wrong, but it's the only way I found to fix the service mode 
	//{ 0x978000, 0x978001,  avgdvg_goQ,NULL },
	//{ 0x000000, 0xfffff, UN_WRITE, NULL }, 
	{-1, -1, NULL, NULL}
};

struct STARSCREAM_DATAREGION quantum_writeword[] =
{
	{ 0x000000, 0x013fff, UN_WRITE,NULL },
	{ 0x018000, 0x01cfff,  NULL,main_ram },
	{ 0x800000, 0x801fff, vec_ram_w,NULL},
	{ 0x840000, 0x84003f,  quantum_snd_write,NULL },
	{ 0x900000, 0x9001ff, NULL,nv_ram },
	{ 0x950000, 0x95001f, quantum_colorram_w, NULL},
	{ 0x958000, 0x958001, quantum_led_write,NULL },
	{ 0x960000, 0x960001, MWA_NOP,NULL },	// enable NVRAM? 
	{ 0x968000, 0x968001, avgdvg_resetQ,NULL },
	//{ 0x970000, 0x970001,  avgdvg_goQ,NULL },
	//{ 0x978000, 0x978001, watchdog_reset_w,NULL },
	// the following is wrong, but it's the only way I found to fix the service mode 
	{ 0x978000, 0x978001,  avgdvg_goQ,NULL },
	{ 0x000000, 0xfffff, UN_WRITE, NULL },
	 {-1, -1, NULL, NULL}
};

/*
void init68k(struct STARSCREAM_PROGRAMREGION *fetch, struct STARSCREAM_DATAREGION *readbyte, struct STARSCREAM_DATAREGION *readword, struct STARSCREAM_DATAREGION *writebyte, struct STARSCREAM_DATAREGION *writeword)
{
      s68000init();
       s68000context.fetch=fetch;
	  s68000context.s_fetch=fetch;
      s68000context.u_fetch=fetch;
      s68000context.s_readbyte=readbyte;
      s68000context.u_readbyte=readbyte;
      s68000context.s_readword=readword;
      s68000context.u_readword=readword;
      s68000context.s_writebyte=writebyte;
      s68000context.u_writebyte=writebyte;
      s68000context.s_writeword=writeword;
      s68000context.u_writeword=writeword;
	  s68000SetContext(&s68000context);
      s68000reset();
	  s68000exec(100);
       s68000reset();
     write_to_log("Initial PC is %06X\n",s68000context.pc);
}

*/

/*--------------------------------------------------------------------------*/
/* Memory handlers                                                          */
/*--------------------------------------------------------------------------*/
/*
unsigned int m68k_read_memory_8(unsigned int address)
{
  ; 
}


void m68k_write_memory_8(unsigned int address, unsigned int value)
{
;
}

unsigned int m68k_read_memory_16(unsigned int address)
{
     write_to_log("read word from %08X (%08X)\n", address, m68k_readpc());
    return (0xFFFF);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
   write_to_log("write word %04X to %08X (%08X)\n", value, address, m68k_readpc());
}
*/


void run_quantum()
{
	int x;
	
	UINT32 dwResult = 0;
	int intres=0;
	int bugger=0;
	int cycles =  (6048000 / 60);

	cache_clear();
	//s68000readOdometer()   //  - Returns the value of the odometer.
    s68000tripOdometer();    // - Returns the value of the odometer, and sets it to zero in the process.
	gammaticks=0;
	videoup=0;

	//write_to_log("FRAME STARTS HERE --------------------------");
		 for (x=0; x < 126; x++)
		{
		    dwResult=s68000exec(800);
            if (dwResult!=0x80000000)
            {                  
                  allegro_message("Bad result : %08X\n",dwResult);
                  exit(-1);
            }
			bugger = s68000tripOdometer();
			gammaticks+=bugger;
			q_vid_ticks+=bugger;
            do_interrupt();
          			
		 }
		 
	    update_mouse(&mickeyx, &mickeyy);//get_mouse_mickeys(&mickeyx, &mickeyy);
		position_mouse(SCREEN_W/2, SCREEN_H/2); 
		//if (videoup==0){  BW_generate_vector_list();} //Catch empto proto frames
	    Q_pokey_do_update();
		itgone=0;

		//write_to_log("Total cycles this frame %d",gammaticks);
}


int init_quantum()
{
    itgone=0;
	memset(main_ram, 0x00, 0x4fff);
	memset(vec_ram, 0x00, 0x1fff);
    memset(program_rom,0x00,0x13fff);
	memset(nv_ram,0xff,0x200);
	memcpy(program_rom,gameImage[0],0x14000);//0x14000
	free(gameImage);
	byteswap(program_rom, 0x14000);
	init68k(quantum_fetch,quantum_readbyte,quantum_readword,quantum_writebyte,quantum_writeword);
	BUFFER_SIZE = 44100 / driver[gamenum].fps;
	chip=2;
	gain=6;
	q_vid_ticks=0;
    //pokey_sh_start (&pokey_interface);	
	Pokey_sound_init(600000, 44100,chip,0 ); // INIT POKEY
	soundbuffer =  malloc(BUFFER_SIZE);
	stream = play_audio_stream(BUFFER_SIZE, 8, FALSE, 44100, config.pokeyvol, 128);
	//START EMU HERE
    QLoadScore();
	return 0;
}


void end_quantum()
{
	QSaveScore();
	//pokey_sh_stop();	
   	free_audio_stream_buffer(stream);
	stop_audio_stream(stream);
	free(soundbuffer);
}

