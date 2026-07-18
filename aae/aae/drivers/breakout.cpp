//==========================================================================
// Breakout (Atari 1976) -- behavioral simulation for AAE.
//
// Not a gate-level port. Real Breakout is discrete logic (no CPU, no ROMs);
// this driver reproduces the *gameplay* with clean 2D physics rendered through
// the same AAE raster + mixer plumbing that drivers/pong.cpp uses. The DICE
// netlist (dice.0.9.src/games/breakout.cpp) is the reference for authentic
// colors, orientation, and gameplay triggers -- none of its logic is ported.
//==========================================================================

#include "aae_mame_driver.h"
#include "driver_registry.h"
#include "old_mame_raster.h"
#include "mixer.h"
#include <math.h>

//--- Layout constants (TUNE) ----------------------------------------------
#define BM_W        288             /* 216 x 288 = exact 3:4 portrait, matches the MAME reference */
#define BM_H        216
#define WALL        4               /* white side walls (~0.02W, per reference) */
#define PLAY_L      (WALL)
#define PLAY_R      (BM_H - WALL)
#define TOP_BAR_TH  9               /* wall across the very top (~0.03H, per reference) */
#define TEXT_SC     4               /* digit scale for the counters and scores (per reference) */
#define COUNTER_D   10              /* ball-used counter row, ~0.035H (below the top wall) */
#define SCORE_D     32              /* score row, ~0.11H (below the counters) */
#define SCORE_DIGITS 3
#define BRICK_ROWS  8
#define BRICK_COLS  14              /* columns computed to span the play width, flush to both walls */
#define BRICK_H     5               /* brick row height (~5.4px/row in the reference) */
#define BRICK_TOP_D 54              /* ~0.19H down, matches the reference */
#define CEIL_D      (TOP_BAR_TH)    /* ball bounces off the TOP WALL (TOP_BOUND in the
                                       netlist), NOT the brick top -- the ball can break
                                       through and rattle in the gap above the bricks */
#define PADDLE_D    261             /* blue paddle line ~0.91H down; play area ends here */
#define PADDLE_TH   5
#define PADDLE_FULL 18
#define PADDLE_HALF 9
#define BALL_W      4               /* ball is wider (lateral) ... */
#define BALL_H      2               /* ... than it is tall (depth) */
#define LOSE_D      (PADDLE_D + PADDLE_TH)   /* just past the blue line = lost */
#define BREAKOUT_FPS 60

//--- Color gel overlay ------------------------------------------------------
// Breakout is a BLACK & WHITE game: the hardware outputs only white pixels,
// and the cabinet colors them with gel strips laid over the monitor (MAME and
// DICE reproduce the same strips as a video overlay). Everything below draws
// white and is tinted here by screen DEPTH band -- so the side walls, the
// ball, and anything else crossing a strip picks up its color, exactly like
// the real gels.
static inline int gel_pen(int d)
{
    if (d >= BRICK_TOP_D && d < BRICK_TOP_D + BRICK_ROWS * BRICK_H)
        return 2 + (d - BRICK_TOP_D) / (2 * BRICK_H);   // red/orange/green/yellow strips
    if (d >= PADDLE_D && d < PADDLE_D + PADDLE_TH)
        return 6;                                       // blue paddle strip
    return 1;                                           // clear: white
}

// Cocktail flip: latched once per frame. On the real board the S2 cabinet
// dip gates P2_CONDITIONAL, which XORs the H and V counters while player 2
// is up -- the whole video (scores included) rotates 180 so the player on
// the opposite side of the table sees the game right-side-up.
static int screen_flipped;

//--- Draw helper: game (depth,lateral) -> rotated bitmap ------------------
static inline void px(int d, int x)
{
    // Display is ROT90 (SWAP_XY|FLIP_X), which mirrors the lateral axis on
    // screen. Pre-flip lateral here so game x=0 renders at screen-left for
    // every caller -- fixes mirrored score digits and the inverted paddle
    // direction in one spot, leaving all game logic in plain coordinates.
    if ((unsigned)d >= (unsigned)BM_W || (unsigned)x >= (unsigned)BM_H)
        return;

    // Gel tint comes from GAME-space depth (the strips follow the image, as
    // MAME does); the cocktail flip is applied after, at the plot.
    int pen = Machine->pens[gel_pen(d)];
    if (screen_flipped) { d = BM_W - 1 - d; x = BM_H - 1 - x; }
    plot_pixel(tmpbitmap, d, (BM_H - 1) - x, pen);
}

static void fill_rect(int d0, int x0, int d1, int x1)
{
    for (int d = d0; d < d1; d++)
        for (int x = x0; x < x1; x++)
            px(d, x);
}

