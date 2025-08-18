// -----------------------------------------------------------------------------
// Legacy MAME-Derived Module
// This file contains code originally developed as part of the M.A.M.E. Project.
// Portions of this file remain under the copyright of the original MAME authors
// and contributors. It has since been adapted and merged into the AAE (Another
// Arcade Emulator) project.
//
// Integration:
//   This module is now part of the **AAE (Another Arcade Emulator)** codebase
//   and is integrated with its rendering, input, and emulation subsystems.
//
// Licensing Notice:
//   - Original portions of this code remain  the M.A.M.E.(TM) Project and its
//     respective contributors under their original terms of distribution.
//   - Redistribution must preserve both this notice and the original MAME
//     copyright acknowledgement.
//
// License:
//   This program is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// Original Copyright:
//   This file is originally part of and copyright the M.A.M.E.(TM) Project.
//   For more information about MAME licensing, see the original MAME source
//   distribution and its associated license files.
//
// -----------------------------------------------------------------------------
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
//
// Notes (2025 rewrite):
// - Public API unchanged (see ay8910.h).
// - Internal mixer now writes a single int16 mono stream per chip.
// - Proper 16-bit signed output (no 8-bit << 8 artifacts).
// - Per-chip master volume honored via interface vol/volshift or
//   via AY8910_set_volume(chip, ALL_8910_CHANNELS, volume).
// - Lightweight DC blocker (can be disabled via macro).
//
//

#include "ay8910.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>    // lround

struct ay_ym_param {
	int r_up;
	int r_down;
	int res_count;
	int res[32]; // only first res_count used
};

// AY-3-8910 (tone), RL = 2000 (Westcott)
static const ay_ym_param kAY8910_Param = {
	800000, 8000000, 16,
	{ 15950, 15350, 15090, 14760, 14275, 13620, 12890, 11370,
	  10600,  8590,  7190,  5985,  4820,  3945,  3017,  2345 }
};

// YM2149 (tone) r_up = 630801 OHM, r_down = 801801 OHM, 16 steps
static const ay_ym_param kYM2149_Param = {
	630, 801, 16,
	{ 73770, 37586, 27458, 21451, 15864, 12371,  8922,  6796,
	   4763,  3521,  2403,  1737,  1123,   762,   438,   251 }
};

// YM2149 (envelope) r_up = 630 OHM, r_down = 801OHM, 32 steps
static const ay_ym_param kYM2149_ParamEnv = {
	630, 801, 32,
	{ 103350,  73770,  52657,  37586,  32125,  27458,  24269,  21451,
	   18447,  15864,  14009,  12371,  10506,   8922,   7787,   6796,
		5689,   4763,   4095,   3521,   2909,   2403,   2043,   1737,
		1397,   1123,    925,    762,    578,    438,    332,    251 }
};

// ----- Build options -------------------------------------------------------

#define AY8910_ENABLE_DC_BLOCK 1   // DC blocker recommended
#define AY_DC_R_FP            4096 // fixed-point base (12-bit frac)
#define AY_DC_R_COEF          4076 // ~0.995 * 4096

#define AY_AVOID_CLIP_DIV3    0    // keep loudness; we clamp to int16

// ----- Legacy compatibility -----------------------------------------------

#ifndef MAX_OUTPUT
#define MAX_OUTPUT 0x7fff
#endif

#ifndef STEP
#define STEP 0x8000 // 32768 fixed-point per-sample substep
#endif

static struct z80PortWrite fakezpw;
static struct AY8910interface* ayintf = nullptr;

static int numchips = 0;
static int aysamples = 0;

// One stream per chip: single mixed buffer (signed 16-bit)
static int16_t* mixbuf[MAX_8910] = { nullptr };

// Partial update bookkeeping
static int updpos[MAX_8910] = { 0 };
static int updlast[MAX_8910] = { 0 };

// Per-chip volume scaling (master gain)
static int ayvol[MAX_8910] = { 0 }; // numerator
static int ayvolshift[MAX_8910] = { 0 }; // right shift

// ----- PSG state -----------------------------------------------------------

struct AY8910
{
	int     Channel;       // (unused)
	int     SampleRate;

	int   (*PortAread)(void) = nullptr;
	int   (*PortBread)(void) = nullptr;
	void  (*PortAwrite)(int offset, int data) = nullptr;
	void  (*PortBwrite)(int offset, int data) = nullptr;

