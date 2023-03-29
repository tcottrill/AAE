/***************************************************************************

 *

 * Retrocade - Video game emulator													

 * Copyright 1998, The Retrocade group

 *																									

 * Permission to distribute the Retrocade executable *WITHOUT GAME ROMS* is		

 * granted to all. It may not be sold without the express written permission

 * of the Retrocade development team or appointed representative.

 *																									

 * Source code is *NOT* distributable without the express written 			

 * permission of the Retrocade group.

 *

 * Atari vector game platform extensions

 * 																								

 ***************************************************************************/



#include "rccore/global.h"

#include "rccore/games.h"

#include "rccore/cpu.h"

#include "platform/atari.h"

#include "graphics/vector.h"

#include "sound/sound.h"

#include "sound/pokey.h"

#include "sound/tms5220.h"

#include "sound/wav.h"

#include "graphics/display.h"

#include "rccore/control.h"

#include "cpucores/mathbox.h"

#include "rccore/memory.h"

#include "graphics/art.h"

#include "graphics/raster8.h"

#include "graphics/vector.h"

#include "osdep/osdep.h"

#include "rccore/registry.h"





// Defines for the Star Wars mathbox



#define READ_ACC      0x02

#define LAC           0x01

#define M_HALT        0x04

#define INC_BIC       0x08

#define CLEAR_ACC     0x10

#define LDC           0x20

#define LDB           0x40

#define LDA           0x80

#define LDA_CACC      0x90

#define LDB_CACC      0x50

#define NOP           0x00

#define R_ACC_M_HLT   0x06

#define INC_BIC_R_ACC 0x0a

#define INC_BIC_M_HLT 0x0c

#define INC_BIC_M_HLT_R_ACC 0x0e

#define C_ACC_M_HLT   0x14

#define LDC_M_HLT     0x24

#define LDB_M_HLT     0x44

#define LDA_M_HLT     0x84



#define SWFIFOSIZE	16



#define	XSTEP		3		// X Joystick step for Red Baron

#define	YSTEP		3		// Y Joystick step for Red Baron



// Controller stuff



enum

{

	// Asteroids



	ASTEROIDS_LEFT, ASTEROIDS_RIGHT, ASTEROIDS_FIRE, ASTEROIDS_HYPERSPACE,

	ASTEROIDS_THRUST, ASTEROIDS_COIN, ASTEROIDS_1PLAYER, ASTEROIDS_2PLAYER,

	ASTEROIDS_SELFTEST, ASTEROIDS_DIAGSTEP,



	// Asteroids Deluxe



	ASTERDLX_LEFT, ASTERDLX_RIGHT, ASTERDLX_FIRE, ASTERDLX_SHIELD,

	ASTERDLX_THRUST, ASTERDLX_COIN, ASTERDLX_1PLAYER, ASTERDLX_2PLAYER,

	ASTERDLX_SELFTEST,



	// Battlezone



	BATTLEZONE_LEFTFW, BATTLEZONE_LEFTRV, BATTLEZONE_RIGHTFW, BATTLEZONE_RIGHTRV,

	BATTLEZONE_FIRE, BATTLEZONE_START, BATTLEZONE_SELFTEST, BATTLEZONE_DIAGSTEP,



	// Space Duel



	SPACE_DUEL_P1LEFT, SPACE_DUEL_P1RIGHT, SPACE_DUEL_P1THRUST,

	SPACE_DUEL_P1FIRE, SPACE_DUEL_P1SHIELD,

	SPACE_DUEL_P2LEFT, SPACE_DUEL_P2RIGHT, SPACE_DUEL_P2THRUST,

	SPACE_DUEL_P2FIRE, SPACE_DUEL_P2SHIELD,

	SPACE_DUEL_SELECT, SPACE_DUEL_START, SPACE_DUEL_COIN,

	SPACE_DUEL_SELFTEST, SPACE_DUEL_DIAGSTEP,



	// Gravitar



	GRAVITAR_LEFT, GRAVITAR_RIGHT, GRAVITAR_THRUST, GRAVITAR_FIRE,

	GRAVITAR_SHIELD, GRAVITAR_P1START, GRAVITAR_P2START, GRAVITAR_COIN,



	// Black Widow



	BLACK_WIDOW_FIREUP, BLACK_WIDOW_FIREDOWN, BLACK_WIDOW_FIRELEFT,

	BLACK_WIDOW_FIRERIGHT, BLACK_WIDOW_MOVEUP, BLACK_WIDOW_MOVELEFT,

	BLACK_WIDOW_MOVERIGHT, BLACK_WIDOW_MOVEDOWN, BLACK_WIDOW_1PSTART,

	BLACK_WIDOW_2PSTART, BLACK_WIDOW_COIN,



	// Lunar Lander



	LUNAR_LANDER_LEFT, LUNAR_LANDER_RIGHT, LUNAR_LANDER_THRUST,

	LUNAR_LANDER_ABORT, LUNAR_LANDER_COIN, LUNAR_LANDER_SELECT,

	LUNAR_LANDER_START, LUNAR_LANDER_SELFTEST, LUNAR_LANDER_DIAGSTEP,



	// Tempest



	TEMPEST_1PSTART, TEMPEST_2PSTART, TEMPEST_SUPERZAP, TEMPEST_FIRE,

	TEMPEST_LEFT, TEMPEST_RIGHT, TEMPEST_SELFTEST, TEMPEST_DIAGSTEP,

	TEMPEST_SPINNER,



	// Red Baron



	RED_BARON_LEFT, RED_BARON_RIGHT, RED_BARON_UP, RED_BARON_DOWN,

	RED_BARON_FIRE, RED_BARON_START, RED_BARON_COIN, RED_BARON_HORIZ,

	RED_BARON_VERT,



	// Major Havoc



	MAJOR_HAVOC_COINLEFT, 

	MAJOR_HAVOC_COINAUX, MAJOR_HAVOC_COINRIGHT, MAJOR_HAVOC_SELFTEST,	

	MAJOR_HAVOC_DIAG_STEP, MAJOR_HAVOC_JUMP1, MAJOR_HAVOC_SHIELD1,

	MAJOR_HAVOC_JUMP2, MAJOR_HAVOC_SHIELD2, MAJOR_HAVOC_ROLLER_LEFT,

	MAJOR_HAVOC_ROLLER_RIGHT, MAJOR_HAVOC_HORIZ,



	// Star Wars

	

	STAR_WARS_LEFTFS, STAR_WARS_RIGHTFS, STAR_WARS_LTHUMB, STAR_WARS_RTHUMB,

	STAR_WARS_COIN, STAR_WARS_HORIZ, STAR_WARS_VERT, STAR_WARS_MLEFT,

	STAR_WARS_MRIGHT, STAR_WARS_MUP, STAR_WARS_MDOWN, STAR_WARS_COIN2, STAR_WARS_COIN3,

	STAR_WARS_SERVICE, STAR_WARS_STEP,



	// Tollian's Web



	TOLLIANS_WEB_COIN,



	// Quantum



	QUANTUM_COIN1, QUANTUM_COIN2, QUANTUM_START1, QUANTUM_START2,

	QUANTUM_TRAKX, QUANTUM_TRAKY,



};



// Function prototypes



UINT8 TempestToggle(UINT32 dwAddr, struct MemoryReadByte *pMemRead);

UINT8 SWGenHandler(UINT32 dwAddr, struct MemoryReadByte *pMemRead);

void SWGenWrite(UINT32 dwAddr, UINT8 bVal, struct MemoryWriteByte *pMemWrite);

UINT8 SWSInRead(UINT32 addr, struct MemoryReadByte *pMemRead);

UINT8 SWSPIARead(UINT32 addr, struct MemoryReadByte *pMemRead);

