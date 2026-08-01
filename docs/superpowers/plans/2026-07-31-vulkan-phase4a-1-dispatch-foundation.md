# Vulkan Phase 4a — Plan 1: Renderer Dispatch Foundation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the `renderer=` startup switch and the thin dispatch layer that routes AAE's 14 externally-referenced renderer entry points to the GL chain (unchanged behavior) or the Vulkan chain (stubs that fall back to GL), plus Vulkan SDK and shader-build wiring.

**Architecture:** Approach A from the spec (`docs/superpowers/specs/2026-07-31-vulkan-rendering-chain-design.md` §3): GL implementations get internal `glchain_*` renames; a new `renderer_dispatch.cpp` defines the original public names and routes on `config.renderer`. No call site outside `aae_video/` changes. After this plan, `renderer=vulkan` cleanly falls back to GL with a popup — the Vulkan chain itself arrives in Plan 2.

**Tech Stack:** MSVC v143 / MSBuild x64, Vulkan SDK (`%VULKAN_SDK%`), glslangValidator for shaders. No test framework — house rule: the build plus a runtime pass is the test (spec §9).

**Plan sequence:** This is Plan 1 of 6 for Phase 4a: (1) dispatch foundation ← you are here, (2) Vulkan core online, (3) raster game path, (4) raster post + artwork, (5) vector path, (6) GUI/overlays/snapshots + full matrix.

**Verified anchors** (all checked 2026-07-31):
- `setup_config()` — `aae/aae/config.cpp:26`; ini pattern `get_config_int("main", ...)`.
- Early cmdline flag loop — `aae/aae/aae_emulator.cpp:1657-1672` (`-debug` parsing, runs inside `emulator_init`).
- `init_gl()` call — `aae_emulator.cpp:913`; `render()` — `:1562`; `GLSwapBuffers()` — `:1574`; `SetvSync` — `:1179`.
- `GLSwapBuffers`/`SetvSync` GL definitions — `aae/system/graphics/sys_gl.cpp:443/581` and `:431/553` (two definitions each: Win32 + Linux branches).
- Present-surface callers of `GLSwapBuffers()` — `aae/system/window/winmain.cpp:873`, `aae/system/window/linux/glx_present.cpp:33`.
- `allegro_message(title, msg)` — declared `aae/system/window/windows_util.h:36`, Linux impl `linux_main.cpp:95`.
- Dispatch surface (externally referenced, enumerated by grep excluding `aae_video/`): `init_gl`, `end_gl` (declared, no external caller — dispatched for symmetry), `set_render`, `render`, `GLSwapBuffers`, `SetvSync`, `emulator_on_window_resize`, `gui_points_init`, `gui_points_draw`, `gui_points_shutdown`, `glcode_vector_hard_clear_fbo1`, `init_raster_overlay`, `shutdown_raster_overlay`, `glcode_get_gl_error`.
- Shared globals (`game_rect_*`, `g_scanline_override`, `g_proj`, `vid_scale`) stay defined where they are — both chains read/write the same data; NOT part of the dispatch.
- Donor `fast_poly` VK interface: UBO `set=0 binding=0` holding `mat4`, vertex = `location 0: R32G32_SFLOAT`, `location 1: R8G8B8A8_UNORM` (`C:\Source2026\Bosconian\Bosconian\sys_graphics\fast_poly.cpp:240-373,501-502`). The GLSL sources EXIST at `C:\Source2026\Bosconian\x64\Release\shaders\fast_poly_vk.{vert,frag}` (verified 2026-07-31; only the SpriteTest repo lacks them) — Task 5 copies them, no reconstruction.

**Build command (the "test runner"):**

```bash
MSBuild.exe aae/aae.vcxproj -p:Configuration=Release -p:Platform=x64 -v:q -nologo
```

Pass = exit 0, no new warnings beyond the known five (spec §9). Run from `C:\Source2026\AAE_publish`. x86 configs are known-broken and not a gate.

---

### Task 0: Create the phase branch

**Files:** none (git only)

- [ ] **Step 1: Branch off the current working branch**

```bash
git -C C:/Source2026/AAE_publish checkout -b feature/vulkan-rendering-chain
```

