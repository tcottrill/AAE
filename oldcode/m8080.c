
#include "m8080.h"
#include "mz80.h"

//LOCAL VARIABLES
extern char *gamename[];
extern gamenum;




static DIPSWITCH invaders_dips[] = {
  
   { "LIVES", {"3 SHIPS", "4 SHIPS", "5 SHIPS ", "6 SHIPS "},
   3, 1, 0xfc, 0, 0,{0, 1, 2, 3 }},
   { "COIN INFO", {"NO","YES"," "," "},
   1, 1, 0x3f, 1, 1,{0x80, 0, 0, 0 }},
   { "BONUS LIFE", {"1000","1500"," "," "},
   1, 1, 0xf7, 0, 0,{0x08, 0, 0, 0 }},
   { "COCKTAIL", {"NO", "YES", " ", " "},
   1, 0x7777, 0, 0, 0,{0, 1, 0, 0, }},
   { "NONE", {"NONE", " ", " ", " "},
   0, 0, 0, 0,0,{0, 0, 0, 0 }}

};

static DIPSWITCH sitv_dips[] = {
  
   { "LIVES", {"3 SHIPS", "4 SHIPS", "5 SHIPS ", "6 SHIPS "},
   3, 1, 0xfc, 0, 0,{0, 1, 2, 3 }},
   { "COIN INFO", {"NO","YES"," "," "},
   1, 1, 0x3f, 1, 1,{0x80, 0, 0, 0 }},
   { "BONUS LIFE", {"1000","1500"," "," "},
   1, 1, 0xf7, 0, 0,{0x08, 0, 0, 0 }},
   { "COCKTAIL", {"NO", "YES", " ", " "},
   1, 0x7777, 0, 0, 0,{0, 1, 0, 0, }},
   { "NONE", {"NONE", " ", " ", " "},
   0, 0, 0, 0,0,{0, 0, 0, 0 }}

};
static DIPSWITCH invaddlx_dips[] = {
  
   { "LIVES", {"3 SHIPS", "4 SHIPS", " ", " "},
   1, 1, 0xfc, 0, 0,{0, 1, 0, 0 }},
   { "SCORE PRESET", {"OFF","ON"," "," "},
   1, 1, 0xf7, 0, 0,{0,0x08,0,0}},
   { "COIN INFO", {"NO","YES"," "," "},
   1, 1, 0x3f, 1, 1,{0x80, 0, 0, 0 }},
   
   { "COCKTAIL", {"NO", "YES", " ", " "},
   1, 0x7777, 0, 0, 0,{0, 1, 0, 0, }},
   { "NONE",{"NONE","","",""},0,0,0,0,0,{0,0,0,0}}

};

static DIPSWITCH invadpt2_dips[] = {
  
   { "LIVES", {"3 SHIPS", "4 SHIPS", " ", " "},
   1, 1, 0xfc, 0, 0,{0, 1, 0, 0 }},
   { "SCORE PRESET", {"OFF","ON"," "," "},
   1, 1, 0xf7, 0, 0,{0,0x08,0,0}},
   { "COIN INFO", {"NO","YES"," "," "},
   1, 1, 0x3f, 1, 1,{0x80, 0, 0, 0 }},
   
   { "COCKTAIL", {"NO", "YES", " ", " "},
   1, 0x7777, 0, 0, 0,{0, 1, 0, 0, }},
   { "NONE",{"NONE","","",""},0,0,0,0,0,{0,0,0,0}}

};

static DIPSWITCH sicv_dips[] = {
  
   { "LIVES", {"3 SHIPS", "4 SHIPS", " ", " "},
   1, 1, 0xfc, 0, 0,{0, 1, 0, 0 }},
   { "SCORE PRESET", {"OFF","ON"," "," "},
   1, 1, 0xf7, 0, 0,{0,0x08,0,0}},
   { "COIN INFO", {"NO","YES"," "," "},
   1, 1, 0x3f, 1, 1,{0x80, 0, 0, 0 }},
   
   { "COCKTAIL", {"NO", "YES", " ", " "},
   1, 0x7777, 0, 0, 0,{0, 1, 0, 0, }},
   { "NONE",{"NONE","","",""},0,0,0,0,0,{0,0,0,0}}

};

static DIPSWITCH clowns_dips[] = {
  
   { "LIVES", {"3 CLOWNS", "4 CLOWNS", " ", " "},
   1, 1, 0xfc, 0, 0,{0, 1, 0, 0 }},
   { "SCORE PRESET", {"OFF","ON"," "," "},
   1, 1, 0xf7, 0, 0,{0,0x08,0,0}},
   { "COIN INFO", {"NO","YES"," "," "},
   1, 1, 0x3f, 1, 1,{0x80, 0, 0, 0 }},
   
   { "COCKTAIL", {"NO", "YES", " ", " "},
   1, 0x7777, 0, 0, 0,{0, 1, 0, 0, }},
   { "NONE",{"NONE","","",""},0,0,0,0,0,{0,0,0,0}}

};
// Invaders global variables


float background_blend_val=.93;

char *invaders_samples[]={ 
	"invaders.zip",
	"0.wav",
    "1.wav",
    "2.wav",
	"3.wav",
	"4.wav",
	"5.wav",
	"6.wav",
	"7.wav",
	"8.wav",
	"9.wav",
     "NULL"
    };


