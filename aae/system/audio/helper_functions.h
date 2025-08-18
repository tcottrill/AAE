/* =============================================================================
* -------------------------------------------------------------------------- -
*License(GPLv3) :
    *This file is part of GameEngine Alpha.
    *
    *<Project Name> is free software : you can redistribute it and /or modify
    * it under the terms of the GNU General Public License as published by
    * the Free Software Foundation, either version 3 of the License, or
    *(at your option) any later version.
    *
    *<Project Name> is distributed in the hope that it will be useful,
    * but WITHOUT ANY WARRANTY; without even the implied warranty of
    * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
    * GNU General Public License for more details.
    *
    * You should have received a copy of the GNU General Public License
    * along with GameEngine Alpha.If not, see < https://www.gnu.org/licenses/>.
*
*Copyright(C) 2022 - 2025  Tim Cottrill
* SPDX - License - Identifier : GPL - 3.0 - or -later
* ============================================================================ =
*/
#pragma once

// String and misc helper functions.


#include <string>
static void byteswap(unsigned char& byte1, unsigned char& byte2);
std::string remove_extension2(const std::string& path);
std::string remove_extension(const std::string& filename);
std::string getFileName(std::string filePath, bool withExtension, char seperator);
std::string dirnameOf(const std::string& fname);
std::string base_name(const std::string& path);
bool ends_with(const std::string& s, const std::string& ending);



