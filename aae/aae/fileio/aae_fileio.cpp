// -----------------------------------------------------------------------------
// Game Engine Alpha - Generic Module
// Generic component or utility file for the Game Engine Alpha project. This
// file may contain helpers, shared utilities, or subsystems that integrate
// seamlessly with the engine's rendering, audio, and gameplay frameworks.
//
// Integration:
//   This library is part of the **Game Engine Alpha** project and is tightly
//   integrated with its texture management, logging, and math utility systems.
//
// Usage:
//   Include this module where needed. It is designed to work as a building block
//   for engine subsystems such as rendering, input, audio, or game logic.
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
// -----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <memory>
#include <filesystem>
#include <cstring>

#include "aae_fileio.h"
#include "sys_log.h"
#include "miniz.h"
#include "aae_mame_driver.h"
#include "memory.h"
#include "path_helper.h"
#include "sha-1.h"
#include "iniFile.h"
#include "mixer.h"

#define DEBUG_LOG 1

#ifdef DEBUG_LOG
#define DLOG(msg, ...) LOG_INFO(msg, ##__VA_ARGS__)
#else
#define DLOG(msg, ...)
#endif

CSHA1 sha1;

// Wrapper for std::filesystem or sys_fileio check
bool file_exists(const std::string& filename)
{
    // You could also just call fileExistsReadable(filename.c_str());
    return std::filesystem::exists(filename) && std::filesystem::is_regular_file(filename);
}

bool file_exists(const char* filename)
{
    return file_exists(std::string(filename));
}

// Wrapper for text saving
int save_file_char(const char* filename, const char* buf, int size) {
    // Cast char* to unsigned char* and let sys_fileio handle it
    return saveFile(filename, (const unsigned char*)buf, size) ? 1 : 0;
}

int load_hi_aae(int start, int size, int image)
{
    std::string fullpath = getpathM("hi", 0) + "/";
    fullpath.append(Machine->gamedrv->name);
    fullpath.append(".aae");

    // Use sys_fileio to check existence
    if (!fileExistsReadable(fullpath.c_str())) {
        LOG_INFO("Hiscore / nvram file not found: %s", fullpath.c_str());
        return 1;
    }

    // Use sys_fileio to load
    uint8_t* data = loadFile(fullpath.c_str());
    if (!data) {
        LOG_INFO("Failed to load Hiscore file");
        return 1;
    }

    // Safety check size
    size_t fileSize = getLastFileSize();
    if (fileSize < (size_t)size) {
        LOG_INFO("Hiscore file too small");
        free(data);
        return 1;
    }

    LOG_INFO("Loading Hiscore table / nvram from %s", fullpath.c_str());
    std::memcpy(Machine->memory_region[CPU0] + start, data, size);
    free(data);
    return 0;
}

int save_hi_aae(int start, int size, int image)
{
    std::string fullpath = getpathM("hi", 0) + "/";
    fullpath.append(Machine->gamedrv->name);
    fullpath.append(".aae");

    LOG_INFO("Saving Hiscore table / nvram to %s", fullpath.c_str());

    // We can copy to buffer or just cast memory pointer if we are careful, 
    // but saving safely implies using the buffer.
    // Machine->memory_region is likely valid memory.

    return saveFile(fullpath.c_str(), Machine->memory_region[CPU0] + start, size) ? 0 : 1;
}

// -----------------------------------------------------------------------------
// Generic NVRAM persistence (MAME generic_0fill / generic_1fill equivalent)
//
// A driver calls nvram_set_region(buf, size, fill) in its init() to register its
// battery-backed RAM region, then uses AAE_DRIVER_NVRAM(generic_nvram_handler).
// The emulator opens the per-game NVRAM file (osd_fopen / OSD_FILETYPE_NVRAM) on
// game start/stop and calls the handler: read_or_write==0 loads it (or fills with
// `fill` on a first boot with no file), ==1 saves it. `fill` defaults to 0x00
// (MAME generic_0fill); pass 0xFF for chips that power up all-ones. Pass a
// negative `fill` when the driver's init() already lays down factory defaults:
// first boot then keeps whatever init() wrote instead of clearing the region.
// -----------------------------------------------------------------------------
static unsigned char* s_nvram_region = nullptr;
static int            s_nvram_region_size = 0;
static int            s_nvram_fill = 0x00;