char *lrescue_samples[]={ 
	"lrescue.zip",
	"alienexplosion.wav",
	"rescueshipexplosion.wav",
	"beamgun.wav",
	"thrust.wav",
	"bonus2.wav",
	"bonus3.wav",
	"shootingstar.wav",
	"stepl.wav",
	"steph.wav",
	"endlevel.wav",
     "NULL"
     };


const char *invad2ct_samples[] =
{
	"invaders.zip",
	"1.wav",	/* Shot/Missle - Player 1 */
	"2.wav",	/* Base Hit/Explosion - Player 1 */
	"3.wav",	/* Invader Hit - Player 1 */
	"4.wav",	/* Fleet move 1 - Player 1 */
	"5.wav",	/* Fleet move 2 - Player 1 */
	"6.wav",	/* Fleet move 3 - Player 1 */
	"7.wav",	/* Fleet move 4 - Player 1 */
	"8.wav",	/* UFO/Saucer Hit - Player 1 */
	"9.wav",	/* Bonus Base - Player 1 */
	"11.wav",	/* Shot/Missle - Player 2 */
	"12.wav",	/* Base Hit/Explosion - Player 2 */
	"13.wav",	/* Invader Hit - Player 2 */
	"14.wav",	/* Fleet move 1 - Player 2 */
	"15.wav",	/* Fleet move 2 - Player 2 */
	"16.wav",	/* Fleet move 3 - Player 2 */
	"17.wav",	/* Fleet move 4 - Player 2 */
	"18.wav",	/* UFO/Saucer Hit - Player 2 */
	"NULL"      /* end of array */
};


struct MemoryWriteByte sInvadersWrite[] =
{
	{0x0000, 0x1FFF, NoWrite},
  	{0x2400, 0x3FFF, InvadersOut},
  
	{0x4000, 0x57ff, NoWrite},
	{-1, 	 -1, 	 NULL}
};

struct MemoryReadByte sInvadersRead[] =
{
	{-1, -1, NULL}
};

struct z80PortRead sInvadersPortRead[] =
{
	{ 0x0000, 0x0002, InvadersPlayerRead, NULL },
	//{ 0x0002, 0x0002, InvadersDipswitchRead, NULL },
	{ 0x0003, 0x0003, InvadersShiftDataRead, NULL },
	{-1,	  -1,	  NULL}
};

struct z80PortWrite sInvadersPortWrite[] =
{
	{ 0x01, 0x02, InvadersShiftAmountWrite },//0x02 //1 for clowns
   	{ 0x03, 0x03, InvadersSoundPort3Write },
	{ 0x02, 0x04, InvadersShiftDataWrite }, //0x04 //02 for clowns
   	{ 0x05, 0x05, InvadersSoundPort5Write },
	{-1,	-1,	NULL}
};

struct z80PortWrite lRescuePortWrite[] =
{
	{ 0x01, 0x02, InvadersShiftAmountWrite },//0x02 //1 for clowns
   	{ 0x03, 0x03, LrescueSoundPort3Write },
	{ 0x02, 0x04, InvadersShiftDataWrite }, //0x04 //02 for clowns
   	{ 0x05, 0x05, LrescueSoundPort5Write },
	{-1,	-1,	NULL}
};

struct z80PortRead BootHillPortRead[] =                                  
{
	{ 0x0000, 0x0002, InvadersPlayerRead, NULL },
	//{ 0x00, 0x00, boothill_port_0_r },
	//{ 0x01, 0x01, boothill_port_1_r },
	//{ 0x02, 0x02, input_port_2_r },
	//{ 0x0002, 0x0002, InvadersDipswitchRead },
	{ 0x03, 0x03, BootHillShiftDataRead },
	{-1,	  -1,	  NULL}
};

struct z80PortWrite BootHillPortWrite[] =                             
{
	{ 0x01, 0x01, InvadersShiftAmountWrite },
	{ 0x02, 0x02, InvadersShiftDataWrite },
	//{ 0x03, 0x03, BootHillSoundPort3Write },
	//{ 0x05, 0x05, BootHillSoundPort5Write },
	{-1,	  -1,	  NULL}
};

struct z80PortRead cLownsPortRead[] =
{
	{ 0x0000, 0x0002, ClownsPlayerRead, NULL },
	//{ 0x0002, 0x0002, InvadersDipswitchRead, NULL },
	{ 0x0003, 0x0003, InvadersShiftDataRead, NULL },
	{-1,	  -1,	  NULL}
};

struct z80PortWrite cLownsPortWrite[] =
{
	{ 0x01, 0x01, InvadersShiftAmountWrite },//0x02 //1 for clowns
   	{ 0x03, 0x03, InvadersSoundPort3Write },
	{ 0x02, 0x02, InvadersShiftDataWrite }, //0x04 //02 for clowns
   	{ 0x05, 0x05, InvadersSoundPort5Write },
	{-1,	-1,	NULL}
};

struct z80PortWrite gmissilePortWrite[] =
{
	{ 0x01, 0x01, InvadersShiftAmountWrite },
   	{ 0x03, 0x03, InvadersSoundPort3Write },
	{ 0x02, 0x02, InvadersShiftDataWrite }, 
   //	{ 0x05, 0x05, InvadersSoundPort5Write },
	{-1,	-1,	NULL}
};

