/***************************************************************************

  circus.cpp

  AAE driver for the Exidy Circus hardware family:
    - Circus (1977)
    - Robot Bowl (1977)
    - Crash (1979)
    - Ripcord (1977)

  Original MAME 0.36 driver by Mike Coates.
  Ported to AAE.

  Hardware overview:
    CPU:    6502 @ 705,562 Hz  (11.289 MHz / 16)
    Video:  256x256, 1-bit monochrome (black + white)
            32x32 tile grid, 8x8 tiles (256 chars, 1bpp)
            Single 16x16 sprite (the clown / ball / car / parachute)
            Sync-generator-drawn lines for borders and diving boards
    Sound:  Single DAC channel
    Input:  Paddle (Circus, Ripcord) or joystick (Crash) or buttons (Robot Bowl)

  Memory map (shared across all four games):
    0000-01FF  RAM (base page + stack)
    1000-1FFF  ROM (Circus/Crash/Ripcord) or mirrors
    2000       Clown/sprite vertical position write
    3000       Clown/sprite horizontal position write
    4000-43FF  Video RAM (32x32 tiles)
    8000       Clown rotation + audio control write
    A000       Control switches read (IN0)
    C000       Option switches read (DSW)
    D000       Paddle position read + interrupt reset (IN2)
    F000-FFFF  ROM (all games, reset/IRQ vectors at FFF8-FFFF)

***************************************************************************/

#include "aae_mame_driver.h"
#include "circus.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "dac.h"

#pragma warning( disable : 4838 4003 )

/*--------------------------------------------------------------------------
  Sprite state: shared across all four game variants.
  "clown" naming matches the original MAME source; for Crash this is the car,
  for Robot Bowl it is the bowling ball, for Ripcord the parachutist.
--------------------------------------------------------------------------*/
static int clown_x = 0;   /* vertical position   (240 - data) */
static int clown_y = 0;   /* horizontal position  (240 - data) */
static int clown_z = 0;   /* sprite frame select  (low nibble of $8000 write) */

/* Which game variant is running -- selects screen refresh routine */
enum {
    GAME_CIRCUS = 0,
    GAME_ROBOTBWL,
    GAME_CRASH,
    GAME_RIPCORD
};
static int game_id = GAME_CIRCUS;

/*--------------------------------------------------------------------------
  Palette: two colors, black and white (1-bit graphics).
--------------------------------------------------------------------------*/
static unsigned char circus_palette[] =
{
   0x00,0x00,0x00, /* BLACK */
    0xff,0xff,0x20, /* YELLOW */
    0x20,0xff,0x20, /* GREEN */
    0x20,0x20,0xFF, /* BLUE */
    0xff,0xff,0xff, /* WHITE */
    0xff,0x20,0x20, /* RED */
};

static unsigned char test_palette[] =
{
    0x00,0x00,0x00, /* BLACK */
    0xff,0xff,0xff, /* WHITE */
};

static unsigned short colortable[] =
{
    0,0,
    0,1,
    0,2,
    0,3,
    0,4,
    0,5
};

void circus_init_palette(unsigned char* palette, unsigned char* colortable,  const unsigned char* color_prom)
{
   // memcpy(palette, circus_palette, sizeof(circus_palette));
    memcpy(palette, test_palette, sizeof(test_palette));
}