//--- Game state -----------------------------------------------------------
static int  bricks[BRICK_ROWS][BRICK_COLS]; // 1 = present, 0 = cleared
static int  score;
static int  paddle_x;      // lateral of paddle left edge
static int  paddle_w;      // current paddle width (PADDLE_FULL or PADDLE_HALF)

enum { ST_ATTRACT, ST_SERVE_WAIT, ST_PLAYING };
static int   state;
static int   lives;
static float ball_d, ball_x;    // ball top-left (depth, lateral), float
static float vel_d, vel_x;      // velocity per frame
static float ball_speed;        // current speed magnitude (px/frame)
static int   prev_start, prev_serve;
static int hit_count;        // paddle volleys this ball (for 4th/12th speed-ups)
static int topwall_hit;      // paddle shrunk this BALL? (F5 latch; cleared by SERVE_WAIT)
static int screen_num;       // 0 = first wall, 1 = second wall
static int walls_done;       // both walls cleared: no more bricks, play out the balls
static int wall_pending;     // wall 1 cleared: the second wall paints on the NEXT
                             // paddle touch (FPD one-shots fire on score-complete
                             // AND BP_HIT in the netlist)
static int brick_armed;      // may the ball destroy a brick? (F7 latch: one brick per
                             // paddle/top-wall trip; an unarmed ball GHOSTS through bricks)
static int serve_seed;       // cycles the "random" serve direction
static int ball_num;         // ball being played now (1..3/5); shown upper-RIGHT
static int player_up;        // 0-based index of the player at the controls;
                             // shown upper-LEFT as 1/2
static int num_players;      // 1 or 2 (START1 / START2)

// Per-player saved state, like the two halves of the 82S16 brick RAM plus
// the per-player score/ball counters. The globals above always hold the
// ACTIVE player's state; these hold the benched player's.
struct breakout_player
{
    int bricks[BRICK_ROWS][BRICK_COLS];
    int score, lives, ball_num;
    int screen_num, walls_done, wall_pending;
};
static struct breakout_player pl[2];
static int frame_tick;       // free-running frame counter, drives the score blink
static int ball_live;        // ball in flight (drawn + simulated)
static int attract_delay;    // frames until the attract ball re-serves itself
static int credits;          // inserted credits; START1 takes one, START2 two
static int prev_coin, prev_start2;
static int game_played;      // 0 until the first game: power-on attract shows the
                             // fresh-latch state (ball roams the whole screen,
                             // ghosting through the bricks)

// Four discrete ball speeds (px/frame), per the manual's 4-speed model. The
// wide spread (fastest ~3.4x the serve speed) is why the ball "gets very fast"
// late; replace these with numbers frame-counted from real footage when you
// have them.
#define SPEED_1           2.3f  // TUNE: slowest -- the serve speed
#define SPEED_2           4.0f  // TUNE: after the 4th volley
#define SPEED_3           5.7f  // TUNE: after the 12th volley
#define SPEED_4           9.5f  // TUNE: fastest -- set instantly on a 5/7-point brick
#define PADDLE_VX_OUTER   1.2f  // TUNE: |horizontal| rebound off the outer paddle halves
#define PADDLE_VX_INNER   0.5f  // TUNE: |horizontal| rebound off the inner paddle halves
#define SERVE_D           ((BRICK_TOP_D + PADDLE_D) / 2)  // ball appears about midway
// Sound gate lengths from the 9602 one-shot RCs (T ~= 0.34*R*C):
//   paddle & brick: A8, 27k / 1uF  ~9 ms  -> 1 frame
//   wall (VB hit):  A7, 68k / 1uF ~23 ms  -> 2 frames
#define SND_GATE_SHORT    1
#define SND_GATE_WALL     2
#define FLASH_FRAMES      16    // TUNE: active-player score-blink half-period (frames)
#define ATTRACT_SERVE_DELAY 90  // frames the attract ball waits before re-serving

// row -> points (row 0 = top); the colors come from the gel strips.
static const int row_points[BRICK_ROWS] = { 7,7, 5,5, 3,3, 1,1 };

//--- 3x5 digit font, one byte per row, low 3 bits = pixels (msb = left) ----
static const unsigned char digit_font[10][5] = {
    {0b111,0b101,0b101,0b101,0b111}, // 0
    {0b010,0b010,0b010,0b010,0b010}, // 1  
    {0b111,0b001,0b111,0b100,0b111}, // 2
    {0b111,0b001,0b111,0b001,0b111}, // 3
    {0b101,0b101,0b111,0b001,0b001}, // 4
    {0b111,0b100,0b111,0b001,0b111}, // 5
    {0b111,0b100,0b111,0b101,0b111}, // 6
    {0b111,0b001,0b010,0b010,0b010}, // 7
    {0b111,0b101,0b111,0b101,0b111}, // 8
    {0b111,0b101,0b111,0b001,0b111}, // 9
};