	int           register_latch = 0;
	unsigned char Regs[16] = { 0 };

	unsigned int UpdateStep = 0;

	int PeriodA = 0, PeriodB = 0, PeriodC = 0, PeriodN = 0, PeriodE = 0;
	int CountA = 0, CountB = 0, CountC = 0, CountN = 0, CountE = 0;

	unsigned int VolA = 0, VolB = 0, VolC = 0, VolE = 0;
	unsigned char EnvelopeA = 0, EnvelopeB = 0, EnvelopeC = 0;
	unsigned char OutputA = 0, OutputB = 0, OutputC = 0, OutputN = 0;
	signed   char CountEnv = 0;

	unsigned char Hold = 0, Alternate = 0, Attack = 0, Holding = 0;

	int RNG = 1;

	// Tone table (legacy 32-layout; odd indices 1..31 used for tone 16 steps)
	unsigned int VolTable[32] = { 0 };
	// YM2149 envelope table (32 steps). Used only if isYM2149 == 1.
	unsigned int VolEnvTable[32] = { 0 };

	// Per-channel pre-mix gain (0..255, 255 = 1.0 when >>8)
	unsigned int ChanGain[3] = { 255, 255, 255 };

	// Always-on one-pole low-pass
	float    lp_y_prev = 0.0f;
	float    lp_alpha = 0.0f;   // dt / (RC + dt), RC = 1/(2*pi*fc)

#if AY8910_ENABLE_DC_BLOCK
	// DC blocker state (per chip)
	int32_t dc_prev_in = 0;
	int32_t dc_prev_out = 0;
#endif

	// Variant: 0 = AY-3-8910, 1 = YM2149 (set at init per chip)
	uint8_t isYM2149 = 0;
};

static AY8910 AYPSG[MAX_8910];

// ----- Register IDs --------------------------------------------------------

#define AY_AFINE   (0)
#define AY_ACOARSE (1)
#define AY_BFINE   (2)
#define AY_BCOARSE (3)
#define AY_CFINE   (4)
#define AY_CCOARSE (5)
#define AY_NOISEPER (6)
#define AY_ENABLE   (7)
#define AY_AVOL     (8)
#define AY_BVOL     (9)
#define AY_CVOL     (10)
#define AY_EFINE    (11)
#define AY_ECOARSE  (12)
#define AY_ESHAPE   (13)
#define AY_PORTA    (14)
#define AY_PORTB    (15)

// ----- Helpers -------------------------------------------------------------

static inline int32_t clamp16(int32_t v)
{
	if (v > 32767) return  32767;
	if (v < -32768) return -32768;
	return v;
}

// Compute resistor-network table (MAME method)
static void build_single_table(double rl_ohms, const ay_ym_param& par,
	double* out /*size >= res_count*/, bool zero_is_off)
{
	double minv = 1e9, maxv = -1e9;

	for (int j = 0; j < par.res_count; j++) {
		// start with pulldown and load
		double rt = 1.0 / par.r_down + 1.0 / rl_ohms;
		double rw = 1.0 / par.res[j];
		rt += 1.0 / par.res[j];

		// AY: level 0 is off (no pull-up). YM: level 0 still conducts.
		if (!zero_is_off || j != 0) {
			rw += 1.0 / par.r_up;
			rt += 1.0 / par.r_up;
		}
		const double v = rw / rt;   // normalized transfer
		out[j] = v;
		if (v < minv) minv = v;
		if (v > maxv) maxv = v;
	}

	// Normalize 0..1 like MAME
	const double range = (maxv > minv) ? (maxv - minv) : 1.0;
	for (int j = 0; j < par.res_count; j++)
		out[j] = (out[j] - minv) / range;
}

// ----- Public API: volume --------------------------------------------------
void AY8910_set_volume(int chip, int channel, int volume)
{
	if (chip < 0 || chip >= numchips) return;

	const int v = std::clamp(volume, 0, 1024); // allow headroom for master
	if (ayvolshift[chip] <= 0) ayvolshift[chip] = 8; // 8-bit fixed shift baseline

	if (channel == ALL_8910_CHANNELS)
	{
		// Per-chip master gain (default unity is 256 >> 8)
		ayvol[chip] = (v == 0 ? 256 : v);
		return;
	}

	if (channel >= 0 && channel < 3)
	{
		AYPSG[chip].ChanGain[channel] = (unsigned int)std::clamp(v, 0, 255);
		return;
	}
}