/*--------------------------------------------------------------------------
  GFX layouts

  charlayout:  8x8 characters, 256 of them, 1 bit per pixel
  clownlayout: 16x16 sprites, 16 of them, 1 bit per pixel
               (each sprite is 32 bytes: two 16-byte halves side by side)
  robotlayout: 8x8 sprite, 1 character, 1 bit per pixel (bowling ball)
--------------------------------------------------------------------------*/
static struct GfxLayout charlayout =
{
    8, 8,       /* 8x8 characters */
    256,        /* 256 characters */
    1,          /* 1 bit per pixel */
    { 0 },     /* single bitplane at offset 0 */
    { 0, 1, 2, 3, 4, 5, 6, 7 },
    { 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
    8*8         /* 8 bytes per character */
};

static struct GfxLayout clownlayout =
{
    16, 16,     /* 16x16 sprites */
    16,         /* 16 sprite frames */
    1,          /* 1 bit per pixel */
    { 0 },     /* single bitplane at offset 0 */
    /* x offsets: first 8 pixels from the first byte, next 8 from 16 bytes later */
    { 0, 1, 2, 3, 4, 5, 6, 7,
      16*8+0, 16*8+1, 16*8+2, 16*8+3, 16*8+4, 16*8+5, 16*8+6, 16*8+7 },
    /* y offsets: consecutive bytes within each 16-byte half */
    { 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8,
      8*8, 9*8, 10*8, 11*8, 12*8, 13*8, 14*8, 15*8 },
    16*16       /* 32 bytes per sprite (16x16 / 8 bits * 2 halves) */
};

static struct GfxLayout robotlayout =
{
    8, 8,       /* 8x8 ball sprite */
    1,          /* just 1 character */
    1,          /* 1 bit per pixel */
    { 0 },
    { 0, 1, 2, 3, 4, 5, 6, 7 },
    { 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
    8           /* 8 bytes total */
};

/* Circus, Crash, Ripcord use the clown (16x16) sprite set */
struct GfxDecodeInfo circus_gfxdecodeinfo[] =
{
    { REGION_GFX1, 0, &charlayout,  0, 1 },
    { REGION_GFX2, 0, &clownlayout, 0, 1 },
    { -1 }
};

/* Robot Bowl uses a tiny 8x8 ball sprite */
struct GfxDecodeInfo robotbwl_gfxdecodeinfo[] =
{
    { REGION_GFX1, 0, &charlayout,  0, 1 },
    { REGION_GFX2, 0, &robotlayout, 0, 1 },
    { -1 }
};

/*--------------------------------------------------------------------------
  DAC sound interface: single channel, full volume
--------------------------------------------------------------------------*/
static struct DACinterface circus_dac_interface =
{
    1,              /* 1 DAC channel */
    { 100 }         /* 100% mixing level */
};

/*--------------------------------------------------------------------------
  VBLANK helper

  The Circus hardware exposes VBLANK as a bit in the DSW port.
  We compute it from elapsed CPU cycles in the current frame.
  At 57 fps with 705562 Hz CPU, one frame is ~12378 cycles.
  VBLANK occupies roughly the last ~8% of the frame (about 16 scanlines
  out of ~262 total NTSC lines).
--------------------------------------------------------------------------*/
static inline int circus_in_vblank(void)
{
    int cpf = 705562 / 57;  /* cycles per frame (~12378) */
    int elapsed = get_exact_cyclecount(0);// get_elapsed_ticks(0);
    /* VBLANK starts at about line 240 of 262, so roughly 91.6% through */
    int vblank_start = (cpf * 200) / 262;
    return (elapsed >= vblank_start) ? 1 : 0;

    const int scan = aae_cpu_getscanline();
    if (scan > 233) return 1; else return 0;
}

/*--------------------------------------------------------------------------
  Memory write handlers
--------------------------------------------------------------------------*/

/* $2000: Clown/sprite vertical position */
WRITE_HANDLER(circus_clown_x_w)
{
    clown_x = 240 - data;
}

/* $3000: Clown/sprite horizontal position */
WRITE_HANDLER(circus_clown_y_w)
{
    clown_y = 240 - data;
}

/* $8000: Clown rotation (low nibble) + audio/event triggers (bits 4-6) + amp enable (bit 7)
   The register selects the sprite frame and also fires sound events.
   Bits 4-6 meanings (from Circus schematics):
     0 = All Off       4 = Miss
     1 = Music         5 = Invert Video
     2 = Pop           6 = Bounce
     3 = Normal Video  7 = Unknown
   Bit 7 enables the amplifier (1 = on).
*/
WRITE_HANDLER(circus_clown_z_w)
{
    clown_z = (data & 0x0f);

    /* Sound event triggers via DAC */
    switch ((data & 0x70) >> 4)
    {
        case 0:  /* All Off */
            DAC_data_w(0, 0);
            break;
        case 1:  /* Music - output a DC level to the DAC */
            DAC_data_w(0, 0x7f);
            break;
        case 2:  /* Pop */
            break;
        case 3:  /* Normal Video */
            break;
        case 4:  /* Miss */
            break;
        case 5:  /* Invert Video */
            break;
        case 6:  /* Bounce */
            break;
        case 7:  /* Unknown */
            break;
    }
}

/*--------------------------------------------------------------------------
  Memory read handlers
--------------------------------------------------------------------------*/

/* IN0 read at $A000: active-high control switches */
READ_HANDLER(circus_IN0_r)
{
    return (UINT8)readinputportbytag("IN0");
}

/* DSW read at $C000: option switches + VBLANK on bit 7 */
READ_HANDLER(circus_DSW_r)
{
    UINT8 dsw = (UINT8)readinputportbytag("DSW");
    const int scan = aae_cpu_getscanline();   // 0..LINES_PER_FRAME-1

    /* For Circus: VBLANK is active LOW on bit 7 (IPT_VBLANK with IP_ACTIVE_LOW) */
    /* For Crash: VBLANK is active HIGH on bit 7 */
    if (game_id == GAME_CIRCUS || game_id == GAME_RIPCORD)
    {
       
        LOG_INFO("SCANLINE AT DSW READ IS %d", scan);
        /* Active low: bit 7 = 0 during vblank, 1 otherwise */
        if (circus_in_vblank())
            dsw &= ~0x80;
        else
            dsw |= 0x80;
    }
    else
    {
        LOG_INFO("SCANLINE AT DSW READ NOT CIRCUS IS %d", scan);
        /* Active high: bit 7 = 1 during vblank */
        if (circus_in_vblank())
            dsw |= 0x80;
        else
            dsw &= ~0x80;
    }

    return dsw;
}

/* IN2 read at $D000: paddle position (Circus/Ripcord) or just input port (Crash/Robot Bowl)
   Reading this port also acknowledges the interrupt on the real hardware. */
READ_HANDLER(circus_IN2_r)
{
    /* Acknowledge the IRQ by clearing the pending interrupt */
    m_cpu_6502[0]->m6502clearpendingint();
    return (UINT8)readinputportbytag("IN2");
}

/*--------------------------------------------------------------------------
  Memory maps
--------------------------------------------------------------------------*/

MEM_READ(circus_readmem)
MEM_ADDR(0xa000, 0xa000, circus_IN0_r)
MEM_ADDR(0xc000, 0xc000, circus_DSW_r)
MEM_ADDR(0xd000, 0xd000, circus_IN2_r)
{ 0x0000, 0x01ff, MRA_RAM},
{ 0x1000, 0x1fff, MRA_ROM },
{ 0x4000, 0x43ff, MRA_RAM },
//{ 0xa000, 0xa000, ip_port_0_r },
//{ 0xc000, 0xc000, ip_port_1_r }, /* DSW */
//{ 0xd000, 0xd000, ip_port_2_r },
{ 0xf000, 0xffff, MRA_ROM },
MEM_END

MEM_WRITE(circus_writemem)
    MEM_ADDR(0x0000, 0x01ff, MWA_RAM)
    MEM_ADDR(0x2000, 0x2000, circus_clown_x_w)
    MEM_ADDR(0x3000, 0x3000, circus_clown_y_w)
    MEM_ADDR(0x4000, 0x43ff, MWA_RAM)        /* video RAM - written directly to MEM[] */
    MEM_ADDR(0x8000, 0x8000, circus_clown_z_w)
    MEM_ADDR(0x1000, 0x1fff, MWA_ROM)
    MEM_ADDR(0xF000, 0xFFFF, MWA_ROM )
MEM_END

/*--------------------------------------------------------------------------
  Interrupt handlers
--------------------------------------------------------------------------*/

/* Circus / Robot Bowl: standard IRQ once per frame */
void circus_interrupt(void)
{
    cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

/* Crash: gets 2 interrupts per frame (set via ipf=2 in the CPU entry) */
void crash_interrupt(void)
{
    cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

/* Ripcord: the MAME driver has a broken interrupt handler that always returns
   ignore_interrupt(). We replicate the same behavior: do not fire IRQ.
   The game reads IN2 ($D000) which increments a counter; the interrupt
   logic may need further investigation to get Ripcord fully working. */
void ripcord_interrupt(void)
{
    /* Ripcord is marked GAME_NOT_WORKING in MAME 0.36.
       For now we fire a normal IRQ and hope for the best. */
    cpu_do_int_imm(CPU0, INT_TYPE_INT);
}

/*--------------------------------------------------------------------------
  Video: line drawing helper

  Draws horizontal or vertical lines only (matching the sync generator
  hardware on the real PCB). Used for borders, diving boards, and
  bowling alley markings.
--------------------------------------------------------------------------*/
static void draw_line(struct osd_bitmap* bitmap, int x1, int y1, int x2, int y2, int dotted)
{
    int col = Machine->pens[1]; /* white */
    int skip = (dotted > 0) ? 2 : 1;

    if (x1 == x2)
    {
        /* Vertical line */
        for (int count = y2; count >= y1; count -= skip)
        {
            if (count >= 0 && count < bitmap->height && x1 >= 0 && x1 < bitmap->width)
                plot_pixel(bitmap, x1, count, col);
        }
    }
    else
    {
        /* Horizontal line */
        for (int count = x2; count >= x1; count -= skip)
        {
            if (count >= 0 && count < bitmap->width && y1 >= 0 && y1 < bitmap->height)
                plot_pixel(bitmap, count, y1, col);
        }
    }
}

/*--------------------------------------------------------------------------
  Video: Robot Bowl scoreboard box helper
--------------------------------------------------------------------------*/
static void draw_robot_box(struct osd_bitmap* bitmap, int x, int y)
{
    int ex = x + 24;
    int ey = y + 26;

    draw_line(bitmap, x,  y,  ex, y,  0);   /* Top */
    draw_line(bitmap, x,  ey, ex, ey, 0);   /* Bottom */
    draw_line(bitmap, x,  y,  x,  ey, 0);   /* Left */
    draw_line(bitmap, ex, y,  ex, ey, 0);   /* Right */

    /* Score grid: horizontal divide + two vertical dividers */
    ey = y + 10;
    draw_line(bitmap, x+8,  ey, ex,  ey, 0);
    draw_line(bitmap, x+8,  y,  x+8, ey, 0);
    draw_line(bitmap, x+16, y,  x+16,ey, 0);
}

/*--------------------------------------------------------------------------
  Video: mark tiles under the sprite as dirty so they get redrawn next frame
--------------------------------------------------------------------------*/
static void mark_sprite_tiles_dirty(int sprite_x, int sprite_y, int sprite_w, int sprite_h)
{
    int sx = sprite_x >> 3;
    int sy = sprite_y >> 3;
    int max_x = sprite_w >> 3;
    int max_y = sprite_h >> 3;

    if (sprite_x & 0x07) max_x++;
    if (sprite_y & 0x07) max_y++;

    for (int y2 = sy; y2 < sy + max_y; y2++)
    {
        for (int x2 = sx; x2 < sx + max_x; x2++)
        {
            if (x2 >= 0 && x2 < 32 && y2 >= 0 && y2 < 32)
                dirtybuffer[x2 + 32 * y2] = 1;
        }
    }
}

/*--------------------------------------------------------------------------
  Video: Circus screen refresh

  Draws the tile layer, the sync-generator borders and diving boards,
  then the clown sprite on top.
--------------------------------------------------------------------------*/
static void circus_vh_screenrefresh(void)
{
    int offs;

    /* Redraw dirty tiles */
    for (offs = videoram_size - 1; offs >= 0; offs--)
    {
       // if (dirtybuffer[offs])
       // {
            dirtybuffer[offs] = 0;

            int sy = offs / 32;
            int sx = offs % 32;

            drawgfx(main_bitmap, Machine->gfx[0],
                    videoram[offs],
                    0,             /* color */
                    0, 0,          /* no flip */
                    8 * sx, 8 * sy,
                    &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
      //  }
    }

    /* Sync generator lines: border and diving boards */
    draw_line(main_bitmap, 0,   18,  255, 18,  0);   /* top border */
    draw_line(main_bitmap, 0,   249, 255, 249, 1);   /* bottom border (dotted) */
    draw_line(main_bitmap, 0,   18,  0,   248, 0);   /* left border */
    draw_line(main_bitmap, 247, 18,  247, 248, 0);   /* right border */

    /* Left diving board */
    draw_line(main_bitmap, 0,   137, 17,  137, 0);
    /* Right diving board */
    draw_line(main_bitmap, 231, 137, 248, 137, 0);
    /* Left lower platform */
    draw_line(main_bitmap, 0,   193, 17,  193, 0);
    /* Right lower platform */
    draw_line(main_bitmap, 231, 193, 248, 193, 0);

    drawgfx(main_bitmap, Machine->gfx[1],
        clown_z,
        0,             /* color */
        0, 0,          /* no flip */
        clown_y, clown_x,  /* note: Y is horiz, X is vert (hardware quirk) */
        &Machine->drv->visible_area, TRANSPARENCY_PEN, 0);



    /* Draw the clown sprite in white */
    //drawgfx(main_bitmap, Machine->gfx[2],clown_z,0,0, 0,clown_y, clown_x,&Machine->drv->visible_area, TRANSPARENCY_PEN, 0);
      
    /* Mark tiles under the sprite dirty for next frame */
    mark_sprite_tiles_dirty(clown_y, clown_x, 16, 16);
}

/*--------------------------------------------------------------------------
  Video: Robot Bowl screen refresh
--------------------------------------------------------------------------*/
static void robotbwl_vh_screenrefresh(void)
{
    int offs;

    /* Redraw dirty tiles */
    for (offs = videoram_size - 1; offs >= 0; offs--)
    {
        if (dirtybuffer[offs])
        {
            dirtybuffer[offs] = 0;

            int sx = offs % 32;
            int sy = offs / 32;

            drawgfx(main_bitmap, Machine->gfx[0],
                    videoram[offs],
                    0, 0, 0,
                    8 * sx, 8 * sy,
                    &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
        }
    }

    /* Sync generator lines: bowling alley and scoreboards */

    /* Scoreboards: three columns of boxes on each side */
    for (offs = 15; offs <= 63; offs += 24)
    {
        draw_robot_box(main_bitmap, offs,       31);
        draw_robot_box(main_bitmap, offs,       63);
        draw_robot_box(main_bitmap, offs,       95);

        draw_robot_box(main_bitmap, offs + 152, 31);
        draw_robot_box(main_bitmap, offs + 152, 63);
        draw_robot_box(main_bitmap, offs + 152, 95);
    }

    /* 10th frame boxes */
    draw_robot_box(main_bitmap, 39,       127);
    draw_line(main_bitmap, 39,       137, 47,       137, 0);  /* extra digit */
    draw_robot_box(main_bitmap, 39 + 152, 127);
    draw_line(main_bitmap, 39 + 152, 137, 47 + 152, 137, 0);

    /* Bowling alley lane markings */
    draw_line(main_bitmap, 103, 17,  103, 205, 0);   /* left gutter solid */
    draw_line(main_bitmap, 111, 17,  111, 203, 1);   /* left lane dotted */
    draw_line(main_bitmap, 152, 17,  152, 205, 0);   /* right gutter solid */
    draw_line(main_bitmap, 144, 17,  144, 203, 1);   /* right lane dotted */

    /* Draw the bowling ball (8x8 sprite, offset by +8 in both axes) */
    drawgfx(main_bitmap, Machine->gfx[1],
            clown_z,
            0, 0, 0,
            clown_y + 8, clown_x + 8,
            &Machine->drv->visible_area, TRANSPARENCY_PEN, 0);

    mark_sprite_tiles_dirty(clown_y, clown_x, 16, 16);
}

/*--------------------------------------------------------------------------
  Video: Crash screen refresh
  (Also used for Ripcord in MAME 0.36)
--------------------------------------------------------------------------*/
static void crash_vh_screenrefresh(void)
{
    int offs;

    /* Redraw dirty tiles */
    for (offs = videoram_size - 1; offs >= 0; offs--)
    {
        if (dirtybuffer[offs])
        {
            dirtybuffer[offs] = 0;

            int sx = offs % 32;
            int sy = offs / 32;

            drawgfx(main_bitmap, Machine->gfx[0],
                    videoram[offs],
                    0, 0, 0,
                    8 * sx, 8 * sy,
                    &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
        }
    }

    /* Draw the car / parachutist sprite */
    drawgfx(main_bitmap, Machine->gfx[1],
            clown_z,
            0, 0, 0,
            clown_y, clown_x,
            &Machine->drv->visible_area, TRANSPARENCY_PEN, 0);


    mark_sprite_tiles_dirty(clown_y, clown_x, 16, 16);
}

/*--------------------------------------------------------------------------
  Video: start / stop
--------------------------------------------------------------------------*/
static int circus_vh_start(void)
{
    /* Point videoram at the CPU memory backing the tile grid */
    videoram = &Machine->memory_region[CPU0][0x4000];
    videoram_size = 0x400;  /* 32x32 = 1024 bytes */

    return generic_vh_start();
}

static void circus_vh_stop(void)
{
    generic_vh_stop();
}

/*--------------------------------------------------------------------------
  Game lifecycle: init / run / end for each variant
--------------------------------------------------------------------------*/

/* --- Circus --- */
int init_circus(void)
{
    game_id = GAME_CIRCUS;
    clown_x = clown_y = clown_z = 0;

    circus_vh_start();
    DAC_sh_start(&circus_dac_interface);

    return 0;
}

void run_circus(void)
{
    watchdog_reset_w(0, 0, 0);
    DAC_sh_update();
    circus_vh_screenrefresh();
}

void end_circus(void)
{
    circus_vh_stop();
    DAC_sh_stop();
}

/* --- Robot Bowl --- */
int init_robotbwl(void)
{
    game_id = GAME_ROBOTBWL;
    clown_x = clown_y = clown_z = 0;

    /* Robot Bowl GFX2 PROM data is bit-inverted; fix it at load time */
    unsigned char* gfx2 = memory_region(REGION_GFX2);
    if (gfx2)
    {
        int len = Machine->memory_region_length[REGION_GFX2];
        for (int i = 0; i < len; i++)
            gfx2[i] ^= 0xFF;
    }

    circus_vh_start();
    DAC_sh_start(&circus_dac_interface);

    return 0;
}

void run_robotbwl(void)
{
    DAC_sh_update();
    robotbwl_vh_screenrefresh();
}

void end_robotbwl(void)
{
    circus_vh_stop();
    DAC_sh_stop();
}

/* --- Crash --- */
int init_crash(void)
{
    game_id = GAME_CRASH;
    clown_x = clown_y = clown_z = 0;

    circus_vh_start();
    DAC_sh_start(&circus_dac_interface);

    return 0;
}

void run_crash(void)
{
    DAC_sh_update();
    crash_vh_screenrefresh();
}

void end_crash(void)
{
    circus_vh_stop();
    DAC_sh_stop();
}

/* --- Ripcord --- */
int init_ripcord(void)
{
    game_id = GAME_RIPCORD;
    clown_x = clown_y = clown_z = 0;

    circus_vh_start();
    DAC_sh_start(&circus_dac_interface);

    return 0;
}

void run_ripcord(void)
{
    DAC_sh_update();
    crash_vh_screenrefresh();  /* Ripcord uses the same refresh as Crash */
}

void end_ripcord(void)
{
    circus_vh_stop();
    DAC_sh_stop();
}

/***************************************************************************
  Input port definitions
***************************************************************************/

/* --- Circus inputs --- */
INPUT_PORTS_START(circus)
    PORT_START("IN0")  /* $A000: control switches */
    PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_START1)
    PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_START2)
    PORT_BIT(0x7c, IP_ACTIVE_HIGH, IPT_UNKNOWN)
    PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_COIN1)

    PORT_START("DSW")  /* $C000: dip switches (bit 7 = VBLANK, managed in circus_DSW_r) */
    PORT_DIPNAME(0x03, 0x00, DEF_STR(Lives))
    PORT_DIPSETTING(0x00, "3")
    PORT_DIPSETTING(0x01, "5")
    PORT_DIPSETTING(0x02, "7")
    PORT_DIPSETTING(0x03, "9")
    PORT_DIPNAME(0x0c, 0x04, DEF_STR(Coinage))
    //	PORT_DIPSETTING(    0x08, DEF_STR( 2C_1C ) )
    PORT_DIPSETTING(0x0c, DEF_STR(2C_1C))
    PORT_DIPSETTING(0x04, DEF_STR(1C_1C))
    PORT_DIPSETTING(0x00, DEF_STR(1C_2C))
    PORT_DIPNAME(0x10, 0x00, "Top Score")
    PORT_DIPSETTING(0x10, "Credit Awarded")
    PORT_DIPSETTING(0x00, "No Award")
    PORT_DIPNAME(0x20, 0x00, "Bonus")
    PORT_DIPSETTING(0x00, "Single Line")
    PORT_DIPSETTING(0x20, "Super Bonus")
    PORT_DIPNAME(0x40, 0x00, DEF_STR(Unknown))
    PORT_DIPSETTING(0x00, DEF_STR(Off))
    PORT_DIPSETTING(0x40, DEF_STR(On))
    PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_VBLANK)

    PORT_START("IN2")  /* $D000: paddle */
    PORT_ANALOG(0xff, 115, IPT_PADDLE, 30, 10, 64, 167)
INPUT_PORTS_END

/* --- Robot Bowl inputs --- */
INPUT_PORTS_START(robotbwl)
    PORT_START("IN0")
    PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_START1 )
    PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_BUTTON1 )
    PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_UNKNOWN )   /* Hook Right */
    PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN )   /* Hook Left */
    PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_BUTTON2 )
    PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT )
    PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT )
    PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_COIN1 )

    PORT_START("DSW")
    PORT_DIPNAME( 0x01, 0x00, "Unknown 1" )
    PORT_DIPSETTING(    0x00, "Off" )
    PORT_DIPSETTING(    0x01, "On" )
    PORT_DIPNAME( 0x02, 0x00, "Unknown 2" )
    PORT_DIPSETTING(    0x00, "Off" )
    PORT_DIPSETTING(    0x02, "On" )
    PORT_DIPNAME( 0x04, 0x04, "Beer Frame" )
    PORT_DIPSETTING(    0x00, "Off" )
    PORT_DIPSETTING(    0x04, "On" )
    PORT_DIPNAME( 0x18, 0x08, "Coinage" )
    PORT_DIPSETTING(    0x10, "2C 1C" )
    PORT_DIPSETTING(    0x08, "1C 1C" )
    PORT_DIPSETTING(    0x00, "1C 2 Players" )
    PORT_DIPNAME( 0x60, 0x00, "Bowl Timer" )
    PORT_DIPSETTING(    0x00, "3 seconds" )
    PORT_DIPSETTING(    0x20, "5 seconds" )
    PORT_DIPSETTING(    0x40, "7 seconds" )
    PORT_DIPSETTING(    0x60, "9 seconds" )
    PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_VBLANK )

    PORT_START("IN2")  /* not used by Robot Bowl but keep it for the shared memory map */
    PORT_BIT( 0xff, IP_ACTIVE_HIGH, IPT_UNKNOWN )