struct z80PortRead gmissilePortRead[] =
{
	{ 0x0000, 0x0002, GmissilePlayerRead, NULL },
	//{ 0x0002, 0x0002, InvadersDipswitchRead, NULL },
	{ 0x0003, 0x0003, InvadersShiftDataRead, NULL },
	{-1,	  -1,	  NULL}
};

struct z80PortRead astropalPortRead[] =
{
	{ 0x0000, 0x0002, AstroPalPlayerRead, NULL },
	//{ 0x0002, 0x0002, InvadersDipswitchRead, NULL },
	{ 0x0003, 0x0003, InvadersShiftDataRead, NULL },
	{-1,	  -1,	  NULL}
};

ROM_START(astropal)
 ROM_LOAD( "2708.0a",   0x0000, 0x0400, ROM_LOAD_NORMAL)
 ROM_LOAD( "2708.1a",   0x0400, 0x0400, ROM_LOAD_NORMAL)
 ROM_LOAD( "2708.2a",   0x0800, 0x0400, ROM_LOAD_NORMAL)
 ROM_LOAD( "2708.3a",   0x0c00, 0x0400, ROM_LOAD_NORMAL)
 ROM_LOAD( "2708.4a",   0x1000, 0x0400, ROM_LOAD_NORMAL)
 ROM_LOAD( "2708.5a",   0x1400, 0x0400, ROM_LOAD_NORMAL)
 ROM_LOAD( "2708.6a",   0x1800, 0x0400, ROM_LOAD_NORMAL)
 ROM_LOAD( "2708.7a",   0x1c00, 0x0400, ROM_LOAD_NORMAL)
ROM_END

struct roms invaders[] =
{
	{"invaders.h", 0x0000, 0x800, ROM_LOAD_NORMAL},
   	{"invaders.g", 0x0800, 0x800, ROM_LOAD_NORMAL},
	{"invaders.f", 0x1000, 0x800, ROM_LOAD_NORMAL},
	{"invaders.e", 0x1800, 0x800, ROM_LOAD_NORMAL},
	{NULL, 0, 0, 0}
};

struct roms invaddlx[] =
{
	{"invdelux.h", 0x0000, 0x800, ROM_LOAD_NORMAL},
   	{"invdelux.g", 0x0800, 0x800, ROM_LOAD_NORMAL},
	{"invdelux.f", 0x1000, 0x800, ROM_LOAD_NORMAL},
	{"invdelux.e", 0x1800, 0x800, ROM_LOAD_NORMAL},
	{"invdelux.d", 0x4000, 0x800, ROM_LOAD_NORMAL},
	{NULL, 0, 0, 0}
};

struct roms invadpt2[] =
{
	{"pv01", 0x0000, 0x800, ROM_LOAD_NORMAL},
   	{"pv02", 0x0800, 0x800, ROM_LOAD_NORMAL},
	{"pv03", 0x1000, 0x800, ROM_LOAD_NORMAL},
	{"pv04", 0x1800, 0x800, ROM_LOAD_NORMAL},
	{"pv05", 0x4000, 0x800, ROM_LOAD_NORMAL},
	{"pv06.1", 0x5000, 0x400, ROM_LOAD_NORMAL},
	{"pv07.2", 0x5400, 0x400, ROM_LOAD_NORMAL},
	{NULL, 0, 0, 0}
};
/*
struct roms lupin3[] =
{
	{"lp12.bin", 0x0000, 0x800, ROM_LOAD_NORMAL},
   	{"lp13.bin", 0x0800, 0x800, ROM_LOAD_NORMAL},
	{"lp14.bin", 0x1000, 0x800, ROM_LOAD_NORMAL},
	{"lp15.bin", 0x1800, 0x800, ROM_LOAD_NORMAL},
	{"lp16.bin", 0x4000, 0x800, ROM_LOAD_NORMAL},
	{"lp17.bin", 0x4800, 0x800, ROM_LOAD_NORMAL},
	{"lp18.bin", 0x5000, 0x800, ROM_LOAD_NORMAL},
	{NULL, 0, 0, 0}
};
*/
struct roms sicv[] =
{
	{"cv17.bin", 0x0000, 0x800, ROM_LOAD_NORMAL},
   	{"cv18.bin", 0x0800, 0x800, ROM_LOAD_NORMAL},
	{"cv19.bin", 0x1000, 0x800, ROM_LOAD_NORMAL},
	{"cv20.bin", 0x1800, 0x800, ROM_LOAD_NORMAL},
	{"cv01_1.bin", 0x5000, 0x400, ROM_LOAD_NORMAL},
	{"cv02_2.bin", 0x5400, 0x400, ROM_LOAD_NORMAL},
	{NULL, 0, 0, 0}
};

struct roms sitv[] =
{
	{"tv0h.s1", 0x0000, 0x800, ROM_LOAD_NORMAL},
	//{"invaders.h", 0x0000, 0x800, ROM_LOAD_NORMAL},
   	{"tv02.rp1",0x0800, 0x800, ROM_LOAD_NORMAL},
	{"tv03.n1", 0x1000, 0x800, ROM_LOAD_NORMAL},
	{"tv04.m1", 0x1800, 0x800, ROM_LOAD_NORMAL},
	{NULL, 0, 0, 0}
};