void nvram_set_region(void* ptr, int size, int fill)
{
    s_nvram_region      = (unsigned char*)ptr;
    s_nvram_region_size = size;
    s_nvram_fill        = fill;   // <0 => no fill: keep init()'s contents on first boot
}

void generic_nvram_handler(void* file, int read_or_write)
{
    if (!s_nvram_region || s_nvram_region_size <= 0)
    {
        LOG_INFO("generic_nvram_handler: no region registered (call nvram_set_region in init)");
        return;
    }

    if (read_or_write)
    {
        osd_fwrite(file, s_nvram_region, s_nvram_region_size);
        LOG_INFO("Saved %d bytes of NVRAM", s_nvram_region_size);
    }
    else if (file)
    {
        osd_fread(file, s_nvram_region, s_nvram_region_size);
        LOG_INFO("Loaded %d bytes of NVRAM", s_nvram_region_size);
    }
    else if (s_nvram_fill >= 0)
    {
        memset(s_nvram_region, s_nvram_fill & 0xff, s_nvram_region_size);   // first boot: no file yet
        LOG_INFO("Initialized %d bytes of NVRAM (fill 0x%02X)", s_nvram_region_size, s_nvram_fill & 0xff);
    }
    else
    {
        LOG_INFO("First-boot NVRAM: kept driver factory defaults (%d bytes)", s_nvram_region_size);
    }
}

// -----------------------------------------------------------------------------
// find_file_by_crc
// Linear scan of an open zip's central directory for an entry whose CRC32
// matches `crc`. Used as the fallback lookup when a ROM file was renamed
// (or the set only differs from its parent by filename) but the content is
// identical to what the RomModule table expects. `crc` == 0 is treated as
// "unknown" and never matches, matching the RomModule convention elsewhere
// in this file (crc == 0 skips the CRC check).
// -----------------------------------------------------------------------------
static mz_uint find_file_by_crc(mz_zip_archive* zip, unsigned int crc)
{
    if (!zip || !crc) return (mz_uint)-1;
    mz_uint numFiles = mz_zip_reader_get_num_files(zip);
    for (mz_uint i = 0; i < numFiles; ++i)
    {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(zip, i, &st)) continue;
        if (st.m_crc32 == crc) return i;
    }
    return (mz_uint)-1;
}

// Resolves the on-disk zip path for a ROM archive base name, trying the
// configured mame_rom_path first, then the default roms path. Returns true
// and fills `outPath` if the zip exists.
static bool resolve_rom_zip_path(const char* archname, std::string& outPath)
{
    outPath = get_config_string("main", "mame_rom_path", "roms");
    outPath.append("/").append(archname).append(".zip");
    if (file_exists(outPath)) return true;

    outPath = getpathM("roms", 0) + "/" + archname + ".zip";
    return file_exists(outPath);
}