Expected: `Switched to a new branch 'feature/vulkan-rendering-chain'` (branching from `refactor/phase3c-linux-window-input`, which carries the spec commit).

---

### Task 1: `renderer=` config field + cmdline override

**Files:**
- Modify: `aae/aae/config.h` (struct + defines)
- Modify: `aae/aae/config.cpp:26+` (inside `setup_config()`)
- Modify: `aae/aae/aae_emulator.cpp:1657-1672` (early flag loop)

- [ ] **Step 1: Add the field and backend constants to `config.h`**

Near the top of `aae/aae/config.h` (after the include guard, before the struct):

```c
// Renderer backend selection ([main] renderer= in aae.ini, -renderer on the
// command line). Resolved once at startup; no runtime switching (Phase 4 spec §3.1).
#define RENDERER_OPENGL 0
#define RENDERER_VULKAN 1
```

Inside the config struct, next to the `int debug;` field:

```c
	int renderer;         // RENDERER_OPENGL (default) or RENDERER_VULKAN
```

- [ ] **Step 2: Read it in `setup_config()`**

In `aae/aae/config.cpp`, inside `setup_config()` next to the `config.debug` line (~line 132):

```c
	{
		// Value is expected lowercase in the ini, matching house style.
		const char* r = get_config_string("main", "renderer", "opengl");
		config.renderer = (r && strcmp(r, "vulkan") == 0) ? RENDERER_VULKAN : RENDERER_OPENGL;
		LOG_INFO("Config: renderer=%s", (config.renderer == RENDERER_VULKAN) ? "vulkan" : "opengl");
	}
```

First confirm `get_config_string` is already used in this file (it is — `config.cpp:80`) so no new include is needed.

- [ ] **Step 3: Add `-renderer` to the early cmdline loop**

`setup_config()` must already have run when this loop executes so the override wins. Verify: grep the call site of `setup_config` (`grep -rn "setup_config()" aae/ --include=*.cpp`) and confirm it executes before `emulator_init`'s flag loop at `aae_emulator.cpp:1657`. If it runs later, place this parse immediately after the `setup_config()` call instead — same code either way.

In the `-debug` loop body (`aae_emulator.cpp:1657-1672`), add after the `-debug` block:

```cpp
		else if (arg == "-renderer")
		{
			if (i + 1 < argc && argv[i + 1])
			{
				const std::string val = to_lowercase(argv[i + 1]);
				if (val == "vulkan")      config.renderer = RENDERER_VULKAN;
				else if (val == "opengl") config.renderer = RENDERER_OPENGL;
				else LOG_INFO("-renderer: unknown value '%s' (use opengl|vulkan)", argv[i + 1]);
				LOG_INFO("Cmdline: renderer=%s",
					(config.renderer == RENDERER_VULKAN) ? "vulkan" : "opengl");
			}
		}
```

- [ ] **Step 4: Build**

Run the build command. Expected: exit 0.

- [ ] **Step 5: Runtime check**

```bash
cd C:/Source2026/AAE_publish/aae/x64/Release && ./aae.exe -renderer vulkan
```

Exit the GUI. Expected in the log (`systemlog.txt` / console): `Config: renderer=opengl` then `Cmdline: renderer=vulkan`. Nothing else changes — the switch has no consumer yet.

- [ ] **Step 6: Commit**

```bash
git -C C:/Source2026/AAE_publish add aae/aae/config.h aae/aae/config.cpp aae/aae/aae_emulator.cpp
git -C C:/Source2026/AAE_publish commit -m "feat(video): add [main] renderer= config + -renderer cmdline override"
```

---

### Task 2: Rename the GL implementations (`glchain_*`)

**Files:**
- Modify: `aae/aae/aae_video/opengl_renderer.h`
- Modify: `aae/aae/aae_video/opengl_renderer.cpp`
- Modify: `aae/system/graphics/sys_gl.h`
- Modify: `aae/system/graphics/sys_gl.cpp`
- Modify: `aae/system/window/winmain.cpp:873`
- Modify: `aae/system/window/linux/glx_present.cpp:33`

Pure renames — zero behavior change. The public names temporarily have no definition between Task 2 and Task 3; that is fine because they build together before the next build step.

- [ ] **Step 1: Rename the 12 definitions in `opengl_renderer.cpp`**

