// -----------------------------------------------------------------------------
// This file is part of the AAE (Another Arcade Emulator) project.
//
// Copyright (C) 2025-2026 Tim Cottrill
//
// This code is released under the GNU General Public License v3.0
// or (at your option) any later version. See the accompanying
// source files and license text for full details.
// -----------------------------------------------------------------------------

#pragma once

#ifndef _aae_loader_shim_h_
#define _aae_loader_shim_h_

#include <string>
#include <stdbool.h>
#include "sys_fileio.h" // Inherit generic functionality

// =============================================================
// Compatibility Layer
// Maps legacy AAE function calls to sys_fileio implementations
// =============================================================

inline unsigned int get_last_file_size() { return (unsigned int)getLastFileSize(); }
inline unsigned int get_last_zip_file_size() { return (unsigned int)getLastZSize(); }
inline int get_last_crc() { return (int)getLastZCrc(); }

inline unsigned char* load_file(const char* filename) { return loadFile(filename); }
inline int save_file(const char* filename, const unsigned char* buf, int size) { return saveFile(filename, buf, size); }
inline unsigned char* load_zip_file(const char* archname, const char* filename) { return loadZip(archname, filename); }
inline bool save_zip_file(const char* archname, const char* filename, const unsigned char* data) { return saveZip(archname, filename, data); }

// Check if file exists (Wraps sys_fileio or std::filesystem logic)
bool file_exists(const std::string& filename);
bool file_exists(const char* filename);

// =============================================================
// AAE Specific Functionality
// =============================================================

// RomModule structure defined here
// Rom setting moved here temporarily
const struct RomModule
{
	const char* filename;
	unsigned int loadAddr;
	int romSize;
	int loadtype;
	//Hashing Checksums
	unsigned int crc;
	const char* sha;
	int disposable;
};

// HANDY ROM Definitions.

#define COMMA ,
#define CRC(n)            (0x ## n)
#define SHA1(x)           COMMA#x
#define ROM_LOAD_NORMAL 0
#define ROM_LOAD_16     1
#define ROM_REGION_START 999
#define	ROMREGION_DISPOSE 0x10

#define ROM_START(name) static struct RomModule rom_##name[] = {
//#define ROM_REGION( romSize, loadtype, disposable) { NULL, ROM_REGION_START, romSize, loadtype, disposable },
 // For regions: filename=NULL, loadAddr=ROM_REGION_START, romSize, loadtype=REGION_*, crc=0, sha=NULL, disposable=flag
	// Note: second param is your REGION_* id (e.g., REGION_GFX1)
#define ROM_REGION(romSize, regionId, disposableFlag) \
    { NULL, ROM_REGION_START, (romSize), (regionId), 0, NULL, (disposableFlag) },
#define ROM_LOAD(filename, loadAddr, romSize, ...) { filename, loadAddr, romSize, ROM_LOAD_NORMAL, __VA_ARGS__ },
#define ROM_LOAD16_BYTE(filename,loadAddr,romSize, ...) { filename,loadAddr,romSize, ROM_LOAD_16, __VA_ARGS__ },
#define ROM_RELOAD(loadAddr,romSize) { (char *)-1, loadAddr,romSize, ROM_LOAD_NORMAL , 0 , 0 },
#define ROM_CONTINUE(loadAddr,romSize) { (char *)-2, loadAddr,romSize, ROM_LOAD_NORMAL, 0 , 0 },
#define ROM_END {NULL, 0, 0, 0, 0, 0}};

// Loads all ROMs defined in a RomModule list from a ZIP archive
int load_roms(const char* archname, const struct RomModule* p);

// Saves a char buffer to disk as text (legacy wrapper)
int save_file_char(const char* filename, const char* buf, int size);

// Hiscore / NVRAM Specifics
int load_hi_aae(int start, int size, int image);
int save_hi_aae(int start, int size, int image);

// Generic NVRAM persistence (MAME generic_0fill equivalent). A driver points
// nvram_set_region() at its battery-backed RAM region in its init(), then uses
// AAE_DRIVER_NVRAM(generic_nvram_handler). The handler saves the region on exit,
// reloads it on start, and 0-fills it on first boot (no file yet).
void nvram_set_region(void* ptr, int size, int fill = 0x00);
void generic_nvram_handler(void* file, int read_or_write);

// Verification Logic
int verify_rom(const char* archname, const struct RomModule* p, int romnum);
int verify_sample(const char** p, int num);

// Batch Sample Loader - loads all samples from a driver's game_samples list
void load_samples_batch(const char* const* sample_list);

// -----------------------------------------------------------------------------
// load_ambient_samples
// Loads the 3 optional ambient audio files (flyback, psnoise, hiss) from
// "samples\aae.zip" (or loose files under "samples\aae\"). These are AAE's
// own ambient effect files and are separate from any game's sample set.
//
// Because load_sample_from_buffer assigns sequential IDs via ++sound_id,
// these samples will get IDs AFTER whatever the current game loaded.
// They are looked up by name ("flyback.wav", "psnoise.wav", "hiss.wav")
// via nameToNum() -- never by hardcoded index -- so the order and count
// of game samples does not matter.
//
// Safe to call even if aae.zip is missing; individual missing files are
// logged but do not set have_error.
// -----------------------------------------------------------------------------
void load_ambient_samples();

#endif