// -----------------------------------------------------------------------------
// verify_rom
// Looks up ROM romnum in archname's zip, falling back to a CRC scan of the
// same zip, then to parentname's zip (by filename, then CRC) when the own
// zip doesn't have it. `parentname` may be nullptr for parent/standalone
// sets. Both archives (whichever were opened) are closed on every exit path.
// -----------------------------------------------------------------------------
int verify_rom(const char* archname, const char* parentname, const struct RomModule* p, int romnum)
{
    if (!archname || !p) return 4;

    const auto& rom = p[romnum];
    LOG_INFO("Trying to verify ROM %s", rom.filename);

    if (!rom.filename || rom.filename == (char*)-1 || rom.filename == (char*)-2)
        return 4;
    if (rom.loadAddr == ROM_REGION_START || rom.loadAddr == 0x999)
        return 4;

    std::string ownPath;
    bool ownExists = resolve_rom_zip_path(archname, ownPath);

    mz_zip_archive ownZip;
    memset(&ownZip, 0, sizeof(ownZip));
    bool ownOpen = ownExists && mz_zip_reader_init_file(&ownZip, ownPath.c_str(), 0);

    mz_zip_archive parentZip;
    memset(&parentZip, 0, sizeof(parentZip));
    bool parentOpen = false;
    std::string parentPath;

    if (!ownOpen && !parentname) {
        LOG_INFO("ROM ZIP not found: %s", ownPath.c_str());
        return 5; // NOZIP
    }

    mz_zip_archive* srcZip = nullptr;
    mz_uint fileIndex = (mz_uint)-1;
    int method = 0; // 0 = own by name, 1 = own by CRC, 2 = parent by name, 3 = parent by CRC

    if (ownOpen) {
        fileIndex = mz_zip_reader_locate_file(&ownZip, rom.filename, 0, 0);
        if (fileIndex == (mz_uint)-1) {
            fileIndex = find_file_by_crc(&ownZip, rom.crc);
            method = 1;
        }
        if (fileIndex != (mz_uint)-1) srcZip = &ownZip;
    }

    if (fileIndex == (mz_uint)-1 && parentname) {
        if (resolve_rom_zip_path(parentname, parentPath))
            parentOpen = mz_zip_reader_init_file(&parentZip, parentPath.c_str(), 0);

        if (parentOpen) {
            fileIndex = mz_zip_reader_locate_file(&parentZip, rom.filename, 0, 0);
            method = 2;
            if (fileIndex == (mz_uint)-1) {
                fileIndex = find_file_by_crc(&parentZip, rom.crc);
                method = 3;
            }
            if (fileIndex != (mz_uint)-1) srcZip = &parentZip;
        }
    }

    int result;
    if (fileIndex == (mz_uint)-1 || !srcZip) {
        if (!ownOpen && !parentOpen) {
            LOG_INFO("ROM ZIP not found: %s", ownPath.c_str());
            result = 5; // NOZIP
        } else {
            LOG_INFO("ROM file not found in zip: %s", rom.filename);
            result = 4; // NOFILE
        }
    }
    else {
        if (method != 0) {
            LOG_INFO("ROM %s found in %s archive via %s", rom.filename,
                (method == 2 || method == 3) ? "parent" : "own",
                (method == 1 || method == 3) ? "CRC" : "name");
        }

        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(srcZip, fileIndex, &st)) {
            LOG_INFO("Could not stat ROM in zip: %s", rom.filename);
            result = 4;
        }
        else {
            unsigned int actualSize = (unsigned int)st.m_uncomp_size;
            if (actualSize != static_cast<unsigned int>(rom.romSize)) {
                LOG_INFO("ROM size mismatch: %s expected %d, got %u", rom.filename, rom.romSize, actualSize);
                result = 3; // BADSIZE
            }
            else {
                unsigned char* buf = (unsigned char*)malloc(actualSize);
                if (!buf || !mz_zip_reader_extract_to_mem(srcZip, fileIndex, buf, actualSize, 0)) {
                    LOG_INFO("ROM file failed to extract from zip: %s", rom.filename);
                    result = 4;
                }
                else {
                    result = 1; // OK

                    if (rom.sha) {
                        const char* calcSha = sha1.CalculateHash(buf, actualSize);
                        if (strcmp(calcSha, rom.sha) != 0) {
                            LOG_INFO("ROM SHA1 mismatch: %s expected %s", rom.filename, rom.sha);
                            result = 0; // BAD?
                        }
                    }

                    // A CRC-fallback lookup (method 1/3) already matched the
                    // expected CRC by construction; only re-check it for
                    // ROMs found by name, same as before this fallback existed.
                    if (result == 1 && rom.crc && (method == 0 || method == 2)) {
                        unsigned int fileCrc = st.m_crc32;
                        if (fileCrc != rom.crc) {
                            LOG_INFO("ROM CRC mismatch: %s expected %08X, got %08X", rom.filename, rom.crc, fileCrc);
                            result = 0; // BAD?
                        }
                    }
                }
                if (buf) free(buf);
            }
        }
    }

    if (ownOpen) mz_zip_reader_end(&ownZip);
    if (parentOpen) mz_zip_reader_end(&parentZip);
    return result;
}

