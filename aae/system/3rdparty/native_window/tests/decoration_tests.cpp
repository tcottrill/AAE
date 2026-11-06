#include "decoration_pixels.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
static void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    using namespace nw::detail;
    check(decorationPixels(0, 0, 30, 1, true, false, "title").empty(), "zero width rejected");
    check(decorationPixels(0, 100000, 100000, 1, true, false, "title").empty(), "oversized allocation rejected");
    check(decorationPixels(0, 100, 30, 0, true, false, "title").empty(), "zero scale rejected");
    auto normal = decorationPixels(0, 810, 38, 1.25, true, false, "Video player");
    check(normal.size() == 810 * 38, "fractional decoration extent retained");
    check(std::all_of(normal.begin(), normal.end(), [](auto p) { return (p >> 24) == 255; }), "all pixels initialized and opaque");
    auto inactive = decorationPixels(0, 810, 38, 1.25, false, false, "Video player");
    check(normal.front() != inactive.front(), "activation changes title bar background");
    auto tiny = decorationPixels(0, 1, 1, 1, true, false, "long title");
    check(tiny.size() == 1, "tiny buffers clip controls and glyphs safely");
    auto border = decorationPixels(1, 6, 720, 1.5, true, false, "ignored");
    check(border.size() == 4320, "border size");
    std::cout << "decoration pixel checks passed\n";
}
