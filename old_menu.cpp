#include "menu.h"
#include "globals.h"
#include "glcode.h"
#include "dips.h"
#include "fonts.h"
#include "input.h"
#include "math.h"
#include "gamekeys.h"
#include "acommon.h"
#include "config.h"
#include "loaders.h"

#define MENU_INT 0
#define MENU_FLOAT 1

int sublevel=0;
int num_this_menu=0;
int currentval=0;
int val_low=0;
int val_high=0;
int it=1;
static int number=0;

typedef struct
{
   char *Heading;
   char *Options[20];
   int NumOptions;
   int Changed; //changed
   int Default;
   int Current;
   int Value[20];
   int menu_type;// MENU_INT MENU_FLOAT
   float Step; 
   int Min;
   int Max;
 } MENUS;

char *mouse_names[] =
{
  "(none)",  "MB1","MB2","MB3","MB4"
};


char *key_names[] =
{
   "(none)",     "A",          "B",          "C",
   "D",          "E",          "F",          "G",
   "H",          "I",          "J",          "K",
   "L",          "M",          "N",          "O",
   "P",          "Q",          "R",          "S",
   "T",          "U",          "V",          "W",
   "X",          "Y",          "Z",          "0",
   "1",          "2",          "3",          "4",
   "5",          "6",          "7",          "8",
   "9",          "0_PAD",      "1_PAD",      "2_PAD",
   "3_PAD",      "4_PAD",      "5_PAD",      "6_PAD",
   "7_PAD",      "8_PAD",      "9_PAD",      "F1",
   "F2",         "F3",         "F4",         "F5",
   "F6",         "F7",         "F8",         "F9",
   "F10",        "F11",        "F12",        "ESC",
   "TILDE",      "MINUS",      "EQUALS",     "BACKSPACE",
   "TAB",        "OPENBRACE",  "CLOSEBRACE", "ENTER",
   "COLON",      "QUOTE",      "BACKSLASH",  "BACKSLASH2",
   "COMMA",      "STOP",       "SLASH",      "SPACE",
   "INSERT",     "DEL",        "HOME",       "END",
   "PGUP",       "PGDN",       "LEFT",       "RIGHT",
   "UP",         "DOWN",       "SLASH_PAD",  "ASTERISK",
   "MINUS_PAD",  "PLUS_PAD",   "DEL_PAD",    "ENTER_PAD",
   "PRTSCR",     "PAUSE",      "ABNT_C1",    "YEN",
   "KANA",       "CONVERT",    "NOCONVERT",  "AT",
   "CIRCUMFLEX", "COLON2",     "KANJI",      "EQUALS_PAD",
   "BACKQUOTE",  "SEMICOLON",  "COMMAND",    "UNKNOWN1",
   "UNKNOWN2",   "UNKNOWN3",   "UNKNOWN4",   "UNKNOWN5",
   "UNKNOWN6",   "UNKNOWN7",   "UNKNOWN8",   "LSHIFT",
   "RSHIFT",     "LCONTROL",   "RCONTROL",   "ALT",
   "ALTGR",      "LWIN",       "RWIN",       "MENU",
   "SCRLOCK",    "NUMLOCK",    "CAPSLOCK",   "MAX"
};

