/* CPU Benchmarking program */
/* Copyright 1997, 1998, Neil Bradley, All rights reserved */

/* This code requires the Atari Asteroids ROMs. It is also assumed that the
 * m6502zp.asm CPU emulator has been assembled.
 */

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <process.h>
#include <time.h>
#include <conio.h>
#include <memory.h>
#include "m6502.h"

void BWVectorGeneratorInternal(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite);
void AsteroidsSwapRam(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite);

struct MemoryWriteByte AsteroidWrite[] =
{
	{0x3000, 0x3000,	BWVectorGeneratorInternal},
	{0x3200, 0x3200,  AsteroidsSwapRam},
	{(UINT32) -1,	(UINT32) -1,		NULL}
};

struct MemoryReadByte AsteroidRead[] =
{
	{(UINT32) -1,	(UINT32) -1,		NULL}
};

char *gameImage = NULL;

struct roms
{
	char *filename;
	UINT16 loadAddr;
	UINT16 romSize;
};

struct roms images[] =
{
	{"035127.02", 0x5000, 0x800},
	{"035145.02", 0x6800, 0x800},
	{"035144.02", 0x7000, 0x800},
	{"035143.02", 0x7800, 0x800},
	{"035143.02", 0xf800, 0x800},
	{NULL, 0, 0}
};

UINT32 frames = 0;
UINT8 swapval = 0;
UINT8 lastswap = 0;
UINT8 buffer[0x100];

void BWVectorGeneratorInternal(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)
{
	++frames;
	/* Vector generator code goes here */
}

void AsteroidsSwapRam(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)
{
	if ((swapval & 4) != lastswap)
	{
		lastswap = swapval & 0x4;
		memcpy(buffer, gameImage + 0x200, 0x100);
		memcpy(gameImage + 0x200, gameImage + 0x300, 0x100);
		memcpy(gameImage + 0x300, buffer, 0x100);
	}
}

void ExecByCycleCount()
{
	UINT32 hit;
	UINT32 lastSec;
	UINT32 prior;
	UINT32 diff;
	UINT32 m6502clockticks;
	UINT32 m6502NmiTicks = 0;
	UINT32 dwElapsedTicks = 0;

	/* Second align our test */

	lastSec = time(0);
	while ((UINT32) time(0) == lastSec);
	lastSec = time(0);
	hit = 0;
	prior = 0;

	m6502zpreset();
	m6502zpexec(100);
	dwElapsedTicks = m6502zpGetElapsedTicks(0xff);

	if (dwElapsedTicks > 7)
		printf("Regular instruction counting enabled\n");
	else
		printf("** Painfully slow! Single stepping enabled! **\n");
	m6502zpreset();

	while (hit == 0)
	{
		if (lastSec != (UINT32) time(0))
		{
			diff = (m6502clockticks - prior) / 1500000;
			printf("%ld Clockticks, %ld frames, %ld Times original speed\n", m6502clockticks - prior, frames, diff);
			frames = 0;
			prior = m6502clockticks;
			lastSec = time(0);
			if (kbhit())
			{
				getch();
				hit = 1;
			}
		}

		/* 4500 Cycles per NMI (~3 milliseconds) */

		m6502zpexec(4500);
		dwElapsedTicks = m6502zpGetElapsedTicks(0xff);

		if (dwElapsedTicks > 7)	/* Normal m6502 core */
		{
			m6502clockticks += dwElapsedTicks;
			m6502zpnmi();
		}
		else
		{
			while (m6502NmiTicks < 4500)
			{
				m6502zpexec(0);	/* Doesn't matter! We're single stepping! */
				m6502clockticks += dwElapsedTicks;
				m6502NmiTicks += dwElapsedTicks;
			}

			m6502zpnmi();
			m6502NmiTicks -= 4500;
		}
	}
}

void main()
{
	FILE *fp;
	UINT32 loop;
	UINT32 cpuInfo;
	CONTEXTM6502 *psCpu1;

	printf("Creating 64K space...\n");

	gameImage = malloc(65536);

	if (gameImage == NULL)
	{
		printf("Can't allocate 64K of space\n");
		exit(1);
	}

	for (loop = 0; loop < 0x10000; loop++)
		gameImage[loop] = (char) 0xff;

	loop = 0;

	while (images[loop].filename)
	{
		fp = fopen(images[loop].filename, "rb");

		if (fp == NULL)
		{
			printf("Can't open %s\n", images[loop].filename);
			exit(1);
		}

		printf("Reading %s...\n", images[loop].filename);

		fread(gameImage + images[loop].loadAddr, 1, images[loop].romSize, fp);
		fclose(fp);
		++loop;
	}

	/* Now that everything's ready to go, let's go ahead and fire up the
    * CPU emulator
    */

	/* Clear our context */

	psCpu1 = malloc(m6502zpGetContextSize());
	memset(psCpu1, 0, m6502zpGetContextSize());

	/* First set the base address of the 64K image */

	psCpu1->m6502Base = gameImage;

	/* Now set up the read/write handlers for the emulation */

	psCpu1->m6502MemoryRead = AsteroidRead;
	psCpu1->m6502MemoryWrite = AsteroidWrite;

	/* Set the context in the 6502 emulator core */

	m6502zpSetContext(psCpu1);		

	/* Now reset the processor to fetch the start vector */

	m6502zpreset();

	/* Set up some defaults so that the Asteroids code actually runs the game
    * and not just diag mode.
    */

	gameImage[0x2001] = (char) 0xff;
	gameImage[0x2007] = (char) 0x0;
	gameImage[0x2002] = (char) 0x0;
	gameImage[0x2c00] = (char) 0xff;
	gameImage[0x2000] = (char) 0xff;
	gameImage[0x2800] = (char) 0x0;
	gameImage[0x2801] = (char) 0x0;
	gameImage[0x2802] = (char) 0x0;
	gameImage[0x2803] = (char) 0x0;

	ExecByCycleCount();

	free(gameImage);	
}