// ----- Core write ----------------------------------------------------------

static void _AYWriteReg(int n, int r, int v)
{
	AY8910* PSG = &AYPSG[n];
	int old;

	PSG->Regs[r] = (unsigned char)v;

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
		PSG->PeriodA = (PSG->Regs[AY_AFINE] + 256 * PSG->Regs[AY_ACOARSE]) * (int)PSG->UpdateStep;
		if (PSG->PeriodA == 0) PSG->PeriodA = (int)PSG->UpdateStep;
		PSG->CountA += PSG->PeriodA - old;
		if (PSG->CountA <= 0) PSG->CountA = 1;
		break;

	case AY_BFINE:
	case AY_BCOARSE:
		PSG->Regs[AY_BCOARSE] &= 0x0f;
		old = PSG->PeriodB;
		PSG->PeriodB = (PSG->Regs[AY_BFINE] + 256 * PSG->Regs[AY_BCOARSE]) * (int)PSG->UpdateStep;
		if (PSG->PeriodB == 0) PSG->PeriodB = (int)PSG->UpdateStep;
		PSG->CountB += PSG->PeriodB - old;
		if (PSG->CountB <= 0) PSG->CountB = 1;
		break;

	case AY_CFINE:
	case AY_CCOARSE:
		PSG->Regs[AY_CCOARSE] &= 0x0f;
		old = PSG->PeriodC;
		PSG->PeriodC = (PSG->Regs[AY_CFINE] + 256 * PSG->Regs[AY_CCOARSE]) * (int)PSG->UpdateStep;
		if (PSG->PeriodC == 0) PSG->PeriodC = (int)PSG->UpdateStep;
		PSG->CountC += PSG->PeriodC - old;
		if (PSG->CountC <= 0) PSG->CountC = 1;
		break;

	case AY_NOISEPER:
		PSG->Regs[AY_NOISEPER] &= 0x1f;
		old = PSG->PeriodN;
		PSG->PeriodN = PSG->Regs[AY_NOISEPER] * (int)PSG->UpdateStep;
		if (PSG->PeriodN == 0) PSG->PeriodN = (int)PSG->UpdateStep;
		PSG->CountN += PSG->PeriodN - old;
		if (PSG->CountN <= 0) PSG->CountN = 1;
		break;

	case AY_AVOL:
		PSG->Regs[AY_AVOL] &= 0x1f;
		PSG->EnvelopeA = PSG->Regs[AY_AVOL] & 0x10;
		PSG->VolA = PSG->EnvelopeA ? PSG->VolE
			: PSG->VolTable[PSG->Regs[AY_AVOL] ? PSG->Regs[AY_AVOL] * 2 + 1 : 0];
		break;

	case AY_BVOL:
		PSG->Regs[AY_BVOL] &= 0x1f;
		PSG->EnvelopeB = PSG->Regs[AY_BVOL] & 0x10;
		PSG->VolB = PSG->EnvelopeB ? PSG->VolE
			: PSG->VolTable[PSG->Regs[AY_BVOL] ? PSG->Regs[AY_BVOL] * 2 + 1 : 0];
		break;

	case AY_CVOL:
		PSG->Regs[AY_CVOL] &= 0x1f;
		PSG->EnvelopeC = PSG->Regs[AY_CVOL] & 0x10;
		PSG->VolC = PSG->EnvelopeC ? PSG->VolE
			: PSG->VolTable[PSG->Regs[AY_CVOL] ? PSG->Regs[AY_CVOL] * 2 + 1 : 0];
		break;

	case AY_EFINE:
	case AY_ECOARSE:
		old = PSG->PeriodE;
		PSG->PeriodE = ((PSG->Regs[AY_EFINE] + 256 * PSG->Regs[AY_ECOARSE])) * (int)PSG->UpdateStep;
		if (PSG->PeriodE == 0) PSG->PeriodE = (int)PSG->UpdateStep / 2;
		PSG->CountE += PSG->PeriodE - old;
		if (PSG->CountE <= 0) PSG->CountE = 1;
		break;

	case AY_ESHAPE:
	{
		PSG->Regs[AY_ESHAPE] &= 0x0f;
		PSG->Attack = (PSG->Regs[AY_ESHAPE] & 0x04) ? 0x1f : 0x00;
		if ((PSG->Regs[AY_ESHAPE] & 0x08) == 0)
		{
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
		// YM2149 uses a 32-step envelope ladder distinct from its 16-step tone table
		PSG->VolE = PSG->isYM2149 ? PSG->VolEnvTable[PSG->CountEnv ^ PSG->Attack]
			: PSG->VolTable[PSG->CountEnv ^ PSG->Attack];
		if (PSG->EnvelopeA) PSG->VolA = PSG->VolE;
		if (PSG->EnvelopeB) PSG->VolB = PSG->VolE;
		if (PSG->EnvelopeC) PSG->VolC = PSG->VolE;
		break;
	}

	case AY_PORTA:
		if (PSG->PortAwrite) PSG->PortAwrite(0, v);
		break;

	case AY_PORTB:
		if ((PSG->Regs[AY_ENABLE] & 0x80) == 0)
			if (PSG->PortBwrite) PSG->PortBwrite(0, v);
		break;
	}
}

