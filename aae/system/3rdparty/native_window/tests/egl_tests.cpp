#include <native_window/egl.h>
#include <iostream>
int main() {
    nw::Error error;
    if (nw::EglBinding::create(nullptr, {}, error) || error.code != nw::ErrorCode::Invalid) return 1;
    if (nw::validGlRequirements({3, -1, true}) || nw::validGlRequirements({2, 1, true}) || nw::validGlRequirements({0, 0, false})) return 2;
    if (!nw::validGlRequirements({2, 1, false}) || !nw::validGlRequirements({3, 3, false}) ||
        !nw::validGlRequirements({3, 3, true}) || !nw::validGlRequirements({4, 6, true})) return 3;
    std::cout << "EGL validation and null-window failure passed\n";
}
