# GL Header Scrub (Backend Abstraction Seam) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove every OpenGL type (`GLuint`/`GLint`/`GLenum`/`GLfloat`/`GLsizei`/`GLboolean`) and GL header include from the public render headers so non-render code (drivers, emulator core, menu, GUI) compiles without ever seeing `glew.h` — the prerequisite seam for the runtime GL-vs-Vulkan backend switch.

**Architecture:** A new backend-neutral header `render_types.h` provides opaque handle typedefs (`rtex_t`, `rprog_t`, `rfbo_t`, `rvao_t`, `rbuf_t` — all `uint32_t`, bit-identical to `GLuint` on MSVC, so implementation bodies compile unchanged). Public headers swap GL types for these and stop including `sys_gl.h`; each render `.cpp` includes `sys_gl.h` itself and becomes the GL backend. The GUI starfield's raw GL calls move behind a small point-sprite API in the renderer. Compile-time `#ifdef __glew_h__ → #error` guards in six non-render translation units become a permanent regression test.

**Tech Stack:** C++ / MSVC 2022, MSBuild, OpenGL 4.2 core (GLEW). No test framework — the tests are the build plus the `__glew_h__` leak guards.

**Key discovery notes (from the 2026-07-11 inventory):**
- No engine-native *enums* are needed: `load_texture` already takes `int filter`; `set_blendmode(GLenum,GLenum)` has **zero call sites** (dead — delete it); `CHECK_FRAMEBUFFER_STATUS()` is only called inside `gl_fbo.cpp` (make it internal). Everything else crossing the boundary is a plain handle.
- `GLuint` == `unsigned int` == `std::uint32_t` on MSVC x64, so `rtex_t*` and `GLuint*` are the same pointer type — definitions in `.cpp` files that keep using GL internally still match the new header signatures.
- Build command used throughout ("the build"):
  `& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" aae\aae.vcxproj /p:Configuration=Release /p:Platform=x64 /v:q /nologo`
  Expected pass state: exit code 0, only the five pre-existing warnings (cpu_i8085.cpp C4101, foodf.cpp C4333, pacman.cpp/phoenix.cpp/gaplus_video.cpp C4018).
- Headers do NOT need vcxproj entries to compile; do not edit the vcxproj (it is tangled with driver WIP).
- If a task's build fails with `'GLuint': undeclared identifier` (or similar) in a render `.cpp`, that file was relying on a transitive `sys_gl.h` include that this task removed — fix by adding `#include "sys_gl.h"` at the top of that `.cpp`'s include block. Expected candidates are listed per task.

---

### Task 1: Create `render_types.h`

**Files:**
- Create: `aae/system/graphics/render_types.h`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
// ====================================================================
// render_types.h - backend-neutral render handle types
//
// Public render headers use these instead of raw GL types so that
// non-render code (drivers, emulator core, menu, GUI) never sees
// glew.h. On the GL backend a handle holds a GL object name (GLuint);
// a Vulkan backend maps handles to its own object tables.
//
// uint32_t is bit-identical to GLuint on MSVC, so GL-backend .cpp
// files pass these straight into GL calls with no casts.
// ====================================================================
#include <cstdint>

using rtex_t  = std::uint32_t;  // texture object
using rprog_t = std::uint32_t;  // shader program
using rfbo_t  = std::uint32_t;  // framebuffer object
using rvao_t  = std::uint32_t;  // vertex array object
using rbuf_t  = std::uint32_t;  // vertex/index buffer object
```

- [ ] **Step 2: Add the GLuint size assertion to the GL backend**

In `aae/system/graphics/sys_gl.cpp`, directly after its include block, add:

```cpp
#include "render_types.h"
static_assert(sizeof(rtex_t) == sizeof(GLuint) && alignof(rtex_t) == alignof(GLuint),
	"render handle types must be bit-identical to GLuint for the GL backend");
