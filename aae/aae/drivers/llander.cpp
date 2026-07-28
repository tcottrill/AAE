//============================================================================
// AAE is a poorly written M.A.M.E (TM) derivitave based on early MAME
// code, 0.29 through .90 mixed with code of my own. This emulator was
// created solely for my amusement and learning and is provided only
// as an archival experience.
//
// All MAME code used and abused in this emulator remains the copyright
// of the dedicated people who spend countless hours creating it. All
// MAME code should be annotated as belonging to the MAME TEAM.
//
// SOME CODE BELOW IS FROM MAME and COPYRIGHT the MAME TEAM.
//============================================================================

/* Lunar Lander Emu */

#include "aae_mame_driver.h"
#include "mixer.h"
#include "driver_registry.h"    // AAE_REGISTER_DRIVER
#include "llander.h"
#include "old_mame_vecsim_dvg.h"

static int lamp0 = 0;
static uint8_t* llander_zeropage;

static const char* llander_samples[] = {
	"llander.zip",
	"lthrust.wav",
	"beep.wav",
	"lexplode.wav",
	"lander6k.wav",
	 0 };

void llander_interrupt()
{
	// Turn off interrupts if self-test is enabled
	if (readinputport(0) & 0x02)
	{
		cpu_do_int_imm(CPU0, INT_TYPE_NMI);
	}
}

READ_HANDLER(llander_zeropage_r)
{
	return llander_zeropage[address & 0xff];
}

READ_HANDLER(llander_DSW1_r)
{
	int res;
	int res1;

	res1 = readinputportbytag("DSW1");

	res = 0xfc | ((res1 >> (2 * (3 - (address & 0x3)))) & 0x3);
	return res;
}

WRITE_HANDLER(llander_zeropage_w)
{
	llander_zeropage[address & 0xff] = data;
}

READ_HANDLER(llander_IN0_r)
{
	int val = readinputportbytag("IN0");

	if (dvg_done())
		val |= 0x01;
	// 3KHz clock on bit 6. eternaticks only advances at scheduler-slice
	// boundaries; add the cycles executed so far inside the current slice
	// (get6502ticks(0), reset each slice by cpu_exec_now) so the bit toggles
	// at instruction granularity - the self-test busy-waits on its edges
	// ($7C09/$7E84 BIT IN0 / BVS-BVC loops).
	if ((get_eterna_ticks(0) + m_cpu_6502[CPU0]->get6502ticks(0)) & 0x100)
		val |= 0x40;

	return val;
}

READ_HANDLER(llander_IN1_r)
{
	int res;
	int bitmask;

	res = readinputportbytag("IN1");

	bitmask = (1 << address);

	if (res & bitmask)
		res = 0x80;
	else
		res = ~0x80;
	return (res);
}

WRITE_HANDLER(llander_snd_reset_w) { ; }

WRITE_HANDLER(llander_sounds_w)
{
	const int level = data & 0x07;                 // 1 = idle floor, up to 7 = full thrust

	// Level 0/1 = no thrust (silent); 2..7 mapped so max level -> max volume.
	const int MAX_LEVEL = 7;                        // top of the 3-bit thrust field
	const int v = (level < 2) ? 0
		: (level - 1) * 255 / (MAX_LEVEL - 1);
	sample_set_volume(1, v);

	if (data & 0x10) { if (!sample_playing(3)) sample_start(3, 1, 0); }
	if (data & 0x20) { if (!sample_playing(4)) sample_start(4, 3, 0); }
	if (data & 0x08) { if (!sample_playing(2)) sample_start(2, 2, 0); }
}

/* Lunar lander LED port seems to be mapped thus:

   NNxxxxxx - Apparently unused
   xxNxxxxx - Unknown gives 4 high pulses of variable duration when coin put in ?
   xxxNxxxx - Start    Lamp ON/OFF == 0/1
   xxxxNxxx - Training Lamp ON/OFF == 1/0
   xxxxxNxx - Cadet    Lamp ON/OFF
   xxxxxxNx - Prime    Lamp ON/OFF
   xxxxxxxN - Command  Lamp ON/OFF

   Selection lamps seem to all be driver 50/50 on/off during attract mode ?
*/

WRITE_HANDLER(ll_led_write)
{
	data = data & 0xff;
	lamp0 = ((data >> (4 - 0)) & 1);
}

MEM_READ(LlanderRead)
MEM_ADDR(0x0000, 0x01ff, llander_zeropage_r)
MEM_ADDR(0x2000, 0x2000, llander_IN0_r)
MEM_ADDR(0x2400, 0x2407, llander_IN1_r)
MEM_ADDR(0x2800, 0x2803, llander_DSW1_r)
MEM_ADDR(0x2c00, 0x2c00, ip_port_3_r)
MEM_ADDR(0x4000, 0x47ff, MRA_RAM)
MEM_ADDR(0x4800, 0x5fff, MRA_ROM) /* vector rom */
MEM_ADDR(0x6000, 0x7fff, MRA_ROM)
MEM_ADDR(0xf800, 0xffff, MRA_ROM)

MEM_END

MEM_WRITE(LlanderWrite)
MEM_ADDR(0x0000, 0x01ff, llander_zeropage_w)
MEM_ADDR(0x3000, 0x3000, dvg_go_w)
MEM_ADDR(0x3200, 0x3200, ll_led_write)
MEM_ADDR(0x3400, 0x3400, watchdog_reset_w)
MEM_ADDR(0x3c00, 0x3c00, llander_sounds_w)
MEM_ADDR(0x3e00, 0x3e00, llander_snd_reset_w)
MEM_ADDR(0x4000, 0x47ff, MWA_RAM)
MEM_ADDR(0x4800, 0x5fff, MWA_ROM)
MEM_ADDR(0x6000, 0x7fff, MWA_ROM)
MEM_END

