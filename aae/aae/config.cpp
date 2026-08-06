#ifdef _WIN32
#include <windows.h>  // MAX_PATH
#else
// POSIX spells it PATH_MAX and it lives in <limits.h>. Aliased rather than
// replaced so the (many) MAX_PATH-sized buffers below stay as they are.
#include <limits.h>
#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif
#endif
#include "config.h"
#include "iniFile.h"
#include "sys_str.h"   // aae_stricmp
#include "aae_mame_driver.h"
#include "menu.h"
#include "sys_log.h"
#include "path_helper.h"
#include <string>
#include <cstdio>
#include <cmath>

char g_aaeIniPath[MAX_PATH];
char g_videoIniPath[MAX_PATH];
char g_gameIniPath[MAX_PATH];

void setup_config() {
    std::string temppath;

    auto clamp_int = [](int v, int lo, int hi) -> int {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
        };

    // Load base config: aae.ini (GLOBAL CONFIG PATH - DO NOT OVERWRITE THIS)
    temppath = getpathM(0, "aae.ini");
    aae_strcpy(g_aaeIniPath, sizeof(g_aaeIniPath), temppath.c_str());
    SetIniFile(g_aaeIniPath);
    LOG_DEBUG("INI PATH (aae.ini) %s", g_aaeIniPath);

    // Load all fields from aae.ini
    // 48000, not 22050. This is the fallback when [main] samplerate is absent,
    // and 22050 caps the output at an 11 kHz bandwidth - audibly thin, and then
    // resampled UP anyway on any HDMI sink, which is 48 kHz-native almost
    // without exception. 48000 is also what the ALSA/HDMI path wants, so the
    // common case now involves no rate conversion at all.
    config.samplerate = get_config_int("main", "samplerate", 48000);

    // Speaker request (Linux output only). Default 2 - measured, not guessed:
    // on the Steam Machine, Dolphin's acclaimed "surround" is a plain stereo
    // stream (pactl: s16le 2ch) room-filled downstream by PipeWire/the
    // soundbar, while a discrete 6ch pulse stream loses its rears inside the
    // SteamOS loopback graph even at 100% channel volume (speaker-test over
    // PipeWire's NATIVE protocol delivers them fine - it is pulse-protocol
    // specific). Stereo out therefore SOUNDS more surround than real 5.1
    // there. speakers=6 keeps the discrete upmix for hardware that takes it.
    config.speakers = get_config_int("main", "speakers", 2);
    if (config.speakers != 2 && config.speakers != 6 && config.speakers != 0)
        config.speakers = 2;

    // Matrix surround encode for the stereo path - see config.h. Default ON:
    // it is what makes downstream upmixers produce rears from our mono-heavy
    // content, verified against the PipeWire graph (both AAE's and Dolphin's
    // stereo streams get identical 6-port upmix treatment; only the L-R
    // content differs).
    config.surround_encode = get_config_int("main", "surround_encode", 1) ? 1 : 0;

    // Per-player mouse assignment: player 1 defaults to the merged system
    // mouse (legacy behavior), the rest to none. When a specific device is
    // assigned, its PATH (stable across reboots) is stored alongside the
    // index and takes precedence at resolution time.
    config.mouse_player[0] = get_config_int("input", "mouse_player1", -1);
    config.mouse_player[1] = get_config_int("input", "mouse_player2", -2);
    config.mouse_player[2] = get_config_int("input", "mouse_player3", -2);
    config.mouse_player[3] = get_config_int("input", "mouse_player4", -2);
    for (int p = 0; p < 4; p++)
    {
        char keyname[32];
        snprintf(keyname, sizeof(keyname), "mouse_player%d_path", p + 1);
        const char* s = get_config_string("input", keyname, "");
        aae_strncpy(config.mouse_player_path[p], sizeof(config.mouse_player_path[p]),
                  s ? s : "");
    }

    // Per-player joystick assignment: AUTO (stick N -> player N) by default.
    // Specific assignments carry a stable id string that wins over the index.
    for (int p = 0; p < 4; p++)
    {
        char keyname[32];
        snprintf(keyname, sizeof(keyname), "joy_player%d", p + 1);
        config.joy_player[p] = get_config_int("input", keyname, -1);

        snprintf(keyname, sizeof(keyname), "joy_player%d_id", p + 1);
        const char* s = get_config_string("input", keyname, "");
        aae_strncpy(config.joy_player_id[p], sizeof(config.joy_player_id[p]),
                  s ? s : "");
    }

    // Per-player keyboard assignment: everyone defaults to the merged system
    // keyboard (the classic several-players-one-keyboard model).
    for (int p = 0; p < 4; p++)
    {
        char keyname[32];
        snprintf(keyname, sizeof(keyname), "kbd_player%d", p + 1);
        config.kbd_player[p] = get_config_int("input", keyname, -1);

        snprintf(keyname, sizeof(keyname), "kbd_player%d_path", p + 1);
        const char* s = get_config_string("input", keyname, "");
        aae_strncpy(config.kbd_player_path[p], sizeof(config.kbd_player_path[p]),
                  s ? s : "");
    }
    config.prescale = get_config_float("main", "prescale", 1);
    config.vid_rotate = get_config_int("main", "vid_rotate", 1);
    config.anisfilter = get_config_int("main", "anisfilter", 0);
    config.translucent = get_config_int("main", "translucent", 0);
    config.m_line = get_config_int("main", "m_line", 20);
    config.m_point = get_config_int("main", "m_point", 16);
    config.linewidth = get_config_float("main", "linewidth", 2.0f);
    config.pointsize = get_config_float("main", "pointsize", 1.6f);
    config.bezel = get_config_int("main", "bezel", 1);
    config.artwork = get_config_int("main", "artwork", 0);
    config.artcrop = get_config_int("main", "artcrop", 0);
    config.overlay = get_config_int("main", "overlay", 0);
    config.explode_point_size = get_config_int("main", "explode_point_size", 12);
    config.fire_point_size = get_config_int("main", "fire_point_size", 12);
    config.vecglow = get_config_int("main", "vectorglow", 5);
    config.vectrail = get_config_int("main", "vectortrail", 1);
    config.glow_filter  = get_config_int("main", "glow_filter", 0);
    // Pyramid glow (glow_filter=1) defaults. On the Pi 5 (Mesa v3d - the
    // project's only aarch64 target) the same values render visibly hotter
    // than on desktop AMD/NVIDIA drivers, so every default is HALVED there to
    // match the desktop picture. Defaults only: an ini-set value always wins,
    // and the per-game re-read below falls back to these loaded globals, so
    // it inherits the platform default automatically. WSL is x86_64 and
    // renders through the desktop GPU (d3d12), so it keeps desktop values.
#if defined(__linux__) && defined(__aarch64__)
    config.glow2_gain   = get_config_float("main", "glow2_gain",   5.0f);
    config.glow2_spread = get_config_float("main", "glow2_spread", 0.5f);
    config.glow2_tail   = get_config_float("main", "glow2_tail",   0.3f);
    config.glow2_core   = get_config_float("main", "glow2_core",   0.5f);
#else
    config.glow2_gain   = get_config_float("main", "glow2_gain",   10.0f);
    config.glow2_spread = get_config_float("main", "glow2_spread", 1.0f);
    config.glow2_tail   = get_config_float("main", "glow2_tail",   0.6f);
    config.glow2_core   = get_config_float("main", "glow2_core",   1.0f);
#endif
    config.gain = get_config_int("main", "gain", 1);
    config.line_smoothing  = get_config_float("main", "line_smoothing",  1.0f);
    config.corner_strength = get_config_float("main", "corner_strength", 0.85f);
    config.shots_textured  = get_config_int  ("main", "shots_textured",  0);
    config.debug = get_config_int("main", "debug", 0);
    {
        // Value is expected lowercase in the ini, matching house style.
        // Default is now "vulkan" (Phase 4b): the VK chain reached parity with
        // GL on Windows and runs on Linux. init_gl() falls back to OpenGL
        // automatically - once, with a popup - if Vulkan init fails, so a
        // machine without a usable Vulkan driver still starts.
        const char* r = get_config_string("main", "renderer", "vulkan");
        config.renderer = (r && strcmp(r, "opengl") == 0) ? RENDERER_OPENGL : RENDERER_VULKAN;
        // What the VIDEO menu edits; applies at the next launch (see config.h).
        config.renderer_pending = config.renderer;
        LOG_INFO("Config: renderer=%s", (config.renderer == RENDERER_VULKAN) ? "vulkan" : "opengl");
    }
    config.vk_validation = get_config_int("main", "vk_validation", 0);
    // Beam supersampling for the Vulkan vector chain: 1 = 1024x1024 beam
    // target (GL parity - beam_init(1)), 2 = 2048x2048 (smoother, ~4x the
    // beam fill and mip cost). Default 1; see config.h.
    config.vk_ssaa = get_config_int("main", "vk_ssaa", 1);
    // GPU timestamp profiler for the Vulkan chain; see config.h. Off costs
    // nothing at all (no query pool, no timestamps).
    config.vk_profile = get_config_int("main", "vk_profile", 0);
    config.debug_profile_code = get_config_int("main", "debug_profile_code", 0);
    config.audio_force_resample = get_config_int("main", "audio_force_resample", 0);

    config.psnoise = get_config_int("main", "psnoise", 0);
    config.hvnoise = get_config_int("main", "hvnoise", 0);
    config.pshiss = get_config_int("main", "pshiss", 0);

    // Volumes are stored as real byte volumes (0..255) only.
    config.mainvol = clamp_int(get_config_int("main", "mainvol", 220), 0, 255);
    config.pokeyvol = clamp_int(get_config_int("main", "pokeyvol", 200), 0, 255);
    config.noisevol = clamp_int(get_config_int("main", "noisevol", 50), 0, 255);

    config.drawzero = get_config_int("main", "drawzero", 0);
    config.widescreen = get_config_int("main", "widescreen", 0);
    config.priority = get_config_int("main", "priority", 1);

    // Performance setting
    config.boostThread = get_config_int("main", "boostThread", 1);
    // Exit confirmation dialog (1 = show prompt, 0 = exit immediately)
    config.confirm_exit = get_config_int("main", "confirm_exit", 1);
    // First-run notice. Absent from the ini means "never shown", so a fresh
    // install (and anyone upgrading over an old aae.ini) sees it once.
    config.first_run = get_config_int("main", "first_run", 1);

    config.kbleds = get_config_int("main", "kbleds", 1);
    config.colordepth = get_config_int("main", "colordepth", 32);
    config.dblbuffer = get_config_int("main", "doublebuffer", 1);
    config.forcesync = get_config_int("main", "force_vsync", 0);
    config.snappng = get_config_int("main", "snappng", 1);
    config.gamma = get_config_int("main", "gamma", 127);
    config.bright = get_config_int("main", "bright", 127);
    config.contrast = get_config_int("main", "contrast", 127);
    config.showinfo = get_config_int("main", "showinfo", 0);
    config.windowed = get_config_int("window", "fullscreen", 0);
    config.aspect = get_config_string("window", "aspect_ratio", "4:3");
    // 0 = AUTO: size the window to the largest aspect-fit on the monitor at
    // startup. Any positive pair is an exact client size, honored literally.
    // The 0 sentinel lives ONLY in the ini/config/menu layer - the window
    // startup code (GenerateFinalWindowSetup / linux_main) resolves it to real
    // pixels before anything downstream sees a size.
    config.screenw = get_config_int("main", "screenw", 0);
    config.screenh = get_config_int("main", "screenh", 0);
    config.exrompath = get_config_string("main", "mame_rom_path", "roms");
    config.exartpath = get_config_string("main", "mame_artwork_path", "artwork");
    config.hack = get_config_int("main", "hack", 0);
    // Raster CRT treatment default is PER-PLATFORM. Desktop: NONE, so color
    // raster games get the color monitor shader (color_enable=1). Pi 5 (the
    // only aarch64 target): the scanlines.png texture overlay instead - a
    // selected overlay stands the color shader down (VkColorMonitorActive /
    // color_monitor_active), trading the multi-tap CRT passes for one cheap
    // textured quad on the v3d GPU. Default only; an ini-set value wins.
#if defined(__linux__) && defined(__aarch64__)
    config.raster_effect = get_config_string("main", "raster_effect", "scanlines.png");
#else
    config.raster_effect = get_config_string("main", "raster_effect", "NONE");
#endif
    config.game_aspect = get_config_string("main", "game_aspect", "AUTO");
    config.flip_gui_controls = get_config_int("main", "flip_gui_controls", 0);
    // Monitor selection (1-based: 1 = primary).
    // Also read early in LoadWindowIniConfig() (winmain.cpp) before the window
    // is created, so the window lands on the right monitor at startup.
    // This copy in config.starting_monitor is available to the rest of the
    // emulator after emulator_init() completes.
    config.starting_monitor = get_config_int("main", "starting_monitor", 1);
    if (config.starting_monitor <= 0)
        config.starting_monitor = 1;

    // System rotation: persisted as raw ORIENTATION_xxx flags.
    // 0=none, 5=ROT90(-ror), 3=ROT180, 6=ROT270(-rol).
    // Command-line -ror/-rol overrides this after loading.
    config.system_rotation = get_config_int("main", "system_rotation", 0);

    // Mono monitor CRT effect (B/W raster games). Defaults are the accepted
    // PET emulator look; tint default is P4 white for arcade monitors.
    //
    // Whether it is ON by default is PER-PLATFORM, the direct sibling of
    // raster_effect above and for the same reason: the mono pass is multi-tap
    // (separable blur plus a halation gather at radius 4), and on the Pi 5's
    // v3d GPU that costs more than these games can spare. Desktop and SteamOS
    // keep it; the only aarch64 target starts without it.
    //
    // Default only - an ini-set value wins either way, so MONO EFFECT in the
    // MONO MONITOR SETUP menu turns it back on and the choice persists.
#if defined(__linux__) && defined(__aarch64__)
    config.mono_enable          = get_config_int  ("monitormono", "mono_enable",          0);
#else
    config.mono_enable          = get_config_int  ("monitormono", "mono_enable",          1);
#endif
    config.mono_blur_h          = get_config_float("monitormono", "mono_blur_h",          0.8f);
    config.mono_blur_v          = get_config_float("monitormono", "mono_blur_v",          0.35f);
    config.mono_halation        = get_config_float("monitormono", "mono_halation",        0.15f);
    config.mono_halation_radius = get_config_float("monitormono", "mono_halation_radius", 4.0f);
    config.mono_scanline        = get_config_float("monitormono", "mono_scanline",        0.0f);
    config.mono_contrast        = get_config_float("monitormono", "mono_contrast",        1.0f);
    config.mono_brightness      = get_config_float("monitormono", "mono_brightness",      0.0f);
    config.mono_tint            = get_config_int  ("monitormono", "mono_tint",            0);

    // Color monitor CRT effect (color raster games). Defaults model a real
    // 80s arcade cab at play distance: operators ran brightness high enough
    // that the beam spot bloomed over the scanline gaps, and the shadow mask
    // was invisible except nose-to-glass. So: soft bright spot with a touch
    // of overdrive, barely-there scanlines and mask, subtle convergence.
    config.color_enable          = get_config_int  ("monitorcolor", "color_enable",          1);
    config.color_blur_h          = get_config_float("monitorcolor", "color_blur_h",          0.25f);
    config.color_blur_v          = get_config_float("monitorcolor", "color_blur_v",          0.10f);
    config.color_converge        = get_config_float("monitorcolor", "color_converge",        0.05f);
    config.color_halation        = get_config_float("monitorcolor", "color_halation",        0.06f);
    config.color_halation_radius = get_config_float("monitorcolor", "color_halation_radius", 4.0f);
    config.color_scanline        = get_config_float("monitorcolor", "color_scanline",        0.10f);
    config.color_contrast        = get_config_float("monitorcolor", "color_contrast",        1.15f);
    config.color_brightness      = get_config_float("monitorcolor", "color_brightness",      0.0f);
    config.color_saturation      = get_config_float("monitorcolor", "color_saturation",      1.0f);
    config.color_mask_type       = get_config_int  ("monitorcolor", "color_mask_type",       1);
    config.color_mask_strength   = get_config_float("monitorcolor", "color_mask_strength",   0.10f);
    config.color_mask_scale      = get_config_float("monitorcolor", "color_mask_scale",      2.0f);

    // Load game-specific overrides for select fields
    // Let getpathM join the filename: it uses std::filesystem, so the separator
    // is correct on both platforms. The old hand-appended "\\" produced a
    // filename with a literal backslash on Linux ("ini\game.ini"), so per-game
    // overrides silently never loaded there - every game inherited whatever
    // config the previous game left behind (wrong aspect on vertical games from
    // the GUI being the visible symptom) - and per-game SAVES (g_gameIniPath is
    // reused by my_set_config_value) landed in a junk file of that name.
    temppath = getpathM("ini", (std::string(Machine->gamedrv->name) + ".ini").c_str());
    aae_strcpy(g_gameIniPath, sizeof(g_gameIniPath), temppath.c_str());
    if (file_exists(g_gameIniPath)) {
        SetIniFile(g_gameIniPath);
        LOG_INFO("Game Config Path: %s", g_gameIniPath);

        // somegame.ini available - video override options
        config.bezel = get_config_int("main", "bezel", config.bezel);
        config.artwork = get_config_int("main", "artwork", config.artwork);
        config.artcrop = get_config_int("main", "artcrop", config.artcrop);
        config.overlay = get_config_int("main", "overlay", config.overlay);
        config.explode_point_size = get_config_int("main", "explode_point_size", config.explode_point_size);
        config.fire_point_size = get_config_int("main", "fire_point_size", config.fire_point_size);
        config.vecglow = get_config_int("main", "vectorglow", config.vecglow);
        config.vectrail = get_config_int("main", "vectortrail", config.vectrail);
        config.glow_filter  = get_config_int("main", "glow_filter", config.glow_filter);
        config.glow2_gain   = get_config_float("main", "glow2_gain",   config.glow2_gain);
        config.glow2_spread = get_config_float("main", "glow2_spread", config.glow2_spread);
        config.glow2_tail   = get_config_float("main", "glow2_tail",   config.glow2_tail);
        config.glow2_core   = get_config_float("main", "glow2_core",   config.glow2_core);
        config.gain = get_config_int("main", "gain", config.gain);
        config.line_smoothing  = get_config_float("main", "line_smoothing",  config.line_smoothing);
        config.corner_strength = get_config_float("main", "corner_strength", config.corner_strength);
        config.shots_textured  = get_config_int  ("main", "shots_textured",  config.shots_textured);
        config.prescale = get_config_float("main", "prescale", config.prescale);
        config.vid_rotate = get_config_int("main", "vid_rotate", config.vid_rotate);
        config.anisfilter = get_config_int("main", "anisfilter", config.anisfilter);
        config.translucent = get_config_int("main", "translucent", config.translucent);
        config.m_line = get_config_int("main", "m_line", config.m_line);
        config.m_point = get_config_int("main", "m_point", config.m_point);
        config.linewidth = get_config_float("main", "linewidth", config.linewidth);
        config.pointsize = get_config_float("main", "pointsize", config.pointsize);
        config.raster_effect = get_config_string("main", "raster_effect", config.raster_effect);
        config.game_aspect = get_config_string("main", "game_aspect", config.game_aspect);

        // Per-game system rotation override (e.g. for cab-specific setups)
        config.system_rotation = get_config_int("main", "system_rotation", config.system_rotation);

        // somegame.ini available - sound override options
        config.psnoise = get_config_int("main", "psnoise", config.psnoise);
        config.hvnoise = get_config_int("main", "hvnoise", config.hvnoise);
        config.pshiss = get_config_int("main", "pshiss", config.pshiss);

        // Volumes are stored as real byte volumes (0..255) only.
        config.mainvol = clamp_int(get_config_int("main", "mainvol", config.mainvol), 0, 255);
        config.pokeyvol = clamp_int(get_config_int("main", "pokeyvol", config.pokeyvol), 0, 255);
        config.noisevol = clamp_int(get_config_int("main", "noisevol", config.noisevol), 0, 255);

        config.samplerate = get_config_int("main", "samplerate", config.samplerate);

        // Per-game mono monitor overrides.
        config.mono_enable          = get_config_int  ("monitormono", "mono_enable",          config.mono_enable);
        config.mono_blur_h          = get_config_float("monitormono", "mono_blur_h",          config.mono_blur_h);
        config.mono_blur_v          = get_config_float("monitormono", "mono_blur_v",          config.mono_blur_v);
        config.mono_halation        = get_config_float("monitormono", "mono_halation",        config.mono_halation);
        config.mono_halation_radius = get_config_float("monitormono", "mono_halation_radius", config.mono_halation_radius);
        config.mono_scanline        = get_config_float("monitormono", "mono_scanline",        config.mono_scanline);
        config.mono_contrast        = get_config_float("monitormono", "mono_contrast",        config.mono_contrast);
        config.mono_brightness      = get_config_float("monitormono", "mono_brightness",      config.mono_brightness);
        config.mono_tint            = get_config_int  ("monitormono", "mono_tint",            config.mono_tint);

        // Per-game color monitor overrides.
        config.color_enable          = get_config_int  ("monitorcolor", "color_enable",          config.color_enable);
        config.color_blur_h          = get_config_float("monitorcolor", "color_blur_h",          config.color_blur_h);
        config.color_blur_v          = get_config_float("monitorcolor", "color_blur_v",          config.color_blur_v);
        config.color_converge        = get_config_float("monitorcolor", "color_converge",        config.color_converge);
        config.color_halation        = get_config_float("monitorcolor", "color_halation",        config.color_halation);
        config.color_halation_radius = get_config_float("monitorcolor", "color_halation_radius", config.color_halation_radius);
        config.color_scanline        = get_config_float("monitorcolor", "color_scanline",        config.color_scanline);
        config.color_contrast        = get_config_float("monitorcolor", "color_contrast",        config.color_contrast);
        config.color_brightness      = get_config_float("monitorcolor", "color_brightness",      config.color_brightness);
        config.color_saturation      = get_config_float("monitorcolor", "color_saturation",      config.color_saturation);
        config.color_mask_type       = get_config_int  ("monitorcolor", "color_mask_type",       config.color_mask_type);
        config.color_mask_strength   = get_config_float("monitorcolor", "color_mask_strength",   config.color_mask_strength);
        config.color_mask_scale      = get_config_float("monitorcolor", "color_mask_scale",      config.color_mask_scale);
    }

    // Clamp mono monitor knobs to the shader's designed ranges (same limits
    // as the PET emulator's tuning table) so a hand-edited ini can't push
    // the effect into broken territory.
    auto clamp_float = [](float v, float lo, float hi) -> float {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
        };
    config.mono_enable          = clamp_int  (config.mono_enable,          0, 1);
    config.mono_blur_h          = clamp_float(config.mono_blur_h,          0.0f, 2.5f);
    config.mono_blur_v          = clamp_float(config.mono_blur_v,          0.0f, 1.0f);
    config.mono_halation        = clamp_float(config.mono_halation,        0.0f, 1.0f);
    config.mono_halation_radius = clamp_float(config.mono_halation_radius, 1.0f, 16.0f);
    config.mono_scanline        = clamp_float(config.mono_scanline,        0.0f, 1.0f);
    config.mono_contrast        = clamp_float(config.mono_contrast,        1.0f, 3.0f);
    config.mono_brightness      = clamp_float(config.mono_brightness,      0.0f, 0.25f);
    config.mono_tint            = clamp_int  (config.mono_tint,            0, 2);

    config.color_enable          = clamp_int  (config.color_enable,          0, 1);
    config.color_blur_h          = clamp_float(config.color_blur_h,          0.0f, 2.5f);
    config.color_blur_v          = clamp_float(config.color_blur_v,          0.0f, 1.0f);
    config.color_converge        = clamp_float(config.color_converge,        0.0f, 2.0f);
    config.color_halation        = clamp_float(config.color_halation,        0.0f, 1.0f);
    config.color_halation_radius = clamp_float(config.color_halation_radius, 1.0f, 16.0f);
    config.color_scanline        = clamp_float(config.color_scanline,        0.0f, 1.0f);
    config.color_contrast        = clamp_float(config.color_contrast,        1.0f, 3.0f);
    config.color_brightness      = clamp_float(config.color_brightness,      0.0f, 0.25f);
    config.color_saturation      = clamp_float(config.color_saturation,      0.0f, 2.0f);
    config.color_mask_type       = clamp_int  (config.color_mask_type,       0, 2);
    config.color_mask_strength   = clamp_float(config.color_mask_strength,   0.0f, 1.0f);
    config.color_mask_scale      = clamp_float(config.color_mask_scale,      1.0f, 6.0f);

    config.linewidth = config.m_line * 0.1f;
    config.pointsize = config.m_point * 0.1f;

    LOG_INFO("Configured Mame Rom Path is %s", config.exrompath);
}