struct roms lrescue[] =
{
	{"lrescue.1", 0x0000, 0x800, ROM_LOAD_NORMAL},
	{"lrescue.2",0x0800, 0x800, ROM_LOAD_NORMAL},
	{"lrescue.3", 0x1000, 0x800, ROM_LOAD_NORMAL},
	{"lrescue.4", 0x1800, 0x800, ROM_LOAD_NORMAL},
	{"lrescue.5", 0x4000, 0x800, ROM_LOAD_NORMAL},
	{"lrescue.6", 0x4800, 0x800, ROM_LOAD_NORMAL},
	{"7643-1.cpu", 0x5000, 0x400, ROM_LOAD_NORMAL},
	{"7643-1.cpu", 0x5400, 0x400, ROM_LOAD_NORMAL},
	{NULL, 0, 0, 0}
};
 
struct roms clowns[] =
{
	{"h2.cpu", 0x0000, 0x400, ROM_LOAD_NORMAL},
	{"g2.cpu", 0x0400, 0x400, ROM_LOAD_NORMAL},
	{"f2.cpu", 0x0800, 0x400, ROM_LOAD_NORMAL},
	{"e2.cpu", 0x0c00, 0x400, ROM_LOAD_NORMAL},
	{"d2.cpu", 0x1000, 0x400, ROM_LOAD_NORMAL},
	{"c2.cpu", 0x1400, 0x400, ROM_LOAD_NORMAL},
	{NULL, 0, 0, 0}
};

struct roms test[] =
{
	{"sitest_716.bin", 0x0000, 0x800, ROM_LOAD_NORMAL},
    {"invaders.g", 0x0800, 0x800, ROM_LOAD_NORMAL},
	{"invaders.f", 0x1000, 0x800, ROM_LOAD_NORMAL},
	{"invaders.e", 0x1800, 0x800, ROM_LOAD_NORMAL},
	{NULL, 0, 0, 0}
};

struct roms gmissile[] =
{
	{"gm_623.h", 0x0000, 0x800, ROM_LOAD_NORMAL},
   	{"gm_623.g", 0x0800, 0x800, ROM_LOAD_NORMAL},
	{"gm_623.f", 0x1000, 0x800, ROM_LOAD_NORMAL},
	{"gm_623.e", 0x1800, 0x800, ROM_LOAD_NORMAL},
	{NULL, 0, 0, 0}
};


 void InvadersInterrupt(void)
{

	if (flip)
	{ mz80int(0);}
	else
	{                //32 37
		if (gamenum!=32 && gamenum !=9 )mz80nmi(); //37
	} //9
	flip ^= 1;
}

void Drawscreen(void)
{		
	int i,w;
			
		for(i=0;i!=224;i+=1)
		  {for (w=0;w!=256;w+=1)
		   {if (screenmem [i][w]!=0){
					
				    if (screenmem [i][w]>7) allegro_message("alert!!! %d",screenmem[i][w]);	
			       	
					switch(screenmem [i][w])
						 {
							case 1: glColor4f(1,0,0,1);break;//RED
							case 2: glColor4f(0,0,1,1);break;//BLUE
							case 3: glColor4f(1,0,1,1);break;//MAGENTA
							case 4: glColor4f(0,1,0,1);break;//GREEN
						    case 5: glColor4f(1,1,0,1);break;//YELLOW
							case 6: glColor4f(0,1,1,1);break;//LT Blue
							case 7: glColor4f(1,1,1,1);break;//WHITE
							
							default: glColor4f(1,1,1,1);write_to_log("Unmapped color %x",screenmem[i][w]);break;
						 }		
					
					if (screen_red){glColor4f(1,0,0,1);} 
				    
					if (horiz){glBegin(GL_POINTS);glVertex2f(w*scalef,24+(i*scalef));glEnd();}
					else      {glBegin(GL_POINTS);glVertex2f(24+(i*scalef),(w*scalef));glEnd();}
							
		             }
		   
		    }
		}

}

static void DrawBackground(void)
{
			    glEnable(GL_TEXTURE_2D);
				//glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);
				//glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);
				glLoadIdentity();
				//glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_ONE);
				//glBlendFunc(GL_ONE, GL_ONE);
                //glBlendFunc(GL_DST_ALPHA, GL_ONE); //invaddlx
				glBlendFunc (GL_ONE, GL_DST_COLOR); 
                //glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); 
                //glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);
                //glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA);
				glBindTexture(GL_TEXTURE_2D, art_tex[1]);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				//glBlendColorEXT( 1,1,1,1);
              	//glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, GL_BLEND);
				if (special){glColor4f(.7f,.7f,.7f,1.0f);}
				else {glColor4f(1.0f,1.0f,1.0f,.5f);}
				
				glColor4f(.8f,.8f,.8f,.5f); //invaddlx
		
				glBegin(GL_QUADS);
     			 glTexCoord2f(1,1);glVertex2f(88,768);
				 glTexCoord2f(0,1);glVertex2f(88,0); 
				 glTexCoord2f(0,0);glVertex2f(936,0);
				 glTexCoord2f(1,0);glVertex2f(936,768);
				glEnd();
}