// draw one digit with each font pixel scaled to sc x sc, top-left at (d,x)
static void draw_digit(int d, int x, int n, int sc)
{
    n %= 10;
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++)
            if (digit_font[n][r] & (1 << (2 - c)))
                fill_rect(d + r * sc, x + c * sc, d + r * sc + sc, x + c * sc + sc);
}

// draw `value` as exactly `ndigits` zero-padded digits, right edge at x_right,
// top at depth d
static void draw_number(int d, int x_right, int value, int ndigits, int sc)
{
    int glyph_w = 3 * sc + sc;      // 3 pixel columns + 1 column spacing
    int x = x_right - glyph_w;
    for (int i = 0; i < ndigits; i++)
    {
        draw_digit(d, x, value % 10, sc);
        value /= 10;
        x -= glyph_w;
    }
}

static void reset_wall(void)
{
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++)
            bricks[r][c] = 1;
}

// Stash the active globals into pl[i] / restore them. Used when alternating
// players in a 2-player game.
static void save_player(int i)
{
    memcpy(pl[i].bricks, bricks, sizeof(bricks));
    pl[i].score = score;         pl[i].lives = lives;
    pl[i].ball_num = ball_num;   pl[i].screen_num = screen_num;
    pl[i].walls_done = walls_done; pl[i].wall_pending = wall_pending;
}

static void load_player(int i)
{
    memcpy(bricks, pl[i].bricks, sizeof(bricks));
    score = pl[i].score;         lives = pl[i].lives;
    ball_num = pl[i].ball_num;   screen_num = pl[i].screen_num;
    walls_done = pl[i].walls_done; wall_pending = pl[i].wall_pending;
}

static void place_ball_on_paddle(void)
{
    ball_x = (float)(paddle_x + paddle_w / 2 - BALL_W / 2);
    ball_d = (float)(PADDLE_D - BALL_H);
    vel_d = vel_x = 0.0f;
    hit_count = 0;
}

static void serve_ball(void)
{
    // Per the manual, the served ball appears about midway down the screen and
    // moves toward the paddle (not launched up from it), at the slowest speed,
    // in one of four "random" directions -- never straight down.
    static const float dirs[4] = { -PADDLE_VX_OUTER, -PADDLE_VX_INNER,
                                     PADDLE_VX_INNER,  PADDLE_VX_OUTER };
    ball_speed = SPEED_1;
    ball_d = (float)SERVE_D;
    ball_x = (float)(BM_H / 2 - BALL_W / 2);
    vel_x = dirs[serve_seed & 3];
    serve_seed++;
    vel_d = sqrtf(ball_speed * ball_speed - vel_x * vel_x);  // +d = toward the paddle
    hit_count = 0;
    brick_armed = 1;
    // The half-paddle latch (F5) is cleared by SERVE_WAIT_n on the real
    // hardware: the shrink lasts only until the ball is lost, and every new
    // serve starts with the full paddle.
    topwall_hit = 0;
    paddle_w = PADDLE_FULL;
    ball_live = 1;
}

static void start_new_game(int players)
{
    int balls = (readinputportbytag("DSW") & 1) ? 5 : 3;   // S4 ball-count DIP

    num_players = players;
    game_played = 1;

    // fresh state for both players (player 2's stays parked if unused)
    score = 0;
    lives = balls;
    ball_num = 1;
    screen_num = 0;
    walls_done = 0;
    wall_pending = 0;
    reset_wall();
    save_player(0);
    save_player(1);
    player_up = 0;

    paddle_w = PADDLE_FULL;
    place_ball_on_paddle();
    state = ST_SERVE_WAIT;
    hit_count = 0;
    topwall_hit = 0;
    ball_live = 0;
}

// returns 1 on the rising edge of button `cur` given previous `prev`
static int rising(int cur, int* prev)
{
    int edge = (cur && !*prev);
    *prev = cur;
    return edge;
}

// Map a pixel (depth,lateral) to a brick cell; return 1 and set r,c if the
// pixel lies on a present brick.
static int brick_at(int d, int x, int* r, int* c)
{
    if (d < BRICK_TOP_D || d >= BRICK_TOP_D + BRICK_ROWS * BRICK_H) return 0;
    if (x < PLAY_L || x >= PLAY_R) return 0;
    int rr = (d - BRICK_TOP_D) / BRICK_H;
    int cc = (x - PLAY_L) * BRICK_COLS / (PLAY_R - PLAY_L);
    if (rr < 0 || rr >= BRICK_ROWS || cc < 0 || cc >= BRICK_COLS) return 0;
    if (!bricks[rr][cc]) return 0;
    *r = rr; *c = cc;
    return 1;
}