int verify_sample(const char** samples, int num)
{
    if (!samples || !samples[num]) return 4;

    std::string sampleZip = getpathM("samples", 0) + std::string("/") + samples[0] + ".zip";
    if (!file_exists(sampleZip)) {
        LOG_INFO("Sample ZIP not found: %s", sampleZip.c_str());
        return 5; // NOZIP
    }

    const char* filename = samples[num];
    unsigned char* buf = loadZip(sampleZip.c_str(), filename);
    if (!buf) {
        LOG_INFO("Sample file not found in zip: %s", filename);
        return 4; // NOFILE
    }

    free(buf);
    return 1; // OK
}

// -----------------------------------------------------------------------------
// load_roms
// This requires complex iteration over the ZIP file which sys_fileio (loadZip)
// does not support (it only supports loading one specific file).
// Therefore, we keep the direct miniz implementation here, but clean it up.
// -----------------------------------------------------------------------------
int load_roms(const char* archname, const char* parentname, const struct RomModule* p)
{
    mz_bool status;
    mz_uint file_index = -1;
    mz_zip_archive zip_archive;         // own archive
    mz_zip_archive parent_zip_archive;  // parent archive (opened lazily)
    mz_zip_archive_file_stat file_stat;
    std::string temppath;
    unsigned char* zipdata = 0;
    const char* shatest = 0;
    const char* last_reload_filename = nullptr;
    mz_zip_archive* last_source_zip = nullptr;  // archive that supplied the preceding real ROM (for ROM_RELOAD)
    mz_uint last_source_index = (mz_uint)-1;    // its file index in that archive (for ROM_RELOAD)
    mz_zip_archive* src_zip = nullptr;          // archive the current entry was actually found in
    int skip = 0;
    int ret = EXIT_SUCCESS;
    int i, j = 0;
    int crc = 0;
    int cpunum = 0;
    int region = 0;
    unsigned int current_uncomp_size = 0;
    bool own_open = false;
    bool parent_open = false;
    bool parent_attempted = false;

    temppath = config.exrompath;
    temppath.append("/");
    temppath.append(archname);
    temppath.append(".zip");

    if (!file_exists(temppath.c_str())) {
        DLOG("Rom not found in external path, looking in rom folder");
        temppath = getpathM("roms", 0) + "/" + archname + ".zip";
    }

    DLOG("ROM Path: %s", temppath.c_str());

    memset(&zip_archive, 0, sizeof(zip_archive));
    memset(&parent_zip_archive, 0, sizeof(parent_zip_archive));

    if (file_exists(temppath.c_str()))
        own_open = mz_zip_reader_init_file(&zip_archive, temppath.c_str(), 0) != 0;

    // Opens the parent archive on first use only. A MAME merged set keeps
    // clone ROMs inside the parent zip, so this is also how we recover when
    // the own zip doesn't exist at all (own_open == false below).
    auto open_parent_if_needed = [&]() -> bool {
        if (parent_attempted) return parent_open;
        parent_attempted = true;
        if (!parentname) return false;

        std::string parentpath = config.exrompath;
        parentpath.append("/").append(parentname).append(".zip");
        if (!file_exists(parentpath.c_str()))
            parentpath = getpathM("roms", 0) + "/" + parentname + ".zip";

        if (!file_exists(parentpath.c_str())) return false;

        parent_open = mz_zip_reader_init_file(&parent_zip_archive, parentpath.c_str(), 0) != 0;
        if (parent_open) LOG_INFO("Opened parent archive: %s.zip", parentname);
        return parent_open;
    };

    if (!own_open) {
        if (!open_parent_if_needed()) {
            LOG_ERROR("Zip File %s failed to open. Archive missing?", archname);
            return EXIT_FAILURE;
        }
        LOG_INFO("Archive %s.zip not found; loading from parent archive %s.zip instead", archname, parentname);
    }

    DLOG("ROM_START(%s)", archname);
    // LOG_INFO("starting with romsize = %d romsize 1 = %d", p[0].romSize, p[1].romSize);

    for (i = 0; p[i].romSize > 0; i += 1)
    {
        if (p[i].loadAddr == ROM_REGION_START)
        {
            new_memory_region(p[i].loadtype, p[i].romSize, p[i].disposable);
            cpunum = p[i].loadtype;
        }
        else {
            if (p[i].filename == (char*)-2) { goto gohere; } // ROM_CONTINUE

            if (p[i].filename == (char*)-1) // ROM_RELOAD
            {
                // Reuse the exact archive + file index that supplied the preceding
                // real ROM, rather than re-locating it by name: if that ROM was
                // found via the CRC fallback (own or parent), its on-disk filename
                // differs from what the RomModule table expects, so a by-name
                // lookup here would fail even though we already know exactly
                // which zip entry to re-read. last_reload_filename is kept only
                // for logging.
                if (last_reload_filename == 0) last_reload_filename = p[i - 1].filename;
                src_zip = last_source_zip;
                file_index = last_source_index;
                LOG_INFO("ROM_RELOAD(0x%04x, 0x%04x)", p[i].loadAddr, p[i].romSize);
            }
            else
            {
                LOG_INFO("Starting to load Rom: %s", p[i].filename);
                last_reload_filename = nullptr;

                file_index = (mz_uint)-1;
                src_zip = nullptr;
                int method = 0; // 0 own/name, 1 own/CRC, 2 parent/name, 3 parent/CRC

                if (own_open) {
                    file_index = mz_zip_reader_locate_file(&zip_archive, p[i].filename, 0, 0);
                    if (file_index == (mz_uint)-1) {
                        file_index = find_file_by_crc(&zip_archive, p[i].crc);
                        method = 1;
                    }
                    if (file_index != (mz_uint)-1) src_zip = &zip_archive;
                }

                if (file_index == (mz_uint)-1 && open_parent_if_needed()) {
                    file_index = mz_zip_reader_locate_file(&parent_zip_archive, p[i].filename, 0, 0);
                    method = 2;
                    if (file_index == (mz_uint)-1) {
                        file_index = find_file_by_crc(&parent_zip_archive, p[i].crc);
                        method = 3;
                    }
                    if (file_index != (mz_uint)-1) src_zip = &parent_zip_archive;
                }

                if (file_index != (mz_uint)-1 && method != 0) {
                    LOG_INFO("ROM %s supplied by %s archive via %s", p[i].filename,
                        (method == 2 || method == 3) ? "parent" : "own",
                        (method == 1 || method == 3) ? "CRC" : "name");
                }

                last_source_zip = src_zip;
                last_source_index = file_index;
            }

            if (file_index == (mz_uint)-1 || !src_zip) {
                LOG_ERROR("File not found in zip: %s", p[i].filename ? p[i].filename : "<null>");
                ret = EXIT_FAILURE;
                goto end;
            }

            if (config.debug_profile_code) {
                if (last_reload_filename) LOG_INFO("Loading Rom: %s", last_reload_filename);
                else if (p[i].filename)   LOG_INFO("Loading Rom: %s", p[i].filename);
            }

            status = mz_zip_reader_file_stat(src_zip, file_index, &file_stat);
            if (status != MZ_TRUE) { LOG_ERROR("Could not read file in Zip, corrupt?"); ret = EXIT_FAILURE; goto end; }

            // CHANGED: Assignment only, declaration moved to top
            current_uncomp_size = (unsigned int)file_stat.m_uncomp_size;

            if ((mz_uint64)p[i].romSize != file_stat.m_uncomp_size)
            {
                if (p[i + 1].filename != (char*)-2) // Not ROM_CONTINUE
                {
                    LOG_ERROR("Warning: File Size Mismatch, check your rom definition and romset.");
                    ret = EXIT_FAILURE; goto end;
                }
            }

            zipdata = (unsigned char*)malloc(current_uncomp_size);
            status = mz_zip_reader_extract_to_mem(src_zip, file_index, zipdata, current_uncomp_size, 0);
            if (status != MZ_TRUE) { LOG_ERROR("File Failed to Extract"); ret = EXIT_FAILURE; goto end; }

            if (p[i].filename != (char*)-1 && p[i].filename != (char*)-2)
            {
                shatest = sha1.CalculateHash(zipdata, current_uncomp_size);
                if (p[i].sha && strcmp(shatest, p[i].sha) != 0) {
                    LOG_ERROR("SHA-1 mismatch. Expected: %s, Got: %s", p[i].sha, shatest);
                }

                crc = static_cast<int>(file_stat.m_crc32);
                if (p[i].crc && (unsigned int)crc != p[i].crc) {
                    LOG_ERROR("CRC mismatch: Expected %x, Got %x", p[i].crc, crc);
                }
            }

            region = cpunum;

            if (!Machine->memory_region[region]) {
                LOG_ERROR("ROM %s targets memory region %d, which was not allocated", p[i].filename ? p[i].filename : "<reload>", region);
                ret = EXIT_FAILURE; goto end;
            }

        gohere:
            if (p[i].filename == (char*)-2) 
            {
                skip = 0; 
                for (int k = i - 1; k >= 0; --k) 
                {
                    skip += p[k].romSize; 
                    if (p[k].filename != (char*)-2) break; } 
            } // cumulative offset of preceding chunks; fixes multi-way ROM_CONTINUE (e.g. dkongjr 5c/5e), not just p[i-1]
            else skip = 0;

            switch (p[i].loadtype)
            {
            case ROM_LOAD_NORMAL:
                for (j = 0; j < p[i].romSize; j++)
                    Machine->memory_region[region][j + p[i].loadAddr] = zipdata[j + skip];
                break;
            case ROM_LOAD_16:
                for (j = 0; j < p[i].romSize; j++)
                    Machine->memory_region[region][(j * 2) + p[i].loadAddr] = zipdata[j];
                break;
            case ROM_LOAD_NIB_LOW_T:
                for (j = 0; j < p[i].romSize; j++) {
                    unsigned char* dst = &Machine->memory_region[region][j + p[i].loadAddr];
                    *dst = (unsigned char)((*dst & 0xF0) | (zipdata[j + skip] & 0x0F));
                }
                break;
            case ROM_LOAD_NIB_HIGH_T:
                for (j = 0; j < p[i].romSize; j++) {
                    unsigned char* dst = &Machine->memory_region[region][j + p[i].loadAddr];
                    *dst = (unsigned char)((*dst & 0x0F) | ((zipdata[j + skip] & 0x0F) << 4));
                }
                break;
            default:
                LOG_ERROR("Invalid load type in ROM loader"); break;
            }

            // Free only if next entry isn't reusing this data (ROM_CONTINUE)
            if (p[i + 1].filename != (char*)-2)
            {
                free(zipdata);
                zipdata = nullptr;
            }
        }
    }

end:
    if (zipdata) free(zipdata); // Safety cleanup if goto end happened
    if (own_open) mz_zip_reader_end(&zip_archive);
    if (parent_open) mz_zip_reader_end(&parent_zip_archive);
    LOG_INFO("Finished loading roms");

    return ret;
}


