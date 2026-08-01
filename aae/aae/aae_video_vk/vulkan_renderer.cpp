// ===========================================================================
// vulkan_renderer.cpp - Vulkan chain stubs (Phase 4a Plan 1).
//
// Every function is a safe no-op. vkchain_init() reports failure so the
// dispatch falls back to GL; none of the other entry points can be reached
// until it returns success (Plan 2).
// ===========================================================================
#include "vulkan_renderer.h"
#include "sys_log.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <vulkan/vulkan.h>

// ===========================================================================
// vkchain_init - Plan 1 smoke check.
// Loads the Vulkan runtime dynamically (no import lib, no SDK install
// needed to build - the loader DLL ships with the GPU driver) and logs the
// instance version. Plan 2 replaces this with the real chain bring-up,
// keeping the same LoadLibrary bootstrap.
// ===========================================================================
int  vkchain_init(void)
{
	HMODULE loader = LoadLibraryA("vulkan-1.dll");
	if (!loader)
	{
		LOG_ERROR("vkchain_init: vulkan-1.dll not found (no Vulkan runtime installed)");
		return 0;
	}

	typedef VkResult (VKAPI_PTR *PFN_EnumVer)(uint32_t*);
	PFN_EnumVer enumVer = (PFN_EnumVer)GetProcAddress(loader, "vkEnumerateInstanceVersion");

	uint32_t v = VK_API_VERSION_1_0;
	if (enumVer)
		enumVer(&v);

	LOG_INFO("Vulkan loader present, instance version %u.%u.%u (headers %u.%u.%u)",
		VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v),
		VK_API_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE),
		VK_API_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE),
		VK_API_VERSION_PATCH(VK_HEADER_VERSION_COMPLETE));

	FreeLibrary(loader);

	LOG_ERROR("vkchain_init: Vulkan chain not implemented yet (Phase 4a Plan 2)");
	return 0;
}
void vkchain_shutdown(void) {}
void vkchain_set_render(void) {}
void vkchain_render(void) {}
void vkchain_swap_buffers(void) {}
void vkchain_set_vsync(bool) {}
void vkchain_on_window_resize(int, int) {}
void vkchain_gui_points_init(int) {}
void vkchain_gui_points_draw(const GuiPointVertex*, int, float) {}
void vkchain_gui_points_shutdown(void) {}
void vkchain_vector_hard_clear(void) {}
void vkchain_init_raster_overlay(void) {}
void vkchain_shutdown_raster_overlay(void) {}
int  vkchain_get_error(void) { return 0; }