//==========================================================================
// Sound: three gated square waves on one mixer stream (adapted from pong.cpp)
//==========================================================================
static int      bo_channel = -1;
static int16_t* bo_frame_buf = nullptr;
static int      bo_frame_len = 0;
static double   bo_phase[3] = { 0, 0, 0 };

// event gates: set to a small positive frame count when the event fires
static int bo_paddle_snd = 0;   // paddle bounce
static int bo_wall_snd   = 0;   // wall bounce
static int bo_brick_snd  = 0;   // brick hit

// Fire a sound gate. In attract mode the 9602 one-shots are held off by
// ATTRACT_n on the real hardware, so the attract ball bounces silently.
static void snd(int* gate, int frames)
{
    if (state != ST_ATTRACT) *gate = frames;
}

// Tone frequencies (Hz), from the ball-counter taps in the netlist (the
// counters tick at line rate, ~15750 Hz):
//   paddle = P_HIT_SOUND  = B7 QC -> /8  ~= 1970 Hz
//   wall   = VB_HIT_SOUND = B7 QD -> /16 ~=  985 Hz
//   brick  = BRICK_SOUND  = B8 QA -> /32 ~=  492 Hz
// (On real hardware the counters are reloaded from the ball position, so the
// pitch wobbles slightly; these are the nominal centers. TUNE by ear.)
static const double bo_freq[3] = { 1970.0, 985.0, 492.0 };  // paddle, wall, brick

static int bo_sh_start(void)
{
    bo_frame_len = config.samplerate / BREAKOUT_FPS;
    bo_channel = mixer_alloc_channel(MIXER_CHIP_STREAM_RANGE_LOW, MIXER_FIRST_RESERVED_CHANNEL);
    if (bo_channel < 0) { LOG_INFO("Breakout: no free mixer channel"); return 1; }
    bo_frame_buf = (int16_t*)malloc(bo_frame_len * sizeof(int16_t));
    if (!bo_frame_buf) return 1;
    memset(bo_frame_buf, 0, bo_frame_len * sizeof(int16_t));
    stream_start(bo_channel, 0, 16, BREAKOUT_FPS, /*stereo=*/false);
    return 0;
}

static void bo_sh_stop(void)
{
    if (bo_channel >= 0) stream_stop(bo_channel, 0);
    bo_channel = -1;
    free(bo_frame_buf);
    bo_frame_buf = nullptr;
}

static void bo_sh_update(void)
{
    const int gate[3] = { bo_paddle_snd, bo_wall_snd, bo_brick_snd };
    const int amp = 6000;
    if (bo_channel < 0) return;

    for (int s = 0; s < bo_frame_len; s++)
    {
        int v = 0;
        for (int t = 0; t < 3; t++)
        {
            bo_phase[t] += bo_freq[t] / (double)config.samplerate;
            if (bo_phase[t] >= 1.0) bo_phase[t] -= 1.0;
            if (gate[t]) v += (bo_phase[t] < 0.5) ? amp : -amp;
        }
        bo_frame_buf[s] = (int16_t)v;
    }
    stream_update(bo_channel, bo_frame_buf);

    // gates are one-shot: each is a short countdown of frames
    if (bo_paddle_snd > 0) bo_paddle_snd--;
    if (bo_wall_snd   > 0) bo_wall_snd--;
    if (bo_brick_snd  > 0) bo_brick_snd--;
}

// Test the ball's leading edge against the wall; on a hit, clear the brick,
// score it, reflect vertically, and return 1.
static int hit_bricks(void)
{
    int r, c;
    int cd = (int)(ball_d + BALL_H / 2);       // ball center depth
    int lead = (vel_d < 0) ? (int)ball_d : (int)(ball_d + BALL_H); // leading edge
    int cx = (int)(ball_x + BALL_W / 2);

    // Power-on attract (no game played yet, no coin): fresh-latch state --
    // the demo ball ghosts through the whole wall and roams the full screen.
    if (state == ST_ATTRACT && !game_played && credits == 0) return 0;

    if (!brick_armed) return 0;   // unarmed: the ball GHOSTS through bricks (F7 latch)

    if (brick_at(lead, cx, &r, &c) || brick_at(cd, cx, &r, &c))
    {
        // In attract mode the brick RAM write and the score count are gated
        // off (ATTRACT_n): the ball bounces off bricks but never removes them.
        // Real-hardware attract footage shows the ball trapped at the top,
        // rattling between the top brick row and the top border.
        if (state != ST_ATTRACT)
        {
            bricks[r][c] = 0;
            score += row_points[r];
            // a 5- or 7-point brick (the two back colour bands) jumps the ball
            // to its fastest speed immediately, per the manual.
            if (row_points[r] >= 5) ball_speed = SPEED_4;
        }
        vel_d = -vel_d;
        brick_armed = 0;          // must hit the paddle or top wall before the next
        snd(&bo_brick_snd, SND_GATE_SHORT);
        return 1;
    }
    return 0;
}

