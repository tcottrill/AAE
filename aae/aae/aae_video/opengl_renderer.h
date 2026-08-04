#ifndef GLCODE_H
#define GLCODE_H

#include "texrect.h"
#include "render_types.h"

// Sane Global Rectangle Coordinates
extern int game_rect_left;
extern int game_rect_right;
extern int game_rect_bottom;
extern int game_rect_top;

// Current projection, mirrored from set_ortho*/set_ortho_raster so the core-profile
// quad shaders can read it as a uniform (replaces the fixed-function GL_PROJECTION
// matrix). Forward-declared to avoid pulling MathUtils into every includer.
namespace aae { namespace math { struct mat4; } }
extern aae::math::mat4 g_proj;

void set_ortho(int width, int height);
// Y-down ortho for the raster rendering path (origin top-left, Y increases downward).
void set_ortho_raster(int width, int height);
void set_render();
void render();
void final_render(int left, int right, int bottom, int top);
// Raster-specific composite and present function.
void final_render_raster();
// True when the mono monitor CRT effect will run for the given
// video_attributes (enabled in config AND the game is B/W raster).
bool mono_monitor_active(int vattr);
bool color_monitor_active(int vattr);
void set_render_fbo4();
void end_render_fbo4();
// Draw pause/menu/exit confirm overlays on top of the current backbuffer.
// Called by both vector and raster rendering paths.
// winW/winH are the current window client dimensions.
// fboSpace: force the 1024x1024 FBO coordinate space (set_ortho(1024,1024))
// regardless of game type. Used by the raster path when compositing the overlay
// into fbo4 for a system-rotated game, so screen_rect can rotate the blit.
void render_ui_overlays(int winW, int winH, bool fboSpace = false);
void glcode_vector_hard_clear_fbo1();
int init_gl(void);
void end_gl();
void emulator_on_window_resize(int newW, int newH);
void Widescreen_calc();
void init_raster_overlay();
void shutdown_raster_overlay();
// Returns the scanline overlay texture handle (0 if not loaded)
rtex_t glcode_get_scanrez_tex();

// Backend-neutral point-sprite drawing for the front-end GUI starfield.
// Vertex layout: position (2 floats) + RGBA color (4 floats).
struct GuiPointVertex {
	float x, y;
	float r, g, b, a;
};
void gui_points_init(int maxPoints);
void gui_points_draw(const GuiPointVertex* pts, int count, float pointSize);
void gui_points_shutdown();
// Clear the window to black and present it. Used before a long game load so
// the previous frame does not sit frozen on screen throughout.
void present_blank_frame();

// Backend window-swap / vsync wrappers. Public names are dispatch functions
// defined in renderer_dispatch.cpp; the GL implementations are glchain_swap_buffers()
// / glchain_set_vsync() in sys_gl.cpp (see sys_gl.h).
void GLSwapBuffers();
void SetvSync(bool enabled);

// Backend-neutral GL error check (returns 0 when no error). Lets non-render
// TUs poll for GL errors without including GL headers.
int glcode_get_gl_error();

// ---------------------------------------------------------------------------
// GL chain implementations (renamed from the public names; the public names
// are now defined by renderer_dispatch.cpp and route on config.renderer).
// ---------------------------------------------------------------------------
int  glchain_init(void);
void glchain_end();
void glchain_set_render();
void glchain_render();
void glchain_on_window_resize(int newW, int newH);
void glchain_gui_points_init(int maxPoints);
void glchain_gui_points_draw(const GuiPointVertex* pts, int count, float pointSize);
void glchain_gui_points_shutdown();
void glchain_present_blank_frame();
void glchain_vector_hard_clear_fbo1();
void glchain_init_raster_overlay();
void glchain_shutdown_raster_overlay();
int  glchain_get_gl_error();
// GL pixel source for the F12 screenshot: glReadPixels the default
// framebuffer, flip it top-row-first, hand it to the shared writer
// (snapshot_write_rgba8_png). Defined in fileio/texture_handler.cpp next to
// the writer; routed from snapshot() in renderer_dispatch.cpp.
void glchain_snapshot();

// Which chain is actually live this session (RENDERER_OPENGL / RENDERER_VULKAN
// from config.h), post Vulkan-fallback. Defined in renderer_dispatch.cpp. No
// callers yet; for future consumers (artwork loader, snapshot path in Plans 2-6).
int  active_renderer(void);

#endif