INPUT_PORTS_END

/* --- Crash inputs --- */
INPUT_PORTS_START(crash)
    PORT_START("IN0")
    PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_START1 )
    PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_START2 )
    PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT | IPF_4WAY )
    PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT  | IPF_4WAY )
    PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_BUTTON1 )
    PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP    | IPF_4WAY )
    PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN  | IPF_4WAY )
    PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_COIN1 )

    PORT_START("DSW")
    PORT_DIPNAME( 0x03, 0x01, "Lives" )
    PORT_DIPSETTING(    0x00, "2" )
    PORT_DIPSETTING(    0x01, "3" )
    PORT_DIPSETTING(    0x02, "4" )
    PORT_DIPSETTING(    0x03, "5" )
    PORT_DIPNAME( 0x0c, 0x04, "Coinage" )
    PORT_DIPSETTING(    0x00, "2C 1C" )
    PORT_DIPSETTING(    0x04, "1C 1C" )
    PORT_DIPSETTING(    0x08, "1C 2C" )
    PORT_DIPNAME( 0x10, 0x00, "Top Score" )
    PORT_DIPSETTING(    0x00, "No Award" )
    PORT_DIPSETTING(    0x10, "Credit Awarded" )
    PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_UNKNOWN )
    PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN )
    PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_VBLANK )

    PORT_START("IN2")  /* Crash does not use a paddle; port present for shared mem map */
    PORT_BIT( 0xff, IP_ACTIVE_HIGH, IPT_UNKNOWN )
