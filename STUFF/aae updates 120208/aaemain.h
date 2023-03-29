#ifndef AAEMAIN_H
#define AAEMAIN_H
//#define ALLEGRO_STATICLINK  1
//#define STATICLINK 1
#pragma warning(disable:4996 4102)


#include <stdio.h>
#include <stdlib.h>
#include <allegro.h>
#include "alleggl.h"
#include "winalleg.h"
//#include "m6502.h"
//#include "mz80.h"
#include <malloc.h>
//#include <process.h>
//#include <float.h>
//#include <memory.h>
#include "log.h"
#include "timer.h"
#include "acommon.h"
#include "loaders.h"

//#include "shader.h"
#include "config.h"
#include "globals.h"




//#include "mhavoc.h"
//#include "starwars.h"

/*
#include "raw_mouse.h"
volatile int logic_counter2;
UINT32 dwElapsedTicks;
#define str_eq(s1,s2)  (!strcmp ((s1),(s2))); //Equate string1 and sring2 true is equal
#define EPSILON 0.0001   // Define your own tolerance
#define FLOAT_EQ(x,v) (((v - EPSILON) < x) && (x <( v + EPSILON)))

INLINE int twos_comp_val(int num, int bits)
{	//return (INT32)(num << (32 - bits)) >> (32 - bits);
    return (num << (32 - bits)) >> (32 - bits);
}


#ifndef UINT32
#define UINT32  unsigned long int
#endif

#ifndef UINT16
#define UINT16  unsigned short int
#endif

#ifndef UINT8
#define UINT8   unsigned char
#endif

#define VECTOR 500
#define RASTER 501

#define PI 3.1418


#define memrdwd(address) ((gameImage[pc]) | (gameImage[pc+1]<<8)) 
#define MAXSTACK 8
#define VEC_SHIFT 16
#define NORMAL      1
#define FLIP        2
#define RRIGHT      3
#define RLEFT       4

#define bitget(p,m) ((p) & (m))
#define bitset(p,m) ((p) |= (m))
#define bitclr(p,m) ((p) &= ~(m))
#define bitflp(p,m) ((p) ^= (m))
#define bit_write(c,p,m) (c ? bit_set(p,m) : bit_clear(p,m))
#define BIT(x) (0x01 << (x))
#define LONGBIT(x) ((unsigned long)0x00000001 << (x))


#define VCTR 0
#define HALT 1
#define SVEC 2
#define STAT 3
#define CNTR 4
#define JSRL 5
#define RTSL 6
#define JMPL 7
#define SCAL 8


#define KEY_NORMAL       0
#define KEY_IMPULSE      1
#define KEY_REPEAT       2
#define KEY_SHFT_REPEAT  3

#define KEY_RATE_NORMAL 10
#define KEY_RATE_FAST 8


#define START1		0
#define START2		1
#define START3		2
#define START4		3
#define COIN1		4
#define COIN2		5
#define P1RIGHT		6 //RIGHT
#define P1LEFT		7 //LEFT
#define P1UP		8 //UP
#define P1DOWN		9 //DOWN
#define P1BUTTON1	10 //LCONTROL
#define P1BUTTON2	11 //LALT
#define P1BUTTON3	12 //SPACE
#define P1BUTTON4	13 //LSHIFT
#define P1BUTTON5	14 //Z
#define P1BUTTON6	15 //X

#define TEST_ADV    16
#define TEST        17
#define RESET       18
#define SNAP        19
#define SHOW_FPS    20
#define PAUSE       21
#define MENU        22
#define QUIT        23



#define P2RIGHT		24 //G
#define P2LEFT		25 //D
#define P2UP		26 //R
#define P2DOWN		27 //F
#define P2BUTTON1	28 //A
#define P2BUTTON2	29 //S
#define P2BUTTON3	30 //Q
#define P2BUTTON4	31 //W
#define P2BUTTON5	32 //L
#define P2BUTTON6	33 //K
#define PAD_0		34
#define PAD_1		35
#define PAD_2		36
#define PAD_3		37
#define PAD_4		38
#define PAD_5		39
#define PAD_6		40
#define PAD_7		41
#define PAD_8		42
#define PAD_9		43


#define J0S0_DigiLeft	0
#define J0S0_DigiRight  1 
#define J0S0_DigiUp		2
#define J0S0_DigiDown	3
#define J0S0_PlusX		4
#define J0S0_MinusX		5
#define J0S0_PlusY		6
#define J0S0_MinusY		7
#define J0S1_DigiLeft	8
#define J0S1_DigiRight	9
#define J0S1_DigiUp		10
#define J0S1_DigiDown	11
#define J0S1_PlusX		12
#define J0S1_MinusX		13
#define J0S1_PlusY		14
#define J0S1_MinusY		15
#define J0S2_DigiLeft	16
#define J0S2_DigiRight	17
#define J0S2_DigiUp		18
#define J0S2_DigiDown	19
#define J0S2_PlusX		20
#define J0S2_MinusX		21
#define J0S2_PlusY		22
#define J0S2_MinusY		23
#define J0S3_DigiLeft	24
#define J0S3_DigiRight	25
#define J0S3_DigiUp		26
#define J0S3_DigiDown	27
#define J0S3_PlusX		28
#define J0S3_MinusX		29
#define J0S3_PlusY		30
#define J0S3_MinusY		31
#define J0Btn0			32
#define J0Btn1			33
#define J0Btn2			34
#define J0Btn3			35
#define J0Btn4			36
#define J0Btn5			37
#define J0Btn6			37
#define J0Btn7			39
#define J0Btn8			40
#define J0Btn9			41
#define J0Btn10			42
#define J0Btn11			43

#define J1S0_DigiLeft   64
#define J1S0_DigiRight	65
#define J1S0_DigiUp		66
#define J1S0_DigiDown	67
#define J1S0_PlusX		68
#define J1S0_MinusX		69
#define J1S0_PlusY		70
#define J1S0_MinusY		71
#define J1S1_DigiLeft	72
#define J1S1_DigiRight	73
#define J1S1_DigiUp		74
#define J1S1_DigiDown	75
#define J1S1_PlusX		76
#define J1S1_MinusX		77
#define J1S1_PlusY		78
#define J1S1_MinusY		79
#define J1S2_DigiLeft	80
#define J1S2_DigiRight	81
#define J1S2_DigiUp		82
#define J1S2_DigiDown	83
#define J1S2_PlusX		84
#define J1S2_MinusX		85
#define J1S2_PlusY		86
#define J1S2_MinusY		87
#define J1S3_DigiLeft	88
#define J1S3_DigiRight	89
#define J1S3_DigiUp		90
#define J1S3_DigiDown	91
#define J1S3_PlusX		92
#define J1S3_MinusX		93
#define J1S3_PlusY		94
#define J1S3_MinusY		95
#define J1Btn0			96
#define J1Btn1			97
#define J1Btn2			98
#define J1Btn3			99
#define J1Btn4			100
#define J1Btn5			101
#define J1Btn6			102
#define J1Btn7			103
#define J1Btn8			103
#define J1Btn9			105
#define J1Btn10			106
#define J1Btn11			107

#define J2S0_DigiLeft	128
#define J2S0_DigiRight	129
#define J2S0_DigiUp		130
#define J2S0_DigiDown	131
#define J2S0_PlusX		132
#define J2S0_MinusX		133
#define J2S0_PlusY		134
#define J2S0_MinusY		135
#define J2S1_DigiLeft	136
#define J2S1_DigiRight	137
#define J2S1_DigiUp		138
#define J2S1_DigiDown	139
#define J2S1_PlusX		140
#define J2S1_MinusX		141
#define J2S1_PlusY		142
#define J2S1_MinusY		143
#define J2S2_DigiLeft	144
#define J2S2_DigiRight	145
#define J2S2_DigiUp		146
#define J2S2_DigiDown	147
#define J2S2_PlusX		148
#define J2S2_MinusX		149
#define J2S2_PlusY		150
#define J2S2_MinusY		151
#define J2S3_DigiLeft	152
#define J2S3_DigiRight	153
#define J2S3_DigiUp		154
#define J2S3_DigiDown	155
#define J2S3_PlusX		156
#define J2S3_MinusX		157
#define J2S3_PlusY		158
#define J2S3_MinusY		159
#define J2Btn1			160
#define J2Btn2			161
#define J2Btn3			162
#define J2Btn4			163
#define J2Btn5			164
#define J2Btn6			165
#define J2Btn7			166
#define J2Btn8			167
#define J2Btn9			168
#define J2Btn10			169
#define J2Btn11			170
#define J2Btn12			171

#define J3S0_DigiLeft	192
#define J3S0_DigiRight	193
#define J3S0_DigiUp		194
#define J3S0_DigiDown	195
#define J3S0_PlusX		196
#define J3S0_MinusX		197
#define J3S0_PlusY		198
#define J3S0_MinusY		199
#define J3S1_DigiLeft	200
#define J3S1_DigiRight	201
#define J3S1_DigiUp		202
#define J3S1_DigiDown	203
#define J3S1_PlusX		204
#define J3S1_MinusX		205
#define J3S1_PlusY		206
#define J3S1_MinusY		207
#define J3S2_DigiLeft	208
#define J3S2_DigiRight	209
#define J3S2_DigiUp		210
#define J3S2_DigiDown	211
#define J3S2_PlusX		212
#define J3S2_MinusX		213
#define J3S2_PlusY		214
#define J3S2_MinusY		215
#define J3S3_DigiLeft	216
#define J3S3_DigiRight	217
#define J3S3_DigiUp		218
#define J3S3_DigiDown	219
#define J3S3_PlusX		220
#define J3S3_MinusX		221
#define J3S3_PlusY		222
#define J3S3_MinusY		223
#define J3Btn1			224
#define J3Btn2			225
#define J3Btn3			226
#define J3Btn4			227
#define J3Btn5			228
#define J3Btn6			229
#define J3Btn7			230
#define J3Btn8			231
#define J3Btn9			232
#define J3Btn10			233
#define J3Btn11			234
#define J3Btn12			235



typedef unsigned char byte;
typedef unsigned short int word;
typedef unsigned long int dword;
typedef BYTE * pBYTE;
typedef WORD * pWORD;

typedef struct {
      
    int next; // index of next entry in array
    int prev; // previous entry (if double-linked)    
	int  gamenum; 		//Short Name of game
    char glname[128];	    //Display name for Game  
	int extopt;   //Any extra options for each game  
	//int numbertag;
 } glist;                      //Only one gamelist at a time

glist gamelist[256];




#define READ_HANDLER(name)  static UINT8 name(UINT32 address, struct MemoryReadByte *psMemRead)
#define WRITE_HANDLER(name)  static void name(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)
#define MEM_WRITE(name) struct MemoryWriteByte name[] = {
#define MEM_READ(name)  struct MemoryReadByte name[] = {
#define MEM_ADDR(start,end,routine) {start,end,routine},
#define MEM_END {(UINT32) -1,(UINT32) -1,NULL}};


#define PORT_WRITE_HANDLER(name) static void name(UINT16 port, UINT8 data, struct z80PortWrite *pPW)
#define PORT_READ_HANDLER(name) static UINT16 name(UINT16 port, struct z80PortRead *pPR)
#define PORT_WRITE(name) struct z80PortWrite name[] = {
#define PORT_READ(name) struct z80PortRead name[] = {
#define PORT_ADDR(start,end,routine) {start,end,routine},
#define PORT_END { -1, -1,NULL}};

/*
struct driver
{
	char *gamename;
	struct roms *rominfo;

};
*/
/*
typedef struct {
char *name;          //Name of value 
char *valname[50];  //List of value names, if used. Must be fully initalized.
int use_valname;    //Whether to use the above name
int saveflt;        //Determine whether the final value is stored as float or int
float valmin;       //Minimum value
float valmax;       //Maximum value
float valstep;      //Step Value

float curr;         //Current Value
float prev;         //Previous value
float def;      //Default Value
} MY_Menu2; 
MY_Menu2 basic_menu[25];		
*/
/*
typedef struct {

char *heading;
char *values[6];
int value_num[6];
int  valpoint;

} MY_Video; 
MY_Video video_menu[25];		

#define HEADING_SIZE    (11)
#define OPTION_SIZE     (12)
#define NUM_SWITCHES    (5)

typedef char *HEADING;
typedef char *OPTIONS;

typedef struct
{
   HEADING Heading;
   OPTIONS Options[10];
   int NumOptions;
   int Changed; //changed
   int Default;
   int Current;
   int Value[10];
} MENUSWITCH;

typedef struct
{
   HEADING Heading;
   OPTIONS Options[20];
   int NumOptions;
   int Changed;
   int Default;
   int Current;
   int Value[20];
} SOUNDSWITCH;



/*   FINAL REVISION ???
typedef struct
{
   HEADING Heading;
   OPTIONS Options[8];
   int NumOptions;
   int MENU_TYPE MENU_INT MENU_FLOAT
   float Changed;
   float Default;
   float Current;
   float Step; /
   int Max;
   int Min;
   int Value[8];
} SOUNDSWITCH;






typedef struct
{
   HEADING Heading;
   OPTIONS Options[8];
   int NumOptions;
   int Switch;
   int Mask;
   int Default;
   int Current;
  // int Changed;
   int Value[8];
} DIPSWITCH;

DIPSWITCH *dips;

typedef struct
{
 HEADING Heading;
 int number;   //"Key def number"
 int Default;  //KEY_LCNTROL
 int Current;  //KEY_SHIFT
 int type;     //KEY_NORMAL, KEY_IMPULSE, KEY_REPEAT, KEY_SHFT_REPEAT
 int count;    //Repeat count, set according to type.
 int Joy;      //JOY_NO  
 int Joytype;  //JOY_DIGITAL
 int JoyMin;   // 01 or Zero if digital else analog
 int JoyMax;   // 01 or Zero if digital else analog
 int Mouse;    //MOUSE_NO
} GAMEKEYS;

GAMEKEYS *gamekeys;

// { {"Player 1 Start"},START1,KEY_1,KEY_1,KEY_NORMAL,0,J0Btn9,0,-128,128,0 },
#define GKEY_START(name) static GAMEKEYS name[] = {
#define GKEY_SET(Heading,number,Default,Current,type,count,Joy,JoyType,JoyMin,JoyMax,Mouse)  {Heading,number,Default,Current,type,count,Joy,JoyType,JoyMin,JoyMax,Mouse},
#define GKEY_END  {{"NONE"},-1,-1,-1,-1,-1,-1,-1,-1 }};
	 //static DIPSWITCH astdelux_dips[] = {
#define DIP_START(name) DIPSWITCH name[] = {
//#define DIP_SET(Heading,Options,Options,Options,Options,Options,Options,Options,Options,NumOptions,Switch,Mask,Default,Current,Value,Value,Value,Value,Value,Value,Value,Value) { Heading,{Options[0],Options[1],Options[2],Options[3],Options[4],Options[5],Options[6]Options[7]},NumOptions,Switch,Mask,Default,Current,{Value[0],Value[1],Value[2],Value[3],Value[4],Value[5],Value[6],Value[7]} },
#define DIP_END {"NONE",{"NONE","NONE","NONE","NONE","NONE","NONE","NONE","NONE"},0,0,0,0,0,{0,0,0,0,0,0,0,0}}};

/*
typedef struct
{
   HEADING Heading;
   OPTIONS Options[8];
   int NumOptions; //This is the number of options available.
   int Switch; //This is the config value being set.
   int Swtype; //float //int , etc //Actual switch will need to be defined according to this.
   int  Step;   //Granularity
   int Default; //This is the value set originally, is this needed?
   int Current;
   int Value[8]; //These are the values to range from?
} MENUOPTS;

MENUOPTS menuopts;


typedef struct {
	int vidwidth;
	int vidheight;
	int vidbpp;
	int vidfin;
	int curvid; //only stored in 51 ;)
	int linewidth; //current val in 51
	int pointsize; //currentval in 51
    
} VIDMODES;
VIDMODES vidmodes[51];


typedef struct {

int now;
int latch;
int latch_count;
int latch_expire;

} MY_Input; 
MY_Input the_input[256];		

typedef struct {
  
char rompath[256];

int ksfps;
int kquit;
int kreset;
int ktest;
int ktestadv;
int kmenu;
int kcoin1;
int kcoin2;
int kstart1;
int kstart2;
int kstart3;
int kstart4;

int kpause;
int ksnap;
int klstate;
int ksstate;

int kp1left;
int kp1right;
int kp1up;
int kp1down;
int kp1but1;
int kp1but2;
int kp1but3;
int kp1but4;
int kp1but5;
int kp1but6;

int kp2left;
int kp2right;
int kp2up;
int kp2down;
int kp2but1;
int kp2but2;
int kp2but3;
int kp2but4;
int kp2but5;
int kp2but6;

int pad0;
int pad1;
int pad2;
int pad3;
int pad4;
int pad5;
int pad6;
int pad7;
int pad8;
int pad9;

int j1left;
int j1right;
int j1up;
int j1down;
int j1but1;
int j1but2;
int j1but3;
int j1but4;
int j1but5;
int j1but6;
int j1but7;
int j1but8;
int j1but9;
int j1but10;
int j1but11;
int j1but12;

int j2left;
int j2right;
int j2up;
int j2down;
int j2but1;
int j2but2;
int j2but3;
int j2but4;
int j2but5;
int j2but6;
int j2but7;
int j2but8;
int j2but9;
int j2but10;
int j2but11;
int j2but12;

int sampling;
int widescreen;
int overlay;
int colordepth;
int screenw;
int screenh;
int windowed;
int language;
int translucent;
float translevel;    
int lives; 

int m_line;
int m_point;
int monitor;

float linewidth;
float pointsize;

int gamma;
int bright;
int contrast;

int fire_point_size;
int explode_point_size;
//int colorhack;
int shotsize;
int cocktail;
int mainvol;
int pokeyvol;
int artwork;
int bezel;
int burnin;
int artcrop;
int vid_rotate;
int vecglow;
int vectrail;
int mouse1xs;
int mouse1ys;
int mouse1x_invert;
int mouse1y_invert;
int mouse1b1;
int mouse1b2;
int psnoise;
int hvnoise;
int pshiss;
int noisevol;
int snappng;
int showfps;
int prescale;
int anisfilter;
int priority;
int forcesync;
int dblbuffer;
int showinfo; //Show readme info message
char *exrompath; //optional path for roms 
int hack;

}settings;

settings config;

//CPU CONTEXT DEFINITIONS
CONTEXTM6502 *psCpu1;
CONTEXTM6502 *psCpu2;

struct mz80context z80;
struct mz80context z802;

SAMPLE *game_sounds[55]; //Global Samples

AUDIOSTREAM *stream; //Global Streaming Sound
BYTE *soundbuffer;
signed char *aybuffer;

int frames; //Global Framecounter
int testsw; //testswitch for many games
unsigned char *gameImage; //Global 6502/Z80/6809 gameImage 1
unsigned char *gameImage2; //Global 6502/Z80/6809 gameImage 2
unsigned char sw1[0x12000];
unsigned char sw2[0x10000];


int gamenum; //Global Gamenumber (really need this one)
int have_error; //Global Error handler
int showinfo; //Global info handler
int done; //End of emulation indicator
int paused; //Paused indicator
double fps_count; //FPS Counter
int mickeyx;   //MouseX var
int mickeyy;   //MouseY var
int showfps;   //ShowFPS Toggle
int show_menu; //ShowMenu Toggle
int showifo; //No clue what this does
//int menulevel;
int mmouse; //Mouse move pointer
int scalef; //SCALING FACOR FOR RASTER GAMES
int gamefps; //GAME REQUIRED FPS
int num_games; //Total number of games ?? needed?
int num_samples; //Total number of samples for selected game
static int keys[256];
int my_j[256];

int my_input[256];

int rotationcount;
int keyin;
//int sdwrite;
int menulevel;//Top Level
int menuitem; //TOP VAL
int key_set_flag;
int total_length;

int chip;  //FOR POKEY              FIX!!!!!!!!!!!!!!!!!!
int gain;  //FOR POKEY  
int BUFFER_SIZE;  //FOR POKEY
int garbage;
int z80dip1;
int z80dip2;
int z80dip3;
int z80dip4;
//int CPUTYPE;
unsigned char *membuffer;
//Prototypes

int init_gui(void);
int init_asteroid(void);

int init_astdelux(void);
int init_llander(void);
int init_tempest(void);
int init_bzone(void);
int init_spacduel(void);
int init_orace(void);
int init_segag80(void);
int init_quantum(void);
int init_cinemat(void);
int init_mhavoc(void);


typedef struct colorsarray {float r,g,b;} colors; 
colors vec_colors[100];

typedef struct segacolors {float c,r,g,b;} segacolor; 
segacolor sega_colors[256];
*/
int mystrcmp(const char *s1, const char *s2);
void sort_games(void);
void run_game(void);
//void init6502(struct MemoryReadByte *read, struct MemoryWriteByte *write, int DataSize);
//void init6502Z(struct MemoryReadByte *read, struct MemoryWriteByte *write);
//void init6502_2(struct MemoryReadByte *read, struct MemoryWriteByte *write, int DataSize);
//void initz80(struct MemoryReadByte *read, struct MemoryWriteByte *write, struct z80PortRead *portread, struct z80PortWrite *portwrite);
//void initz80_2(struct MemoryReadByte *read, struct MemoryWriteByte *write, struct z80PortRead *portread, struct z80PortWrite *portwrite);
//void init8080(struct MemoryReadByte *read, struct MemoryWriteByte *write, struct z80PortRead *portread, struct z80PortWrite *portwrite);
void reset_to_default_keys();
void ListDisplaySettings(void);
void SetGammaRamp(double gamma, double bright, double contrast);
void reset_for_new_game();

#endif