void setup_video_config() {

    LOG_DEBUG("SETUP VIDEO CONFIG CALLED");

    std::string temppath = getpathM(0, "video.ini");
    aae_strcpy(g_videoIniPath, sizeof(g_videoIniPath), temppath.c_str());
    SetIniFile(g_videoIniPath);
    std::string name = Machine->gamedrv->name;

    const char* scalePrefix = "full";
    if (config.bezel && !config.artcrop)  scalePrefix = "bezel";
    else if (config.bezel && config.artcrop) scalePrefix = "crop";

    auto key = [&](const char* suffix) -> std::string {
        return std::string(scalePrefix) + suffix;
        };

    // Default Bezel Settings.
    bezelzoom = 1.0;
    bezelx = 0;
    bezely = 0;

    if (config.bezel && config.artcrop == 0)
    {
        bezelzoom = 1.0;
        bezelx = 0;
        bezely = 0;
    }
    else if (config.bezel && config.artcrop)
    {
        bezelzoom = get_config_float(name.c_str(), "bezcropzoom", 1.0f);
        bezelx = get_config_int(name.c_str(), "bezcropx", 0);
        bezely = get_config_int(name.c_str(), "bezcropy", 0);
    }
    /*
    // Try reading new sane keys first (e.g. full_left, full_right)
    game_rect_left = get_config_int(name.c_str(), key("_left").c_str(), -9999);

    if (game_rect_left == -9999) {
        // Fallback to reading legacy misnamed keys (fullsx = left, fullsy = right, fullex = bottom, fulley = top)
        game_rect_left = get_config_int(name.c_str(), key("sx").c_str(), 0);
        game_rect_right = get_config_int(name.c_str(), key("sy").c_str(), 1024);
        game_rect_bottom = get_config_int(name.c_str(), key("ex").c_str(), 0);
        game_rect_top = get_config_int(name.c_str(), key("ey").c_str(), 1024);
    }
    else {
        game_rect_right = get_config_int(name.c_str(), key("_right").c_str(), 1024);
        game_rect_bottom = get_config_int(name.c_str(), key("_bottom").c_str(), 0);
        game_rect_top = get_config_int(name.c_str(), key("_top").c_str(), 1024);
    }
    */
    game_rect_left = get_config_int(name.c_str(), key("_left").c_str(), 0);
    game_rect_right = get_config_int(name.c_str(), key("_right").c_str(), 1024);
    game_rect_bottom = get_config_int(name.c_str(), key("_bottom").c_str(), 0);
    game_rect_top = get_config_int(name.c_str(), key("_top").c_str(), 1024);

    LOG_INFO("VIDEO CONFIG: Left %d, Right %d, Bottom %d, Top %d ", game_rect_left, game_rect_right, game_rect_bottom, game_rect_top);
}


