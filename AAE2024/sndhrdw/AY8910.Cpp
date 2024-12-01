//==========================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME 
// code, 0.29 through .90 mixed with code of my own. This emulator was 
// created solely for my amusement and learning and is provided only 
// as an archival experience. 
// 
// All MAME code used and abused in this emulator remains the copyright 
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
// 
// THE CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.  
//==========================================================================

//Note to me: Mix the AY Channels to 16 bit so we don't have to support multiple streams per device emulated. 

#include "sndhrdw/ay8910.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "samples.h"

static struct z80PortWrite fakezpw;
static struct AY8910interface* ayintf;

#define MAX_OUTPUT 0x7fff
#define STEP 0x8000

unsigned char* buf[MAX_8910][3];
static int numchips;
static int updpos[MAX_8910];
static int updlast[MAX_8910];
static int ayvol[MAX_8910];
static int ayvolshift[MAX_8910];
static int aysamples;
static int sample_pos;

struct AY8910
{
	int Channel;
	int SampleRate;
	int (*PortAread)(void);
	int (*PortBread)(void);
	void (*PortAwrite)(int offset, int data);
	void (*PortBwrite)(int offset, int data);
	int register_latch;
	unsigned char Regs[16];
	unsigned int UpdateStep;
	int PeriodA, PeriodB, PeriodC, PeriodN, PeriodE;
	int CountA, CountB, CountC, CountN, CountE;
	unsigned int VolA, VolB, VolC, VolE;
	unsigned char EnvelopeA, EnvelopeB, EnvelopeC;
	unsigned char OutputA, OutputB, OutputC, OutputN;
	signed char CountEnv;
	unsigned char Hold, Alternate, Attack, Holding;
	int RNG;
	unsigned int VolTable[32];
};

/* register id's */
#define AY_AFINE	(0)
#define AY_ACOARSE	(1)
#define AY_BFINE	(2)
#define AY_BCOARSE	(3)
#define AY_CFINE	(4)
#define AY_CCOARSE	(5)
#define AY_NOISEPER	(6)
#define AY_ENABLE	(7)
#define AY_AVOL		(8)
#define AY_BVOL		(9)
#define AY_CVOL		(10)
#define AY_EFINE	(11)
#define AY_ECOARSE	(12)
#define AY_ESHAPE	(13)

#define AY_PORTA	(14)
#define AY_PORTB	(15)

static struct AY8910 AYPSG[MAX_8910];		/* array of PSG's */

