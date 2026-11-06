#include "decoration_pixels.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace nw::detail {
namespace {
struct Font {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    bool ready = false;
    Font() {
        // Fonts exposed by common Linux runtimes and Flatpak. These are trusted
        // system fonts, not arbitrary fonts supplied by media or clipboard data.
        for (const char* name : {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "/run/host/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/run/host/fonts/dejavu/DejaVuSans.ttf"}) {
            std::ifstream file(name, std::ios::binary | std::ios::ate);
            if (!file) continue;
            const auto length = file.tellg();
            if (length < 12 || length > 32 * 1024 * 1024) continue;
            bytes.resize(static_cast<std::size_t>(length));
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) continue;
            const int offset = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
            if (offset >= 0 && stbtt_InitFont(&info, bytes.data(), offset)) { ready = true; break; }
        }
    }
};
unsigned nextCodepoint(const std::string& text, std::size_t& pos) {
    const unsigned first = static_cast<unsigned char>(text[pos++]);
    if (first < 128) return first;
    unsigned count = first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : 1;
    unsigned value = first & (count == 3 ? 7 : count == 2 ? 15 : 31);
    while (count--) {
        if (pos == text.size()) return 0xfffd;
        const unsigned byte = static_cast<unsigned char>(text[pos++]);
        if ((byte & 0xc0) != 0x80) return 0xfffd;
        value = (value << 6) | (byte & 63);
    }
    return value;
}
}

std::vector<std::uint32_t> decorationPixels(int part, int width, int height,
    double scale, bool active, bool maximized, const std::string& title) {
    constexpr std::size_t maxPixels = 4 * 1024 * 1024;
    if (width <= 0 || height <= 0 || !std::isfinite(scale) || scale <= 0 || scale > 16 ||
        std::size_t(width) > maxPixels / std::size_t(height)) return {};
    const std::uint32_t background = part == 0 ? (active ? 0xff292d33u : 0xff202226u) : 0xff373b40u;
    std::vector<std::uint32_t> pixels(std::size_t(width) * height, background);
    if (part != 0) return pixels;
    auto point = [&](int x, int y, std::uint32_t color) {
        if (x >= 0 && y >= 0 && x < width && y < height) pixels[std::size_t(y) * width + x] = color;
    };
    auto line = [&](int x0, int y0, int x1, int y1) {
        const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
        const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        for (;;) {
            point(x0, y0, 0xffe3e6eau);
            if (x0 == x1 && y0 == y1) break;
            const int twice = 2 * error;
            if (twice >= dy) { error += dy; x0 += sx; }
            if (twice <= dx) { error += dx; y0 += sy; }
        }
    };
    const int unit = std::max(1, int(std::lround(30 * scale)));
    const int radius = std::max(1, int(std::lround(4 * scale)));
    const int cy = height / 2;
    const int close = width - unit / 2;
    line(close - radius, cy - radius, close + radius, cy + radius);
    line(close - radius, cy + radius, close + radius, cy - radius);
    const int maximize = close - unit;
    line(maximize - radius, cy - radius, maximize + radius, cy - radius);
    line(maximize + radius, cy - radius, maximize + radius, cy + radius);
    line(maximize + radius, cy + radius, maximize - radius, cy + radius);
    line(maximize - radius, cy + radius, maximize - radius, cy - radius);
    if (maximized) line(maximize - radius + 2, cy - radius - 2, maximize + radius + 2, cy - radius - 2);
    line(close - 2 * unit - radius, cy + radius, close - 2 * unit + radius, cy + radius);

    thread_local Font font;
    if (!font.ready) return pixels; // Controls remain usable even without a system font.
    const float fontScale = stbtt_ScaleForPixelHeight(&font.info, float(16 * scale));
    int ascent = 0, descent = 0;
    stbtt_GetFontVMetrics(&font.info, &ascent, &descent, nullptr);
    const int baseline = int((height - (ascent - descent) * fontScale) * .5f + ascent * fontScale);
    float pen = float(12 * scale);
    const int clip = std::max(0, width - 3 * unit - int(8 * scale));
    for (std::size_t pos = 0; pos < title.size() && pos < 2048 && pen < clip;) {
        const unsigned codepoint = nextCodepoint(title, pos);
        if (codepoint < 32) continue;
        int gw = 0, gh = 0, gx = 0, gy = 0;
        auto* glyph = stbtt_GetCodepointBitmap(&font.info, 0, fontScale, int(codepoint), &gw, &gh, &gx, &gy);
        if (glyph) {
            for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) {
                const int px = int(pen) + gx + x, py = baseline + gy + y;
                if (px < 0 || px >= clip || py < 0 || py >= height) continue;
                const unsigned alpha = glyph[std::size_t(y) * gw + x];
                const auto destination = pixels[std::size_t(py) * width + px];
                std::uint32_t result = 0xff000000u;
                for (int shift : {0, 8, 16}) {
                    const unsigned channel = (destination >> shift) & 255;
                    result |= ((channel * (255 - alpha) + 230 * alpha + 127) / 255) << shift;
                }
                point(px, py, result);
            }
            stbtt_FreeBitmap(glyph, nullptr);
        }
        int advance = 0;
        stbtt_GetCodepointHMetrics(&font.info, int(codepoint), &advance, nullptr);
        pen += advance * fontScale;
    }
    return pixels;
}
}
