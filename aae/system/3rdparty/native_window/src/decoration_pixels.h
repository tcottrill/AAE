#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace nw::detail {
// Bounded opaque ARGB pixels for our own client decorations. No window system,
// renderer, FreeType, libpng, or application UI dependency.
std::vector<std::uint32_t> decorationPixels(int part, int width, int height,
    double scale, bool active, bool maximized, const std::string& title);
}
