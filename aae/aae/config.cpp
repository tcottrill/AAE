#include "framework.h"
#include "config.h"
#include "iniFile.h"
#include "aae_mame_driver.h"
#include "menu.h"
#include "sys_log.h"
#include "path_helper.h"
#include <string>
#include <cstdio>

char aaepath[MAX_PATH];
char gamepath[MAX_PATH];


void setup_config() {
    std::string temppath;

    // Load base config: aae.ini
    temppath = getpathM(0, "aae.ini");
    strcpy_s(aaepath, sizeof(aaepath), temppath.c_str());
    SetIniFile(aaepath);
    LOG_DEBUG("INI PATH HERE %s", aaepath);
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
    config.pokeyvol = get_config_int("main", "pokeyvol", 200);
    config.mainvol = get_config_int("main", "mainvol", 220);
    config.noisevol = get_config_int("main", "noisevol", 50);
    config.drawzero = get_config_int("main", "drawzero", 0);
    config.widescreen = get_config_int("main", "widescreen", 0);
    config.priority = get_config_int("main", "priority", 1);
    // --- NEW PERFORMANCE SETTING ---
    config.boostThread = get_config_int("main", "boostThread", 1);
    config.kbleds = get_config_int("main", "kbleds", 1);
    config.colordepth = get_config_int("main", "colordepth", 32);
    config.dblbuffer = get_config_int("main", "doublebuffer", 1);
    config.forcesync = get_config_int("main", "force_vsync", 0);
    config.snappng = get_config_int("main", "snappng", 1);
    config.gamma = get_config_int("main", "gamma", 127);
    config.bright = get_config_int("main", "bright", 127);
    config.contrast = get_config_int("main", "contrast", 127);
    config.showinfo = get_config_int("main", "showinfo", 0);
    config.windowed = get_config_int("main", "windowed", 0);
    config.aspect = get_config_string("main", "aspect_ratio", "4:3");
    config.screenw = get_config_int("main", "screenw", 1024);
    config.screenh = get_config_int("main", "screenh", 768);
    config.exrompath = get_config_string("main", "mame_rom_path", "roms");
    config.hack = get_config_int("main", "hack", 0);

    // Load game-specific overrides for select fields
    temppath = getpathM("ini", 0) + std::string("\\") + Machine->gamedrv->name + ".ini";
    strcpy_s(gamepath, sizeof(gamepath), temppath.c_str());
    if (file_exists(gamepath)) {
        SetIniFile(gamepath);
        LOG_INFO("Game Config Path: %s", gamepath);
       
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
             
        // somegame.ini available - sound override options
        config.psnoise = get_config_int("main", "psnoise", config.psnoise);
        config.hvnoise = get_config_int("main", "hvnoise", config.hvnoise);
        config.pshiss = get_config_int("main", "pshiss", config.pshiss);
        config.pokeyvol = get_config_int("main", "pokeyvol", config.pokeyvol);
        config.mainvol = get_config_int("main", "mainvol", config.mainvol);
        config.noisevol = get_config_int("main", "noisevol", config.noisevol);
        config.samplerate = get_config_int("main", "samplerate", config.samplerate);
    }

    config.linewidth = config.m_line * 0.1f;
    config.pointsize = config.m_point * 0.1f;

    LOG_INFO("Configured Mame Rom Path is %s", config.exrompath);
}

void setup_video_config() {
    std::string temppath = getpathM(0, "video.ini");
    strcpy_s(aaepath, sizeof(aaepath), temppath.c_str());
    SetIniFile(aaepath);

    std::string name = Machine->gamedrv->name;
    sx = get_config_int(name, "fullsx", 0);
    sy = get_config_int(name, "fullsy", 1024);
    ex = get_config_int(name, "fullex", 1024);
    ey = get_config_int(name, "fulley", 0);

    if (config.bezel && gamenum && config.artcrop == 0) {
        b1sx = get_config_int(name, "bezelsx", 0);
        b1sy = get_config_int(name, "bezelsy", 900);
        b2sx = get_config_int(name, "bezelex", 900);
        b2sy = get_config_int(name, "bezeley", 0);
        bezelzoom = 1.0;
        bezelx = 0;
        bezely = 0;
    }

    if (config.bezel && config.artcrop && gamenum) {
        b1sx = get_config_int(name, "cropsx", 0);
        b1sy = get_config_int(name, "cropsy", 900);
        b2sx = get_config_int(name, "cropex", 900);
        b2sy = get_config_int(name, "cropey", 0);
        bezelzoom = get_config_float(name, "bezcropzoom", 1.0f);
        bezelx = get_config_int(name, "bezcropx", 0);
        bezely = get_config_int(name, "bezcropy", 0);
    }
}

void setup_game_config() {
    setup_config();
    setup_video_menu();
    setup_sound_menu();
    set_points_lines();
}

// Writes an int, float, or string value to either aae.ini or game.ini
void my_set_config_value(const char* section, const char* key, const std::string& value, int path) {
    const char* target_path = (path == 0) ? aaepath : gamepath;

    SetIniFile(target_path);  // target_path is assumed to already exist in memory
    set_config_string(section, key, value.c_str());
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