// ----- Public API: read/write/ports ----------------------------

void AYWriteReg(int chip, int r, int v)
{
	AY8910* PSG = &AYPSG[chip];
	if (r > 15) return;

	if (r < 14)
	{
		if (r == AY_ESHAPE || PSG->Regs[r] != (unsigned char)v)
		{
			AY8910partupdate(chip);
		}
	}
	_AYWriteReg(chip, r, v);
}

unsigned char AYReadReg(int n, int r)
{
	AY8910* PSG = &AYPSG[n];
	if (r > 15) return 0;

	switch (r)
	{
	case AY_PORTA:
		if (PSG->PortAread)
			PSG->Regs[AY_PORTA] = (unsigned char)PSG->PortAread();
		break;
	case AY_PORTB:
		if (PSG->PortBread)
			PSG->Regs[AY_PORTB] = (unsigned char)PSG->PortBread();
		break;
	}
	return PSG->Regs[r];
}

void AY8910Write(int chip, int a, int data)
{
	AY8910* PSG = &AYPSG[chip];
	if (a & 1)
		AYWriteReg(chip, PSG->register_latch, data);
	else
		PSG->register_latch = data & 0x0f;
}

int AY8910Read(int chip)
{
	AY8910* PSG = &AYPSG[chip];
	return AYReadReg(chip, PSG->register_latch);
}

// z80 port wrappers
UINT16 AY8910_read_port_0_r(UINT16, struct z80PortRead*) { return (UINT16)AY8910Read(0); }
UINT16 AY8910_read_port_1_r(UINT16, struct z80PortRead*) { return (UINT16)AY8910Read(1); }
UINT16 AY8910_read_port_2_r(UINT16, struct z80PortRead*) { return (UINT16)AY8910Read(2); }
UINT16 AY8910_read_port_3_r(UINT16, struct z80PortRead*) { return (UINT16)AY8910Read(3); }
UINT16 AY8910_read_port_4_r(UINT16, struct z80PortRead*) { return (UINT16)AY8910Read(4); }

void AY8910_control_port_0_w(UINT16, UINT8 value, struct z80PortWrite*) { AY8910Write(0, 0, value); }
void AY8910_control_port_1_w(UINT16, UINT8 value, struct z80PortWrite*) { AY8910Write(1, 0, value); }
void AY8910_control_port_2_w(UINT16, UINT8 value, struct z80PortWrite*) { AY8910Write(2, 0, value); }
void AY8910_control_port_3_w(UINT16, UINT8 value, struct z80PortWrite*) { AY8910Write(3, 0, value); }
void AY8910_control_port_4_w(UINT16, UINT8 value, struct z80PortWrite*) { AY8910Write(4, 0, value); }

void AY8910_write_port_0_w(UINT16, UINT8 value, struct z80PortWrite*) { AY8910Write(0, 1, value); }
void AY8910_write_port_1_w(UINT16, UINT8 value, struct z80PortWrite*) { AY8910Write(1, 1, value); }
void AY8910_write_port_2_w(UINT16, UINT8 value, struct z80PortWrite*) { AY8910Write(2, 1, value); }
void AY8910_write_port_3_w(UINT16, UINT8 value, struct z80PortWrite*) { AY8910Write(3, 1, value); }
void AY8910_write_port_4_w(UINT16, UINT8 value, struct z80PortWrite*) { AY8910Write(4, 1, value); }

