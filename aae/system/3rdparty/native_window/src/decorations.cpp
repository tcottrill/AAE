#include "internal.h"
#include "decoration_pixels.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <limits>

namespace nw::detail {
ShmBuffer::~ShmBuffer() {
    if (proxy) wl_buffer_destroy(proxy);
    if (memory && memory != MAP_FAILED) munmap(memory, bytes);
}
Decoration::~Decoration() {
    if (sub) wl_subsurface_destroy(sub);
    if (surface) wl_surface_destroy(surface);
}
int NativeWindow::decorationPart(wl_surface* candidate) const {
    for (int i = 0; i < 4; ++i)
        if (decorations[i] && decorations[i]->surface == candidate) return i;
    return -1;
}
namespace {
std::unique_ptr<ShmBuffer> createBuffer(Core& core, int width, int height,
                                      const std::vector<std::uint32_t>& pixels) {
    if (pixels.empty() || pixels.size() > std::size_t(std::numeric_limits<int>::max()) / 4) return {};
    auto buffer = std::make_unique<ShmBuffer>();
    buffer->bytes = pixels.size() * sizeof(std::uint32_t);
    char name[] = "/tmp/native-window-chrome-XXXXXX";
    const int fd = mkstemp(name);
    if (fd < 0) return {};
    unlink(name);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (ftruncate(fd, static_cast<off_t>(buffer->bytes)) < 0) { close(fd); return {}; }
    buffer->memory = mmap(nullptr, buffer->bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer->memory == MAP_FAILED) { buffer->memory = nullptr; close(fd); return {}; }
    std::memcpy(buffer->memory, pixels.data(), buffer->bytes);
    auto* pool = wl_shm_create_pool(core.shm, fd, int(buffer->bytes));
    close(fd);
    if (!pool) return {};
    buffer->proxy = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    if (!buffer->proxy) return {};
    static const wl_buffer_listener listener = {
        [](void* data, wl_buffer*) { static_cast<ShmBuffer*>(data)->released = true; }
    };
    wl_buffer_add_listener(buffer->proxy, &listener, buffer.get());
    return buffer;
}
}
void NativeWindow::drawDecorations() {
    decorationDirty = true;
    if (!useClientDecoration || !decorated || state.fullscreen) {
        if (shell) xdg_surface_set_window_geometry(shell, 0, 0,
            std::max(1, state.logicalSize.width), std::max(1, state.logicalSize.height));
        for (auto& decoration : decorations) decoration.reset();
        decorationDirty = false;
        return;
    }
    if (!core->alive || !core->shm || !core->subcompositor || !surface) return;
    // Integer-scale shared memory surfaces don't need extra viewport objects.
    // Ceil makes their backing pixels at least as dense as fractional content.
    const int scale = int(std::clamp(std::ceil(state.scale), 1.0, 16.0));
    const int width = state.logicalSize.width, height = state.logicalSize.height;
    if (width <= 0 || height <= 0 || width > 65536 || height > 65536) {
        core->fail(ErrorCode::Invalid, "Decoration dimensions exceed supported bounds"); return;
    }
    if (shell) xdg_surface_set_window_geometry(shell, -4, -30, width + 8, height + 34);
    const int widths[4] = {width + 8, 4, 4, width + 8};
    const int heights[4] = {30, height, height, 4};
    const int xs[4] = {-4, -4, width, -4};
    const int ys[4] = {-30, 0, 0, height};
    for (auto& decoration : decorations) if (decoration) {
        auto& buffers = decoration->buffers;
        buffers.erase(std::remove_if(buffers.begin(), buffers.end(),
            [](const auto& b) { return b->released; }), buffers.end());
        if (buffers.size() >= 3) return; // Retry after buffer release, without unbounded allocation.
    }
    std::unique_ptr<ShmBuffer> next[4];
    for (int i = 0; i < 4; ++i) {
        const int pixelWidth = widths[i] * scale, pixelHeight = heights[i] * scale;
        auto pixels = decorationPixels(i, pixelWidth, pixelHeight, scale,
            state.activated, state.maximized, title);
        next[i] = createBuffer(*core, pixelWidth, pixelHeight, pixels);
        if (!next[i]) { core->fail(ErrorCode::System, "Unable to allocate window decoration buffer"); return; }
    }
    for (int i = 0; i < 4; ++i) {
        if (!decorations[i]) {
            decorations[i] = std::make_unique<Decoration>();
            auto& d = *decorations[i];
            d.surface = wl_compositor_create_surface(core->compositor);
            if (d.surface) d.sub = wl_subcompositor_get_subsurface(core->subcompositor, d.surface, surface);
            if (!d.surface || !d.sub) { core->fail(ErrorCode::System, "Unable to create window decoration surface"); return; }
            wl_subsurface_set_desync(d.sub);
        }
        auto& decoration = *decorations[i];
        wl_subsurface_set_position(decoration.sub, xs[i], ys[i]);
        wl_surface_set_buffer_scale(decoration.surface, scale);
        wl_surface_attach(decoration.surface, next[i]->proxy, 0, 0);
        wl_surface_damage(decoration.surface, 0, 0, widths[i], heights[i]);
        wl_surface_commit(decoration.surface);
        decoration.buffers.push_back(std::move(next[i]));
    }
    decorationDirty = false;
}
}
