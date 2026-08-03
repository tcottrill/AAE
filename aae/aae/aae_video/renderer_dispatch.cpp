// ===========================================================================
// renderer_dispatch.cpp - Phase 4 renderer dispatch (spec sec.3.2).
//
// Defines the public renderer entry points the emulator core, GUI and
// window layer have always called, and routes each to the GL chain
// (glchain_*) or the Vulkan chain (vkchain_*) based on config.renderer.
//
// The Vulkan-vs-GL decision is latched ONCE per session (process lifetime),
// not once per init_gl() call: run_game() calls init_gl() on every game
// load, so the first failed Vulkan attempt sets s_vulkanFailed and every
// later call goes straight to GL - no repeat vkchain_init() attempts, no
// repeat failure popup. We never touch the ini (spec sec.5). s_active is the
// single source of truth for which chain is live.
// ===========================================================================
#include "config.h"
#include "sys_log.h"
#include "opengl_renderer.h"
#include "sys_gl.h"
#include "../aae_video_vk/vulkan_renderer.h"
#include "texture_handler.h"   // snapshot() declaration + the shared PNG writer

void allegro_message(const char* title, const char* message);

static int s_active = RENDERER_OPENGL;

// Set once, the first time vkchain_init() fails, so later init_gl() calls
// (one per game load) do not retry Vulkan or re-show the fallback popup.
static int s_vulkanFailed = 0;

// Which chain actually runs this session (post-fallback). For future
// consumers (artwork loaders, snapshot path) - not part of the GL surface.
int active_renderer(void) { return s_active; }

int init_gl(void)
{
	s_active = config.renderer;
	if (s_active == RENDERER_VULKAN)
	{
		if (s_vulkanFailed)
		{
			// Already failed once this session - go straight to GL, silently.
			s_active = RENDERER_OPENGL;
		}
		else if (vkchain_init())
		{
			LOG_INFO("Renderer: Vulkan");
			return 1;
		}
		else
		{
			s_vulkanFailed = 1;
			LOG_ERROR("Vulkan init failed; falling back to OpenGL for this session");
			allegro_message("AAE",
				"Vulkan initialization failed.\n"
				"Falling back to OpenGL for this session.\n"
				"See the log for details.");
			s_active = RENDERER_OPENGL;
		}
	}
	LOG_INFO("Renderer: OpenGL");
	return glchain_init();
}

void end_gl()
{
	if (s_active == RENDERER_VULKAN) { vkchain_shutdown(); return; }
	glchain_end();
}

void set_render()
{
	if (s_active == RENDERER_VULKAN) { vkchain_set_render(); return; }
	glchain_set_render();
}

void render()
{
	if (s_active == RENDERER_VULKAN) { vkchain_render(); return; }
	glchain_render();
}

void GLSwapBuffers()
{
	if (s_active == RENDERER_VULKAN) { vkchain_swap_buffers(); return; }
	glchain_swap_buffers();
}

void SetvSync(bool enabled)
{
	if (s_active == RENDERER_VULKAN) { vkchain_set_vsync(enabled); return; }
	glchain_set_vsync(enabled);
}

void emulator_on_window_resize(int newW, int newH)
{
	if (s_active == RENDERER_VULKAN) { vkchain_on_window_resize(newW, newH); return; }
	glchain_on_window_resize(newW, newH);
}

void gui_points_init(int maxPoints)
{
	if (s_active == RENDERER_VULKAN) { vkchain_gui_points_init(maxPoints); return; }
	glchain_gui_points_init(maxPoints);
}

void gui_points_draw(const GuiPointVertex* pts, int count, float pointSize)
{
	if (s_active == RENDERER_VULKAN) { vkchain_gui_points_draw(pts, count, pointSize); return; }
	glchain_gui_points_draw(pts, count, pointSize);
}

void gui_points_shutdown()
{
	if (s_active == RENDERER_VULKAN) { vkchain_gui_points_shutdown(); return; }
	glchain_gui_points_shutdown();
}

void present_blank_frame()
{
	if (s_active == RENDERER_VULKAN) { vkchain_present_blank_frame(); return; }
	glchain_present_blank_frame();
}

void glcode_vector_hard_clear_fbo1()
{
	if (s_active == RENDERER_VULKAN) { vkchain_vector_hard_clear(); return; }
	glchain_vector_hard_clear_fbo1();
}

void init_raster_overlay()
{
	if (s_active == RENDERER_VULKAN) { vkchain_init_raster_overlay(); return; }
	glchain_init_raster_overlay();
}

void shutdown_raster_overlay()
{
	if (s_active == RENDERER_VULKAN) { vkchain_shutdown_raster_overlay(); return; }
	glchain_shutdown_raster_overlay();
}

int glcode_get_gl_error()
{
	if (s_active == RENDERER_VULKAN) return vkchain_get_error();
	return glchain_get_gl_error();
}

// F12 screenshot. Both chains end at the SAME writer
// (snapshot_write_rgba8_png, fileio/texture_handler.cpp), so the filename,
// the snap/ directory and the PNG encoding are identical between backends;
// only the pixel source differs.
//
// The GL side reads the default framebuffer synchronously right here. The
// Vulkan side cannot: this runs from the emulator's input handling mid-tick,
// with the frame's dynamic-rendering pass open (or with no frame open at all
// when the swapchain is deferred), and glReadPixels would resolve through a
// NULL GLEW pointer because no GL context exists. So the VK entry point only
// LATCHES the request; it is serviced at the next VK_EndFrame, which captures
// the frame the user actually saw.
void snapshot()
{
	if (s_active == RENDERER_VULKAN) { vkchain_request_snapshot(); return; }
	glchain_snapshot();
}
