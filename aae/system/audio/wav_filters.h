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
#include <cstdint>
#include <vector>


void highPassFilter(std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate);
void lowPassFilter (std::vector<int16_t>& audioSample, float cutoffFreq, float sampleRate);