```

- [ ] **Step 3: Run the build — expect PASS (nothing uses the header yet beyond the assert)**

- [ ] **Step 4: Commit**

```bash
git add aae/system/graphics/render_types.h aae/system/graphics/sys_gl.cpp
git commit -m "feat(render): add backend-neutral render handle types (render_types.h)"
```

---

### Task 2: Reroute the GUI starfield through the renderer

**Files:**
- Modify: `aae/aae/aae_video/opengl_renderer.h` (add point-sprite API)
- Modify: `aae/aae/aae_video/opengl_renderer.cpp` (implementation)
- Modify: `aae/aae/gui/driver_gui.cpp:133-254, 880-881` (remove all raw GL)

- [ ] **Step 1: Declare the backend-neutral point API in `opengl_renderer.h`**

Add near the other draw declarations:

```cpp
// Backend-neutral point-sprite drawing for the front-end GUI starfield.
// Vertex layout: position (2 floats) + RGBA color (4 floats).
struct GuiPointVertex {
	float x, y;
	float r, g, b, a;
};
void gui_points_init(int maxPoints);
void gui_points_draw(const GuiPointVertex* pts, int count, float pointSize);
void gui_points_shutdown();
```

- [ ] **Step 2: Implement in `opengl_renderer.cpp`**

Add (uses the existing `fragStarPoint` program, `g_proj`, and `config.pointsize` restore — this is the exact GL code lifted from driver_gui.cpp):

```cpp
// ---------------------------------------------------------------------------
// GUI point sprites (starfield). GL lives here so gui code stays GL-free.
// ---------------------------------------------------------------------------
static GLuint s_guiPointVAO = 0;
static GLuint s_guiPointVBO = 0;

void gui_points_init(int maxPoints)
{
	glGenVertexArrays(1, &s_guiPointVAO);
	glGenBuffers(1, &s_guiPointVBO);

	glBindVertexArray(s_guiPointVAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_guiPointVBO);
	glBufferData(GL_ARRAY_BUFFER, maxPoints * sizeof(GuiPointVertex), nullptr, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GuiPointVertex), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GuiPointVertex), (void*)(2 * sizeof(float)));

	glBindVertexArray(0);
}

void gui_points_draw(const GuiPointVertex* pts, int count, float pointSize)
{
	if (count <= 0 || !s_guiPointVAO) return;

	glPointSize(pointSize);
	bind_shader(fragStarPoint);
	set_uniform_mat4f(fragStarPoint, "uProj", aae::math::value_ptr(g_proj));

	glBindVertexArray(s_guiPointVAO);
	glBindBuffer(GL_ARRAY_BUFFER, s_guiPointVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(GuiPointVertex), pts);
	glDrawArrays(GL_POINTS, 0, count);
	glBindVertexArray(0);

	unbind_shader();
	glPointSize(config.pointsize);
}

void gui_points_shutdown()
{
	if (s_guiPointVAO) { glDeleteVertexArrays(1, &s_guiPointVAO); s_guiPointVAO = 0; }
	if (s_guiPointVBO) { glDeleteBuffers(1, &s_guiPointVBO); s_guiPointVBO = 0; }
}
```

(If `opengl_renderer.cpp` does not already include `gl_shader.h` and the math header for `aae::math::value_ptr`, add those includes — both are already used elsewhere in the file, so no change is expected.)

- [ ] **Step 3: Rewrite driver_gui.cpp starfield to use the API**

In `aae/aae/gui/driver_gui.cpp`:

Delete lines 133-139 (`s_starVAO`, `s_starVBO`, `StarVertex`) and replace with nothing — the vertex struct now comes from `opengl_renderer.h`.

Replace `drawStars` (lines 202-235) with:

```cpp
static void drawStars(Star stars[], int count)
{
	static GuiPointVertex buf[256];
	int n = 0;

	for (int i = 0; i < count; ++i)
	{
		if (stars[i].blink && !stars[i].blinkVisible)
			continue;

		buf[n].x = (float)stars[i].x;
		buf[n].y = (float)stars[i].y;
		buf[n].r = stars[i].r / 255.0f;
		buf[n].g = stars[i].g / 255.0f;
		buf[n].b = stars[i].b / 255.0f;
		buf[n].a = 1.0f;
		n++;
	}

	gui_points_draw(buf, n, 3.0f);
}
```

Replace `initStarGPU` (lines 237-254) with:

```cpp
static void initStarGPU()
{
	gui_points_init(kNumStars);
}
```

Replace the teardown at lines 880-881 (`if (s_starVAO) {...}` / `if (s_starVBO) {...}`) with:

```cpp
	gui_points_shutdown();
```

Remove `#include "gl_shader.h"` (line 52) — `bind_shader`/`fragStarPoint` are no longer referenced in this file.

- [ ] **Step 4: Verify no raw GL remains in driver_gui.cpp**

Run: `rg -n "gl[A-Z]|GL_[A-Z]|GLuint|GLfloat" aae/aae/gui/driver_gui.cpp`
Expected: no matches.