static void DrawOverlay(void)
{
			   glEnable(GL_BLEND); 
	           glEnable(GL_TEXTURE_2D);
			   //glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);
			   //glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);
			   //glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
			   //glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA);
			   //glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
			   //glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
			   glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_COLOR);
			   //glBlendFunc(GL_SRC_ALPHA, GL_ONE); //Only Works for Space invaders
			   //glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); 
               //glBlendFunc(GL_SRC_ALPHA, GL_DST_COLOR);
               // glBlendFunc(GL_ONE, GL_ONE);
			   glBindTexture(GL_TEXTURE_2D, art_tex[2]);
			   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
			   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
						
			   glColor4f(.70f,.70f,.70f,.8f);
							
			glBegin( GL_QUADS );
			glTexCoord2i(0,0); glVertex2f(0,0);
			glTexCoord2i(0,1); glVertex2f(0, 256*scalef);
			glTexCoord2i(1,1); glVertex2f( (256*scalef), 256*scalef);
			glTexCoord2i(1,0); glVertex2f( (256*scalef), 0);
			glEnd();
			glDisable(GL_BLEND);
}

 void InvadersOut(UINT32 addr, UINT8 data, struct MemoryWriteByte *pMemWrite)
{
		
	int b,x,y;
    //c000 for lupin3
	int offset=0x5000;
	// A new update of screen RAM goes here!
	z80.z80Base[addr] = data;
	x=(addr-0x2400)>>5;
	y=(addr-0x2400)&0x1F;
	
	c=7;
	
	if (player2) {offset=0x5400;}
	if (scolor)  {c = z80.z80Base[offset+( 0x80 + y + ((x / 8) << 5))];c=c & 0x07;}
	
	if (horiz)
			for (b=0;b<8;b++)
			  { 
				if(data&0x01)
				screenmem[x][255-((y<<3)+b)]=c; 
			    else
				screenmem[x][255-((y<<3)+b)]=0;
				data=data>>1;
			  }
	        else
			   for (b=0;b<8;b++)
				{ 
				if(data&0x01)
				 screenmem[x][((y<<3)+b)]=c; 
				else
				 screenmem[x][((y<<3)+b)]=0;
				 data=data>>1;
				}
}

UINT16 BootHillShiftDataRead(UINT16 port, struct z80PortRead *pPR)
{
	if (iInvadersShiftAmount < 0x10)
	{return (((((iInvadersShiftData1 << 8) | iInvadersShiftData2) << (iInvadersShiftAmount & 0x07)) >> 8) & 0xff);}
    else
    {    
    	int reverse_data1,reverse_data2;
        
    	/* Reverse the bytes */

        reverse_data1 = ((iInvadersShiftData1 & 0x01) << 7)
                      | ((iInvadersShiftData1 & 0x02) << 5)
                      | ((iInvadersShiftData1 & 0x04) << 3)
                      | ((iInvadersShiftData1 & 0x08) << 1)
                      | ((iInvadersShiftData1 & 0x10) >> 1)
                      | ((iInvadersShiftData1 & 0x20) >> 3)
                      | ((iInvadersShiftData1 & 0x40) >> 5)
                      | ((iInvadersShiftData1 & 0x80) >> 7);

        reverse_data2 = ((iInvadersShiftData2 & 0x01) << 7)
                      | ((iInvadersShiftData2 & 0x02) << 5)
                      | ((iInvadersShiftData2 & 0x04) << 3)
                      | ((iInvadersShiftData2 & 0x08) << 1)
                      | ((iInvadersShiftData2 & 0x10) >> 1)
                      | ((iInvadersShiftData2 & 0x20) >> 3)
                      | ((iInvadersShiftData2 & 0x40) >> 5)
                      | ((iInvadersShiftData2 & 0x80) >> 7);

		return ((((reverse_data2 << 8) | reverse_data1) << (0xff-iInvadersShiftAmount)) >> 8) & 0xff;
    }

}

static UINT16 GmissilePlayerRead(UINT16 port, struct z80PortRead *pPR)
{
	if (port==0)
	{
		return(port0); //f4 //00 //7f clowns
		
        }
        if (port==1)
        {      
                bInvadersPlayer=port1; //0x01 //ff clowns
                if(key[config.kstart1]) bInvadersPlayer=0xbf;//04
                if(key[config.kstart1]) bInvadersPlayer|=0x02;//02
                if(key[config.kcoin1]) bInvadersPlayer=0xfd;//01
                if(key[config.kp1left]) bInvadersPlayer=0xfb;
                if(key[config.kp1right]) bInvadersPlayer=0xf7;
				if(key[config.kp1but1]) bInvadersPlayer=0x7f;
		        return(bInvadersPlayer);
        }
        if (port==2)
        {
                bInvadersPlayer=port2; //0x81 //00 clowns
				
				if(key[config.kp2left]) bInvadersPlayer|=0x20;
                if(key[config.kp2right]) bInvadersPlayer|=0x40;
                if(key[config.kp2but1]) bInvadersPlayer|=0x10;
		        return(bInvadersPlayer);
        }
	
	   if (port==3)
        {
		    allegro_message("shit p2");
			return(0);
		}
	else
		return(bInvadersPlayer);//
}


