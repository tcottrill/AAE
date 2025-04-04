#pragma once

// String and misc helper functions.


#include <string>
static void byteswap(unsigned char& byte1, unsigned char& byte2);
std::string remove_extension2(const std::string& path);
std::string remove_extension(const std::string& filename);
std::string getFileName(std::string filePath, bool withExtension, char seperator);
std::string dirnameOf(const std::string& fname);
std::string base_name(const std::string& path);