void run_llander()
{
}

// Returns 1 if the fake "Artwork Mod" dipswitch (DSW2, llander set only) is On.
// Reads Machine->input_ports directly: the per-frame input_port_value[] array
// (readinputportbytag) is not yet built when init_game() runs. llander1 has no
// such switch -> returns 0.
static int llander_artwork_mod_enabled(void)
{
	for (const InputPort* in = Machine->input_ports; in->type != IPT_END; in++)
	{
		if ((in->type & ~IPF_MASK) == IPT_DIPSWITCH_NAME &&
			in->name && strcmp(in->name, "Artwork Mod") == 0)
			return (in->default_value & in->mask) != 0;
	}
	return 0;
}

// Artwork-mod ROM patches: the "<NAME> MISSION" HUD label under VERTICAL SPEED,
// and the game-select difficulty box (rectangle + title + description lines +
// lander icon). Installed at init when the Artwork Mod dipswitch is On. All
// patched addresses are specific to the llander (rev 2) ROM set.
static void llander_install_artwork_mod(void)
{
	// --- HUD mission label: "<NAME> MISSION" under "VERTICAL SPEED" ---
	// Routine at $1000 (unmapped exec space), position CUR at $1080; the
	// DrawHudPanel call at $6079 is repointed to it. Draws the real HUD, then -
	// only when GameState ($22) & $C0 is nonzero ($40/$80, real gameplay; the
	// attract demo flight runs at GameState=0) - the mission string (index =
	// PLYMOD $23 & 3) from the shared table at $1285/$1289.
	{
		unsigned char* M = Machine->memory_region[CPU0];

		static const unsigned char hook_code[] = {
			0x20,0x95,0x67,   // JSR $6795   DrawHudPanel (draw the real HUD first)
			0xA5,0x22,        // LDA $22     GameState
			0x29,0xC0,        // AND #$C0    gameplay states b6/b7 ($40/$80); attract = 0/$10/$20
			0xF0,0x16,        // BEQ +22     -> skip (RTS)
			0xA9,0x10,        // LDA #$10    missPosVEC hi
			0xA0,0x80,        // LDY #$80    missPosVEC lo
			0x20,0xCD,0x7E,   // JSR $7ECD   EmitVec4  (append the position move)
			0xA5,0x23,        // LDA $23     PLYMOD (game type: 0=TRAINING..3=COMMAND)
			0x29,0x03,        // AND #$03    -> mission 0..3
			0xAA,             // TAX         (split lo/hi tables: index by mission, NOT *2)
			0xBC,0x85,0x12,   // LDY $1285,X mTblLo2 ("<NAME> MISSION" lo, shared w/ the box)
			0xBD,0x89,0x12,   // LDA $1289,X mTblHi2 (string hi)
			0xAA,             // TAX         X=hi, Y=lo (DrawStringVec convention)
			0x4C,0xF2,0x79,   // JMP $79F2   DrawStringVec (RTSes back to $652D)
			0x60              // skip: RTS   (GameState != $40 -> label not drawn)
		};
		for (int i = 0; i < (int)sizeof(hook_code); i++) M[0x1000 + i] = hook_code[i];

		static const unsigned char hook_data[] = {
			// $1080 missPos: absolute CUR, word0=$A000|Y, word1=X (Y increases upward).
			// x=600 left-aligns with column 2's labels; y=650 sits under VERTICAL SPEED.
			0x9A,0xA2, 0x58,0x02    // CUR  y=650 ($A28A), x=600 ($0258)
		};
		for (int i = 0; i < (int)sizeof(hook_data); i++) M[0x1080 + i] = hook_data[i];

		// Repoint the DrawHudPanel call: $6079  JSR $6795  ->  JSR $1000 (my_hook)
		M[0x6079] = 0x20; M[0x607A] = 0x00; M[0x607B] = 0x10;
	}

	// --- game-select difficulty box (GameState ($22) == $20 only) ---
	// Repoints the DrawAttractFrame call at $60A6 to a hook at $1100 that draws the
	// real attract frame, then appends: a 250x300 line-rectangle, a "<NAME> MISSION"
	// title, 3-4 description lines (per-mission subroutines at $1400+), and a lander
	// icon (SHIP00 copy, rotated 90 deg CCW, 4x scale - see landerShape below).
	// Box bottom-left: x = 12 + 250*mission (12/262/512/762), y=30; title/lines/
	// lander track the box via per-mission CUR tables (split lo/hi pointer tables,
	// indexed by mission, NOT *2). Usable display is ~1024 wide.
	// DVG encoding: CUR word0=$A000|Y, word1=(scale<<12)|X; VEC opcode 9 with the
	// scale register at 0 draws 1:1 magnitude->pixel.
	{
		unsigned char* M = Machine->memory_region[CPU0];

		static const unsigned char hook_code2[] = {
			0x20,0x7E,0x68,   // JSR $687E   DrawAttractFrame (draw the real attract screen first)
			0xA5,0x22,        // LDA $22     GameState
			0xC9,0x20,        // CMP #$20    only fire in the exact game-select state (not $10)
			0xD0,0x70,        // BNE +112    -> skip (RTS)
			0xA5,0x23,        // LDA $23     PLYMOD (game type: 0=TRAINING..3=COMMAND)
			0x29,0x03,        // AND #$03    -> mission 0..3
			0xAA,             // TAX
			0xBC,0xA0,0x11,   // LDY $11A0,X boxCurPtrLo
			0xBD,0xA4,0x11,   // LDA $11A4,X boxCurPtrHi
			0x20,0xCD,0x7E,   // JSR $7ECD   EmitVec4 (append the mission-indexed box CUR)
			0xA9,0x11,        // LDA #$11    boxEdges hi ($1180)
			0xA0,0x80,        // LDY #$80    boxEdges lo
			0xA2,0x10,        // LDX #$10    16 bytes: 4 VEC edges (no CUR - that's above)
			0x20,0xA6,0x7E,   // JSR $7EA6   CopyToVecRam (A=srcHi,Y=srcLo,X=count)
			0xA5,0x23,        // LDA $23     PLYMOD
			0x29,0x03,        // AND #$03
			0xAA,             // TAX
			0xBC,0xB8,0x11,   // LDY $11B8,X textCurPtrLo
			0xBD,0xBC,0x11,   // LDA $11BC,X textCurPtrHi
			0x20,0xCD,0x7E,   // JSR $7ECD   EmitVec4 (append the mission-indexed title CUR)
			0xA5,0x23,        // LDA $23     PLYMOD
			0x29,0x03,        // AND #$03
			0xAA,             // TAX         (split lo/hi tables: index by mission, NOT *2)
			0xBC,0x85,0x12,   // LDY $1285,X mTblLo2 ("<NAME> MISSION" lo)
			0xBD,0x89,0x12,   // LDA $1289,X mTblHi2
			0xAA,             // TAX         X=hi, Y=lo (DrawStringVec convention)
			0x20,0xF2,0x79,   // JSR $79F2   DrawStringVec (title)
			// --- dispatch to this mission's description-line subroutine ---
			0xA5,0x23,        // LDA $23     PLYMOD
			0x29,0x03,        // AND #$03
			0xC9,0x00,        // CMP #$00
			0xF0,0x0E,        // BEQ +14 -> do_training
			0xC9,0x01,        // CMP #$01
			0xF0,0x10,        // BEQ +16 -> do_cadet
			0xC9,0x02,        // CMP #$02
			0xF0,0x12,        // BEQ +18 -> do_prime
			0x20,0x8F,0x14,   // JSR $148F   do_command_lines (mission==3, fallthrough)
			0x4C,0x62,0x11,   // JMP $1162   after_lines
			0x20,0x00,0x14,   // do_training: JSR $1400   do_training_lines
			0x4C,0x62,0x11,   // JMP $1162   after_lines
			0x20,0x39,0x14,   // do_cadet:    JSR $1439   do_cadet_lines
			0x4C,0x62,0x11,   // JMP $1162   after_lines
			0x20,0x64,0x14,   // do_prime:    JSR $1464   do_prime_lines
			// --- after_lines ($1162): draw the lander ---
			0xA5,0x23,        // LDA $23     PLYMOD
			0x29,0x03,        // AND #$03
			0xAA,             // TAX
			0xBC,0xD0,0x11,   // LDY $11D0,X landerCurPtrLo
			0xBD,0xD4,0x11,   // LDA $11D4,X landerCurPtrHi
			0x20,0xCD,0x7E,   // JSR $7ECD   EmitVec4 (mission-indexed lander CUR, scale reg=0)
			0xA9,0x11,        // LDA #$11    landerShape hi ($11D8)
			0xA0,0xD8,        // LDY #$D8    landerShape lo
			0xA2,0x74,        // LDX #$74    116 bytes: rotated+4x legs(9)+body(20) VCTR entries
			0x20,0xA6,0x7E,   // JSR $7EA6   CopyToVecRam
			0x60              // skip: RTS   (GameState != $20 -> nothing extra drawn)
		};
		for (int i = 0; i < (int)sizeof(hook_code2); i++) M[0x1100 + i] = hook_code2[i];

		// Description-line subroutines at $1400+, one per mission: a flat sequence of
		// (CUR via EmitVec4 $7ECD) + (string via DrawStringVec $79F2) pairs, one pair
		// per line, ending RTS. $1400 is past the end of hook_data2 ($1346).
		static const unsigned char hook_code3[] = {
			// $1400 do_training_lines (4 lines, ends RTS at $1438)
			0xA9,0x13,0xA0,0x13, 0x20,0xCD,0x7E, 0xA2,0x12,0xA0,0x8D, 0x20,0xF2,0x79, // LIGHT GRAVITY
			0xA9,0x13,0xA0,0x17, 0x20,0xCD,0x7E, 0xA2,0x12,0xA0,0x9A, 0x20,0xF2,0x79, // FRICTION
			0xA9,0x13,0xA0,0x1B, 0x20,0xCD,0x7E, 0xA2,0x12,0xA0,0xA2, 0x20,0xF2,0x79, // FROM ATMOSPHERE
			0xA9,0x13,0xA0,0x1F, 0x20,0xCD,0x7E, 0xA2,0x12,0xA0,0xB1, 0x20,0xF2,0x79, // CONTROLLER ROTATION
			0x60,
			// $1439 do_cadet_lines (3 lines, ends RTS at $1463)
			0xA9,0x13,0xA0,0x23, 0x20,0xCD,0x7E, 0xA2,0x12,0xA0,0xC4, 0x20,0xF2,0x79, // MODERATE GRAVITY
			0xA9,0x13,0xA0,0x27, 0x20,0xCD,0x7E, 0xA2,0x12,0xA0,0xD4, 0x20,0xF2,0x79, // NO FRICTION
			0xA9,0x13,0xA0,0x2B, 0x20,0xCD,0x7E, 0xA2,0x12,0xA0,0xDF, 0x20,0xF2,0x79, // CONTROLLED ROTATION
			0x60,
			// $1464 do_prime_lines (3 lines, ends RTS at $148E)
			0xA9,0x13,0xA0,0x2F, 0x20,0xCD,0x7E, 0xA2,0x12,0xA0,0xF2, 0x20,0xF2,0x79, // STRONG GRAVITY
			0xA9,0x13,0xA0,0x33, 0x20,0xCD,0x7E, 0xA2,0x12,0xA0,0xD4, 0x20,0xF2,0x79, // NO FRICTION
			0xA9,0x13,0xA0,0x37, 0x20,0xCD,0x7E, 0xA2,0x12,0xA0,0xDF, 0x20,0xF2,0x79, // CONTROLLED ROTATION
			0x60,
			// $148F do_command_lines (3 lines, ends RTS at $14B9)
			0xA9,0x13,0xA0,0x3B, 0x20,0xCD,0x7E, 0xA2,0x12,0xA0,0xC4, 0x20,0xF2,0x79, // MODERATE GRAVITY
			0xA9,0x13,0xA0,0x3F, 0x20,0xCD,0x7E, 0xA2,0x12,0xA0,0xD4, 0x20,0xF2,0x79, // NO FRICTION
			0xA9,0x13,0xA0,0x43, 0x20,0xCD,0x7E, 0xA2,0x13,0xA0,0x00, 0x20,0xF2,0x79, // ROTATIONAL MOMENTUM
			0x60
		};
		for (int i = 0; i < (int)sizeof(hook_code3); i++) M[0x1400 + i] = hook_code3[i];

		static const unsigned char hook_data2[] = {
			// $1180 boxEdges: 4 VEC edges (opcode 9, z=15) tracing a 250x300 box clockwise
			// from the box CUR below.
			0x00,0x90, 0xFA,0xF0,   // VEC  dx=+250 dy=0    z=15  (bottom edge)
			0x2C,0x91, 0x00,0xF0,   // VEC  dx=0    dy=+300 z=15  (right edge)
			0x00,0x90, 0xFA,0xF4,   // VEC  dx=-250 dy=0    z=15  (top edge)
			0x2C,0x95, 0x00,0xF0,   // VEC  dx=0    dy=-300 z=15  (left edge, closes the box)

			// $1190 boxCurTbl: per-mission CUR to the box's bottom-left corner.
			// x = 12 + 250*mission (12/262/512/762), y=30.
			0x1E,0xA0, 0x0C,0x00,   // [0] TRAINING x=12
			0x1E,0xA0, 0x06,0x01,   // [1] CADET    x=262
			0x1E,0xA0, 0x00,0x02,   // [2] PRIME    x=512
			0x1E,0xA0, 0xFA,0x02,   // [3] COMMAND  x=762
			// $11A0 boxCurPtrLo / $11A4 boxCurPtrHi (index by mission, NOT *2)
			0x90,0x94,0x98,0x9C,
			0x11,0x11,0x11,0x11,

			// $11A8 textCurTbl: per-mission CUR for the title line, y=300.
			// Font is 12px/char monospace: x = box_left + (250 - 12*len)/2.
			0x2C,0xA1, 0x29,0x00,   // [0] x=41   (TRAINING MISSION, 16 chars, box-left 12 + 29)
			0x2C,0xA1, 0x35,0x01,   // [1] x=309  (CADET MISSION, 13 chars, box-left 262 + 47)
			0x2C,0xA1, 0x2F,0x02,   // [2] x=559  (PRIME MISSION, 13 chars, box-left 512 + 47)
			0x2C,0xA1, 0x1D,0x03,   // [3] x=797  (COMMAND MISSION, 15 chars, box-left 762 + 35)
			// $11B8 textCurPtrLo / $11BC textCurPtrHi
			0xA8,0xAC,0xB0,0xB4,
			0x11,0x11,0x11,0x11,

			// $11C0 landerCurTbl: per-mission CUR for the lander icon.
			// x = box_left + 125 (box center), y=132; scale-register nibble 0.
			0x84,0xA0, 0x89,0x00,   // [0] x=137  y=132  (TRAINING box center)
			0x84,0xA0, 0x83,0x01,   // [1] x=387  y=132  (CADET)
			0x84,0xA0, 0x7D,0x02,   // [2] x=637  y=132  (PRIME)
			0x84,0xA0, 0x77,0x03,   // [3] x=887  y=132  (COMMAND)
			// $11D0 landerCurPtrLo / $11D4 landerCurPtrHi
			0xC0,0xC4,0xC8,0xCC,
			0x11,0x11,0x11,0x11,

			// $11D8 landerShape: copy of the vector-ROM "SHIP00" shape ($4916: legs octagon
			// $4800 + body outline), each vector rotated 90 deg CCW ((dx,dy) -> (-dy,dx))
			// and scaled 4x, re-encoded as opcode-9/scale-0 VCTRs (1:1 magnitude->pixel).
			// Original (dx,dy,z) values are in the trailing comments.
			// Legs (9 entries):
			0x08,0x94, 0x1C,0x00,   // dx=28  dy=-8  z=0   (orig -2,-7,0  move)
			0x14,0x94, 0x14,0xC4,   // dx=-20 dy=-20 z=12  (orig -5,5,12)
			0x00,0x94, 0x14,0xC4,   // dx=-20 dy=0   z=12  (orig 0,5,12)
			0x14,0x90, 0x14,0xC4,   // dx=-20 dy=20  z=12  (orig 5,5,12)
			0x14,0x90, 0x00,0xC4,   // dx=0   dy=20  z=12  (orig 5,0,12)
			0x14,0x90, 0x14,0xC0,   // dx=20  dy=20  z=12  (orig 5,-5,12)
			0x00,0x94, 0x14,0xC0,   // dx=20  dy=0   z=12  (orig 0,-5,12)
			0x14,0x94, 0x14,0xC0,   // dx=20  dy=-20 z=12  (orig -5,-5,12)
			0x14,0x94, 0x00,0xC4,   // dx=0   dy=-20 z=12  (orig -5,0,12)
			// Body (20 entries):
			0x14,0x94, 0x3C,0x04,   // dx=-60 dy=-20 z=0   (orig -5,15,0  move)
			0x00,0x94, 0x14,0xC0,   // dx=20  dy=0   z=12  (orig 0,-5,12)
			0x00,0x94, 0x14,0x00,   // dx=20  dy=0   z=0   (orig 0,-5,0  move)
			0x00,0x94, 0x14,0xC0,   // dx=20  dy=0   z=12  (orig 0,-5,12)
			0x0C,0x94, 0x00,0xC4,   // dx=0   dy=-12 z=12  (orig -3,0,12)
			0x00,0x94, 0x3C,0xC4,   // dx=-60 dy=0   z=12  (orig 0,15,12)
			0x0C,0x90, 0x00,0xC4,   // dx=0   dy=12  z=12  (orig 3,0,12)
			0x0C,0x94, 0x00,0x04,   // dx=0   dy=-12 z=0   (orig -3,0,0  move)
			0x20,0x94, 0x10,0xC4,   // dx=-16 dy=-32 z=12  (orig -8,4,12)
			0x00,0x94, 0x08,0x04,   // dx=-8  dy=0   z=0   (orig 0,2,0   move)
			0x00,0x94, 0x10,0xC0,   // dx=16  dy=0   z=12  (orig 0,-4,12)
			0x00,0x94, 0x4C,0x00,   // dx=76  dy=0   z=0   (orig 0,-19,0 move)
			0x00,0x94, 0x10,0xC0,   // dx=16  dy=0   z=12  (orig 0,-4,12)
			0x00,0x94, 0x08,0x04,   // dx=-8  dy=0   z=0   (orig 0,2,0   move)
			0x20,0x90, 0x10,0xC4,   // dx=-16 dy=32  z=12  (orig 8,4,12)
			0x00,0x94, 0x10,0x04,   // dx=-16 dy=0   z=0   (orig 0,4,0   move)
			0x18,0x94, 0x0C,0xC0,   // dx=12  dy=-24 z=12  (orig -6,-3,12)
			0x00,0x94, 0x34,0xC4,   // dx=-52 dy=0   z=12  (orig 0,13,12)
			0x18,0x90, 0x0C,0xC0,   // dx=12  dy=24  z=12  (orig 6,-3,12)
			0x18,0x94, 0x0C,0x04,   // dx=-12 dy=-24 z=0   (orig -6,3,0  move)

			// $124C..$1284: "<NAME> MISSION" strings, used by both the box title and the
			// HUD label hook ($1000). Char encoding: byte=glyph*2, glyph=11+(letter-'A'),
			// digits=1+digit, space=0, bit7 set on the string's last byte.
			// $124C "TRAINING MISSION" (16 bytes)
			0x3C,0x38,0x16,0x26,0x30,0x26,0x30,0x22,0x00,0x2E,0x26,0x3A,0x3A,0x26,0x32,0xB0,
			// $125C "CADET MISSION" (13 bytes)
			0x1A,0x16,0x1C,0x1E,0x3C,0x00,0x2E,0x26,0x3A,0x3A,0x26,0x32,0xB0,
			// $1269 "PRIME MISSION" (13 bytes)
			0x34,0x38,0x26,0x2E,0x1E,0x00,0x2E,0x26,0x3A,0x3A,0x26,0x32,0xB0,
			// $1276 "COMMAND MISSION" (15 bytes)
			0x1A,0x32,0x2E,0x2E,0x16,0x30,0x1C,0x00,0x2E,0x26,0x3A,0x3A,0x26,0x32,0xB0,
			// $1285 mTblLo2 / $1289 mTblHi2 (index by mission, NOT *2)
			0x4C,0x5C,0x69,0x76,
			0x12,0x12,0x12,0x12,

			// $128D..$1312: description-line strings, 9 unique (MODERATE GRAVITY, NO
			// FRICTION, and CONTROLLED ROTATION are stored once and referenced from
			// multiple subroutines). Same char encoding as above.
			// $128D "LIGHT GRAVITY" (13 bytes) - TRAINING line 2
			0x2C,0x26,0x22,0x24,0x3C,0x00,0x22,0x38,0x16,0x40,0x26,0x3C,0xC6,
			// $129A "FRICTION" (8 bytes) - TRAINING line 3
			0x20,0x38,0x26,0x1A,0x3C,0x26,0x32,0xB0,
			// $12A2 "FROM ATMOSPHERE" (15 bytes) - TRAINING line 4
			0x20,0x38,0x32,0x2E,0x00,0x16,0x3C,0x2E,0x32,0x3A,0x34,0x24,0x1E,0x38,0x9E,
			// $12B1 "CONTROLLER ROTATION" (19 bytes) - TRAINING line 5
			0x1A,0x32,0x30,0x3C,0x38,0x32,0x2C,0x2C,0x1E,0x38,0x00,0x38,0x32,0x3C,0x16,0x3C,0x26,0x32,0xB0,
			// $12C4 "MODERATE GRAVITY" (16 bytes) - CADET line2 / COMMAND line2
			0x2E,0x32,0x1C,0x1E,0x38,0x16,0x3C,0x1E,0x00,0x22,0x38,0x16,0x40,0x26,0x3C,0xC6,
			// $12D4 "NO FRICTION" (11 bytes) - CADET line3 / PRIME line3 / COMMAND line3
			0x30,0x32,0x00,0x20,0x38,0x26,0x1A,0x3C,0x26,0x32,0xB0,
			// $12DF "CONTROLLED ROTATION" (19 bytes) - CADET line4 / PRIME line4
			0x1A,0x32,0x30,0x3C,0x38,0x32,0x2C,0x2C,0x1E,0x1C,0x00,0x38,0x32,0x3C,0x16,0x3C,0x26,0x32,0xB0,
			// $12F2 "STRONG GRAVITY" (14 bytes) - PRIME line2
			0x3A,0x3C,0x38,0x32,0x30,0x22,0x00,0x22,0x38,0x16,0x40,0x26,0x3C,0xC6,
			// $1300 "ROTATIONAL MOMENTUM" (19 bytes) - COMMAND line4
			0x38,0x32,0x3C,0x16,0x3C,0x26,0x32,0x30,0x16,0x2C,0x00,0x2E,0x32,0x2E,0x1E,0x30,0x3C,0x3E,0xAE,

			// $1313..$1346: per-description-line absolute CURs (scale 0). y steps -28 per
			// line from the title's y=300; x = box_left + (250 - 12*len)/2. hook_code3
			// references each blob by its address; the bytes are the (Y,X) coordinates.
			// $1313 TRAINING line2 "LIGHT GRAVITY" y=272 x=59 (12+47)
			0x10,0xA1, 0x3B,0x00,
			// $1317 TRAINING line3 "FRICTION" y=244 x=89 (12+77)
			0xF4,0xA0, 0x59,0x00,
			// $131B TRAINING line4 "FROM ATMOSPHERE" y=216 x=47 (12+35)
			0xD8,0xA0, 0x2F,0x00,
			// $131F TRAINING line5 "CONTROLLER ROTATION" y=188 x=23 (12+11)
			0xBC,0xA0, 0x17,0x00,
			// $1323 CADET line2 "MODERATE GRAVITY" y=272 x=291 (262+29)
			0x10,0xA1, 0x23,0x01,
			// $1327 CADET line3 "NO FRICTION" y=244 x=321 (262+59)
			0xF4,0xA0, 0x41,0x01,
			// $132B CADET line4 "CONTROLLED ROTATION" y=216 x=273 (262+11)
			0xD8,0xA0, 0x11,0x01,
			// $132F PRIME line2 "STRONG GRAVITY" y=272 x=553 (512+41)
			0x10,0xA1, 0x29,0x02,
			// $1333 PRIME line3 "NO FRICTION" y=244 x=571 (512+59)
			0xF4,0xA0, 0x3B,0x02,
			// $1337 PRIME line4 "CONTROLLED ROTATION" y=216 x=523 (512+11)
			0xD8,0xA0, 0x0B,0x02,
			// $133B COMMAND line2 "MODERATE GRAVITY" y=272 x=791 (762+29)
			0x10,0xA1, 0x17,0x03,
			// $133F COMMAND line3 "NO FRICTION" y=244 x=821 (762+59)
			0xF4,0xA0, 0x35,0x03,
			// $1343 COMMAND line4 "ROTATIONAL MOMENTUM" y=216 x=773 (762+11)
			0xD8,0xA0, 0x05,0x03
		};
		for (int i = 0; i < (int)sizeof(hook_data2); i++) M[0x1180 + i] = hook_data2[i];

		// Repoint the DrawAttractFrame call: $60A6  JSR $687E  ->  JSR $1100 (hook_code2)
		M[0x60A6] = 0x20; M[0x60A7] = 0x00; M[0x60A8] = 0x11;
	}
}