void _AYWriteReg(int n, int r, int v)
{
	struct AY8910* PSG = &AYPSG[n];
	int old;

	PSG->Regs[r] = v;

	/* A note about the period of tones, noise and envelope: for speed reasons,*/
	/* we count down from the period to 0, but careful studies of the chip     */
	/* output prove that it instead counts up from 0 until the counter becomes */
	/* greater or equal to the period. This is an important difference when the*/
	/* program is rapidly changing the period to modulate the sound.           */
	/* To compensate for the difference, when the period is changed we adjust  */
	/* our internal counter.                                                   */
	/* Also, note that period = 0 is the same as period = 1. This is mentioned */
	/* in the YM2203 data sheets. However, this does NOT apply to the Envelope */
	/* period. In that case, period = 0 is half as period = 1. */
	switch (r)
	{
	case AY_AFINE:
	case AY_ACOARSE:
		PSG->Regs[AY_ACOARSE] &= 0x0f;
		old = PSG->PeriodA;
		PSG->PeriodA = (PSG->Regs[AY_AFINE] + 256 * PSG->Regs[AY_ACOARSE]) * PSG->UpdateStep;
		if (PSG->PeriodA == 0) PSG->PeriodA = PSG->UpdateStep;
		PSG->CountA += PSG->PeriodA - old;
		if (PSG->CountA <= 0) PSG->CountA = 1;
		break;
	case AY_BFINE:
	case AY_BCOARSE:
		PSG->Regs[AY_BCOARSE] &= 0x0f;
		old = PSG->PeriodB;
		PSG->PeriodB = (PSG->Regs[AY_BFINE] + 256 * PSG->Regs[AY_BCOARSE]) * PSG->UpdateStep;
		if (PSG->PeriodB == 0) PSG->PeriodB = PSG->UpdateStep;
		PSG->CountB += PSG->PeriodB - old;
		if (PSG->CountB <= 0) PSG->CountB = 1;
		break;
	case AY_CFINE:
	case AY_CCOARSE:
		PSG->Regs[AY_CCOARSE] &= 0x0f;
		old = PSG->PeriodC;
		PSG->PeriodC = (PSG->Regs[AY_CFINE] + 256 * PSG->Regs[AY_CCOARSE]) * PSG->UpdateStep;
		if (PSG->PeriodC == 0) PSG->PeriodC = PSG->UpdateStep;
		PSG->CountC += PSG->PeriodC - old;
		if (PSG->CountC <= 0) PSG->CountC = 1;
		break;
	case AY_NOISEPER:
		PSG->Regs[AY_NOISEPER] &= 0x1f;
		old = PSG->PeriodN;
		PSG->PeriodN = PSG->Regs[AY_NOISEPER] * PSG->UpdateStep;
		if (PSG->PeriodN == 0) PSG->PeriodN = PSG->UpdateStep;
		PSG->CountN += PSG->PeriodN - old;
		if (PSG->CountN <= 0) PSG->CountN = 1;
		break;
	case AY_AVOL:
		PSG->Regs[AY_AVOL] &= 0x1f;
		PSG->EnvelopeA = PSG->Regs[AY_AVOL] & 0x10;
		PSG->VolA = PSG->EnvelopeA ? PSG->VolE : PSG->VolTable[PSG->Regs[AY_AVOL] ? PSG->Regs[AY_AVOL] * 2 + 1 : 0];
		break;
	case AY_BVOL:
		PSG->Regs[AY_BVOL] &= 0x1f;
		PSG->EnvelopeB = PSG->Regs[AY_BVOL] & 0x10;
		PSG->VolB = PSG->EnvelopeB ? PSG->VolE : PSG->VolTable[PSG->Regs[AY_BVOL] ? PSG->Regs[AY_BVOL] * 2 + 1 : 0];
		break;
	case AY_CVOL:
		PSG->Regs[AY_CVOL] &= 0x1f;
		PSG->EnvelopeC = PSG->Regs[AY_CVOL] & 0x10;
		PSG->VolC = PSG->EnvelopeC ? PSG->VolE : PSG->VolTable[PSG->Regs[AY_CVOL] ? PSG->Regs[AY_CVOL] * 2 + 1 : 0];
		break;
	case AY_EFINE:
	case AY_ECOARSE:
		old = PSG->PeriodE;
		PSG->PeriodE = ((PSG->Regs[AY_EFINE] + 256 * PSG->Regs[AY_ECOARSE])) * PSG->UpdateStep;
		if (PSG->PeriodE == 0) PSG->PeriodE = PSG->UpdateStep / 2;
		PSG->CountE += PSG->PeriodE - old;
		if (PSG->CountE <= 0) PSG->CountE = 1;
		break;
	case AY_ESHAPE:
		/* envelope shapes:
		C AtAlH
		0 0 x x  \___

		0 1 x x  /___

		1 0 0 0  \\\\

		1 0 0 1  \___

		1 0 1 0  \/\/
				  ___
		1 0 1 1  \

		1 1 0 0  ////
				  ___
		1 1 0 1  /

		1 1 1 0  /\/\

		1 1 1 1  /___

		The envelope counter on the AY-3-8910 has 16 steps. On the YM2149 it
		has twice the steps, happening twice as fast. Since the end result is
		just a smoother curve, we always use the YM2149 behaviour.
		*/
		PSG->Regs[AY_ESHAPE] &= 0x0f;
		PSG->Attack = (PSG->Regs[AY_ESHAPE] & 0x04) ? 0x1f : 0x00;
		if ((PSG->Regs[AY_ESHAPE] & 0x08) == 0)
		{
			/* if Continue = 0, map the shape to the equivalent one which has Continue = 1 */
			PSG->Hold = 1;
			PSG->Alternate = PSG->Attack;
		}
		else
		{
			PSG->Hold = PSG->Regs[AY_ESHAPE] & 0x01;
			PSG->Alternate = PSG->Regs[AY_ESHAPE] & 0x02;
		}
		PSG->CountE = PSG->PeriodE;
		PSG->CountEnv = 0x1f;
		PSG->Holding = 0;
		PSG->VolE = PSG->VolTable[PSG->CountEnv ^ PSG->Attack];
		if (PSG->EnvelopeA) PSG->VolA = PSG->VolE;
		if (PSG->EnvelopeB) PSG->VolB = PSG->VolE;
		if (PSG->EnvelopeC) PSG->VolC = PSG->VolE;
		break;
	case AY_PORTA:
		if (PSG->PortAwrite)
			(*PSG->PortAwrite)(0, v);
		break;
	case AY_PORTB:
		if ((PSG->Regs[AY_ENABLE] & 0x80) == 0)
			if (PSG->PortBwrite)
				(*PSG->PortBwrite)(0, v);
		break;
	}
}