// MEM port wrappers
UINT8 AY8910_read_port_0_r(UINT32, struct MemoryReadByte*) { return (UINT8)AY8910Read(0); }
UINT8 AY8910_read_port_1_r(UINT32, struct MemoryReadByte*) { return (UINT8)AY8910Read(1); }
UINT8 AY8910_read_port_2_r(UINT32, struct MemoryReadByte*) { return (UINT8)AY8910Read(2); }
UINT8 AY8910_read_port_3_r(UINT32, struct MemoryReadByte*) { return (UINT8)AY8910Read(3); }
UINT8 AY8910_read_port_4_r(UINT32, struct MemoryReadByte*) { return (UINT8)AY8910Read(4); }

void AY8910_control_port_0_w(UINT32, UINT8 value, struct MemoryWriteByte*) { AY8910Write(0, 0, value); }
void AY8910_control_port_1_w(UINT32, UINT8 value, struct MemoryWriteByte*) { AY8910Write(1, 0, value); }
void AY8910_control_port_2_w(UINT32, UINT8 value, struct MemoryWriteByte*) { AY8910Write(2, 0, value); }
void AY8910_control_port_3_w(UINT32, UINT8 value, struct MemoryWriteByte*) { AY8910Write(3, 0, value); }
void AY8910_write_port_0_w(UINT32, UINT8 value, struct MemoryWriteByte*) { AY8910Write(0, 1, value); }
void AY8910_write_port_1_w(UINT32, UINT8 value, struct MemoryWriteByte*) { AY8910Write(1, 1, value); }
void AY8910_write_port_2_w(UINT32, UINT8 value, struct MemoryWriteByte*) { AY8910Write(2, 1, value); }
void AY8910_write_port_3_w(UINT32, UINT8 value, struct MemoryWriteByte*) { AY8910Write(3, 1, value); }

// ----- Clock/volume table/reset -------------------------------------------

void AY8910_set_clock(int chip, int clock)
{
	AY8910* PSG = &AYPSG[chip];
	// Tone & noise from clock/8; UpdateStep = events per sample in STEP domain.
	PSG->UpdateStep = (unsigned int)(((double)STEP * PSG->SampleRate * 8.0) / (double)clock);
}

static void build_mixer_table(int chip)
{
	AY8910* PSG = &AYPSG[chip];

	// Tone ladder
	if (PSG->isYM2149) {
		// YM2149: RL = 1k, level 0 conducts (zero_is_off = false)
		double tone[16];
		build_single_table(1000.0, kYM2149_Param, tone, /*zero_is_off=*/false);
		PSG->VolTable[0] = 0;
		for (int i = 1; i < 16; i++) {
			const unsigned v = (unsigned)std::lround(tone[i] * (double)MAX_OUTPUT);
			PSG->VolTable[i * 2 + 1] = v;  // odd indices used by tone path
			PSG->VolTable[i * 2] = v;  // harmless mirror
		}
		// YM envelope ladder (32)
		double env[32];
		build_single_table(1000.0, kYM2149_ParamEnv, env, /*zero_is_off=*/false);
		for (int i = 0; i < 32; i++)
			PSG->VolEnvTable[i] = (unsigned)std::lround(env[i] * (double)MAX_OUTPUT);
	}
	else {
		// AY-3-8910: RL = 2k, level 0 is OFF (zero_is_off = true)
		double tone[16];
		build_single_table(2000.0, kAY8910_Param, tone, /*zero_is_off=*/true);
		PSG->VolTable[0] = 0;
		for (int i = 1; i < 16; i++) {
			const unsigned v = (unsigned)std::lround(tone[i] * (double)MAX_OUTPUT);
			PSG->VolTable[i * 2 + 1] = v;  // odd (tone)
			PSG->VolTable[i * 2] = v;  // mirror
		}
		// For AY we keep using the legacy envelope path which reads VolTable[env]
	}
}