/////////////////// MAIN() for program ///////////////////////////////////////////////////
int init_llander()
{
	//init6502(LlanderRead, LlanderWrite, 0x7fff, CPU0);

	llander_zeropage = &Machine->memory_region[CPU0][0x00];
	sample_start(1, 0, 1);
	sample_set_volume(1, 5);

	// ROM patches (HUD mission label + game-select box) gated by the Artwork Mod dip.
	if (llander_artwork_mod_enabled())
		llander_install_artwork_mod();

	dvg_start();
	return 0;
}

void end_llander()
{
}

//Lunar Lander Input Ports
INPUT_PORTS_START(llander)
PORT_START("IN0") /* IN0 */
/* Bit 0 is VG_HALT, handled in the machine dependant part */
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNKNOWN)
PORT_BITX(0x02, 0x02, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
PORT_DIPSETTING(0x02, DEF_STR(Off))
PORT_DIPSETTING(0x00, DEF_STR(On))
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_TILT)
/* Bits 3,4,5 unknown. Bit 6 is the 3KHz source - it must default LOW (active
   high) so llander_IN0_r's live clock bit controls it; with it inside the old
   active-low 0x78 mask it read as permanently 1 and the self-test's 3KHz
   edge-wait loops ($7C09) spun forever. */
	PORT_BIT(0x38, IP_ACTIVE_LOW, IPT_UNKNOWN)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN)
	PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)

	PORT_START("IN1") /* IN1 */
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_START1)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN1)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN2)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_COIN3)
	PORT_BITX(0x10, IP_ACTIVE_HIGH, IPT_START2, "Select Game", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
	PORT_BITX(0x20, IP_ACTIVE_HIGH, IPT_BUTTON1, "Abort", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)

	PORT_START("DSW1") /* DSW1 */
	PORT_DIPNAME(0x03, 0x01, "Right Coin")
	PORT_DIPSETTING(0x00, "*1")
	PORT_DIPSETTING(0x01, "*4")
	PORT_DIPSETTING(0x02, "*5")
	PORT_DIPSETTING(0x03, "*6")
	PORT_DIPNAME(0x0c, 0x00, "Language")
	PORT_DIPSETTING(0x00, "English")
	PORT_DIPSETTING(0x04, "French")
	PORT_DIPSETTING(0x08, "Spanish")
	PORT_DIPSETTING(0x0c, "German")
	PORT_DIPNAME(0x20, 0x00, DEF_STR(Coinage))
	PORT_DIPSETTING(0x00, "Normal")
	PORT_DIPSETTING(0x20, DEF_STR(Free_Play))
	PORT_DIPNAME(0xd0, 0x80, "Fuel units")
	PORT_DIPSETTING(0x00, "450")
	PORT_DIPSETTING(0x40, "600")
	PORT_DIPSETTING(0x80, "750")
	PORT_DIPSETTING(0xc0, "900")
	PORT_DIPSETTING(0x10, "1100")
	PORT_DIPSETTING(0x50, "1300")
	PORT_DIPSETTING(0x90, "1550")
	PORT_DIPSETTING(0xd0, "1800")

	/* The next one is a potentiometer */
	PORT_START("IN3")/* IN3 */
	PORT_ANALOGX(0xff, 0x00, IPT_PADDLE | IPF_REVERSE, 100, 10, 0, 255, OSD_KEY_UP, OSD_KEY_DOWN, OSD_JOY_UP, OSD_JOY_DOWN)

	/* Fake port - no memory handler maps it. Read by llander_artwork_mod_enabled()
	   at init to gate the artwork-mod ROM patches. llander (rev 2) set only. */
	PORT_START("DSW2")
	PORT_DIPNAME(0x01, 0x01, "Artwork Mod")
	PORT_DIPSETTING(0x00, DEF_STR(Off))
	PORT_DIPSETTING(0x01, DEF_STR(On))
	INPUT_PORTS_END

	INPUT_PORTS_START(llander1)
	PORT_START("IN0") /* IN0 */
	/* Bit 0 is VG_HALT, handled in the machine dependant part */
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNKNOWN)
	PORT_BITX(0x02, 0x02, IPT_DIPSWITCH_NAME | IPF_TOGGLE, DEF_STR(Service_Mode), OSD_KEY_F2, IP_JOY_NONE)
	PORT_DIPSETTING(0x02, DEF_STR(Off))
	PORT_DIPSETTING(0x00, DEF_STR(On))
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_TILT)
	/* Bits 3,4,5 unknown. Bit 6 is the 3KHz source - must default LOW so the
	   llander_IN0_r live clock bit controls it (see the llander set's IN0). */
	PORT_BIT(0x38, IP_ACTIVE_LOW, IPT_UNKNOWN)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN)
	PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_SERVICE, "Diagnostic Step", OSD_KEY_F1, IP_JOY_NONE)

	PORT_START("IN1") /* IN1 */
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_START1)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_COIN1)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN2)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_COIN3)
	PORT_BITX(0x10, IP_ACTIVE_HIGH, IPT_START2, "Select Game", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
	PORT_BITX(0x20, IP_ACTIVE_HIGH, IPT_BUTTON1, "Abort", IP_KEY_DEFAULT, IP_JOY_DEFAULT)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_2WAY)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT | IPF_2WAY)

	PORT_START("DSW1") /* DSW1 */
	PORT_DIPNAME(0x03, 0x01, "Right Coin")
	PORT_DIPSETTING(0x00, "*1")
	PORT_DIPSETTING(0x01, "*4")
	PORT_DIPSETTING(0x02, "*5")
	PORT_DIPSETTING(0x03, "*6")
	PORT_DIPNAME(0x0c, 0x00, "Language")
	PORT_DIPSETTING(0x00, "English")
	PORT_DIPSETTING(0x04, "French")
	PORT_DIPSETTING(0x08, "Spanish")
	PORT_DIPSETTING(0x0c, "German")
	PORT_DIPNAME(0x10, 0x00, DEF_STR(Coinage))
	PORT_DIPSETTING(0x00, "Normal")
	PORT_DIPSETTING(0x10, DEF_STR(Free_Play))
	PORT_DIPNAME(0xc0, 0x80, "Fuel units")
	PORT_DIPSETTING(0x00, "450")
	PORT_DIPSETTING(0x40, "600")
	PORT_DIPSETTING(0x80, "750")
	PORT_DIPSETTING(0xc0, "900")

	/* The next one is a potentiometer */
	PORT_START("IN3") /* IN3 */
	PORT_ANALOGX(0xff, 0x00, IPT_PADDLE | IPF_REVERSE, 100, 10, 0, 255, OSD_KEY_UP, OSD_KEY_DOWN, OSD_JOY_UP, OSD_JOY_DOWN)
	INPUT_PORTS_END

	// Lunar Lander ROMSETS
	ROM_START(llander)
	ROM_REGION(0x10000, REGION_CPU1, 0)
	// Vector ROM
	ROM_LOAD("034599-01.r3", 0x4800, 0x0800, CRC(355a9371) SHA1(6ecb40169b797d9eb623bcb17872f745b1bf20fa))
	ROM_LOAD("034598-01.np3", 0x5000, 0x0800, CRC(9c4ffa68) SHA1(eb4ffc289d254f699f821df3146aa2c6cd78597f))
	ROM_LOAD("034597-01.m3", 0x5800, 0x0800, CRC(ebb744f2) SHA1(e685b094c1261a351e4e82dfb487462163f136a4)) // Built from original Atari source code

	ROM_LOAD("034572-02.f1", 0x6000, 0x0800, CRC(b8763eea) SHA1(5a15eaeaf825ccdf9ce013a6789cf51da20f785c))
	ROM_LOAD("034571-02.de1", 0x6800, 0x0800, CRC(77da4b2f) SHA1(4be6cef5af38734d580cbfb7e4070fe7981ddfd6))
	ROM_LOAD("034570-01.c1", 0x7000, 0x0800, CRC(2724e591) SHA1(ecf4430a0040c227c896aa2cd81ee03960b4d641))
	ROM_LOAD("034569-02.b1", 0x7800, 0x0800, CRC(72837a4e) SHA1(9b21ba5e1518079c326ca6e15b9993e6c4483caa))
	ROM_RELOAD(0xf800, 0x0800)	/* for reset/interrupt vectors */
	// DVG PROM
	ROM_REGION(0x100, REGION_PROMS, 0)
	ROM_LOAD("034602-01.c8", 0x0000, 0x0100, CRC(97953db8) SHA1(8cbded64d1dd35b18c4d5cece00f77e7b2cab2ad))
	ROM_END

	ROM_START(llander1)
	ROM_REGION(0x10000, REGION_CPU1, 0)
	// Vector ROM
	ROM_LOAD("034599-01.r3", 0x4800, 0x0800, CRC(355a9371) SHA1(6ecb40169b797d9eb623bcb17872f745b1bf20fa))
	ROM_LOAD("034598-01.np3", 0x5000, 0x0800, CRC(9c4ffa68) SHA1(eb4ffc289d254f699f821df3146aa2c6cd78597f))
	ROM_LOAD("034597-01.m3", 0x5800, 0x0800, CRC(ebb744f2) SHA1(e685b094c1261a351e4e82dfb487462163f136a4)) // Built from original Atari source code

	ROM_LOAD("034572-01.f1", 0x6000, 0x0800, CRC(2aff3140) SHA1(4fc8aae640ce655417c11d9a3121aae9a1238e7c))
	ROM_LOAD("034571-01.de1", 0x6800, 0x0800, CRC(493e24b7) SHA1(125a2c335338ccabababef12fd7096ef4b605a31))
	ROM_LOAD("034570-01.c1", 0x7000, 0x0800, CRC(2724e591) SHA1(ecf4430a0040c227c896aa2cd81ee03960b4d641))
	ROM_LOAD("034569-01.b1", 0x7800, 0x0800, CRC(b11a7d01) SHA1(8f2935dbe04ee68815d69ea9e71853b5a145d7c3))
	ROM_RELOAD(0xf800, 0x0800)	/* for reset/interrupt vectors */
	// DVG PROM
	ROM_REGION(0x100, REGION_PROMS, 0)
	ROM_LOAD("034602-01.c8", 0x0000, 0x0100, CRC(97953db8) SHA1(8cbded64d1dd35b18c4d5cece00f77e7b2cab2ad))
	ROM_END

	AAE_DRIVER_BEGIN(llander, "llander", "Lunar Lander")
	AAE_DRIVER_ROM(rom_llander)
	AAE_DRIVER_FUNCS(init_llander, run_llander, end_llander)
	AAE_DRIVER_INPUT(input_ports_llander)
	AAE_DRIVER_SAMPLES(llander_samples)
	AAE_DRIVER_ART(nullptr)

	AAE_DRIVER_CPUS(
		AAE_CPU_ENTRY(CPU_M6502, 1512000, 100, 6, INT_TYPE_NMI, llander_interrupt,
			LlanderRead, LlanderWrite, nullptr, nullptr, nullptr, nullptr),
		AAE_CPU_NONE_ENTRY(),
		AAE_CPU_NONE_ENTRY(),
		AAE_CPU_NONE_ENTRY()
	)

	// Vector video
	AAE_DRIVER_VIDEO_CORE(40, 0, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
	AAE_DRIVER_SCREEN(1024, 768, 0, 1050, 0, 900)
	// Vector game => no raster decode/palette conversion
	AAE_DRIVER_RASTER_NONE()
	// No hiscore yet
	AAE_DRIVER_HISCORE_NONE()
	// Vector RAM base/size
	AAE_DRIVER_VECTORRAM(0x4000, 0x800)
	// No NVRAM handler
	AAE_DRIVER_NVRAM_NONE()
	AAE_DRIVER_LAYOUT_NONE()
	AAE_DRIVER_END()

	AAE_DRIVER_BEGIN(llander1, "llander1", "Lunar Lander (Revision 1)")
	AAE_DRIVER_ROM(rom_llander1)
	AAE_DRIVER_FUNCS(init_llander, run_llander, end_llander)
	// Rev-1 port list: rev-1 DSW bits, no "Artwork Mod" switch (the artwork-mod
	// patches target rev-2 addresses).
	AAE_DRIVER_INPUT(input_ports_llander1)
	AAE_DRIVER_SAMPLES(llander_samples)
	AAE_DRIVER_ART(nullptr)
	AAE_DRIVER_CPUS(
		AAE_CPU_ENTRY(CPU_M6502, 1512000, 100, 6, INT_TYPE_NMI, llander_interrupt,
			LlanderRead, LlanderWrite, nullptr, nullptr, nullptr, nullptr),
		AAE_CPU_NONE_ENTRY(),
		AAE_CPU_NONE_ENTRY(),
		AAE_CPU_NONE_ENTRY()
	)
	// Vector video
	AAE_DRIVER_VIDEO_CORE(40, 0, VIDEO_TYPE_VECTOR | VECTOR_USES_BW, ORIENTATION_DEFAULT)
	AAE_DRIVER_SCREEN(1024, 768, 0, 1050, 0, 900)
	// Vector game => no raster decode/palette conversion
	AAE_DRIVER_RASTER_NONE()
	// No hiscore yet
	AAE_DRIVER_HISCORE_NONE()
	// Vector RAM base/size
	AAE_DRIVER_VECTORRAM(0x4000, 0x800)
	// No NVRAM handler
	AAE_DRIVER_NVRAM_NONE()
	AAE_DRIVER_LAYOUT_NONE()
	AAE_DRIVER_END()

	AAE_REGISTER_DRIVER(llander)
	AAE_REGISTER_DRIVER(llander1)
	//////////////////  END OF MAIN PROGRAM /////////////////////////////////////////////