#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <memory>
#include "aae_fileio.h"
#include "log.h"
#include "miniz.h"
#include "aae_mame_driver.h"
#include "memory.h"
#include "path_helper.h"
#include "loaders.h"
#include "gameroms.h"
#include "sha-1.h"

#define DEBUG_LOG 1

#ifdef DEBUG_LOG
#define DLOG(msg, ...) LOG_INFO(msg, ##__VA_ARGS__)
#else
#define DLOG(msg, ...)
#endif

CSHA1 sha1;
unsigned int filesz = 0;
unsigned int uncomp_size = 0;
uint32_t last_zip_crc = 0;

int get_last_crc() { return last_zip_crc; }
unsigned int get_last_file_size() { return filesz; }
unsigned int get_last_zip_file_size() { return uncomp_size; }

int getFileSize(FILE* input) {
    fseek(input, 0, SEEK_END);
    int size = ftell(input);
    fseek(input, 0, SEEK_SET);
    return size;
}

unsigned char* load_file(const char* filename) {
    filesz = 0;
    FILE* fd = nullptr;
    if (fopen_s(&fd, filename, "rb") != 0 || !fd) {
        LOG_INFO("Failed to find file: %s", filename);
        return nullptr;
    }

    filesz = getFileSize(fd);
    std::unique_ptr<unsigned char[]> buf(new unsigned char[filesz]);
    fread(buf.get(), 1, filesz, fd);
    fclose(fd);
    return buf.release();
}

int save_file_char(const char* filename, const char* buf, int size) {
    FILE* fd = nullptr;
    if (fopen_s(&fd, filename, "w") != 0 || !fd) {
        LOG_INFO("Saving RAM failed: %s", filename);
        return 0;
    }
    fwrite(buf, size, 1, fd);
    fclose(fd);
    return 1;
}

int save_file(const char* filename, const unsigned char* buf, int size) {
    FILE* fd = nullptr;
    if (fopen_s(&fd, filename, "wb") != 0 || !fd) {
        LOG_INFO("Failed to save file: %s", filename);
        return 0;
    }
    fwrite(buf, size, 1, fd);
    fclose(fd);
    return 1;
}

unsigned char* load_zip_file(const char* archname, const char* filename) {
    mz_zip_archive zip_archive;
    mz_zip_archive_file_stat file_stat;
    unsigned char* buf = nullptr;
    mz_bool status;

    DLOG("Opening Archive %s", archname);
    memset(&zip_archive, 0, sizeof(zip_archive));
    status = mz_zip_reader_init_file(&zip_archive, archname, 0);
    if (!status) {
        LOG_INFO("Zip Archive %s not found", archname);
        return nullptr;
    }

    mz_uint file_index = mz_zip_reader_locate_file(&zip_archive, filename, 0, 0);
    if (file_index == -1) {
        LOG_INFO("File %s not found in archive %s", filename, archname);
        mz_zip_reader_end(&zip_archive);
        return nullptr;
    }

    if (!mz_zip_reader_file_stat(&zip_archive, file_index, &file_stat)) {
        LOG_INFO("Corrupt zip file or read error in %s", archname);
        mz_zip_reader_end(&zip_archive);
        return nullptr;
    }

    uncomp_size = (unsigned int)file_stat.m_uncomp_size;
    buf = (unsigned char*)malloc(uncomp_size);
    if (!buf) {
        LOG_INFO("Memory allocation failed for zip extraction");
        mz_zip_reader_end(&zip_archive);
        return nullptr;
    }

    status = mz_zip_reader_extract_to_mem(&zip_archive, file_index, buf, uncomp_size, 0);
    mz_zip_reader_end(&zip_archive);

    if (!status) {
        LOG_INFO("Failed to extract file %s from archive %s", filename, archname);
        free(buf);
        return nullptr;
    }

    DLOG("Zip file %s loaded successfully from archive %s", filename, archname);
    return buf;
}

bool save_zip_file(const char* archname, const char* filename, const unsigned char* data) {
    bool status = mz_zip_add_mem_to_archive_file_in_place(archname, filename, data, strlen((const char*)data) + 1, 0, 0, MZ_BEST_COMPRESSION);
    if (!status) {
        LOG_INFO("Failed to add %s to archive %s", filename, archname);
        return false;
    }
    return true;
}