| Old definition | New name |
|---|---|
| `init_gl` | `glchain_init` |
| `end_gl` | `glchain_end` |
| `set_render` | `glchain_set_render` |
| `render` | `glchain_render` |
| `emulator_on_window_resize` | `glchain_on_window_resize` |
| `gui_points_init` | `glchain_gui_points_init` |
| `gui_points_draw` | `glchain_gui_points_draw` |
| `gui_points_shutdown` | `glchain_gui_points_shutdown` |
| `glcode_vector_hard_clear_fbo1` | `glchain_vector_hard_clear_fbo1` |
| `init_raster_overlay` | `glchain_init_raster_overlay` |
| `shutdown_raster_overlay` | `glchain_shutdown_raster_overlay` |
| `glcode_get_gl_error` | `glchain_get_gl_error` |

Rename ONLY the definitions and any calls to these names *inside* `aae_video/` files (e.g. if `render()` internals or `mame_layout.cpp` call `init_raster_overlay`, update those calls to `glchain_init_raster_overlay`). Do NOT touch `final_render*`, `set_ortho*`, `render_ui_overlays`, `raster_poly_update` or anything not in the table.

- [ ] **Step 2: Declare the renamed functions in `opengl_renderer.h`**

Add below the existing declarations (keep ALL existing declarations — the public names will be defined by the dispatch in Task 3):

```cpp
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
void glchain_vector_hard_clear_fbo1();
void glchain_init_raster_overlay();
void glchain_shutdown_raster_overlay();
int  glchain_get_gl_error();
```

- [ ] **Step 3: Rename `GLSwapBuffers`/`SetvSync` in `sys_gl.cpp`**

Both functions have TWO definitions (Win32 and Linux `#ifdef` branches — lines ~443/581 and ~431/553). Rename all four definitions:

- `GLSwapBuffers` → `glchain_swap_buffers`
- `SetvSync` → `glchain_set_vsync`

In `sys_gl.h`, add next to the existing declarations (again, keep the old declarations):

```cpp
// GL backend implementations; public GLSwapBuffers/SetvSync are dispatch
// functions in renderer_dispatch.cpp as of Phase 4.
void glchain_swap_buffers();
void glchain_set_vsync(bool enabled);
```

- [ ] **Step 4: Point the present surfaces at the GL impl directly**

These two are GL-path presentation code, so they call the GL implementation, not the dispatch:

`aae/system/window/winmain.cpp:873` — inside `Win32PresentSurface::SwapBuffers()`: change `GLSwapBuffers();` to `glchain_swap_buffers();`

`aae/system/window/linux/glx_present.cpp:33` — same change.

- [ ] **Step 5: Do NOT build yet**

The public names now have declarations but no definitions; callers still reference them, so linking would fail. Task 3 restores them. (If you must checkpoint, `MSBuild ... -t:ClCompile` compiles without linking.)

---

### Task 3: The dispatch TU + Vulkan chain stubs with GL fallback

**Files:**
- Create: `aae/aae/aae_video/renderer_dispatch.cpp`
- Create: `aae/aae/aae_video_vk/vulkan_renderer.h`
- Create: `aae/aae/aae_video_vk/vulkan_renderer.cpp`
- Modify: `aae/aae.vcxproj` (+ `aae/aae.vcxproj.filters`) — add the two new .cpp files

- [ ] **Step 1: Create `vulkan_renderer.h`**

```cpp
#pragma once
// ===========================================================================
// vulkan_renderer.h - Vulkan chain orchestration entry points (vkchain_*).
//
// Phase 4a Plan 1 ships these as stubs: vkchain_init() returns 0, which makes
// renderer_dispatch fall back to the GL chain with a popup (spec §5). Plan 2
// brings the real chain online behind exactly these signatures.
// ===========================================================================

struct GuiPointVertex;   // defined in aae_video/opengl_renderer.h

int  vkchain_init(void);                 // 1 = chain is up, 0 = failed
void vkchain_shutdown(void);
void vkchain_set_render(void);           // maps to VK_BeginFrame (spec §3.4)
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
```

- [ ] **Step 2: Create `vulkan_renderer.cpp` (stubs)**

