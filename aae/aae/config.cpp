#include "framework.h"
#include "config.h"
#include "iniFile.h"
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
    strcpy_s(g_aaeIniPath, sizeof(g_aaeIniPath), temppath.c_str());
    SetIniFile(g_aaeIniPath);
    LOG_DEBUG("INI PATH (aae.ini) %s", g_aaeIniPath);

    // Load all fields from aae.ini
    config.samplerate = get_config_int("main", "samplerate", 22050);
    config.prescale = get_config_int("main", "prescale", 1);
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
    config.gain = get_config_int("main", "gain", 1);
    config.debug = get_config_int("main", "debug", 0);
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
    config.screenw = get_config_int("main", "screenw", 1024);
    config.screenh = get_config_int("main", "screenh", 768);
    config.exrompath = get_config_string("main", "mame_rom_path", "roms");
    config.exartpath = get_config_string("main", "mame_artwork_path", "artwork");
    config.hack = get_config_int("main", "hack", 0);
    config.raster_effect = get_config_string("main", "raster_effect", "NONE");
    config.flip_gui_controls = get_config_int("main", "flip_gui_controls", 0);

    // Load game-specific overrides for select fields
    temppath = getpathM("ini", 0) + std::string("\\") + Machine->gamedrv->name + ".ini";
    strcpy_s(g_gameIniPath, sizeof(g_gameIniPath), temppath.c_str());
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
        config.gain = get_config_int("main", "gain", config.gain);
        config.prescale = get_config_int("main", "prescale", config.prescale);
        config.vid_rotate = get_config_int("main", "vid_rotate", config.vid_rotate);
        config.anisfilter = get_config_int("main", "anisfilter", config.anisfilter);
        config.translucent = get_config_int("main", "translucent", config.translucent);
        config.m_line = get_config_int("main", "m_line", config.m_line);
        config.m_point = get_config_int("main", "m_point", config.m_point);
        config.linewidth = get_config_float("main", "linewidth", config.linewidth);
        config.pointsize = get_config_float("main", "pointsize", config.pointsize);
        config.raster_effect = get_config_string("main", "raster_effect", config.raster_effect);

        // somegame.ini available - sound override options
        config.psnoise = get_config_int("main", "psnoise", config.psnoise);
        config.hvnoise = get_config_int("main", "hvnoise", config.hvnoise);
        config.pshiss = get_config_int("main", "pshiss", config.pshiss);

        // Volumes are stored as real byte volumes (0..255) only.
        config.mainvol = clamp_int(get_config_int("main", "mainvol", config.mainvol), 0, 255);
        config.pokeyvol = clamp_int(get_config_int("main", "pokeyvol", config.pokeyvol), 0, 255);
        config.noisevol = clamp_int(get_config_int("main", "noisevol", config.noisevol), 0, 255);

        config.samplerate = get_config_int("main", "samplerate", config.samplerate);
    }

    config.linewidth = config.m_line * 0.1f;
    config.pointsize = config.m_point * 0.1f;

    LOG_INFO("Configured Mame Rom Path is %s", config.exrompath);
}

void setup_video_config() {

    LOG_DEBUG("SETUP VIDEO CONFIG CALLED");

    std::string temppath = getpathM(0, "video.ini");
    strcpy_s(g_videoIniPath, sizeof(g_videoIniPath), temppath.c_str());
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

    LOG_INFO("VIDEO CONFIG: Left %d, Right %d, Bottom %d, Top %d ", game_rect_left, game_rect_right, game_rect_bottom, game_rect_top);
}

void setup_game_config() {
    setup_config();
    set_points_lines();
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