/* write a register on AY8910 chip number 'n' */
void AYWriteReg(int chip, int r, int v)
{
	struct AY8910* PSG = &AYPSG[chip];

	if (r > 15) return;
	if (r < 14)
	{
		if (r == AY_ESHAPE || PSG->Regs[r] != v)
		{
			AY8910partupdate(chip);
		}
	}

	_AYWriteReg(chip, r, v);
}

unsigned char AYReadReg(int n, int r)
{
	struct AY8910* PSG = &AYPSG[n];

	if (r > 15) return 0;

	switch (r)
	{
	case AY_PORTA:
		if (PSG->PortAread)
			PSG->Regs[AY_PORTA] = (*PSG->PortAread)();
		break;
	case AY_PORTB:
		if (PSG->PortBread)
			PSG->Regs[AY_PORTB] = (*PSG->PortBread)();
		break;
	}
	return PSG->Regs[r];
}

void AY8910Write(int chip, int a, int data)
{
	struct AY8910* PSG = &AYPSG[chip];

	if (a & 1)
	{	/* Data port */
		AYWriteReg(chip, PSG->register_latch, data);
	}
	else
	{	/* Register port */
		PSG->register_latch = data & 0x0f;
	}
}

int AY8910Read(int chip)
{
	struct AY8910* PSG = &AYPSG[chip];

	return AYReadReg(chip, PSG->register_latch);
}

/* AY8910 interface */
UINT16 AY8910_read_port_0_r(UINT16 offset, struct z80PortRead* zpr) { return AY8910Read(0); }
UINT16 AY8910_read_port_1_r(UINT16 offset, struct z80PortRead* zpr) { return AY8910Read(1); }
UINT16 AY8910_read_port_2_r(UINT16 offset, struct z80PortRead* zpr) { return AY8910Read(2); }
UINT16 AY8910_read_port_3_r(UINT16 offset, struct z80PortRead* zpr) { return AY8910Read(3); }
UINT16 AY8910_read_port_4_r(UINT16 offset, struct z80PortRead* zpr) { return AY8910Read(4); }

void AY8910_control_port_0_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw) { AY8910Write(0, 0, value); }
void AY8910_control_port_1_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw) { AY8910Write(1, 0, value); }
void AY8910_control_port_2_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw) { AY8910Write(2, 0, value); }
void AY8910_control_port_3_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw) { AY8910Write(3, 0, value); }
void AY8910_control_port_4_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw) { AY8910Write(4, 0, value); }

void AY8910_write_port_0_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw) { AY8910Write(0, 1, value); }
void AY8910_write_port_1_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw) { AY8910Write(1, 1, value); }
void AY8910_write_port_2_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw) { AY8910Write(2, 1, value); }
void AY8910_write_port_3_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw) { AY8910Write(3, 1, value); }
void AY8910_write_port_4_w(UINT16 offset, UINT8 value, struct z80PortWrite* zpw) { AY8910Write(4, 1, value); }

