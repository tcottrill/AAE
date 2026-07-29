#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <string>
#include <filesystem>
#include "sys_fileio.h"
#include "sys_log.h"
#include "path_helper.h"   // getpathM/getpathU - the single exe-path primitive
#include "miniz.h"

// Globals used to store state of last operation
size_t filesz = 0;
size_t uncomp_size = 0;
uint32_t last_crc = 0;

#pragma warning(disable : 4996)

// -----------------------------------------------------------------------------
// Portable shims for the MSVC-only calls this file used to make directly.
//
// aae_fopen: plain fopen works on both, and the 4996 pragma above already
// suppresses MSVC's "consider fopen_s" deprecation for this translation unit,
// so no warning count changes. Returns nullptr on failure, unlike fopen_s's
// errno_t-out-param convention, which is why the call sites below read a
// little differently now.
//
// aae_fseek64 / aae_ftell64: 64-bit file offsets. MSVC spells them
// _fseeki64/_ftelli64; POSIX has fseeko/ftello, which are 64-bit on Linux
// when _FILE_OFFSET_BITS=64 (the default for 64-bit builds). Needed because
// ROM and sample archives can exceed 2 GB.
// -----------------------------------------------------------------------------
static inline FILE* aae_fopen(const char* path, const char* mode)
{
    return std::fopen(path, mode);
}

static inline int aae_fseek64(FILE* f, int64_t off, int origin)
{
#ifdef _WIN32
    return _fseeki64(f, off, origin);
#else
    return fseeko(f, (off_t)off, origin);
#endif
}

static inline int64_t aae_ftell64(FILE* f)
{
#ifdef _WIN32
    return _ftelli64(f);
#else
    return (int64_t)ftello(f);
#endif
}

size_t getLastFileSize() {
    return filesz;
}

size_t getLastZSize() {
    return uncomp_size;
}

uint32_t getLastZCrc() {
    return last_crc;
}

bool DirectoryExists(const char* dirName) {
    // std::filesystem instead of GetFileAttributesA. The error_code overload
    // does not throw, so a missing or inaccessible path reports false exactly
    // as INVALID_FILE_ATTRIBUTES did.
    if (!dirName) return false;
    std::error_code ec;
    return std::filesystem::is_directory(dirName, ec) && !ec;
}

// These two used to wrap GetModuleFileName themselves, duplicating what
// path_helper.cpp already does. Porting the same job twice would have been a
// mistake, so they now delegate to the single platform primitive there
// (GetModuleFileNameW on Windows, /proc/self/exe on Linux).
//
// Note the names are historical and misleading: they return the EXECUTABLE's
// directory, not the process's current working directory.
std::wstring getCurrentDirectoryW() {
    return getpathU(nullptr, nullptr);
}

std::string getCurrentDirectory() {
    return getpathM(nullptr, nullptr);
}

int getFileSize(FILE* input) {
    fseek(input, 0, SEEK_END);
    int size = ftell(input);
    fseek(input, 0, SEEK_SET);
    return size;
}

uint8_t* loadFile(const std::string& filename) {
    return loadFile(filename.c_str());
}

uint8_t* loadFile(const char* filename) {
    filesz = 0;
    FILE* fd = aae_fopen(filename, "rb");
    if (!fd) {
        LOG_INFO("Failed to open file: %s", filename);
        return nullptr;
    }

    filesz = getFileSize(fd);
    uint8_t* buf = static_cast<uint8_t*>(malloc(filesz));
    if (!buf) {
        fclose(fd);
        LOG_INFO("Memory allocation failed: %zu bytes", filesz);
        return nullptr;
    }
    fread(buf, 1, filesz, fd);
    fclose(fd);
    return buf;
}

bool saveFile(const char* filename, const unsigned char* buf, int size) {
    FILE* fd = aae_fopen(filename, "wb");
    if (!fd) {
        LOG_INFO("Failed to save file: %s", filename);
        return false;
    }
    fwrite(buf, size, 1, fd);
    fclose(fd);
    return true;
}

bool fileExistsReadable(const char* filename) {
    FILE* f = aae_fopen(filename, "rb");
    if (!f)
        return false;

    // Optional: Check if file has size, though simple existence is usually enough
    aae_fseek64(f, 0, SEEK_END);
    int64_t file_size = aae_ftell64(f);
    aae_fseek64(f, 0, SEEK_SET);

    fclose(f);
    return (file_size > 0);
}

void replaceExtension(std::string& str, const std::string& rep) {
    size_t pos = str.rfind('.');
    if (pos != std::string::npos)
        str.replace(pos + 1, std::string::npos, rep);
}

std::string removeExtension(const std::string& filename) {
    size_t pos = filename.find_last_of('.');
    if (pos == std::string::npos)
        return filename;
    return filename.substr(0, pos);
}

std::string getBaseName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

unsigned char* loadZip(const char* archname, const char* filename) {
    uncomp_size = 0;
    last_crc = 0;

    mz_zip_archive zip_archive{};
    if (!mz_zip_reader_init_file(&zip_archive, archname, 0)) {
        LOG_INFO("Zip Archive %s not found", archname);
        return nullptr;
    }

    int file_index = mz_zip_reader_locate_file(&zip_archive, filename, nullptr, 0);
    if (file_index < 0) {
        LOG_INFO("File %s not found in archive %s", filename, archname);
        mz_zip_reader_end(&zip_archive);
        return nullptr;
    }

    mz_zip_archive_file_stat file_stat{};
    if (!mz_zip_reader_file_stat(&zip_archive, file_index, &file_stat)) {
        LOG_INFO("Zip file stat failed");
        mz_zip_reader_end(&zip_archive);
        return nullptr;
    }

    uncomp_size = static_cast<size_t>(file_stat.m_uncomp_size);
    last_crc = static_cast<uint32_t>(file_stat.m_crc32);

    unsigned char* buf = (unsigned char*)malloc(uncomp_size);
    if (!buf) {
        LOG_INFO("Memory allocation failed");
        mz_zip_reader_end(&zip_archive);
        return nullptr;
    }

    if (!mz_zip_reader_extract_to_mem(&zip_archive, file_index, buf, uncomp_size, 0)) {
        LOG_INFO("Zip extraction failed");
        free(buf);
        mz_zip_reader_end(&zip_archive);
        return nullptr;
    }

    mz_zip_reader_end(&zip_archive);
    // Logging can be verbose, uncomment if needed
    // LOG_INFO("Zip file %s loaded from archive %s", filename, archname);
    return buf;
}

bool saveZip(const char* archname, const char* filename, const unsigned char* data) {
    if (!mz_zip_add_mem_to_archive_file_in_place(archname, filename, data, strlen((const char*)data) + 1, 0, 0, MZ_BEST_COMPRESSION)) {
        LOG_INFO("Failed to write to zip archive %s", archname);
        return false;
    }
    return true;
}