// --- SAMPLE LOADER ---

// Custom Deleter for unique_ptr using free
struct MallocDeleter {
    void operator()(uint8_t* p) const { free(p); }
};

int load_sample_core(const std::string& zip_path, const std::string& zip_entry, const std::string& disk_path)
{
    std::unique_ptr<uint8_t, MallocDeleter> buffer;
    size_t size = 0;
    bool loaded_from_zip = false;

    // A: Try loading from Zip Archive first via sys_fileio
    if (!zip_path.empty()) {
        uint8_t* zip_data = loadZip(zip_path.c_str(), zip_entry.c_str());
        if (zip_data) {
            buffer.reset(zip_data);
            size = getLastZSize();
            loaded_from_zip = true;
        }
    }

    // B: Fallback to direct file load via sys_fileio
    if (!buffer) {
        uint8_t* file_data = loadFile(disk_path.c_str());
        if (file_data) {
            buffer.reset(file_data);
            size = getLastFileSize();
        }
    }

    // C: Validation
    if (!buffer || size == 0) {
        LOG_ERROR("Failed to load sample: '%s' (Checked Zip: '%s' and Disk: '%s')",
            zip_entry.c_str(), zip_path.c_str(), disk_path.c_str());
        return -1;
    }

    // D: Submit to Mixer
    int sample_id = load_sample_from_buffer(buffer.get(), size, zip_entry.c_str(), true);

    if (sample_id >= 0) {
        LOG_INFO("Loaded sample ID %d: %s %s",
            sample_id, zip_entry.c_str(), (loaded_from_zip ? "[Zip]" : "[File]"));
    }

    return sample_id;
}