static UINT16 AstroPalPlayerRead(UINT16 port, struct z80PortRead *pPR)
{
	int p1=0x81;
	int p2=0x1;
	/*
	port 1:
& 0×01 low (coin)
& 0×02 high start 2
& 0×04 high start 1
& 0×08 high forward
& 0×10 high fire
& 0×20 high rotate left
& 0×40 high rotate right
& 0×80 low unk (game doesn’t work without it)
*/
	if (port==0)
	{return(port0); }
		
        
        if (port==1)
        {   
			if(key[config.kstart1])   bitset(p1, 0x04);
            if(key[config.kstart2])   bitset(p1, 0x02);
            if(key[config.kcoin1])    bitclr(p1, 0x01);
            if(key[config.kp1left])   bitset(p1, 0x20);
            if(key[config.kp1right])  bitset(p1, 0x40);
			if(key[config.kp1but1])   bitset(p1, 0x10);
			if(key[config.kp1but2])   bitset(p1, 0x08);
		    return(p1);
        }
        if (port==2)
        {
           	if(key[config.kp2left])  bitset(p2, 0x20);
            if(key[config.kp2right]) bitset(p2, 0x40);
            if(key[config.kp2but1])  bitset(p2, 0x10);
			if(key[config.kp1but2])  bitset(p2, 0x08);
		    return(p2);
        }
	
	   if (port==3)
        {return(0x7f);}
	  
	   return(0);
}




UINT16 boothill_port_0_r(UINT16 port, struct z80PortRead *pPR)
{
    //return (0 & 0x8F) | BootHillTable[0x01 >> 5];
	return port0;
}

UINT16 boothill_port_1_r(UINT16 port, struct z80PortRead *pPR)
{
    //return (0 & 0x8F) | BootHillTable[0x011 >> 5];
	return port1;

}

static UINT16 ClownsPlayerRead(UINT16 port, struct z80PortRead *pPR)
{
    static unsigned char paddle=0x7f;

	if (port==0)
	{   
		if(key[config.kp1right])paddle+=2;if (paddle > 0xf0) {paddle=0xfd;}
		if(key[config.kp1left]) paddle-=2;if (paddle < 0x02) {paddle=0x02;}
		return(paddle); //f4 //00 //7f clowns
		
        }
        if (port==1)
        {      
                bInvadersPlayer=port1; //0x01 //ff clowns
                if(key[config.kstart1]) bInvadersPlayer=0xdf;//04
                if(key[config.kstart2]) bInvadersPlayer=0xef;//02
                if(key[config.kcoin1]) bInvadersPlayer=0xbf;//01
                //if(key[KEY_LEFT]) bInvadersPlayer=0x20;
               // if(key[KEY_RIGHT]) bInvadersPlayer|=0x40;
               // if(key[KEY_LCONTROL]) bInvadersPlayer|=0x10;
		        return(bInvadersPlayer);
        }
        if (port==2)
        {
                bInvadersPlayer=port2; //0x81 //00 clowns
				
				//if(key[KEY_LEFT]) bInvadersPlayer|=0x20;
                //if(key[KEY_RIGHT]) bInvadersPlayer|=0x40;
                //if(key[KEY_LCONTROL]) bInvadersPlayer|=0x10;
		        return(bInvadersPlayer);
        }
	
	   if (port==3)
        {
		    allegro_message("WTF? P2");
			return(bInvadersPlayer);
		}
	else
		return(bInvadersPlayer);

}


UINT16 InvadersPlayerRead(UINT16 port, struct z80PortRead *pPR)
{ 
	
	if (port==0)
	{

		return(port0); //f4 //00 //7f clowns
		
        }
        if (port==1)
        {      
                bInvadersPlayer=port1;  //0x01 //ff clowns
               	if(key[config.kstart1]) bInvadersPlayer|=0x04;//04
                if(key[config.kstart2]) bInvadersPlayer|=0x02;//02
                if(key[config.kcoin1]) bInvadersPlayer&=~0x01;//01
                if(key[config.kp1left]) bInvadersPlayer|=0x20;
                if(key[config.kp1right]) bInvadersPlayer|=0x40;
                if(key[config.kp1but1]) bInvadersPlayer|=0x10;
		        return(bInvadersPlayer);
        }
        if (port==2)
        {
                bInvadersPlayer=port2; //0x81 //00 clowns
				
				if(key[config.kp2left]) bInvadersPlayer|=0x20;
                if(key[config.kp2right]) bInvadersPlayer|=0x40;
                if(key[config.kp2but1]) bInvadersPlayer|=0x10;
		        return( bInvadersPlayer | z80dip1);
        }
	
	   if (port==3)
        {
		    allegro_message("Port Read Port3?");
			return(0);
		}
	else
		return(bInvadersPlayer);//

}

static UINT16 InvadersDipswitchRead(UINT16 port, struct z80PortRead *pPR)
{
	return port2;//bInvadersDipswitch;
}

static void InvadersShiftAmountWrite(UINT16 port, UINT8 data, struct z80PortWrite *pPW)
{
	iInvadersShiftAmount = data;// & 0x07;
}

 static void InvadersShiftDataWrite(UINT16 port, UINT8 data, struct z80PortWrite *pPW)
{
	iInvadersShiftData2 = iInvadersShiftData1;
	iInvadersShiftData1 = data;
}

UINT16 InvadersShiftDataRead(UINT16 port, struct z80PortRead *pPR)
{                                                                                        //& 0x07
	return (((((iInvadersShiftData1 << 8) | iInvadersShiftData2) << (iInvadersShiftAmount )) >> 8) & 0xff);
    //return ((((shift_data1 << 8) | shift_data2) << shift_amount) >> 8) & 0xff;
}



