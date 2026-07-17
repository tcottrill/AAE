/***********************************************************

     Astrocade custom 'IO' chip sound chip driver
     Frank Palazzolo

     Ported from MAME 0.90 to AAE (Another Arcade Emulator)

     Three square-wave tone generators with a shared master
     oscillator, vibrato, and a noise source (Space Zap, Gorf,
     Wizard of Wor, Robby Roto, ...).

     Differences from the MAME original:
     - No MachineSound wrapper; astrocade_sh_start() takes an
       astrocade_interface* directly.
     - MAME's pull-model stream callback is replaced by the AAE
       push model: register writes catch the chip up to the CPU's
       current position in the frame (cpu_scale_by_cycles), and
       astrocade_sh_update() fills the frame tail and pushes the
       buffer once per video frame via stream_update().
     - MAME 0.90 delivers the reg-8 "Sound Block Transfer" through
       16-bit port decoding (Z80 B register on A8-A15 during OTIR).
       AAE's Z80 core dispatches 8-bit port addresses, so case 8
       is handled 0.57-style by reading the live B register via
       cpu_z80::GetBC().
     - astrocade_sound1_w/astrocade_sound2_w use the AAE z80PortWrite
       signature; map them with PORT_ADDR(0x10, 0x18, ...) /
       PORT_ADDR(0x50, 0x58, ...). The register offset is decoded
       from the port's low nibble.

 ***********************************************************/
#ifndef ASTROCADE_SOUND_H
#define ASTROCADE_SOUND_H

#include "deftypes.h"   /* UINT8/UINT16, z80PortWrite */

#define MAX_ASTROCADE_CHIPS 2   /* max number of emulated chips */

struct astrocade_interface
{
	int num;                                /* total number of sound chips in the machine */
	int baseclock;                          /* astrocade clock rate (Z80 clock, 1789773) */
	int mixing_level[MAX_ASTROCADE_CHIPS];  /* master volume, 0..255 */
};

/* Lifecycle */
int  astrocade_sh_start(const struct astrocade_interface* intf_in);
void astrocade_sh_stop(void);
void astrocade_sh_update(void);   /* call once per video frame from the driver's run_* */

/* Raw register write: offset 0-7 = registers, 8 = block transfer (reads Z80 B) */
void astrocade_sound_w(int num, int offset, int data);

/* Z80 port write handlers - wire into the driver's port map:
     PORT_ADDR(0x10, 0x18, astrocade_sound1_w)
     PORT_ADDR(0x50, 0x58, astrocade_sound2_w)   */
void astrocade_sound1_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);
void astrocade_sound2_w(UINT16 port, UINT8 data, struct z80PortWrite* pPW);

#endif