static int wall_empty(void)
{
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++)
            if (bricks[r][c]) return 0;
    return 1;
}

// Resolve all collisions at the ball's current position -- bricks, side walls,
// the ceiling/backwall, and the paddle. Called once per sub-step.
static void ball_collisions(void)
{
    hit_bricks();

    // side walls
    if (ball_x < PLAY_L)               { ball_x = PLAY_L;               vel_x = -vel_x; snd(&bo_wall_snd, SND_GATE_WALL); }
    if (ball_x > PLAY_R - BALL_W)      { ball_x = PLAY_R - BALL_W;      vel_x = -vel_x; snd(&bo_wall_snd, SND_GATE_WALL); }

    // top wall (TOP_BOUND / BTB_HIT): the ball travels the open gap above the
    // bricks and rattles between here and the brick tops after breaking through
    if (ball_d < CEIL_D)
    {
        ball_d = CEIL_D;
        vel_d = -vel_d;
        snd(&bo_wall_snd, SND_GATE_WALL);
        brick_armed = 1;          // the top wall re-arms the ball for another brick
        if (!topwall_hit) { topwall_hit = 1; paddle_w = PADDLE_HALF; }
    }

    // Attract, per real-hardware footage: WITH a credit inserted, the
    // serve-wait logic parks the paddle hardware as the full-width bar and it
    // reflects -- the demo ball ends up trapped below, rattling between this
    // bar and the bottom brick row. WITHOUT a credit the bar is inert: the
    // ball falls through, wraps back in at the top, and ends up trapped
    // above, rattling between the top border and the top brick row.
    if (state == ST_ATTRACT)
    {
        // The bar reflects at power-on (fresh latches) and whenever a credit
        // is in (serve-wait parks the paddle full width). Only the post-game
        // uncredited attract leaves it inert, letting the ball wrap.
        if ((credits > 0 || !game_played) &&
            vel_d > 0 &&
            ball_d + BALL_H >= PADDLE_D &&
            ball_d + BALL_H <= PADDLE_D + PADDLE_TH)
        {
            ball_d = PADDLE_D - BALL_H;
            vel_d = -vel_d;
        }
        return;
    }

    // paddle
    if (vel_d > 0 &&
        ball_d + BALL_H >= PADDLE_D &&
        ball_d + BALL_H <= PADDLE_D + PADDLE_TH &&
        ball_x + BALL_W > paddle_x &&
        ball_x < paddle_x + paddle_w)
    {
        ball_d = PADDLE_D - BALL_H;
        snd(&bo_paddle_snd, SND_GATE_SHORT);
        brick_armed = 1;          // the paddle re-arms the ball for another brick

        // FPD: the second wall is painted by a one-shot that fires only on
        // score-complete AND a paddle hit -- the cleared field stays empty
        // until the ball touches the paddle (verified on real-PCB footage).
        if (wall_pending)
        {
            reset_wall();
            screen_num = 1;
            wall_pending = 0;
        }

        // offset -1.0 (left edge) .. +1.0 (right edge) of paddle center
        float center = paddle_x + paddle_w / 2.0f;
        float off = ((ball_x + BALL_W / 2.0f) - center) / (paddle_w / 2.0f);
        if (off < -1.0f) off = -1.0f;
        if (off >  1.0f) off =  1.0f;

        // Four discrete rebound directions (Fig 3-4): the outer half of the
        // paddle kicks the ball out with a wider horizontal speed, the inner
        // half a gentler one. The horizontal speed is a fixed magnitude (never
        // zero -> the ball is never perpendicular to the paddle), which also
        // makes the rebound steeper as ball_speed increases.
        float ax = (off < -0.5f || off > 0.5f) ? PADDLE_VX_OUTER : PADDLE_VX_INNER;
        vel_x = (off < 0.0f) ? -ax : ax;
        float vd2 = ball_speed * ball_speed - vel_x * vel_x;
        vel_d = -sqrtf(vd2 > 0.04f ? vd2 : 0.04f);   // always upward

        ++hit_count;
        if (hit_count == 4)  ball_speed = fmaxf(ball_speed, SPEED_2);
        if (hit_count == 12) ball_speed = fmaxf(ball_speed, SPEED_3);
    }
}

