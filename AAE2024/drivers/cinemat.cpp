/* Cinematronics Emu */
#include "../cpu_code/ccpu.h"
#include "cinemat.h"
#include "aae_mame_driver.h"
#include "../sndhrdw/samples.h"
#include "../vidhrdwr/vector.h"

//#include "sys_video/glcode.h"

//From M.A.M.E. (TM)
/***************************************************************************

	Cinematronics vector hardware

	driver by Aaron Giles

	Special thanks to Neil Bradley, Zonn Moore, and Jeff Mitchell of the
	Retrocade Alliance

	Games supported:
		* Space Wars
		* Barrier
		* Star Hawk
		* Star Castle
		* Tailgunner
		* Rip Off
		* Speed Freak
		* Sundance
		* Warrior
		* Armor Attack
		* Solar Quest
		* Demon
		* War of the Worlds
		* Boxing Bugs

***************************************************************************/

//What is this?
extern char *gamename;
extern int gamenum;

//static int cframe = 0;

/////NEW VARIABLES AND STUFF
static UINT16* rambase;
static UINT8 coin_detected;
static UINT8 coin_last_reset;
static UINT8 mux_select;

void (*sound_write) (unsigned char, unsigned char);
//void (*get_cini_input) ();
int cinemat_outputs = 0xff;

int thisframe = 0;
int lastframe = 0;

UINT8 bSwapXY;
UINT8 bFlipX;
UINT8 bFlipY;

UINT8 bNewFrame;
UINT32 current_frame = 0;

/*************************************
 *
 *  Coin handlers
 *
 *************************************/
static int input_changed(int coin_inserted)
{
	/* on the falling edge of a new coin, set the coin_detected flag */
	if (coin_inserted == 0)
		coin_detected = 1;
}

static int coin_read(int coin_input_r)
{
	return !coin_detected;
}

/*

static int speedfrk_wheel_r(int offset)
{
	static UINT8 speedfrk_steer[] = { 0xe, 0x6, 0x2, 0x0, 0x3, 0x7, 0xf };
	static int last_wheel = 0, delta_wheel, last_frame = 0, gear = 0xe0;
	int  current_frame, val;

	// check the fake gear input port and determine the bit settings for the gear
	if ((input_port_5_r(0) & 0xf0) != 0xf0)
		gear = input_port_5_r(0) & 0xf0;

	val = (input_port_1_r(0) & 0xff00) | gear;

	// add the start key into the mix
	if (input_port_1_r(0) & 0x80)
		val |= 0x80;
	else
		val &= ~0x80;

// and for the cherry on top, we add the scrambled analog steering
	current_frame = cframe;//cpu_getcurrentframe();
	if (current_frame != last_frame)
	{
		// the shift register is cleared once per 'frame'
		delta_wheel = readinputport(4) - last_wheel;
		wrlog("DELTA WHEEL %d", delta_wheel);
		last_wheel += delta_wheel;
		if (delta_wheel > 3)
			delta_wheel = 3;
		else if (delta_wheel < -3)
			delta_wheel = -3;
	}
	last_frame = current_frame;

	//return (speedfrk_steer[delta_wheel + 3] >> offset) & 1;
	val |= speedfrk_steer[delta_wheel + 3];

	return val;
}

*/

/*************************************
 *
 *  Speed Freak inputs
 *
 *************************************/

static int speedfrk_wheel_r(int offset)
{
	static const UINT8 speedfrk_steer[] = { 0xe, 0x6, 0x2, 0x0, 0x3, 0x7, 0xf };

	static int last_wheel = 0, last_frame = 0;
	int delta_wheel = 3;

	// the shift register is cleared once per 'frame'
	delta_wheel = (INT8)readinputportbytag("WHEEL") / 8;
	//delta_wheel = delta_wheel / 4;
	//wrlog("DELTA WHEEL is %d", delta_wheel);

	if (delta_wheel > 3)
		delta_wheel = 3;
	else if (delta_wheel < -3)
		delta_wheel = -3;

	return (speedfrk_steer[delta_wheel + 3] >> offset) & 1;
}

static int speedfrk_gear_r(int offset)
{
	static int gear = 0x0e;
	int gearval = readinputportbytag("GEAR");

	// check the fake gear input port and determine the bit settings for the gear
	if ((gearval & 0x0f) != 0x0f)
		gear = gearval & 0x0f;

	// add the start key into the mix -- note that it overlaps 4th gear
	if (!(readinputportbytag("INPUTS") & 0x80))
		gear &= ~0x08;

	return (gear >> offset) & 1;
}

/*************************************
 *
 *  Sundance inputs
 *
 *************************************/