```cpp
// ===========================================================================
// vulkan_renderer.cpp - Vulkan chain stubs (Phase 4a Plan 1).
//
// Every function is a safe no-op. vkchain_init() reports failure so the
// dispatch falls back to GL; none of the other entry points can be reached
// until it returns success (Plan 2).
// ===========================================================================
#include "vulkan_renderer.h"
#include "sys_log.h"

int  vkchain_init(void)
{
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
```

- [ ] **Step 3: Create `renderer_dispatch.cpp`**

```cpp
// ===========================================================================
// renderer_dispatch.cpp - Phase 4 renderer dispatch (spec §3.2).
//
// Defines the public renderer entry points the emulator core, GUI and
// window layer have always called, and routes each to the GL chain
// (glchain_*) or the Vulkan chain (vkchain_*) based on config.renderer.
//
// The decision is made ONCE, inside init_gl(): if Vulkan is requested but
// fails to initialize, we fall back to GL for the session and never touch
// the ini (spec §5). s_active is the single source of truth afterwards.
// ===========================================================================
#include "config.h"
#include "sys_log.h"
#include "opengl_renderer.h"
#include "sys_gl.h"
#include "../aae_video_vk/vulkan_renderer.h"

void allegro_message(const char* title, const char* message);

static int s_active = RENDERER_OPENGL;

// Which chain actually runs this session (post-fallback). For future
// consumers (artwork loaders, snapshot path) — not part of the GL surface.
int active_renderer(void) { return s_active; }

int init_gl(void)
{
	s_active = config.renderer;
	if (s_active == RENDERER_VULKAN)
	{
		if (vkchain_init())
		{
			LOG_INFO("Renderer: Vulkan");
			return 1;
		}
		LOG_ERROR("Vulkan init failed; falling back to OpenGL for this session");
		allegro_message("AAE",
			"Vulkan initialization failed.\n"
			"Falling back to OpenGL for this session.\n"
			"See the log for details.");
		s_active = RENDERER_OPENGL;
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
```

Check the actual signatures in `opengl_renderer.h` before compiling — if any public declaration differs from the above (e.g. `init_gl(void)` vs `init_gl()`, parameter types on `gui_points_draw`), the dispatch definitions must match the HEADER exactly. Add a declaration for `active_renderer()` to `renderer_dispatch`'s consumers later; nothing needs it in Plan 1.

- [ ] **Step 4: Register the new files in the project**