INPUT_PORTS_END

/* --- Ripcord inputs --- */
INPUT_PORTS_START(ripcord)
    PORT_START("IN0")
    PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_START1 )
    PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_START2 )
    PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_BUTTON1 )
    PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_BUTTON2 )
    PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_UNKNOWN )
    PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_UNKNOWN )
    PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_UNKNOWN )
    PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_COIN1 )

    PORT_START("DSW")
    PORT_DIPNAME( 0x03, 0x03, "Lives" )
    PORT_DIPSETTING(    0x00, "3" )
    PORT_DIPSETTING(    0x01, "5" )
    PORT_DIPSETTING(    0x02, "7" )
    PORT_DIPSETTING(    0x03, "9" )
    PORT_DIPNAME( 0x0c, 0x04, "Coinage" )
    PORT_DIPSETTING(    0x00, "2P 1C" )
    PORT_DIPSETTING(    0x04, "1P 1C" )
    PORT_DIPSETTING(    0x08, "1P 2C" )
    PORT_DIPNAME( 0x10, 0x10, "Top Score" )
    PORT_DIPSETTING(    0x10, "Credit Awarded" )
    PORT_DIPSETTING(    0x00, "No Award" )
    PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_VBLANK )

    PORT_START("IN2")  /* paddle */
    PORT_ANALOG(0xff, 115, IPT_PADDLE, 30, 10, 64, 167)