static void update_ball(void)
{
    // Move the ball in <=1px sub-steps so it can't tunnel through the paddle or
    // a brick at high speed. Total travel this frame = ball_speed.
    float budget = ball_speed;
    for (int guard = 0; budget > 0.001f && guard < 64; guard++)
    {
        float vmag = sqrtf(vel_d * vel_d + vel_x * vel_x);
        if (vmag < 0.001f) break;
        float step = (budget < 1.0f) ? budget : 1.0f;
        ball_d += vel_d / vmag * step;
        ball_x += vel_x / vmag * step;
        ball_collisions();
        if (ball_d > LOSE_D) break;      // fell past the paddle line
        budget -= step;
    }

    // lost (ball passed the blue paddle line)
    if (ball_d > LOSE_D)
    {
        if (state == ST_ATTRACT)
        {
            // Uncredited attract: the ball counters wrap through the frame;
            // the ball re-enters at the top and ends up trapped there,
            // rattling between the top brick row and the top border.
            ball_d = (float)CEIL_D;
            brick_armed = 1;
            return;
        }
        ball_live = 0;
        lives--;
        ball_num++;              // this player's NEXT ball, if they get one
        save_player(player_up);

        // 2-player games alternate on every miss (PLAY_CP clocks the B4
        // player counter); a player with no balls left is skipped.
        int nxt = -1;
        if (num_players == 2 && pl[1 - player_up].lives > 0)
            nxt = 1 - player_up;
        else if (lives > 0)
            nxt = player_up;

        if (nxt < 0)
        {
            // all balls gone: drop straight into attract on the leftover
            // field, like the real EGL -> ATTRACT transition (scores stay)
            state = ST_ATTRACT;
            attract_delay = ATTRACT_SERVE_DELAY;
        }
        else
        {
            player_up = nxt;
            load_player(nxt);
            place_ball_on_paddle();
            state = ST_SERVE_WAIT;
        }
    }
    else if (state == ST_PLAYING && !walls_done && !wall_pending && wall_empty())
    {
        if (screen_num == 0)
        {
            // wall 1 complete: arm the FPD repaint -- the second wall appears
            // on the next paddle touch, not immediately
            wall_pending = 1;
        }
        else
        {
            // both walls cleared (896 max): the wall never regenerates; the
            // ball plays on over the empty field until the balls run out
            walls_done = 1;
        }
    }
}

//==========================================================================
// Video
//==========================================================================
static int breakout_vh_start(void)
{
    tmpbitmap = osd_create_bitmap(BM_W, BM_H);
    if (!tmpbitmap)
        return 1;
    osd_clearbitmap(tmpbitmap);
    return 0;
}

static void breakout_vh_stop(void)
{
    if (tmpbitmap) { osd_free_bitmap(tmpbitmap); tmpbitmap = nullptr; }
}

static void draw_bricks(void)
{
    int pw = PLAY_R - PLAY_L;
    for (int r = 0; r < BRICK_ROWS; r++)
    {
        int d0 = BRICK_TOP_D + r * BRICK_H;
        for (int c = 0; c < BRICK_COLS; c++)
        {
            if (!bricks[r][c]) continue;
            int x0 = PLAY_L + c * pw / BRICK_COLS;
            int x1 = PLAY_L + (c + 1) * pw / BRICK_COLS;
            // 1px mortar between bricks; the end column stays flush to the wall
            int xgap = (c == BRICK_COLS - 1) ? 0 : 1;
            fill_rect(d0, x0, d0 + BRICK_H - 1, x1 - xgap);
        }
    }
}

static void draw_paddle(void)
{
    // white on the hardware; the blue comes from the gel strip
    if (state == ST_PLAYING || state == ST_SERVE_WAIT)
        fill_rect(PADDLE_D, paddle_x, PADDLE_D + PADDLE_TH, paddle_x + paddle_w);
    else
        // idle / attract: full-width line where the paddle travels
        fill_rect(PADDLE_D, PLAY_L, PADDLE_D + PADDLE_TH, PLAY_R);
}

static void draw_ball(void)
{
    if (ball_live)
        fill_rect((int)ball_d, (int)ball_x,
                  (int)ball_d + BALL_H, (int)ball_x + BALL_W);
}

