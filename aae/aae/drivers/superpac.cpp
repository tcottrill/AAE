/***************************************************************************

  superpac.cpp  --  AAE driver for Super Pac-Man and Pac & Pal

  Hardware family: Namco Super Pac-Man board (dual 6809, custom I/O,
                   Namco 8-voice sound)

  Games covered:
    superpac  - Super Pac-Man (US)             1982 Namco
    superpcm  - Super Pac-Man (Midway license) 1982 Namco / Bally Midway
    pacnpal   - Pac & Pal                      1983 Namco
    pacnchmp  - Pac-Man & Chomp Chomp          1983 Namco

  CPU 1 memory map:
    0000-03ff  video RAM  (tile indices, 32x32 logical but 28x36 visible)
    0400-07ff  color RAM
    0800-0f7f  general RAM
    0f80-0fff  sprite RAM area 1  (sprite index + color)
    1000-177f  general RAM
    1780-17ff  sprite RAM area 2  (x/y position)
    1800-1f7f  general RAM
    1f80-1fff  sprite RAM area 3  (high y bit, flip flags, size flags)
    2000       watchdog timer NOP
    4040-43ff  RAM shared with CPU 2  (sound CPU)
    4800-480f  custom I/O chip 1
    4810-481f  custom I/O chip 2
    5000       reset CPU 2
    5002-5003  CPU 1 IRQ enable
    5008-5009  sound enable
    500a-500b  CPU 2 halt/run
    8000       watchdog timer NOP
    c000-ffff  ROM  (superpac/superpcm: 2 x 8K; pacnpal: 3 x 8K from a000)

  CPU 2 memory map (Super Pac-Man):
    0000-003f  Namco sound registers
    0040-03ff  RAM shared with CPU 1
    f000-ffff  ROM  (4K sound ROM)
    CPU 2 runs free -- no interrupt on superpac

  CPU 2 memory map (Pac & Pal):
    0000-003f  Namco sound registers
    0040-03ff  RAM shared with CPU 1
    2000-2001  CPU 2 IRQ enable
    2006-2007  sound enable
    f000-ffff  ROM  (4K sound ROM)
    CPU 2 takes IRQ from VBLANK

  Screen: 28 tiles wide x 36 tiles tall (224 x 288 pixels), rotated 90 degrees
  Video RAM layout is 32x32; 16 locations do not map to any screen position.

  Converted from MAME 0.36 superpac driver (Aaron Giles) for AAE.

***************************************************************************/

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

#include "aae_mame_driver.h"
#include "superpac.h"
#include "old_mame_raster.h"
#include "driver_registry.h"
#include "namco.h"
#include "timer.h"

#pragma warning( disable : 4838 4003 )


// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------

/* shared RAM between CPU1 and CPU2; pointer set in init_superpac() to point
   into CPU0 address space at 0x4040 */
static unsigned char superpac_sharedram_buf[0x3c0];
unsigned char* superpac_sharedram = superpac_sharedram_buf;

/* custom I/O chip register banks; pointers set in init_superpac() */
static unsigned char superpac_customio_1_buf[16];
static unsigned char superpac_customio_2_buf[16];
unsigned char* superpac_customio_1 = superpac_customio_1_buf;
unsigned char* superpac_customio_2 = superpac_customio_2_buf;

static unsigned char interrupt_enable_1 = 0;
static unsigned char interrupt_enable_2 = 0;  /* pacnpal CPU2 only */

/* credit counter for custom I/O emulation */
static int credits = 0;

/* raw coin/start edge-detection state for superpac customio emulation */
static int coin1, coin2, start1, start2;

/* coin/credit ratio tables for superpac -- indexed by DSW1 bits 2:0 */
static const int superpac_crednum[] = { 1, 2, 3, 6, 7, 1, 3, 1 };
static const int superpac_credden[] = { 1, 1, 1, 1, 1, 2, 2, 3 };


// ---------------------------------------------------------------------------
// Namco sound interface -- 8 voices, matches Super Pac-Man hardware
// ---------------------------------------------------------------------------

static struct namco_interface namco_interface =
{
    23920,          /* sample rate (approximate value matching hardware) */
    8,              /* number of voices */
    220,            /* playback volume */
    REGION_SOUND1   /* waveform data memory region */
};


// ---------------------------------------------------------------------------
// Machine init
// ---------------------------------------------------------------------------

void superpac_init_machine(void)
{
    /* reset coin/start edge detection and credit counter */
    coin1 = coin2 = start1 = start2 = credits = 0;

    /* interrupts disabled until the game explicitly enables them */
    interrupt_enable_1 = interrupt_enable_2 = 0;
}


// ---------------------------------------------------------------------------
// Shared RAM handlers
// CPU2 sits in a tight polling loop between sound jobs; the idle-loop
// spinuntil_int hook is left commented out as in the mappy driver -- it can
// be re-enabled later if host CPU usage is a concern.
// ---------------------------------------------------------------------------

READ_HANDLER(superpac_sharedram_r)
{
    return superpac_sharedram[address];
}

READ_HANDLER(superpac_sharedram_r2)
{
    /* superpac CPU2 idle-loop detection would go here if needed */
    return superpac_sharedram[address];
}

WRITE_HANDLER(superpac_sharedram_w)
{
    superpac_sharedram[address] = data;
}

/* pacnpal CPU2 uses a separate shared RAM handler so we can add
   pac & pal specific idle-loop detection without touching the superpac path */
READ_HANDLER(pacnpal_sharedram_r2)
{
    return superpac_sharedram[address];
}

WRITE_HANDLER(pacnpal_sharedram_w2)
{
    superpac_sharedram[address] = data;
}


// ---------------------------------------------------------------------------
// Custom I/O chip write
// Writing to offset 8 selects the operating mode used by the read handler.
// The chip is a write-to-configure, read-to-poll device.
// ---------------------------------------------------------------------------

WRITE_HANDLER(superpac_customio_w_1)
{
    superpac_customio_1[address] = data;
}

WRITE_HANDLER(superpac_customio_w_2)
{
    superpac_customio_2[address] = data;
}


// ---------------------------------------------------------------------------
// superpac_update_credits
// Called from the custom I/O read handler each time the game polls it.
// The custom I/O chip tracks raw coin/start pulses and converts them into a
// credit count using the coin ratio stored in DSW1 bits 2:0.
// ---------------------------------------------------------------------------

static void superpac_update_credits(void)
{
    int val  = readinputport(3) & 0x0f;   /* low nibble: coin slots */
    int temp;

    /* coin slot 1 -- edge detect (active high) */
    if (val & 1)
    {
        if (!coin1) { credits++; coin1 = 1; }
    }
    else
    {
        coin1 = 0;
    }

    /* coin slot 2 -- edge detect */
    if (val & 2)
    {
        if (!coin2) { credits++; coin2 = 1; }
    }
    else
    {
        coin2 = 0;
    }

    temp = readinputport(1) & 7;          /* DSW1 coin-A setting */
    val  = readinputport(3) >> 4;         /* high nibble: start buttons */

    /* 1P start -- deduct credits per ratio */
    if (val & 1)
    {
        if (!start1 && credits >= superpac_credden[temp])
        {
            credits -= superpac_credden[temp];
            start1 = 1;
        }
    }
    else
    {
        start1 = 0;
    }

    /* 2P start -- deducts double the per-credit ratio */
    if (val & 2)
    {
        if (!start2 && credits >= 2 * superpac_credden[temp])
        {
            credits -= 2 * superpac_credden[temp];
            start2 = 1;
        }
    }
    else
    {
        start2 = 0;
    }
}


// ---------------------------------------------------------------------------
// Custom I/O read -- chip 1 (superpac)
// Mode is set by a write to offset 8:
//   mode 4: normal gameplay  -- returns coins, credits, joystick, buttons
//   mode 8: test mode        -- returns 0 for addresses 9-15, resets credits
//   other:  return stored value
// ---------------------------------------------------------------------------

READ_HANDLER(superpac_customio_r_1)
{
    //int val;
    int temp;
    int mode = superpac_customio_1[8];

    superpac_update_credits();

    if (mode == 4)
    {
        switch (address)
        {
        case 0:     /* high BCD digit of displayed credit count */
            temp = readinputport(1) & 7;
            return (credits * superpac_crednum[temp] / superpac_credden[temp]) / 10;

        case 1:     /* low BCD digit of displayed credit count */
            temp = readinputport(1) & 7;
            return (credits * superpac_crednum[temp] / superpac_credden[temp]) % 10;

        case 4:     /* player 1 joystick (4 direction bits) */
            return readinputport(2) & 0x0f;

        case 5:     /* player 1 buttons + high nibble */
            return readinputport(2) >> 4;

        case 6:
        case 7:
            return 0x0f;    /* unused -- pulled high on hardware */

        default:
            return superpac_customio_1[address];
        }
    }
    else if (mode == 8)
    {
        /* test mode: clear credits and return 0 for the diagnostic range */
        credits = 0;
        if (address >= 9 && address <= 15)
            return 0;
    }

    return superpac_customio_1[address];
}