void load_samples_batch(const char* const* sample_list)
{
    if (!sample_list || !sample_list[0]) return;

    std::string archiveName = sample_list[0];
    std::string fullZipPath = "samples/" + archiveName;

    std::string subFolderName = archiveName;
    size_t lastDot = subFolderName.find_last_of('.');
    if (lastDot != std::string::npos) {
        subFolderName = subFolderName.substr(0, lastDot);
    }

    LOG_INFO("Batch loading samples. Archive: '%s', Fallback Dir: 'samples/%s/'",
        fullZipPath.c_str(), subFolderName.c_str());

    int i = 1;
    while (sample_list[i] != nullptr) {
        const char* filename = sample_list[i];
        if (strcmp(filename, "NULL") == 0) break;

        std::string entryName = filename;
        std::string fullDiskPath = "samples/" + subFolderName + "/" + entryName;

        if (load_sample_core(fullZipPath, entryName, fullDiskPath) < 0) {
            // Sample file missing/unloadable: register a silent placeholder so the
            // remaining samples keep their expected indices. Drivers call
            // sample_start() with hard-coded indices that must match the load
            // order, so skipping a missing sample would shift every later sample
            // down one slot and play the wrong sounds.
            load_silent_sample(entryName.c_str());
        }
        i++;
    }
}