void setup_game_config() {
    setup_config();
    set_points_lines();
}


void sanity_check_config()
{
    //SANITY CHECKS GO HERE
    if (config.prescale < 1 || config.prescale > 5)
    {
        LOG_INFO("!!!!!Raster prescale set to unsupported value, supported values are 1 - 5");
        config.prescale = 2; have_error = 3;
    }

    // Only the two beam-buffer sizes are offered: 1 -> 1024x1024,
    // 2 -> 2048x2048. Higher factors are not clamped-to but rejected, because
    // 3 would ask for a 3072-square target whose per-frame mip cascade costs
    // more than the extra smoothness is worth on any GPU this runs on.
    if (config.vk_ssaa != 1 && config.vk_ssaa != 2)
    {
        LOG_INFO("!!!!!vk_ssaa set to unsupported value, supported values are 1 (1024x1024 beam buffer) or 2 (2048x2048)");
        config.vk_ssaa = 1; have_error = 3;
    }

    // Diagnostic toggle, so anything non-zero means "on" - but pin it to 0/1
    // so the log line and the profiler's own gate agree.
    if (config.vk_profile != 0)
        config.vk_profile = 1;

    if (config.anisfilter < 2 || config.anisfilter > 16 || (config.anisfilter % 2 != 0))
    {
        if (config.anisfilter != 0) { //FINAL CHECK
            LOG_INFO("!!!!!Ansitropic Filterings set to unsupported value, supported values are 2,4,8,16 !!!!!");
            have_error = 3;
            config.anisfilter = 0; //RESET TIL FIXED
        }
    }

    if (config.priority < 0 || config.priority > 4)
    {
        LOG_INFO("!!!!!Priority set to unsupported value, supported values are 0,1,2,3,4 - defaulted to 1!!!!!");
        config.priority = 1;
    }

    // Vector glow and trail effects are only meaningful for vector games.
    // Force them off for any raster game regardless of ini settings.
    if (Machine && Machine->drv && (Machine->drv->video_attributes & VIDEO_RASTER_CLASS_MASK))
    {
        if (config.vecglow)
        {
            LOG_INFO("sanity_check_config: vecglow disabled (raster game).");
            config.vecglow = 0;
        }
        if (config.vectrail)
        {
            LOG_INFO("sanity_check_config: vectrail disabled (raster game).");
            config.vectrail = 0;
        }
    }

}
// Writes an int, float, or string value to either aae.ini or game.ini
void my_set_config_value(const char* section, const char* key, const std::string& value, int path) {
    // path==0 writes to the global aae.ini in the program root.
    // path!=0 writes to the per-game ini under ini\ (game name).
    const char* target_path = (path == 0) ? g_aaeIniPath : g_gameIniPath;

    SetIniFile(target_path);
    set_config_string(section, key, value.c_str());
    LOG_DEBUG("INI SAVE [%s] %s.%s = %s", target_path, section, key, value.c_str());
}

void my_set_config_int(const char* section, const char* key, int val, int path) {
    my_set_config_value(section, key, std::to_string(val), path);
}

void my_set_config_float(const char* section, const char* key, float val, int path) {
    my_set_config_value(section, key, std::to_string(val), path);
}

void my_set_config_string(const char* section, const char* key, const char* val, int path) {
    my_set_config_value(section, key, std::string(val), path);
}

// Parse a "W:H" aspect string ("4:3", "16:9"...). Returns w/h, or 0 for
// "AUTO"/empty/unparsable (callers treat 0 as "no override").
float aspect_from_string(const char* s)
{
    if (!s || !s[0]) return 0.0f;
    if (aae_stricmp(s, "AUTO") == 0) return 0.0f;
    float w = 0.0f, h = 0.0f;
#ifdef _WIN32
    // sscanf_s on Windows: MSVC raises C4996 for plain sscanf and this project
    // builds with that as an error. For %f with no string conversions the two
    // take identical arguments.
    if (sscanf_s(s, "%f:%f", &w, &h) == 2 && w > 0.0f && h > 0.0f)
#else
    if (sscanf(s, "%f:%f", &w, &h) == 2 && w > 0.0f && h > 0.0f)
#endif
        return w / h;
    return 0.0f;
}