- [ ] **Step 5: Run the build — expect PASS. Launch the emulator briefly and confirm the front-end starfield still renders and blinks.**

- [ ] **Step 6: Commit**

```bash
git add aae/aae/aae_video/opengl_renderer.h aae/aae/aae_video/opengl_renderer.cpp aae/aae/gui/driver_gui.cpp
git commit -m "refactor(render): move GUI starfield GL calls behind gui_points_* renderer API"
```

---

### Task 3: Scrub `gl_shader.h`

**Files:**
- Modify: `aae/aae/aae_video/gl_shader.h` (lines 13, 15-22, 25-34)
- Modify: `aae/aae/aae_video/gl_shader.cpp`, `aae/aae/vidhrdwr/emu_vector_draw.cpp`, `aae/aae/vidhrdwr/fast_poly.cpp` (includes only)

- [ ] **Step 1: Edit the header**

Replace `#include "sys_gl.h"` (line 13) with `#include "render_types.h"`.
Change every `GLuint` in lines 15-22 (the eight `extern GLuint frag*` program globals) to `rprog_t`.
Change the signatures:

```cpp
void bind_shader(rprog_t program);
void delete_shader(rprog_t* program);
void set_uniform1i(rprog_t program, const char* name, int value);
// ... same rprog_t swap for set_uniform1f / 2f / 3f / 4f / set_uniform_mat4f
```