// -----------------------------------------------------------------------------
// load_ambient_samples
//
// Loads the 3 optional AAE ambient audio files from "samples/aae.zip"
// (with loose-file fallback to "samples/aae/").
//
// The ambient files are:
//   - flyback.wav   (CRT horizontal flyback chatter)
//   - psnoise.wav   (power supply hum / buzz)
//   - hiss.wav      (background static / tape hiss)
//
// These are loaded into the mixer's sample registry just like any game
// sample. Because load_sample_from_buffer() assigns sequential IDs via
// ++sound_id, the ambient samples will always get IDs AFTER whatever the
// current game has loaded. They are looked up by NAME (via nameToNum)
// rather than by index, so the number of game samples does not matter.
//
// This function is safe to call even if aae.zip does not exist or if
// individual files are missing -- each file is loaded independently and
// a missing file is logged as INFO, not treated as a fatal error.
// -----------------------------------------------------------------------------
void load_ambient_samples()
{
    // The ambient sample filenames, loaded from "samples/aae.zip"
    static const char* ambient_files[] = {
        "flyback.wav",
        "psnoise.wav",
        "hiss.wav",
        nullptr
    };

    const std::string zipPath = "samples/aae.zip";
    const std::string diskBase = "samples/aae/";

    LOG_INFO("Loading ambient samples from '%s' (fallback: '%s')", zipPath.c_str(), diskBase.c_str());

    int loaded_count = 0;

    for (int i = 0; ambient_files[i] != nullptr; ++i)
    {
        const std::string entryName = ambient_files[i];
        const std::string diskPath = diskBase + entryName;

        int id = load_sample_core(zipPath, entryName, diskPath);

        if (id >= 0) {
            LOG_INFO("Ambient sample '%s' loaded as sample ID %d", entryName.c_str(), id);
            loaded_count++;
        }
        else {
            // Not fatal -- ambient audio is optional
            LOG_INFO("Ambient sample '%s' not found (optional, skipping)", entryName.c_str());
        }
    }

    LOG_INFO("Ambient sample loading complete: %d of 3 loaded", loaded_count);
}