void SWSOutWrite(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void SWSPIAWrite(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

UINT8 SWSPIARead(UINT32 addr, struct MemoryReadByte *pMemRead);

void SWPRNGClear(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void SWADCStart(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void SWMathbox(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void SWSReset(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void SWMainWrite(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void SWSTimer(void);

void AsteroidsSwapRam(UINT32 addr, UINT8 swapval, struct MemoryWriteByte *pMemWrite);

void AsteroidsSound(UINT32 addr, UINT8 swapval, struct MemoryWriteByte *pMemWrite);

void AsteroidsVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

UINT8 SpinnerHandler(UINT32 addr, struct MemoryReadByte *pMemRead);

void AsteroidsNmi(void);

void LunarLanderNmi(void);

void AsteroidsTimerToggle(void);

void LunarLanderTimerToggle(void);

UINT8 BlackWidowHandler(UINT32 dwAddr, struct MemoryReadByte *pMemRead);

void BaronKeyboardJoystickHandler(void);

void TempestColorWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite);

UINT8 BaronJoystickRd(UINT32 addr, struct MemoryReadByte *pMemRead);

void MH2400hz(void);

void MHAlphaBankWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite);

void MHVectorWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite);

UINT8 MHVectorRead(UINT32 address, struct MemoryReadByte *pMemRead);

UINT8 MHAlphaBankRead(UINT32 dwAddress, struct MemoryReadByte *pMemRead);

void MHPlayerRamWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite);

UINT8 MHPlayerRamRead(UINT32 address, struct MemoryReadByte *pMemRead);

void MHGammaChangeBits(UINT32 dwAddress, UINT8 bByte, struct MemoryWriteByte *pMemWrite);

void MHGammaWriteAlpha(UINT32 dwAddress, UINT8 bData, struct MemoryWriteByte *pMemWrite);

UINT8 MHAlphaCommRead(UINT32 dwAddress, struct MemoryReadByte *pMemRead);

void SWVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void SWBankSwitch(UINT32 addr, UINT8 bank, struct MemoryWriteByte *pMemWrite);

void MajorHavocControllerCheck(void);



void QuantumPeriodicHandler(void);

void QuantumWriteMainCPUByte(UINT32 dwAddr, UINT8 bData, struct MemoryWriteByte *pWrite);

void QuantumWriteMainCPUWord(UINT32 dwAddr, UINT16 wData, struct MemoryWriteWord *pWrite);

UINT8 QuantumReadMainCPUByte(UINT32 dwAddr, struct MemoryReadByte *pRead);

UINT16 QuantumReadMainCPUWord(UINT32 dwAddr, struct MemoryReadWord *pRead);



UINT8 QuantumFunctionsReadByte(UINT32 dwAddr, struct MemoryReadByte *pRead);

UINT16 QuantumFunctionsReadWord(UINT32 dwAddr, struct MemoryReadWord *pRead);

void QuantumFunctionsWriteByte(UINT32 dwAddr, UINT8 bData, struct MemoryWriteByte *pWrite);

void QuantumFunctionsWriteWord(UINT32 dwAddr, UINT16 wData, struct MemoryWriteWord *pWrite);



UINT8 TolliansWebPlayerRamRead(UINT32 dwAddr, struct MemoryReadByte *pMemRead);

void TolliansWebPlayerRamWrite(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

UINT8 TolliansWebInputs(UINT32 dwAddr, struct MemoryReadByte *pMemRead);

void TolliansWebVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void TolliansWebVectorWrite(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

UINT8 TolliansWebVectorRead(UINT32 dwAddr, struct MemoryReadByte *pMemRead);

void TolliansWebBankSwitch(UINT32 dwAddr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void TolliansWebColorWrite(UINT32 dwAddr, UINT8 data, struct MemoryWriteByte *pMemWrite);





volatile static INT8 bJoystickX = 0;

static UINT16 wBaronX = 0x80;					// X Coordinate of Red Baron joystick

static UINT16 wBaronY = 0x80;

static UINT16 wStarWarsX = 0;

static UINT16 wStarWarsY = 0;

static UINT16 wStarWarsButtons = 0;	// 1=Left, 2=Right, 3=Up, 4=Down



void MathboxSaveState(struct sRegistryBlock **ppsReg)

{

	SAVE_DATA("Mout", &Mout, sizeof(UINT16), REG_UINT16)

	SAVE_DATA("r00", &r00, sizeof(UINT16) * 17, REG_UINT16)

}



void MathboxLoadState(struct sRegistryBlock **ppsReg)

{

	LOAD_DATA("Mout", &Mout, sizeof(UINT16))

	LOAD_DATA("r00", &r00, sizeof(UINT16))

}



/***************************************************************************

											ASTEROIDS

 ***************************************************************************/



// Default game configuration for games that are supported on this platform



struct sRomDef sAsteroidsRoms[] =

{

	{"035127.02",	0,	0x5000},

	{"035145.02",	0,	0x6800},

	{"035144.02",	0,	0x7000},

	{"035143.02",	0,	0x7800},

	{"035143.02",	0,	0xf800},



	// List terminator



	{NULL,	0,	0}

};



// High score saving area for Asteroids



struct sHighScore sAsteroidsHighScores[] =

{ 

	{0,	0x1d,	0x13},

	{0,	0x34,	0x22},

	{(UINT32) INVALID}

};



// Game controllers in Asteroids



struct sGameFunction sAsteroidsFunctions[] =

{

	{ASTEROIDS_LEFT,	"Left",	"left",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x2407, 0x00, 0xff, sizeof(UINT8), (void *) 0x2407, 0x00, 0x00},



	{ASTEROIDS_RIGHT, "Right", "right", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x2406, 0x00, 0xff, sizeof(UINT8), (void *) 0x2406, 0x00, 0x00},



	{ASTEROIDS_THRUST, "Thrust", "thrust", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x2405, 0x00, 0xff, sizeof(UINT8), (void *) 0x2405, 0x00, 0x00},



	{ASTEROIDS_FIRE, "Fire", "fire",	BINARY, VIRTUAL, 0,	

		sizeof(UINT8), (void *) 0x2004, 0x00, 0xff, sizeof(UINT8), (void *) 0x2004, 0x00, 0x00},



	{ASTEROIDS_HYPERSPACE, "Hyperspace", "hyperspace", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x2003, 0x00, 0xff, sizeof(UINT8), (void *) 0x2003, 0x00, 0x00},



	{ASTEROIDS_COIN, "Coin up", "coin", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x2400, 0x00, 0xff, sizeof(UINT8), (void *) 0x2400, 0x00, 0x00},



	{ASTEROIDS_1PLAYER, "1 Player start", "start1", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x2403, 0x00, 0xff, sizeof(UINT8), (void *) 0x2403, 0x00, 0x00},



	{ASTEROIDS_2PLAYER, "2 Player start", "start2", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x2404, 0x00, 0xff, sizeof(UINT8), (void *) 0x2404, 0x00, 0x00},



	{ASTEROIDS_SELFTEST, "Self test", "selftest", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x2007, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00},



	{ASTEROIDS_DIAGSTEP, "Diag step", "diagstep", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x2005, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00},



	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



// Fill regions for Asteroids



struct sMemoryInit AsteroidsFill[] =

{

	{0x2000,	0x2001,	0xff},		// Region for default status

	{0x2c00,	0x2c00,	0xff},		// Forgot what this does



	// Our terminator



	{(UINT32) INVALID, (UINT32) INVALID, 0}

};



// Asteroids read/write structures



struct MemoryWriteByte AsteroidsWrite[] =

{

	{0x2000,	0x2007,	NothingWrite},

	{0x3000,	0x3000,	AsteroidsVectorGenerator},

	{0x3200, 0x3200,  AsteroidsSwapRam},

	{0x3600,	0x3c05,	AsteroidsSound},

	{-1,		-1,		NULL}

};



struct MemoryReadByte AsteroidsRead[] =

{

	{-1,		-1,		NULL}

};



struct sWaveName sAsteroidsBoom[] =

{

	{"Asteroids/Low boom"},

	{"Asteroids/Medium boom"},

	{"Asteroids/High boom"},

	{NULL}

};



struct sWaveName sAsteroidsThump[] =

{

	{"Low thump"},

	{"High thump"},

	{NULL}

};



struct sWaveName sAsteroidsShipfire[] =

{

	{"Ship fire"},

	{NULL},

};



struct sWaveName sAsteroidsSaucerfire[] =

{

	{"Saucer fire"},

	{NULL},

};



struct sWaveName sAsteroidsThrust[] =

{

	{"Asteroids/Thrust"},

	{NULL},

};



struct sWaveName sAsteroidsLife[] =

{

	{"Life"},

	{NULL},

};



struct sWaveName sAsteroidsSaucer[] =

{

	{"Small saucer"},

	{"Big saucer"},

	{NULL},

};



struct sSoundDeviceEntry sAsteroidsSound[] =

{

	{DEV_WAV, 0x80, (void *) sAsteroidsBoom},

	{DEV_WAV, 0x80, (void *) sAsteroidsThrust},

	{DEV_WAV, 0x80, (void *) sAsteroidsThump},

	{DEV_WAV, 0x40, (void *) sAsteroidsShipfire},

	{DEV_WAV, 0x80, (void *) sAsteroidsSaucerfire},

	{DEV_WAV, 0x80, (void *) sAsteroidsLife},

	{DEV_WAV, 0x80, (void *) sAsteroidsSaucer},



	// List terminator



	{INVALID}

};



struct sCpu AsteroidsProcList[] =

{

	{CPU_6502ZP, 	RUNNING, 0x000100fa, 1500000, NULL, AsteroidsFill, AsteroidsWrite, 	AsteroidsRead, 

	 NULL, NULL, 0, 0, 250, AsteroidsTimerToggle, 6000, AsteroidsNmi},



	PROC_TERMINATOR

};



// Asteroids variable list



UINT8 lastswap = 0;						// Last swap value

char buffer[0x100];					// Swap buffer for Asteroids



void AsteroidsSwapRam(UINT32 addr, UINT8 swapval, struct MemoryWriteByte *pMemWrite)

{

	if ((swapval & 4) != lastswap)

	{

		lastswap = swapval & 0x4;

		MemoryCopy(buffer, psCurrentCpu->bMemBase + 0x200, 0x100);

		MemoryCopy(psCurrentCpu->bMemBase + 0x200, psCurrentCpu->bMemBase + 0x300, 0x100);

		MemoryCopy(psCurrentCpu->bMemBase + 0x300, buffer, 0x100);

	}

}



void AsteroidsDeluxeSwapRam(UINT32 addr, UINT8 swapval, struct MemoryWriteByte *pMemWrite)

{

	if ((swapval & 0x80) != lastswap)

	{

		lastswap = swapval & 0x80;

		MemoryCopy(buffer, psCurrentCpu->bMemBase + 0x200, 0x100);

		MemoryCopy(psCurrentCpu->bMemBase + 0x200, psCurrentCpu->bMemBase + 0x300, 0x100);

		MemoryCopy(psCurrentCpu->bMemBase + 0x300, buffer, 0x100);

	}

}



static UINT8 bLastFire = 0;

static UINT8 bLastSFire = 0;

static UINT8 bOldSaucer = 0;

static UINT8 bSaucerSelect = 0;

static UINT8 bThrust = 0;

static UINT8 bLife = 0;



void AsteroidsSound(UINT32 addr, UINT8 bData, struct MemoryWriteByte *pMemWrite)

{

	// Explosions



	if ( ((bData & 0x3f) == 0x3e) && (0x3600 == addr))

	{

		bData = bData >> 6;

		if (3 == bData)

			bData = 1;

		WavePlayback(0, bData);

		return;

	}



	// Thrust



	if (0x3c03 == addr)

	{

		if (bData & 0x80){

			if (!bThrust){

				WavePlayback(1, 0);

				bThrust = TRUE;

			}

		} else {

			if (bThrust){

				StopWavePlayback(1);

				bThrust = FALSE;

			}

		}

		return;

	}



	// Thump



	if (0x3a00 == addr)

	{

		if (bData & 0x10)

		{

			if (bData & 0x0f)

				WavePlayback(2, 0);

			else

				WavePlayback(2, 1);

		}

		return;

	}



	// Ship Fire



	if (0x3c04 == addr)

	{

		bData &= 0x80;

		if (bData == bLastFire)

			return;

		bLastFire = bData;



		if (bData & 0x80)

			WavePlayback(3, 0);

		else

			StopWavePlayback(3);

		return;

	}



	// Saucer Fire



	if (0x3c01 == addr)

	{

		if (bData){

			if (!bLastSFire){

				WavePlayback(4, 0);

				bLastSFire = TRUE;

			}

		} else {

			if (bLastSFire){

				StopWavePlayback(4);

				bLastSFire = FALSE;

			}

		}

		return;

	}



	// Life



	if (0x3c05 == addr)

	{

		if (bData & 0x80){

			if (!bLife){

				WavePlayback(5, 0);

				bLife = TRUE;

			}

		} else {

			if (bLife){

				StopWavePlayback(5);

				bLife = FALSE;

			}

		}

		return;

	}



	// Saucer select



	if (0x3c02 == addr)

	{

		bSaucerSelect = (bData >> 7);

		return;

	}



	// Saucer sound



	if (0x3c00 == addr)

	{

		// No need to restart!



		if ((bData & 0x80) && (bOldSaucer & 0x80))

			return;



		if (((bData & 0x80) == 0) && ((bOldSaucer & 0x80) == 0))

			return;



		bOldSaucer = bData;



		if (bData & 0x80)

			WavePlayback(6, bSaucerSelect);

		else

			StopWavePlayback(6);

		return;

	}

}





/************************************************************************

 *					

 * Name : AsteroidsVectorGenerator()

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine will be called when the DVG needs to be called.

 * 				

 ************************************************************************/



void AsteroidsVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	DigitalVectorGenerator((UINT16 *) (0x4000 + psCurrentCpu->bMemBase));

}



/************************************************************************

 *					

 * Name : AsteroidsTimerToggle()

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called every 3KHZ

 * 				

 ************************************************************************/



void AsteroidsTimerToggle(void)

{

	psCurrentCpu->bMemBase[0x2001] ^= 0x80;

}



static UINT8 bLastPoll = 0;



void AsteroidsNmi(void)

{

	if (bLastPoll != psCurrentCpu->bMemBase[0x2007])	// Something has changed!

	{

		bLastPoll = psCurrentCpu->bMemBase[0x2007];

		if (0 == bLastPoll)	// If we're going out of test mode, reset RAM!

		{

			bResetCpus = 1;

			return;

		}

		else

			CpuReset(0);		// Normal reset

	} 



	// Only do an NMI if we're *NOT* in self test mode



	if ((psCurrentCpu->bMemBase[0x2007] & 0x80) == 0)

		CpuNMI(0);

}



UINT32 AsteroidsSetup(struct sRegistryBlock **ppsReg)

{

	if (ppsReg)

	{

		// Load stuff up here



		LOAD_DATA("bLastFire", &bLastFire, sizeof(UINT8))

		LOAD_DATA("bLastSFire", &bLastSFire, sizeof(UINT8))

		LOAD_DATA("bOldSaucer", &bOldSaucer, sizeof(UINT8))

		LOAD_DATA("bSaucerSelect", &bSaucerSelect, sizeof(UINT8))

		LOAD_DATA("bThrust", &bThrust, sizeof(UINT8))

		LOAD_DATA("bLife", &bLife, sizeof(UINT8))

		LOAD_DATA("bLastPoll", &bLastPoll, sizeof(UINT8))

		LOAD_DATA("lastswap", &lastswap, sizeof(UINT8))

		LOAD_DATA("buffer", &buffer, sizeof(buffer))



		return(RETRO_SETUPOK);

	}

	else

	{

		return(RETRO_SETUPOK);

	}



}



UINT32 AsteroidsShutdown(struct sRegistryBlock **ppsReg)

{

	if (ppsReg)

	{

		// Load stuff up here



		SAVE_DATA("bLastFire", &bLastFire, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bLastSFire", &bLastSFire, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bOldSaucer", &bOldSaucer, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bSaucerSelect", &bSaucerSelect, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bThrust", &bThrust, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bLife", &bLife, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bLastPoll", &bLastPoll, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("lastswap", &lastswap, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("buffer", &buffer, sizeof(UINT8), REG_UINT8)



		return(RETRO_SETUPOK);

	}

	else

	{

		return(RETRO_SETUPOK);

	}



}



/***************************************************************************

										ASTEROIDS DELUXE

 ***************************************************************************/



UINT8 AsteroidsDeluxeRangeRead(UINT32 dwAddr, struct MemoryReadByte *pMemRead);





// Default game configuration for games that are supported on this platform



struct sRomDef sDeluxeRoms[] =

{

	// Vector ROMs

	

	{"036800.02",	0,	0x4800},

	{"036799.01",	0,	0x5000},



	// Game ROMs



	{"036430.02",	0,	0x6000},

	{"036431.02",	0,	0x6800},

	{"036432.02",	0,	0x7000},

	{"036433.03",	0,	0x7800},



	// Here for restart vector



	{"036433.03",	0,	0xf800},



	// List terminator



	{NULL,	0,	0}

};



// Fill regions for Deluxe



struct sMemoryInit DeluxeFill[] =

{

	{0x2000,	0x2001,	0xff},		// Region for default status

	{0x2c00,	0x2c00,	0xff},		// Forgot what this does



	// Our terminator



	{(UINT32) INVALID, (UINT32) INVALID, 0}

};



// High score saving area for Asteroids Deluxe



struct sHighScore sDeluxeHighScores[] =

{ 

	{0,	0x62,	0x6},

	{0,	0x23,	0x1e},

	{0,	0x47, 0x0e},

	{(UINT32) INVALID}

};



// Deluxe read/write structures



struct MemoryWriteByte DeluxeWrite[] =

{

	{0x3000, 0x3000,	AsteroidsVectorGenerator},

	{0x3c04, 0x3c04,	AsteroidsDeluxeSwapRam},

	{0x2000, 0x2007,	NothingWrite},

	{0x2400, 0x2407,	NothingWrite},

	{0x2c00, 0x2c0f,	PokeyWrite,	bPokey1},

	{0x3600, 0x3600,	AsteroidsSound},

	{0x3c03, 0x3c03,	AsteroidsSound},

	{-1,		-1,		NULL}

};



struct MemoryReadByte DeluxeRead[] =

{

	{0x2000,	0x2c0f,	AsteroidsDeluxeRangeRead,	bPokey1},

	{-1,		-1,		NULL}

};



struct sCpu DeluxeProcList[] =

{

	{CPU_6502ZP, 	RUNNING, 0x00010064, 1500000, NULL, DeluxeFill, DeluxeWrite, 	DeluxeRead, 

	 NULL, 

	 NULL, 

	 0, 

	 0, 

	 250, 

	 AsteroidsTimerToggle, 

	 6000, 

	 AsteroidsNmi},



	PROC_TERMINATOR

};



// Game controllers in Asteroids Deluxe



struct sGameFunction sAsteroidsDeluxeFunctions[] =

{

	{ASTERDLX_LEFT,	"Left",	"left",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x2407, 0x00, 0xff, sizeof(UINT8), (void *) 0x2407, 0x00, 0x00},



	{ASTERDLX_RIGHT, "Right", "right", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x2406, 0x00, 0xff, sizeof(UINT8), (void *) 0x2406, 0x00, 0x00},



	{ASTERDLX_THRUST, "Thrust", "thrust", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x2405, 0x00, 0xff, sizeof(UINT8), (void *) 0x2405, 0x00, 0x00},



	{ASTERDLX_FIRE, "Fire", "fire",	BINARY, VIRTUAL, 0,	

		sizeof(UINT8), (void *) 0x2004, 0x00, 0xff, sizeof(UINT8), (void *) 0x2004, 0x00, 0x00},



	{ASTERDLX_SHIELD, "Shield", "shield", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x2003, 0x00, 0xff, sizeof(UINT8), (void *) 0x2003, 0x00, 0x00},



	{ASTERDLX_COIN, "Coin up", "coin", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x2400, 0x00, 0xff, sizeof(UINT8), (void *) 0x2400, 0x00, 0x00},



	{ASTERDLX_1PLAYER, "1 Player start", "start1", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x2403, 0x00, 0xff, sizeof(UINT8), (void *) 0x2403, 0x00, 0x00},



	{ASTERDLX_2PLAYER, "2 Player start", "start2", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x2404, 0x00, 0xff, sizeof(UINT8), (void *) 0x2404, 0x00, 0x00},



	{ASTERDLX_SELFTEST, "Self test", "selftest", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x2000, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00},



	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



struct sSoundDeviceEntry sDeluxeSound[] =

{

	{DEV_POKEY_CLUSTER, 0x80, (void *) 1, (void *) 1373300, 0},		// One pokey for this game

	{DEV_WAV,			0x60, (void *) &sAsteroidsBoom},

	{DEV_WAV,			0x80, (void *) &sAsteroidsThrust},



	// List terminator



	{INVALID}

};



static UINT8 bAsteroidsDeluxeIN;

static UINT8 bAsteroidsDeluxeDSW;



struct sDipSwitch sAsteroidsDeluxeDipSwitches[] = {

	{0x00,	ACTUAL,	0,	&bAsteroidsDeluxeIN},

	{0x04,	ACTUAL,	0,	&bAsteroidsDeluxeDSW},

	{0x9d,	ACTUAL,	0,	&bPokey1[ALLPOT_C]},

	

	DIP_TERMINATOR

};



struct sDipEnum sAsteroidsDeluxeLanguageChoices[4] = {

	{"English", 0}, {"German", 1}, {"French", 2}, {"Spanish", 3}

};



struct sDipEnum sAsteroidsDeluxeShipsChoices[4] = {

	{"2", 0}, {"3", 1}, {"4", 2}, {"5", 3}

};



struct sDipEnum sAsteroidsDeluxeMinimumPlayChoices[2] = {

	{"1", 0}, {"2", 1}

};



struct sDipEnum sAsteroidsDeluxeDifficultyChoices[2] = {

	{"Easy", 1}, {"Hard", 0},

};



struct sDipEnum sAsteroidsDeluxeBonusChoices[4] = {

	{"10000", 0}, {"12000", 1}, {"15000", 2}, {"None", 3}

};



struct sDipEnum sAsteroidsDeluxeCoinsChoices[4] = {

	{"1 Coin / 1 Play", 1}, 	{"1 Coin / 2 Plays", 2},

	{"2 Coins / 1 Play", 0},	{"Free Play", 3}

};



struct sDipEnum sAsteroidsDeluxeBonusCoinsChoices[5] = {

	{"1 each 2", 6}, 	{"1 each 4", 5},	{"1 each 5", 3},

	{"2 each 4", 4},	{"None", 7}

};



struct sDipField sAsteroidsDeluxeDipFields[] = {

	// Dip switches at 0x2000-0x2007

	{

		"Service Mode",

		"Puts the game in service mode.",

		0, 7, 0x80,

		DIP_ENUM(StdOffOnChoices)

	},

    // Dip switches at 0x2800-0x2803

    {

    	"Language",

    	"Selects the language the game will display.",

    	1, 0, 0x03,

    	DIP_ENUM(sAsteroidsDeluxeLanguageChoices)

    },

    {

    	"Ships",

    	"Sets the number of player lives per game.",

    	1, 2, 0x0c,

    	DIP_ENUM(sAsteroidsDeluxeShipsChoices)

    },

    {

		"Minimum Plays",

		"Sets the minimum number of credits required to play.",

		1, 4, 0x10,

		DIP_ENUM(sAsteroidsDeluxeMinimumPlayChoices)

	},

	{

		"Difficulty",

		"Sets the game's difficulty.",

		1, 5, 0x20,

		DIP_ENUM(sAsteroidsDeluxeDifficultyChoices)

	},

	{

		"Bonus Ship",

		"Sets the number of points required for a bonus ship.",

		1, 6, 0xc0,

		DIP_ENUM(sAsteroidsDeluxeBonusChoices)

	},

	// Dipswitches at 0x2c08

	{

		"Coins/Credit",

		"Sets the number of coins per play.",

		2, 0, 0x03,

		DIP_ENUM(sAsteroidsDeluxeCoinsChoices)

	},

	{

		"Bonus Coins",

		"Sets the number of coins required for bonus coin(s).",

		2, 5, 0xe0,

		DIP_ENUM(sAsteroidsDeluxeBonusCoinsChoices)

	},

	{NULL}

};



UINT8 AsteroidsDeluxeRangeRead(UINT32 dwAddr, struct MemoryReadByte *pMemRead)

{

	if (0x2007 == dwAddr)

	{	

		return bAsteroidsDeluxeIN & 0x80;		                

	}

	else

	if ((dwAddr >= 0x2800) && (dwAddr <= 0x2803))

	{

		return (bAsteroidsDeluxeDSW >> ((0x2803 - dwAddr) << 1)) | 0xfc;

	}

	else

	if ((dwAddr >= 0x2c00) && (dwAddr <= 0x2c0f))

	{

		return PokeyRead(dwAddr, pMemRead);

	}

	else

		return psCurrentCpu->bMemBase[dwAddr];

}



/***************************************************************************

										Lunar Lander

 ***************************************************************************/



// Default game configuration for games that are supported on this platform

void LunarLanderSound(UINT32 addr, UINT8 swapval, struct MemoryWriteByte *pMemWrite);





struct sRomDef sLunarLanderRoms[] =

{

	// Vector ROMs

	

	{"034599.01",	0,	0x4800},

	{"034598.01",	0,	0x5000},

	{"034597.01",	0,	0x5800},



	// Game ROMs



	{"034572.02",	0,	0x6000},

	{"034571.02",	0,	0x6800},

	{"034570.01",	0,	0x7000},

	{"034569.02",	0,	0x7800},



	// Here for restart vector



	{"034569.02",	0,	0xf800},



	// List terminator



	{NULL,	0,	0}

};



// Fill regions for LunarLander



struct sMemoryInit LunarLanderFill[] =

{

	{0x2000,	0x2000,	0x87},		// Region for default status

	{0x2800,	0x2801,	0x03},

	{0x2802,	0x2803,	0x00},



	// Our terminator



	{(UINT32) INVALID, (UINT32) INVALID, 0}

};



// High score saving area for Lunar Lander



struct sHighScore sLunarLanderHighScores[] =

{ 

	{0,	0xa4,	0x02},	// Just the last score

	{(UINT32) INVALID}

};



// LunarLander read/write structures



struct MemoryWriteByte LunarLanderWrite[] =

{

	{0x2000, 0x2fff,	NothingWrite},

	{0x3000, 0x3000,	AsteroidsVectorGenerator},

	{0x3c00, 0x3c00,	LunarLanderSound},

	{-1,		-1,		NULL}

};



struct MemoryReadByte LunarLanderRead[] =

{

	{-1,		-1,		NULL}

};



struct sCpu LunarLanderProcList[] =

{

	{CPU_6502ZP, RUNNING, 0x00010064, 1500000,  NULL, &LunarLanderFill[0], LunarLanderWrite, LunarLanderRead, 

	 NULL, NULL, 0, 0, 250, LunarLanderTimerToggle, 6000, LunarLanderNmi},



	PROC_TERMINATOR

};



// Game controllers in Lunar Lander



struct sGameFunction sLunarLanderFunctions[] =

{

	{LUNAR_LANDER_LEFT,	"Rotate left",	"left",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x2407, 0x00, 0xff, sizeof(UINT8), (void *) 0x2407, 0x00, 0x00},



	{LUNAR_LANDER_RIGHT,	"Rotate right","right",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x2406, 0x00, 0xff, sizeof(UINT8), (void *) 0x2406, 0x00, 0x00},



	{LUNAR_LANDER_THRUST,"Thrust",		"thrust",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0xc000, 0x00, 0xff, sizeof(UINT8), (void *) 0xc000, 0x00, 0x00},



	{LUNAR_LANDER_ABORT,	"Abort",			"abort",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x2405, 0x00, 0xff, sizeof(UINT8), (void *) 0x2405, 0x00, 0x00},



	{LUNAR_LANDER_COIN,	"Coin up",		"coin",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x2401, 0x00, 0x00, sizeof(UINT8), (void *) 0x2401, 0x00, 0xff},



	{LUNAR_LANDER_SELECT,"Select game",	"select",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x2404, 0x00, 0x00, sizeof(UINT8), (void *) 0x2404, 0x00, 0xff},





	{LUNAR_LANDER_START,	"Start game",	"start1",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x2400, 0x00, 0xff, sizeof(UINT8), (void *) 0x2400, 0x00, 0x00},



	{LUNAR_LANDER_SELFTEST, "Self test", "selftest", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x2000, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00},



	{LUNAR_LANDER_DIAGSTEP, "Diag step", "diagstep", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x2000, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00},



	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



struct sWaveName sLunarLanderThrust[] =

{

	{"Thrust"},

	{NULL},

};



struct sWaveName sLunarLanderExplosion[] =

{

	{"Explosion"},

	{NULL},

};



struct sWaveName sLunarLanderBeep[] =

{

	{"Beep"},

	{NULL},

};



struct sSoundDeviceEntry sLunarLanderSound[] =

{

	{DEV_WAV, 0x00, (void *) sLunarLanderThrust},

	{DEV_WAV, 0x80, (void *) sLunarLanderExplosion},

	{DEV_WAV, 0x80, (void *) sLunarLanderBeep},



	// List terminator



	{INVALID}

};



static UINT8 bLunarKeyboard = 0;	// Here for keyboarding....



void LunarLanderNmi()

{

	if (bLastPoll != (psCurrentCpu->bMemBase[0x2000] & 0x02))	// Something has changed!

	{

		bLastPoll = psCurrentCpu->bMemBase[0x2000] & 0x02;

		if (0 == bLastPoll)	// If we're going out of test mode, reset RAM!

		{

			bResetCpus = 1;

			return;

		}

		else

			CpuReset(0);		// Normal reset

	} 



	// Only do an NMI if we're *NOT* in self test mode



	if (psCurrentCpu->bMemBase[0x2000] & 0x02)

		CpuNMI(0);

}



static UINT32 dwKeyCount = 0;



void LunarLanderTimerToggle()

{

	psCurrentCpu->bMemBase[0x2000] ^= 0x40;



	dwKeyCount++;

	if (dwKeyCount < 20)

		return;



	dwKeyCount = 0;



	// If this is true, the thrust is being held down



	if (psCurrentCpu->bMemBase[0xc000])

		bLunarKeyboard = 1;



	// If this is true, our keyboard handler isn't doing squat.



	if (0 == bLunarKeyboard)

		return;



	// Either the key is being held down, or the value is decrementing



	if (psCurrentCpu->bMemBase[0xc000] != 0 && psCurrentCpu->bMemBase[0x2c00] != 0xfe) 

		psCurrentCpu->bMemBase[0x2c00] += 2;

	else

	if (psCurrentCpu->bMemBase[0xc000] == 0 && psCurrentCpu->bMemBase[0x2c00] != 0)

		psCurrentCpu->bMemBase[0x2c00] -= 2;



	if (0 == psCurrentCpu->bMemBase[0x2c00])

		bLunarKeyboard = 0;

}





void LunarLanderSound(UINT32 addr, UINT8 bData, struct MemoryWriteByte *pMemWrite)

{

static	UINT8 bThrust = FALSE;

static	UINT8 bExplode = FALSE;

static	UINT8 bBeep = FALSE;

static	int	bLastThrust = 0;

UINT8	bIntensity;

UINT8	bTone;

UINT8	bPitch;

UINT8	bVolume[8] = {

	0x00,

	0x10,

	0x20,

	0x30,

	0x40,

	0x50,

	0x68,

	0x80

};





	bPitch = bData & 0x08;

	bIntensity = bData & 0x07;

	bTone = bData & 0x30;



	if (!bThrust){

		// thrust plays continuously...

		WavePlayback(0, 0);

		bThrust = TRUE;

	}





	if (bTone){

		// beep sound

		if (!bBeep){

			bBeep = TRUE;

			WavePlayback(2, 0);		// play beep

		}

	}

	else if (bPitch){

		// explosion sound

		if (!bExplode){

			bExplode = TRUE;

			SetChannelVolume(0, 0);	// silence thrust

			StopWavePlayback(2);	// stop beep

			bBeep = FALSE;

			WavePlayback(1, 0);		// play explosion

		}

	} else {

		bExplode = FALSE;



		// thrust sound

		if (bLastThrust != bIntensity){

			// set thrust volume

			SetChannelVolume(0, bVolume[bIntensity] );

			bLastThrust = bIntensity;

		}



	}



	if (!bTone){

		if (bBeep){

			StopWavePlayback(2);	// stop beep

			bBeep = FALSE;

		}

	}



}





/************************************************************************

 *					

 * Name : LunarLanderSetup()

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called just before LunarLander starts executing

 * 				

 ************************************************************************/



UINT32 LunarLanderSetup(struct sRegistryBlock **ppsRegBlock)

{

	if (ppsRegBlock)

	{

		// Do any necessary loading from saved file registry here



		

		return(RETRO_SETUPOK);

	}

	else

	{

		bLunarKeyboard = 0;		// Make sure we're not running!

		return(RETRO_SETUPOK);

	}

}



/***************************************************************************

										  SPACE DUEL

 ***************************************************************************/



void SpaceDuelVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void SpaceDuelTimerToggle();



// Default game configuration for games that are supported on this platform



struct sRomDef sLunarBattleRoms[] =

{

	// Vector ROMs



	{"2800.716",0,	0x2800},

	{"3000.532", 0, 0x3000},



	// Main program ROMs



	{"4000.532", 0, 0x4000},

	{"5000.532",0,	0x5000},

	{"6000.532",	0,	0x6000},

	{"7000.532",0, 0x7000},

	{"8000.532",	0, 0x8000},

	{"9000.532",0,	0x9000},



	// Duplicated so it can get a reset vector



	{"9000.532", 0, 0xf000},



	// List terminator



	{NULL,	0,	0}

};



struct sRomDef sSpaceDuelRoms[] =

{

	// Vector ROMs



	{"136006.106",	0,	0x2800},

	{"136006.107",	0,	0x3000},



	// Main program ROMs



	{"136006.201", 0, 0x4000},

	{"136006.102",	0,	0x5000},

	{"136006.103",	0,	0x6000},

	{"136006.104", 0, 0x7000},

	{"136006.105",	0, 0x8000},



	// Duplicated so it can get a reset vector



	{"136006.105", 0, 0xf000},



	// List terminator



	{NULL,	0,	0}

};



// Fill regions for SpaceDuel



struct sMemoryInit SpaceDuelFill[] =

{

	{0x1408,	0x1408,	0x03},		// Switch settings

	{0x1008,	0x1008,	0x02},		// Here as well

	{0x0800,	0x0800,	0xff},		// Prevents test mode



	// Our terminator



	{(UINT32) INVALID, (UINT32) INVALID, 0}

};



// SpaceDuel read/write structures



struct MemoryWriteByte SpaceDuelWrite[] =

{

	{0x0c80,	0x0c80,	SpaceDuelVectorGenerator},

	{0x0905,	0x0905,	NothingWrite},

	{0x1000, 0x100f,	PokeyWrite, bPokey1},

	{0x1400, 0x140f,	PokeyWrite, bPokey2},

	{0x4000, 0xffff,	NothingWrite},	// Protect the ROM!

	{-1,		-1,		NULL}

};



struct MemoryReadByte SpaceDuelRead[] =

{

	{0x1000, 0x100f,	PokeyRead, bPokey1},

	{0x1400, 0x140f,	PokeyRead, bPokey2},

	{-1,		-1,		NULL}

};



struct sCpu SpaceDuelProcList[] =

{

	{CPU_6502ZP, RUNNING, 0x00010064, 1500000,  NULL, &SpaceDuelFill[0], SpaceDuelWrite, SpaceDuelRead, 

	 NULL, NULL, 0, 0x1800, 0, 0, 0, NULL},



	PROC_TERMINATOR

};



struct sGameFunction sSpaceDuelFunctions[] =

{

	{SPACE_DUEL_P1LEFT,	"P1 Left", "p1left", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x0902, 0xff, 0x80, sizeof(UINT8), (void *) 0x0902, 0x7f, 0x00},



	{SPACE_DUEL_P1RIGHT,	"P1 Right", "p1right", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x0902, 0xff, 0x40, sizeof(UINT8), (void *) 0x0902, 0xbf, 0x00},



	{SPACE_DUEL_P1THRUST,"P1 Thrust", "p1thrust", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x0904, 0xff, 0x80, sizeof(UINT8), (void *) 0x0904, 0x7f, 0x00},



	{SPACE_DUEL_P1FIRE, "P1 Fire", "p1fire", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x0900, 0xff, 0x40, sizeof(UINT8), (void *) 0x900, 0xbf, 0x00},



	{SPACE_DUEL_P1SHIELD, "P1 Sheild", "p1shield", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x0900, 0xff, 0x80, sizeof(UINT8), (void *) 0x900, 0x7f, 0x00},



	{SPACE_DUEL_P2LEFT,	"P2 Left",	"p2left", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x0903, 0xff, 0x80, sizeof(UINT8), (void *) 0x903, 0x7f, 0x00},



	{SPACE_DUEL_P2RIGHT, "P2 Right", "p2right", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x0903, 0xff, 0x40, sizeof(UINT8), (void *) 0x903, 0xbf, 0x00},



	{SPACE_DUEL_P2THRUST, "P2 Thrust", "p2thrust", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x905, 0xff, 0x80, sizeof(UINT8), (void *) 0x905, 0x7f, 0x00},



	{SPACE_DUEL_P2FIRE, "P2 Fire", "p2fire", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x901, 0xff, 0x40, sizeof(UINT8), (void *) 0x901, 0xbf, 0x00},



	{SPACE_DUEL_P2SHIELD, "P2 Shield", "p2shield", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x901, 0xff, 0x80, sizeof(UINT8), (void *) 0x901, 0x7f, 0x00},



	{SPACE_DUEL_SELECT, "Select", "select", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x906, 0xff, 0x80, sizeof(UINT8), (void *) 0x906, 0x7f, 0x00},



	{SPACE_DUEL_START,	"Start",	"start1",	BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x904, 0xff, 0x40, sizeof(UINT8), (void *) 0x904, 0xbf, 0x00},



	{SPACE_DUEL_COIN,	"Coin",	"coin",	BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x800, 0xfe, 0x00, sizeof(UINT8), (void *) 0x800, 0xff, 0x01},



	{SPACE_DUEL_SELFTEST, "Self test", "selftest", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x800, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00},



	{SPACE_DUEL_DIAGSTEP, "Diag step", "diagstep", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x800, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00},



	// Terminator



	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



// Audio information



struct sSoundDeviceEntry sSpaceDuelSound[] =

{

	{DEV_POKEY_CLUSTER, 0x80, (void *) 2, (void *) 1373300, (void *) 0},		// Two pokeys for this game - CPU # 0 drives it



	// List terminator



	{INVALID}

};



/************************************************************************

 *					

 * Name : 

 *					

 * Entry: 

 *					

 * Exit : 

 *					

 * Description:

 *					

 * 

 * 				

 ************************************************************************/



void SpaceDuelVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	AnalogVectorGenerator((UINT16 *) (0x2000 + psCurrentCpu->bMemBase), FALSE, FALSE);

}



/***************************************************************************

										  GRAVITAR

 ***************************************************************************/



void GravitarVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void GravitarTimerToggle();



// Default game configuration for games that are supported on this platform

	

struct sRomDef sGravitarRoms[] =

{

	// Vector ROMs



	{"136010.210",	0,	0x2800},

	{"136010.207",	0,	0x3000},

	{"136010.208",	0,	0x4000},

	{"136010.209",	0,	0x5000},



	// Main program ROMs



	{"136010.201", 0, 0x9000},

	{"136010.202",	0,	0xa000},

	{"136010.203",	0,	0xb000},

	{"136010.204", 0, 0xc000},

	{"136010.205",	0, 0xd000},

	{"136010.206", 0,	0xe000},

	{"136010.206", 0, 0xf000},



	// List terminator



	{NULL,	0,	0}

};



// Fill regions for Gravitar



struct sMemoryInit GravitarFill[] =

{

	{0x7800,	0x7800,	0xff},		// Switch settings

	{0x8800,	0x8800,	0xff},		// Here as well

	{0x8000,	0x8000,	0xff},		// Prevents test mode

	{0x6808,	0x6808,	0x02},

	{0x6008,	0x6008,	0x10},



	// Our terminator



	{(UINT32) INVALID, (UINT32) INVALID, 0}

};



// Game controllers in Gravitar



struct sGameFunction sGravitarFunctions[] =

{

	{GRAVITAR_LEFT,	"Rotate Left",	"left",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x8000, 0xf7, 0x00, sizeof(UINT8), (void *) 0x8000, 0xff, 0x08},



	{GRAVITAR_RIGHT, "Rotate Right",	"right",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x8000, 0xfb, 0x00, sizeof(UINT8), (void *) 0x8000, 0xff, 0x04},



	{GRAVITAR_THRUST,	"Thrust",	"thrust",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x8000, 0xef, 0x00, sizeof(UINT8), (void *) 0x8000, 0xff, 0x10},



	{GRAVITAR_FIRE,	"Fire",	"fire",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x8000, 0xfd, 0x00, sizeof(UINT8), (void *) 0x8000, 0xff, 0x02},



	{GRAVITAR_SHIELD,	"Shield",	"shield",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x8000, 0xfe, 0x00, sizeof(UINT8), (void *) 0x8000, 0xff, 0x01},



	{GRAVITAR_P1START,	"1 Player start",	"start1",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x8800, 0xdf, 0x00, sizeof(UINT8), (void *) 0x8800, 0xff, 0x20},



	{GRAVITAR_P2START,	"2 Player start",	"start2",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x8800, 0xbf, 0x00, sizeof(UINT8), (void *) 0x8800, 0xff, 0x40},



	{GRAVITAR_COIN,		"Coin",	"coin", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x7800, 0xfd, 0x00, sizeof(UINT8), (void *) 0x7800, 0xff, 0x02},



	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



// Gravitar read/write structures



struct MemoryWriteByte GravitarWrite[] =

{

	{0x8840, 0x8840,	GravitarVectorGenerator},

	{0x8800, 0x8800,	NothingWrite},

	{0x6000, 0x600f,	PokeyWrite, bPokey1},

	{0x6800, 0x680f,	PokeyWrite, bPokey2},

	{-1,		-1,		NULL}

};



struct MemoryReadByte GravitarRead[] =

{

	{0x7800,	0x7800,	BlackWidowHandler},

	{0x6000, 0x600f,	PokeyRead, bPokey1},

	{0x6800, 0x680f,	PokeyRead, bPokey2},

	{-1,		-1,		NULL}

};



struct sCpu GravitarProcList[] =

{

	{CPU_6502ZP, RUNNING, 6150, 1500000,  NULL, &GravitarFill[0], GravitarWrite, GravitarRead,

	 NULL, NULL, 0, 6150, 0, 0, 0, NULL},



	PROC_TERMINATOR

};



struct sSoundDeviceEntry sGravitarSound[] =

{

	{DEV_POKEY_CLUSTER, 0x80, (void *) 2, (void *) 1373300, (void *) 0},		// Two pokeys for this game - CPU # 0 drives it



	// List terminator



	{INVALID}

};



/************************************************************************

 *					

 * Name : GravitarVectorGenerator

 *					

 * Entry: Address/value

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called when Gravitar's vector generator is pulsed

 * 				

 ************************************************************************/



void GravitarVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	AnalogVectorGenerator((UINT16 *) (0x2000 + psCurrentCpu->bMemBase), FALSE, FALSE);

}



/***************************************************************************

										  Black Widow

 ***************************************************************************/



void BlackWidowVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void BlackWidowTimerToggle();



// Default game configuration for games that are supported on this platform

	

struct sRomDef sBlackWidowRoms[] =

{

	// Vector ROMs



	{"136017.107",	0,	0x2800},

	{"136017.108",	0,	0x3000},

	{"136017.109",	0,	0x4000},

	{"136017.110",	0,	0x5000},



	// Main program ROMs



	{"136017.101", 0, 0x9000},

	{"136017.102",	0,	0xa000},

	{"136017.103",	0,	0xb000},

	{"136017.104", 0, 0xc000},

	{"136017.105",	0, 0xd000},

	{"136017.106", 0,	0xe000},

	{"136017.106", 0, 0xf000},



	// List terminator



	{NULL,	0,	0}

};



// Fill regions for BlackWidow



struct sMemoryInit BlackWidowFill[] =

{

	{0x7800,	0x7800,	0xff},		// Switch settings

	{0x8800,	0x8800,	0xff},		// Here as well

	{0x8000,	0x8000,	0xff},		// Prevents test mode



	// Our terminator



	{(UINT32) INVALID, (UINT32) INVALID, 0}

};



// BlackWidow read/write structures



struct MemoryWriteByte BlackWidowWrite[] =

{

	{0x2800, 0x2fff,	NothingWrite},

	{0x8840, 0x8840,	BlackWidowVectorGenerator},

	{0x6000, 0x600f,	PokeyWrite, bPokey1},

	{0x6800, 0x680f,	PokeyWrite, bPokey2},

	{0x7800, 0x7800,	NothingWrite},

	{0x8000, 0x8000,	NothingWrite},

	{0x8800, 0x8800,	NothingWrite},

	{0x9000,	0xffff,	NothingWrite},	/* Avoid trashed ROM image */

	{-1,		-1,		NULL}

};



struct MemoryReadByte BlackWidowRead[] =

{

	{0x7800,	0x7800,	BlackWidowHandler},

	{0x6000, 0x600f,	PokeyRead, bPokey1},

	{0x6800, 0x680f,	PokeyRead, bPokey2},

	{-1,		-1,		NULL}

};



struct sCpu BlackWidowProcList[] =

{

	{CPU_6502ZP, RUNNING, 0x00010080, 2400000, NULL, &BlackWidowFill[0], BlackWidowWrite, BlackWidowRead,

	 NULL, NULL, 0, 8192, 0, 0, 0, NULL},



	PROC_TERMINATOR

};



// Game controllers in Black Widow



struct sGameFunction sBlackWidowFunctions[] =

{

	{BLACK_WIDOW_FIREUP, "Fire up", "fireup", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x8800, 0xf7, 0x0, sizeof(UINT8), (void *) 0x8800, 0xff, 0x08},



	{BLACK_WIDOW_FIREDOWN, "Fire down", "firedown", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x8800, 0xfb, 0x0, sizeof(UINT8), (void *) 0x8800, 0xff, 0x04},



	{BLACK_WIDOW_FIRELEFT, "Fire left", "fireleft", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x8800, 0xfd, 0x0, sizeof(UINT8), (void *) 0x8800, 0xff, 0x02},



	{BLACK_WIDOW_FIRERIGHT, "Fire right", "fireright", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x8800, 0xfe, 0x0, sizeof(UINT8), (void *) 0x8800, 0xff, 0x01},



	{BLACK_WIDOW_MOVEUP, "Move up", "moveup", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x8000, 0xf7, 0x0, sizeof(UINT8), (void *) 0x8000, 0xff, 0x08},



	{BLACK_WIDOW_MOVEDOWN, "Move down", "movedown", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x8000, 0xfb, 0x0, sizeof(UINT8), (void *) 0x8000, 0xff, 0x04},



	{BLACK_WIDOW_MOVELEFT, "Move left", "moveleft", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x8000, 0xfd, 0x0, sizeof(UINT8), (void *) 0x8000, 0xff, 0x02},



	{BLACK_WIDOW_MOVERIGHT, "Move right", "moveright", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x8000, 0xfe, 0x0, sizeof(UINT8), (void *) 0x8000, 0xff, 0x01},



	{BLACK_WIDOW_1PSTART,	"1 Player start", "start1", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x8800, 0xdf, 0x0, sizeof(UINT8), (void *) 0x8800, 0xff, 0x20}, 



	{BLACK_WIDOW_2PSTART,	"2 Player start", "start2", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x8800, 0xbf, 0x0, sizeof(UINT8), (void *) 0x8800, 0xff, 0x40},



	{BLACK_WIDOW_COIN,	"Coin up",	"coin",	BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x7800, 0xfe, 0x0, sizeof(UINT8), (void *) 0x7800, 0xff, 0x01},



	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



struct sSoundDeviceEntry sBlackWidowSound[] =

{

	{DEV_POKEY_CLUSTER, 0x80, (void *) 2, (void *) 1373300, (void *) 0},		// Two pokeys for this game - CPU # 0



	// List terminator



	{INVALID}

};



/************************************************************************

 *					

 * Name : BlackWidowVectorGenerator

 *					

 * Entry: Address/value

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called when BlackWidow's vector generator is pulsed

 * 				

 ************************************************************************/



void BlackWidowVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	AnalogVectorGenerator((UINT16 *) (0x2000 + psCurrentCpu->bMemBase), FALSE, FALSE);

}



UINT8 BlackWidowHandler(UINT32 dwAddr, struct MemoryReadByte *pMemRead)

{

	UINT8 bVal;



	bVal = psCurrentCpu->bMemBase[0x7800] & 0x3f;

	if (((psCurrentCpu->dwCycleCount + psCurrentCpu->psCpu->cpuGetElapsedTicks(FALSE)) % 500) < 250)

		bVal |= 0x80;		// 3KHZ 

	if (VectorHalt(0))

		bVal |= 0x40;		// VG Is busy! 

	return(bVal);

}



/***************************************************************************

										  Tempest

 ***************************************************************************/



void TempestVectorGeneratorHandler(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void TempestTimerToggle();



// Default game configuration for games that are supported on this platform

	

struct sRomDef sTempestRoms[] =

{

	// Vector ROMs



	{"136002.123",	0,	0x3000},

	{"136002.124",	0,	0x3800},



	// Main program ROMs



	{"136002.113", 0, 0x9000},

	{"136002.114",	0,	0x9800},

	{"136002.115",	0,	0xa000},

	{"136002.116", 0, 0xa800},

	{"136002.117",	0, 0xb000},

	{"136002.118", 0,	0xb800},

	{"136002.119", 0, 0xc000},

	{"136002.120", 0, 0xc800},

	{"136002.121", 0, 0xd000},

	{"136002.122", 0, 0xd800},

	{"136002.122", 0, 0xf800},



	// List terminator



	{NULL,	0,	0}

};



struct sRomDef sTempestTubes[] =

{

	// Vector ROMs



	{"136002.123",	0,	0x3000},

	{"136002.124",	0,	0x3800},



	// Main program ROMs



	{"136002.113", 0, 0x9000},

	{"136002.114",	0,	0x9800},

	{"136002.115",	0,	0xa000},

	{"136002.316", 0, 0xa800},

	{"136002.217",	0, 0xb000},

	{"tube.118", 0,	0xb800},

	{"136002.119", 0, 0xc000},

	{"136002.120", 0, 0xc800},

	{"136002.121", 0, 0xd000},

	{"136002.122", 0, 0xd800},

	{"136002.122", 0, 0xf800},



	// List terminator



	{NULL,	0,	0}

};



// Fill regions for Tempest



struct sMemoryInit TempestFill[] =

{

	{0x6040,	0x6040,	0x00},

	{0x0c00,	0x0c00,	0xff},

	{0x0d00,	0x0d00,	0x02},

	{0x0e00,	0x0e00,	0x10},

	{0x60c8,	0x60c8,	0xff},



	// Our terminator



	{(UINT32) INVALID, (UINT32) INVALID, 0}

};



// Tempest read/write structures



struct MemoryWriteByte TempestWrite[] =

{

	{0x4800, 0x4800,	TempestVectorGeneratorHandler},

	{0x0c00,	0x0fff,	NothingWrite},

	{0x6080,	0x609f,	MathboxGo},

	{0x3000, 0x3fff,	NothingWrite},

	{0x60c8, 0x60c8,	NothingWrite},

	{0x60c0, 0x60cf,	PokeyWrite, bPokey1},

	{0x60d0, 0x60df,	PokeyWrite, bPokey2},

	{0x0800,	0x080f,	TempestColorWrite},

	{0x011f,	0x011f,	NothingWrite},

	{-1,		-1,		NULL}

};



struct MemoryReadByte TempestRead[] =

{

	{0x0c00,	0x0c00,	TempestToggle},

	{0x6060,	0x6060,	MathboxLow},

	{0x6070,	0x6070,	MathboxHigh},

	{0x60c8, 0x60c8,	SpinnerHandler},

	{0x60c0, 0x60cf,	PokeyRead, bPokey1},

	{0x60d0, 0x60df,	PokeyRead, bPokey2},

	{-1,		-1,		NULL}

};



INT8 kbHdlr = 0;	// Here to simulate a spinner

UINT8 bTempestVal = 0;		// Value of our current spinner

static UINT8 bSpinValue = 0;



struct sCpu TempestProcList[] =

{

	{CPU_6502ZP, RUNNING, 6200, 1912000,  NULL, &TempestFill[0], TempestWrite, TempestRead,

	 NULL, NULL, 0, 6200, 0, NULL, 0, NULL},

	 

	PROC_TERMINATOR

};



struct sSoundDeviceEntry sTempestSound[] =

{

	{DEV_POKEY_CLUSTER, 0x80, (void *) 2, (void *) 1373300, 0},		// Two pokeys for this game - CPU # 0 drives it



	// List terminator



	{INVALID}

};



// Game controllers in Tempest



struct sGameFunction sTempestFunctions[] =

{

	{TEMPEST_1PSTART,	"1 Player start", "start1", BINARY, ACTUAL, 0,

		sizeof(UINT8), (void *) (bPokey2 + 0x08), 0xdf, 0x20, sizeof(UINT8), (void *) (bPokey2 + 0x08), 0xdf, 0x00},



	{TEMPEST_2PSTART,	"2 Player start", "start2", BINARY, ACTUAL, 0,

		sizeof(UINT8), (void *) (bPokey2 + 0x08), 0xbf, 0x40, sizeof(UINT8), (void *) (bPokey2 + 0x08), 0xbf, 0x00},



	{TEMPEST_SUPERZAP,"Superzap", "superzap", BINARY, ACTUAL, 0,

		sizeof(UINT8), (void *) (bPokey2 + 0x08), 0xf7, 0x08, sizeof(UINT8), (void *) (bPokey2 + 0x08), 0xf7, 0x00},

		

	{TEMPEST_FIRE,		"Fire", "fire", BINARY, ACTUAL, 0,

		sizeof(UINT8), (void *) (bPokey2 + 0x08), 0xef, 0x10, sizeof(UINT8), (void *) (bPokey2 + 0x08), 0xef, 0x00},



	{TEMPEST_LEFT,		"Left", "left", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x6101, 0xff, 0x80, sizeof(UINT8), (void *) 0x6101, 0x7f, 0x00},



	{TEMPEST_RIGHT,	"Right", "right", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x6101, 0xff, 0x40, sizeof(UINT8), (void *) 0x6101, 0xbf, 0x00},



	{TEMPEST_SPINNER,	"Spinner",	"spinner", BALLISTICS, ACTUAL, 0,

		sizeof(UINT8), (void *) &bTempestVal, (UINT32) -7, (UINT32) 7},



	{TEMPEST_SELFTEST,	"Self test", "selftest", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x0c00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00},



	{TEMPEST_DIAGSTEP,	"Diag step", "diagstep", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x0c00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00},

	

	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



UINT32 TempestSetup(struct sRegistryBlock **ppsReg)

{

	if (ppsReg)

	{

		LOAD_DATA("kbHdlr", &kbHdlr, sizeof(UINT8))

		LOAD_DATA("bTempestVal", &bTempestVal, sizeof(UINT8))

		LOAD_DATA("bSpinValue", &bSpinValue, sizeof(UINT8))



		MathboxLoadState(ppsReg);

		return(RETRO_SETUPOK);

	}

	else

	{

		return(RETRO_SETUPOK);

	}

}



UINT32 TempestShutdown(struct sRegistryBlock **ppsReg)

{

	if (ppsReg)

	{

		SAVE_DATA("kbHdlr", &kbHdlr, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bTempestVal", &bTempestVal, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bSpinValue", &bSpinValue, sizeof(UINT8), REG_UINT8)

		MathboxSaveState(ppsReg);

		return(RETRO_SETUPOK);

	}

	else

	{

		return(RETRO_SETUPOK);

	}

}



/************************************************************************

 *					

 * Name : TempestToggle

 *				

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine will toggle the AVG busy bit so we don't get duplicated

 * vectors.

 * 				

 ************************************************************************/



UINT8 TempestToggle(UINT32 dwAddr, struct MemoryReadByte *pMemRead)

{

	UINT8 bVal;



	bVal = psCurrentCpu->bMemBase[0xc00] & 0x3f;

	if (((psCurrentCpu->dwCycleCount + psCurrentCpu->psCpu->cpuGetElapsedTicks(FALSE)) % 500) < 250)

		bVal |= 0x80;		// 3KHZ

	if (VectorHalt(0))

		bVal |= 0x40;		// VG Is busy!

	return(bVal);

}





/************************************************************************

 *					

 * Name : TempestColorWrite

 *					

 * Entry: Address & data to change in the color palette

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called when Tempest wants to do a color write

 * 				

 ************************************************************************/



void TempestColorWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite)

{

	data = (data & 0xF);

	address &= 0x0f;

	bColorPalette[address] = 0x0f - data;

}



/************************************************************************

 *					

 * Name : TempestVectorGenerator

 *					

 * Entry: Address/value

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called when Tempest's vector generator is pulsed

 * 				

 ************************************************************************/



void TempestVectorGeneratorHandler(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	TempestVectorGenerator((UINT16 *) (0x2000 + psCurrentCpu->bMemBase));

}



UINT8 SpinnerHandler(UINT32 addr, struct MemoryReadByte *pMemRead)

{

	if (psCurrentCpu->bMemBase[0x6101] & 0x80)

	{

		if (kbHdlr > -2)

			kbHdlr--;

	}

	else	

	if (psCurrentCpu->bMemBase[0x6101] & 0x40)

	{

		if (kbHdlr < 2)

			kbHdlr++;

	}

	else

	{

		if (kbHdlr < 0)

			kbHdlr++;

		if (kbHdlr > 0)

			kbHdlr--;

	}



	bSpinValue += kbHdlr;

	bSpinValue += bTempestVal;



	return(bSpinValue);

}



/***************************************************************************

										Battlezone

 ***************************************************************************/



void BattlezoneSound(UINT32 addr, UINT8 swapval, struct MemoryWriteByte *pMemWrite);

void BattlezoneSoundEnable(UINT8 bState);

void BattlezonePokeyWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite);

void BattlezoneVectorGeneratorWrite(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void BattlezoneTimerToggle();



static	UINT8 bPokeySoundEnable = TRUE;

static	UINT8 bSoundEnable = TRUE;

static	UINT8 bEngineLow = FALSE;

static	UINT8 bEngineSound = FALSE;

static	UINT8 bExplosion = FALSE;

static	UINT8 bFire = FALSE;

static	UINT8 bDive = FALSE;

static	UINT8 bPlayerHit = FALSE;

static	UINT8 bFireLow = FALSE;

static	UINT8 bFireHigh = FALSE;



// Default game configuration for games that are supported on this platform

	

struct sRomDef sBattlezoneRoms[] =

{

	// Vector ROMs

	

	{"036422.01",	0,	0x3000},

	{"036421.01",	0,	0x3800},



	// Game ROMs



	{"036414.01",	0,	0x5000},

	{"036413.01",	0,	0x5800},

	{"036412.01",	0,	0x6000},

	{"036411.01",	0,	0x6800},

	{"036410.01",	0,	0x7000},

	{"036409.01",	0,	0x7800},

	{"036409.01",	0,	0xf800},



	// List terminator



	{NULL,	0,	0}

};



// Fill regions for Battlezone



struct sMemoryInit BattlezoneFill[] =

{

	{0x1800,	0x1800,	0x00},

	{0x1802,	0x1802,	0x7f},

	{0x0800,	0x0800,	0xff},

	{0x0a00,	0x0a00,	0x02},

	{0x0c00,	0x0c00,	0x10},



	// Our terminator



	{(UINT32) INVALID, (UINT32) INVALID, 0}

};



// Battlezone read/write structures



struct MemoryWriteByte BattlezoneWrite[] =

{

	{0x1200, 0x1200,	BattlezoneVectorGeneratorWrite},

	{0x1820, 0x182f,	BattlezonePokeyWrite, bPokey1},

	{0x1828,	0x1828,	NothingWrite},		/* Don't write to the pokey! */

	{0x1860, 0x187f,	MathboxGo},

	{0x800,	0x0fff,		NothingWrite},

	{0x1840, 0x1840,	BattlezoneSound},

	{-1,		-1,		NULL}

};



struct MemoryReadByte BattlezoneRead[] =

{

	{0x1810,	0x1810,	MathboxLow},

	{0x1818,	0x1818,	MathboxHigh},

	{0x1820, 0x182f,	PokeyRead, bPokey1},

	{-1,		-1,		NULL}

};



struct sCpu BattlezoneProcList[] =

{

	{CPU_6502ZP, RUNNING, 6250, 2000000,  NULL, &BattlezoneFill[0], BattlezoneWrite, BattlezoneRead,

	 NULL, NULL, 6250, 0, 0, 0, 0, NULL},



	PROC_TERMINATOR

};



// Game controllers in Battlezone



struct sGameFunction sBattlezoneFunctions[] =

{

	{BATTLEZONE_LEFTFW,	"Left forward",	"leftforward",	BINARY,	ACTUAL,	0, 

		sizeof(UINT8),	(void *) (bPokey1 + 0x08), 0xff, 0x08, sizeof(UINT8), (void *) (bPokey1 + 0x08), 0xf7, 0x00},



	{BATTLEZONE_LEFTRV,	"Left reverse",	"leftreverse",	BINARY,	ACTUAL,	0, 

		sizeof(UINT8),	(void *) (bPokey1 + 0x08), 0xff, 0x04, sizeof(UINT8), (void *) (bPokey1 + 0x08), 0xfb, 0x00},



	{BATTLEZONE_RIGHTFW,	"Right forward",	"rightforward",	BINARY,	ACTUAL,	0, 

		sizeof(UINT8),	(void *) (bPokey1 + 0x08), 0xff, 0x02, sizeof(UINT8), (void *) (bPokey1 + 0x08), 0xfd, 0x00},



	{BATTLEZONE_RIGHTRV,	"Right reverse",	"rightreverse",	BINARY,	ACTUAL,	0, 

		sizeof(UINT8),	(void *) (bPokey1 + 0x08), 0xff, 0x01, sizeof(UINT8), (void *) (bPokey1 + 0x08), 0xfe, 0x00},



	{BATTLEZONE_FIRE,	"Fire",	"fire",	BINARY, ACTUAL, 0,

		sizeof(UINT8),	(void *) (bPokey1 + 0x08), 0xff, 0x10, sizeof(UINT8), (void *) (bPokey1 + 0x08), 0xef, 0x00},



	{BATTLEZONE_START,	"Start",	"start1",	BINARY, ACTUAL, 0,

		sizeof(UINT8),	(void *) (bPokey1 + 0x08), 0xff, 0x20, sizeof(UINT8), (void *) (bPokey1 + 0x08), 0xdf, 0x00},



	{BATTLEZONE_SELFTEST, "Self test", "selftest", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x0800, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00},



	{BATTLEZONE_DIAGSTEP, "Diag step", "diagstep", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x0800, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00},



	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



struct sWaveName sBattlezoneEngine[] =

{

	{"Engine Low"},

	{"Engine High"},

	{NULL},

};



struct sWaveName sBattlezoneExplosionLow[] =

{

	{"Explosion Low"},

	{NULL},

};



struct sWaveName sBattlezoneExplosionHigh[] =

{

	{"Explosion High"},

	{NULL},

};



struct sWaveName sBattlezoneFireLow[] =

{

	{"Fire Low"},

	{NULL},

};



struct sWaveName sBattlezoneFireHigh[] =

{

	{"Fire High"},

	{NULL},

};



struct sSoundDeviceEntry sBattlezoneSound[] =

{

	{DEV_POKEY_CLUSTER, 0x80, (void *) 1, (void *) 1373300, (void *) 0},		// One pokey for this game - CPU # 0 drives it

	{DEV_WAV,			0x80, (void *) sBattlezoneEngine},

	{DEV_WAV,			0x80, (void *) sBattlezoneExplosionLow},

	{DEV_WAV,			0x80, (void *) sBattlezoneExplosionHigh},

	{DEV_WAV,			0x80, (void *) sBattlezoneFireLow},

	{DEV_WAV,			0x80, (void *) sBattlezoneFireHigh},



	// List terminator



	{INVALID}

};





/************************************************************************

 *					

 * Name : BattlezonePokeyWrite

 *					

 * Entry: Address/value

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * battlezone wrapper for the pokey write routine so we can

 * disable sound output correctly as needed (during attract mode)

 * 				

 ************************************************************************/

void BattlezonePokeyWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite)

{

	// sound output enable bit

	if ( *(0x1840 + psCurrentCpu->bMemBase) & 0x20){

		if (!bPokeySoundEnable){

			BattlezoneSoundEnable(TRUE);

			bPokeySoundEnable = TRUE;

		}

	} else {

		if (bPokeySoundEnable){

			// sound is disabled

			bPokeySoundEnable = FALSE;

			BattlezoneSoundEnable(FALSE);

		}

	}



	PokeyWrite(address, data, pMemWrite);

}





/************************************************************************

 *					

 * Name : BattlezoneVectorGeneratorWrite

 *					

 * Entry: Address/value

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called when Battlezone's vector generator is pulsed

 * 				

 ************************************************************************/



void BattlezoneVectorGeneratorWrite(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	if (psActiveGame->psBestMode->bColorDepth > 8 && pGameBackdropImage)

	{

		// clip vectors against the backdrop region

		// so that they only display in the "black" area of the

		// backdrop (the real game used a cardboard frame over

		// the monitor screen).

		BattlezoneVectorGenerator((UINT16 *) (0x2000 + psCurrentCpu->bMemBase), 188, FALSE);

	} else {

		// no backdrop so don't clip for it.

		AnalogVectorGenerator((UINT16 *) (0x2000 + psCurrentCpu->bMemBase), 150, FALSE);

	}

}



void BattlezoneSound(UINT32 addr, UINT8 bData, struct MemoryWriteByte *pMemWrite)

{



	// start leds...maybe one day we can use this :-)

/*	if (data & 0x40){

		start leds on...

	} else {

		start leds off...

	}

*/



	// sound output enable bit

	if ( bData & 0x20){

		if (!bSoundEnable){

			BattlezoneSoundEnable(TRUE);

			bSoundEnable = TRUE;

		}

	} else {

		if (bSoundEnable){

			// sound is disabled

			bSoundEnable = FALSE;

			BattlezoneSoundEnable(FALSE);

		}

		return;

	}



	// engine sounds

	if (bData & 0x80){

		bEngineSound = TRUE;

		if (bData & 0x10) {

			// fast engine sound

			if (bEngineLow){

				WavePlayback(0, 1);

				bEngineLow = FALSE;

			}

		} else {

			// engine idle sound

			if (!bEngineLow){

				WavePlayback(0, 0);

				bEngineLow = TRUE;

			}

		}

	} else {

		if (bEngineSound){

			StopWavePlayback(0);

			bEngineSound = FALSE;

		}

	}



	// explosion sounds

	if (bData & 0x01) {

		if (bData & 0x02) {

			// loud explosion

			if (!bPlayerHit){

				bPlayerHit = TRUE;

				WavePlayback(2, 0);

			}

		} else {

			// soft explosion

			if (!bExplosion){

				bExplosion = TRUE;

				WavePlayback(1, 0);

			}

		}

	} else {

		bExplosion = FALSE;

		bPlayerHit = FALSE;

	}



	// fire sounds

	if (bData & 0x04){

		if (bData & 0x08) {

			if (!bFireHigh){

				// loud fire sound

				WavePlayback(4, 0);

				bFireHigh = TRUE;

			}

		} else {

			if (!bFireLow){

				// soft fire sound

				WavePlayback(3, 0);

				bFireLow = TRUE;

			}

		}

	} else {

		bFireLow = FALSE;

		bFireHigh = FALSE;

	}





}





/************************************************************************

 *					

 * Name : BattlezoneSoundEnable()

 *					

 * Entry: TRUE/FALSE

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * Enables/Disables Battlezone sound output

 * 				

 ************************************************************************/



void BattlezoneSoundEnable(UINT8 bState)

{

	if (bState){

		// turn sound ON

		SetChannelVolume(0, sBattlezoneSound[0].bVolume); // pokey

		SetChannelVolume(1, sBattlezoneSound[1].bVolume); // engine

		SetChannelVolume(2, sBattlezoneSound[2].bVolume); // explosion (low)

		SetChannelVolume(3, sBattlezoneSound[3].bVolume); // explosion (high)

		SetChannelVolume(4, sBattlezoneSound[4].bVolume); // fire (low)

		SetChannelVolume(5, sBattlezoneSound[5].bVolume); // fire (high)

	} else {

		// turn sound OFF

		SetChannelVolume(0, 0); // pokey

		SetChannelVolume(1, 0); // engine

		SetChannelVolume(2, 0); // explosion (low)

		SetChannelVolume(3, 0); // explosion (high)

		SetChannelVolume(4, 0); // fire (low)

		SetChannelVolume(5, 0); // fire (high)

	}

}





/************************************************************************

 *					

 * Name : BattlezoneSetup()

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *

 * This routine is called just before Battlezone starts executing

 *

 ************************************************************************/



UINT32 BattlezoneSetup(struct sRegistryBlock **ppsReg)

{	

	if (ppsReg)

	{

		// Do any necessary loading from saved file registry here



		LOAD_DATA("bSoundEnable", &bSoundEnable, sizeof(UINT8))

		LOAD_DATA("bPokeySoundEnable", &bPokeySoundEnable, sizeof(UINT8))

		LOAD_DATA("bEngineLow", &bEngineLow, sizeof(UINT8))

		LOAD_DATA("bEngineSound", &bEngineSound, sizeof(UINT8))

		LOAD_DATA("bExplosion", &bExplosion, sizeof(UINT8))

		LOAD_DATA("bPlayerHit", &bPlayerHit, sizeof(UINT8))

		LOAD_DATA("bFireLow", &bFireLow, sizeof(UINT8))

		LOAD_DATA("bFireHigh", &bFireHigh, sizeof(UINT8))



		MathboxLoadState(ppsReg);

		return(RETRO_SETUPOK);

	}

	else

	{

		bColorPalette[0] = 2;			// Normal stuff

		bColorPalette[3] = 2;			// Clipped stuff

		bColorPalette[4] = 4;			// Red area for status

		return(RETRO_SETUPOK);

	}

}



/************************************************************************

 *					

 * Name : BattlezoneShutdown()

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called just before Battlezone shuts down

 * 				

 ************************************************************************/



UINT32 BattlezoneShutdown(struct sRegistryBlock **ppsReg)

{	

	if (ppsReg)

	{

		SAVE_DATA("bPokeySoundEnable", &bPokeySoundEnable, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bSoundEnable", &bSoundEnable, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bEngineLow", &bEngineLow, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bEngineSound", &bEngineSound, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bExplosion", &bExplosion, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bPlayerHit", &bPlayerHit, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bFireLow", &bFireLow, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bFireHigh", &bFireHigh, sizeof(UINT8), REG_UINT8)



		MathboxSaveState(ppsReg);



		// Do any necessary loading from saved file registry here



		return(RETRO_SETUPOK);

	}

	else

	{

		return(RETRO_SETUPOK);

	}

}



/***************************************************************************

										Red Baron

 ***************************************************************************/



void RedBaronPokeyWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite);

void RedBaronSound(UINT32 addr, UINT8 swapval, struct MemoryWriteByte *pMemWrite);

void RedBaronSoundEnable(UINT8 bState);

void RedBaronVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite);

void RedBaronTimerToggle();



// Default game configuration for games that are supported on this platform

	

struct sRomDef sRedBaronRoms[] =

{

	// Vector ROMs

	

	{"037006.01E",	0,	0x3000},

	{"037007.01E",	0,	0x3800},



	// Game ROMs



	{"037587.01",	0,	0x4800},

	{"037587.01",	0,	0x5000},

	{"037000.01E",	0,	0x5000},

	{"036998.01E",	0,	0x6000},

	{"036997.01E",	0,	0x6800},

	{"036996.01E",	0,	0x7000},

	{"036995.01E",	0,	0x7800},

	{"036995.01E",	0,	0xf800},





	// List terminator



	{NULL,	0,	0}

};



// Fill regions for RedBaron



struct sMemoryInit RedBaronFill[] =

{

	{0x1800,	0x1800,	0x00},

	{0x1802,	0x1802,	0x7f},

	{0x0800,	0x0800,	0xff},

	{0x0a00,	0x0a00,	0x02},

	{0x0c00,	0x0c00,	0x10},

	{0x9000,	0x9001,	0x80},



	// Our terminator



	{(UINT32) INVALID, (UINT32) INVALID, 0}

};



// RedBaron read/write structures



struct MemoryWriteByte RedBaronWrite[] =

{

	{0x1200, 0x1200,	RedBaronVectorGenerator},

	{0x1860, 0x187f,	MathboxGo},

	{0x800,	0x0fff,		NothingWrite},

	{0x1800, 0x1807,	NothingWrite},

	{0x1810, 0x181f,	RedBaronPokeyWrite, bPokey1},

	{0x1808, 0x1808,	RedBaronSound},

	{-1,		-1,		NULL}

};



struct MemoryReadByte RedBaronRead[] =

{

	{0x1804,	0x1804,	MathboxLow},

	{0x1806,	0x1806,	MathboxHigh},

	{0x1818, 0x1818,	BaronJoystickRd},

	{0x1810, 0x181f,	PokeyRead, bPokey1},

	{-1,		-1,		NULL}

};



struct sCpu RedBaronProcList[] =

{

	{CPU_6502ZP, RUNNING, 6150, 1500000,  NULL, &RedBaronFill[0], RedBaronWrite, RedBaronRead,

	 NULL, NULL, 6150, 0, 50000, BaronKeyboardJoystickHandler, 0, NULL},



	PROC_TERMINATOR

};



// Game controllers in Red Baron



struct sGameFunction sRedBaronFunctions[] =

{

	{RED_BARON_HORIZ,	NULL, "horizontal", ANALOG, ACTUAL, 0,

		sizeof(UINT16), (void *) &wBaronX, 0x60, 0xa0, 0x0, 0x0000, 0x0, 0x0},



	{RED_BARON_VERT,	NULL, "vertical", ANALOG, ACTUAL, 0,

		sizeof(UINT16), (void *) &wBaronY, 0x60, 0xa0, 0x0, 0x0000, 0x0, 0x0},



	{RED_BARON_LEFT,	"Left",	"left",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x9002, 0x00, 0x80, sizeof(UINT8), (void *) 0x9002, 0x7f, 0x00},



	{RED_BARON_RIGHT,	"Right",	"right",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x9002, 0x00, 0x40, sizeof(UINT8), (void *) 0x9002, 0xbf, 0x00},



	{RED_BARON_UP,		"Up",	"up",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x9002, 0x00, 0x20, sizeof(UINT8), (void *) 0x9002, 0xdf, 0x00},



	{RED_BARON_DOWN,	"Down",	"down",	BINARY,	VIRTUAL,	0, 

		sizeof(UINT8), (void *) 0x9002, 0x00, 0x10, sizeof(UINT8), (void *) 0x9002, 0xef, 0x00},



	{RED_BARON_FIRE,	"Fire",	"fire",	BINARY,	VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x1802, 0xff, 0x80, sizeof(UINT8), (void *) 0x1802, 0x7f, 0x00},



	{RED_BARON_START,	"Start game", "start1", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x1802, 0xbf, 0x00, sizeof(UINT8), (void *) 0x1802, 0xff, 0x40},



	{RED_BARON_COIN,	"Coin",	"coin", BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x0800, 0xfe, 0x00, sizeof(UINT8), (void *) 0x0800, 0xff, 0x01},



	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



struct sWaveName sRedBaronFire[] =

{

	{"Fire"},

	{NULL},

};



struct sWaveName sRedBaronExplosion[] =

{

	{"Explosion"},

	{NULL},

};



struct sWaveName sRedBaronDive[] =

{

	{"Dive"},

	{NULL},

};



struct sSoundDeviceEntry sRedBaronSound[] =

{

	{DEV_POKEY_CLUSTER, 0x80, (void *) 1, (void *) 1373300, (void *) 0},		// One pokey for this game - CPU # 0 drives it

	{DEV_WAV, 0x80, (void *) sRedBaronFire},

	{DEV_WAV, 0x80, (void *) sRedBaronExplosion},

	{DEV_WAV, 0x40, (void *) sRedBaronDive},



	// List terminator



	{INVALID}

};



struct sDipSwitch sRedBaronDipSwitches[] = {

	{0xff, VIRTUAL, 0, (void *) 0x0800},

	{0xfd, VIRTUAL, 0, (void *) 0x0a00},

	{0xe7, VIRTUAL, 0, (void *) 0x0c00},



	DIP_TERMINATOR

};



struct sDipEnum sRedBaronLanguageChoices[4] = {

	{"English", 3}, {"Spanish", 2}, {"French", 1}, {"German", 0}

};



struct sDipEnum sRedBaronBonusChoices[4] = {

	{"2k, 10k, 30k", 3}, {"4k 15k 40k", 2}, {"6k, 20k, 50k", 1}, {"None", 0}

};



struct sDipEnum sRedBaronLifeChoices[4] = {

	{"2", 3}, {"3", 2}, {"4", 1}, {"5", 0}

};



struct sDipEnum sRedBaronMinimumPlays[2] = {

	{"One Play", 1}, {"Two Plays", 0}

};



struct sDipField sRedBaronDipFields[] = {

	// Dip switches at 0x0800

	{

		"Service Mode",

		"Puts the game in service mode.",

		0, 4, 0x10,

		DIP_ENUM(StdOnOffChoices)

	},

	// Dip switches at 0x0c00

	{

		"Language",

		"Selects the language the game will display.",

		2, 0, 0x03,

		DIP_ENUM(sRedBaronLanguageChoices)

	},

	{

		"Bonus Planes",

		"Sets the number of points required for a bonus plane.",

		2, 2, 0x0c,

		DIP_ENUM(sRedBaronBonusChoices)

	},

	{

		"Lives/Game",

		"Sets the number of player lives per game.",

		2, 4, 0x30,

		DIP_ENUM(sRedBaronLifeChoices)

	},

	{

		"Minimum Plays",

		"Sets the minimum number of credits required to play.",

		2, 6, 0x40,

		DIP_ENUM(sRedBaronMinimumPlays)

	},

	{

		"Difficulty Adjust",

		"Sets whether the game should auto-adjust the game's difficulty.",

		2, 7, 0x80, 

		DIP_ENUM(StdOnOffChoices)

	},

	{ NULL }

};



/************************************************************************

 *					

 * Name : RedBaronVectorGenerator

 *					

 * Entry: Address/value

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called when RedBaron's vector generator is pulsed

 * 				

 ************************************************************************/



void RedBaronVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	AnalogVectorGenerator((UINT16 *) (0x2000 + psCurrentCpu->bMemBase), FALSE, FALSE);

}





/************************************************************************

 *					

 * Name : RedBaronPokeyWrite

 *					

 * Entry: Address/value

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * Red Baron wrapper for the pokey write routine so we can

 * disable sound output correctly as needed (during attract mode)

 * 				

 ************************************************************************/

void RedBaronPokeyWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite)

{

	// sound output enable bit

	if ( *(0x1808 + psCurrentCpu->bMemBase) & 0x08){

		if (bSoundEnable){

			// sound is disabled

			bSoundEnable = FALSE;

			RedBaronSoundEnable(FALSE);

		}

	} else {

		if (!bSoundEnable){

			RedBaronSoundEnable(TRUE);

			bSoundEnable = TRUE;

		}

	}



	PokeyWrite(address, data, pMemWrite);

}





void RedBaronSound(UINT32 addr, UINT8 bData, struct MemoryWriteByte *pMemWrite)

{

	psCurrentCpu->bMemBase[addr] = bData;



	// sound output enable bit

	if (bData & 0x08){

		if (bSoundEnable){

			// sound is disabled

			bSoundEnable = FALSE;

			RedBaronSoundEnable(FALSE);

		}

		return;

	} else {

		if (!bSoundEnable){

			RedBaronSoundEnable(TRUE);

			bSoundEnable = TRUE;

		}

	}



	// fire sounds

	if (bData & 0x04) {

		bFire = TRUE;

		WavePlayback(0, 0);

	} else {

		bFire = FALSE;

	}



	// explosion sounds

	if (bData & 0xf0) {

		if (!bExplosion){

			WavePlayback(1, 0);

			bExplosion = TRUE;

		}

	} else {

		bExplosion = FALSE;

	}



	// dive sound

	if (bData & 0x02) {

		if (!bDive){

			WavePlayback(2, 0);

			bDive = TRUE;

		}

	} else {

		bDive = FALSE;

	}





}



/************************************************************************

 *					

 * Name : RedBaronSoundEnable()

 *					

 * Entry: TRUE/FALSE

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * Enables/Disables Red Baron sound output

 * 				

 ************************************************************************/



void RedBaronSoundEnable(UINT8 bState)

{

	if (bState){

		// turn sound ON

		SetChannelVolume(0, sRedBaronSound[0].bVolume); // pokey

		SetChannelVolume(1, sRedBaronSound[1].bVolume); // fire

		SetChannelVolume(2, sRedBaronSound[2].bVolume); // explosion

		SetChannelVolume(3, sRedBaronSound[3].bVolume); // dive

	} else {

		// turn sound OFF

		SetChannelVolume(0, 0); // pokey

		SetChannelVolume(1, 0); // fire

		SetChannelVolume(2, 0); // explosion

		SetChannelVolume(3, 0); // dive

	}

}





/************************************************************************

 *					

 * Name : RedBaronSetup()

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called just before RedBaron starts executing

 * 				

 ************************************************************************/



UINT32 RedBaronSetup(struct sRegistryBlock **ppsReg)

{	

	if (ppsReg)

	{

		// Do any necessary loading from saved file registry here



		LOAD_DATA("bSoundEnable", &bSoundEnable, sizeof(UINT8))

		LOAD_DATA("bExplosion", &bExplosion, sizeof(UINT8))

		LOAD_DATA("bFire", &bFire, sizeof(UINT8))

		LOAD_DATA("bDive", &bDive, sizeof(UINT8))

		LOAD_DATA("wBaronX", &wBaronX, sizeof(UINT16))

		LOAD_DATA("wBaronY", &wBaronY, sizeof(UINT16))



		MathboxLoadState(ppsReg);

		return(RETRO_SETUPOK);

	}

	else

	{

		bColorPalette[0] = 3;			// Normal stuff

		bColorPalette[1] = 3;			// Normal stuff

		wBaronX = 0x80;					// Center them joysticks!

		wBaronY = 0x80;



		return(RETRO_SETUPOK);

	}

}



/************************************************************************

 *					

 * Name : BaronJoystickRd

 *					

 * Entry: Address of read

 *					

 * Exit : Returns either X or Y coordinate of Red Baron's joystick position

 *					

 * Description:

 *					

 * This routine is called when Red Baron needs to read the joystick.

 * 				

 ************************************************************************/



UINT8 BaronJoystickRd(UINT32 addr, struct MemoryReadByte *pMemRead)



{

	if (psCurrentCpu->bMemBase[0x1808] & 1)

		return((UINT8) wBaronX);

	return((UINT8) wBaronY);

}



void BaronKeyboardJoystickHandler()

{

	// If we're centered, the keyboard no longer has control



	// Left?		



	if (psCurrentCpu->bMemBase[0x9002] & 0x80)

	{

		if (wBaronX > 0x60)		// Until we reach 60h

			wBaronX -= XSTEP;

	}



	// Right?		



	if (psCurrentCpu->bMemBase[0x9002] & 0x40)

	{

		if (wBaronX < 0x0a0) 	// Until we reach a0h

			wBaronX += XSTEP;

	}

	// Up?		



	if (psCurrentCpu->bMemBase[0x9002] & 0x20)

	{

		if (wBaronY > 0x60)		// Until we reach 60h

			wBaronY -= YSTEP;

	}



	// Down?



	if (psCurrentCpu->bMemBase[0x9002] & 0x10)

	{

		if (wBaronY < 0x0a0) 	// Until we reach a0h

			wBaronY += YSTEP;

	}

}



/************************************************************************

 *					

 * Name : RedBaronShutdown()

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called just before Battlezone shuts down

 * 				

 ************************************************************************/



UINT32 RedBaronShutdown(struct sRegistryBlock **ppsReg)

{	

	if (ppsReg)

	{

		SAVE_DATA("bSoundEnable", &bSoundEnable, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bExplosion", &bExplosion, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bFire", &bFire, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bDive", &bDive, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("wBaronX", &wBaronX, sizeof(UINT16), REG_UINT16)

		SAVE_DATA("wBaronY", &wBaronY, sizeof(UINT16), REG_UINT16)



		MathboxSaveState(ppsReg);



		// Do any necessary loading from saved file registry here



		return(RETRO_SETUPOK);

	}

	else

	{

		return(RETRO_SETUPOK);

	}

}







/***************************************************************************

										Major Havoc

 ***************************************************************************/



static UINT8 bMHCoinSwitch = 0;

static UINT8 bMHPlayer1 = 0;

static UINT8 bOldMHBankId = 0;			// Default bank is 0

static UINT8 *MHPlayer1 = NULL;		// Player data

static UINT8 *MHPlayer2 = NULL;		// Player data

static UINT8 *MHVectorArea = NULL;	// Vector generator area

static INT8 bDirValue = 0;



// Default game configuration for games that are supported on this platform



struct sRomDef sMajorHavocRoms[] =

{

	// All possible alpha processor banks



	{"136025.215",	0,	0x2000},

	{"136025.210", 0, 0x5000},

	{"136025.216",	0,	0x8000},

	{"136025.217",	0,	0xc000},



	{"136025.215",	1,	0x0000},

	{"136025.210", 1, 0x5000},

	{"136025.216",	1,	0x8000},

	{"136025.217",	1,	0xc000},	



	{"136025.318",	2,	0x2000},

	{"136025.210", 2, 0x5000},

	{"136025.216",	2,	0x8000},

	{"136025.217",	2,	0xc000},	



	{"136025.318",	3,	0x0000},

	{"136025.210", 3, 0x5000},

	{"136025.216",	3,	0x8000},

	{"136025.217",	3,	0xc000},	



	// Gamma CPU bank



	{"136025.108",	4,	0x8000},

	{"136025.108",	4,	0xc000},



	// ROMs we use but throw away later



	{"136025.106",	0xff},	

	{"136025.107", 0xff},	



	// List terminator



	{NULL,	0,	0}

};



struct sRomDef sMajorHavocRvRoms[] =

{

	// All possible alpha processor banks



	{"136025.915",	0,	0x2000},

	{"136025.210", 0, 0x5000},

	{"136025.916",	0,	0x8000},

	{"136025.917",	0,	0xc000},



	{"136025.915",	1,	0x0000},

	{"136025.210", 1, 0x5000},

	{"136025.916",	1,	0x8000},

	{"136025.917",	1,	0xc000},	



	{"136025.918",	2,	0x2000},

	{"136025.210", 2, 0x5000},

	{"136025.916",	2,	0x8000},

	{"136025.917",	2,	0xc000},	



	{"136025.918",	3,	0x0000},

	{"136025.210", 3, 0x5000},

	{"136025.916",	3,	0x8000},

	{"136025.917",	3,	0xc000},	



	// Gamma CPU bank



	{"136025.908",	4,	0x8000},

	{"136025.908",	4,	0xc000},



	// ROMs we use but throw away later



	{"136025.106",	0xff},	

	{"136025.907", 0xff},	



	// List terminator



	{NULL,	0,	0}

};



// Game controllers in MajorHavoc



struct sGameFunction sMajorHavocFunctions[] =

{

	{MAJOR_HAVOC_COINLEFT,	NULL, "coin1", BINARY, ACTUAL, 0,

		sizeof(UINT8), (void *) &bMHCoinSwitch, 0xbf, 0x00, sizeof(UINT8), (void *) &bMHCoinSwitch, 0xff, 0x40},



	{MAJOR_HAVOC_COINRIGHT,	NULL, "coin2", BINARY, ACTUAL, 0,

		sizeof(UINT8), (void *) &bMHCoinSwitch, 0x7f, 0x00, sizeof(UINT8), (void *) &bMHCoinSwitch, 0xff, 0x80},



	{MAJOR_HAVOC_COINAUX,	NULL, "coin3", BINARY, ACTUAL, 0,

		sizeof(UINT8), (void *) &bMHCoinSwitch, 0xdf, 0x00, sizeof(UINT8), (void *) &bMHCoinSwitch, 0xff, 0x20},



	{MAJOR_HAVOC_SELFTEST, NULL, "selftest", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x1200, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00},



	{MAJOR_HAVOC_DIAG_STEP, NULL, "diagstep", TOGGLE, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x1200, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00},



	{MAJOR_HAVOC_JUMP1, NULL, "p1jump",	BINARY, VIRTUAL, 4,

		sizeof(UINT8), (void *) 0x2800, 0x7f, 0x00, sizeof(UINT8), (void *) 0x2800, 0xff, 0x80},

		

	{MAJOR_HAVOC_SHIELD1, NULL, "p1shield",	BINARY, VIRTUAL, 4,

		sizeof(UINT8), (void *) 0x2800, 0xbf, 0x00, sizeof(UINT8), (void *) 0x2800, 0xff, 0x40},

	

	{MAJOR_HAVOC_JUMP2, NULL, "p2jump",	BINARY, VIRTUAL, 4,

		sizeof(UINT8), (void *) 0x2800, 0xdf, 0x00, sizeof(UINT8), (void *) 0x2800, 0xff, 0x20},

		

	{MAJOR_HAVOC_SHIELD2, NULL, "p2shield",	BINARY, VIRTUAL, 4,

		sizeof(UINT8), (void *) 0x2800, 0xef, 0x00, sizeof(UINT8), (void *) 0x2800, 0xff, 0x10},



	{MAJOR_HAVOC_ROLLER_RIGHT,	NULL,	"rollerright",	BINARY, VIRTUAL, 4,

		sizeof(UINT8), (void *) 0xe000, 0xff, 0x80, sizeof(UINT8), (void *) 0xe000, 0x7f, 0x00},



	{MAJOR_HAVOC_ROLLER_LEFT,	NULL,	"rollerleft",	BINARY, VIRTUAL, 4,

		sizeof(UINT8), (void *) 0xe000, 0xff, 0x40, sizeof(UINT8), (void *) 0xe000, 0xbf, 0x00}, 



	{MAJOR_HAVOC_HORIZ,	"Roller", "roller", BALLISTICS, ACTUAL, 0,

		sizeof(UINT8), (void *) &bJoystickX, 40, -40},



	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



struct sMemoryInit MajorHavocFillAlpha[] =

{

	{0x1200, 0x1200, 0xf0},

	// Our terminator



	{(UINT32) INVALID, (UINT32) INVALID, 0}

};



// MajorHavoc read/write structures



struct MemoryWriteByte MajorHavocWriteAlpha[] =

{

	{0x0200, 0x07ff, MHPlayerRamWrite},

	{0x1000, 0x1fff, MHAlphaBankWrite},

	{0x4000, 0x4fff, MHVectorWrite},

	{0x2000, 0xffff, NothingWrite},

	{-1,		-1,		NULL}

};



struct MemoryReadByte MajorHavocReadAlpha[] =

{

	{0x0200, 0x07ff, MHPlayerRamRead},

	{0x1000, 0x1fff, MHAlphaBankRead},

	{0x4000, 0x7fff, MHVectorRead},

	{-1,		-1,		NULL}

};



// High score saving area for Major Havoc



struct sHighScore sMajorHavocHighScores[] =

{ 

	{4,	0x6000,	0x200},	// CPU #4, Address 0x6000 for 0x200 bytes

	{(UINT32) INVALID}

};



// Fill regions for MajorHavoc



struct sMemoryInit MajorHavocFillGamma[] =

{

	// Our terminator



	{(UINT32) INVALID, (UINT32) INVALID, 0}

};



// MajorHavoc read/write structures



struct MemoryWriteByte MajorHavocWriteGamma[] =

{

	{0x2000,	0x201f,	PokeyClusterDataWrite},

	{0x2020,	0x203f,	PokeyClusterCtrlWrite},



	{0x2040, 0x4fff,	NothingWrite},

	{0x5000, 0x5000,	MHGammaWriteAlpha},

	{0x8000, 0xffff,	NothingWrite},

	{-1,		-1,		NULL}

};



struct MemoryReadByte MajorHavocReadGamma[] =

{

	{0x2020,	0x203f,	PokeyClusterCtrlRead},

	{0x3000, 0x3000,	MHAlphaCommRead},

	{-1,		-1,		NULL}

};



struct sCpu MajorHavocProcList[] =

{

	{CPU_6502ZP, RUNNING, 0x0000080, 2500000,  NULL, MajorHavocFillAlpha, MajorHavocWriteAlpha, MajorHavocReadAlpha, 

	 NULL, NULL, 0, 8000, 520, MH2400hz, 0, NULL},



	{CPU_6502ZP, BANKED, 0x0000080, 2500000,  NULL, MajorHavocFillAlpha, MajorHavocWriteAlpha, MajorHavocReadAlpha, 

	 NULL, NULL, 0, 8000, 520, MH2400hz, 0, NULL},



	{CPU_6502ZP, BANKED, 0x0000080, 2500000,  NULL, MajorHavocFillAlpha, MajorHavocWriteAlpha, MajorHavocReadAlpha, 

	 NULL, NULL, 0, 8000, 520, MH2400hz, 0, NULL},



	{CPU_6502ZP, BANKED, 0x0000080, 2500000,  NULL, MajorHavocFillAlpha, MajorHavocWriteAlpha, MajorHavocReadAlpha, 

	 NULL, NULL, 0, 8000, 520, MH2400hz, 0, NULL},



	{CPU_6502ZP, RUNNING, 0x0000040, 1250000,  NULL, MajorHavocFillGamma, MajorHavocWriteGamma, MajorHavocReadGamma, 

	 NULL, NULL, 0, 4000, 25000, MajorHavocControllerCheck, 0, NULL}, 



	PROC_TERMINATOR

};



// Audio information



struct sSoundDeviceEntry sMajorHavocSound[] =

{

	{DEV_POKEY_CLUSTER, 0x80, (void *) 4, (void *) 1373300, (void *) 4},		// Four pokeys for this game - CPU #4!



	// List terminator



	{INVALID}

};



/************************************************************************

 *					

 * Name : MajorHavocSetup

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine will handle all the ncessary initialization for Major Havoc

 * 				

 ************************************************************************/



UINT32 MajorHavocSetup(struct sRegistryBlock **ppsReg)

{

	if (ppsReg)

	{

		// Do any necessary loading from saved file registry here



		LOAD_DATA("MHPlayer1", MHPlayer1, sizeof(UINT8) * 0x600)

		LOAD_DATA("MHPlayer2", MHPlayer2, sizeof(UINT8) * 0x600)

		LOAD_DATA("bMHCoinSwitch", &bMHCoinSwitch, sizeof(UINT8))

		LOAD_DATA("bMHPlayer1", &bMHPlayer1, sizeof(UINT8))

		LOAD_DATA("bOldMHBankId", &bOldMHBankId, sizeof(UINT8))

		LOAD_DATA("bDirValue", &bDirValue, sizeof(UINT8))



		return(RETRO_SETUPOK);

	}

	else

	{

		bOldMHBankId = 0;			// We're banking on 0

		MHPlayer1 = MyMalloc(0x600, "MajorHavocSetup()");

		MHPlayer2 = MyMalloc(0x600, "MajorHavocSetup()");

		MHVectorArea = MyMalloc(40*1024, "MajorHavocSetup()");

		MemoryCopy(MHVectorArea + 0x1000, sActiveCpu[0].bMemBase + 0x5000, 0x1000);

		MemoryCopy(MHVectorArea + 0x2000, pbAltRomData, 0x4000);

		MemoryCopy(MHVectorArea + 0x6000, (UINT8 *) (pbAltRomData + 0x4000), 0x4000);

		AltRomEject();				// Get rid of any outstanding Alt ROMs!

		return(RETRO_SETUPOK);

	}

}



/************************************************************************

 *					

 * Name : MajorHavocShutdown

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine will handle all the ncessary initialization for Major Havoc

 * 				

 ************************************************************************/



UINT32 MajorHavocShutdown(struct sRegistryBlock **ppsReg)

{

	if (ppsReg)

	{

		// Put code here to write anything to the save file registry



		SAVE_DATA("MHPlayer1", MHPlayer1, sizeof(UINT8) * 0x600, REG_UINT8)

		SAVE_DATA("MHPlayer2", MHPlayer2, sizeof(UINT8) * 0x600, REG_UINT8)

		SAVE_DATA("bMHCoinSwitch", &bMHCoinSwitch, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bMHPlayer1", &bMHPlayer1, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bOldMHBankId", &bOldMHBankId, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bDirValue", &bDirValue, sizeof(UINT8), REG_UINT8)



		return(RETRO_SETUPOK);

	}

	else

	{

		bOldMHBankId = 0;			// We're banking on 0

		MyFree((void *) &MHPlayer1, "MajorHavocShutdown()");

		MyFree((void *) &MHPlayer2, "MajorHavocShutdown()");

		MyFree((void *) &MHVectorArea, "MajorHavocShutdown()");

		return(RETRO_SETUPOK);

	}

}



void MH2400hz()

{

	if (sActiveCpu[0].bMemBase[0x1200] & 0x02)
		sActiveCpu[0].bMemBase[0x1200] &= 0xfd;	// 2.4KHZ toggle
	else
	  sActiveCpu[0].bMemBase[0x1200] |= 0x02;	// 2.4KHZ toggle

}



static UINT8 bLastJoy = 0;



void MajorHavocControllerCheck()

{

	if (sActiveCpu[4].bMemBase[0xe000] & 0x80)	// Right?

	{

		if (bDirValue > 0)

			bDirValue = 0;

		if (bDirValue > -20)

		{

			--bDirValue;

			--bDirValue;

		}

	}

	else

	if (sActiveCpu[4].bMemBase[0xe000] & 0x40)	// Left?

	{

		if (bDirValue < 0)

			bDirValue = 0;

		if (bDirValue < 20)

		{

			++bDirValue;

			++bDirValue;

		}

	}

	else

	{

		if (bDirValue < 0)

		{

			++bDirValue;

			++bDirValue;

		}

		if (bDirValue > 0)

		{

			--bDirValue;

			--bDirValue;

		}

	}



	if (bJoystickX)

		bDirValue = bJoystickX;



	if (bLastJoy && 0 == bJoystickX)

		bDirValue = 0;



	bLastJoy = bJoystickX;



	sActiveCpu[4].bMemBase[0x3800] += bDirValue;

}



/************************************************************************

 *					

 * Name : PlayerRamWrite

 *					

 * Entry: Address & byte

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine writes to player RAM

 * 				

 ************************************************************************/



void MHPlayerRamWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite)

{

	if (sActiveCpu[0].bMemBase[0x1780] == 0)

		MHPlayer1[address - 0x200] = data;

	else

		MHPlayer2[address - 0x200] = data;

}



/************************************************************************

 *					

 * Name : MHPlayerRamRead

 *					

 * Entry: Address to read

 *					

 * Exit : Byte read back

 *					

 * Description:

 *					

 * This routine is called when player RAM is read

 * 				

 ************************************************************************/



UINT8 MHPlayerRamRead(UINT32 address, struct MemoryReadByte *pMemRead)

{

	if (sActiveCpu[0].bMemBase[0x1780] == 0)

		return(MHPlayer1[address - 0x200]);

	else

		return(MHPlayer2[address - 0x200]);

}



/************************************************************************

 *					

 * Name : MHVectorWrite

 *					

 * Entry: Address & data

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This gets called when we're writing to the vector engine's area

 * 				

 ************************************************************************/



void MHVectorWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite)

{

	MHVectorArea[address - 0x4000] = data;

}



/************************************************************************

 *					

 * Name : MHVectorRead

 *					

 * Entry: Address

 *					

 * Exit : Data

 *					

 * Description:

 *					

 * This routine is called when the vector region is read from

 * 				

 ************************************************************************/



UINT8 MHVectorRead(UINT32 address, struct MemoryReadByte *pMemRead)

{

	if (address < 0x6000)

		return(MHVectorArea[address - 0x4000]);

	return(0xff);

}



/************************************************************************

 *					

 * Name : MHAlphaBankRead

 *					

 * Entry: Address to read

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine will handle a read to the gamma->alpha status port

 * 				

 ************************************************************************/



UINT8 MHAlphaBankRead(UINT32 dwAddress, struct MemoryReadByte *pMemRead)

{

	if (0x1000 == dwAddress)

	{

		sActiveCpu[4].bMemBase[0x2800] |= 0x02;	// Alpha received

		sActiveCpu[0].bMemBase[0x1200] &= 0xfb;	// Knock out gamma XMIT bit

	}



	if (0x1200 == dwAddress)

	{

		if (VectorHalt(0))

			sActiveCpu[0].bMemBase[0x1200] |= 0x01;	// Vector generator done!

		else

			sActiveCpu[0].bMemBase[0x1200] &= 0xfe;	// Vector generator done!



		if (0 == bMHPlayer1)

			return(bMHCoinSwitch | (sActiveCpu[0].bMemBase[0x1200] & 0x1f));

		else

			return(sActiveCpu[0].bMemBase[0x1200]);

	}



	return(sActiveCpu[0].bMemBase[dwAddress]);

}



/************************************************************************

 *					

 * Name : MHAlphaBankWrite

 *					

 * Entry: Address & data

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called when a bank switch on the alpha CPU is happening.

 * 				

 ************************************************************************/



void MHAlphaBankWrite(UINT32 address, UINT8 data, struct MemoryWriteByte *pMemWrite)

{

	if (address >= 0x1000 && address < 0x1400)

		return;



	if (address >= 0x1400 && address <= 0x141f)

	{

		bColorPalette[(address & 0x1f) >> 1] = (data & 0x0f);

		return;

	}



	if (0x1600 == address)

 	{

		if ((data & 0x8) == 0)			// Gamma reset CPU order?

			CpuReset(4);



		bMHPlayer1 = data & 0x20;

		return;		

	}



	if (address == 0x1640)

	{

		MajorHavocVG((UINT16 *) (MHVectorArea));

		return;

	}



	if (address == 0x1740)

	{

		RASSERT(data < 4);



		BankSwitch(bOldMHBankId, data);



		// Copy program RAM region



		MemoryCopy(sActiveCpu[data].bMemBase + 0x800,

					  sActiveCpu[bOldMHBankId].bMemBase + 0x800,

					  0x200);



		// Copy zero page and stack page to our target



		MemoryCopy(sActiveCpu[data].bMemBase,

					  sActiveCpu[bOldMHBankId].bMemBase,

					  0x200);



		bOldMHBankId = data;

		return;

	}



	// Put other stuff here, too.



	sActiveCpu[0].bMemBase[address] = data;



	if (address == 0x17c0)

	{

		sActiveCpu[4].bMemBase[0x3000] = data; // Tell the gamma about it

		sActiveCpu[4].bMemBase[0x2800] |= 1;	// Let the gamma know its there

		sActiveCpu[0].bMemBase[0x1200] &= 0xf7; // Gamma hasn't received it

		CpuNMI(4);

		ReleaseTimeslice();							// Release our timeslice

	}

}



/************************************************************************

 *					

 * Name : GammaWriteAlpha

 *					

 * Entry: Address & data byte

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called when the gamma CPU wants to send a msg to the

 * alpha CPU.

 * 				

 ************************************************************************/



void MHGammaWriteAlpha(UINT32 dwAddress, UINT8 bData, struct MemoryWriteByte *pMemWrite)

{

	sActiveCpu[4].bMemBase[dwAddress] = bData;

	sActiveCpu[4].bMemBase[0x2800] &= 0xfd;	// Alpha hasn't received it yet

	sActiveCpu[0].bMemBase[0x1000] = bData; // Put it in the alpha CPU

	sActiveCpu[0].bMemBase[0x1200] |= 0x04; // Signal it!

	ReleaseTimeslice();

}



/************************************************************************

 *					

 * Name : MHAlphaCommRead

 *					

 * Entry: Address to read

 *					

 * Exit : Byte read (cmd read)

 *					

 * Description:

 *					

 * Called when the alpha command register is read on the gamma board

 * 				

 ************************************************************************/



UINT8 MHAlphaCommRead(UINT32 dwAddress, struct MemoryReadByte *pMemRead)

{

	sActiveCpu[4].bMemBase[0x2800] &= 0xfe;	// Clear alpha XMTD flag

	sActiveCpu[0].bMemBase[0x1200] |= 0x08;	// Gamma RCVD flag set



	return(sActiveCpu[4].bMemBase[dwAddress]);

}



/***************************************************************************

										Star Wars

 ***************************************************************************/



// Default game configuration for games that are supported on this platform



struct sRomDef sStarWarsRoms[] =

{

	// All possible Star Wars processor banks



	{"136021.105",	0,	0x3000},		// 4K vector ROM

	{"136021.102",	0,	0x8000},

	{"136021.203",	0,	0xA000},

	{"136021.104",	0,	0xC000},

	{"136021.206",	0,	0xE000},



	{"136021.107",	1,	0x4000},		// Sound ROMs

	{"136021.107",	1,	0xC000},

	{"136021.208",	1,	0x6000},

	{"136021.208",	1,	0xE000},



	{"136021.214",	0xff},		// BANK 0 and 1 of ROM 0

	{"136021.110",	0xff},		// And a place to put the mathbox ROMs

	{"136021.111",	0xff},

	{"136021.112",	0xff},

	{"136021.113",	0xff},



	// List terminator



	{NULL,	0,	0}

};



// StarWars read/write structures



struct MemoryWriteByte StarWarsWriteGameCPU[] =

{

	{0x4300, 0x4707,	SWGenWrite},

	{-1,		-1,		NULL}

};



struct MemoryReadByte StarWarsReadGameCPU[] =

{

	{0x4320, 0x4703,	SWGenHandler},

	{-1,		-1,		NULL}

};



struct MemoryWriteByte StarWarsWriteSoundCPU[] =

{

	{0x0000,	0x0000,	SWSOutWrite},

	{0x1080,	0x109f,	SWSPIAWrite},

	{0x1800,	0x181f,	PokeyClusterDataWrite},

	{0x1820,	0x183f,	PokeyClusterCtrlWrite},

	{-1,		-1,		NULL}

};



struct MemoryReadByte StarWarsReadSoundCPU[] =

{

	{0x0800,	0x0800,	SWSInRead},

	{0x1080,	0x109f,	SWSPIARead},

	{0x1820,	0x183f,	PokeyClusterCtrlRead},

	{-1,		-1,		NULL}

};



struct sCpu StarWarsProcList[] =

{

	{CPU_6809BS, RUNNING, 8196, 1500000,  NULL, NULL, StarWarsWriteGameCPU, StarWarsReadGameCPU, 

	 NULL, NULL, 0, 8196, 0, NULL, 0, NULL},



	{CPU_6809, RUNNING, 4096, 1500000,  NULL, NULL, StarWarsWriteSoundCPU, StarWarsReadSoundCPU, 

	 NULL, NULL, 0, 0, 512, SWSTimer, 0, NULL},



	PROC_TERMINATOR

};



// Game controllers in Star Wars



struct sGameFunction sStarWarsFunctions[] =

{

	{STAR_WARS_HORIZ,	"Move horizontal", "horizontal",	ANALOG,	ACTUAL,	0,

		sizeof(UINT16), (void *) &wStarWarsX, 0, 0xff},



	{STAR_WARS_VERT,	"Move vertical", 	"vertical",	ANALOG,	ACTUAL,	0,

		sizeof(UINT16), (void *) &wStarWarsY, 0xff, 0x0},



	{STAR_WARS_MLEFT,	"Move Left",	"left",	BINARY,	ACTUAL, 0,

		sizeof(UINT8),	(void *) &wStarWarsButtons,	BIT_01_ON,

		sizeof(UINT8),	(void *) &wStarWarsButtons,	BIT_01_OFF},



	{STAR_WARS_MRIGHT,	"Move Right",	"right",	BINARY, ACTUAL, 0,

		sizeof(UINT8),	(void *) &wStarWarsButtons,	BIT_02_ON,

		sizeof(UINT8),	(void *) &wStarWarsButtons,	BIT_02_OFF},



	{STAR_WARS_MUP,		"Move Up",		"up",		BINARY,	ACTUAL, 0,

		sizeof(UINT8),	(void *) &wStarWarsButtons,	BIT_04_ON,

		sizeof(UINT8),	(void *) &wStarWarsButtons,	BIT_04_OFF},



	{STAR_WARS_MDOWN,	"Move Down",	"down",	BINARY, ACTUAL, 0,

		sizeof(UINT8),	(void *) &wStarWarsButtons, BIT_08_ON,

		sizeof(UINT8),	(void *) &wStarWarsButtons, BIT_08_OFF},



	{STAR_WARS_LEFTFS,	"Left Fire Switch",		"lfiresw",	BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x4300, BIT_80_OFF,

		sizeof(UINT8), (void *) 0x4300, BIT_80_ON},



	{STAR_WARS_RIGHTFS, "Right Fire Switch",	"rfiresw",	BINARY,	VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x4300, BIT_40_OFF,

		sizeof(UINT8), (void *) 0x4300, BIT_40_ON},



	{STAR_WARS_SERVICE, "Service Mode Switch",	"servicesw",	TOGGLE,	VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x4300, BIT_10_ON,

		sizeof(UINT8), (void *) 0x4300, BIT_10_OFF},



	{STAR_WARS_COIN3,	"Aux Coin",				"auxcoin",	BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x4300, BIT_04_OFF,

		sizeof(UINT8), (void *) 0x4300, BIT_04_ON},



	{STAR_WARS_COIN,	"Coin",					"coin",		BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x4300, BIT_02_OFF,

		sizeof(UINT8), (void *) 0x4300, BIT_02_ON},



	{STAR_WARS_COIN2,	"Coin",					"coin2",		BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x4300, BIT_01_OFF,

		sizeof(UINT8), (void *) 0x4300, BIT_01_ON},



	{STAR_WARS_LTHUMB,	"Left Thumb Switch",	"lthumb",	BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x4320, BIT_20_OFF,

		sizeof(UINT8), (void *) 0x4320, BIT_20_ON},



	{STAR_WARS_RTHUMB,	"Right Thumb Switch",	"rthumb",	BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x4320, BIT_10_OFF,

		sizeof(UINT8), (void *) 0x4320, BIT_10_ON},



	{STAR_WARS_STEP,	"Diagnostic Step",		"diagstep",	BINARY, VIRTUAL, 0,

		sizeof(UINT8), (void *) 0x4320, BIT_04_OFF,

		sizeof(UINT8), (void *) 0x4320, BIT_04_ON},



	// Terminator



	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



// Audio information



struct sSoundDeviceEntry sStarWarsSound[] =

{

	{DEV_POKEY_CLUSTER, 0x80, (void *) 4, (void *) 1473300, (void *) 1},		// Four pokeys for this game

	{DEV_TMS5220, 0x80},		// And one TMS 5220



	// List terminator



	{INVALID}

};



// Star Wars dip switch settings



struct sDipSwitch sStarWarsSwitches[] = {

	{ 0xf3, VIRTUAL, 0, (void *) 0x4340 },

	{ 0xf3, VIRTUAL, 0, (void *) 0x4360 },



	DIP_TERMINATOR

};



struct sDipEnum sStarWarsShieldsChoices[4] = {

	{ "9", 3 }, { "8", 2 }, { "7", 1 }, { "6", 0 }, 

};



struct sDipEnum sStarWarsDifficultyChoices[4] = {

	{ "Hardest", 3 }, { "Hard", 2 }, { "Moderate", 1 }, { "Easy", 0 }, 

};



struct sDipEnum sStarWarsBonusChoices[] = {

	{ "3", 3 }, { "2", 2 }, { "1", 1 }, { "0", 0 },

};



struct sDipEnum sStarWarsModeChoices[] = {

	{ "Normal", 1 }, { "Freeze", 0 }

};



struct sDipEnum sStarWarsCreditsChoices[] = {

	{ "1/2", 3 }, { "1", 2 }, { "2", 1 }, { "Free Play", 0 }

};



struct sDipField sStarWarsDipFields[] = {

	// Dip switches at 0x4340

	{

		"Shields",

		"Set the starting number of shields.",

		0, 0, 0x03,

		DIP_ENUM(sStarWarsShieldsChoices)

	},

	{

		"Difficulty",

		"Set the game difficulty.",

		0, 2, 0x0c,

		DIP_ENUM(sStarWarsDifficultyChoices)

	},

	{

		"Bonus Sheilds",

		"Set the number of bonus sheilds upon completion of a wave.",

		0, 4, 0x30,

		DIP_ENUM(sStarWarsBonusChoices)

	},

	{

		"Attract Music",

		"Turn music during attract mode on or off.",

		0, 6, 0x40,

		DIP_ENUM(StdOnOffChoices)

	},

	{

		"Game Mode",

		"Put game in normal or freeze mode.",

		0, 7, 0x80,

		DIP_ENUM(sStarWarsModeChoices)

	},

	// Dip switches at 0x4360

	{

		"Coins/Credit",

		"Sets the number of coins per credit.",

		1, 0, 0x03,

		DIP_ENUM(sStarWarsCreditsChoices)

	},

	{ NULL }

};



// NVRAM save info

struct sHighScore sStarWarsHighScores[] =

{ 

	{0,	0x4500,	0x100},

	{(UINT32) INVALID}

};



// Star Wars global variables



static INT32 MPA=0;			// PROM address counter

static INT32 BIC=0;			// Block index counter

static INT32 PROM_STR[1024];	// Storage for instruction strobe only

static INT32 PROM_MAS[1024];	// Storage for direct address only

static INT32 PROM_AM[1024];	// Storage for address mode select only

static UINT8 ADC=0;

static UINT8 bSWMainRead;		// Value for /MAINREAD/SOUT

static UINT8 bSWMainWrite[SWFIFOSIZE];	// Value for /MAINWRITE/SIN

static UINT8 bSWSPortA;		// 6532 Port A data

static UINT8 bSWSDDRA;			// 6532 Port A direction

static UINT8 bSWSPortB;		// 6532 Port B data

static UINT8 bSWSDDRB;			// 6532 Port B direction

static INT16 wSWSClock;		// 6532 clock ticks until interrupt

static UINT8 bSWSTimer;		// Is the 6532 timer active???

static UINT16 wSWSMultiplier = 1;	// 6532 clock decrement value

static UINT8 bSWSFlags = 0;	// 6532 flags

static UINT8 bSWFIFOHead;

static UINT8 bSWFIFOTail;

static UINT8 bSWFIFOCount;

static INT16	RESULT;

static INT16	DIVISOR, DIVIDEND;

static UINT8 PRN;

static UINT8 bBankVal = 0x0;



/************************************************************************

 *					

 * Name : StarWarsSetup

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine takes care of converting the Star Wars mathbox PROMs to

 * something we can use.  Thanks to Steve Baines for the use of his Star

 * Wars mathbox code.

 *

 ************************************************************************/

UINT32 StarWarsSetup(struct sRegistryBlock **ppsReg)

{

	UINT16	wCount,wValue;

	UINT8	*pRAM;

	UINT8 **ppsBank;



	if (ppsReg)

	{

		// Do any necessary loading from saved file registry here



		LOAD_DATA("MPA", &MPA, sizeof(UINT32))

		LOAD_DATA("BIC", &BIC, sizeof(UINT32))

		LOAD_DATA("wSWSMultiplier", &wSWSMultiplier, sizeof(UINT16))

		LOAD_DATA("wSWSClock", &wSWSClock, sizeof(UINT16))

		LOAD_DATA("RESULT", &RESULT, sizeof(UINT16))

		LOAD_DATA("DIVISOR", &DIVISOR, sizeof(UINT16))

		LOAD_DATA("DIVIDEND", &DIVIDEND, sizeof(UINT16))

		LOAD_DATA("wStarWarsX", &wStarWarsX, sizeof(UINT16))

		LOAD_DATA("wStarWarsY", &wStarWarsY, sizeof(UINT16))

		LOAD_DATA("wStarWarsButtons", &wStarWarsButtons, sizeof(UINT16))

		LOAD_DATA("ADC", &ADC, sizeof(UINT8))

		LOAD_DATA("bSWSPortA", &bSWSPortA, sizeof(UINT8))

		LOAD_DATA("bSWSDDRA", &bSWSDDRA, sizeof(UINT8))

		LOAD_DATA("bSWSPortB", &bSWSPortB, sizeof(UINT8))

		LOAD_DATA("bSWSDDRB", &bSWSDDRB, sizeof(UINT8))

		LOAD_DATA("bSWSTimer", &bSWSTimer, sizeof(UINT8))

		LOAD_DATA("bSWSFlags", &bSWSFlags, sizeof(UINT8))

		LOAD_DATA("PRN", &PRN, sizeof(UINT8))

		LOAD_DATA("bBankVal", &bBankVal, sizeof(UINT8))



		sActiveCpu[0].psCpu->cpuGetContext(sActiveCpu[0].pContext);

		ppsBank = sActiveCpu[0].pContext->m6809bs.m6809bsBanks;



		// Set back up the bank switched context



		if (0x80 & bBankVal)

		{

			ppsBank[12] = pbAltRomData+0x2000;

			ppsBank[13] = pbAltRomData+0x2800;

			ppsBank[14] = pbAltRomData+0x3000;

			ppsBank[15] = pbAltRomData+0x3800;

		}

		else

		{

			ppsBank[12] = 0;

			ppsBank[13] = 0;

			ppsBank[14] = 0;

			ppsBank[15] = 0;

		}



		sActiveCpu[0].psCpu->cpuSetContext(sActiveCpu[0].pContext);



		return(RETRO_SETUPOK);

	}

	else

	{

		MemoryCopy(sActiveCpu[0].bMemBase+0x6000, pbAltRomData, 0x2000);



		pRAM = pbAltRomData + 0x4000;

		bBankVal = 0;



		// Translate the SW mathbox proms to something usable



		for(wCount=0;wCount<1024;wCount++)

		{

			// Translate PROMS into 16 bit code

			wValue = 0;

			wValue = (wValue | ((pRAM[0x0c00+wCount]     ) & 0x000f));	// Set LS nibble

			wValue = (wValue | ((pRAM[0x0800+wCount]<< 4 ) & 0x00f0));

			wValue = (wValue | ((pRAM[0x0400+wCount]<< 8 ) & 0x0f00));

			wValue = (wValue | ((pRAM[0x0000+wCount]<<12 ) & 0xf000));	// Set MS nibble



			// Perform pre-decoding



			PROM_STR[wCount]=(wValue>>8)&0x00ff;

			PROM_MAS[wCount]=(wValue&0x007f);

			PROM_AM[wCount]=((wValue>>7)&0x0001);

		}



		sActiveCpu[0].bMemBase[0x4300] = 0xdf;

		sActiveCpu[0].bMemBase[0x4320] = 0x34;

		sActiveCpu[0].bMemBase[0x4340] = 0xb3;

		sActiveCpu[0].bMemBase[0x4360] = 0xfd;



		return(RETRO_SETUPOK);

	}

}



UINT32 StarWarsShutdown(struct sRegistryBlock **ppsReg)

{

	if (ppsReg)

	{

		SAVE_DATA("MPA", &MPA, sizeof(UINT32), REG_UINT32)

		SAVE_DATA("BIC", &BIC, sizeof(UINT32), REG_UINT32)

		SAVE_DATA("wSWSMultiplier", &wSWSMultiplier, sizeof(UINT16), REG_UINT16)

		SAVE_DATA("wSWSClock", &wSWSClock, sizeof(UINT16), REG_UINT16)

		SAVE_DATA("RESULT", &RESULT, sizeof(UINT16), REG_UINT16)

		SAVE_DATA("DIVISOR", &DIVISOR, sizeof(UINT16), REG_UINT16)

		SAVE_DATA("DIVIDEND", &DIVIDEND, sizeof(UINT16), REG_UINT16)

		SAVE_DATA("wStarWarsX", &wStarWarsX, sizeof(UINT16), REG_UINT16)

		SAVE_DATA("wStarWarsY", &wStarWarsY, sizeof(UINT16), REG_UINT16)

		SAVE_DATA("wStarWarsButtons", &wStarWarsButtons, sizeof(UINT16), REG_UINT16)

		SAVE_DATA("ADC", &ADC, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bSWSPortA", &bSWSPortA, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bSWSDDRA", &bSWSDDRA, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bSWSPortB", &bSWSPortB, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bSWSDDRB", &bSWSDDRB, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bSWSTimer", &bSWSTimer, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bSWSFlags", &bSWSFlags, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("PRN", &PRN, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bBankVal", &bBankVal, sizeof(UINT8), REG_UINT8)



		return(RETRO_SETUPOK);

	}

	else

	{

		return(RETRO_SETUPOK);

	}

}



/************************************************************************

 *					

 * Name : SWRunMathbox

 *					

 * Entry: Address/value

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called when Star Wars's vector generator is pulsed

 * 				

 ************************************************************************/

static void SWRunMathbox(void)

{

	INT16	ACC = 0, A = 0, B = 0, C = 0;

	INT32	RAMWORD=0;

	INT32	MA_byte;

	INT32	tmp;

	INT32	M_STOP=100000;			// Limit on number of instructions allowed before halt

	INT32	MA;

	INT32	IP15_8, IP7, IP6_0;		// Instruction PROM values

	UINT8	*pRAM = sActiveCpu[0].bMemBase;

	

	while(M_STOP>0)

	{

		IP15_8 = PROM_STR[MPA];

		IP7    = PROM_AM[MPA];

		IP6_0  = PROM_MAS[MPA];



		if(IP7==0)

		{

			MA=((IP6_0 &3) | ( (BIC&0x01ff) <<2) );		// MA10-2 set to BIC8-0

		}

		else

		{

			MA=IP6_0;

		}





		// Convert RAM offset to eight bit addressing (2kx8 rather than 1k*16)

		// and apply base address offset



		MA_byte=0x5000+(MA<<1);



		RAMWORD=((pRAM[MA_byte+1]&0x00ff) | ((pRAM[MA_byte]&0x00ff)<<8));



		/*

		 * RAMWORD is the sixteen bit Math RAM value for the selected address

		 * MA_byte is the base address of this location as seen by the main CPU

		 * IP is the 16 bit instruction word from the PROM. IP7_0 have already

		 * been used in the address selection stage

		 * IP15_8 provide the instruction strobes

		 */



		switch(IP15_8)

		{

		case READ_ACC:

			pRAM[MA_byte+1]=(ACC & 0x00ff);

			pRAM[MA_byte  ]=( ((ACC & 0xff00) >>8) & 0x00ff);

			break;



		case R_ACC_M_HLT:

			M_STOP=0;

			pRAM[MA_byte+1]=(ACC & 0x00ff);

			pRAM[MA_byte  ]=( ((ACC & 0xff00) >>8) & 0x00ff);

			break;



		case LAC:

			ACC = (INT16) RAMWORD;

			break;



		case M_HALT:

			M_STOP=0;

			break;



		case INC_BIC:

			BIC++;

			BIC=BIC&0x1ff;		// Restrict to 9 bits

			break;



		case INC_BIC_M_HLT:

			M_STOP=0;

			BIC++;

			BIC=BIC&0x1ff;		// Restrict to 9 bits

			break;



		case INC_BIC_M_HLT_R_ACC:

			M_STOP=0;

			BIC++;

			BIC=BIC&0x1ff;		// Restrict to 9 bits

			pRAM[MA_byte+1]=(ACC & 0x00ff);

			pRAM[MA_byte  ]=( ((ACC & 0xff00) >>8) & 0x00ff);

			break;



		case INC_BIC_R_ACC:

			BIC++;

			BIC=BIC&0x1ff;		// Restrict to 9 bits

			pRAM[MA_byte+1]=(ACC & 0x00ff);

			pRAM[MA_byte  ]=( ((ACC & 0xff00) >>8) & 0x00ff);

			break;



		case CLEAR_ACC:

			ACC = 0;

			break;



		case NOP:

			break;



		case C_ACC_M_HLT:

			ACC = 0;

			M_STOP=1;

			break;



		case LDC:

			// Writing to C triggers the calculation

			C = (INT16) RAMWORD;

			// ACC=ACC+(  ( (long)((A-B)*C) )>>14  );

			// round the result - this fixes bad tranch vectors in Star Wars

			ACC=ACC+(((((INT32)((A-B)*C))>>13)+1)>>1);

			break;



		case LDB:

			B = (INT16) RAMWORD;

			break;



		case LDC_M_HLT:

			// Writing to C triggers the calculation

			M_STOP=0;

			C = (INT16) RAMWORD;

			ACC=ACC+(INT16) (((INT32)((A-B)*C))>>14);

			// round the result - this fixes bad tranch vectors in Star Wars

			ACC=ACC+(((((INT32)((A-B)*C))>>13)+1)>>1);

			break;



		case LDB_M_HLT:

			M_STOP=0;

			B = (INT16) RAMWORD;

			break;



		case LDA_M_HLT:

			M_STOP=0;

			A = (INT16) RAMWORD;

			break;



		case LDB_CACC:

			ACC = 0;

			B = (INT16) RAMWORD;

			break;



		case LDA:

			A = (INT16) RAMWORD;

			break;



		case LDA_CACC:

			ACC = 0;

			A = (INT16) RAMWORD;

				break;



		default:

			break;

		}



		/*

		 * Now update the PROM address counter

		 * Done like this because the top two bits are not part of the counter

		 * This means that each of the four pages should wrap around rather than

		 * leaking from one to another.  It may not matter, but I've put it in anyway

		 */



		tmp=MPA;

		tmp++;

		MPA=(MPA&0x0300)|(tmp&0x00ff);		// New MPA value



		M_STOP--;			// Decrease count

	}

}



// Global divider variables



/************************************************************************

 *					

 * Name : SWMathbox

 *					

 * Entry: Address/value

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine is called when Star Wars's mathbox required

 * 				

 ************************************************************************/

void SWMathbox(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	switch(addr & 0x07)

    {

	case 0:

		MPA=(val<<2);			// Set starting PROM address

		SWRunMathbox();			// and run the Mathbox

		break;



	case 1:

		BIC=( (BIC & 0x00ff) | ((UINT16)(val & 0x01)<<8) );

		break;



	case 2:

		BIC=((BIC & 0x0100) | val);

		break;



	case 4: /* dvsrh */

		DIVISOR = ((DIVISOR & 0x00ff) | (val<<8));

		break;



	case 5: /* dvsrl */

		DIVISOR = ((DIVISOR & 0xff00) | (val));

		if(DIVISOR!=0)

		{

			RESULT = (INT16)(((INT32)DIVIDEND<<14)/(INT32)DIVISOR);

		}

		else

		{

			RESULT = (INT16)-1;

		}

		break;



	case 6: /* dvddh */

		DIVIDEND = ((DIVIDEND & 0x00ff) | ((UINT16) (val<<8)));

		break;



	case 7: /* dvddl */

		DIVIDEND = ((DIVIDEND & 0xff00) | (val));

		break;



	default:

		break;

	}

}



UINT8 SWGenHandler(UINT32 dwAddr, struct MemoryReadByte *pMemRead)

{

	if (0x4320 == dwAddr)

	{

		UINT8 bVal = 0x34;



		// Get the game CPU's context into memory so we can look at it.

		psCurrentCpu->psCpu->cpuGetContext(psCurrentCpu->pContext);



		// Code segment is really at F978, but the PC is not updated yet...

		if(0xf978 == psCurrentCpu->pContext->m6809.m6809pc)

			bVal |= 0x80;



		if(VectorHalt(0))

			bVal |= 0x40;



		return(bVal | (psCurrentCpu->bMemBase[0x4320] & 0x3f));

	}



	if (0x4380 == dwAddr)

		return(ADC);

	

	if (0x4400 == dwAddr)

	{

		bSWSPortA &= 0xbf;	// Allow writes for the sound CPU

		return(bSWMainRead);

	}



	if (0x4401 == dwAddr)

	{

		if(bSWFIFOCount == SWFIFOSIZE)

			return(bSWSPortA & 0x40) | 0x80;

		return(bSWSPortA & 0x40);

	}



	if (0x4700 == dwAddr)

		return(RESULT >> 8);



	if (0x4701 == dwAddr)

		return(RESULT & 0xff);



	if (0x4703 == dwAddr)

	{

		PRN=(INT8)((PRN+0x2364)^2);

		return(PRN);

	}



	return(psCurrentCpu->bMemBase[dwAddr]);

}



void SWGenWrite(UINT32 dwAddr, UINT8 bVal, struct MemoryWriteByte *pMemWrite)

{

	if (0x4400 == dwAddr)

	{

		if(SWFIFOSIZE == bSWFIFOCount)

		{

			bSWFIFOCount = 1;

			bSWMainWrite[bSWFIFOHead] = bVal;

			bSWFIFOTail = (bSWFIFOHead+1)%SWFIFOSIZE;

			return;

		}

	

		bSWMainWrite[bSWFIFOTail] = bVal;

		bSWFIFOTail = (bSWFIFOTail+1)%SWFIFOSIZE;

		bSWFIFOCount++;

	}



	if (0x4600 == dwAddr)

	{

		AnalogVectorGenerator((UINT16 *) (0x0 + psCurrentCpu->bMemBase), FALSE, TRUE);

		return;

	}



	if (0x4684 == dwAddr)		// Bank switch?

	{

		UINT8 **ppsBank = psCurrentCpu->pContext->m6809bs.m6809bsBanks;

		

		psCurrentCpu->psCpu->cpuGetContext(psCurrentCpu->pContext);

		bBankVal = bVal;



		if (0x80 & bVal)

		{

			ppsBank[12] = pbAltRomData+0x2000;

			ppsBank[13] = pbAltRomData+0x2800;

			ppsBank[14] = pbAltRomData+0x3000;

			ppsBank[15] = pbAltRomData+0x3800;

		}

		else

		{

			ppsBank[12] = 0;

			ppsBank[13] = 0;

			ppsBank[14] = 0;

			ppsBank[15] = 0;

		}



		psCurrentCpu->psCpu->cpuSetContext(psCurrentCpu->pContext);



		return;

	}



	if (0x4685 == dwAddr)

	{

		PRN = 0;

		return;

	}



	if (0x46e0 == dwAddr)

	{	

		bSWMainRead = 0;

		bSWSPortA = 0x3f;

		bSWSTimer = 0;

		bSWSFlags = 0;

		bSWFIFOHead = 0;

		bSWFIFOTail = 0;

		bSWFIFOCount = 0;

		return;

	}



	if (dwAddr >= 0x4700 && dwAddr <= 0x4707)

	{

		SWMathbox(dwAddr, bVal, pMemWrite);

		return;

	}





	if (dwAddr >= 0x46c0 && dwAddr <= 0x46c2)

	{ 

		dwAddr &= 0x03;



		if ((wStarWarsButtons & 1) && wStarWarsX)

			wStarWarsX--;

		if ((wStarWarsButtons & 2) && wStarWarsX < 0xfe)

			wStarWarsX++;

		if ((wStarWarsButtons & 4) && wStarWarsY)

			wStarWarsY--;

		if ((wStarWarsButtons & 8) && wStarWarsY < 0xfe)

			wStarWarsY++;



		if (0x00 == dwAddr)	// ADC 0?

			ADC = (0xff - wStarWarsY) & 0xff;

		if (0x01 == dwAddr) // ADC 1?

			ADC = wStarWarsX & 0xff;

		return;

	}

}



/************************************************************************

 *					

 * Name : SWSInRead

 *					

 * Entry: Address/read structure

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * Have the sound CPU read the shared byte left by the game CPU...

 * 				

 ************************************************************************/

UINT8 SWSInRead(UINT32 addr, struct MemoryReadByte *pMemRead)

{

	UINT8 bTemp;



    if(0 == bSWFIFOCount)

    {

        return(bSWMainWrite[bSWFIFOHead]);

    }



    bSWFIFOCount--;

    bTemp = bSWMainWrite[bSWFIFOHead];

    bSWFIFOHead = (bSWFIFOHead+1)%SWFIFOSIZE;



    return(bTemp);

}



/************************************************************************

 *					

 * Name : SWSOutWrite

 *					

 * Entry: Address/value

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * Write data for the main CPU to read

 * 				

 ************************************************************************/

void SWSOutWrite(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	bSWMainRead = val;

	bSWSPortA |= 0x40;	// Forbid any more writes until the game CPU reads

}



/************************************************************************

 *					

 * Name : SWSPIARead

 *					

 * Entry: Address/value

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * Read data from the 6532A PIA

 * 				

 ************************************************************************/

UINT8 SWSPIARead(UINT32 addr, struct MemoryReadByte *pMemRead)

{

	switch(addr-0x1080)

	{

		case 0x00:			// Read Port A

			if (0 == bSWFIFOCount)

				return(((bSWSPortA&0x7b) | 0x10));

			else

				return(((bSWSPortA&0x7b) | 0x90));



        case 0x01:			// Read Port A DDR

            return(bSWSDDRA);



        case 0x02:			// Read Port B

            return(bSWSPortB);



        case 0x03:			// Read Port B DDR

            return(bSWSDDRB);



        case 0x05:

			{

				UINT8 bTemp = bSWSFlags;



				bSWSFlags = 0;



				return(bTemp);

			}



		default:

            return(0);

     }

}



/************************************************************************

 *					

 * Name : SWSPIAWrite

 *					

 * Entry: Address/value

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * Write data for the 6532A PIA

 * 				

 ************************************************************************/

void SWSPIAWrite(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	switch(addr-0x1080)

	{

		case 0x00:			// Write to Port A

			if(bSWSPortA & 0x01)

			{

				bSWSPortA = (bSWSPortA & (~bSWSDDRA)) | (val & bSWSDDRA);

				if(!(bSWSPortA & 0x01))

				{

					Tms5220Write(bSWSPortB);

				}

			}

			else

			{

				bSWSPortA = (bSWSPortA & (~bSWSDDRA)) | (val & bSWSDDRA);

			}

            return;



        case 0x01:			// Write to Port A DDR

            bSWSDDRA = val;

            return;



        case 0x02:			// Write to Port B

			// For some reason it bails hard using the DDR -- must be before it's

			// initialized.  Anyhow, when it's initialized, it's 0xff, so it

			// effectively does nothing.

			//

			// bSWSPortB = (bSWSPortB & (~bSWSDDRB)) | (val & bSWSDDRB);

			bSWSPortB = val;

            return;



        case 0x03:			// Write to Port B DDR

            bSWSDDRB = val;

            return;





		case 0x0e:			// Decrement timer every 1 clock

			wSWSMultiplier = 0x01;

            return;



		case 0x0f:			// Decrement timer every 8 clocks

			wSWSMultiplier = 0x08;

            return;



		case 0x10:			// Decrement timer every 64 clocks

			wSWSMultiplier = 0x40;

            return;



		case 0x11:			// Decrement timer every 1024 clocks

			wSWSMultiplier = 0x400;

            return;



        case 0x1f:

            wSWSClock = val*0x400;



            bSWSTimer = 1;

            return;



		default:

            return;

     }

}



/************************************************************************

 *					

 * Name : SWSTimer

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * A periodic function to check the 6532 timer to see if we interrupt the

 * sound CPU

 * 				

 ************************************************************************/

void SWSTimer(void)

{

	if(bSWSTimer)

	{

		// BGS: Tweaking value, was previously 0x3e0

		wSWSClock -= (0x400/wSWSMultiplier);



		if(wSWSClock <= 0)

		{

			bSWSTimer = 0;		// Stop the timer

			bSWSFlags = 0x80;	// Set the PIA interrupt flag

			CpuINT(1,0);		// Generate an IRQ on the sound CPU

		}

	}

}



/***************************************************************************

										Quantum

 ***************************************************************************/



// Default game configuration for games that are supported on this platform



static UINT8 *bQuantumVG = NULL;

static UINT8 bControllerReset = 0;

static volatile INT8 bXDelta = 0;

static volatile INT8 bYDelta = 0;

static INT8 bXVal = 0;

static INT8 bYVal = 0;

static UINT8 *QuantumNvram = NULL;

static UINT8 bVector = 0;

static UINT16 wQuantumSwitches = 0xfffe;



struct sRomDef sQuantumRoms[] =

{

	{"136016.106",	0,	0x0000},		// Even ROM 0

	{"136016.101",	0,	0x2000},		// Odd ROM 0

	{"136016.107",	0,	0x4000},		// Even ROM 1

	{"136016.102",	0,	0x6000},		// Odd ROM 1

	{"136016.108",	0,	0x8000},		// Even ROM 2

	{"136016.103",	0,	0xa000},		// Odd ROM 2

	{"136016.109",	0,	0xc000},		// Even ROM 3

	{"136016.104",	0,	0xe000},		// Odd ROM 3

	{"136016.110",	0,	0x10000},	// Even ROM 4

	{"136016.105",	0,	0x12000},	// Odd ROM 4 



// List terminator



	{NULL,	0,	0}

};



// Game controllers in Quantum



struct sGameFunction sQuantumFunctions[] =

{

	{QUANTUM_COIN1,	"Left coin",	"lcoin",	BINARY,	ACTUAL,	0, 

		sizeof(UINT8),	(void *) &wQuantumSwitches, 0xdf, 0x0, sizeof(UINT8), (void *) &wQuantumSwitches, 0xdf, 0x20},



	{QUANTUM_COIN2,	"Right coin",	"rcoin",	BINARY,	ACTUAL,	0, 

		sizeof(UINT8),	(void *) &wQuantumSwitches, 0xef, 0x0, sizeof(UINT8), (void *) &wQuantumSwitches, 0xef, 0x10},



	{QUANTUM_START2,	"2 Player start",	"start2",	BINARY,	ACTUAL,	0, 

		sizeof(UINT8),	(void *) &wQuantumSwitches, 0xf7, 0x0, sizeof(UINT8), (void *) &wQuantumSwitches, 0xf7, 0x08},



	{QUANTUM_START1,	"1 Player start",	"start1",	BINARY,	ACTUAL,	0, 

		sizeof(UINT8),	(void *) &wQuantumSwitches, 0xfb, 0x0, sizeof(UINT8), (void *) &wQuantumSwitches, 0xfb, 0x04},



	{QUANTUM_TRAKX,	"Trackball X",	"trackx",	BALLISTICS, ACTUAL, 0,

		sizeof(UINT8), (void *) &bXDelta, (UINT32) -3, (UINT32) 3},



	{QUANTUM_TRAKY,	"Trackball Y",	"tracky",	BALLISTICS, ACTUAL, 0,

		sizeof(UINT8), (void *) &bYDelta, (UINT32) -3, (UINT32) 3},



	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



struct sSoundDeviceEntry sQuantumSound[] =

{

	{DEV_POKEY_CLUSTER, 0x80, (void *) 2, (void *) 1373300},		// Two pokeys for this game



	// List terminator



	{INVALID}

};



struct sBigCpuBlock sQuantumBlock[]=

{

	{MEM_EXEC,	0x0,	0x17fff, NULL, NULL, NULL, NULL},	// ROM Region

	{MEM_RAM,	0x18000, 0x1ffff, NULL, NULL, NULL, NULL},	// RAM

	{MEM_RAM,	0x800000, 0x803fff, NULL, NULL, NULL, NULL},	// Vector generator RAM

	{MEM_RAM,	0x960000, 0x9601ff, NULL, NULL, NULL, NULL}, // NVRAM

	{MEM_TRAP,	0x840000, 0x9fffff, NULL, NULL, QuantumFunctionsReadWord, QuantumFunctionsWriteWord},

	{INVALID}

};



struct sCpu QuantumProcList[] =

{

	{CPU_M68000, RUNNING, 0x00018235, 9000000,  sQuantumBlock, NULL,

	 NULL, NULL, NULL, NULL, 0, 0, 33333, QuantumPeriodicHandler, 0, NULL},



	PROC_TERMINATOR

};



// Runtime fetch junk





UINT16 QuantumFunctionsReadWord(UINT32 dwAddr, struct MemoryReadWord *pRead)

{

	struct MemoryReadByte p;

	UINT16 wVal;



	if (dwAddr >= 0x940000 && dwAddr < 0x948000)

	{

		if (bControllerReset)

		{

			bXVal = bControllerReset;

			bYVal = bControllerReset;

			--bControllerReset;

		}



		wVal = (UINT16) ((bYVal & 0xf) | (bXVal << 4));

		return(wVal);

	}



	if (dwAddr >= 0x948000 && dwAddr < 0x950000)

	{

		if (bVector > 3)

			wQuantumSwitches |= 0x0001;

		else

			wQuantumSwitches &= 0xfffe;

		return(HOST_TO_LE16(wQuantumSwitches));

	}



	if (dwAddr >= 0x840000 && dwAddr < 0x840040)

	{

		dwAddr = (dwAddr >> 1);



		if (dwAddr & 0x10)

			p.pUserArea = bPokey2;

		else

			p.pUserArea = bPokey1;



		return((UINT16) PokeyRead(dwAddr, &p));

	}



	return(0xffff);

}



void QuantumFunctionsWriteWord(UINT32 dwAddr, UINT16 wData, struct MemoryWriteWord *pWrite)

{

	struct MemoryWriteByte p;



	if (dwAddr == 0x970000 && dwAddr < 0x977fff)

	{

		bXVal += bXDelta;

		bYVal -= bYDelta;



		QuantumVectorGenerator((UINT16 *) bQuantumVG, 0);

		return;

	}



	if (dwAddr >= 0x950000 && dwAddr < 0x950020)

	{

		bColorPalette[(dwAddr >> 1) & 0x0f] = (wData & 0xf);

		return;

	}



	if (dwAddr >= 0x840000 && dwAddr < 0x840040)

	{

		dwAddr = dwAddr >> 1;



		if (dwAddr & 0x10)

			p.pUserArea = bPokey2;

		else

			p.pUserArea = bPokey1;



		PokeyWrite(dwAddr & 0x0f, (UINT8) (wData & 0xff), &p);

		return;

	}



}



void QuantumPeriodicHandler(void)

{

	bVector++;

	if (bVector == 6)

		bVector = 0;

	CpuINT(0, 1);

}



/************************************************************************

 *					

 * Name : QuantumSetup

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine will interleave the ROMs loaded into the Quantum memory space

 * 				

 ************************************************************************/



UINT32 QuantumSetup(struct sRegistryBlock **ppsReg)

{

	UINT8 *pbData;



	if (ppsReg)

	{

		// Do any necessary loading from saved file registry here



		LOAD_DATA("wQuantumSwitches", &wQuantumSwitches, sizeof(UINT16))

		LOAD_DATA("bXVal", &bXVal, sizeof(UINT8))

		LOAD_DATA("bYVal", &bYVal, sizeof(UINT8))

		LOAD_DATA("bVector", &bVector, sizeof(UINT8))

		LOAD_DATA("bControllerReset", &bControllerReset, sizeof(UINT8))



		return(RETRO_SETUPOK);

	}

	else

	{

		bControllerReset = 0x0f;

		wQuantumSwitches = 0xffff;



		// swap 128K's worth of 8K roms.



		pbData = FindMemoryBase(0, 0, 1);

		RASSERT(pbData);

		InterleaveRoms(pbData, 0x18000, 8192);



		bQuantumVG = FindMemoryBase(0, 0x800000, 1);

		RASSERT(bQuantumVG);

		QuantumNvram = FindMemoryBase(0, 0x960000, 1);

		RASSERT(QuantumNvram);



		return(RETRO_SETUPOK);

	}

}



UINT32 QuantumShutdown(struct sRegistryBlock **ppsReg)

{

	if (ppsReg)

	{

		// Put code here to write anything to the save file registry



		SAVE_DATA("wQuantumSwitches", &wQuantumSwitches, sizeof(UINT16), REG_UINT16)

		SAVE_DATA("bXVal", &bXVal, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bYVal", &bYVal, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bVector", &bVector, sizeof(UINT8), REG_UINT8)

		SAVE_DATA("bControllerReset", &bControllerReset, sizeof(UINT8), REG_UINT8)



		return(RETRO_SETUPOK);

	}

	else

	{

		return(RETRO_SETUPOK);

	}

}



/***************************************************************************

								Empire Strikes Back

 ***************************************************************************/



// Default game configuration for games that are supported on this platform



struct sRomDef sESBRoms[] =

{

	// All possible Star Wars processor banks



	// CPU 0 - Main processor



	{"136031.111",	0,	0x3000},		// 4K vector ROM

	// Nothing is loaded at 8K - it gets bank switched in later on

	{"136031.102",	0,	0xA000},

	{"136021.103",	0,	0xC000},

	{"136031.104",	0,	0xE000},



	// CPU 1 - Sound CPU



	{"136031.113",	1,	0x4000},		// Sound ROMs

	{"136031.113",	1,	0xC000},

	{"136031.112",	1,	0x6000},

	{"136031.112",	1,	0xE000},

			

	// CPU 2 - General definitions



	{"136031.101",	2,	0x0000},		// BANK 0 and 1 of ROM 0



	{"PROM_7H.BIN",	2,	0x4000},		// And a place to put the mathbox ROMs

	{"PROM_7J.BIN",	2,	0x4400},

	{"PROM_7K.BIN",	2,	0x4800},

	{"PROM_7L.BIN",	2,	0x4c00},



	{"136031.106",	2, 0x8000},		// BANK 0 and 1

	{"136031.105",	2, 0xc000},		// BANK 2 and 3



	// List terminator



	{NULL,	0,	0}

};



// ESB read/write structures



struct MemoryWriteByte ESBWriteGameCPU[] =

{

	{-1,		-1,		NULL}

};



struct MemoryReadByte ESBReadGameCPU[] =

{

	{-1,		-1,		NULL}

};



struct sCpu ESBProcList[] =

{

	{CPU_6809, RUNNING, 0x00010180, 1500000,  NULL, NULL, ESBWriteGameCPU, ESBReadGameCPU, 

	 NULL, NULL, 0, 8192, 0, NULL, 0, NULL},



	{CPU_6809, RUNNING, 0x00010120, 1500000,  NULL, NULL, StarWarsWriteSoundCPU, StarWarsReadSoundCPU,

	 NULL, NULL, 0, 0, 0x100, SWSTimer, 0, NULL},



	{CPU_6809, INACTIVE, 0x00010080, 0,  NULL, NULL, StarWarsWriteGameCPU, StarWarsReadGameCPU, 

	 NULL, NULL, 0, 0, 0, NULL, 0, NULL},



	PROC_TERMINATOR

};



/************************************************************************

 *					

 * Name : StarWarsSetup

 *					

 * Entry: Nothing

 *					

 * Exit : Nothing

 *					

 * Description:

 *					

 * This routine takes care of converting the Star Wars mathbox PROMs to

 * something we can use.  Thanks to Steve Baines for the use of his Star

 * Wars mathbox code.

 * 				

 ************************************************************************/

UINT32 ESBSetup(struct sRegistryBlock **ppsReg)

{

	UINT16	wCount,wValue;

	UINT8	*pRAM;



	if (ppsReg)

	{

		// Do any necessary loading from saved file registry here



		return(RETRO_SAVE_GAME_NOT_SUPPORTED);

	}

	else

	{

		MemoryCopy(sActiveCpu[0].bMemBase+0x6000, sActiveCpu[2].bMemBase, 0x2000);

		MemoryCopy(sActiveCpu[0].bMemBase+0x8000, sActiveCpu[2].bMemBase + 0x8000, 0x2000);



		pRAM = sActiveCpu[2].bMemBase+0x4000;



		// Translate the SW mathbox proms to something usable



		for(wCount=0;wCount<1024;wCount++)

		{

			// Translate PROMS into 16 bit code

			wValue = 0;

			wValue = (wValue | ((pRAM[0x0c00+wCount]     ) & 0x000f));	// Set LS nibble

			wValue = (wValue | ((pRAM[0x0800+wCount]<< 4 ) & 0x00f0));

			wValue = (wValue | ((pRAM[0x0400+wCount]<< 8 ) & 0x0f00));

			wValue = (wValue | ((pRAM[0x0000+wCount]<<12 ) & 0xf000));	// Set MS nibble



			// Perform pre-decoding

			PROM_STR[wCount]=(wValue>>8)&0x00ff;

			PROM_MAS[wCount]=(wValue&0x007f);

			PROM_AM[wCount]=((wValue>>7)&0x0001);

		}



		sActiveCpu[0].bMemBase[0x4300] = 0xff;

		sActiveCpu[0].bMemBase[0x4320] = 0xff;

		sActiveCpu[0].bMemBase[0x4340] = 0xff;

		sActiveCpu[0].bMemBase[0x4360] = 0xff;



		return(RETRO_SETUPOK);

	}

}



/***************************************************************************

										Tollian's Web

 ***************************************************************************/



// Default game configuration for games that are supported on this platform



struct sRomDef sTolliansWebRoms[] =

{

	// All possible processor banks



	{"page01.tw",	0,	0x2000},

	{"vec5000.tw",	0,	0x5000},

	{"8000.tw",		0, 0x8000},

	{"a000.tw",		0,	0xa000},

	{"c000.tw",		0,	0xc000},

	{"e000.tw",		0,	0xe000},

	{"e000.tw",		0,	0xf000},



	{"page01.tw",	1,	0x0000},

	{"vec5000.tw",	1,	0x5000},

	{"8000.tw",		1, 0x8000},

	{"a000.tw",		1,	0xa000},

	{"c000.tw",		1,	0xc000},

	{"e000.tw",		1,	0xe000},

	{"e000.tw",		1,	0xf000},



	{"vec5000.tw",	0xff},

	{"vpg01.tw",	0xff},

	{"vpg23.tw",	0xff},



	// List terminator



	{NULL,	0,	0}

};



// Game controllers in Tollian's web



struct sGameFunction sTolliansWebFunctions[] =

{

	{TOLLIANS_WEB_COIN, "Coin up", "coin", BINARY, VIRTUAL, 0, 

		sizeof(UINT8), (void *) 0x1060, 0xf7, 0x08, sizeof(UINT8), (void *) 0x1060, 0xf7, 0x00}, 



	{INVALID, NULL, NULL, (UINT8) INVALID, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

};



// Tollian's Web read/write structures



struct MemoryWriteByte TolliansWebWrite[] =

{

	{0x300,	0x7ff,	TolliansWebPlayerRamWrite},

	{0x1020,	0x102f,	PokeyWrite},

	{0x1030,	0x103f,	PokeyWrite},

	{0x1040,	0x10a0,	NothingWrite},

	{0x10a4,	0x10a4,	TolliansWebVectorGenerator},

	{0x10b4,	0x10b7,	TolliansWebBankSwitch},

	{0x1400,	0x141f,	TolliansWebColorWrite},

	{0x4000,	0x4fff,	TolliansWebVectorWrite},

	{0x5000,	0xffff,	NothingWrite},

	{-1,		-1,		NULL}

};



struct MemoryReadByte TolliansWebRead[] =

{

	{0x300,	0x7ff,	TolliansWebPlayerRamRead},

	{0x1020,	0x102f,	PokeyRead},

	{0x1030,	0x103f,	PokeyRead},

	{0x1040,	0x1040,	TolliansWebInputs},

	{0x4000,	0x7fff,	TolliansWebVectorRead},

	{-1,		-1,		NULL}

};



// High score saving area for Tollian's Web



struct sHighScore sTolliansWebHighScores[] =

{ 

	{(UINT32) INVALID}

};



struct sCpu TolliansWebProcList[] =

{

	{CPU_6502ZP, RUNNING, 0x00010080, 2400000,  NULL, NULL, TolliansWebWrite, TolliansWebRead, 

	 NULL, NULL, 0, 6000, 0, NULL, 0, NULL},



	{CPU_6502ZP, BANKED, 0x00010080, 2400000,  NULL, NULL, TolliansWebWrite, TolliansWebRead, 

	 NULL, NULL, 0, 6000, 0, NULL, 0, NULL},



	PROC_TERMINATOR

};



// Audio information



struct sSoundDeviceEntry sTolliansWebSound[] =

{

	{DEV_POKEY_CLUSTER, 0x80, (void *) 2, (void *) 1373300},		// Two pokeys for this game



	// List terminator



	{INVALID}

};



UINT32 TolliansWebSetup(struct sRegistryBlock **ppsReg)

{

	UINT32 dwLoop = 0;



	if (ppsReg)

	{

		// Do any necessary loading from saved file registry here



		return(RETRO_SAVE_GAME_NOT_SUPPORTED);

	}

	else

	{

		for (dwLoop = 0; dwLoop < 2; dwLoop++)

		{

			sActiveCpu[dwLoop].bMemBase[0x8001] = 0xff;

			sActiveCpu[dwLoop].bMemBase[0x8254] = 0x8f;

			sActiveCpu[dwLoop].bMemBase[0x9120] = 0x12;

			sActiveCpu[dwLoop].bMemBase[0x96d0] = 0x4c;

			sActiveCpu[dwLoop].bMemBase[0xe509] = 0x98;

			sActiveCpu[dwLoop].bMemBase[0xea91] = 0xc9;

			sActiveCpu[dwLoop].bMemBase[0xec76] = 0x2c;

			sActiveCpu[dwLoop].bMemBase[0xefff] = 0x85;

			sActiveCpu[dwLoop].bMemBase[0xffff] = 0x85;

		}



		MHVectorArea = MyMalloc(0xa000, "TolliansWebSetup()");

		MemoryCopy(MHVectorArea + 0x1000, pbAltRomData, 0x1000);

		MemoryCopy(MHVectorArea + 0x2000, (UINT8 *) (pbAltRomData + 0x1000), 0x4000);

		MemoryCopy(MHVectorArea + 0x6000, (UINT8 *) (pbAltRomData + 0x5000), 0x4000);



		MHPlayer1 = MyMalloc(0x600, "TolliansWebSetup()");

		MHPlayer2 = MyMalloc(0x600, "TolliansWebSetup()");

		sActiveCpu[0].bMemBase[0x1060] = 0xff;

		AltRomEject();				// Get rid of any outstanding Alt ROMs!



		return(RETRO_SETUPOK);

	}

}



UINT32 TolliansWebShutdown(struct sRegistryBlock **ppsReg)

{

	if (ppsReg)

	{

		// Put code here to write anything to the save file registry



		return(RETRO_SAVE_GAME_NOT_SUPPORTED);

	}

	else

	{

		MyFree((void *) MHPlayer1, "TolliansWebShutdown()");

		MyFree((void *) MHPlayer2, "TolliansWebShutdown()");

		MyFree((void *) MHVectorArea, "TolliansWebShutdown()");

		return(RETRO_SETUPOK);

	}

}



void TolliansWebPlayerRamWrite(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	if (bMHPlayer1)

		MHPlayer2[addr - 0x300] = val;

	else

		MHPlayer1[addr - 0x300] = val;

}



UINT8 TolliansWebPlayerRamRead(UINT32 dwAddr, struct MemoryReadByte *pMemRead)

{

	if (bMHPlayer1)

		return(MHPlayer2[dwAddr - 0x300]);

	else

		return(MHPlayer1[dwAddr - 0x300]);

}

													

void TolliansWebVectorGenerator(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	MajorHavocVG((UINT16 *) MHVectorArea);

}



void TolliansWebVectorWrite(UINT32 addr, UINT8 val, struct MemoryWriteByte *pMemWrite)

{

	if (addr >= 0x5000)

		return;

	MHVectorArea[addr - 0x4000] = val;

}



UINT8 TolliansWebVectorRead(UINT32 dwAddr, struct MemoryReadByte *pMemRead)

{

	if (dwAddr < 0x8000)

		return(MHVectorArea[dwAddr - 0x4000]);

	else

		return(0xff);

}



UINT8 TolliansWebInputs(UINT32 dwAddr, struct MemoryReadByte *pMemRead)

{

	if (VectorHalt(0))

		return(sActiveCpu[0].bMemBase[dwAddr] | 0x01);

	else

		return(sActiveCpu[0].bMemBase[dwAddr] & 0xfe);

}



void TolliansWebBankSwitch(UINT32 dwAddr, UINT8 data, struct MemoryWriteByte *pMemWrite)

{

	if (0x10b4 == dwAddr)

	{

		BankSwitch(bOldMHBankId, data);



		// Copy program RAM region



		MemoryCopy(sActiveCpu[data].bMemBase + 0x800,

					  sActiveCpu[bOldMHBankId].bMemBase + 0x800,

					  0x200);



		// Copy zero page and stack page to our target



		MemoryCopy(sActiveCpu[data].bMemBase,

					  sActiveCpu[bOldMHBankId].bMemBase,

					  0x200);



		bOldMHBankId = data;

		return;

	}



	if (0x10b8 == dwAddr)

		bMHPlayer1 = data;

}



void TolliansWebColorWrite(UINT32 dwAddr, UINT8 data, struct MemoryWriteByte *pMemWrite)

{

	bColorPalette[(dwAddr & 0x1f) >> 1] = (data & 0x0f);

	return;

}