(Keep each function's remaining parameters exactly as they are today — only the `GLuint program` parameter type changes.)

- [ ] **Step 2: Match the definitions**

In `gl_shader.cpp`, change the same eight global definitions and function definition signatures from `GLuint` to `rprog_t`. Ensure `gl_shader.cpp` includes `sys_gl.h` directly (add `#include "sys_gl.h"` if it only had it transitively).

- [ ] **Step 3: Run the build.** Expected transitive-include breakage candidates (fix per the note in the header of this plan): `emu_vector_draw.cpp`, `fast_poly.cpp` — both make raw GL calls and previously got GL via `gl_shader.h`. Add `#include "sys_gl.h"` to each if they fail.

- [ ] **Step 4: Commit**

```bash
git add aae/aae/aae_video/gl_shader.h aae/aae/aae_video/gl_shader.cpp aae/aae/vidhrdwr/emu_vector_draw.cpp aae/aae/vidhrdwr/fast_poly.cpp
git commit -m "refactor(render): gl_shader.h public API uses rprog_t, no GL includes"
```

---

### Task 4: Scrub `gl_fbo.h`

**Files:**
- Modify: `aae/aae/aae_video/gl_fbo.h` (lines 52, 58-63, 68-83, 142, 149)
- Modify: `aae/aae/aae_video/gl_fbo.cpp` (lines 102, 275; includes)

- [ ] **Step 1: Edit the header**

Replace `#include "sys_gl.h"` (line 52) with:

```cpp
#include <initializer_list>
#include "render_types.h"
```

Change the six `extern GLuint fbo*` globals (58-63) to `extern rfbo_t ...`.
Change the twelve `extern GLuint img*` globals (68-83) to `extern rtex_t ...`.
Change line 142 to `void fbo_generate_mipmaps(std::initializer_list<rtex_t> textures);`
Delete line 149 (`GLenum CHECK_FRAMEBUFFER_STATUS();`) — it is only called inside gl_fbo.cpp.

- [ ] **Step 2: Edit gl_fbo.cpp**

Add `#include "sys_gl.h"` if not already direct. Change the global definitions to match (`rfbo_t`/`rtex_t`). Change line 275's definition to `std::initializer_list<rtex_t>`. Make `CHECK_FRAMEBUFFER_STATUS` internal: change line 102 to `static GLenum CHECK_FRAMEBUFFER_STATUS()`.

- [ ] **Step 3: Run the build.** Breakage candidate: `opengl_renderer.cpp` (uses the img/fbo globals — types are bit-identical so only missing-GL-include errors are possible; it already includes sys_gl.h via its own header until Task 8, so expect clean).

- [ ] **Step 4: Commit**

```bash
git add aae/aae/aae_video/gl_fbo.h aae/aae/aae_video/gl_fbo.cpp
git commit -m "refactor(render): gl_fbo.h exposes rfbo_t/rtex_t handles, no GL includes"
```

---

### Task 5: Scrub `texture_handler.h`

**Files:**
- Modify: `aae/aae/fileio/texture_handler.h` (lines 16-21, 24, 26, 32, 35)
- Modify: `aae/aae/fileio/texture_handler.cpp` (matching definitions)

- [ ] **Step 1: Edit the header**

Add `#include "render_types.h"` at the top. Change:

```cpp
extern rtex_t error_tex[2];
extern rtex_t pause_tex[2];
extern rtex_t fun_tex[4];
extern rtex_t art_tex[8];
extern rtex_t game_tex[10];
extern rtex_t menu_tex[7];

rtex_t load_texture(const char* filename, const char* archname, int numcomponents, int filter, bool modernGL = true);
void set_texture(rtex_t* texture, bool linear, bool mipmapping, bool blending, bool set_color);
bool get_texture_size(rtex_t tex, int* outW, int* outH);
int make_single_bitmap(rtex_t* texture, const char* filename, const char* archname, int mtype);
```

- [ ] **Step 2: Match the definitions**

Find them: `rg -n "load_texture|set_texture|get_texture_size|make_single_bitmap" aae/aae/fileio/texture_handler.cpp`
Apply the identical signature changes to the definitions (bodies keep using GL freely — same bits). Ensure `texture_handler.cpp` includes `sys_gl.h` directly.

- [ ] **Step 3: Fix GL_TRUE/GL_FALSE at set_texture call sites**

Run: `rg -n "set_texture\(" aae --glob "*.cpp"`
At every call site replace `GL_TRUE` → `true` and `GL_FALSE` → `false` in the argument list (implicit conversion would compile in render files, but non-render callers lose access to the GL macros once headers are clean — change them all now).

- [ ] **Step 4: Run the build — expect PASS.**

- [ ] **Step 5: Commit**

```bash
git add aae/aae/fileio/texture_handler.h aae/aae/fileio/texture_handler.cpp
git add -u
git commit -m "refactor(render): texture_handler.h uses rtex_t + bool, no GL types"
```

---

### Task 6: Scrub the small render-internal headers (`vector_fonts.h`, `texrect.h`, `fast_poly.h`, `sys_texture.h`)

**Files:**
- Modify: `aae/aae/aae_video/vector_fonts.h:101-111`
- Modify: `aae/system/graphics/texrect.h` (lines 5-6, 46-48)
- Modify: `aae/aae/vidhrdwr/fast_poly.h` (lines 35, 66-67)
- Modify: `aae/system/graphics/sys_texture.h` (lines 52, 143, 173)
- Modify: `aae/system/graphics/texrect.cpp`, `aae/system/graphics/sys_texture.cpp`, `aae/aae/aae_video/vector_fonts.cpp` (includes/definitions as below)

- [ ] **Step 1: vector_fonts.h fields** (lines 101-111): `GLuint` → `rprog_t` for `vfProgram`, `rvao_t` for `vfVAO`/`quadVAO`, `rbuf_t` for `vfVBO`/`quadVBO`; `GLint` → `std::int32_t` for `attrPos/attrColor/attrOrigin/attrAngle/uniMVP`. Add `#include "render_types.h"` (it currently gets types via `opengl_renderer.h` — keep that include for now; Task 8 cleans it).

- [ ] **Step 2: texrect.h**: replace includes `sys_gl.h`/`shader_util.h` (lines 5-6) with `#include "render_types.h"` and `#include <cstdint>`. Fields: `rprog_t prog_; rvao_t vao_ = 0; rbuf_t vbo_ = 0; std::int32_t sampler_loc_, uproj_loc_;`. In `texrect.cpp` add `#include "sys_gl.h"` and `#include "shader_util.h"`.

- [ ] **Step 3: fast_poly.h**: replace `#include "sys_gl.h"` (line 35) with `#include "render_types.h"`; fields `rvao_t vao = 0; rbuf_t vbo = 0;` (`fast_poly.cpp` already got `sys_gl.h` directly in Task 3).

- [ ] **Step 4: sys_texture.h**: replace `#include "glew.h"` (line 52) with `#include "render_types.h"`; `rtex_t GetTexID() const;` (line 143), `rtex_t texid = 0;` (line 173). Match the `GetTexID` definition in `sys_texture.cpp` (it includes `sys_gl.h` already).

- [ ] **Step 5: Run the build.** Breakage candidate: `vector_fonts.cpp` (raw GL user) — add `#include "sys_gl.h"` if it errors.

- [ ] **Step 6: Commit**

```bash
git add -u
git commit -m "refactor(render): vector_fonts/texrect/fast_poly/sys_texture headers use neutral handles"
```

---### Task 7: Scrub `mame_layout.h` and `emu_vector_draw.h`

**Files:**
- Modify: `aae/aae/aae_video/mame_layout.h` (lines 33, 58, 116, 123)
- Modify: `aae/aae/aae_video/mame_layout.cpp` (lines 731, 1137; includes)
- Modify: `aae/aae/vidhrdwr/emu_vector_draw.h` (lines 21, 53, 54)
- Modify: `aae/aae/vidhrdwr/emu_vector_draw.cpp` (lines 36, 41-…)

- [ ] **Step 1: mame_layout.h**: replace `#include "framework.h"` (line 33) with `#include "render_types.h"` (the header only needed framework.h for GL types; `<string>/<vector>/<map>` are already included above it). Change `GLuint textureID = 0;` (58) → `rtex_t textureID = 0;`; `Layout_Render(const LayoutView& view, GLuint screenTexture, ...)` (116) → `rtex_t screenTexture`; `GLuint Layout_GetOverlayTexture(...)` (123) → `rtex_t`.

- [ ] **Step 2: mame_layout.cpp**: add `#include "framework.h"` to its include block (it makes raw GL + WindowSetup calls); match the two definition signatures at lines 731 and 1137.

- [ ] **Step 3: emu_vector_draw.h**: replace `#include "sys_gl.h"` (line 21) with `#include "render_types.h"`. Change line 53 to `void set_texture_id(rtex_t* id);`. **Delete** line 54 (`void set_blendmode(GLenum sfactor, GLenum dfactor);`) — verify it is dead first:
`rg -n "set_blendmode" aae` → expected matches ONLY at emu_vector_draw.h:54 and emu_vector_draw.cpp:41.

- [ ] **Step 4: emu_vector_draw.cpp**: change the `set_texture_id` definition (line 36) to `rtex_t*`; delete the whole `set_blendmode` function definition (starts line 41). (`sys_gl.h` include was added in Task 3.) The lone caller `old_mame_vecsim_dvg.cpp:321` passes `&game_tex[0]` which is `rtex_t*` after Task 5 — no change needed.

- [ ] **Step 5: Run the build — expect PASS.**

- [ ] **Step 6: Commit**

```bash
git add -u
git commit -m "refactor(render): mame_layout/emu_vector_draw headers GL-free; drop dead set_blendmode"
```

---

### Task 8: Scrub `opengl_renderer.h`, `vector_draw.h`, `gl_texturing.h`, `sys_gl.h`

**Files:**
- Modify: `aae/aae/aae_video/opengl_renderer.h` (lines 6, 22, 24, 50)
- Modify: `aae/aae/aae_video/opengl_renderer.cpp` (line 297; includes)
- Modify: `aae/aae/aae_video/vector_draw.h` (remove `sys_gl.h` include)
- Modify: `aae/aae/aae_video/gl_texturing.h` (line 6)
- Modify: `aae/system/graphics/sys_gl.h:61`, `aae/system/graphics/sys_gl.cpp:391`
- Modify (include fixes as needed): `aae/aae/aae_video/gl_texturing.cpp`, `aae/system/window/windows_util.cpp`, `aae/aae/aae_video/vector_draw.cpp`

- [ ] **Step 1: opengl_renderer.h**: replace `#include "sys_gl.h"` (line 6) with `#include "render_types.h"` (keep `texrect.h`, now clean). `void set_ortho(int width, int height);` (22), `void set_ortho_raster(int width, int height);` (24), `rtex_t glcode_get_scanrez_tex();` (50).

- [ ] **Step 2: opengl_renderer.cpp**: add `#include "sys_gl.h"` at the top of the include block; match the three definition signatures (`set_ortho`, `set_ortho_raster`, `glcode_get_scanrez_tex` at :297).

- [ ] **Step 3: vector_draw.h**: delete its `#include "sys_gl.h"` (the header body has no GL declarations — verify: `rg -n "GL" aae/aae/aae_video/vector_draw.h` should show only comments). Add `#include "sys_gl.h"` to `vector_draw.cpp` if not direct.

- [ ] **Step 4: gl_texturing.h**: replace `#include "sys_gl.h"` (line 6) with nothing (header declares no GL types); add `#include "sys_gl.h"` to `gl_texturing.cpp`.

- [ ] **Step 5: sys_gl.h/. cpp**: change `float ReSizeGLScene(GLsizei width, GLsizei height);` (h:61) and the definition (cpp:391) to `(int width, int height)`. `sys_gl.h` keeps its `glew.h`/`wglew.h` includes — it IS the GL backend's central header; the point is that public headers no longer include it.

- [ ] **Step 6: Run the build.** Breakage candidates (add `#include "sys_gl.h"`): `gl_texturing.cpp`, `windows_util.cpp`, `vector_draw.cpp`, and any render .cpp that only saw GL through `opengl_renderer.h`.

- [ ] **Step 7: Commit**

```bash
git add -u
git commit -m "refactor(render): opengl_renderer/vector_draw/gl_texturing headers GL-free; sys_gl.h API neutral"
```

---

### Task 9: Leak guards, negative test, final verification

**Files:**
- Modify: `aae/aae/aae_emulator.cpp`, `aae/aae/menu.cpp`, `aae/aae/acommon.cpp`, `aae/aae/gui/driver_gui.cpp`, `aae/aae/drivers/tempest.cpp`, `aae/aae/cpu_code/ccpu.cpp`
- Modify: `CHANGELOG.txt`

- [ ] **Step 1: Add the permanent regression guard to each of the six TUs**, directly after its include block:

```cpp
// Regression guard: this file must never see OpenGL headers. If this fires,
// a render header re-leaked glew.h — fix the header, not this guard.
#ifdef __glew_h__
#error "OpenGL headers leaked into a non-render translation unit"
#endif
```

- [ ] **Step 2: Run the build — expect PASS (all six TUs are now GL-free).** If any guard fires, the error names the TU; trace which included render header still pulls `sys_gl.h`/`glew.h` and fix that header.

- [ ] **Step 3: Negative-test the guard (prove the test can fail)**: temporarily re-add `#include "sys_gl.h"` to `gl_fbo.h`, run the build, and confirm it FAILS with the guard's `#error` in `aae_emulator.cpp`. Revert the temporary include. Run the build again — PASS.

- [ ] **Step 4: Header sweep verification**

Run: `rg -n "GLuint|GLint|GLenum|GLfloat|GLsizei|GLboolean|GLchar" aae/aae/aae_video/*.h aae/system/graphics/*.h aae/aae/fileio/texture_handler.h aae/aae/vidhrdwr/emu_vector_draw.h aae/aae/vidhrdwr/fast_poly.h`
Expected: matches only in comments (and none in `render_types.h`). `sys_gl.h`/`shader_util.h` are exempt — they are the GL backend's own headers, included only by render `.cpp` files.

- [ ] **Step 5: Runtime smoke test**: launch the emulator; check front-end GUI (starfield), one vector game (tempest), one raster game with artwork. Confirm the GL error log stays clean (core-profile context rejects any regression loudly).

- [ ] **Step 6: Update CHANGELOG.txt** with a line describing the seam: "Render headers are now OpenGL-free (backend-neutral handle types); GL confined to renderer .cpp files in preparation for the Vulkan backend."

- [ ] **Step 7: Commit**

```bash
git add -u
git commit -m "refactor(render): glew leak guards in non-render TUs + changelog for GL header scrub"
```

---

## Self-review notes

- **Spec coverage:** opaque handles ✔ (Task 1, applied Tasks 3-8); engine-native enums — investigated and *not needed* (documented in header notes: filter is already `int`, blend API is dead code, format enums never cross the boundary); driver_gui reroute ✔ (Task 2); ~700 implementation-side refs untouched ✔ (only signatures/includes change in .cpp files).
- **Type consistency:** `rprog_t` for programs everywhere (`gl_shader.h`, `vector_fonts.h`, `texrect.h`); `rtex_t` for textures (`gl_fbo.h` imgs, `texture_handler.h`, `mame_layout.h`, `emu_vector_draw.h`, `sys_texture.h`, `opengl_renderer.h`); `rfbo_t` only in `gl_fbo.h`; `GuiPointVertex` defined once in `opengl_renderer.h` and used in `driver_gui.cpp`.
- **Ordering rationale:** driver_gui reroute (Task 2) precedes header scrubs so its raw GL never breaks mid-plan; `opengl_renderer.h` (Task 8) goes last among headers because most render .cpp files lean on its transitive `sys_gl.h`.