INPUT_PORTS_END

/***************************************************************************
  ROM definitions
***************************************************************************/

ROM_START(circus)
    ROM_REGION(0x10000, REGION_CPU1, 0)
    ROM_LOAD("9004a.1a", 0x1000, 0x0200, CRC(7654ea75) SHA1(fa29417618157002b8ecb21f4c15104c8145a742))
    ROM_LOAD("9005.2a", 0x1200, 0x0200, CRC(b8acdbc5) SHA1(634bb11089f7a57a316b6829954cc4da4523f267))
    ROM_LOAD("9006.3a", 0x1400, 0x0200, CRC(901dfff6) SHA1(c1f48845456e88d54981608afd00ddb92d97da99))
    ROM_LOAD("9007.5a", 0x1600, 0x0200, CRC(9dfdae38) SHA1(dc59a5f90a5a49fa071aada67eda768d3ecef010))
    ROM_LOAD("9008.6a", 0x1800, 0x0200, CRC(c8681cf6) SHA1(681cfea75bee8a86f9f4645e6c6b94b44762dae9))
    ROM_LOAD("9009.7a", 0x1a00, 0x0200, CRC(585f633e) SHA1(46133409f42e8cbc095dde576ce07d97b235972d))
    ROM_LOAD("9010.8a", 0x1c00, 0x0200, CRC(69cc409f) SHA1(b77289e62313e8535ce40686df7238aa9c0035bc))
    ROM_LOAD("9011.9a", 0x1e00, 0x0200, CRC(aff835eb) SHA1(d6d95510d4a046f48358fef01103bcc760eb71ed))
    ROM_RELOAD(0xfe00, 0x0200) /* for the reset and interrupt vectors */

    ROM_REGION(0x0800, REGION_GFX1, 0)
    ROM_LOAD("9003.4c", 0x0000, 0x0200, CRC(6efc315a) SHA1(d5a4a64a901853fff56df3c65512afea8336aad2))
    ROM_LOAD("9002.3c", 0x0200, 0x0200, CRC(30d72ef5) SHA1(45fc8285e213bf3906a26205a8c0b22f311fd6c3))
    ROM_LOAD("9001.2c", 0x0400, 0x0200, CRC(361da7ee) SHA1(6e6fe5b37ccb4c11aa4abbd9b7df772953abfe7e))
    ROM_LOAD("9000.1c", 0x0600, 0x0200, CRC(1f954bb3) SHA1(62a958b48078caa639b96f62a690583a1c8e83f5))

    ROM_REGION(0x0200, REGION_GFX2, 0)
    ROM_LOAD("9012.14d", 0x0000, 0x0200, CRC(2fde3930) SHA1(a21e2d342f16a39a07edf4bea8d698a52216ecba))