// ---------------------------------------------------------------------------
// Custom I/O read -- chip 2 (superpac)
// Mode 9: normal gameplay -- DIP switches and start/coin inputs
// Mode 8: test mode
// ---------------------------------------------------------------------------

READ_HANDLER(superpac_customio_r_2)
{
    //int val;
    int mode = superpac_customio_2[8];

    if (mode == 9)
    {
        switch (address)
        {
        case 0:     /* DSW1 low nibble */
            return readinputport(1) & 0x0f;

        case 1:     /* DSW1 high nibble */
            return readinputport(1) >> 4;

        case 2:     /* unused */
            return 0;

        case 3:     /* DSW0 low nibble */
            return readinputport(0) & 0x0f;

        case 4:     /* DSW0 high nibble */
            return readinputport(0) >> 4;

        case 5:     /* unused */
            return 0;

        case 6:     /* start buttons (high nibble of port 3, bits 2:3 only) */
            return (readinputport(3) >> 4) & 0x0c;

        case 7:     /* unused */
            return 0;

        default:
            return superpac_customio_2[address];
        }
    }
    else if (mode == 8)
    {
        credits = 0;
        if (address >= 9 && address <= 15)
            return 0;
    }

    return superpac_customio_2[address];
}


// ---------------------------------------------------------------------------
// Custom I/O read -- chip 1 (pacnpal)
// Pac & Pal uses modes 1 and 3 for chip 1 (gameplay) and mode 4 for chip 2.
// The mode-1/3 distinction: mode 3 appears during attract/demo; both return
// the same live data.
// ---------------------------------------------------------------------------

READ_HANDLER(pacnpal_customio_r_1)
{
    int val, temp;
    int mode = superpac_customio_1[8];

    /* coin/credit ratio tables for pacnpal (same as superpac) */
    static const int crednum[] = { 1, 2, 3, 6, 7, 1, 3, 1 };
    static const int credden[] = { 1, 1, 1, 1, 1, 2, 2, 3 };

    if (mode == 1 || mode == 3)
    {
        switch (address)
        {
        case 0:     /* coin slots, low nibble of port 3 */
            val = readinputport(3) & 0x0f;
            return val;

        case 1:     /* player 1 joystick */
            return readinputport(2) & 0x0f;

        case 2:     /* unused */
            return 0;

        case 3:     /* start buttons + fire button OR */
            val  = (readinputport(3) >> 4) & 3;
            val |= val << 2;                /* replicate for both halves */
            val |= readinputport(2) >> 4;   /* fire button in high nibble */
            return val;

        case 4:
        case 5:
        case 6:
        case 7:
            return 0x0f;    /* unused -- pulled high */

        default:
            return superpac_customio_1[address];
        }
    }
    else if (mode == 4)
    {
        /* mode 4 used during credit display */
        switch (address)
        {
        case 0:
            temp = readinputport(1) & 7;
            return (credits * crednum[temp] / credden[temp]) / 10;

        case 1:
            temp = readinputport(1) & 7;
            return (credits * crednum[temp] / credden[temp]) % 10;

        case 4:     return readinputport(2) & 0x0f;
        case 5:     return readinputport(2) >> 4;
        case 6:
        case 7:     return 0x0f;

        default:
            return superpac_customio_1[address];
        }
    }
    else if (mode == 8)
    {
        credits = 0;
        if (address >= 9 && address <= 15)
            return 0;
    }

    return superpac_customio_1[address];
}


// ---------------------------------------------------------------------------
// Custom I/O read -- chip 2 (pacnpal)
// Pac & Pal uses mode 3 for normal gameplay and mode 8 for test.
// ---------------------------------------------------------------------------

READ_HANDLER(pacnpal_customio_r_2)
{
    int mode = superpac_customio_2[8];

    if (mode == 3)
    {
        switch (address)
        {
        case 0:
        case 1:
        case 2:
        case 3:
            return 0;

        case 4:     /* DSW0 low nibble */
            return readinputport(0) & 0x0f;

        case 5:     /* DSW1 high nibble */
            return readinputport(1) >> 4;

        case 6:     /* DSW1 low nibble */
            return readinputport(1) & 0x0f;

        case 7:     /* start buttons (bits 2:3 of coin port high nibble) */
            return (readinputport(3) >> 4) & 0x0c;

        default:
            return superpac_customio_2[address];
        }
    }
    else if (mode == 8)
    {
        credits = 0;
        if (address >= 9 && address <= 15)
            return 0;
    }

    return superpac_customio_2[address];
}


// ---------------------------------------------------------------------------
// Interrupt enable / fire
// The hardware uses the address bus (offset) to set enable state, not the
// data bus.  Writing to 0x5002 disables, 0x5003 enables (offset & 1).
// ---------------------------------------------------------------------------

WRITE_HANDLER(superpac_interrupt_enable_1_w)
{
    interrupt_enable_1 = (unsigned char)(address & 1);
}

/* pacnpal CPU2 interrupt enable lives at 0x2000-0x2001 in CPU2 space */
WRITE_HANDLER(superpac_interrupt_enable_2_w)
{
    interrupt_enable_2 = (unsigned char)(address & 1);
}

