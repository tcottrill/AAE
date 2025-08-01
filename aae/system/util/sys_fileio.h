#pragma once

#ifndef FILEIO_H
#define FILEIO_H

#include <string>
#include <cstdint>

// Get Current Directory
std::wstring getCurrentDirectoryW();
std::string getCurrentDirectory();

// Path Management
bool DirectoryExists(const char* dirName);
bool fileExistsReadable(const char* filename); // No need to be `static` in header

// Get information on last file operation
size_t getLastFileSize();
int getFileSize(FILE* input);

// Get information on last compression operation
size_t getLastZSize();

// Load/Save File
uint8_t* loadFile(const std::string& filename);
uint8_t* loadFile(const char* filename);
bool saveFile(const char* filename, const unsigned char* buf, int size);

// Load/Save Zip
unsigned char* loadZip(const char* archname, const char* filename);
bool saveZip(const char* archname, const char* filename, const unsigned char* data);

// File Manipulation
void replaceExtension(std::string& str, const std::string& rep);
std::string getBaseName(const std::string& path);
std::string removeExtension(const std::string& filename);

#endif
