// -----------------------------------------------------------------------------
// vk_texture_loader.h - Minimal Vulkan texture loader for the front-end GUI
// (Phase 4a Plan 6, Task 1 item 2 - trimmed from the deferred Plan 4 Task 4
// loader down to what the GUI needs).
//
// Loads an image (filesystem or zip-archived, via stb_image - already
// vendored under system/3rdparty and linked from sys_texture.cpp's
// STB_IMAGE_IMPLEMENTATION TU) and uploads it through sys_vk's UNORM texture
// builder. UNORM, not SRGB: GUI art bytes pass straight through to the
// swapchain, matching the single sRGB encode-on-store the raster/vector VK
// chains already rely on (see vulkan_renderer.cpp's format trace comment on
// EnsureRasterRenderer).
//
// Investigation finding (Plan 6 Task 1 commit 1): as of this commit, the
// front-end GUI (driver_gui.cpp + menu.cpp) has NO live textured-quad draws
// to seam - texture_handler.h's game_tex[]/menu_tex[]/fun_tex[] are declared
// but unused ("menu_tex // Now unused." in texture_handler.cpp), and neither
// driver_gui.cpp nor menu.cpp call load_texture/set_texture or any textured
// DrawQuad. So VkTex_Load/VkTex_LoadCached below are infrastructure with no
// current caller - built per the plan item, ready for whenever a textured
// menu/marquee draw lands. VkTex_GetSolidWhite IS used now, by
// VectorFont::DrawQuad's Vulkan seam (vector_fonts.cpp), which draws solid
// GUI background/highlight rects through ScreenQuadVK::RecordRect with a
// 1x1 white texture tinted by the caller's color.
//
// ASCII-only comments.
// -----------------------------------------------------------------------------

#pragma once
#ifndef VK_TEXTURE_LOADER_H
#define VK_TEXTURE_LOADER_H

#include "sys_vk.h"   // VkContext, VkTexture

// Loads 'filename' (from the zip archive 'archname' if non-null, else the
// filesystem) into a freshly created VkTexture via stb_image + sys_vk's
// UNORM builder. Returns false (outTex left untouched) on any failure (file
// missing, decode failure, GPU upload failure); logs via LOG_ERROR. Caller
// owns outTex and must VK_DestroyTexture it eventually.
bool VkTex_Load(VkContext& ctx, const char* filename, const char* archname,
                 bool flipY, bool generateMips, VkTexture& outTex);

// Filename+archive keyed cache: loads once, returns the same VkTexture* on
// repeat calls with the same (filename, archname) pair. Owned internally -
// callers must not VK_DestroyTexture the returned pointer; it is cleaned up
// by VkTex_ShutdownCache. Returns nullptr on load failure.
VkTexture* VkTex_LoadCached(VkContext& ctx, const char* filename, const char* archname,
                             bool flipY, bool generateMips);

// Lazily-created 1x1 opaque-white texture, used to draw solid-color rects
// through ScreenQuadVK::RecordRect (the tint parameter supplies the color).
// Returns nullptr on GPU upload failure.
VkTexture* VkTex_GetSolidWhite(VkContext& ctx);

// Destroys every texture VkTex_LoadCached / VkTex_GetSolidWhite created.
// Call once from vkchain_shutdown, before VK_Shutdown tears down the device.
void VkTex_ShutdownCache(VkContext& ctx);

#endif // VK_TEXTURE_LOADER_H
