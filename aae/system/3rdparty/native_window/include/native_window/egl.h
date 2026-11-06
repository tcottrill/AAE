#pragma once
#include "window.h"
namespace nw {
struct GlRequirements { int major = 3, minor = 3; bool coreProfile = true; };
bool validGlRequirements(const GlRequirements&);
class EglBinding {
public:
    // Window/context outlive this binding. Owner thread only. Failures clean up.
    static std::unique_ptr<EglBinding> create(Window*, const GlRequirements&, Error&);
    ~EglBinding();
    EglBinding(const EglBinding&) = delete;
    EglBinding& operator=(const EglBinding&) = delete;
    bool makeCurrent(Error&);
    bool present(Error&); // resizes from observed framebuffer before swap
    void* getProcAddress(const char*) const;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
    explicit EglBinding(std::unique_ptr<Impl>);
};
}