static void AY8910Update(int chip, pBYTE* buffer, int length)
{
	struct AY8910* PSG = &AYPSG[chip];
	pBYTE buf1;
	pBYTE buf2;
	pBYTE buf3;
	int outn;
	int mixshift = 23 + ayvolshift[chip];
	int prelen = length * STEP;

	if (!length)
		return;

	buf1 = buffer[0] + updpos[chip];
	buf2 = buffer[1] + updpos[chip];
	buf3 = buffer[2] + updpos[chip];

	/* The 8910 has three outputs, each output is the mix of one of the three */
	/* tone generators and of the (single) noise generator. The two are mixed */
	/* BEFORE going into the DAC. The formula to mix each channel is: */
	/* (ToneOn | ToneDisable) & (NoiseOn | NoiseDisable). */
	/* Note that this means that if both tone and noise are disabled, the output */
	/* is 1, not 0, and can be modulated changing the volume. */

	/* If the channels are disabled, set their output to 1, and increase the */
	/* counter, if necessary, so they will not be inverted during this update. */
	/* Setting the output to 1 is necessary because a disabled channel is locked */
	/* into the ON state (see above); and it has no effect if the volume is 0. */
	/* If the volume is 0, increase the counter, but don't touch the output. */
	if (PSG->Regs[AY_ENABLE] & 0x01)
	{
		if (PSG->CountA <= prelen)
			PSG->CountA += prelen;
		PSG->OutputA = 1;
	}
	else if (PSG->Regs[AY_AVOL] == 0)
	{
		/* note that I do count += length, NOT count = length + 1. You might think */
		/* it's the same since the volume is 0, but doing the latter could cause */
		/* interferencies when the program is rapidly modulating the volume. */
		if (PSG->CountA <= prelen)
			PSG->CountA += prelen;
	}
	if (PSG->Regs[AY_ENABLE] & 0x02)
	{
		if (PSG->CountB <= prelen)
			PSG->CountB += prelen;
		PSG->OutputB = 1;
	}
	else if (PSG->Regs[AY_BVOL] == 0)
	{
		if (PSG->CountB <= prelen)
			PSG->CountB += prelen;
	}
	if (PSG->Regs[AY_ENABLE] & 0x04)
	{
		if (PSG->CountC <= prelen)
			PSG->CountC += prelen;
		PSG->OutputC = 1;
	}
	else if (PSG->Regs[AY_CVOL] == 0)
	{
		if (PSG->CountC <= prelen) PSG->CountC += prelen;
	}

	/* for the noise channel we must not touch OutputN - it's also not necessary */
	/* since we use outn. */
	if ((PSG->Regs[AY_ENABLE] & 0x38) == 0x38)	/* all off */
		if (PSG->CountN <= prelen)
			PSG->CountN += prelen;

	outn = (PSG->OutputN | PSG->Regs[AY_ENABLE]);

	/* buffering loop */
	while (length)
	{
		int vola, volb, volc;
		int left;

		/* vola, volb and volc keep track of how long each square wave stays */
		/* in the 1 position during the sample period. */
		vola = volb = volc = 0;

		left = STEP;
		do
		{
			int nextevent;
			if (PSG->CountN < left)
				nextevent = PSG->CountN;
			else
				nextevent = left;

			if (outn & 0x08)
			{
				if (PSG->OutputA)
					vola += PSG->CountA;
				PSG->CountA -= nextevent;

				while (PSG->CountA <= 0)
				{
					PSG->CountA += PSG->PeriodA;
					if (PSG->CountA > 0)
					{
						PSG->OutputA ^= 1;
						vola += PSG->OutputA * PSG->PeriodA;
						break;
					}
					PSG->CountA += PSG->PeriodA;
					vola += PSG->PeriodA;
				}

				if (PSG->OutputA)
					vola -= PSG->CountA;
			}
			else
			{
				PSG->CountA -= nextevent;
				while (PSG->CountA <= 0)
				{
					PSG->CountA += PSG->PeriodA;
					if (PSG->CountA > 0)
					{
						PSG->OutputA ^= 1;
						break;
					}
					PSG->CountA += PSG->PeriodA;
				}
			}

			if (outn & 0x10)
			{
				if (PSG->OutputB)
					volb += PSG->CountB;
				PSG->CountB -= nextevent;
				while (PSG->CountB <= 0)
				{
					PSG->CountB += PSG->PeriodB;
					if (PSG->CountB > 0)
					{
						PSG->OutputB ^= 1;
						volb += PSG->OutputB * PSG->PeriodB;
						break;
					}
					PSG->CountB += PSG->PeriodB;
					volb += PSG->PeriodB;
				}
				if (PSG->OutputB)
					volb -= PSG->CountB;
			}
			else
			{
				PSG->CountB -= nextevent;
				while (PSG->CountB <= 0)
				{
					PSG->CountB += PSG->PeriodB;
					if (PSG->CountB > 0)
					{
						PSG->OutputB ^= 1;
						break;
					}
					PSG->CountB += PSG->PeriodB;
				}
			}

			if (outn & 0x20)
			{
				if (PSG->OutputC) volc += PSG->CountC;
				PSG->CountC -= nextevent;
				while (PSG->CountC <= 0)
				{
					PSG->CountC += PSG->PeriodC;
					if (PSG->CountC > 0)
					{
						PSG->OutputC ^= 1;
						volc += PSG->OutputC * PSG->PeriodC;
						break;
					}
					PSG->CountC += PSG->PeriodC;
					volc += PSG->PeriodC;
				}
				if (PSG->OutputC)
					volc -= PSG->CountC;
			}
			else
			{
				PSG->CountC -= nextevent;
				while (PSG->CountC <= 0)
				{
					PSG->CountC += PSG->PeriodC;
					if (PSG->CountC > 0)
					{
						PSG->OutputC ^= 1;
						break;
					}
					PSG->CountC += PSG->PeriodC;
				}
			}

			PSG->CountN -= nextevent;
			if (PSG->CountN <= 0)
			{
				/* Is noise output going to change? */
				if ((PSG->RNG + 1) & 2)	/* (bit0^bit1)? */
				{
					PSG->OutputN = ~PSG->OutputN;
					outn = (PSG->OutputN | PSG->Regs[AY_ENABLE]);
				}

				/* The Random Number Generator of the 8910 is a 17-bit shift */
				/* register. The input to the shift register is bit0 XOR bit2 */
				/* (bit0 is the output). */

				/* The following is a fast way to compute bit 17 = bit0^bit2. */
				/* Instead of doing all the logic operations, we only check */
				/* bit 0, relying on the fact that after two shifts of the */
				/* register, what now is bit 2 will become bit 0, and will */
				/* invert, if necessary, bit 16, which previously was bit 18. */
				if (PSG->RNG & 1)
					PSG->RNG ^= 0x28000;
				PSG->RNG >>= 1;
				PSG->CountN += PSG->PeriodN;
			}

			left -= nextevent;
		} while (left > 0);

		/* update envelope */
		if (PSG->Holding == 0)
		{
			PSG->CountE -= STEP;
			if (PSG->CountE <= 0)
			{
				do
				{
					PSG->CountEnv--;
					PSG->CountE += PSG->PeriodE;
				} while (PSG->CountE <= 0);
				/* check envelope current position */
				if (PSG->CountEnv < 0)
				{
					if (PSG->Hold)
					{
						if (PSG->Alternate)
							PSG->Attack ^= 0x1f;
						PSG->Holding = 1;
						PSG->CountEnv = 0;
					}
					else
					{
						/* if CountEnv has looped an odd number of times (usually 1), */
						/* invert the output. */
						if (PSG->Alternate && (PSG->CountEnv & 0x20))
							PSG->Attack ^= 0x1f;

						PSG->CountEnv &= 0x1f;
					}
				}

				PSG->VolE = PSG->VolTable[PSG->CountEnv ^ PSG->Attack];
				/* reload volume */
				if (PSG->EnvelopeA)
					PSG->VolA = PSG->VolE;
				if (PSG->EnvelopeB)
					PSG->VolB = PSG->VolE;
				if (PSG->EnvelopeC)
					PSG->VolC = PSG->VolE;
			}
		}

		*(buf1++) = (unsigned char)((vola * PSG->VolA) >> mixshift);
		*(buf2++) = (unsigned char)((volb * PSG->VolB) >> mixshift);
		*(buf3++) = (unsigned char)((volc * PSG->VolC) >> mixshift);

		length--;
	}
}