void AY8910_reset(int chip)
{
	AY8910* PSG = &AYPSG[chip];

	PSG->register_latch = 0;
	PSG->RNG = 1;
	PSG->OutputA = PSG->OutputB = PSG->OutputC = 0;
	PSG->OutputN = 0xff;

	for (int i = 0; i < AY_PORTA; i++)
		_AYWriteReg(chip, i, 0);

	PSG->ChanGain[0] = PSG->ChanGain[1] = PSG->ChanGain[2] = 255;
	PSG->VolA = PSG->VolB = PSG->VolC = PSG->VolE = 0;
	PSG->EnvelopeA = PSG->EnvelopeB = PSG->EnvelopeC = 0;
	PSG->CountEnv = 0x1f;
	PSG->Holding = 0;

#if AY8910_ENABLE_DC_BLOCK
	PSG->dc_prev_in = 0;
	PSG->dc_prev_out = 0;
#endif

	PSG->lp_y_prev = 0.0f;
}

// ----- Init/Start/Clear ----------------------------------------------------

static int AY8910_init(int chip,
	int clock, int volume, int volshift, int sample_rate,
	int (*portAread)(void), int (*portBread)(void),
	void (*portAwrite)(int offset, int data),
	void (*portBwrite)(int offset, int data))
{
	AY8910* PSG = &AYPSG[chip];
	std::memset(PSG, 0, sizeof(AY8910));

	PSG->SampleRate = sample_rate;
	PSG->PortAread = portAread;
	PSG->PortBread = portBread;
	PSG->PortAwrite = portAwrite;
	PSG->PortBwrite = portBwrite;

	// Variant selection (no API change):
	//   Pass a NEGATIVE baseclock to select YM2149 for this chip.
	//   Positive baseclock => AY-3-8910 (default).
	if (clock < 0) {
		PSG->isYM2149 = 1;
		clock = -clock;
	}
	else {
		PSG->isYM2149 = 0;
	}

	// Master gain defaults (unity = 256 >> 8)
	ayvol[chip] = (volume <= 0) ? 256 : volume;
	ayvolshift[chip] = (volshift <= 0) ? 8 : volshift;
	PSG->ChanGain[0] = PSG->ChanGain[1] = PSG->ChanGain[2] = 255;

	AY8910_set_clock(chip, clock);

	// Build the per-variant tables (tone + optional YM envelope)
	build_mixer_table(chip);

	AY8910_reset(chip);

	// Per-chip master volume unity
	AY8910_set_volume(chip, ALL_8910_CHANNELS, 256);

	// Always-on mild LP (~8 kHz)
	const float fc = 8000.0f;
	const float dt = 1.0f / (float)PSG->SampleRate;
	const float RC = 1.0f / (6.283185307f * fc);   // 1 / (2*pi*fc)
	PSG->lp_alpha = dt / (RC + dt);
	PSG->lp_y_prev = 0.0f;

	return 0;
}

int AY8910_sh_start(struct AY8910interface* intf)
{
	ayintf = intf;
	numchips = intf->num;

	aysamples = config.samplerate / Machine->gamedrv->fps;

	for (int chip = 0; chip < numchips; ++chip)
	{
		// Allocate the per-chip mixed buffer
		mixbuf[chip] = (int16_t*)std::malloc(sizeof(int16_t) * (size_t)aysamples);
		if (!mixbuf[chip]) return 1;

		updpos[chip] = 0;
		updlast[chip] = 0;

		if (AY8910_init(chip, intf->baseclock,
			intf->vol[chip], intf->volshift[chip],
			config.samplerate,
			intf->portAread[chip], intf->portBread[chip],
			intf->portAwrite[chip], intf->portBwrite[chip]) != 0)
		{
			return 1;
		}
	}

	// Start one stream per chip (mono 16-bit)
	for (int chip = 0; chip < numchips; ++chip)
		stream_start(chip, chip, 16, Machine->gamedrv->fps);

	return 0;
}

void AY8910clear(void)
{
	for (int chip = 0; chip < numchips; ++chip)
	{
		if (mixbuf[chip])
		{
			std::free(mixbuf[chip]);
			mixbuf[chip] = nullptr;
		}
	}
	for (int chip = 0; chip < numchips; ++chip)
		stream_stop(chip, chip);
}

// ----- Core mixer (single-stream) -----------------------------------------