static const struct
{
	const char* portname;
	UINT16 bitmask;
} sundance_port_map[16] =
{
	{"PAD1", 0x155},	/* bit  0 is set if P1 1,3,5,7,9 is pressed */
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },

	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },

	{ "PAD2", 0x1a1 },	/* bit  8 is set if P2 1,6,8,9 is pressed */
	{ "PAD1", 0x1a1 },	/* bit  9 is set if P1 1,6,8,9 is pressed */
	{ "PAD2", 0x155 },	/* bit 10 is set if P2 1,3,5,7,9 is pressed */
	{ 0, 0 },

	{ "PAD1", 0x093 },	/* bit 12 is set if P1 1,2,5,8 is pressed */
	{ "PAD2", 0x093 },	/* bit 13 is set if P2 1,2,5,8 is pressed */
	{ "PAD1", 0x048 },	/* bit 14 is set if P1 4,8 is pressed */
	{ "PAD2", 0x048 },	/* bit 15 is set if P2 4,8 is pressed */
};

static int sundance_read(int offset) //static READ8_HANDLER( sundance_inputs_r )
{
	// handle special keys first
	if (sundance_port_map[offset].portname)
		return (readinputportbytag(sundance_port_map[offset].portname) & sundance_port_map[offset].bitmask) ? 0 : 1;
	else
		return (readinputport(1) >> offset) & 1;
}

/**************************************
 *
 *  Boxing Bugs inputs
 *
 *************************************/

static  int boxingb_dial_r(int offset)
{
	UINT16 value = readinputportbytag("DIAL");
	offset -= 0x0b;

	if (!MUX_VAL) offset += 4;
	return (value >> offset) & 1;
}

/*************************************
 *
 *  General input handlers
 *
 *************************************/

UINT16 get_ccpu_inputs(int offset)
{
	switch (gamenum)
	{
	case BOXINGB: {
		if (offset > 0xb)  return  boxingb_dial_r(offset);
		else
			return (readinputport(1) >> offset) & 1;
		break;
	}

	case SUNDANCE:
		return sundance_read(offset);
		break;

	case SPEEDFRK: {
		if (offset < 0x04)  return  speedfrk_wheel_r(offset);
		else if (offset > 0x03 && offset < 0x07)  return  speedfrk_gear_r(offset);
		else if (offset > 0x06) return (readinputport(1) >> offset) & 1;
		break;
	}

	default: return (readinputport(1) >> offset) & 1;
	}

	return 0;
}

UINT16 get_ccpu_switches(int offset)
{
	static const UINT8 switch_shuffle[8] = { 2,5,4,3,0,1,6,7 };
	static int coindown = 0;
	UINT16 test = readinputport(0);

	if (offset == 7)
	{
		if ((test & 0x80) && coindown) { coindown--; } //Clear Countdown
		else if ((test & 0x80) == 0 && coindown) { bitset(test, 0x80); } //Ignore Coin down.
		else if ((test & 0x80) == 0 && coindown == 0) { coindown = 2; } //Set coin countdown
	}

	return (test >> switch_shuffle[offset]) & 1;
}

/*************************************
 *
 *  General output handlers
 *
 *************************************/

void coin_handler(int data)//coin_reset_w
{
	//wrlog("Coin handler write %x",data);
	 //logerror("Coin port write %x\n",data);
	/* on the rising edge of a coin reset, clear the coin_detected flag */
	if (coin_last_reset != data && data != 0)
	{
		coin_detected = 0;
		//wrlog("CLEARING COIN DETECTED");
	}
	coin_last_reset = data;
	// wrlog("Coin_detected value HERE %d ",coin_detected);
}

static int mux_set(int data) // mux_select_w
{
	mux_select = data;
	wrlog("MUX SELECT");
	//cinemat_sound_control_w(machine, 0x07, data);
}

UINT8 joystick_read(void)
{
	//	if (port_tag_to_index("ANALOGX") != -1)
	//	{
	int xval = (INT16)(cpunum_get_reg(0, CCPU_X) << 4) >> 4;

	//wrlog("joystick read XVAL %x: MUXVAL %x  ", xval, MUX_VAL);
	//wrlog("Returned value %x",( (readinputportbytag(MUX_VAL ? "IN2" : "IN3") << 4) - xval) < 0x800);

	return ((readinputportbytag(MUX_VAL ? "ANALOGX" : "ANALOGY") << 4) - xval) < 0x800;
	//	}

	return 0;
}

/*************************************
 *
 *  Boxing Bugs inputs
 *
 *************************************/

//static int bbug_read(int offset, int value)// boxingb_dial_r
//{
	//int value;
//	value = readinputportbytag("DIAL");
//	if (!mux_select) offset += 4;
//	return (value >> offset) & 1;
//}

