//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
#pragma once
// ===========================================================================
// vulkan_renderer.h - Vulkan chain orchestration entry points (vkchain_*).
//
// The Vulkan twin of the GL chain in aae_video/opengl_renderer.cpp; which one
// runs is picked by renderer_dispatch. vkchain_init() returning 0 makes the
// dispatcher fall back to the GL chain with a popup.
// ===========================================================================

#include "colordefs.h"    // rgb_t (GL-free header, see vector_draw.h)

struct GuiPointVertex;   // defined in aae_video/opengl_renderer.h

int  vkchain_init(void);                 // 1 = chain is up, 0 = failed
void vkchain_shutdown(void);
void vkchain_set_render(void);           // maps to VK_BeginFrame
void vkchain_render(void);               // record + composite
void vkchain_swap_buffers(void);         // maps to VK_EndFrame (submit + present)
void vkchain_set_vsync(bool enabled);
void vkchain_on_window_resize(int newW, int newH);
void vkchain_gui_points_init(int maxPoints);
void vkchain_gui_points_draw(const GuiPointVertex* pts, int count, float pointSize);
void vkchain_gui_points_shutdown(void);
void vkchain_present_blank_frame(void);
void vkchain_vector_hard_clear(void);

// F12 screenshot (routed from snapshot() in renderer_dispatch.cpp). Latches a
// request only - the swapchain readback runs at the next vkchain_swap_buffers,
// because this is called mid-tick from the emulator input path with the
// frame's render pass open. See aae_video_vk/snapshot_vk.h.
void vkchain_request_snapshot(void);

// Draws a solid-color, alpha-blended rect in GUI-local space
// (VectorFont's Initialize(1024,768) coordinate frame - x centered, y-up)
// through ScreenQuadVK::RecordRect + a 1x1 white texture. Used by
// VectorFont::DrawQuad's Vulkan seam (vector_fonts.cpp) for the GUI's
// solid-color background/highlight rects (VF's own GL quad path is
// GL-direct and invisible under Vulkan). x,y is the rect CENTER, matching
// VectorFont::DrawQuad's own parameter convention.
void vkchain_gui_draw_quad(float x, float y, float width, float height, rgb_t color);

// True only while render_ui_overlays() is being replayed for the in-game
// overlay pass (menu / PAUSED / exit dialog / FPS).
//
// It is the discriminator between the two GUI-space coordinate regimes, and
// both VF seams need it:
//   - overlay pass  -> the DEFAULT 0..1024 box, which carries no per-game
//     flip, so the Y-down authored content must be mirrored at emission.
//   - GUI front-end -> the [gui] rect from video.ini, whose INVERTED
//     bottom/top IS the flip (GL relies on exactly the same thing), so
//     mirroring again would flip it twice and stand the menu on its head.
bool vkchain_ui_overlay_active(void);

// In-game UI overlays: black dim rect over the overlay
// letterbox box (pause dim / exit-confirm dim), alpha 0..255. Called from the
// shared render_ui_overlays() (opengl_renderer.cpp) in place of its GL
// quad_from_center when the Vulkan chain is active. The GL dim covers fbo4's
// full 1024 space (letterboxed to the window); this covers the same on-screen
// box directly.
void vkchain_ui_dim_quad(int alpha);
void vkchain_init_raster_overlay(void);
void vkchain_shutdown_raster_overlay(void);
int  vkchain_get_error(void);

// Vector artwork: the VK mirror of run_game Step 6's load_artwork
// call. Frees the previous game's artwork VkTextures (device drain inside -
// call between games only, never mid-frame) and loads the driver's ART_TEX
// table entries with the same search order / art_loaded[] / config-flag /
// menu-flag bookkeeping as the GL loader (see VkArt_LoadForGame).
struct artworks;   // aae_mame_driver.h
void vkchain_load_artwork(const struct artworks* p);

// Raster artwork: the VK mirror of run_game Step 7's
// Layout_LoadForGame call. Parses the driver's MAME .lay layout through the
// SHARED mame_layout.cpp parser and uploads its element textures as
// VkTextures. Device drain inside - call between games only, never mid-frame.
struct AAEDriver;  // aae_mame_driver.h
void vkchain_load_layout(const struct AAEDriver* drv);