In `aae/aae.vcxproj`, add to the `<ItemGroup>` containing `<ClCompile>` entries (match neighbors' formatting):

```xml
<ClCompile Include="aae\aae_video\renderer_dispatch.cpp" />
<ClCompile Include="aae\aae_video_vk\vulkan_renderer.cpp" />
```

And the header:

```xml
<ClInclude Include="aae\aae_video_vk\vulkan_renderer.h" />
```

Mirror in `aae.vcxproj.filters` next to the other `aae_video` entries. Verify the relative-path convention by looking at how `aae\aae_video\opengl_renderer.cpp` is listed and match it exactly.

- [ ] **Step 5: Build**

Run the build command. Expected: exit 0. Link errors about `glchain_*` mean a Task 2 rename was missed (the error names the exact symbol).

- [ ] **Step 6: Runtime — GL default unchanged**

```bash
cd C:/Source2026/AAE_publish/aae/x64/Release && ./aae.exe asteroid
```

Expected: asteroid boots, plays, sounds correct; log shows `Renderer: OpenGL`. Quit, run `./aae.exe pacman` — same check for the raster path.

- [ ] **Step 7: Runtime — Vulkan requested, falls back**

```bash
cd C:/Source2026/AAE_publish/aae/x64/Release && ./aae.exe asteroid -renderer vulkan
```

Expected: popup "Vulkan initialization failed... Falling back to OpenGL", then asteroid plays on GL. Log shows `vkchain_init: Vulkan chain not implemented yet` then `Renderer: OpenGL`.

- [ ] **Step 8: Commit**

```bash
git -C C:/Source2026/AAE_publish add -A aae/aae/aae_video aae/aae/aae_video_vk aae/system/graphics/sys_gl.h aae/system/graphics/sys_gl.cpp aae/system/window/winmain.cpp aae/system/window/linux/glx_present.cpp aae/aae.vcxproj aae/aae.vcxproj.filters
git -C C:/Source2026/AAE_publish commit -m "feat(video): renderer dispatch layer - GL chain renamed glchain_*, vkchain_ stubs with GL fallback"
```

---

### Task 4: Vulkan SDK wiring

**Files:**
- Modify: `aae/aae.vcxproj` (x64 Debug + Release property groups)
- Modify: `aae/aae/aae_video_vk/vulkan_renderer.cpp`

- [ ] **Step 1: Declare the SDK dependency explicitly in the project**

For BOTH x64 configurations in `aae/aae.vcxproj` (the donor repos relied on an invisible per-machine `.user.props` — spec §6 requires this to be explicit):

- `<AdditionalIncludeDirectories>`: append `$(VULKAN_SDK)\Include;`
- Linker `<AdditionalDependencies>`: append `vulkan-1.lib;`
- Linker `<AdditionalLibraryDirectories>`: append `$(VULKAN_SDK)\Lib;`

If `%VULKAN_SDK%` is not set in the environment, stop and install the LunarG Vulkan SDK first — do not hardcode a path.

- [ ] **Step 2: Prove compile + link with a loader query**

In `vulkan_renderer.cpp`, add the include and extend `vkchain_init`:

```cpp
#include <vulkan/vulkan.h>

int  vkchain_init(void)
{
	uint32_t v = VK_API_VERSION_1_0;
	vkEnumerateInstanceVersion(&v);
	LOG_INFO("Vulkan loader present, instance version %u.%u.%u (headers %u.%u.%u)",
		VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v),
		VK_API_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE),
		VK_API_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE),
		VK_API_VERSION_PATCH(VK_HEADER_VERSION_COMPLETE));
	LOG_ERROR("vkchain_init: Vulkan chain not implemented yet (Phase 4a Plan 2)");
	return 0;
}
```

- [ ] **Step 3: Build**

Run the build command. Expected: exit 0. An unresolved `vkEnumerateInstanceVersion` means the lib dir/lib name from Step 1 is wrong for this SDK.

- [ ] **Step 4: Runtime check**

```bash
cd C:/Source2026/AAE_publish/aae/x64/Release && ./aae.exe asteroid -renderer vulkan
```

Expected: log now shows `Vulkan loader present, instance version 1.3.x` (or 1.4.x) before the fallback popup. Fallback and GL play still work.

- [ ] **Step 5: Commit**

```bash
git -C C:/Source2026/AAE_publish add aae/aae.vcxproj aae/aae/aae_video_vk/vulkan_renderer.cpp
git -C C:/Source2026/AAE_publish commit -m "build: explicit Vulkan SDK include/lib wiring + loader smoke check"
```

---

### Task 5: Shader home + build rule

**Files:**
- Create: `aae/shaders/vk/fast_poly_vk.vert`
- Create: `aae/shaders/vk/fast_poly_vk.frag`
- Modify: `aae/aae.vcxproj` (CustomBuild items)

- [ ] **Step 1: Copy the existing canonical GLSL sources**

The `fast_poly_vk` GLSL sources already exist in the Bosconian output tree — copy them verbatim, do not rewrite:

```bash
mkdir -p C:/Source2026/AAE_publish/aae/shaders/vk
cp C:/Source2026/Bosconian/x64/Release/shaders/fast_poly_vk.vert C:/Source2026/AAE_publish/aae/shaders/vk/
cp C:/Source2026/Bosconian/x64/Release/shaders/fast_poly_vk.frag C:/Source2026/AAE_publish/aae/shaders/vk/
```

For reference, the copied content must be exactly this (verified against the Fpoly pipeline: UBO set=0 binding=0 mat4, loc 0 vec2 pos R32G32_SFLOAT, loc 1 vec4 color R8G8B8A8_UNORM):

`fast_poly_vk.vert`:

```glsl
#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inColor;

layout(set = 0, binding = 0) uniform Globals
{
    mat4 uProj;
} g;

layout(location = 0) out vec4 vColor;

void main()
{
    vColor = inColor;
    gl_Position = g.uProj * vec4(inPos.xy, 0.0, 1.0);
}
```

`fast_poly_vk.frag`:

```glsl
#version 450

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vColor;
}
```

- [ ] **Step 2: Add the CustomBuild rule to `aae/aae.vcxproj`**

New `<ItemGroup>` (paths relative to the vcxproj, which lives in `aae/`):

```xml
<ItemGroup>
  <CustomBuild Include="shaders\vk\fast_poly_vk.vert">
    <Command>if not exist "$(OutDir)shaders\vk" mkdir "$(OutDir)shaders\vk"
"$(VULKAN_SDK)\Bin\glslangValidator.exe" -V "%(FullPath)" -o "$(OutDir)shaders\vk\%(Filename)%(Extension).spv"</Command>
    <Outputs>$(OutDir)shaders\vk\%(Filename)%(Extension).spv</Outputs>
    <Message>glslangValidator %(Filename)%(Extension)</Message>
  </CustomBuild>
  <CustomBuild Include="shaders\vk\fast_poly_vk.frag">
    <Command>if not exist "$(OutDir)shaders\vk" mkdir "$(OutDir)shaders\vk"
"$(VULKAN_SDK)\Bin\glslangValidator.exe" -V "%(FullPath)" -o "$(OutDir)shaders\vk\%(Filename)%(Extension).spv"</Command>
    <Outputs>$(OutDir)shaders\vk\%(Filename)%(Extension).spv</Outputs>
    <Message>glslangValidator %(Filename)%(Extension)</Message>
  </CustomBuild>
</ItemGroup>
```

Every future shader (Plans 2-6) is added as another `<CustomBuild>` pair here.

- [ ] **Step 3: Build and confirm the .spv outputs**

Run the build command, then:

```bash
ls C:/Source2026/AAE_publish/aae/x64/Release/shaders/vk/
```

Expected: `fast_poly_vk.vert.spv`, `fast_poly_vk.frag.spv`. A glslangValidator syntax error fails the build — that is the shader "test".

- [ ] **Step 4: Commit**

```bash
git -C C:/Source2026/AAE_publish add aae/shaders aae/aae.vcxproj
git -C C:/Source2026/AAE_publish commit -m "build: canonical VK shader home (aae/shaders/vk) + glslangValidator build rule; import fast_poly_vk GLSL from Bosconian donor"
```

---

### Task 6: GL regression gate

**Files:** none (verification only)

- [ ] **Step 1: Runtime matrix on the default (GL) renderer**

From `aae/x64/Release`, run each and confirm identical behavior to a pre-plan build (boots, plays, correct sound, correct visuals):

```bash
./aae.exe asteroid
```
```bash
./aae.exe tempest
```
```bash
./aae.exe pacman
```
```bash
./aae.exe warlords
```

- [ ] **Step 2: GUI pass**

Launch `./aae.exe` with no args: navigate the menu, confirm the starfield draws (`gui_points_*` went through the dispatch), start a game from the GUI, return, quit. Enter KEY CONFIG and rebind a key.

- [ ] **Step 3: Window ops**

In any game: resize the window, toggle borderless fullscreen, minimize/restore. All must behave as before.

- [ ] **Step 4: Fallback path once more**

```bash
./aae.exe -renderer vulkan
```

GUI boots on GL after the popup; log shows the loader version line.

- [ ] **Step 5: Commit the plan checkboxes and close out**

```bash
git -C C:/Source2026/AAE_publish add docs/superpowers/plans/2026-07-31-vulkan-phase4a-1-dispatch-foundation.md
git -C C:/Source2026/AAE_publish commit -m "docs: check off Plan 1 (dispatch foundation) verification"
```

---

## Self-review notes (done at write time)

- **Spec coverage:** implements spec §3.1 (switch), §3.2 (dispatch + renames), §5 (fallback policy, `vk_validation` deferred to Plan 2 where validation exists), §6 (shader home + build rule, SDK explicitness). §3.3-§3.5, §4 are Plans 2-6 by design.
- **Placeholder scan:** every code step carries full code; the two "verify before editing" instructions (setup_config ordering, header signatures) are checks against drift with exact grep targets, not deferred work.
- **Type consistency:** `vkchain_*` signatures match the dispatch calls; `glchain_*` declarations match the rename table; `GuiPointVertex` is forward-declared in `vulkan_renderer.h` and fully defined in `opengl_renderer.h` which `renderer_dispatch.cpp` includes.