ROM_END

ROM_START(robotbwl)
    ROM_REGION(0x10000, REGION_CPU1, 0)
    ROM_LOAD("robotbwl.1a", 0xf000, 0x0200, CRC(df387a0b) SHA1(97291f1a93cbbff987b0fbc16c2e87ad0db96e12))
    ROM_LOAD("robotbwl.2a", 0xf200, 0x0200, CRC(c948274d) SHA1(1bf8c6e994d601d4e6d30ca2a9da97e140ff5eee))
    ROM_LOAD("robotbwl.3a", 0xf400, 0x0200, CRC(8fdb3ec5) SHA1(a9290edccb8f75e7ec91416d46617516260d5944))
    ROM_LOAD("robotbwl.5a", 0xf600, 0x0200, CRC(ba9a6929) SHA1(9cc6e85431b5d82bf3a624f7b35ddec399ad6c80))
    ROM_LOAD("robotbwl.6a", 0xf800, 0x0200, CRC(16fd8480) SHA1(935bb0c87d25086f326571c83f94f831b1a8b036))
    ROM_LOAD("robotbwl.7a", 0xfa00, 0x0200, CRC(4cadbf06) SHA1(380c10aa83929bfbfd89facb252e68c307545755))
    ROM_LOAD("robotbwl.8a", 0xfc00, 0x0200, CRC(bc809ed3) SHA1(2bb4cdae8c9619eebea30cc323960a46a509bb58))
    ROM_LOAD("robotbwl.9a", 0xfe00, 0x0200, CRC(07487e27) SHA1(b5528fb3fec474df2b66f36e28df13a7e81f9ce3))

    ROM_REGION(0x0800, REGION_GFX1, 0)
    ROM_LOAD("robotbwl.4c", 0x0000, 0x0200, CRC(a5f7acb9) SHA1(556dd34d0fa50415b128477e208e96bf0c050c2c))
    ROM_LOAD("robotbwl.3c", 0x0200, 0x0200, CRC(d5380c9b) SHA1(b9670e87011a1b3aebd1d386f1fe6a74f8c77be9))
    ROM_LOAD("robotbwl.2c", 0x0400, 0x0200, CRC(47b3e39c) SHA1(393c680fba3bd384e2c773150c3bae4d735a91bf))
    ROM_LOAD("robotbwl.1c", 0x0600, 0x0200, CRC(b2991e7e) SHA1(32b6d42bb9312d6cbe5b4113fcf2262bfeef3777))

    ROM_REGION(0x0020, REGION_GFX2, 0)
    ROM_LOAD("robotbwl.14d", 0x0000, 0x0020, CRC(a402ac06) SHA1(3bd75630786bcc86d9e9fbc826adc909eef9b41f))