// Mix 'length' samples for one chip into its mono buffer segment.
// Pipeline: integrate squares -> channel gains -> master gain -> DC high-pass
// -> ALWAYS-ON RC low-pass -> clamp to int16.
static void AY8910UpdateMixed(int chip, int length)
{
	if (length <= 0) return;

	AY8910* PSG = &AYPSG[chip];
	int16_t* dst = mixbuf[chip] + updpos[chip];

	const int prelen = length * STEP;

	// Keep phase sane for disabled/zero-volume channels by fast-forwarding.
	if (PSG->Regs[AY_ENABLE] & 0x01) { if (PSG->CountA <= prelen) PSG->CountA += prelen; PSG->OutputA = 1; }
	else if (PSG->Regs[AY_AVOL] == 0) { if (PSG->CountA <= prelen) PSG->CountA += prelen; }

	if (PSG->Regs[AY_ENABLE] & 0x02) { if (PSG->CountB <= prelen) PSG->CountB += prelen; PSG->OutputB = 1; }
	else if (PSG->Regs[AY_BVOL] == 0) { if (PSG->CountB <= prelen) PSG->CountB += prelen; }

	if (PSG->Regs[AY_ENABLE] & 0x04) { if (PSG->CountC <= prelen) PSG->CountC += prelen; PSG->OutputC = 1; }
	else if (PSG->Regs[AY_CVOL] == 0) { if (PSG->CountC <= prelen) PSG->CountC += prelen; }

	if ((PSG->Regs[AY_ENABLE] & 0x38) == 0x38) {
		if (PSG->CountN <= prelen) PSG->CountN += prelen;
	}

	int outn = (PSG->OutputN | PSG->Regs[AY_ENABLE]);

	while (length--)
	{
		int vola = 0, volb = 0, volc = 0;
		int left = STEP;

		do
		{
			const int nextevent = (PSG->CountN < left) ? PSG->CountN : left;

			// ----- Channel A -----
			if (outn & 0x08)
			{
				if (PSG->OutputA) vola += PSG->CountA;
				PSG->CountA -= nextevent;
				while (PSG->CountA <= 0)
				{
					PSG->CountA += PSG->PeriodA;
					if (PSG->CountA > 0) { PSG->OutputA ^= 1; vola += PSG->OutputA * PSG->PeriodA; break; }
					PSG->CountA += PSG->PeriodA; vola += PSG->PeriodA;
				}
				if (PSG->OutputA) vola -= PSG->CountA;
			}
			else
			{
				PSG->CountA -= nextevent;
				while (PSG->CountA <= 0)
				{
					PSG->CountA += PSG->PeriodA;
					if (PSG->CountA > 0) { PSG->OutputA ^= 1; break; }
					PSG->CountA += PSG->PeriodA;
				}
			}

			// ----- Channel B -----
			if (outn & 0x10)
			{
				if (PSG->OutputB) volb += PSG->CountB;
				PSG->CountB -= nextevent;
				while (PSG->CountB <= 0)
				{
					PSG->CountB += PSG->PeriodB;
					if (PSG->CountB > 0) { PSG->OutputB ^= 1; volb += PSG->OutputB * PSG->PeriodB; break; }
					PSG->CountB += PSG->PeriodB; volb += PSG->PeriodB;
				}
				if (PSG->OutputB) volb -= PSG->CountB;
			}
			else
			{
				PSG->CountB -= nextevent;
				while (PSG->CountB <= 0)
				{
					PSG->CountB += PSG->PeriodB;
					if (PSG->CountB > 0) { PSG->OutputB ^= 1; break; }
					PSG->CountB += PSG->PeriodB;
				}
			}

			// ----- Channel C -----
			if (outn & 0x20)
			{
				if (PSG->OutputC) volc += PSG->CountC;
				PSG->CountC -= nextevent;
				while (PSG->CountC <= 0)
				{
					PSG->CountC += PSG->PeriodC;
					if (PSG->CountC > 0) { PSG->OutputC ^= 1; volc += PSG->OutputC * PSG->PeriodC; break; }
					PSG->CountC += PSG->PeriodC; volc += PSG->PeriodC;
				}
				if (PSG->OutputC) volc -= PSG->CountC;
			}
			else
			{
				PSG->CountC -= nextevent;
				while (PSG->CountC <= 0)
				{
					PSG->CountC += PSG->PeriodC;
					if (PSG->CountC > 0) { PSG->OutputC ^= 1; break; }
					PSG->CountC += PSG->PeriodC;
				}
			}

			// ----- Noise -----
			PSG->CountN -= nextevent;
			if (PSG->CountN <= 0)
			{
				if ((PSG->RNG + 1) & 2) { PSG->OutputN = (unsigned char)~PSG->OutputN; outn = (PSG->OutputN | PSG->Regs[AY_ENABLE]); }
				if (PSG->RNG & 1) PSG->RNG ^= 0x28000;
				PSG->RNG >>= 1;
				PSG->CountN += PSG->PeriodN;
			}

			left -= nextevent;
		} while (left > 0);

		// ----- Envelope progression -----
		if (PSG->Holding == 0)
		{
			PSG->CountE -= STEP;
			if (PSG->CountE <= 0)
			{
				do { PSG->CountEnv--; PSG->CountE += PSG->PeriodE; } while (PSG->CountE <= 0);

				if (PSG->CountEnv < 0)
				{
					if (PSG->Hold) {
						if (PSG->Alternate) PSG->Attack ^= 0x1f;
						PSG->Holding = 1; PSG->CountEnv = 0;
					}
					else {
						if (PSG->Alternate && (PSG->CountEnv & 0x20)) PSG->Attack ^= 0x1f;
						PSG->CountEnv &= 0x1f;
					}
				}

				// Swap in YM's 32-step env table when in YM mode
				PSG->VolE = PSG->isYM2149 ? PSG->VolEnvTable[PSG->CountEnv ^ PSG->Attack]
					: PSG->VolTable[PSG->CountEnv ^ PSG->Attack];
				if (PSG->EnvelopeA) PSG->VolA = PSG->VolE;
				if (PSG->EnvelopeB) PSG->VolB = PSG->VolE;
				if (PSG->EnvelopeC) PSG->VolC = PSG->VolE;
			}
		}

		// ----- Convert integrated areas to sample values (>>15 for STEP) -----
		int32_t a = ((int64_t)vola * (int64_t)PSG->VolA) >> 15;
		int32_t b = ((int64_t)volb * (int64_t)PSG->VolB) >> 15;
		int32_t c = ((int64_t)volc * (int64_t)PSG->VolC) >> 15;

		// Per-channel pre-mix gains (0..255 -> >>8)
		a = (a * (int)PSG->ChanGain[0]) >> 8;
		b = (b * (int)PSG->ChanGain[1]) >> 8;
		c = (c * (int)PSG->ChanGain[2]) >> 8;

		int32_t sum = a + b + c;

		// Per-chip master gain
		if (ayvol[chip] != 0)
			sum = (sum * ayvol[chip]) >> ayvolshift[chip];

		// ----- DC high-pass -----
#if AY8910_ENABLE_DC_BLOCK
		const int32_t in = sum;
		const int32_t out = in - PSG->dc_prev_in
			+ (int32_t)(((int64_t)PSG->dc_prev_out * AY_DC_R_COEF + (AY_DC_R_FP / 2)) / AY_DC_R_FP);
		PSG->dc_prev_in = in;
		PSG->dc_prev_out = out;
		int32_t hp = out;
#else
		int32_t hp = sum;
#endif

		// ----- ALWAYS-ON one-pole RC low-pass (float state) -----
		float s = (float)hp;
		PSG->lp_y_prev = PSG->lp_y_prev + PSG->lp_alpha * (s - PSG->lp_y_prev);
		s = PSG->lp_y_prev;

		// Final clamp to int16
		if (s > 32767.0f) s = 32767.0f;
		else if (s < -32768.0f) s = -32768.0f;

		*dst++ = (int16_t)(int)s;
	}
}

// ----- Partial/full frame update & push ------------------------------------

void AY8910partupdate(int chip)
{
	int work = cpu_scale_by_cycles(aysamples, ayintf->baseclock);
	int updlen = work - updlast[chip];

	if (updlen > 32)
	{
		AY8910UpdateMixed(chip, updlen);
		updpos[chip] += updlen;
		updlast[chip] = work;
	}
}

void AY8910_sh_update(void)
{
	for (int i = 0; i < numchips; ++i)
	{
		AY8910UpdateMixed(i, aysamples - updpos[i]);
		stream_update(i, mixbuf[i]);
		updpos[i] = 0;
		updlast[i] = 0;
	}
}