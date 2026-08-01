#pragma once
// ===========================================================================
// vulkan_renderer.h - Vulkan chain orchestration entry points (vkchain_*).
//
// Phase 4a Plan 1 ships these as stubs: vkchain_init() returns 0, which makes
// renderer_dispatch fall back to the GL chain with a popup (spec sec.5). Plan 2
// brings the real chain online behind exactly these signatures.
// ===========================================================================

struct GuiPointVertex;   // defined in aae_video/opengl_renderer.h

int  vkchain_init(void);                 // 1 = chain is up, 0 = failed
void vkchain_shutdown(void);
void vkchain_set_render(void);           // maps to VK_BeginFrame (spec sec.3.4)
void vkchain_render(void);               // record + composite
void vkchain_swap_buffers(void);         // maps to VK_EndFrame (submit + present)
void vkchain_set_vsync(bool enabled);
void vkchain_on_window_resize(int newW, int newH);
void vkchain_gui_points_init(int maxPoints);
void vkchain_gui_points_draw(const GuiPointVertex* pts, int count, float pointSize);
void vkchain_gui_points_shutdown(void);
void vkchain_vector_hard_clear(void);
void vkchain_init_raster_overlay(void);
void vkchain_shutdown_raster_overlay(void);
int  vkchain_get_error(void);
