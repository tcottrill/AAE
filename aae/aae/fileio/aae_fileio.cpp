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
    std::string fullpath = getpathM("hi", 0) + "\\";
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
    std::string fullpath = getpathM("hi", 0) + "\\";
    fullpath.append(Machine->gamedrv->name);
    fullpath.append(".aae");

    LOG_INFO("Saving Hiscore table / nvram to %s", fullpath.c_str());

    // We can copy to buffer or just cast memory pointer if we are careful, 
    // but saving safely implies using the buffer.
    // Machine->memory_region is likely valid memory.

    return saveFile(fullpath.c_str(), Machine->memory_region[CPU0] + start, size) ? 0 : 1;
}

// -----------------------------------------------------------------------------
// verify_rom
// Uses sys_fileio::loadZip and getters to verify
// -----------------------------------------------------------------------------
int verify_rom(const char* archname, const struct RomModule* p, int romnum)
{
    std::string zipPath;
    if (!archname || !p) return 4;

    const auto& rom = p[romnum];
    LOG_INFO("Trying to verify ROM %s", rom.filename);

    if (!rom.filename || rom.filename == (char*)-1 || rom.filename == (char*)-2)
        return 4;
    if (rom.loadAddr == ROM_REGION_START || rom.loadAddr == 0x999)
        return 4;

    zipPath = get_config_string("main", "mame_rom_path", "roms");
    zipPath.append("\\");
    zipPath.append(archname);
    zipPath.append(".zip");

    if (!file_exists(zipPath)) {
        zipPath = getpathM("roms", 0) + "\\" + archname + ".zip";
        if (!file_exists(zipPath)) {
            LOG_INFO("ROM ZIP not found: %s", zipPath.c_str());
            return 5; // NOZIP
        }
    }

    // Use Generic Loader
    unsigned char* buf = loadZip(zipPath.c_str(), rom.filename);
    if (!buf) {
        LOG_INFO("ROM file not found in zip: %s", rom.filename);
        return 4; // NOFILE
    }

    // Use Generic State Getters
    unsigned int actualSize = (unsigned int)getLastZSize();

    if (actualSize != static_cast<unsigned int>(rom.romSize)) {
        LOG_INFO("ROM size mismatch: %s expected %d, got %u", rom.filename, rom.romSize, actualSize);
        free(buf);
        return 3; // BADSIZE
    }

    if (rom.sha) {
        const char* calcSha = sha1.CalculateHash(buf, actualSize);
        if (strcmp(calcSha, rom.sha) != 0) {
            LOG_INFO("ROM SHA1 mismatch: %s expected %s", rom.filename, rom.sha);
            free(buf);
            return 0; // BAD?
        }
    }

    if (rom.crc) {
        int fileCrc = (int)getLastZCrc(); // From sys_fileio
        if (fileCrc != static_cast<int>(rom.crc)) {
            LOG_INFO("ROM CRC mismatch: %s expected %08X, got %08X", rom.filename, rom.crc, fileCrc);
            free(buf);
            return 0; // BAD?
        }
    }

    free(buf);
    return 1; // OK
}