static void do_video()
{
	        int xmin=0;
			int ymin=0;
			int xmax=1024;
			int ymax=768;
	        //SETUP
			glClearColor(0.0f,0.0f,0.0f,0.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glLoadIdentity();
			glViewport(0,0,1023,767);
			glDisable(GL_TEXTURE_2D);
			glDisable(GL_POINT_SMOOTH);
			glDisable(GL_BLEND);			
			glLineWidth(1);
	     	glPointSize(1.0 * scalef);
			glLoadIdentity();
            //DRAW CODE		
			Drawscreen();  //DRAW TEXTURE MAP	
			
			if (special && config.overlay) {DrawOverlay();} //OVERLAY CODE
			    //Copy and clear, replace with buffering!
				glEnable(GL_TEXTURE_2D);
				glDisable(GL_BLEND);
				glBindTexture(GL_TEXTURE_2D, game_tex[1]);
				//glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
				//glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); //nearest!!
			    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0,256*scalef, 256*scalef, 0); //RGBA8?
			    
				glClearColor(0.0f,0.0f,0.0f,0.0f);
				glClear(GL_COLOR_BUFFER_BIT );
				glViewport(0,0,SCREEN_W-1,SCREEN_H-1); //Reset for scaled? 1024/768 view
				glLoadIdentity();
						
		        if (background && config.artwork){DrawBackground();}	 //BACKGROUND CODE
			
				glEnable(GL_BLEND);
				glEnable(GL_DEPTH_TEST);
				//glBlendFunc(GL_SRC_ALPHA, GL_ONE); //Only Works for Space invaders
				glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); 
				if (background && config.artwork){glColor4f(1.0f,1.0f,1.0f,background_blend_val);} //THIS NEEDS ADJUSTMENT
				else {glColor4f(1.0f,1.0f,1.0f,1.0f);}
				
				glColor4f(1.0f,1.0f,1.0f,1.0f);
				 ///////////////////////Bind for final draw
				glBindTexture(GL_TEXTURE_2D,game_tex[1]); 
				if(config.anisfilter)
				//{glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, config.anisfilter); }//ANSITROPIC FILTERING
				{
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
				    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				}
				else
				{
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); 
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				}
				glEnable(GL_ALPHA_TEST);
		        glAlphaFunc(GL_GREATER, 0.01);
               	
										
				if (horiz)
				{    
					
				 glBegin(GL_QUADS);
				 glTexCoord2f(1,0);glVertex2i(0,768);
				 glTexCoord2f(1,1);glVertex2i(0,0); 
				 glTexCoord2f(0,1);glVertex2i(1024,0);
				 glTexCoord2f(0,0);glVertex2i(1024,768);
				 glEnd();
				
				}
				else{
				
				 glTranslatef(128.0f,0.0f,0.0f);
		         glBegin( GL_QUADS );
					glTexCoord2i(0,0); glVertex2f(0,0);
					glTexCoord2i(0,1); glVertex2f(0, 768);
					glTexCoord2i(1,1); glVertex2f( 768, 768);
					glTexCoord2i(1,0); glVertex2f(768, 0);
				 glEnd();
				}
           
				glDisable(GL_ALPHA_TEST);
				glDisable(GL_DEPTH_TEST);

}

static void do_cpu()
{
   UINT32 dwResult = 0;
   int i;
  
       for(i=0;i<2;i++)
		{
		  dwResult = mz80exec(17067);	// Execute 
		  // 0x80000000 Is a "good" execution
		  if (dwResult != 0x80000000)
		   {
			allegro_message("Hit invalid instruction @ address %.4xh\n", dwResult);
			write_to_log("Serious emu error. Hit invalid instruction @ address %.4xh\n", dwResult);
			exit(1);
		   }
		 InvadersInterrupt();
		}

}

static int exec_si()
{
    double frametime= 16.666666;
	static double starttime=0;
	static double gametime=0;

    while (!done)
	{  
		do_cpu(); //Run cpu for specific time
		msg_loop(); //Call main message loop
		do_video(); //Draw Screen
		video_loop(); //Common video stuff handling
		
		gametime=TimerGetTimeMS();
		while (((double)(gametime)-(double)starttime)  < (double) frametime)
		{if ((double)((double)gametime- (double) starttime) < 12 ){rest(1);}(double) gametime=TimerGetTimeMS();}
        
		fps_count =  1000 /((double)(gametime)-(double)starttime);
		(double) starttime=TimerGetTimeMS();
		//Start counting time here. Add time of last flip to total. Fairly accurate!  
		allegro_gl_flip();
	}
    return 0;
}

