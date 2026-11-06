#pragma once
#include <algorithm>
#include <climits>
#include <cmath>
#include <set>
#include <string>
#include <vector>
namespace nw::detail {
inline int integerScale(int preferred, const std::vector<int> &entered) {
  if (preferred > 0)
    return preferred;
  int scale = 1;
  for (int value : entered)
    scale = std::max(scale, value);
  return scale;
}
inline int pixelExtent(int logical, unsigned scale120) {
  return int(std::clamp(std::ceil(double(std::max(1, logical)) *
                                  (scale120 ? scale120 : 120u) / 120.0),
                        1.0, double(INT_MAX)));
}
inline int configuredExtent(int proposed, int previous) {
  return proposed > 0 ? proposed : std::max(1, previous);
}
inline int clampWait(long long ms) { return int(std::clamp(ms, 0LL, 1000LL)); }
struct HeldInput {
  std::set<unsigned> keys, buttons;
  auto reset() {
    auto result = std::make_pair(std::move(keys), std::move(buttons));
    keys.clear();
    buttons.clear();
    return result;
  }
};
inline bool validUtf8(const std::string &s) {
  for (std::size_t i = 0; i < s.size();) {
    unsigned c = (unsigned char)s[i++];
    if (!c)
      return false;
    if (c < 128)
      continue;
    unsigned n, value, minimum;
    if (c >= 0xc2 && c <= 0xdf) {
      n = 1;
      value = c & 31;
      minimum = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
      n = 2;
      value = c & 15;
      minimum = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
      n = 3;
      value = c & 7;
      minimum = 65536;
    } else
      return false;
    if (i + n > s.size())
      return false;
    while (n--) {
      unsigned b = (unsigned char)s[i++];
      if ((b & 0xc0) != 0x80)
        return false;
      value = (value << 6) | (b & 63);
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff))
      return false;
  }
  return true;
}
inline bool parseUriList(const std::string &text,
                         std::vector<std::string> &paths) {
  std::vector<std::string> out;
  if (text.find('\0') != std::string::npos)
    return false;
  for (std::size_t start = 0; start < text.size();) {
    auto end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    auto line = text.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty() || line[0] == '#')
      continue;
    if (line.rfind("file://", 0) != 0)
      return false;
    line.erase(0, 7);
    if (line.rfind("localhost/", 0) == 0)
      line.erase(0, 9);
    if (line.empty() || line[0] != '/' ||
        line.find_first_of("?#\r") != std::string::npos)
      return false;
    std::string path;
    auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
      if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
      return -1;
    };
    for (std::size_t i = 0; i < line.size(); ++i) {
      char c = line[i];
      if (c == '%') {
        if (i + 2 >= line.size())
          return false;
        int a = hex(line[i + 1]), b = hex(line[i + 2]);
        if (a < 0 || b < 0)
          return false;
        c = char(a * 16 + b);
        i += 2;
      }
      if (!c)
        return false;
      path += c;
    }
    // Linux filenames are byte strings; do not reject non-UTF8 local paths.
    out.push_back(std::move(path));
  }
  paths = std::move(out);
  return true;
}
} // namespace nw::detail