int verify_sample(const char** samples, int num)
{
    if (!samples || !samples[num]) return 4;

    std::string sampleZip = getpathM("samples", 0) + std::string("\\") + samples[0] + ".zip";
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
int load_roms(const char* archname, const struct RomModule* p)
{
    mz_bool status;
    mz_uint file_index = -1;
    mz_zip_archive zip_archive;
    mz_zip_archive_file_stat file_stat;
    std::string temppath;
    unsigned char* zipdata = 0;
    const char* shatest = 0;
    const char* last_reload_filename = nullptr;
    int skip = 0;
    int ret = EXIT_SUCCESS;
    int i, j = 0;
    int crc = 0;
    int cpunum = 0;
    int region = 0;
    unsigned int current_uncomp_size = 0; 

    temppath = config.exrompath;
    temppath.append("\\");
    temppath.append(archname);
    temppath.append(".zip");

    if (!file_exists(temppath.c_str())) {
        DLOG("Rom not found in external path, looking in rom folder");
        temppath = getpathM("roms", 0) + "\\" + archname + ".zip";
    }

    DLOG("ROM Path: %s", temppath.c_str());

    memset(&zip_archive, 0, sizeof(zip_archive));
    status = mz_zip_reader_init_file(&zip_archive, temppath.c_str(), 0);
    if (!status) {
        LOG_ERROR("Zip File %s failed to open. Archive missing?", archname);
        return EXIT_FAILURE;
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
                if (last_reload_filename == 0) last_reload_filename = p[i - 1].filename;
                file_index = mz_zip_reader_locate_file(&zip_archive, last_reload_filename, 0, 0);
                LOG_INFO("ROM_RELOAD(0x%04x, 0x%04x)", p[i].loadAddr, p[i].romSize);
            }
            else
            {
                LOG_INFO("Starting to load Rom: %s", p[i].filename);
                last_reload_filename = nullptr;
                file_index = mz_zip_reader_locate_file(&zip_archive, p[i].filename, 0, 0);
            }

            if (file_index == -1) {
                LOG_ERROR("File not found in zip: %s", p[i].filename ? p[i].filename : "<null>");
                ret = EXIT_FAILURE;
                goto end;
            }

            if (config.debug_profile_code) {
                if (last_reload_filename) LOG_INFO("Loading Rom: %s", last_reload_filename);
                else if (p[i].filename)   LOG_INFO("Loading Rom: %s", p[i].filename);
            }

            status = mz_zip_reader_file_stat(&zip_archive, file_index, &file_stat);
            if (status != MZ_TRUE) { LOG_ERROR("Could not read file in Zip, corrupt?"); ret = EXIT_FAILURE; goto end; }

            // CHANGED: Assignment only, declaration moved to top
            current_uncomp_size = (unsigned int)file_stat.m_uncomp_size;

            if (p[i].romSize != file_stat.m_uncomp_size)
            {
                if (p[i + 1].filename != (char*)-2) // Not ROM_CONTINUE
                {
                    LOG_ERROR("Warning: File Size Mismatch, check your rom definition and romset.");
                    ret = EXIT_FAILURE; goto end;
                }
            }

            zipdata = (unsigned char*)malloc(current_uncomp_size);
            status = mz_zip_reader_extract_to_mem(&zip_archive, file_index, zipdata, current_uncomp_size, 0);
            if (status != MZ_TRUE) { LOG_ERROR("File Failed to Extract"); ret = EXIT_FAILURE; goto end; }

            if (p[i].filename != (char*)-1 && p[i].filename != (char*)-2)
            {
                shatest = sha1.CalculateHash(zipdata, current_uncomp_size);
                if (p[i].sha && strcmp(shatest, p[i].sha) != 0) {
                    LOG_ERROR("SHA-1 mismatch. Expected: %s, Got: %s", p[i].sha, shatest);
                }

                crc = static_cast<int>(file_stat.m_crc32);
                if (p[i].crc && crc != p[i].crc) {
                    LOG_ERROR("CRC mismatch: Expected %x, Got %x", p[i].crc, crc);
                }
            }

            region = cpunum;

        gohere:
            if (p[i].filename == (char*)-2) { skip = p[i - 1].romSize; }
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
    mz_zip_reader_end(&zip_archive);
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
    std::string fullZipPath = "samples\\" + archiveName;

    std::string subFolderName = archiveName;
    size_t lastDot = subFolderName.find_last_of('.');
    if (lastDot != std::string::npos) {
        subFolderName = subFolderName.substr(0, lastDot);
    }

    LOG_INFO("Batch loading samples. Archive: '%s', Fallback Dir: 'samples\\%s\\'",
        fullZipPath.c_str(), subFolderName.c_str());

    int i = 1;
    while (sample_list[i] != nullptr) {
        const char* filename = sample_list[i];
        if (strcmp(filename, "NULL") == 0) break;

        std::string entryName = filename;
        std::string fullDiskPath = "samples\\" + subFolderName + "\\" + entryName;

        load_sample_core(fullZipPath, entryName, fullDiskPath);
        i++;
    }
}

// -----------------------------------------------------------------------------
// load_ambient_samples
//
// Loads the 3 optional AAE ambient audio files from "samples\aae.zip"
// (with loose-file fallback to "samples\aae\").
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
    // The ambient sample filenames, loaded from "samples\aae.zip"
    static const char* ambient_files[] = {
        "flyback.wav",
        "psnoise.wav",
        "hiss.wav",
        nullptr
    };

    const std::string zipPath = "samples\\aae.zip";
    const std::string diskBase = "samples\\aae\\";

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
