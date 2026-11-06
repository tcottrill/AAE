#include <iostream>
#include <native_window/window.h>
int main() {
  nw::Error error;
  auto context = nw::Context::create({}, error);
  if (!context) {
    std::cerr << error.message << '\n';
    return 1;
  }
  auto window = context->createWindow({}, error);
  if (!window) {
    std::cerr << error.message << '\n';
    return 1;
  }
  // A renderer may now borrow nativeDisplay/nativeSurface and attach buffers.
  // This core-only example intentionally has no renderer or toolkit dependency.
  bool done = false;
  while (!done && context->pump(std::chrono::milliseconds{100}))
    for (const auto &event : context->takeEvents())
      if (event.type == nw::EventType::Close)
        done = true;
}