/*************************************
 *
 *  CPU configurations
 *
 *************************************/

static struct CCPUConfig config_nojmi =
{
	joystick_read,
	cinemat_vector_callback
};

static struct CCPUConfig config_jmi =
{
	NULL,
	cinemat_vector_callback
};
////////////////////////////////////////////////////
/////END OF NEW CODE//////
////////////////////////////////////////////////////

void run_cinemat(void)
{
	//Note: What the crap is this, get it in the CPU code and proper reset and testswitch 

	int b = 1;

	b = run_ccpu(19923000 / 4 / 38);//131072);19923000
	cinevid_up();
}

int init_cinemat(void)
{
	wrlog("Cinemat init started, gamenum is %d", gamenum);
	//SET DEFAULTS
	bSwapXY = 0;    // (for vector generation)
	cache_clear();
	//ioInputs = 0xffff; ioSwitches = 0xfff0;  //(cleared values)
	cinemat_outputs = 0xff; //0xff
	//ioOutputs = 0xff;
	init_snd();
	sound_write = &null_sound;

	// reset the coin states
	coin_detected = 0;
	coin_last_reset = 0;

	// reset mux select
	mux_select = 0;
	MUX_VAL = 0;
	SOUNDBITS = 0;
	//Set Some Defaults
	CCPUROMSIZE = 4;
	//init_ccpu(0);
	video_type_set(COLOR_BILEVEL);

	switch (gamenum)
	{
	case STARHAWK: sound_write = &starhawk_sound;
		video_type_set(COLOR_BILEVEL);
		// ccpu_Config(1, CCPU_MEMSIZE_4K, CCPU_MONITOR_BILEV);
		CCPUROMSIZE = 4;
		init_ccpu(0);
		break;
	case RIPOFF:   sound_write = &ripoff_sound;
		video_type_set(COLOR_BILEVEL);
		//ccpu_Config(1, CCPU_MEMSIZE_8K, CCPU_MONITOR_BILEV);
		CCPUROMSIZE = 8;
		init_ccpu(0);
		break;

	case SOLARQ: sound_write = &solarq_sound;
		// ccpu_Config(1, CCPU_MEMSIZE_16K, CCPU_MONITOR_64LEV);
		video_type_set(COLOR_64LEVEL);
		CCPUROMSIZE = 16;
		init_ccpu(0);
		break;
	case STARCAS: sound_write = &starcas_sound;
		//ccpu_Config(1, CCPU_MEMSIZE_8K, CCPU_MONITOR_BILEV);
		CCPUROMSIZE = 8;
		video_type_set(COLOR_BILEVEL);
		init_ccpu(0);
		break;
	case ARMORA: sound_write = &armora_sound;
		//ccpu_Config(1, CCPU_MEMSIZE_16K, CCPU_MONITOR_BILEV);
		CCPUROMSIZE = 16;
		video_type_set(COLOR_BILEVEL);
		init_ccpu(0);
		break;
	case BARRIER: bSwapXY = 1; sound_write = &barrier_sound;
		video_type_set(COLOR_BILEVEL);
		init_ccpu(0);
		CCPUROMSIZE = 8;
		break;
	case SUNDANCE: bSwapXY = 1; sound_write = &sundance_sound;
		video_type_set(COLOR_16LEVEL);
		CCPUROMSIZE = 8;
		init_ccpu(0);
		break;
	case WARRIOR: sound_write = &warrior_sound;
		video_type_set(COLOR_BILEVEL);
		CCPUROMSIZE = 8;
		init_ccpu(0);
		break;

	case TAILG: sound_write = &tailg_sound;
		video_type_set(COLOR_BILEVEL);
		CCPUROMSIZE = 8;
		init_ccpu(1);
		break;

	case SPACEWAR: sound_write = &spacewar_sound;
		video_type_set(COLOR_BILEVEL);
		CCPUROMSIZE = 4;
		init_ccpu(1);
		break;

	case SPEEDFRK: sound_write = &null_sound;
		video_type_set(COLOR_BILEVEL);
		CCPUROMSIZE = 8;
		init_ccpu(1);
		break;

	case DEMON:    sound_write = &demon_sound;
		video_type_set(COLOR_BILEVEL);
		CCPUROMSIZE = 16;
		init_ccpu(0);
		break;

	case BOXINGB:  sound_write = &boxingb_sound;
		video_type_set(COLOR_RGB);
		CCPUROMSIZE = 32;
		init_ccpu(0);
		break;

	case WOTW:     sound_write = &wotwc_sound;
		video_type_set(COLOR_RGB);
		CCPUROMSIZE = 16;
		init_ccpu(0);
		break;
	}

	return 0;
}

void end_cinemat()
{
	//cineReset();

	cache_clear();
}


