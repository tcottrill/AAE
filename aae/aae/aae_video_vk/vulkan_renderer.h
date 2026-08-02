#pragma once
// ===========================================================================
// vulkan_renderer.h - Vulkan chain orchestration entry points (vkchain_*).
//
// Phase 4a Plan 1 ships these as stubs: vkchain_init() returns 0, which makes
// renderer_dispatch fall back to the GL chain with a popup (spec sec.5). Plan 2
// brings the real chain online behind exactly these signatures.
// ===========================================================================

#include "colordefs.h"    // rgb_t (GL-free header, see vector_draw.h)

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

// Plan 6 Task 1: draws a solid-color, alpha-blended rect in GUI-local space
// (VectorFont's Initialize(1024,768) coordinate frame - x centered, y-up)
// through ScreenQuadVK::RecordRect + a 1x1 white texture. Used by
// VectorFont::DrawQuad's Vulkan seam (vector_fonts.cpp) for the GUI's
// solid-color background/highlight rects (VF's own GL quad path is
// GL-direct and invisible under Vulkan). x,y is the rect CENTER, matching
// VectorFont::DrawQuad's own parameter convention.
void vkchain_gui_draw_quad(float x, float y, float width, float height, rgb_t color);
void vkchain_init_raster_overlay(void);
void vkchain_shutdown_raster_overlay(void);
int  vkchain_get_error(void);

// Plan 8 (vector artwork): the VK mirror of run_game Step 6's load_artwork
// call. Frees the previous game's artwork VkTextures (device drain inside -
// call between games only, never mid-frame) and loads the driver's ART_TEX
// table entries with the same search order / art_loaded[] / config-flag /
// menu-flag bookkeeping as the GL loader (see VkArt_LoadForGame).
struct artworks;   // aae_mame_driver.h
void vkchain_load_artwork(const struct artworks* p);