static void breakout_draw(void)
{
    fillbitmap(tmpbitmap, Machine->pens[0], &Machine->drv->visible_area);

    // frame: wall across the very top; side walls run the full height, all the
    // way to the bottom of the screen (they extend below the paddle line, as in
    // the reference), even though the play area itself ends at the paddle.
    // The gel strips tint the wall segments they cross, as on the real monitor.
    fill_rect(0, 0, TOP_BAR_TH, BM_H);   // top wall
    fill_rect(0, 0, BM_W, WALL);         // left wall (full height)
    fill_rect(0, PLAY_R, BM_W, BM_H);    // right wall (full height)

    // top indicators (per Fig 3-2): player-up number upper-LEFT ("1" or "2"),
    // ball-being-played number upper-RIGHT.
    draw_digit(COUNTER_D, 7, player_up + 1, TEXT_SC);   // ~0.06W
    draw_digit(COUNTER_D, 127, ball_num, TEXT_SC);      // ~0.62W

    // scores: player 1 (left), player 2 (right; stays 000 in 1-player).
    // The score of the player up blinks at 50% duty while a game is in
    // progress (manual: "the score of the player up will be blinking").
    frame_tick++;
    int game_active = (state == ST_PLAYING || state == ST_SERVE_WAIT);
    int blink_on = (frame_tick / FLASH_FRAMES) & 1;
    int p1_score = (player_up == 0) ? score : pl[0].score;
    int p2_score = (player_up == 1) ? score : pl[1].score;
    if (!game_active || player_up != 0 || blink_on)
        draw_number(SCORE_D, 62, p1_score, SCORE_DIGITS, TEXT_SC);
    if (!game_active || player_up != 1 || blink_on)
        draw_number(SCORE_D, 180, p2_score, SCORE_DIGITS, TEXT_SC);
    draw_bricks();
    draw_paddle();
    draw_ball();

    copybitmap(main_bitmap, tmpbitmap, 0, 0, 0, 0,
        &Machine->drv->visible_area, TRANSPARENCY_NONE, 0);
}

//==========================================================================
// Palette: pen 1 is the game's only real output (white); pens 2-6 are the
// gel strip tints (white light through the colored gels), per the DICE
// VIDEO_OVERLAY colors.
//==========================================================================
static const unsigned char breakout_palette[] =
{
    0x00,0x00,0x00,  // 0 black
    0xff,0xff,0xff,  // 1 white
    0xcc,0x26,0x0d,  // 2 red gel     (0.80, 0.15, 0.05)
    0xf2,0xa6,0x0d,  // 3 amber gel   (0.95, 0.65, 0.05)
    0x0d,0xa6,0x40,  // 4 green gel   (0.05, 0.65, 0.25)
    0xf2,0xf2,0x33,  // 5 yellow gel  (0.95, 0.95, 0.20)
    0x0d,0xa6,0xf2,  // 6 blue gel    (0.05, 0.65, 0.95)
};

static void breakout_init_palette(unsigned char* palette,
    unsigned char* colortable, const unsigned char* color_prom)
{
    memcpy(palette, breakout_palette, sizeof(breakout_palette));
}

//==========================================================================
// Driver glue
//==========================================================================
int init_breakout(void)
{
    LOG_INFO("INIT: Breakout Driver Init (behavioral simulation, no CPU)");
    if (breakout_vh_start())
        return 1;
    score = 0;
    paddle_w = PADDLE_FULL;
    paddle_x = (PLAY_L + PLAY_R) / 2 - paddle_w / 2;
    reset_wall();
    lives = 3;
    state = ST_ATTRACT;
    prev_start = prev_serve = 0;
    prev_start2 = 0;
    ball_num = 1; player_up = 0;
    num_players = 1;
    memset(pl, 0, sizeof(pl));
    screen_flipped = 0;
    ball_live = 0;
    walls_done = 0;
    credits = 0;
    prev_coin = 0;
    game_played = 0;
    attract_delay = ATTRACT_SERVE_DELAY;
    place_ball_on_paddle();
    bo_sh_start();
    return 0;
}

static void update_paddle(void)
{
    // Each player has their own analog port; assign each one its own mouse
    // in ANALOG CONFIG (multi-mouse) for a true 2-pot cocktail setup.
    int pos = (int)readinputportbytag(player_up == 1 ? "IN2" : "IN1");

    // Cocktail player 2: the video is flipped, so mirror the control in game
    // space to keep on-screen motion consistent from that player's seat.
    if (screen_flipped)
        pos = (PLAY_L + PLAY_R - 1) - pos;

    paddle_x = pos - paddle_w / 2;
    if (paddle_x < PLAY_L)            paddle_x = PLAY_L;
    if (paddle_x > PLAY_R - paddle_w) paddle_x = PLAY_R - paddle_w;
}