ROM_END

ROM_START(crash)
    ROM_REGION(0x10000, REGION_CPU1, 0)
    ROM_LOAD("crash.a1",  0x1000, 0x0200, CRC(b9571203) SHA1(0))
    ROM_LOAD("crash.a2",  0x1200, 0x0200, CRC(b4581a95) SHA1(0))
    ROM_LOAD("crash.a3",  0x1400, 0x0200, CRC(597555ae) SHA1(0))
    ROM_LOAD("crash.a4",  0x1600, 0x0200, CRC(0a15d69f) SHA1(0))
    ROM_LOAD("crash.a5",  0x1800, 0x0200, CRC(a9c7a328) SHA1(0))
    ROM_LOAD("crash.a6",  0x1a00, 0x0200, CRC(c7d62d27) SHA1(0))
    ROM_LOAD("crash.a7",  0x1c00, 0x0200, CRC(5e5af244) SHA1(0))
    ROM_LOAD("crash.a8",  0x1e00, 0x0200, CRC(3dc50839) SHA1(0))
    ROM_RELOAD(            0xfe00, 0x0200)  /* reset and interrupt vectors */

    ROM_REGION(0x0800, REGION_GFX1, 0)
    ROM_LOAD("crash.c4",  0x0000, 0x0200, CRC(ba16f9e8) SHA1(0))
    ROM_LOAD("crash.c3",  0x0200, 0x0200, CRC(3c8f7560) SHA1(0))
    ROM_LOAD("crash.c2",  0x0400, 0x0200, CRC(38f3e4ed) SHA1(0))
    ROM_LOAD("crash.c1",  0x0600, 0x0200, CRC(e9adf1e1) SHA1(0))

    ROM_REGION(0x0200, REGION_GFX2, 0)
    ROM_LOAD("crash.d14", 0x0000, 0x0200, CRC(833f81e4) SHA1(0))
ROM_END

ROM_START(ripcord)
    ROM_REGION(0x10000, REGION_CPU1, 0)
    ROM_LOAD("9027.1a",  0x1000, 0x0200, CRC(56b8dc06) SHA1(0))
    ROM_LOAD("9028.2a",  0x1200, 0x0200, CRC(a8a78a30) SHA1(0))
    ROM_LOAD("9029.4a",  0x1400, 0x0200, CRC(fc5c8e07) SHA1(0))
    ROM_LOAD("9030.5a",  0x1600, 0x0200, CRC(b496263c) SHA1(0))
    ROM_LOAD("9031.6a",  0x1800, 0x0200, CRC(cdc7d46e) SHA1(0))
    ROM_LOAD("9032.7a",  0x1a00, 0x0200, CRC(a6588bec) SHA1(0))
    ROM_LOAD("9033.8a",  0x1c00, 0x0200, CRC(fd49b806) SHA1(0))
    ROM_LOAD("9034.9a",  0x1e00, 0x0200, CRC(7caf926d) SHA1(0))
    ROM_RELOAD(           0xfe00, 0x0200)  /* reset and interrupt vectors */

    ROM_REGION(0x0800, REGION_GFX1, 0)
    ROM_LOAD("9026.5c",  0x0000, 0x0200, CRC(06e7adbb) SHA1(0))
    ROM_LOAD("9025.4c",  0x0200, 0x0200, CRC(3129527e) SHA1(0))
    ROM_LOAD("9024.2c",  0x0400, 0x0200, CRC(bcb88396) SHA1(0))
    ROM_LOAD("9023.1c",  0x0600, 0x0200, CRC(9f86ed5b) SHA1(0))

    ROM_REGION(0x0200, REGION_GFX2, 0)
    ROM_LOAD("9035.14d", 0x0000, 0x0200, CRC(c9979802) SHA1(0))