void AY8910_set_clock(int chip, int clock)
{
	struct AY8910* PSG = &AYPSG[chip];

	/* the step clock for the tone and noise generators is the chip clock    */
	/* divided by 8; for the envelope generator of the AY-3-8910, it is half */
	/* that much (clock/16), but the envelope of the YM2149 goes twice as    */
	/* fast, therefore again clock/8.                                        */
	/* Here we calculate the number of steps which happen during one sample  */
	/* at the given sample rate. No. of events = sample rate / (clock/8).    */
	/* STEP is a multiplier used to turn the fraction into a fixed point     */
	/* number.                                                               */
	PSG->UpdateStep = ((double)STEP * PSG->SampleRate * 8) / clock;
}

static void build_mixer_table(int chip)
{
	struct AY8910* PSG = &AYPSG[chip];
	int i;
	double out;

	/* calculate the volume->voltage conversion table */
	/* The AY-3-8910 has 16 levels, in a logarithmic scale (3dB per step) */
	/* The YM2149 still has 16 levels for the tone generators, but 32 for */
	/* the envelope generator (1.5dB per step). */
	out = MAX_OUTPUT;
	for (i = 31; i > 0; i--)
	{
		PSG->VolTable[i] = out + 0.5; /* round to nearest */
		out /= (1.188502227);  /* = 10 ^ (1.5/20) = 1.5dB */
  //      out /= (1.14);
	}
	PSG->VolTable[0] = 0;
}

