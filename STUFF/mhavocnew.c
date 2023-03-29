

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
		MemoryCopy(sActiveCpu[data].bMemBase + 0x800,sActiveCpu[bOldMHBankId].bMemBase + 0x800,0x200);
		// Copy zero page and stack page to our target
		MemoryCopy(sActiveCpu[data].bMemBase,sActiveCpu[bOldMHBankId].bMemBase,0x200);
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