ROM_END

/***************************************************************************
  Driver registrations

  Hardware: Single 6502 at 705,562 Hz (11.289 MHz / 16), 57 fps.
  cpu_div = 100 (slices per frame).
  ipf = 1 for Circus/Robot Bowl/Ripcord, 2 for Crash (2 IRQs per frame).
***************************************************************************/

/* --- Circus (1977, Exidy) --- */
AAE_DRIVER_BEGIN(drv_circus, "circus", "Circus")
AAE_DRIVER_ROM(rom_circus)
AAE_DRIVER_FUNCS(&init_circus, &run_circus, &end_circus)
AAE_DRIVER_INPUT(input_ports_circus)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
    AAE_CPU_ENTRY(
        /*type*/     CPU_M6502,
        /*freq*/     705562,       /* 11.289 MHz / 16 */
        /*div*/      100,
        /*ipf*/      1,
        /*int type*/ INT_TYPE_INT,
        /*int cb*/   &circus_interrupt,
        /*r8*/       circus_readmem,
        /*w8*/       circus_writemem,
        /*pr*/       nullptr,
        /*pw*/       nullptr,
        /*r16*/      nullptr,
        /*w16*/      nullptr
    ),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(57, 1000, VIDEO_TYPE_RASTER_BW | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(32*8, 32*8, 0*8, 31*8-1, 0*8, 32*8-1)
AAE_DRIVER_RASTER(circus_gfxdecodeinfo, 2, 2, circus_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

/* --- Robot Bowl (1977, Exidy) --- */
AAE_DRIVER_BEGIN(drv_robotbwl, "robotbwl", "Robot Bowl")
AAE_DRIVER_ROM(rom_robotbwl)
AAE_DRIVER_FUNCS(&init_robotbwl, &run_robotbwl, &end_robotbwl)
AAE_DRIVER_INPUT(input_ports_robotbwl)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
    AAE_CPU_ENTRY(
        /*type*/     CPU_M6502,
        /*freq*/     705562,
        /*div*/      100,
        /*ipf*/      1,
        /*int type*/ INT_TYPE_INT,
        /*int cb*/   &circus_interrupt,
        /*r8*/       circus_readmem,
        /*w8*/       circus_writemem,
        /*pr*/       nullptr,
        /*pw*/       nullptr,
        /*r16*/      nullptr,
        /*w16*/      nullptr
    ),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(57, DEFAULT_REAL_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_BW | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(32*8, 32*8, 0*8, 31*8-1, 0*8, 32*8-1)
AAE_DRIVER_RASTER(robotbwl_gfxdecodeinfo, 2, 2, circus_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

/* --- Crash (1979, Exidy) --- */
AAE_DRIVER_BEGIN(drv_crash, "crash", "Crash")
AAE_DRIVER_ROM(rom_crash)
AAE_DRIVER_FUNCS(&init_crash, &run_crash, &end_crash)
AAE_DRIVER_INPUT(input_ports_crash)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
    AAE_CPU_ENTRY(
        /*type*/     CPU_M6502,
        /*freq*/     705562,
        /*div*/      100,
        /*ipf*/      2,            /* Crash gets 2 IRQs per frame */
        /*int type*/ INT_TYPE_INT,
        /*int cb*/   &crash_interrupt,
        /*r8*/       circus_readmem,
        /*w8*/       circus_writemem,
        /*pr*/       nullptr,
        /*pw*/       nullptr,
        /*r16*/      nullptr,
        /*w16*/      nullptr
    ),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(57, DEFAULT_REAL_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_BW | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(32*8, 32*8, 0*8, 31*8-1, 0*8, 32*8-1)
AAE_DRIVER_RASTER(circus_gfxdecodeinfo, 2, 2, circus_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

/* --- Ripcord (1977, Exidy) - marked not working in MAME 0.36 --- */
AAE_DRIVER_BEGIN(drv_ripcord, "ripcord", "Rip Cord")
AAE_DRIVER_ROM(rom_ripcord)
AAE_DRIVER_FUNCS(&init_ripcord, &run_ripcord, &end_ripcord)
AAE_DRIVER_INPUT(input_ports_ripcord)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
    AAE_CPU_ENTRY(
        /*type*/     CPU_M6502,
        /*freq*/     705562,
        /*div*/      100,
        /*ipf*/      1,
        /*int type*/ INT_TYPE_INT,
        /*int cb*/   &ripcord_interrupt,
        /*r8*/       circus_readmem,
        /*w8*/       circus_writemem,
        /*pr*/       nullptr,
        /*pw*/       nullptr,
        /*r16*/      nullptr,
        /*w16*/      nullptr
    ),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(57, DEFAULT_REAL_60HZ_VBLANK_DURATION, VIDEO_TYPE_RASTER_BW | VIDEO_SUPPORTS_DIRTY, ORIENTATION_DEFAULT)
AAE_DRIVER_SCREEN(32*8, 32*8, 0*8, 31*8-1, 0*8, 32*8-1)
AAE_DRIVER_RASTER(circus_gfxdecodeinfo, 2, 2, circus_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

/*--------------------------------------------------------------------------
  Auto-register all four drivers with the AAE driver registry
--------------------------------------------------------------------------*/
AAE_REGISTER_DRIVER(drv_circus)
AAE_REGISTER_DRIVER(drv_robotbwl)
AAE_REGISTER_DRIVER(drv_crash)
AAE_REGISTER_DRIVER(drv_ripcord)