/* CPU1 VBLANK interrupt -- fires 60 times per second if enabled */
void superpac_interrupt_1(void)
{
    if (interrupt_enable_1)
        cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

/* CPU2 VBLANK interrupt -- pacnpal only; superpac CPU2 runs free */
void superpac_interrupt_2(void)
{
    if (interrupt_enable_2)
        cpu_do_int_imm(CPU1, INT_TYPE_INT);
}

/* CPU2 halt / run control.  Mapped at 0x500a (halt) and 0x500b (run). */
WRITE_HANDLER(superpac_cpu_enable_w)
{
    cpu_enable(CPU1, address & 1);
}

/* Pulse-reset CPU2.  Written at 0x5000; the CPU2 /RESET line is strobed. */
WRITE_HANDLER(superpac_reset_2_w)
{
    //cpu_set_reset_line(CPU1, PULSE_LINE);
	cpu_reset(CPU1);
}


// ---------------------------------------------------------------------------
// Sound write-through wrappers
// Namco sound registers live at 0x0000-0x003f in CPU2 address space.
// Sound enable is at 0x5008-0x5009 in CPU1 space.
// ---------------------------------------------------------------------------

WRITE_HANDLER(superpac_sound_w)
{
    mappy_sound_w(address, data);
}

WRITE_HANDLER(superpac_sound_enable_w)
{
    /* address & 1: 0 = disable sound, 1 = enable sound */
    mappy_sound_enable_w(address, data);
}


// ---------------------------------------------------------------------------
// Video RAM / color RAM write-through (mark dirty buffer for screenrefresh)
// ---------------------------------------------------------------------------

WRITE_HANDLER(superpac_videoram_w)
{
    if (videoram[address] != (unsigned char)data)
    {
        dirtybuffer[address] = 1;
        videoram[address] = (unsigned char)data;
    }
}

WRITE_HANDLER(superpac_colorram_w)
{
    if (colorram[address] != (unsigned char)data)
    {
        dirtybuffer[address] = 1;
        colorram[address] = (unsigned char)data;
    }
}


// ---------------------------------------------------------------------------
// Color PROM conversion
//
// The Super Pac-Man board has:
//   32 x 8-bit palette PROM   (at REGION_PROMS + 0x0000)
//   256 x 4-bit char color LUT PROM (at REGION_PROMS + 0x0020, 256 bytes)
//   256 x 4-bit sprite color LUT PROM (at REGION_PROMS + 0x0120, 256 bytes)
//
// Palette PROM bit layout (same resistor network as Dig Dug / Mappy family):
//   bit 7 -- 220 ohm -- BLUE
//         -- 470 ohm -- BLUE
//         -- 220 ohm -- GREEN
//         -- 470 ohm -- GREEN
//         -- 1k  ohm -- GREEN
//         -- 220 ohm -- RED
//         -- 470 ohm -- RED
//   bit 0 -- 1k  ohm -- RED
//
// The palette PROM is stored in reverse order (entry 0 is the highest index).
// Char lookup uses palette indices 0-15 (low 4 bits of the LUT byte).
// Sprite lookup uses palette indices 31 down to 16 (0x1f - low 4 bits).
// ---------------------------------------------------------------------------

void superpac_vh_convert_color_prom(unsigned char*       palette,
                                     unsigned char*       colortable,
                                     const unsigned char* color_prom)
{
    int i;

    /* decode 32 palette entries from the palette PROM */
    for (i = 0; i < 32; i++)
    {
        int bit0, bit1, bit2;
        unsigned char entry = color_prom[31 - i]; /* PROM is stored reversed */

        /* RED: bits 0, 1, 2 (resistor weighted 1k, 470, 220) */
        bit0 = (entry >> 0) & 1;
        bit1 = (entry >> 1) & 1;
        bit2 = (entry >> 2) & 1;
        palette[3 * i + 0] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

        /* GREEN: bits 3, 4, 5 */
        bit0 = (entry >> 3) & 1;
        bit1 = (entry >> 4) & 1;
        bit2 = (entry >> 5) & 1;
        palette[3 * i + 1] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;

        /* BLUE: bits 6, 7 (only 2 bits; bit 0 of BLUE is always 0) */
        bit0 = 0;
        bit1 = (entry >> 6) & 1;
        bit2 = (entry >> 7) & 1;
        palette[3 * i + 2] = 0x21 * bit0 + 0x47 * bit1 + 0x97 * bit2;
    }

    /* character color lookup table: 64 color sets x 4 pixels each.
       The LUT PROM byte gives a palette index in bits 3:0. */
    for (i = 0; i < 64 * 4; i++)
        colortable[i] = (unsigned char)(color_prom[i + 32] & 0x0f);

    /* sprite color lookup table: 64 color sets x 4 pixels each.
       Sprites use the upper half of the palette (indices 16-31), so the
       index is computed as 0x1f minus the raw PROM nibble. */
    for (i = 0; i < 64 * 4; i++)
        colortable[64 * 4 + i] = (unsigned char)(0x1f - (color_prom[i + 32 + 256] & 0x0f));
}


// ---------------------------------------------------------------------------
// Sprite drawing helper
//
// Sprites are 2bpp, 16x16, with 64 color sets.
// Transparency color is pen 16 (first entry of the sprite color range).
// ---------------------------------------------------------------------------

static void superpac_draw_sprite(struct osd_bitmap* dest,
                                  unsigned int code, unsigned int color,
                                  int flipx, int flipy,
                                  int sx, int sy)
{
    drawgfx(dest, Machine->gfx[1], code, color, flipx, flipy, sx, sy,
            &Machine->drv->visible_area, TRANSPARENCY_COLOR, 16);
     
}


// ---------------------------------------------------------------------------
// superpac_vh_screenrefresh
//
// The screen is 28 tiles wide x 36 tiles tall but video RAM is laid out as a
// 32x32 grid.  The conversion from memory offset to screen position is:
//
//   offset = my*32 + mx    (my = row 0..31, mx = col 0..31)
//
//   if my <= 1:             screen (sx, sy) = (my+34, mx-2)
//   elif my >= 30:          screen (sx, sy) = (my-30, mx-2)
//   else:                   screen (sx, sy) = (mx+2,  my-2)
//
// Rows 0-1 and 30-31 wrap around to the sides of the screen.
// drawgfx() silently clips anything outside the visible area.
//
// Sprites: up to 64 sprites, 3 attribute bytes each spread across spriteram,
// spriteram_2, and spriteram_3.  Sprites can be normal size (16x16),
// 2x horizontal (32x16), 2x vertical (16x32), or 2x both ways (32x32).
// ---------------------------------------------------------------------------

void superpac_vh_screenrefresh(struct osd_bitmap* bitmap, int full_refresh)
{
    int offs;


    /* for every character in the Video RAM, check if it has been modified */
    /* since last time and update it accordingly. */
    for (offs = videoram_size - 1; offs >= 0; offs--)
    {
        if (dirtybuffer[offs])
        {
            int sx, sy, mx, my;


            dirtybuffer[offs] = 0;

            /* Even if Super Pac-Man's screen is 28x36, the memory layout is 32x32. We therefore */
            /* have to convert the memory coordinates into screen coordinates. */
            /* Note that 32*32 = 1024, while 28*36 = 1008: therefore 16 bytes of Video RAM */
            /* don't map to a screen position. We don't check that here, however: range */
            /* checking is performed by drawgfx(). */

             mx = offs / 32;
             my = offs % 32;
          
            if (mx <= 1)
            {
                sx = 29 - my;
                sy = mx + 34;
            }
            else if (mx >= 30)
            {
                sx = 29 - my;
                sy = mx - 30;
            }
            else
            {
                sx = 29 - mx;
                sy = my + 2;
            }

            drawgfx(tmpbitmap, Machine->gfx[0],
                videoram[offs],
                colorram[offs],
                0, 0, 8 * sx, 8 * sy,
                &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
        }
    }

    /* copy the character mapped graphics */
    copybitmap(bitmap, tmpbitmap, 0, 0, 0, 0, &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);

    /* Draw the sprites. */
    for (offs = 0; offs < spriteram_size; offs += 2)
    {
        /* is it on? */

        if ((spriteram_3[offs + 1] & 2) == 0)
        {
            int sprite = spriteram[offs];
            int color = spriteram[offs + 1];
            int x = spriteram_2[offs] - 17;
            int y = (spriteram_2[offs + 1] - 40) + 0x100 * (spriteram_3[offs + 1] & 1);
            int flipx = spriteram_3[offs] & 2;
            int flipy = spriteram_3[offs] & 1;

            switch (spriteram_3[offs] & 0x0c)
            {
            case 0:		/* normal size */
                superpac_draw_sprite(bitmap, sprite, color, flipx, flipy, x, y);
                break;

            case 4:		/* 2x vertical */
                sprite &= ~1;
                if (!flipy)
                {
                    superpac_draw_sprite(bitmap, sprite, color, flipx, flipy, x, y);
                    superpac_draw_sprite(bitmap, 1 + sprite, color, flipx, flipy, x, 16 + y);
                }
                else
                {
                    superpac_draw_sprite(bitmap, sprite, color, flipx, flipy, x, 16 + y);
                    superpac_draw_sprite(bitmap, 1 + sprite, color, flipx, flipy, x, y);
                }
                break;

            case 8:		/* 2x horizontal */
                sprite &= ~2;
                if (!flipx)
                {
                    superpac_draw_sprite(bitmap, 2 + sprite, color, flipx, flipy, x, y);
                    superpac_draw_sprite(bitmap, sprite, color, flipx, flipy, 16 + x, y);
                }
                else
                {
                    superpac_draw_sprite(bitmap, sprite, color, flipx, flipy, x, y);
                    superpac_draw_sprite(bitmap, 2 + sprite, color, flipx, flipy, 16 + x, y);
                }
                break;

            case 12:		/* 2x both ways */
                sprite &= ~3;
                if (!flipy && !flipx)
                {
                    superpac_draw_sprite(bitmap, 2 + sprite, color, flipx, flipy, x, y);
                    superpac_draw_sprite(bitmap, 3 + sprite, color, flipx, flipy, x, 16 + y);
                    superpac_draw_sprite(bitmap, sprite, color, flipx, flipy, 16 + x, y);
                    superpac_draw_sprite(bitmap, 1 + sprite, color, flipx, flipy, 16 + x, 16 + y);
                }
                else if (flipy && flipx)
                {
                    superpac_draw_sprite(bitmap, 1 + sprite, color, flipx, flipy, x, y);
                    superpac_draw_sprite(bitmap, sprite, color, flipx, flipy, x, 16 + y);
                    superpac_draw_sprite(bitmap, 3 + sprite, color, flipx, flipy, 16 + x, y);
                    superpac_draw_sprite(bitmap, 2 + sprite, color, flipx, flipy, 16 + x, 16 + y);
                }
                else if (flipx)
                {
                    superpac_draw_sprite(bitmap, sprite, color, flipx, flipy, x, y);
                    superpac_draw_sprite(bitmap, 1 + sprite, color, flipx, flipy, x, 16 + y);
                    superpac_draw_sprite(bitmap, 2 + sprite, color, flipx, flipy, 16 + x, y);
                    superpac_draw_sprite(bitmap, 3 + sprite, color, flipx, flipy, 16 + x, 16 + y);
                }
                else /* flipy */
                {
                    superpac_draw_sprite(bitmap, 3 + sprite, color, flipx, flipy, x, y);
                    superpac_draw_sprite(bitmap, 2 + sprite, color, flipx, flipy, x, 16 + y);
                    superpac_draw_sprite(bitmap, 1 + sprite, color, flipx, flipy, 16 + x, y);
                    superpac_draw_sprite(bitmap, sprite, color, flipx, flipy, 16 + x, 16 + y);
                }
                break;
            }
        }
    }
}


// ---------------------------------------------------------------------------
// GFX layouts
//
// Super Pac-Man has separate ROM regions for chars and sprites (unlike Mappy
// which packs both into REGION_GFX1).
//
// Char layout (SPV-1.3C / sp1.6 -- 4K):
//   8x8 tiles, 2bpp, 256 tiles
//   Pixels stored in groups of 4 per byte (bits 3:0 and 7:4), two bytes per
//   row, reading left-to-right as { 0, 1, 2, 3, 8*8, 8*8+1, 8*8+2, 8*8+3 }
//   Rows in normal top-to-bottom order (unlike Mappy's 90-degree rotation).
//
// Sprite layout (SPV-2.3F -- 8K):
//   16x16 sprites, 2bpp, 128 sprites
//   Same pixel packing as chars; rows in top-to-bottom order.
// ---------------------------------------------------------------------------

static struct GfxLayout charlayout =
{
    8,8,                                           /* 8*8 characters */
    256,                                           /* 256 characters */
    2,                                             /* 2 bits per pixel */
    { 0, 4 },                                      /* the two bitplanes for 4 pixels are packed into one byte */
    { 7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },    /* characters are rotated 90 degrees */
    { 8 * 8 + 0, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3, 0, 1, 2, 3 },    /* bits are packed in groups of four */
    16 * 8                                           /* every char takes 16 bytes */
};


/* SUPERPAC -- ROM SPV-2.3F (8K) */
static struct GfxLayout spritelayout =
{
    16,16,                                         /* 16*16 sprites */
    128,                                           /* 128 sprites */
    2,                                             /* 2 bits per pixel */
    { 0, 4 },                                      /* the two bitplanes for 4 pixels are packed into one byte */
    { 39 * 8, 38 * 8, 37 * 8, 36 * 8, 35 * 8, 34 * 8, 33 * 8, 32 * 8,
            7 * 8, 6 * 8, 5 * 8, 4 * 8, 3 * 8, 2 * 8, 1 * 8, 0 * 8 },
    { 0, 1, 2, 3, 8 * 8, 8 * 8 + 1, 8 * 8 + 2, 8 * 8 + 3,
            16 * 8 + 0, 16 * 8 + 1, 16 * 8 + 2, 16 * 8 + 3, 24 * 8 + 0, 24 * 8 + 1, 24 * 8 + 2, 24 * 8 + 3 },
    64 * 8                                           /* every sprite takes 64 bytes */
};

struct GfxDecodeInfo superpac_gfxdecodeinfo[] =
{
    { REGION_GFX1, 0, &charlayout,   0,     64 },
    { REGION_GFX2, 0, &spritelayout, 64*4,  64 },
    { -1 }
};


// ---------------------------------------------------------------------------
// run_* callbacks -- called every frame by the AAE emulator core
// ---------------------------------------------------------------------------

void run_superpac(void)
{
    watchdog_reset_w(0, 0, 0);
    superpac_vh_screenrefresh(Machine->scrbitmap, 0);
    namco_sh_update();
}

/* pacnpal uses the same run loop as superpac */
void run_pacnpal(void)
{
    watchdog_reset_w(0, 0, 0);
    superpac_vh_screenrefresh(Machine->scrbitmap, 0);
    namco_sh_update();
}


// ---------------------------------------------------------------------------
// init_superpac -- pointer fixup and subsystem startup
//
// Video RAM is 0x0000-0x03ff (1K), color RAM is 0x0400-0x07ff.
// Three sprite RAM banks are interleaved with general RAM in 0x0800-0x1fff.
// Shared RAM and custom I/O are in CPU1 space at 0x4040 and 0x4800.
// ---------------------------------------------------------------------------

int init_superpac(void)
{
    /* tile and color RAM */
    videoram      = &Machine->memory_region[CPU0][0x0000];
    videoram_size = 0x0400;
    colorram      = &Machine->memory_region[CPU0][0x0400];

    /* sprite attribute banks */
    spriteram      = &Machine->memory_region[CPU0][0x0f80]; /* sprite index + color */
    spriteram_size = 0x0080;
    spriteram_2    = &Machine->memory_region[CPU0][0x1780]; /* x/y position */
    spriteram_3    = &Machine->memory_region[CPU0][0x1f80]; /* high y, flip, size */

    /* shared RAM: CPU1 sees it at 0x4040, CPU2 sees it at 0x0040.
       Both point to the same buffer so the sound CPU can communicate. */
    superpac_sharedram  = &Machine->memory_region[CPU0][0x4040];
    superpac_customio_1 = &Machine->memory_region[CPU0][0x4800];
    superpac_customio_2 = &Machine->memory_region[CPU0][0x4810];

    /* start sound CPU halted; main CPU enables it via superpac_cpu_enable_w */
    cpu_enable(CPU1, 0);

    superpac_init_machine();

    /* generic_vh_start allocates tmpbitmap and dirtybuffer */
    if (generic_vh_start() != 0)
        return 0;

    namco_sh_start(&namco_interface);

    interrupt_enable_1 = 0;
    interrupt_enable_2 = 0;
    cpu_setOPbaseoverride(nullptr);

    return 1;
}

void end_superpac(void)
{
    generic_vh_stop();
}


// ---------------------------------------------------------------------------
// init_pacnpal -- same board, slightly different ROM map and CPU2 behavior
//
// pacnpal loads ROMs at 0xa000, 0xc000, and 0xe000.  CPU2 takes interrupts
// and has sound enable at 0x2006-0x2007 (like Mappy) rather than at 0x5008.
// ---------------------------------------------------------------------------

int init_pacnpal(void)
{
    videoram      = &Machine->memory_region[CPU0][0x0000];
    videoram_size = 0x0400;
    colorram      = &Machine->memory_region[CPU0][0x0400];

    spriteram      = &Machine->memory_region[CPU0][0x0f80];
    spriteram_size = 0x0080;
    spriteram_2    = &Machine->memory_region[CPU0][0x1780];
    spriteram_3    = &Machine->memory_region[CPU0][0x1f80];

    superpac_sharedram  = &Machine->memory_region[CPU0][0x4040];
    superpac_customio_1 = &Machine->memory_region[CPU0][0x4800];
    superpac_customio_2 = &Machine->memory_region[CPU0][0x4810];

    cpu_enable(CPU1, 0);

    superpac_init_machine();

    if (generic_vh_start() != 0)
        return 0;

    namco_sh_start(&namco_interface);

    interrupt_enable_1 = 0;
    interrupt_enable_2 = 0;
    cpu_setOPbaseoverride(nullptr);

    return 1;
}

void end_pacnpal(void)
{
    generic_vh_stop();
}


// ---------------------------------------------------------------------------
// Memory maps
// ---------------------------------------------------------------------------

/*
 * CPU1 read map -- superpac / superpcm
 * ROM at c000-ffff (two 8K ROMs).
 * 0000-1fff: general RAM (tile, color, sprite, work RAM) -- plain MRA_RAM.
 * 4040-43ff: shared RAM with sound CPU.
 * 4800-481f: custom I/O chips.
 * a000-bfff: open bus -- reads as 0xff (MRA_ROM covers it harmlessly since
 *            the ROM region is 64K but ROMs only load at c000-ffff).
 */
MEM_READ(superpac_readmem_cpu1)
MEM_ADDR(0xc000, 0xffff, MRA_ROM)
MEM_ADDR(0x4800, 0x480f, superpac_customio_r_1)
MEM_ADDR(0x4810, 0x481f, superpac_customio_r_2)
MEM_ADDR(0x4040, 0x43ff, superpac_sharedram_r)
MEM_ADDR(0x0000, 0x1fff, MRA_RAM)
MEM_END

/*
 * CPU1 read map -- pacnpal / pacnchmp
 * ROM at a000-ffff (three 8K ROMs).
 */
MEM_READ(pacnpal_readmem_cpu1)
MEM_ADDR(0xa000, 0xffff, MRA_ROM)
MEM_ADDR(0x4800, 0x480f, pacnpal_customio_r_1)
MEM_ADDR(0x4810, 0x481f, pacnpal_customio_r_2)
MEM_ADDR(0x4040, 0x43ff, superpac_sharedram_r)
MEM_ADDR(0x0000, 0x1fff, MRA_RAM)
MEM_END

/*
 * CPU1 write map -- shared by superpac, superpcm, pacnpal, pacnchmp
 * Sound enable differs between superpac (0x5008-0x5009) and pacnpal
 * (handled by CPU2 at 0x2006-0x2007), but we map it here for superpac;
 * pacnpal CPU1 simply never writes to 0x5008 for sound purposes.
 */
MEM_WRITE(superpac_writemem_cpu1)
MEM_ADDR(0x0000, 0x03ff, superpac_videoram_w)
MEM_ADDR(0x0400, 0x07ff, superpac_colorram_w)
MEM_ADDR(0x0800, 0x0f7f, MWA_RAM)
MEM_ADDR(0x0f80, 0x0fff, MWA_RAM)              /* sprite RAM area 1 */
MEM_ADDR(0x1000, 0x177f, MWA_RAM)
MEM_ADDR(0x1780, 0x17ff, MWA_RAM)              /* sprite RAM area 2 */
MEM_ADDR(0x1800, 0x1f7f, MWA_RAM)
MEM_ADDR(0x1f80, 0x1fff, MWA_RAM)              /* sprite RAM area 3 */
MEM_ADDR(0x2000, 0x2000, MWA_NOP)              /* watchdog NOP */
MEM_ADDR(0x4040, 0x43ff, superpac_sharedram_w)
MEM_ADDR(0x4800, 0x480f, superpac_customio_w_1)
MEM_ADDR(0x4810, 0x481f, superpac_customio_w_2)
MEM_ADDR(0x5000, 0x5000, superpac_reset_2_w)
MEM_ADDR(0x5002, 0x5003, superpac_interrupt_enable_1_w)
MEM_ADDR(0x5008, 0x5009, superpac_sound_enable_w)
MEM_ADDR(0x500a, 0x500b, superpac_cpu_enable_w)
MEM_ADDR(0x8000, 0x8000, MWA_NOP)              /* watchdog NOP */
MEM_ADDR(0xc000, 0xffff, MWA_ROM)
MEM_END

/*
 * CPU1 write map -- pacnpal / pacnchmp
 * Extends ROM area down to 0xa000 to cover the third program ROM.
 * Sound enable is handled by CPU2 for pacnpal; we keep the NOP mapping
 * at 0x5008-0x5009 in case the game writes there anyway.
 */
MEM_WRITE(pacnpal_writemem_cpu1)
MEM_ADDR(0x0000, 0x03ff, superpac_videoram_w)
MEM_ADDR(0x0400, 0x07ff, superpac_colorram_w)
MEM_ADDR(0x0800, 0x0f7f, MWA_RAM)
MEM_ADDR(0x0f80, 0x0fff, MWA_RAM)
MEM_ADDR(0x1000, 0x177f, MWA_RAM)
MEM_ADDR(0x1780, 0x17ff, MWA_RAM)
MEM_ADDR(0x1800, 0x1f7f, MWA_RAM)
MEM_ADDR(0x1f80, 0x1fff, MWA_RAM)
MEM_ADDR(0x2000, 0x2000, MWA_NOP)
MEM_ADDR(0x4040, 0x43ff, superpac_sharedram_w)
MEM_ADDR(0x4800, 0x480f, superpac_customio_w_1)
MEM_ADDR(0x4810, 0x481f, superpac_customio_w_2)
MEM_ADDR(0x5000, 0x5000, superpac_reset_2_w)
MEM_ADDR(0x5002, 0x5003, superpac_interrupt_enable_1_w)
MEM_ADDR(0x5008, 0x5009, MWA_NOP)              /* sound enable is on CPU2 for pacnpal */
MEM_ADDR(0x500a, 0x500b, superpac_cpu_enable_w)
MEM_ADDR(0x8000, 0x8000, MWA_NOP)
MEM_ADDR(0xa000, 0xffff, MWA_ROM)
MEM_END

/*
 * CPU2 read map -- superpac / superpcm
 * CPU2 runs free (no interrupts); just ROM and shared RAM.
 */
MEM_READ(superpac_readmem_cpu2)
MEM_ADDR(0xf000, 0xffff, MRA_ROM)
MEM_ADDR(0x0040, 0x03ff, superpac_sharedram_r2)
MEM_ADDR(0x0000, 0x003f, MRA_RAM)              /* Namco sound register readback */
MEM_END

/*
 * CPU2 write map -- superpac / superpcm
 * Sound registers at 0x0000-0x003f, shared RAM at 0x0040-0x03ff.
 */
MEM_WRITE(superpac_writemem_cpu2)
MEM_ADDR(0xf000, 0xffff, MWA_ROM)
MEM_ADDR(0x0040, 0x03ff, superpac_sharedram_w)
MEM_ADDR(0x0000, 0x003f, superpac_sound_w)
MEM_END

/*
 * CPU2 read map -- pacnpal / pacnchmp
 * Pac & Pal's sound CPU takes VBLANK interrupts and has a separate enable.
 */
MEM_READ(pacnpal_readmem_cpu2)
MEM_ADDR(0xf000, 0xffff, MRA_ROM)
MEM_ADDR(0x0040, 0x03ff, pacnpal_sharedram_r2)
MEM_ADDR(0x0000, 0x003f, MRA_RAM)
MEM_END

/*
 * CPU2 write map -- pacnpal / pacnchmp
 * Adds interrupt enable (0x2000-0x2001) and sound enable (0x2006-0x2007)
 * registers that superpac CPU2 does not have.
 */
MEM_WRITE(pacnpal_writemem_cpu2)
MEM_ADDR(0xf000, 0xffff, MWA_ROM)
MEM_ADDR(0x0040, 0x03ff, pacnpal_sharedram_w2)
MEM_ADDR(0x0000, 0x003f, superpac_sound_w)
MEM_ADDR(0x2000, 0x2001, superpac_interrupt_enable_2_w)
MEM_ADDR(0x2006, 0x2007, superpac_sound_enable_w)
MEM_END


// ---------------------------------------------------------------------------
// Input ports
// ---------------------------------------------------------------------------

/*
 * Super Pac-Man input layout:
 *   Port 0 = DSW0  (difficulty rank 0-F, coin B ratio, demo sounds, freeze)
 *   Port 1 = DSW1  (coin A ratio, bonus life, lives)
 *   Port 2 = FAKE  (joystick + button -- read via custom I/O chip emulation)
 *   Port 3 = FAKE  (coins + start buttons, cabinet, service)
 *
 * Ports 2 and 3 are not directly memory-mapped; they are read by the custom
 * I/O chip handler (superpac_customio_r_1 / superpac_customio_r_2).
 */
INPUT_PORTS_START(superpac)

PORT_START("DSW0")
PORT_DIPNAME(0x0f, 0x00, DEF_STR(Difficulty))
PORT_DIPSETTING(   0x00, "Rank 0 - Normal")
PORT_DIPSETTING(   0x01, "Rank 1 - Easiest")
PORT_DIPSETTING(   0x02, "Rank 2")
PORT_DIPSETTING(   0x03, "Rank 3")
PORT_DIPSETTING(   0x04, "Rank 4")
PORT_DIPSETTING(   0x05, "Rank 5")
PORT_DIPSETTING(   0x06, "Rank 6 - Medium")
PORT_DIPSETTING(   0x07, "Rank 7")
PORT_DIPSETTING(   0x08, "Rank 8 - Default")
PORT_DIPSETTING(   0x09, "Rank 9")
PORT_DIPSETTING(   0x0a, "Rank A")
PORT_DIPSETTING(   0x0b, "Rank B - Hardest")
PORT_DIPSETTING(   0x0c, "Rank C - Easy Auto")
PORT_DIPSETTING(   0x0d, "Rank D - Auto")
PORT_DIPSETTING(   0x0e, "Rank E - Auto")
PORT_DIPSETTING(   0x0f, "Rank F - Hard Auto")
PORT_DIPNAME(0x30, 0x00, DEF_STR(Coin_B))
PORT_DIPSETTING(   0x20, DEF_STR(2C_1C))
PORT_DIPSETTING(   0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(   0x30, DEF_STR(2C_3C))
PORT_DIPSETTING(   0x10, DEF_STR(1C_2C))
PORT_DIPNAME(0x40, 0x00, DEF_STR(Demo_Sounds))
PORT_DIPSETTING(   0x40, DEF_STR(Off))
PORT_DIPSETTING(   0x00, DEF_STR(On))
PORT_DIPNAME(0x80, 0x00, "Freeze")
PORT_DIPSETTING(   0x00, DEF_STR(Off))
PORT_DIPSETTING(   0x80, DEF_STR(On))

PORT_START("DSW1")
PORT_DIPNAME(0x07, 0x00, DEF_STR(Coin_A))
PORT_DIPSETTING(   0x07, DEF_STR(3C_1C))
PORT_DIPSETTING(   0x05, DEF_STR(2C_1C))
PORT_DIPSETTING(   0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(   0x06, DEF_STR(2C_3C))
PORT_DIPSETTING(   0x01, DEF_STR(1C_2C))
PORT_DIPSETTING(   0x02, DEF_STR(1C_3C))
PORT_DIPSETTING(   0x03, DEF_STR(1C_6C))
PORT_DIPSETTING(   0x04, DEF_STR(1C_7C))
PORT_DIPNAME(0x38, 0x00, DEF_STR(Bonus_Life))
PORT_DIPSETTING(   0x38, "None")
PORT_DIPSETTING(   0x30, "30k")
PORT_DIPSETTING(   0x08, "30k 80k")
PORT_DIPSETTING(   0x00, "30k 100k")
PORT_DIPSETTING(   0x10, "30k 120k")
PORT_DIPSETTING(   0x18, "30k 80k 80k")
PORT_DIPSETTING(   0x20, "30k 100k 100k")
PORT_DIPSETTING(   0x28, "30k 120k 120k")
PORT_DIPNAME(0xc0, 0x00, DEF_STR(Lives))
PORT_DIPSETTING(   0x40, "1")
PORT_DIPSETTING(   0x80, "2")
PORT_DIPSETTING(   0x00, "3")
PORT_DIPSETTING(   0xc0, "5")

PORT_START("FAKE -- player controls (read via custom I/O chip)")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP    | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN  | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT  | IPF_4WAY)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1, 1)
PORT_BITX(0x20, IP_ACTIVE_HIGH, IPT_BUTTON1, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT(0xc0, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("FAKE -- coins, start, cabinet, service (read via custom I/O chip)")
PORT_BIT_IMPULSE(0x01, IP_ACTIVE_HIGH, IPT_COIN1,  1)
PORT_BIT_IMPULSE(0x02, IP_ACTIVE_HIGH, IPT_COIN2,  1)
PORT_BIT(0x0c,         IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_HIGH, IPT_START1, 1)
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_HIGH, IPT_START2, 1)
PORT_DIPNAME(0x40, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(   0x00, DEF_STR(Upright))
PORT_DIPSETTING(   0x40, DEF_STR(Cocktail))
PORT_SERVICE(0x80, IP_ACTIVE_HIGH)

INPUT_PORTS_END


/*
 * Pac & Pal input layout:
 *   Port 0 = DSW0  (coin B, difficulty rank)
 *   Port 1 = DSW1  (coin A, bonus life, lives)
 *   Port 2 = FAKE  (joystick + button)
 *   Port 3 = FAKE  (coins + start, cabinet, service)
 */
INPUT_PORTS_START(pacnpal)

PORT_START("DSW0")
PORT_DIPNAME(0x03, 0x00, DEF_STR(Coin_B))
PORT_DIPSETTING(   0x02, DEF_STR(2C_1C))
PORT_DIPSETTING(   0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(   0x03, DEF_STR(2C_3C))
PORT_DIPSETTING(   0x01, DEF_STR(1C_2C))
PORT_DIPNAME(0x0c, 0x00, "Rank")
PORT_DIPSETTING(   0x00, "A")
PORT_DIPSETTING(   0x04, "B")
PORT_DIPSETTING(   0x08, "C")
PORT_DIPSETTING(   0x0c, "D")
PORT_BIT(0xf0, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("DSW1")
PORT_DIPNAME(0x07, 0x00, DEF_STR(Coin_A))
PORT_DIPSETTING(   0x07, DEF_STR(3C_1C))
PORT_DIPSETTING(   0x05, DEF_STR(2C_1C))
PORT_DIPSETTING(   0x00, DEF_STR(1C_1C))
PORT_DIPSETTING(   0x06, DEF_STR(2C_3C))
PORT_DIPSETTING(   0x01, DEF_STR(1C_2C))
PORT_DIPSETTING(   0x02, DEF_STR(1C_3C))
PORT_DIPSETTING(   0x03, DEF_STR(1C_6C))
PORT_DIPSETTING(   0x04, DEF_STR(1C_7C))
PORT_DIPNAME(0x38, 0x18, DEF_STR(Bonus_Life))
PORT_DIPSETTING(   0x00, "None")
PORT_DIPSETTING(   0x38, "30k")
PORT_DIPSETTING(   0x18, "20k 70k")
PORT_DIPSETTING(   0x20, "30k 70k")
PORT_DIPSETTING(   0x28, "30k 80k")
PORT_DIPSETTING(   0x30, "30k 100k")
PORT_DIPSETTING(   0x08, "20k 70k 70k")
PORT_DIPSETTING(   0x10, "30k 80k 80k")
PORT_DIPNAME(0xc0, 0x80, DEF_STR(Lives))
PORT_DIPSETTING(   0x00, "1")
PORT_DIPSETTING(   0x40, "2")
PORT_DIPSETTING(   0x80, "3")
PORT_DIPSETTING(   0xc0, "5")

PORT_START("FAKE -- player controls")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP    | IPF_4WAY)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_4WAY)
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN  | IPF_4WAY)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT  | IPF_4WAY)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_HIGH, IPT_BUTTON1, 2)
PORT_BITX(0x20, IP_ACTIVE_HIGH, IPT_BUTTON1, 0, IP_KEY_PREVIOUS, IP_JOY_PREVIOUS)
PORT_BIT(0xc0, IP_ACTIVE_HIGH, IPT_UNUSED)

PORT_START("FAKE -- coins, start, cabinet, service")
PORT_BIT_IMPULSE(0x01, IP_ACTIVE_HIGH, IPT_COIN1,  2)
PORT_BIT_IMPULSE(0x02, IP_ACTIVE_HIGH, IPT_COIN2,  2)
PORT_BIT(0x0c,         IP_ACTIVE_HIGH, IPT_UNUSED)
PORT_BIT_IMPULSE(0x10, IP_ACTIVE_HIGH, IPT_START1, 2)
PORT_BIT_IMPULSE(0x20, IP_ACTIVE_HIGH, IPT_START2, 2)
PORT_DIPNAME(0x40, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(   0x00, DEF_STR(Upright))
PORT_DIPSETTING(   0x40, DEF_STR(Cocktail))
PORT_SERVICE(0x80, IP_ACTIVE_HIGH)

INPUT_PORTS_END


// ---------------------------------------------------------------------------
// ROM definitions
// ---------------------------------------------------------------------------
/*
 * Super Pac-Man (US, Namco)
 * sp1-2.1c / sp1-1.1b are the correct Namco board labels.
 * GFX: sp1-6.3c (chars 4K), spv-2.3f (sprites 8K).
 */
    ROM_START(superpac)
    ROM_REGION(0x10000, REGION_CPU1, 0)
    ROM_LOAD("sp1-2.1c", 0xc000, 0x2000, CRC(4bb33d9c) SHA1(dd87f71b4db090a32a6b791079eedd17580cc741))
    ROM_LOAD("sp1-1.1b", 0xe000, 0x2000, CRC(846fbb4a) SHA1(f6bf90281986b9b7a3ef1dbbeddb722182e84d7c))

    ROM_REGION(0x10000, REGION_CPU2, 0)
    ROM_LOAD("spc-3.1k", 0xf000, 0x1000, CRC(04445ddb) SHA1(ce7d14963d5ddaefdeaf433a6f82c43cd1611d9b))

    ROM_REGION(0x1000, REGION_GFX1, ROMREGION_DISPOSE)
    ROM_LOAD("sp1-6.3c", 0x0000, 0x1000, CRC(91c5935c) SHA1(10579edabc26a0910253fab7d41b4c19ecdaaa09))

    ROM_REGION(0x2000, REGION_GFX2, ROMREGION_DISPOSE)
    ROM_LOAD("spv-2.3f", 0x0000, 0x2000, CRC(670a42f2) SHA1(9171922df07e31fd1dc415766f7d2cc50a9d10dc))

    ROM_REGION(0x0220, REGION_PROMS, 0)
    ROM_LOAD("superpac.4c", 0x0000, 0x0020, CRC(9ce22c46) SHA1(d97f53ef4c5ef26659a22ed0de4ce7ef3758c924))  /* palette */
    ROM_LOAD("superpac.4e", 0x0020, 0x0100, CRC(1253c5c1) SHA1(df46a90170e9761d45c90fbd04ef2aa1e8c9944b))  /* char color LUT */
    ROM_LOAD("superpac.3l", 0x0120, 0x0100, CRC(d4d7026f) SHA1(a486573437c54bfb503424574ad82655491e85e1))  /* sprite color LUT */

    ROM_REGION(0x0100, REGION_SOUND1, 0)
    ROM_LOAD("superpac.3m", 0x0000, 0x0100, CRC(ad43688f) SHA1(072f427453efb1dda8147da61804fff06e1bc4d5))  /* Namco waveform PROM */
    ROM_END

    /*
     * Super Pac-Man (Midway license)
     * spc-2.1c / spc-1.1b are the Midway board labels.
     * GFX: spv-1.3c (different char ROM), same sprite and PROM set as superpac.
     */
    ROM_START(superpcm)
    ROM_REGION(0x10000, REGION_CPU1, 0)
    ROM_LOAD("spc-2.1c", 0xc000, 0x2000, CRC(1a38c30e) SHA1(ae0ee9f3df0991a80698fe745a7a853a4bb60710))
    ROM_LOAD("spc-1.1b", 0xe000, 0x2000, CRC(730e95a9) SHA1(ca73c8bcb03c2f5c05968c707a5d3f7f9956b886))

    ROM_REGION(0x10000, REGION_CPU2, 0)
    ROM_LOAD("spc-3.1k", 0xf000, 0x1000, CRC(04445ddb) SHA1(ce7d14963d5ddaefdeaf433a6f82c43cd1611d9b))

    ROM_REGION(0x1000, REGION_GFX1, ROMREGION_DISPOSE)
    ROM_LOAD("spv-1.3c", 0x0000, 0x1000, CRC(78337e74) SHA1(11222adb55e6bce508896ccb1f6dbab0c1d44e5b))

    ROM_REGION(0x2000, REGION_GFX2, ROMREGION_DISPOSE)
    ROM_LOAD("spv-2.3f", 0x0000, 0x2000, CRC(670a42f2) SHA1(9171922df07e31fd1dc415766f7d2cc50a9d10dc))

    ROM_REGION(0x0220, REGION_PROMS, 0)
    ROM_LOAD("superpac.4c", 0x0000, 0x0020, CRC(9ce22c46) SHA1(d97f53ef4c5ef26659a22ed0de4ce7ef3758c924))
    ROM_LOAD("superpac.4e", 0x0020, 0x0100, CRC(1253c5c1) SHA1(df46a90170e9761d45c90fbd04ef2aa1e8c9944b))
    ROM_LOAD("superpac.3l", 0x0120, 0x0100, CRC(d4d7026f) SHA1(a486573437c54bfb503424574ad82655491e85e1))

    ROM_REGION(0x0100, REGION_SOUND1, 0)
    ROM_LOAD("superpac.3m", 0x0000, 0x0100, CRC(ad43688f) SHA1(072f427453efb1dda8147da61804fff06e1bc4d5))
    ROM_END

    /*
     * Pac & Pal (Japan, set 1)
     * Three program ROMs from 0xa000. CPU2 takes VBLANK IRQ unlike superpac.
     * ROM labels use board position suffix (.1d, .1c, .1b, .1k, .3c, .3f etc).
     */
    ROM_START(pacnpal)
    ROM_REGION(0x10000, REGION_CPU1, 0)
    ROM_LOAD("pap1-3b.1d", 0xa000, 0x2000, CRC(ed64a565) SHA1(b16930981490d97486d4df96acbb3d1cddbd3a80))
    ROM_LOAD("pap1-2b.1c", 0xc000, 0x2000, CRC(15308bcf) SHA1(334603f8904f8968d05edc420b5f9e3b483ee86d))
    ROM_LOAD("pap3-1.1b", 0xe000, 0x2000, CRC(3cac401c) SHA1(38a14228469fa4a20cbc5d862198dc901842682e))

    ROM_REGION(0x10000, REGION_CPU2, 0)
    ROM_LOAD("pap1-4.1k", 0xf000, 0x1000, CRC(330e20de) SHA1(5b23e5dcc38dc644a36efc8b03eba34cea540bea))

    ROM_REGION(0x1000, REGION_GFX1, ROMREGION_DISPOSE)
    ROM_LOAD("pap1-6.3c", 0x0000, 0x1000, CRC(a36b96cb) SHA1(e0a11b5a43cbf756ddb045c743973d0a55dbb979))

    ROM_REGION(0x2000, REGION_GFX2, ROMREGION_DISPOSE)
    ROM_LOAD("pap1-5.3f", 0x0000, 0x2000, CRC(fb6f56e3) SHA1(fd10d2ee49b4e059e9ef6046bc86d97e3185164d))

    ROM_REGION(0x0220, REGION_PROMS, 0)
    ROM_LOAD("pap1-6.4c", 0x0000, 0x0020, CRC(52634b41) SHA1(dfb109c8e2c62ae1612ba0e3272468d152123842)) /* palette */
    ROM_LOAD("pap1-5.4e", 0x0020, 0x0100, CRC(ac46203c) SHA1(3f47f1991aab9640c0d5f70fad85d20d6cf2ea3d)) /* chars */
    ROM_LOAD("pap1-4.3l", 0x0120, 0x0100, CRC(686bde84) SHA1(541d08b43dbfb789c2867955635d2c9e051fedd9)) /* sprites */

    ROM_REGION(0x0100, REGION_SOUND1, 0)
    ROM_LOAD("pap1-3.3m", 0x0000, 0x0100, CRC(94782db5) SHA1(ac0114f0611c81dfac9469253048ae0214d570ee))
    ROM_END

    /*
     * Pac & Pal (Japan, set 2)
     * Different program ROMs only; GFX, PROMs and sound identical to pacnpal set 1.
     * Uses the same machine driver and memory maps as pacnpal.
     */
    ROM_START(pacnpal2)
    ROM_REGION(0x10000, REGION_CPU1, 0)
    ROM_LOAD("pap1_3.1d", 0xa000, 0x2000, CRC(d7ec2719) SHA1(b633a5360a199d528bcef209c06a21f266525769))
    ROM_LOAD("pap1_2.1c", 0xc000, 0x2000, CRC(0245396e) SHA1(7e8467e317879621a7b31bc922b5187f20fcea78))
    ROM_LOAD("pap1_1.1b", 0xe000, 0x2000, CRC(7f046b58) SHA1(2024019e5fafb698bb5775075c9b88c5ed35f7ba))

    ROM_REGION(0x10000, REGION_CPU2, 0)
    ROM_LOAD("pap14.1k", 0xf000, 0x1000, CRC(330e20de) SHA1(5b23e5dcc38dc644a36efc8b03eba34cea540bea))

    ROM_REGION(0x1000, REGION_GFX1, ROMREGION_DISPOSE)
    ROM_LOAD("pap16.3c", 0x0000, 0x1000, CRC(a36b96cb) SHA1(e0a11b5a43cbf756ddb045c743973d0a55dbb979))

    ROM_REGION(0x2000, REGION_GFX2, ROMREGION_DISPOSE)
    ROM_LOAD("pap15.3f", 0x0000, 0x2000, CRC(fb6f56e3) SHA1(fd10d2ee49b4e059e9ef6046bc86d97e3185164d))

    ROM_REGION(0x0220, REGION_PROMS, 0)
    ROM_LOAD("papi6.4c", 0x0000, 0x0020, CRC(52634b41) SHA1(dfb109c8e2c62ae1612ba0e3272468d152123842))
    ROM_LOAD("papi5.4e", 0x0020, 0x0100, CRC(ac46203c) SHA1(3f47f1991aab9640c0d5f70fad85d20d6cf2ea3d))
    ROM_LOAD("papi4.3l", 0x0120, 0x0100, CRC(686bde84) SHA1(541d08b43dbfb789c2867955635d2c9e051fedd9))

    ROM_REGION(0x0100, REGION_SOUND1, 0)
    ROM_LOAD("papi3.3m", 0x0000, 0x0100, CRC(83c31a98) SHA1(8f1219a6c2b565ae9d8f72a9c277dc4bd38ec40f))
    ROM_END

    /*
     * Pac-Man & Chomp Chomp
     * Shares pap1.1b at 0xe000 with pacnpal set 1.
     * PROMs are flagged BAD_DUMP in the reference -- CRC/SHA1 match pacnpal
     * PROMs and are used as-is since no clean dump is known.
     */
    ROM_START(pacnchmp)
    ROM_REGION(0x10000, REGION_CPU1, 0)
    ROM_LOAD("pap3.1d", 0xa000, 0x2000, CRC(20a07d3d) SHA1(2135ad154b575a73cfb1b0f0f282dfc013672aec))
    ROM_LOAD("pap3.1c", 0xc000, 0x2000, CRC(505bae56) SHA1(590ce9f0e92115a71eb76b71ab4eac16ffa2a28e))
    ROM_LOAD("pap1.1b", 0xe000, 0x2000, CRC(3cac401c) SHA1(38a14228469fa4a20cbc5d862198dc901842682e))

    ROM_REGION(0x10000, REGION_CPU2, 0)
    ROM_LOAD("pap14.1k", 0xf000, 0x1000, CRC(330e20de) SHA1(5b23e5dcc38dc644a36efc8b03eba34cea540bea))

    ROM_REGION(0x1000, REGION_GFX1, ROMREGION_DISPOSE)
    ROM_LOAD("pap2.3c", 0x0000, 0x1000, CRC(93d15c30) SHA1(5da4120b680726c83a651b445254604cbf7cc883))

    ROM_REGION(0x2000, REGION_GFX2, ROMREGION_DISPOSE)
    ROM_LOAD("pap2.3f", 0x0000, 0x2000, CRC(39f44aa4) SHA1(0696539cb2c7fcda2f6c295c7d65678dac18950b))

    ROM_REGION(0x0220, REGION_PROMS, 0)
    ROM_LOAD("papi6.4c", 0x0000, 0x0020, CRC(52634b41) SHA1(dfb109c8e2c62ae1612ba0e3272468d152123842))  /* palette -- bad dump, borrowed from pacnpal */
    ROM_LOAD("papi5.4e", 0x0020, 0x0100,  CRC(ac46203c) SHA1(3f47f1991aab9640c0d5f70fad85d20d6cf2ea3d))  /* char color LUT -- bad dump */
    ROM_LOAD("papi4.3l", 0x0120, 0x0100, CRC(686bde84) SHA1(541d08b43dbfb789c2867955635d2c9e051fedd9))  /* sprite color LUT -- bad dump */

    ROM_REGION(0x0100, REGION_SOUND1, 0)
    ROM_LOAD("papi3.3m", 0x0000, 0x0100, CRC(83c31a98) SHA1(8f1219a6c2b565ae9d8f72a9c277dc4bd38ec40f))
    ROM_END

// ---------------------------------------------------------------------------
// Driver descriptors
//
// Screen: 28 tiles wide x 36 tiles tall = 224 x 288 pixels, ROT90.
// The AAE_DRIVER_SCREEN macro takes (width, height, xmin, xmax, ymin, ymax).
// Color table: 64 char sets x 4 + 64 sprite sets x 4 = 512 entries.
// Total palette entries: 32.
//
// Both CPUs run at 1.1 MHz.
// cpu_slices = 100 for tight dual-CPU synchronization (matches MAME original).
//
// superpac / superpcm:
//   CPU2 has no interrupt (runs free); INT_TYPE_INT with nullptr callback.
//
// pacnpal / pacnchmp:
//   CPU2 fires VBLANK IRQ; interrupt callback is superpac_interrupt_2.
// ---------------------------------------------------------------------------

/* ---- Super Pac-Man (US, Namco) ---- */
AAE_DRIVER_BEGIN(drv_superpac, "superpac", "Super Pac-Man")
AAE_DRIVER_ROM(rom_superpac)
AAE_DRIVER_FUNCS(&init_superpac, &run_superpac, &end_superpac)
AAE_DRIVER_INPUT(input_ports_superpac)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
    /* CPU0: main game CPU -- takes VBLANK IRQ */
    AAE_CPU_ENTRY(
        /*type*/     CPU_M6809,
        /*freq*/     18432000 / 12,
        /*div*/      100,
        /*ipf*/      1,
        /*int type*/ INT_TYPE_INT,
        /*int cb*/   &superpac_interrupt_1,
        /*r8*/       superpac_readmem_cpu1,
        /*w8*/       superpac_writemem_cpu1,
        /*pr*/       nullptr,
        /*pw*/       nullptr,
        /*r16*/      nullptr,
        /*w16*/      nullptr
    ),
    /* CPU1: sound CPU -- no interrupt on superpac, runs free */
    AAE_CPU_ENTRY(
        /*type*/     CPU_M6809,
        /*freq*/     18432000 / 12,
        /*div*/      100,
        /*ipf*/      1,
        /*int type*/ INT_TYPE_INT,
        /*int cb*/   nullptr,
        /*r8*/       superpac_readmem_cpu2,
        /*w8*/       superpac_writemem_cpu2,
        /*pr*/       nullptr,
        /*pw*/       nullptr,
        /*r16*/      nullptr,
        /*w16*/      nullptr
    ),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION,
    VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
/* screen is 224 wide x 288 tall in natural orientation (pre-rotation) */
//AAE_DRIVER_SCREEN(28 * 8, 36 * 8, 0, 28 * 8 - 1, 0, 36 * 8 - 1)
AAE_DRIVER_SCREEN(28 * 8, 36 * 8,  0 * 8, 28 * 8 - 1, 0 * 8, 36 * 8 - 1 )

/* 32 palette entries, 512-entry color LUT (64 char sets + 64 sprite sets, 4 pixels each) */
AAE_DRIVER_RASTER(superpac_gfxdecodeinfo, 32, 4 * (64 + 64), superpac_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
//AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")

AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_superpac)


/* ---- Super Pac-Man (Midway license) -- identical hardware, different program ROMs ---- */
AAE_DRIVER_BEGIN(drv_superpcm, "superpcm", "Super Pac-Man (Midway)")
AAE_DRIVER_ROM(rom_superpcm)
AAE_DRIVER_FUNCS(&init_superpac, &run_superpac, &end_superpac)
AAE_DRIVER_INPUT(input_ports_superpac)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
    AAE_CPU_ENTRY(
        CPU_M6809, 18432000 / 12, 100, 1, INT_TYPE_INT, &superpac_interrupt_1,
        superpac_readmem_cpu1, superpac_writemem_cpu1, nullptr, nullptr, nullptr, nullptr
    ),
    AAE_CPU_ENTRY(
        CPU_M6809, 18432000 / 12, 100, 1, INT_TYPE_INT, nullptr,
        superpac_readmem_cpu2, superpac_writemem_cpu2, nullptr, nullptr, nullptr, nullptr
    ),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION,
    VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ROT90)
AAE_DRIVER_SCREEN(28 * 8, 36 * 8, 0, 28 * 8 - 1, 0, 36 * 8 - 1)
AAE_DRIVER_RASTER(superpac_gfxdecodeinfo, 32, 4 * (64 + 64), superpac_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_superpcm)


/* ---- Pac & Pal ---- */
AAE_DRIVER_BEGIN(drv_pacnpal, "pacnpal", "Pac & Pal")
AAE_DRIVER_ROM(rom_pacnpal)
AAE_DRIVER_FUNCS(&init_pacnpal, &run_pacnpal, &end_pacnpal)
AAE_DRIVER_INPUT(input_ports_pacnpal)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
    /* CPU0: main game CPU -- takes VBLANK IRQ */
    AAE_CPU_ENTRY(
        /*type*/     CPU_M6809,
        /*freq*/     18432000 / 12,
        /*div*/      100,
        /*ipf*/      1,
        /*int type*/ INT_TYPE_INT,
        /*int cb*/   &superpac_interrupt_1,
        /*r8*/       pacnpal_readmem_cpu1,
        /*w8*/       pacnpal_writemem_cpu1,
        /*pr*/       nullptr,
        /*pw*/       nullptr,
        /*r16*/      nullptr,
        /*w16*/      nullptr
    ),
    /* CPU1: sound CPU -- also takes VBLANK IRQ on pacnpal */
    AAE_CPU_ENTRY(
        /*type*/     CPU_M6809,
        /*freq*/     18432000 / 12,
        /*div*/      100,
        /*ipf*/      1,
        /*int type*/ INT_TYPE_INT,
        /*int cb*/   &superpac_interrupt_2,
        /*r8*/       pacnpal_readmem_cpu2,
        /*w8*/       pacnpal_writemem_cpu2,
        /*pr*/       nullptr,
        /*pw*/       nullptr,
        /*r16*/      nullptr,
        /*w16*/      nullptr
    ),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(28 * 8, 36 * 8, 0, 28 * 8 - 1, 0, 36 * 8 - 1)
AAE_DRIVER_RASTER(superpac_gfxdecodeinfo, 32, 4 * (64 + 64), superpac_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
//AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_pacnpal)


/* ---- Pac-Man & Chomp Chomp (alternate region pacnpal) ---- */
AAE_DRIVER_BEGIN(drv_pacnchmp, "pacnchmp", "Pac-Man & Chomp Chomp")
AAE_DRIVER_ROM(rom_pacnchmp)
AAE_DRIVER_FUNCS(&init_pacnpal, &run_pacnpal, &end_pacnpal)
AAE_DRIVER_INPUT(input_ports_pacnpal)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
    AAE_CPU_ENTRY(
        CPU_M6809, 1100000, 100, 1, INT_TYPE_INT, &superpac_interrupt_1,
        pacnpal_readmem_cpu1, pacnpal_writemem_cpu1, nullptr, nullptr, nullptr, nullptr
    ),
    AAE_CPU_ENTRY(
        CPU_M6809, 1100000, 100, 1, INT_TYPE_INT, &superpac_interrupt_2,
        pacnpal_readmem_cpu2, pacnpal_writemem_cpu2, nullptr, nullptr, nullptr, nullptr
    ),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION,
    VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ROT90)
AAE_DRIVER_SCREEN(28 * 8, 36 * 8, 0, 28 * 8 - 1, 0, 36 * 8 - 1)
AAE_DRIVER_RASTER(superpac_gfxdecodeinfo, 32, 4 * (64 + 64), superpac_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

/* ---- Pac & Pal (set 2) -- different program ROMs only, same hardware as pacnpal ---- */
AAE_DRIVER_BEGIN(drv_pacnpal2, "pacnpal2", "Pac & Pal (set 2)")
AAE_DRIVER_ROM(rom_pacnpal2)
AAE_DRIVER_FUNCS(&init_pacnpal, &run_pacnpal, &end_pacnpal)
AAE_DRIVER_INPUT(input_ports_pacnpal)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
AAE_CPU_ENTRY(
CPU_M6809, 1100000, 100, 1, INT_TYPE_INT, &superpac_interrupt_1,
pacnpal_readmem_cpu1, pacnpal_writemem_cpu1, nullptr, nullptr, nullptr, nullptr
),
AAE_CPU_ENTRY(
CPU_M6809, 1100000, 100, 1, INT_TYPE_INT, &superpac_interrupt_2,
pacnpal_readmem_cpu2, pacnpal_writemem_cpu2, nullptr, nullptr, nullptr, nullptr
),
AAE_CPU_NONE_ENTRY(),
AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, DEFAULT_60HZ_VBLANK_DURATION,
VIDEO_TYPE_RASTER_COLOR | VIDEO_SUPPORTS_DIRTY, ROT90)
AAE_DRIVER_SCREEN(28 * 8, 36 * 8, 0, 28 * 8 - 1, 0, 36 * 8 - 1)
AAE_DRIVER_RASTER(superpac_gfxdecodeinfo, 32, 4 * (64 + 64), superpac_vh_convert_color_prom)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT("default.lay", "Upright_Artwork")

AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_pacnpal2)

AAE_REGISTER_DRIVER(drv_pacnchmp)
