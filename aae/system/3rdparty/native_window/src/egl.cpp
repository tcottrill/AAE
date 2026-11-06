#include <native_window/egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <wayland-egl.h>
#include <cstdio>
#include <utility>
#include <map>
#include <set>
namespace nw {
bool validGlRequirements(const GlRequirements& r) {
    return r.major >= 1 && r.major <= 4 && r.minor >= 0 && r.minor <= 9 &&
        (!r.coreProfile || r.major > 3 || (r.major == 3 && r.minor >= 2));
}
namespace {
// All operations are confined to the window owner thread. Multiple bindings
// share one initialized EGLDisplay; eglInitialize itself is not refcounted.
struct DisplaySession {
    EGLDisplay display;
    ~DisplaySession() { eglTerminate(display); }
};
thread_local std::map<EGLDisplay, std::weak_ptr<DisplaySession>> sessions;
thread_local std::set<Window*> boundWindows;
bool fail(Error& e, const char* operation) {
    char message[160];
    std::snprintf(message, sizeof(message), "%s (EGL error 0x%x)", operation, eglGetError());
    e = {ErrorCode::System, message}; return false;
}
}
struct EglBinding::Impl {
    Window* window = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    wl_egl_window* drawable = nullptr;
    Size size;
    std::shared_ptr<DisplaySession> session;
    ~Impl() {
        if (display != EGL_NO_DISPLAY) {
            if (context != EGL_NO_CONTEXT && eglGetCurrentContext() == context)
                eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
            if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        }
        if (drawable) wl_egl_window_destroy(drawable);
        if (window) boundWindows.erase(window);
    }
    bool resize(Error& e) {
        const auto s = window->state();
        if (!s.configured || s.framebufferSize.width <= 0 || s.framebufferSize.height <= 0) {
            e = {ErrorCode::Invalid, "Window has no configured framebuffer"}; return false;
        }
        if (size.width != s.framebufferSize.width || size.height != s.framebufferSize.height) {
            size = s.framebufferSize;
            wl_egl_window_resize(drawable, size.width, size.height, 0, 0);
        }
        return true;
    }
};
EglBinding::EglBinding(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}
EglBinding::~EglBinding() = default;
std::unique_ptr<EglBinding> EglBinding::create(Window* w, const GlRequirements& r, Error& e) {
    e = {};
    if (!w || !validGlRequirements(r) || !w->nativeDisplay() || !w->nativeSurface() || !w->state().configured) {
        e = {ErrorCode::Invalid, "EGL requires a configured live window and valid desktop GL requirements"}; return {};
    }
    if (boundWindows.count(w)) { e = {ErrorCode::Invalid, "Window already has an EGL binding"}; return {}; }
    for (auto it = sessions.begin(); it != sessions.end();) {
        if(it->second.expired()) it = sessions.erase(it); else ++it;
    }
    auto p = std::make_unique<Impl>(); p->window = w;
    boundWindows.insert(w);
    // Explicit platform selection avoids EGL_DEFAULT_DISPLAY/X11 probing.
    auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!getDisplay) { e = {ErrorCode::Unsupported, "EGL_EXT_platform_wayland unavailable"}; return {}; }
    p->display = getDisplay(EGL_PLATFORM_WAYLAND_EXT, w->nativeDisplay(), nullptr);
    if (p->display == EGL_NO_DISPLAY) { fail(e, "Get Wayland EGL display"); return {}; }
    p->session = sessions[p->display].lock();
    if (!p->session) {
        EGLint major, minor;
        if (!eglInitialize(p->display, &major, &minor)) { fail(e, "Initialize EGL"); return {}; }
        p->session = std::make_shared<DisplaySession>(); p->session->display = p->display;
        sessions[p->display] = p->session;
    }
    if (!eglBindAPI(EGL_OPENGL_API)) { fail(e, "Bind desktop OpenGL"); return {}; }
    EGLint attributes[] = {EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config; EGLint count = 0;
    if (!eglChooseConfig(p->display, attributes, &config, 1, &count) || !count) { fail(e, "Choose OpenGL EGL configuration"); return {}; }
    EGLint ctx[] = {EGL_CONTEXT_MAJOR_VERSION_KHR,r.major,EGL_CONTEXT_MINOR_VERSION_KHR,r.minor,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,r.coreProfile ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR : EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR,EGL_NONE};
    if (r.major < 3 || (r.major == 3 && r.minor < 2)) ctx[4] = EGL_NONE;
    p->context = eglCreateContext(p->display, config, EGL_NO_CONTEXT, ctx);
    if (p->context == EGL_NO_CONTEXT) { fail(e, "Create requested OpenGL context"); return {}; }
    p->size = w->state().framebufferSize;
    p->drawable = wl_egl_window_create(static_cast<wl_surface*>(w->nativeSurface()), p->size.width, p->size.height);
    if (!p->drawable) { e = {ErrorCode::System, "Create Wayland EGL drawable failed"}; return {}; }
    p->surface = eglCreateWindowSurface(p->display, config, reinterpret_cast<EGLNativeWindowType>(p->drawable), nullptr);
    if (p->surface == EGL_NO_SURFACE) { fail(e, "Create EGL window surface"); return {}; }
    auto binding = std::unique_ptr<EglBinding>(new EglBinding(std::move(p)));
    if (!binding->makeCurrent(e)) return {};
    auto getString = reinterpret_cast<PFNGLGETSTRINGPROC>(binding->getProcAddress("glGetString"));
    int actualMajor = 0, actualMinor = 0;
    const char* version = getString ? reinterpret_cast<const char*>(getString(GL_VERSION)) : nullptr;
    if (!version || std::sscanf(version, "%d.%d", &actualMajor, &actualMinor) != 2 ||
        actualMajor < r.major || (actualMajor == r.major && actualMinor < r.minor)) {
        e = {ErrorCode::Unsupported, "Driver did not provide requested desktop OpenGL version"}; return {};
    }
    if (r.major > 3 || (r.major == 3 && r.minor >= 2)) {
        auto getInteger = reinterpret_cast<PFNGLGETINTEGERVPROC>(binding->getProcAddress("glGetIntegerv"));
        GLint profile = 0; if (getInteger) getInteger(GL_CONTEXT_PROFILE_MASK, &profile);
        const GLint wanted = r.coreProfile ? GL_CONTEXT_CORE_PROFILE_BIT : GL_CONTEXT_COMPATIBILITY_PROFILE_BIT;
        if (!(profile & wanted)) { e = {ErrorCode::Unsupported, "Driver did not provide requested OpenGL profile"}; return {}; }
    }
    if (!eglSwapInterval(binding->impl_->display, 1)) { fail(e, "Enable EGL vsync"); return {}; }
    return binding;
}
bool EglBinding::makeCurrent(Error& e) {
    e = {}; if (!impl_->resize(e)) return false;
    return eglMakeCurrent(impl_->display, impl_->surface, impl_->surface, impl_->context) ? true : fail(e,"Make EGL context current");
}
bool EglBinding::present(Error& e) {
    e = {}; if (!impl_->resize(e)) return false;
    return eglSwapBuffers(impl_->display, impl_->surface) ? true : fail(e,"Present EGL buffer");
}
void* EglBinding::getProcAddress(const char* name) const {
    return name && *name ? reinterpret_cast<void*>(eglGetProcAddress(name)) : nullptr;
}
}