void AY8910_reset(int chip)
{
	int i;
	struct AY8910* PSG = &AYPSG[chip];

	PSG->register_latch = 0;
	PSG->RNG = 1;
	PSG->OutputA = 0;
	PSG->OutputB = 0;
	PSG->OutputC = 0;
	PSG->OutputN = 0xff;
	for (i = 0; i < AY_PORTA; i++)
		_AYWriteReg(chip, i, 0);	/* AYWriteReg() uses the timer system; we cannot */
								/* call it at this time because the timer system */
								/* has not been initialized. */
}

static int AY8910_init(int chip,
	int clock, int volume, int volshift, int sample_rate,
	int (*portAread)(void), int (*portBread)(void),
	void (*portAwrite)(int offset, int data), void (*portBwrite)(int offset, int data))
{
	struct AY8910* PSG = &AYPSG[chip];

	memset(PSG, 0, sizeof(struct AY8910));
	PSG->SampleRate = sample_rate;
	PSG->PortAread = portAread;
	PSG->PortBread = portBread;
	PSG->PortAwrite = portAwrite;
	PSG->PortBwrite = portBwrite;
	ayvol[chip] = volume;
	ayvolshift[chip] = volshift;

	wrlog("AY8910 INIT ----------->");

	AY8910_set_clock(chip, clock);
	AY8910_reset(chip);

	return 0;
}

void AY8910_sh_update(void)
{
	int i, ch;

	//if(soundcard==0)     return;
	//wrlog("AY8910 Update ----------->");

	for (i = 0; i < numchips; i++)
	{
		AY8910Update(i, buf[i], aysamples - updpos[i]);
		for (ch = 0; ch < 3; ch++)
			//  playstreamedsample((i*3)+ch,buf[i][ch],aysamples,ayvol[i]);
			aae_play_streamed_sample((i * 3) + ch, buf[i][ch], aysamples, 44100, ayvol[i]);

		updpos[i] = 0;
		updlast[i] = 0;
	}
}

void AY8910partupdate(int chip)
{
	int work, updlen;

	//if(soundcard==0)     return;

	//get ticks this frame
	work = cpu_scale_by_cycles(aysamples);// , ayintf->baseclock); // get current position based on the timer

 //update since last
	updlen = work - updlast[chip];
	if (updlen > 32)
	{
		AY8910Update(chip, buf[chip], updlen);
		//update position
		updpos[chip] += updlen;
		//store this tick
		updlast[chip] = work;
	}
}

int AY8910_sh_start(struct AY8910interface* intf)
{
	int chip;
	numchips = intf->num;
	aysamples = 44100 / driver[gamenum].fps;
	int emulation_rate = aysamples * driver[gamenum].fps;
	ayintf = intf;

	for (chip = 0; chip < intf->num; chip++)
	{
		buf[chip][0] = (unsigned char*)malloc(aysamples * sizeof(unsigned char));
		buf[chip][1] = (unsigned char*)malloc(aysamples * sizeof(unsigned char));
		buf[chip][2] = (unsigned char*)malloc(aysamples * sizeof(unsigned char));

		updpos[chip] = 0;
		updlast[chip] = 0;

		if (AY8910_init(chip, intf->baseclock,
			intf->vol[chip], intf->volshift[chip],
			44100,
			intf->portAread[chip], intf->portBread[chip],
			intf->portAwrite[chip], intf->portBwrite[chip]) != 0)
			return 1;
		build_mixer_table(chip);
	}

	aae_stream_init(0, emulation_rate, aysamples, 128);
	aae_stream_init(1, emulation_rate, aysamples, 128);
	aae_stream_init(2, emulation_rate, aysamples, 128);
	aae_stream_init(3, emulation_rate, aysamples, 128);
	aae_stream_init(4, emulation_rate, aysamples, 128);
	aae_stream_init(5, emulation_rate, aysamples, 128);

	return 0;
}

void AY8910clear(void)
{
	int chip;

	aae_stop_stream(0);
	aae_stop_stream(1);
	aae_stop_stream(2);
	aae_stop_stream(3);
	aae_stop_stream(4);
	aae_stop_stream(5);

	for (chip = 0; chip < numchips; chip++)
	{
		free(buf[chip][0]);
		free(buf[chip][1]);
		free(buf[chip][2]);
	}
}

void memAY8910_control_port_0_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb)
{
	AY8910Write(0, 0, value);
}

void memAY8910_write_port_0_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb)
{
	AY8910Write(0, 1, value);
}

void memAY8910_control_port_1_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb)
{
	AY8910Write(1, 0, value);
}

void memAY8910_write_port_1_w(UINT32 offset, UINT8 value, struct MemoryWriteByte* mwb)
{
	AY8910Write(1, 1, value);
}