// 5/6/25 - 5/8/25:
//  Added ROM_CONTINUE support
//  Added better ROM_RELOAD support
//  Added NON-MAME crc and SHA-1 checking. 
//  Not the cleanest code, but it's working. 

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
    int test = 0;
    int skip = 0;
    int ret = EXIT_SUCCESS;
    int i, j = 0;
    int crc = 0;
    int size = 0;
    int cpunum = 0;
    int region = 0;

    temppath = config.exrompath;
    temppath.append("\\");
    temppath.append(archname);
    temppath.append(".zip");

    if (!file_exist(temppath.c_str())) {
        DLOG("Rom not found in external path, looking in rom folder");
        temppath = getpathM("roms", 0) + "\\" + archname + ".zip";
    }

    DLOG("ROM Path: %s", temppath.c_str());

    memset(&zip_archive, 0, sizeof(zip_archive));
    status = mz_zip_reader_init_file(&zip_archive, temppath.c_str(), 0);
    if (!status) {
        LOG_INFO("Zip File %s failed to open. Archive missing?", archname);
        goto end;
    }

    //Start Loading ROMS HERE////
    DLOG("ROM_START(%s)", archname);
    //LOG_INFO("starting with romsize = %d romsize 1 = %d",p[0].romSize,p[1].romSize);
    for (i = 0; p[i].romSize > 0; i += 1)
    {
        //   Check for ROM_REGION: IF SO, decode and skip
        if (p[i].loadAddr == ROM_REGION_START)
        {
            // This is temporary, to help me build romsets with the correct SHA without typing
            //LOG_INFO("ROM_REGION(0x%04x, %s)", p[i].romSize, rom_regions[p[i].loadtype]);
            //Allocate Memory for this region
            new_memory_region(p[i].loadtype, p[i].romSize);
            cpunum = p[i].loadtype;
        }
        else {
            // Find the requested file, ignore case
            // Is this ROM_CONTINUE, then skip to bottom.
            if (p[i].filename == (char*)-2) { goto gohere; }
            //Is it a ROM_RELOAD? Then use previous stored filename. Here we go!
            if (p[i].filename == (char*)-1)
            {
                if (last_reload_filename == 0) // No Previously stored filename?
                {
                    last_reload_filename = p[i - 1].filename; // Set it!
                }
                file_index = mz_zip_reader_locate_file(&zip_archive, last_reload_filename, 0, 0);
                ////// TEMP PRINTING
                //LOG_INFO("ROM_RELOAD(0x%04x, 0x%04x)", p[i].loadAddr, p[i].romSize);
            }
            else
            {
                // Were loading normally, so lets take this opportunity to reset the rom reload filename for reuse. 
                last_reload_filename = nullptr;
                file_index = mz_zip_reader_locate_file(&zip_archive, p[i].filename, 0, 0);
            }

            if (file_index == -1) {
                LOG_INFO("File not found in zip: %s", p[i].filename ? p[i].filename : "<null>");
                goto end;
            }

            //We have a valid file, lets tell everyone.
            if (config.debug_profile_code) {
                if (p[i].filename == (char*)-1)
                    LOG_INFO("Loading Rom: %s", p[i - 1].filename);
                else
                    LOG_INFO("Loading Rom: %s", p[i].filename);
            }
            // Get information on the current file
            status = mz_zip_reader_file_stat(&zip_archive, file_index, &file_stat);
            if (status != MZ_TRUE) { LOG_INFO("Could not read file in Zip, corrupt?"); ret = EXIT_FAILURE; goto end; }

            //Get File Size
            uncomp_size = (unsigned int)file_stat.m_uncomp_size;

            // Size mismatch?
            if (p[i].romSize != file_stat.m_uncomp_size)
            {
                // Check if ROM_CONTINUE is screwing things up
                if (p[i + 1].filename != (char*)-2)
                {
                    LOG_INFO("Warning: File Size Mismatch, check your rom definition and romset.");
                    ret = EXIT_FAILURE; goto end;
                }
            }

            zipdata = (unsigned char*)malloc(uncomp_size);//p[i].romSize); // We don't use romSize here because of ROM_CONTINUE
            // Read (decompress) the file
            status = mz_zip_reader_extract_to_mem(&zip_archive, file_index, zipdata, uncomp_size, 0);
            if (status != MZ_TRUE) { LOG_INFO("File Failed to Extract"); ret = EXIT_FAILURE; goto end; }
            //LOG_INFO("Passed file extract");
            if (p[i].filename != (char*)-1 && p[i].filename != (char*)-2)
            {
                //SHA-1 Check
                shatest = sha1.CalculateHash(zipdata, uncomp_size);
                if (p[i].sha && strcmp(shatest, p[i].sha) != 0) {
                    LOG_INFO("SHA-1 mismatch. Expected: %s, Got: %s", p[i].sha, shatest);
                }

                //CRC Check
                crc = static_cast<int>(file_stat.m_crc32);
                if (p[i].crc && crc != p[i].crc) {
                    LOG_INFO("CRC mismatch: Expected %x, Got %x", p[i].crc, crc);
                }
            }

            region = cpunum;// Machine->drv->cpu[cpunum].memory_region;

        gohere:
            //This is for ROM CONTINUE Support
            if (p[i].filename == (char*)-2) { skip = p[i - 1].romSize; }
            else skip = 0;
            //Get the memory region for the current CPU number

            switch (p[i].loadtype)
            {
            case ROM_LOAD_NORMAL:
            {
                for (j = 0; j < p[i].romSize; j++)
                {
                    Machine->memory_region[region][j + p[i].loadAddr] = zipdata[j + skip];
                }
                break;
            }
            case ROM_LOAD_16:
            {
                for (j = 0; j < p[i].romSize; j++)
                {
                    Machine->memory_region[region][(j * 2) + p[i].loadAddr] = zipdata[j];
                }
                break;
            }

            default:  LOG_INFO("Invalid load type in ROM loader"); break;
            }
            //Finished loading Rom
            //LOG_INFO("ROM Loaded");
            if (p[i + 1].filename != (char*)-2)
            {
                free(zipdata);
            }
        }
    }

end:
    // Close the archive
    mz_zip_reader_end(&zip_archive);
    //	LOG_INFO("ROM_END");                   //// TEMPORARY FOR BUILDING ROMSETS
    LOG_INFO("Finished loading roms");

    if (ret == EXIT_SUCCESS)
    {
        if (config.debug_profile_code) { LOG_INFO("Zip file loaded Successfully"); }
        return EXIT_SUCCESS;
    }
    else
    {
        LOG_INFO("Zip file failed to load!");
        return EXIT_FAILURE;
    }
    return EXIT_FAILURE;
}