char *joy_names[] =
{
   "(none)","J0S0_DigiLeft",
 "J0S0_DigiRight",
 "J0S0_DigiUp",
 "J0S0_DigiDown",
 "J0S0_PlusX",
 "J0S0_MinusX",
 "J0S0_PlusY",
 "J0S0_MinusY",
 "J0S1_DigiLeft",
 "J0S1_DigiRight",
 "J0S1_DigiUp",
 "J0S1_DigiDown",
 "J0S1_PlusX",
 "J0S1_MinusX",
 "J0S1_PlusY",
 "J0S1_MinusY",
 "J0S2_DigiLeft",
 "J0S2_DigiRight",
 "J0S2_DigiUp",
 "J0S2_DigiDown",
 "J0S2_PlusX",
 "J0S2_MinusX",
 "J0S2_PlusY",
 "J0S2_MinusY",
 "J0S3_DigiLeft",
 "J0S3_DigiRight",
 "J0S3_DigiUp",
 "J0S3_DigiDown",
 "J0S3_PlusX",
 "J0S3_MinusX",
 "J0S3_PlusY",
 "J0S3_MinusY",
 "J0B0",
 "J0B1",
 "J0B2",
 "J0B3",
 "J0B4",
 "J0B5",
 "J0B6",
 "J0B7",
 "J0B8",
 "J0B9",
 "J0B10",
 "J0B11",

 "J1S0_DigiLeft",
 "J1S0_DigiRight",
 "J1S0_DigiUp",
 "J1S0_DigiDown",
 "J1S0_PlusX",
 "J1S0_MinusX",
 "J1S0_PlusY",
 "J1S0_MinusY",
 "J1S1_DigiLeft",
 "J1S1_DigiRight",
 "J1S1_DigiUp",
 "J1S1_DigiDown",
 "J1S1_PlusX",
 "J1S1_MinusX",
 "J1S1_PlusY",
 "J1S1_MinusY",
 "J1S2_DigiLeft",
 "J1S2_DigiRight",
 "J1S2_DigiUp",
 "J1S2_DigiDown",
 "J1S2_PlusX",
 "J1S2_MinusX",
 "J1S2_PlusY",
 "J1S2_MinusY",
 "J1S3_DigiLeft",
 "J1S3_DigiRight",
 "J1S3_DigiUp",
 "J1S3_DigiDown",
 "J1S3_PlusX",
 "J1S3_MinusX",
 "J1S3_PlusY",
 "J1S3_MinusY",
 "J1B0",
 "J1B1",
 "J1B2",
 "J1B3",
 "J1B4",
 "J1B5",
 "J1B6",
 "J1B7",
 "J1B8",
 "J1B9",
 "J1B10",
 "J1B11",

 "J2S0_DigiLeft",
 "J2S0_DigiRight",
 "J2S0_DigiUp",
 "J2S0_DigiDown",
 "J2S0_PlusX",
 "J2S0_MinusX",
 "J2S0_PlusY",
 "J2S0_MinusY",
 "J2S1_DigiLeft",
 "J2S1_DigiRight",
 "J2S1_DigiUp",
 "J2S1_DigiDown",
 "J2S1_PlusX",
 "J2S1_MinusX",
 "J2S1_PlusY",
 "J2S1_MinusY",
 "J2S2_DigiLeft",
 "J2S2_DigiRight",
 "J2S2_DigiUp",
 "J2S2_DigiDown",
 "J2S2_PlusX",
 "J2S2_MinusX",
 "J2S2_PlusY",
 "J2S2_MinusY",
 "J2S3_DigiLeft",
 "J2S3_DigiRight",
 "J2S3_DigiUp",
 "J2S3_DigiDown",
 "J2S3_PlusX",
 "J2S3_MinusX",
 "J2S3_PlusY",
 "J2S3_MinusY",
 "J2B1",
 "J2B2",
 "J2B3",
 "J2B4",
 "J2B5",
 "J2B6",
 "J2B7",
 "J2B8",
 "J2B9",
 "J2B10",
 "J2B11",
 "J2B12",

 "J3S0_DigiLeft",
 "J3S0_DigiRight",
 "J3S0_DigiUp",
 "J3S0_DigiDown",
 "J3S0_PlusX",
 "J3S0_MinusX",
 "J3S0_PlusY",
 "J3S0_MinusY",
 "J3S1_DigiLeft",
 "J3S1_DigiRight",
 "J3S1_DigiUp",
 "J3S1_DigiDown",
 "J3S1_PlusX",
 "J3S1_MinusX",
 "J3S1_PlusY",
 "J3S1_MinusY",
 "J3S2_DigiLeft",
 "J3S2_DigiRight",
 "J3S2_DigiUp",
 "J3S2_DigiDown",
 "J3S2_PlusX",
 "J3S2_MinusX",
 "J3S2_PlusY",
 "J3S2_MinusY",
 "J3S3_DigiLeft",
 "J3S3_DigiRight",
 "J3S3_DigiUp",
 "J3S3_DigiDown",
 "J3S3_PlusX",
 "J3S3_MinusX",
 "J3S3_PlusY",
 "J3S3_MinusY",
 "J3Btn1",
 "J3Btn2",
 "J3Btn3",
 "J3Btn4",
 "J3Btn5",
 "J3Btn6",
 "J3Btn7",
 "J3Btn8",
 "J3Btn9",
 "J3Btn10",
 "J3Btn11",
 "J3Btn12",
};

static MENUS glmenu[] = {
   { "WINDOWED ", {"NO", "YES","","","","","","","",""},
   1, 100,1,1,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},	
   { "RESOLUTION ", {"640x480x32", "800x600x32","1024x768x32","1152x864x32","1280x1024","1600x1200","","","",""},
   5, 200,1,1,{0,1,2,3,4,5,0,0,0,0},MENU_INT,0,0,0}, 
   
   { "GAMMA ADJUST ", {"-8%","-5%","-3%","0%","+3%","+5%","+8%","+12%","+15","+18"},
   9, 300,0,0,{157,147,137,127,117,107,100,97,94,90},MENU_FLOAT,.5,-20,20},
   { "BRIGHTNESS ADJ", {"-8%","-5%","-3%","0%","+3%","+5%","+8%","+12%","+15","+18"},
   9, 300,0,0,{113,117,122,127,134,140,144,149,155,161},MENU_FLOAT,.5,-20,20},
   { "CONTRAST ADJ", {"-8%","-5%","-3%","0%","+3%","+5%","+8%","+12%","+15","+18"},
   9, 300,0,0,{157,147,137,127,117,107,100,97,94,90},MENU_FLOAT,.5,-20,20},
   
   { "VSYNC", {"DISABLED", "ENABLED","","","","","","","",""},
   1, 400,1,1,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},	
   { "DRAW 0 LINES", {"DISABLED", "ENABLED","","","","","","","",""},
   1, 500,3,3,{0,1,2,4,6,0,0,0,0,0},MENU_INT,0,0,0},	
   { "GAME ASPECT", {"STRETCH","4:3 on 16:9","4:3 on 16:10","","","","","","",""},
   2, 600,0,0,{0,1,2,0,0,0,0,0,0,0},MENU_INT,0,0,0},	
   { "PHOSPHER TRAIL", {"NONE", "LITTLE","MORE","MAX","","","","","",""},
   3, 700,0,0,{0,1,2,3,0,0,0,0,0,0},MENU_INT,0,0,0},	
   { "VECTOR GLOW", {"NO", "YES","MORE","MAX","","","","","",""},
   1, 800,0,0,{0,1,2,3,0,0,0,0,0,0},MENU_FLOAT,1,0,25},	
   { "LINEWIDTH", {"NO", "YES","","","","","","","",""},
   8, 900,0,0,{0,0,0,0,0,0,0,0,0,0},MENU_FLOAT,.1,1,7},	
   { "POINTSIZE", {"NO", "YES","","","","","","","",""},
   8, 1000,0,0,{0,0,0,0,0,0,0,0,0,0},MENU_FLOAT,.1,1,7},	
   { "MONITOR GAIN", {"ANALOG", "LCD","","","","","","","",""},
   1, 1100,0,0,{0,1,0,0,0,0,0,0,0,0},MENU_FLOAT,1,-127,127},	
   { "ARTWORK", {"NO", "YES","","","","","","","",""},
   1, 1200,1,1,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},
   { "OVERLAY", {"NO", "YES","","","","","","","",""},
   1, 1300,1,1,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},	
   { "BEZEL ART", {"NO", "YES","","","","","","","",""},
   1, 1400,1,1,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},
   { "CROP BEZEL", {"NO", "YES","","","","","","","",""},
   1, 1500,1,1,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},	
   { "TBA", {"NO", "YES","","","","","","","",""},
   1, 1600,0,0,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},	
   { "PRIORITY", {"LOW", "NORMAL","ABOVE NORMAL","HIGH","DANGER!","","","","",""},
   4, 1700,1,1,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},	
   { "KB LEDS", {"DISABLED", "ENABLED","","","","","","","",""},
   1, 1800,1,1,{0,1,0,0,0,0,0,0,0,0}},	
   { "NONE", {"NONE", " ", " ", " "}, 0,0, 0,0,{0,0,0,0,0,0,0,0,0,0},0,0,0,0}
};