void run_breakout(void)
{
    int start  = (int)(readinputportbytag("IN0") & 1);
    int serve  = (int)(readinputportbytag("IN0") & 2);
    int coin   = (int)(readinputportbytag("IN0") & 4);
    int start2 = (int)(readinputportbytag("IN0") & 8);

    if (rising(coin, &prev_coin) && credits < 9)
        credits++;

    // Cocktail player 2 flips the whole video (P2_CONDITIONAL = PLAYER_2
    // gated by the S2 cabinet dip). Latched here, applied in px().
    screen_flipped = (readinputportbytag("DSW") & 2) &&
                     player_up == 1 &&
                     (state == ST_PLAYING || state == ST_SERVE_WAIT);

    switch (state)
    {
    case ST_ATTRACT:
        // Attract auto-play: the ball logic free-runs over the leftover brick
        // field -- silent, bricks bounce but never break. See ball_collisions
        // for the credit-dependent bottom-bar / top-trap behavior.
        if (credits > 0 && rising(start, &prev_start))
        {
            credits--;
            start_new_game(1);
            break;
        }
        if (credits >= 2 && rising(start2, &prev_start2))
        {
            credits -= 2;
            start_new_game(2);
            break;
        }
        if (!ball_live)
        {
            if (--attract_delay <= 0) serve_ball();   // stays in ST_ATTRACT
        }
        else
            update_ball();
        break;
    case ST_SERVE_WAIT:
        update_paddle();                 // no ball on screen yet (per the manual)
        if (rising(serve, &prev_serve)) { serve_ball(); state = ST_PLAYING; }
        break;
    case ST_PLAYING:
        update_paddle();
        update_ball();
        break;
    }
    // keep edge trackers fresh in states that didn't read them
    prev_start = start; prev_serve = serve; prev_coin = coin; prev_start2 = start2;

    breakout_draw();
    bo_sh_update();
}

void end_breakout(void)
{
    bo_sh_stop();
    breakout_vh_stop();
}

INPUT_PORTS_START(breakout)
PORT_START("IN0")
PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_START1)
PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON1)   // serve
PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_COIN1)
PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_START2)    // 2-player game (2 credits)
PORT_BIT(0xf0, IP_ACTIVE_LOW, IPT_UNUSED)

PORT_START("IN1")   // player 1 paddle: lateral position, PLAY_L..PLAY_R
PORT_ANALOG(0xff, (PLAY_L + PLAY_R) / 2, IPT_AD_STICK_X, 100, 5, PLAY_L, PLAY_R - 1)

PORT_START("IN2")   // player 2 paddle (own device via ANALOG CONFIG multi-mouse)
PORT_ANALOG(0xff, (PLAY_L + PLAY_R) / 2, IPT_AD_STICK_X | IPF_PLAYER2, 100, 5, PLAY_L, PLAY_R - 1)

PORT_START("DSW")   // S4 (ball count) and S2 (cabinet) DIPs on the real board
PORT_DIPNAME(0x01, 0x00, "Balls")
PORT_DIPSETTING(0x00, "3")
PORT_DIPSETTING(0x01, "5")
PORT_DIPNAME(0x02, 0x00, DEF_STR(Cabinet))
PORT_DIPSETTING(0x00, DEF_STR(Upright))
PORT_DIPSETTING(0x02, DEF_STR(Cocktail))   // video flips 180 while player 2 is up
INPUT_PORTS_END

AAE_DRIVER_BEGIN(drv_breakout, "breakout", "Breakout")
AAE_DRIVER_ROM(nullptr)
AAE_DRIVER_FUNCS(&init_breakout, &run_breakout, &end_breakout)
AAE_DRIVER_INPUT(input_ports_breakout)
AAE_DRIVER_SAMPLES_NONE()
AAE_DRIVER_ART_NONE()

AAE_DRIVER_CPUS(
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY(),
    AAE_CPU_NONE_ENTRY()
)

AAE_DRIVER_VIDEO_CORE(60, 0, VIDEO_TYPE_RASTER_BW, ORIENTATION_ROTATE_90)
AAE_DRIVER_SCREEN(BM_W, BM_H, 0, BM_W - 1, 0, BM_H - 1)
AAE_DRIVER_RASTER(0, 7, 7, breakout_init_palette)
AAE_DRIVER_HISCORE_NONE()
AAE_DRIVER_VECTORRAM(0, 0)
AAE_DRIVER_NVRAM_NONE()
AAE_DRIVER_LAYOUT_NONE()
AAE_DRIVER_END()

AAE_REGISTER_DRIVER(drv_breakout)
