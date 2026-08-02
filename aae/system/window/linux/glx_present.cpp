//==============================================================================
// glx_present.cpp -- see glx_present.h.
//==============================================================================
#include "linux/glx_present.h"

#include "sys_gl.h"
#include "sys_log.h"

#include <X11/Xlib.h>
#include <GL/glx.h>

// Vulkan surface support (Phase 4b). VK_USE_PLATFORM_XLIB_KHR must be defined
// BEFORE vulkan.h to get VkXlibSurfaceCreateInfoKHR, and vulkan.h must follow
// Xlib.h so its Display/Window typedefs are the real ones. dlfcn for the
// runtime loader - AAE never links libvulkan (spec sec. 6; the Win32 side
// LoadLibrary's vulkan-1.dll for exactly the same reason).
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include <dlfcn.h>

void GlxPresentSurface::Attach(void* display, unsigned long window)
{
	m_display = display;
	m_window  = window;
}

void GlxPresentSurface::SwapBuffers()
{
	// Diagnostic: a window that never presents looks the same as a window that
	// was never created. Log the first few swaps so "is anything drawing?" is
	// answerable from the log alone.
	static int s_swapCount = 0;
	if (s_swapCount < 3) {
		++s_swapCount;
		LOG_INFO("GlxPresentSurface::SwapBuffers #%d", s_swapCount);
	}

	// Routed through sys_gl.cpp rather than calling glXSwapBuffers directly:
	// that file owns the GLX context and the display/window it was made
	// current on, and having two places that think they own the swap is how
	// you end up swapping a drawable the context is not bound to.
	glchain_swap_buffers();
}

void GlxPresentSurface::GetDrawableSize(int* w, int* h) const
{
	if (w) *w = 0;
	if (h) *h = 0;
	if (!m_display || !m_window) return;

	Display* dpy = static_cast<Display*>(m_display);
	Window root;
	int x = 0, y = 0;
	unsigned int width = 0, height = 0, border = 0, depth = 0;

	if (XGetGeometry(dpy, static_cast<Drawable>(m_window), &root,
	                 &x, &y, &width, &height, &border, &depth)) {
		if (w) *w = static_cast<int>(width);
		if (h) *h = static_cast<int>(height);
	}
}

//------------------------------------------------------------------------------
// Vulkan (Phase 4b): the X11 twin of Win32PresentSurface's implementation in
// winmain.cpp. The Raspberry Pi 5 is why this exists - Mesa v3d tops out near
// GL/GLES 3.1, below the GL renderer's "#version 330 core" shaders - while the
// Steam Machine's radeonsi does GL 4.6 and can run either chain.
//
// This class already owns the Display*/Window (Attach(), called by the X11
// window backend), which is exactly what vkCreateXlibSurfaceKHR needs, so the
// surface is created straight from those two handles.
//
// XLIB rather than XCB: the window backend is Xlib and gamescope hosts
// XWayland, so an Xlib surface is what the existing window can supply. A
// Wayland-native surface would need a Wayland window first - not a Phase 4b
// requirement.
//------------------------------------------------------------------------------
const char* const* GlxPresentSurface::RequiredVkInstanceExtensions(uint32_t* count) const
{
	static const char* kExtensions[] = { "VK_KHR_surface", "VK_KHR_xlib_surface" };
	if (count) *count = 2;
	return kExtensions;
}

bool GlxPresentSurface::CreateVkSurface(void* instance, void* outSurface)
{
	if (!instance || !outSurface || !m_display || !m_window)
	{
		LOG_ERROR("GlxPresentSurface::CreateVkSurface: bad args or no window");
		return false;
	}

	// Runtime load, no link: same policy as the Win32 side. ".so.1" is the
	// versioned SONAME every loader ships; the unversioned ".so" is a
	// dev-package symlink, tried second so a machine with only the SDK
	// installed still works.
	void* loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
	if (!loader)
		loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
	if (!loader)
	{
		LOG_ERROR("GlxPresentSurface::CreateVkSurface: libvulkan.so.1 not found (%s)",
		          dlerror() ? dlerror() : "no error text");
		return false;
	}

	PFN_vkGetInstanceProcAddr gipa =
		(PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
	PFN_vkCreateXlibSurfaceKHR createXlibSurface = gipa
		? (PFN_vkCreateXlibSurfaceKHR)gipa((VkInstance)instance, "vkCreateXlibSurfaceKHR")
		: nullptr;

	bool ok = false;
	if (createXlibSurface)
	{
		VkXlibSurfaceCreateInfoKHR sci{ VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
		sci.dpy    = static_cast<Display*>(m_display);
		sci.window = static_cast<Window>(m_window);

		VkResult r = createXlibSurface((VkInstance)instance, &sci, nullptr,
		                               (VkSurfaceKHR*)outSurface);
		ok = (r == VK_SUCCESS);
		if (!ok)
			LOG_ERROR("GlxPresentSurface::CreateVkSurface: vkCreateXlibSurfaceKHR failed (VkResult=%d)", (int)r);
	}
	else
	{
		LOG_ERROR("GlxPresentSurface::CreateVkSurface: vkCreateXlibSurfaceKHR not available "
		          "(driver missing VK_KHR_xlib_surface?)");
	}

	// Refcounted: sys_vk.cpp holds its own handle on the loader for the
	// session, so dropping this reference does not unload the library.
	dlclose(loader);
	return ok;
}