static MENUS soundmenu[] = {
   { "MAIN VOLUME",  {"NO", "YES","","","","","","","",""},
   1, 100,0,0,{0,0,0,0,0,0,0,0,0,0},MENU_FLOAT,5,0,20},
   { "POKEY/AY VOLUME", {"NO", "YES","","","","","","","",""},
   1, 200,0,0,{0,0,0,0,0,0,0,0,0,0},MENU_FLOAT,5,0,20},
   { "AMBIENT VOLUME", {"NO", "YES","","","","","","","",""},
   1, 200,0,0,{0,0,0,0,0,0,0,0,0,0},MENU_FLOAT,5,0,20},
   { "HV CHATTER", {"NO", "YES","","","","","","","","","","","","","","","","","",""},
   1, 300,0,0,{0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},
   { "PS HISS", {"NO", "YES","","","","","","","","","","","","","","","","","",""},
   1, 400,0,0,{0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},	
   { "PS NOISE",{"NO", "YES","","","","","","","","","","","","","","","",""},
   1,500,0,0,{0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},
   { "NONE", {"NONE", " ", " ", " "}, 0,0, 0,0,{0,0,0,0,0,0,0,0,0,0},0,0,0,0}
};

static MENUS mousemenu[] = {
 { "MOUSE X SENSITIVITY ", {"1", "2","3","4","5","6","7","8","9",""},
   8, 100,2,2,{0,1,2,3,4,5,6,7,8,9},MENU_INT,0,0,0},
 { "MOUSE Y SENSITIVITY ", {"1", "2","3","4","5","6","7","8","9",""},
   8, 200,2,2,{0,1,2,3,4,5,6,7,8,9},MENU_INT,0,0,0},
 { "MOUSE X INVERT", {"NO", "YES","?","?","?","","","","",""},
   1, 300,0,0,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},	
 { "MOUSE Y INVERT", {"NO", "YES","?","?","?","","","","",""},
   1, 400,0,0,{0,1,0,0,0,0,0,0,0,0},MENU_INT,0,0,0},	
 { "NONE", {"NONE", " ", " ", " "}, 0,0, 0,0,{0,0,0,0,0,0,0,0,0,0},0,0,0,0}
};



#define ROOTMENU   100
#define INPUTMENU  200
#define ANALOGMENU 300
#define DIPMENU    400
#define VIDEOMENU  500
#define AUDIOMENU  600

MENUS *curr_menu;


void do_the_menu(void)
{

	//glScalef(1.05f,1.05f,1);
	//glTranslatef(792,-210,0);
	//glRotatef(90,0,0,1);


   if ( KeyCheck(config.ksnap)) {snapshot("snaptest.bmp");} //Quick FIX
   switch (menulevel)
   {
    
   case ROOTMENU:   do_root_menu();break;
   case INPUTMENU:  do_keyboard_menu();break;
   case ANALOGMENU: do_mouse_menu();break;
   case DIPMENU:    do_settings_menu();break; 
   case VIDEOMENU:  do_video_menu();break;
   case AUDIOMENU:  do_sound_menu();break;
   default: do_root_menu();
   
   }
}

void do_root_menu(void)
{
     int x;
     int C;
	 int top=565;
	 num_this_menu=5;
		 
	 draw_a_quad( 280,742,634, 175, 20, 20, 80, 220, 1);
	
     for (x=0; x < 5; x++)
	 {
	    C=255;
	 	switch (x){
		 case 0: if (menuitem==1){C=0;}glPrint(380,top-(75*x),255,C,255,255,1.3,0,0,"INPUT SETTINGS");break;
	     case 1: if (menuitem==2){C=0;}glPrint(380,top-(75*x),255,C,255,255,1.3,0,0,"ANALOG INPUT");break;
	     case 2: if (menuitem==3){C=0;}glPrint(380,top-(75*x),255,C,255,255,1.3,0,0,"DIPSWTCHES");break;
		 case 3: if (menuitem==4){C=0;}glPrint(380,top-(75*x),255,C,255,255,1.3,0,0,"VIDEO SETUP");break;
         case 4: if (menuitem==5){C=0;}glPrint(380,top-(75*x),255,C,255,255,1.3,0,0,"SOUND SETUP");break;
	    } 
		 draw_center_tex(&menu_tex[x], 32, 335,580-(74*x), 0, NORMAL, 255,255,255,255, 2);
  	 }
}



void do_keyboard_menu(void)
{
	int x=0;
	int color=255;
	int spacing=21;
	int start=620;
    int page=0;
    int shift=0;
    
	
	MK=GK;

	if (number==0){
	while (MK[number].Default !=-1)
	{  
		//wrlog("MK value Num %x Header %s current %x",MK[number].Default,MK[number].Heading,MK[number].Current);
		number++;
	}
	
	num_this_menu=(number-1);
	page=0;
	}
	
	draw_a_quad( 230,842,745, 106, 20, 20, 80, 220, 1);
    if (gamenum==0)
	{glPrint(285,690,255,255,255,255,1.3,0,0,"Input Configuration (Global)");} //title of screen
	else
	{glPrint(285,690,255,255,255,255,1.3,0,0,"Input Configuration (This Game)");} //title of screen
	
	if (sublevel !=1)glPrint(285,665,255,255,255,255,1.1,0,0,"Press F1 to Reset to Defaults");
    
	if ((menuitem-1) >= 24) {page=(num_this_menu+1);x+=24;}
	if ((menuitem-1) <= 23) {page=24;}
	if (num_this_menu < 23) {page=(num_this_menu+1);}
	while (x < page )
	{	
	if (x==menuitem-1){color=0;}else {color=255;}
    if (MK[x].Type != IN_DEFAULT){
	glPrint(145, start, 255,color,255,255, 1, 0 ,0,MK[x].Heading);
	glPrint(430, start, 255,255,255,255, 1, 0 ,0,"%s",key_names[MK[x].Current]);
	glPrint(570, start, 255,255,255,255, 1, 0 ,0,"%s",joy_names[MK[x].Joystick]);
	glPrint(800, start, 255,255,255,255, 1, 0 ,0,"%s",mouse_names[MK[x].mouseb]);
	}
	
	start-=spacing;	
	x++;	
	}
	if ((num_this_menu > 24) && ((menuitem-1) < 24)){
		glPrint(475, start-10, 255,30,30,255, 1, 0 ,0,"(MORE)");}

	if (sublevel==1) {change_key();}
}


void do_settings_menu(void)
{
	int yval=490;
	int color=0;
    char name[12];
    strncpy(name,driver[gamenum].name,10);
	name[0]=toupper(name[0]);
	it=1;
    if (number==0){
		while (dips[number].Switch !=0)
		{number++;}
		num_this_menu=(number-1);
		currentval=dips[0].Current; }
    
   draw_a_quad( 230,792,634, 146, 20, 20, 80, 220, 1);
	//draw_a_quad( 280,742,634, 175, 20, 20, 80, 220, 1);
  glPrint(265,585,255,255,255,255,1.3,0,0,"Dipswitches for %s",name);
  //glPrint(380,585,255,255,255,255,1.3,0,0,"Menuitem %d",currentval); 
  glPrint(265,545,255,255,255,255,1.3,0,0,"Press F1 to reset to defaults"); 
  val_low=0;
  
  do
  {
   if (menuitem==it){color=0;}else{color=255;} glPrint(245, yval, 255,color,255,255, 1.3, 0 ,0,dips[(it-1)].Heading);
   if (menuitem==it){ val_high=(dips[(it-1)].NumOptions);glPrint(530, yval, 255,255,255,255, 1.3, 0 ,0,"%s",dips[(it-1)].Options[currentval]);}
   else {glPrint(530, yval, 255,255,255,255, 1.3, 0 ,0,"%s",dips[(it-1)].Options[dips[(it-1)].Current]);}
   it++;yval-=35;
   }while (dips[(it-1)].Switch !=0);
 }
void do_sound_menu(void)
{
     int yval=460;
	int color=0;
	it=1;
   
	if (number==0){
		while (soundmenu[number].NumOptions !=0)
		{number++;}
		num_this_menu=(number-1);
		currentval=soundmenu[0].Current; }
    
  draw_a_quad( 270,752,620, 275, 20, 20, 80, 220, 1);
  if (gamenum==0){glPrint(310,540,255,255,255,255,1.3,0,0,"Sound Settings - Global");}
  else {glPrint(285,540,255,255,255,255,1.3,0,0,"Sound Settings - This Game");}
  val_low=0;
  
  do
  {
   if (menuitem==it){color=0;}else{color=255;}glPrint(300, yval, 255,color,255,255, 1.1, 0 ,0,soundmenu[(it-1)].Heading);
   if (menuitem==it){  
	   if(soundmenu[(it-1)].menu_type==MENU_INT){val_high=(soundmenu[(it-1)].NumOptions);glPrint(640, yval, 255,255,255,255, 1.1, 0 ,0,"%s",soundmenu[(it-1)].Options[currentval]);}
	   else if (soundmenu[(it-1)].menu_type==MENU_FLOAT) {val_low = soundmenu[(it-1)].Min;val_high=(soundmenu[(it-1)].Max);glPrint(640, yval, 255,255,255,255, 1.1, 0 ,0,"%2.0f%%",soundmenu[(it-1)].Step*currentval);}            
      
  }
  else {  if(soundmenu[(it-1)].menu_type==MENU_INT){ glPrint(640, yval, 255,255,255,255, 1.1, 0 ,0,"%s",soundmenu[(it-1)].Options[soundmenu[(it-1)].Current]);}
           else if (soundmenu[(it-1)].menu_type==MENU_FLOAT) { glPrint(640, yval, 255,255,255,255, 1.1, 0 ,0,"%2.0f%%",soundmenu[(it-1)].Step*soundmenu[(it-1)].Current);}
       }
  it++;yval-=35; //35
  }while (soundmenu[(it-1)].NumOptions !=0);

}

void do_video_menu(void)
{
 	int yval=630;//580 //600
	int color=0;
	it=1;
   
	if (number==0){
	 	           while (glmenu[number].NumOptions !=0){number++;}
		           num_this_menu=(number-1);currentval=glmenu[0].Current; 
	              }
  //draw_a_quad( 230,792,694, 36, 20, 20, 80, 220, 1);
  draw_a_quad( 230,795,715, 36, 20, 20, 80, 220, 1);
  menu_textureit(&menu_tex[6],650,555, 16, 40); 
  if (gamenum==0){glPrint(330,yval+30,255,255,255,255,1.3,0,0,"GL Settings - Global");}
  else {glPrint(300,yval+30,255,255,255,255,1.3,0,0,"GL Settings - This Game");}
  val_low=0;
  
  do
  {
   if (menuitem==it){color=0;}else{color=255;}glPrint(245, yval, 255,color,255,255, 1.1, 0 ,0,glmenu[(it-1)].Heading);
   if (menuitem==it){  
	   if(glmenu[(it-1)].menu_type==MENU_INT){val_high=(glmenu[(it-1)].NumOptions);glPrint(550, yval, 255,255,255,255, 1.1, 0 ,0,"%s",glmenu[(it-1)].Options[currentval]);}
	   else if (glmenu[(it-1)].menu_type==MENU_FLOAT) {val_low = glmenu[(it-1)].Min * ceilf((1/glmenu[(it-1)].Step));val_high=(glmenu[(it-1)].Max * ceilf((1/glmenu[(it-1)].Step)));glPrint(550, yval, 255,255,255,255, 1.1, 0 ,0,"%2.1f",glmenu[(it-1)].Step*currentval);}            
                     }
  
   else {  if(glmenu[(it-1)].menu_type==MENU_INT){ glPrint(550, yval, 255,255,255,255, 1.1, 0 ,0,"%s",glmenu[(it-1)].Options[glmenu[(it-1)].Current]);}
           else if (glmenu[(it-1)].menu_type==MENU_FLOAT) { glPrint(550, yval, 255,255,255,255, 1.1, 0 ,0,"%2.1f",glmenu[(it-1)].Step*glmenu[(it-1)].Current);}
        }
   it++;yval-=31; //35
   }while (glmenu[(it-1)].NumOptions !=0);
}

void change_menu_level(int dir) //This is up and down
{ 


	int level=0;


	if (dir)
	{   
		if (menulevel==ANALOGMENU){ if ((mousemenu[menuitem-1].Current)!=currentval){mousemenu[menuitem-1].Current=currentval;} }
		if (menulevel==DIPMENU){ if ((dips[menuitem-1].Current)!=currentval){dips[menuitem-1].Current=currentval;} }
		if (menulevel==VIDEOMENU){ if ((glmenu[menuitem-1].Current)!=currentval){glmenu[menuitem-1].Current=currentval;} }
		if (menulevel==AUDIOMENU){ if ((soundmenu[menuitem-1].Current)!=currentval){soundmenu[menuitem-1].Current=currentval;} }
		
		if( menuitem < (num_this_menu+1)){menuitem++;} //ACTUAl CHANGE
		
		if (menulevel==ANALOGMENU){currentval=mousemenu[menuitem-1].Current;} //DIPS
		if (menulevel==DIPMENU){currentval=dips[menuitem-1].Current;} //DIPS
		if (menulevel==VIDEOMENU){currentval=glmenu[menuitem-1].Current;} 
		if (menulevel==AUDIOMENU){currentval=soundmenu[menuitem-1].Current;}
		
		
	}
	else
	{  if (menulevel==ANALOGMENU){ if ((mousemenu[menuitem-1].Current)!=currentval){mousemenu[menuitem-1].Current=currentval;}}
	   if (menulevel==DIPMENU){ if ((dips[menuitem-1].Current)!=currentval){dips[menuitem-1].Current=currentval;}}
       if (menulevel==VIDEOMENU){ if ((glmenu[menuitem-1].Current)!=currentval){glmenu[menuitem-1].Current=currentval;}}
       if (menulevel==AUDIOMENU){ if ((soundmenu[menuitem-1].Current)!=currentval){soundmenu[menuitem-1].Current=currentval;}}
   
    
	                 
	   if( menuitem > 1){menuitem--;}
		 
	   if (menulevel==ANALOGMENU){currentval=mousemenu[menuitem-1].Current;}
	   if (menulevel==DIPMENU){currentval=dips[menuitem-1].Current;}
	   if (menulevel==VIDEOMENU){currentval=glmenu[menuitem-1].Current;}
	   if (menulevel==AUDIOMENU){currentval=soundmenu[menuitem-1].Current;}
	}
	
}
void change_menu_item(int dir) //This is right and left
{
 if (dir)
 {currentval++; if (currentval > val_high) currentval=val_low;
   if (menulevel==AUDIOMENU){check_sound_menu();}
   if (menulevel==VIDEOMENU){check_video_menu();}
 }
 else
 {currentval--; if (currentval < val_low) currentval=val_high;
  if (menulevel==AUDIOMENU){check_sound_menu();}
  if (menulevel==VIDEOMENU){check_video_menu();}
 }
}

void select_menu_item(void) //This is enter
{
   switch (menulevel)
   {
    
	case ROOTMENU: {menulevel=menulevel*(menuitem+1);
		      if (menulevel == DIPMENU && gamenum==0){menulevel=ROOTMENU;} //GUI has no dips
			  menuitem=1;
			  break;}
	   
  //For changing levels
   case INPUTMENU: {sublevel=1;break; }
  // case 300: do_joystick_menu();break;
  // case 400: do_mouse_menu();break;
   //case 500: { do_settings_menu();break;}
  // case 600: do_video_menu();break;
   }
}

void change_menu(void)
{
 //This is menu exit;
	
	//Tweak for dipswitch to make sure to set selected item on exit
	if (menulevel == AUDIOMENU) {if ((soundmenu[menuitem-1].Current)!=currentval){soundmenu[menuitem-1].Current=currentval;} number=0;save_sound_menu();}
	if (menulevel == VIDEOMENU) {if ((glmenu[menuitem-1].Current)!=currentval){glmenu[menuitem-1].Current=currentval;} number=0;save_video_menu();}
	if (menulevel == DIPMENU) {if ((dips[menuitem-1].Current)!=currentval){dips[menuitem-1].Current=currentval;} number=0;save_dips();}
	if (menulevel == ANALOGMENU) {if ((mousemenu[menuitem-1].Current)!=currentval){mousemenu[menuitem-1].Current=currentval;} number=0;save_mouse_menu();}
	
	if (menulevel == INPUTMENU) {save_keys();}
	if (menulevel > ROOTMENU) menulevel=100;menuitem=1;number=0;
	
}

void reset_menu()
{   
	//This is for setting defaults                  //Below Resets selected item as well.
	if (menulevel == DIPMENU) {reset_to_default_dips();currentval=dips[(menuitem-1)].Default;}
	if (menulevel == INPUTMENU) {set_default_keys();}
}

void change_key(void)
{
   int k=0;
   int w=0;
   int i;
   static int clr=0;
   static int ledenable=0;
   key_set_flag=1;
   glPrint(460,665,255,200,30,255,1.1,0,0,"Press a Key");
   
   
   if (clr==0)
   {
          //Check that no keys are pressed.
	      set_aae_leds(0,0,0); 
		  ledenable = config.kbleds;
		  config.kbleds=0;
	      for (i = 156;i >= 0;i--)
		  {if (key[i]) {w++;}}
   }
   if (w==0) {clr=1;}

   if (clr)
       {
	       for (i = 156;i >= 0;i--)
		    if (key[i])
			 {
			 k=i;
			 if (k==KEY_PAUSE || k==KEY_ENTER){k=0;clr=0;return;}
            // gamekeys[(menuitem-1)].Current=k;
			 sublevel=0;key[k]=0;
			 clear_keybuf();
			 force_keys();
			 config.kbleds=ledenable;
			 ledenable=0;
			 key_set_flag=0;
			 clr=0;
			 }
      }

 //  for ( i=0; i<256; i++) if (key[i]) k=i;
  
	 // if (k==KEY_PAUSE || k==KEY_ENTER){k=0;} //Make sure to trap the entering of the key
    	
	 // gamekeys[(menuitem-1)].Current=k;
	 // if (k) {sublevel=0;key[k]=0;clear_keybuf();force_keys(); key_set_flag=0;}
	 // sublevel=0;key[k]=0;clear_keybuf();force_keys(); key_set_flag=0;
  
}


void do_mouse_menu(void)
{   
	int yval=500;
	int color=0;
	it=1;
   
	if (number==0){
		while (mousemenu[number].NumOptions !=0)
		{number++;}
		num_this_menu=(number-1);
		currentval=mousemenu[0].Current; }
    
  draw_a_quad( 230,792,645, 346, 20, 20, 80, 220, 1);
  if (gamenum==0){glPrint(300,580,255,255,255,255,1.3,0,0,"Mouse Settings - Global");}
  else {glPrint(280,580,255,255,255,255,1.3,0,0,"Mouse Settings - This Game");}
  val_low=0;
  
  do
  {
   if (menuitem==it){color=0;}else{color=255;} glPrint(255, yval, 255,color,255,255, 1.3, 0 ,0,mousemenu[(it-1)].Heading);
   if (menuitem==it){ val_high=(mousemenu[(it-1)].NumOptions);glPrint(680, yval, 255,255,255,255, 1.3, 0 ,0,"%s",mousemenu[(it-1)].Options[currentval]);}
   else {glPrint(680, yval, 255,255,255,255, 1.3, 0 ,0,"%s",mousemenu[(it-1)].Options[mousemenu[(it-1)].Current]);}
   it++;yval-=35;
   }while (mousemenu[(it-1)].NumOptions !=0);
 
}

void check_sound_menu()
{
	if ((menuitem-1)==0){config.mainvol = (currentval * 12.75);set_volume((int)(currentval * 12.75),0); play_sample(game_sounds[num_samples-5],currentval,128,1000,0);} //Main Vol
	if ((menuitem-1)==1){ config.pokeyvol =  (currentval * 12.75);play_sample(game_sounds[num_samples-5],currentval,128,1000,0);} //Pokey Vol
	if ((menuitem-1)==2){ config.noisevol = (currentval * 12.75);play_sample(game_sounds[num_samples-5],currentval,128,1000,0);}//Noise Vol
	if ((menuitem-1)==3){ config.hvnoise = currentval; setup_ambient(VECTOR);}
	if ((menuitem-1)==4){ config.psnoise = currentval; setup_ambient(VECTOR);}
	if ((menuitem-1)==5){ config.pshiss = currentval; setup_ambient(VECTOR);}
}

void setup_sound_menu(void)
{ 
	int x=0;
 
   soundmenu[0].Current=ceilf(config.mainvol);
   soundmenu[1].Current=ceilf(config.pokeyvol);
   soundmenu[2].Current=ceilf(config.noisevol);
   soundmenu[3].Current=config.hvnoise;
   soundmenu[4].Current=config.psnoise;
   soundmenu[5].Current=config.pshiss;
   while (soundmenu[x].NumOptions !=0){soundmenu[x].Changed=soundmenu[x].Current;x++;}//SET TO DETECT CHANGED VALUES
}

void save_sound_menu()
{
  if (soundmenu[0].Changed != soundmenu[0].Current) my_set_config_int("main", "mainvol", soundmenu[0].Current, gamenum);//soundmenu[0].Value[soundmenu[0].Current], gamenum);
  if (soundmenu[1].Changed != soundmenu[1].Current) my_set_config_int("main", "pokeyvol",soundmenu[1].Current, gamenum);//.Value[soundmenu[1].Current], gamenum);
  if (soundmenu[2].Changed != soundmenu[2].Current) my_set_config_int("main", "noisevol",soundmenu[2].Current, gamenum);//.Value[soundmenu[2].Current], gamenum);
  if (soundmenu[3].Changed != soundmenu[3].Current) my_set_config_int("main", "hvnoise",  soundmenu[3].Current, gamenum);
  if (soundmenu[4].Changed != soundmenu[4].Current) my_set_config_int("main", "psnoise" ,soundmenu[4].Current, gamenum);
  if (soundmenu[5].Changed != soundmenu[5].Current) my_set_config_int("main", "pshiss", soundmenu[5].Current, gamenum);
}

void setup_mouse_menu(void)
{
  int x=0;

   mousemenu[0].Current=config.mouse1xs;
   mousemenu[1].Current=config.mouse1ys;
   mousemenu[2].Current=config.mouse1x_invert;
   mousemenu[3].Current=config.mouse1y_invert;
   while (mousemenu[x].NumOptions !=0){mousemenu[x].Changed=mousemenu[x].Current;x++;}//SET TO DETECT CHANGED VALUES
}

void save_mouse_menu()
{
  
  if (mousemenu[0].Changed != mousemenu[0].Current) my_set_config_int("main", "mouse1xs", mousemenu[0].Current, gamenum);
  if (mousemenu[1].Changed != mousemenu[1].Current) my_set_config_int("main", "mouse1ys", mousemenu[1].Current, gamenum);
  if (mousemenu[2].Changed != mousemenu[2].Current) my_set_config_int("main", "mouse1x_invert", mousemenu[2].Current, gamenum);
  if (mousemenu[3].Changed != mousemenu[3].Current) my_set_config_int("main", "mouse1y_invert",  mousemenu[3].Current, gamenum);
  
}


void check_video_menu()
{
	if ((menuitem-1)==2){SetGammaRamp(127+(currentval*2),config.bright,config.contrast); }
    if ((menuitem-1)==3){SetGammaRamp(config.gamma,127+(currentval*2),config.contrast); }
	if ((menuitem-1)==4){SetGammaRamp(config.gamma,config.bright,127+(currentval*2)); }
	if ((menuitem-1)==6) {config.drawzero = currentval;}
	if ((menuitem-1)==7) {config.widescreen = currentval;Widescreen_calc();} //Recalculate Widescreen value
	if ((menuitem-1)==8)  {config.vectrail= currentval;}
	if ((menuitem-1)==9)  {config.vecglow = currentval;}
	if ((menuitem-1)==10) {config.linewidth = glmenu[10].Step*currentval;}
	if ((menuitem-1)==11) {config.pointsize = glmenu[11].Step*currentval;}
	if ((menuitem-1)==12) {config.gain=currentval;}
	
	if ((menuitem-1)==13) {if (art_loaded[0])config.artwork=currentval;}
	if ((menuitem-1)==14) {if (art_loaded[1])config.overlay=currentval;}
	if ((menuitem-1)==15) {if (art_loaded[3]) config.bezel=currentval;setup_video_config();}
	if ((menuitem-1)==16) {config.artcrop=currentval;setup_video_config();} //Reconfigure video size
	
	if ((menuitem-1)==19){ config.kbleds =currentval;}
	//SetGammaRamp(double gamma, double bright, double contrast);
}


void setup_video_menu(void)
{   int x=0;

	if (config.windowed) {glmenu[0].Current=1;}else {glmenu[0].Current=0;}//WINDOWED
	
	switch (config.screenw) //RESOLUTION
	 { case 640: glmenu[1].Current=0;break;
	   case 800: glmenu[1].Current=1;break;
	   case 1024: glmenu[1].Current=2;break;
	   case 1152: glmenu[1].Current=3;break;
	   case 1280: glmenu[1].Current=4;break;
       case 1600: glmenu[1].Current=5;break;
	   default: glmenu[1].Current=0;break;
	 }
    glmenu[2].Current=(config.gamma-127)/2;
	glmenu[3].Current=(config.bright-127)/2;
	glmenu[4].Current=(config.contrast-127)/2;
	 if (config.forcesync) {glmenu[5].Current=1;}else {glmenu[5].Current=0;}//VSYNC
  
     //Draw zero lines
	 if (config.drawzero) {glmenu[6].Current=1;}else {glmenu[6].Current=0;}//VSYNC
     if (config.widescreen)  {glmenu[7].Current=1;}else {glmenu[7].Current=0;}
	

	    switch (config.vectrail) //SAMPLE LEVEL
	 { case 0: glmenu[8].Current=0;break;
	   case 1: glmenu[8].Current=1;break;
	   case 2: glmenu[8].Current=2;break;
	   case 3: glmenu[8].Current=3;break;
	   default: glmenu[8].Current=0;break;
	 }
	    
	   glmenu[9].Current=config.vecglow;
	   glmenu[10].Current=config.m_line;// / .1; //config.m_line;
       glmenu[11].Current=config.m_point;// / .1;//config.m_point;
	   glmenu[12].Current=config.gain;//'if (config.monitor) {glmenu[12].Current=1;}else {glmenu[12].Current=0;}//MONITOR
	   if (config.artwork) {glmenu[13].Current=1;}else {glmenu[13].Current=0;}//ARTWORK
	   if (config.overlay) {glmenu[14].Current=1;}else {glmenu[14].Current=0;}//OVERLAY
	   if (config.bezel)   {glmenu[15].Current=1;}else {glmenu[15].Current=0;}//BEZEL
	   if (config.artcrop) {glmenu[16].Current=1;}else {glmenu[16].Current=0;}//CROP BEZEL
	   if (config.burnin)  {glmenu[17].Current=1;}else {glmenu[17].Current=0;}//SCREEN BURN
	 switch (config.priority) //POINTSIZE
	 { case 0: glmenu[18].Current=0;break;
	   case 1: glmenu[18].Current=1;break;
	   case 2: glmenu[18].Current=2;break;
	   case 3: glmenu[18].Current=3;break;
	   case 4: glmenu[18].Current=4;break;
	   default: glmenu[18].Current=2;break;
	 }
       glmenu[19].Current=config.kbleds;
       
	   while (glmenu[x].NumOptions !=0){glmenu[x].Changed=glmenu[x].Current;x++;}//SET TO DETECT CHANGED VALUES
}

void save_video_menu(void)
{
	int x=0;
	int y=0;

	if (glmenu[0].Changed != glmenu[0].Current){ my_set_config_int("main", "windowed",glmenu[0].Current, gamenum);}
   
      switch (glmenu[1].Current) //RESOLUTION
	 { 
	   case 0: x=640;y=480;break;  
	   case 1: x=800;y=600;break;   
	   case 2: x=1024;y=768;break;  
	   case 3: x=1152;y=864;break;  
	   case 4: x=1280;y=1024;break;  
       case 5: x=1600;y=1200;break; 
	   default:x=1024;y=768;break;
	 }
	 if (glmenu[1].Changed != glmenu[1].Current){ my_set_config_int("main", "screenw", x, gamenum);
	                                                my_set_config_int("main", "screenh", y, gamenum);}
	  
     if (glmenu[2].Changed != glmenu[2].Current)   my_set_config_int("main", "gamma", (glmenu[2].Current*2)+127, gamenum);
	 if (glmenu[3].Changed != glmenu[3].Current)   my_set_config_int("main", "bright", (glmenu[3].Current*2)+127, gamenum);
	 if (glmenu[4].Changed != glmenu[4].Current)   my_set_config_int("main", "contrast", (glmenu[4].Current*2)+127, gamenum);
	 if (glmenu[5].Changed != glmenu[5].Current)   my_set_config_int("main", "force_vsync",glmenu[5].Current, gamenum);
     if (glmenu[6].Changed != glmenu[6].Current)   my_set_config_int("main", "drawzero", glmenu[6].Value[glmenu[6].Current], gamenum);
	 if (glmenu[7].Changed != glmenu[7].Current)   my_set_config_int("main", "widescreen", glmenu[7].Value[glmenu[7].Current], gamenum);
	 if (glmenu[8].Changed != glmenu[8].Current)   my_set_config_int("main", "vectortrail", glmenu[8].Value[glmenu[8].Current], gamenum);
	 if (glmenu[9].Changed != glmenu[9].Current)   my_set_config_int("main", "vectorglow", glmenu[9].Current, gamenum);
	 if (glmenu[10].Changed != glmenu[10].Current) my_set_config_int("main", "m_line",glmenu[10].Current, gamenum);
	 if (glmenu[11].Changed != glmenu[11].Current) my_set_config_int("main", "m_point",glmenu[11].Current, gamenum);
	 if (glmenu[12].Changed != glmenu[12].Current) my_set_config_int("main", "gain",glmenu[12].Current, gamenum);
	 if (glmenu[13].Changed != glmenu[13].Current) my_set_config_int("main", "artwork",glmenu[13].Current, gamenum);
	 if (glmenu[14].Changed != glmenu[14].Current) my_set_config_int("main", "overlay",glmenu[14].Current, gamenum);
	 if (glmenu[15].Changed != glmenu[15].Current) my_set_config_int("main", "bezel",glmenu[15].Current, gamenum);
	 if (glmenu[16].Changed != glmenu[16].Current) my_set_config_int("main", "artcrop",glmenu[16].Current, gamenum);
	 if (glmenu[17].Changed != glmenu[17].Current) my_set_config_int("main", "screenburn",glmenu[17].Current, gamenum);
	 if (glmenu[18].Changed != glmenu[18].Current) my_set_config_int("main", "priority",glmenu[18].Current, gamenum);
	 if (glmenu[19].Changed != glmenu[19].Current) my_set_config_int("main", "kbleds",glmenu[19].Current, gamenum);
}

void set_points_lines()
{
   
	config.linewidth =  glmenu[10].Step*(glmenu[10].Current);
	config.pointsize =  glmenu[11].Step*(glmenu[11].Current);
	glLineWidth(config.linewidth);//linewidth
	glPointSize(config.pointsize);//pointsize
}