int init_si(void)
{
	int i;
	int w;

	int goodload=0; 
	int goodloads=0;    
	
    scolor=0;
	bInvadersPlayer = 0x00;
    bInvadersDipswitch = 0x00;
	
	horiz=0;
	flip = 0;
	player2=0;
    background=1;
    special=0;
    lrt_voice=0;
    screenflip=0;
    screen_support_red=1;
    screen_red=0;

	setup_game_config();
	scalef=config.prescale;
	if (config.screenh < 600) {scalef=1;}
    gamefps=60;
	//Clear Video Memory
	for(i=0;i!=224;i++){for(w=0;w!=256;w++){screenmem[i][w]=0;}}
    
	switch (gamenum)
		{
   	case 29:  port0=0;
			  port1=0x1;
			  port2=0x0;
			  background=1;
			  special=1;
			  scolor=0;screen_support_red=0;
			  dips = invaders_dips;
			  init8080(sInvadersRead, sInvadersWrite, sInvadersPortRead, sInvadersPortWrite);
			  goodload=load_roms(gamename[gamenum], invaders);
			  make_single_bitmap( &art_tex[1],"invaders.png","invaders.zip"); 
			  make_single_bitmap( &art_tex[2],"tintover.png","invaders.zip");
			  break;
			  
	case 30: { init8080(sInvadersRead, sInvadersWrite, sInvadersPortRead, sInvadersPortWrite);
		       port0=0x40;port1=0x81;port2=0x00;background=1;scolor=0;screen_support_red=0;special=1;
		       goodload=load_roms(gamename[gamenum], invaddlx);
			   dips = invaddlx_dips;
			   make_single_bitmap( &art_tex[1],"invaddlx.png","invaddlx.zip");
			   make_single_bitmap( &art_tex[2],"tintover.png","invaddlx.zip");
			   break;}
    case 31: { init8080(sInvadersRead, sInvadersWrite, sInvadersPortRead,lRescuePortWrite);
		       port0=0xf4;port1=0x01;port2=0x81;background=0;scolor=1;screen_support_red=1;special=0;
		       dips = invaddlx_dips;
			   goodload=load_roms(gamename[gamenum], lrescue);
			   break;}
	//case 8: {port0=0x0;port1=0x01;port2=0x00;background=0;color=0;screen_support_red=0;overlay=0;LoadRom(test);break;}	
	 case 32: {init8080(sInvadersRead, sInvadersWrite, gmissilePortRead, gmissilePortWrite);
		       port0=0xff;port1=0xff;port2=0xcf;horiz=1;background=0;scolor=0;screen_support_red=0;
			   special=0;
			   dips = invaders_dips;
			   goodload=load_roms(gamename[gamenum], gmissile);break;}
     case 33: {init8080(sInvadersRead, sInvadersWrite, cLownsPortRead, cLownsPortWrite);
		       port0=0x7f;port1=0xff;port2=0x0;horiz=1;special=0;background=0;scolor=0;screen_support_red=0;
		       dips = invaders_dips;
			   goodload=load_roms(gamename[gamenum], clowns);break;}

     case 34: {init8080(sInvadersRead, sInvadersWrite, sInvadersPortRead, sInvadersPortWrite);
		       port0=0xf4;port1=0x01;port2=0x0;background=0;scolor=1;screen_support_red=1;
		        dips = sicv_dips;
			   goodload=load_roms(gamename[gamenum], sicv);
			   make_single_bitmap( &art_tex[1],"sicv.png","sicv.zip");break;}
		
	 case 35: {init8080(sInvadersRead, sInvadersWrite, sInvadersPortRead, sInvadersPortWrite);
		       port0=0x01;port1=0x81;port2=0x00;background=1;scolor=0;screen_support_red=1;
		       dips = sitv_dips;
			   special=1;
			   goodload=load_roms(gamename[gamenum], sitv);
			   make_single_bitmap( &art_tex[1],"invaders.png","sitv.zip");
			   make_single_bitmap( &art_tex[2],"tintover.png","invaders.zip");
			   break;}

     case 36: {init8080(sInvadersRead, sInvadersWrite, sInvadersPortRead, sInvadersPortWrite); 
		       port0=0xf4;port1=0x01;port2=0;background=0;scolor=1;screen_support_red=1;
		       dips = invadpt2_dips;
			   goodload=load_roms(gamename[gamenum], invadpt2);break;}

     case 37: { init8080(sInvadersRead, sInvadersWrite,astropalPortRead,sInvadersPortWrite);
		       port0=0x03;port1=0x81;port2=0x80;background=0;scolor=0;screen_support_red=0;special=0;
			   horiz=1;
		       dips = invaddlx_dips;
			   goodload=load_roms(gamename[gamenum], astropal);break;}
    
     // case 37: {init8080(sInvadersRead, sInvadersWrite, BootHillPortRead, BootHillPortWrite);
	//	       horiz=1;port0=0xbf;port1=0xbf;port2=0xe0;background=1;scolor=0;screen_support_red=0;
	//	       dips = invaders_dips;
	//		   goodload=load_roms(gamename[gamenum], boothill);
      //         make_single_bitmap( &art_tex[1],"boothill.png","boothill.zip");break;}
               //bfbfe0
		}
	
		if (gamenum==31){goodloads=load_samples(lrescue_samples);
		                 lrt_voice=allocate_voice(game_sounds[9]);
	                     voice_set_playmode(lrt_voice, PLAYMODE_PLAY);
                         voice_set_volume(lrt_voice, config.mainvol);
		                  }
		else {goodloads=load_samples(invaders_samples);}
	if (goodloads==0) {write_to_log("Sample loading issue - one or more samples missing ..."); }
	if (goodload==0)  {write_to_log("Rom loading failure, exiting..."); have_error=10; return 0;}
    //CRANK ER UP
	
	retrieve_dips();
	
	setup_ambient(RASTER);

	exec_si(); //Run game
	//EXIT	
	if (gamenum==31){deallocate_voice(lrt_voice);}
	
	iInvadersShiftData1=0;
	iInvadersShiftData2=0;
	iInvadersShiftAmount=0;
	
	special=0;

	save_dips();
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); 
	
	return_to_menu();
	return(0);
}



