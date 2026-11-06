#include "helpers.h"
#include <cstdlib>
#include <iostream>
static void check(bool b) {
  if (!b) {
    std::cerr << "core check failed\n";
    std::exit(1);
  }
}
int main() {
  using namespace nw::detail;
  check(pixelExtent(801, 150) == 1002);
  check(pixelExtent(0, 120) == 1);
  check(pixelExtent(640, 180) == 960);
  check(pixelExtent(100, 96) == 80);
  check(integerScale(0, {1, 2, 3}) == 3);
  check(integerScale(1, {3}) == 1);
  check(integerScale(0, {}) == 1);
  check(configuredExtent(0, 640) == 640);
  check(configuredExtent(-1, 640) == 640);
  check(configuredExtent(1280, 640) == 1280);
  std::vector<std::string> p;
  check(parseUriList(
      "# comment\r\nfile:///tmp/a%20b\r\nfile://localhost/tmp/c\n", p));
  check(p.size() == 2 && p[0] == "/tmp/a b" && p[1] == "/tmp/c");
  check(!parseUriList("file://remote/tmp/a\n", p));
  check(!parseUriList("file:///tmp/%00\n", p));
  check(!parseUriList("file:///tmp/%xz\n", p));
  check(!parseUriList("https://example.com/a\n", p));
  check(!parseUriList("file:///tmp/a?query\n", p));
  check(validUtf8("\xc3\xa9") && !validUtf8("\xc0\x80") &&
        !validUtf8(std::string("a\0b", 3)));
  HeldInput held;
  held.keys.insert(30);
  held.buttons.insert(272);
  auto reset = held.reset();
  check(reset.first.size() == 1 && reset.second.size() == 1);
  check(held.keys.empty() && held.buttons.empty());
  check(held.reset().first.empty());
  check(clampWait(-10) == 0 && clampWait(5000) == 1000);
  std::cout << "native core headless checks passed\n";
}
