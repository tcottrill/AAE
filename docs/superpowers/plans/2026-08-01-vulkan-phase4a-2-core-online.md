# Vulkan Phase 4a — Plan 2: Vulkan Core Online

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `renderer=vulkan` brings up a real Vulkan 1.3 swapchain through AAE's `IPresentSurface` contract and presents a cleared frame every emulator tick — window resize, borderless fullscreen, vsync toggle, and shutdown all working — replacing the Plan 1 fallback popup.

**Architecture:** Import the Bosconian donor `sys_vk` into `aae/system/graphics/vk/`, platform-neutralized per spec §3.5: `VK_Init(ctx, IPresentSurface&)`, runtime loader bootstrap (`LoadLibraryA`/`dlopen` — zero link deps, spec §6), extensions/surface/extent through the contract. `Win32PresentSurface::CreateVkSurface` gets its real implementation. `winmain` learns to skip GL context creation when Vulkan is selected. The `vkchain_*` stubs from Plan 1 become the real frame loop: `set_render()`→`VK_BeginFrame`, `GLSwapBuffers()`→`VK_EndFrame`.

**Tech Stack:** Vendored Vulkan headers v309 (`aae/system/3rdparty/vulkan/`), vulkan-1.dll at runtime, MSVC x64. Donor: `C:\Source2026\Bosconian\Bosconian\sys_graphics\sys_vk.{h,cpp}` (the NEWEST sys_vk — per-image `renderFinished` semaphores, fence-after-acquire ordering, per-slot cmd buffer reset).

**Verified anchors (2026-08-01):**
- Donor `sys_vk.h` (370 lines): `VkContext` struct with `HWND hwnd` (line 56), ~90 `PFN_vk*_` members, `kFramesInFlight = 2`. `VK_Init(VkContext&, HWND, bool enableValidation = false, bool vsync = true)`.
- Donor `sys_vk.cpp` Win32 touch points: `#include <windows.h>`/`vulkan_win32.h` in sys_vk.h:7-21; `GetClientRect` extent fallback (~:245); `vkCreateWin32SurfaceKHR` block (~:826-841); bootstrap `ctx.vkGetInstanceProcAddr_ = vkGetInstanceProcAddr;` (~:622) plus direct `vkGetInstanceProcAddr(nullptr, ...)` calls (~:625, :631) — these link against vulkan-1.lib and must become runtime-loaded.
- AAE window contract: `aae/system/window/sys_window.h` — `IPresentSurface::{SwapBuffers, GetDrawableSize, RequiredVkInstanceExtensions, CreateVkSurface}`; `GetSystemWindow().Presentation()`.
- `Win32PresentSurface::RequiredVkInstanceExtensions` already returns `{"VK_KHR_surface", "VK_KHR_win32_surface"}` (winmain.cpp:882-887); `CreateVkSurface` is an honest stub (winmain.cpp:889-895).
- winmain GL bring-up to bypass under Vulkan: `InitOpenGLContext(false,false,true)` at winmain.cpp:1279; GL black-frame present at :1287-1289; window is created hidden and shown at :1291 only after the black frame.
- winmain already reads `aae.ini` before window creation (`GenerateFinalWindowSetup` reads `[main] starting_monitor`), so an early `renderer=` read uses the same machinery.
- Plan 1 dispatch: `aae/aae/aae_video_vk/vulkan_renderer.cpp` has 14 `vkchain_*` stubs; `renderer_dispatch.cpp` falls back to GL (session-latched) when `vkchain_init()` returns 0 — that fallback stays as the error path.
- `glchain_swap_buffers` guards on `hDC` internally (verified in review), so a Vulkan session calling it accidentally is a no-op, not a crash.
- Bug catalog (donor engine's `vulkan-frame-pitfalls` skill) — the donor sys_vk already embodies the fixes for entries 1 (per-slot `vkResetCommandBuffer`, never pool reset), 2 (direction-aware acquire/release barrier masks), and the fence-after-acquire deadlock fix. Import these code paths VERBATIM — do not "simplify" them.

**Build command:** `MSBuild.exe aae/aae.vcxproj -p:Configuration=Release -p:Platform=x64 -v:q -nologo` from `C:\Source2026\AAE_publish` (canonical MSBuild path: `"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"`). Pass = exit 0, no new warnings beyond the known five. **Agents never launch aae.exe** — runtime verification is the user-run gate in Task 6.

**Plan sequence:** Plan 2 of 6. After this: (3) raster game path, (4) raster post + artwork, (5) vector path, (6) GUI/overlays/snapshots + full matrix.

---

### Task 1: Import and neutralize `sys_vk`

**Files:**
- Create: `aae/system/graphics/vk/sys_vk.h` (from donor `C:\Source2026\Bosconian\Bosconian\sys_graphics\sys_vk.h`)
- Create: `aae/system/graphics/vk/sys_vk.cpp` (from donor `C:\Source2026\Bosconian\Bosconian\sys_graphics\sys_vk.cpp`)
- Modify: `aae/aae.vcxproj` + `aae/aae.vcxproj.filters` (register sys_vk.cpp/h; add `./system/graphics/vk` to AdditionalIncludeDirectories in BOTH x64 configs)
- Modify: `CMakeLists.txt` (add sys_vk.cpp to AAE_COMMON_SOURCES; bump the source-count drift check 49→50; add the include dir if CMake mirrors include paths)

- [ ] **Step 1: Copy the donor files verbatim, then apply the neutralization edits below — nothing else.**

The donor is battle-tested; every unlisted line imports unchanged. The edits:

**(a) sys_vk.h header block** — replace lines 7-21 (`#ifdef _WIN32` windows.h + `VK_USE_PLATFORM_WIN32_KHR` + `vulkan_win32.h`) with:

```cpp
#include <vulkan/vulkan.h>

#include <stdint.h>
#include <vector>
#include <string>

// Platform-neutral: surface creation, required instance extensions, and
// drawable size all come from the window layer's IPresentSurface contract
// (aae/system/window/sys_window.h). No windows.h, no vulkan_win32.h here.
class IPresentSurface;
```

**(b) VkContext members** — replace `HWND hwnd = nullptr;` (donor :56) with:

```cpp
    // The window-layer presentation contract (never null while initialized).
    IPresentSurface* present = nullptr;
    // Runtime handle for vulkan-1.dll / libvulkan.so.1 (spec sec. 6: no
    // import-lib link; the loader is bound with LoadLibrary/dlopen so builds
    // need no Vulkan SDK or package installed).
    void* loaderModule = nullptr;
```

Delete the member `PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR_...` if present (surface creation moved behind the contract).

**(c) VK_Init signature** — in both .h and .cpp:

```cpp
bool VK_Init(VkContext& ctx, IPresentSurface& present, bool enableValidation = false, bool vsync = true);
```

Body start: `ctx.present = &present;` replaces `ctx.hwnd = hwnd;`.

**(d) Loader bootstrap** — at the top of sys_vk.cpp add (after existing includes):

```cpp
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// ---------------------------------------------------------------------------
// Runtime loader bootstrap (spec sec. 6). Only these two touch the platform;
// every other Vulkan function is fetched through vkGetInstanceProcAddr_/
// vkGetDeviceProcAddr_ exactly as the donor engine always did.
// ---------------------------------------------------------------------------
static void* LoaderOpen(void)
{
#ifdef _WIN32
	return (void*)LoadLibraryA("vulkan-1.dll");
#else
	return dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
}

static void* LoaderSym(void* module, const char* name)
{
#ifdef _WIN32
	return (void*)GetProcAddress((HMODULE)module, name);
#else
	return dlsym(module, name);
#endif
}

static void LoaderClose(void* module)
{
	if (!module) return;
#ifdef _WIN32
	FreeLibrary((HMODULE)module);
#else
	dlclose(module);
#endif
}
```

Then in `VK_Init`, replace the direct-link bootstrap (donor ~:622-631):

```cpp
	ctx.loaderModule = LoaderOpen();
	if (!ctx.loaderModule)
	{
		LOG_ERROR("VK_Init: Vulkan runtime not found (vulkan-1.dll / libvulkan.so.1)");
		return false;
	}
	ctx.vkGetInstanceProcAddr_ =
		(PFN_vkGetInstanceProcAddr)LoaderSym(ctx.loaderModule, "vkGetInstanceProcAddr");
	if (!ctx.vkGetInstanceProcAddr_)
	{
		LOG_ERROR("VK_Init: vkGetInstanceProcAddr not found in loader");
		return false;
	}
```

Every subsequent pre-instance call in the donor of the form `vkGetInstanceProcAddr(nullptr, "...")` becomes `ctx.vkGetInstanceProcAddr_(nullptr, "...")` (donor ~:625, :631 — grep for others). In `VK_Shutdown`, after everything else: `LoaderClose(ctx.loaderModule); ctx.loaderModule = nullptr;`.

**(e) Instance extensions** — where the donor builds its instance extension list (hardcoded `VK_KHR_surface` + `VK_KHR_win32_surface`), replace with:

```cpp
	uint32_t extCount = 0;
	const char* const* platformExts = present.RequiredVkInstanceExtensions(&extCount);
	std::vector<const char*> extensions(platformExts, platformExts + extCount);
	if (enableValidation)
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
```

(Match how the donor appends the debug-utils extension — keep its logic, only the platform pair changes source.)

**(f) Surface creation** — replace the `vkCreateWin32SurfaceKHR` block (donor ~:826-841) with:

```cpp
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	if (!present.CreateVkSurface((void*)ctx.instance, (void*)&surface) || surface == VK_NULL_HANDLE)
	{
		LOG_ERROR("VK_Init: IPresentSurface::CreateVkSurface failed");
		return false;
	}
	ctx.surface = surface;
```

**(g) Extent fallback** — replace the `GetClientRect(ctx.hwnd, ...)` fallback (donor ~:245) with:

```cpp
		int dw = 0, dh = 0;
		if (ctx.present)
			ctx.present->GetDrawableSize(&dw, &dh);
		if (dw > 0 && dh > 0)
		{
			extent.width = (uint32_t)dw;
			extent.height = (uint32_t)dh;
		}
		else
		{
			extent.width = 640;
			extent.height = 480;
		}
```

**(h) Includes/logging** — sys_vk.cpp includes `"sys_window.h"` (for IPresentSurface) and uses AAE's `sys_log.h` (donor already does). ASCII-only comments throughout; keep donor comments (they document real bug fixes — especially the frame-pass contract, fence-after-acquire, and per-slot command buffer reset comments).

**(i) Clear color** — find the `VkClearValue`/`clearValue` in `VK_BeginFrame`'s `vkCmdBeginRendering` and set it to a distinctive dark blue `{0.02f, 0.05f, 0.20f, 1.0f}` with the comment `// Plan 2 gate color: prove VK is presenting (raster/vector chains draw over this from Plan 3 on).`

- [ ] **Step 2: Register in build systems**

vcxproj: `<ClCompile Include="system\graphics\vk\sys_vk.cpp" />`, `<ClInclude Include="system\graphics\vk\sys_vk.h" />`, matching filter entries (new filter `Source Files\system\graphics\vk` or per existing convention). Append `./system/graphics/vk` to `<AdditionalIncludeDirectories>` in BOTH x64 configs (lines ~111 and ~150).
CMakeLists.txt: add `aae/system/graphics/vk/sys_vk.cpp` to `AAE_COMMON_SOURCES`, bump the drift-check (currently `EQUAL 49`) to 50, and add the include dir wherever `aae/system/3rdparty` is added (~line 454).

- [ ] **Step 3: Build (compile/link only — nothing calls VK_Init yet)**

Release + Debug x64, exit 0, no new warnings. Common failure: a missed `hwnd` reference — the compiler will name it.

- [ ] **Step 4: Commit**

```bash
git -C C:/Source2026/AAE_publish add aae/system/graphics/vk aae/aae.vcxproj aae/aae.vcxproj.filters CMakeLists.txt
git -C C:/Source2026/AAE_publish commit -m "feat(vk): import sys_vk from Bosconian donor, platform-neutralized behind IPresentSurface + runtime loader bootstrap"
```

---

### Task 2: Real `Win32PresentSurface::CreateVkSurface`

**Files:**
- Modify: `aae/system/window/winmain.cpp` (the stub at ~:889-895)

- [ ] **Step 1: Implement the stub**

At the top of winmain.cpp (which already includes windows.h), add after the existing includes:

```cpp
// Vulkan surface creation for the Win32 window backend (Phase 4a Plan 2).
// Uses the vendored headers; binds the two needed entry points from the
// runtime loader so nothing links vulkan-1.lib (spec sec. 6).
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
```

Replace the stub body:

```cpp
bool Win32PresentSurface::CreateVkSurface(void* instance, void* outSurface)
{
	if (!instance || !outSurface || !g_hWnd)
	{
		LOG_ERROR("Win32PresentSurface::CreateVkSurface: bad args or no window");
		return false;
	}

	HMODULE loader = LoadLibraryA("vulkan-1.dll");
	if (!loader)
	{
		LOG_ERROR("Win32PresentSurface::CreateVkSurface: vulkan-1.dll not found");
		return false;
	}

	PFN_vkGetInstanceProcAddr gipa =
		(PFN_vkGetInstanceProcAddr)GetProcAddress(loader, "vkGetInstanceProcAddr");
	PFN_vkCreateWin32SurfaceKHR createWin32Surface = gipa
		? (PFN_vkCreateWin32SurfaceKHR)gipa((VkInstance)instance, "vkCreateWin32SurfaceKHR")
		: nullptr;

	bool ok = false;
	if (createWin32Surface)
	{
		VkWin32SurfaceCreateInfoKHR sci{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
		sci.hinstance = GetModuleHandleW(nullptr);
		sci.hwnd = g_hWnd;

		VkResult r = createWin32Surface((VkInstance)instance, &sci, nullptr, (VkSurfaceKHR*)outSurface);
		ok = (r == VK_SUCCESS);
		if (!ok)
			LOG_ERROR("Win32PresentSurface::CreateVkSurface: vkCreateWin32SurfaceKHR failed (VkResult=%d)", (int)r);
	}
	else
	{
		LOG_ERROR("Win32PresentSurface::CreateVkSurface: vkCreateWin32SurfaceKHR not available");
	}

	FreeLibrary(loader);  // refcounted; sys_vk holds its own reference for the session
	return ok;
}
```

Adapt `g_hWnd`/logging names to what winmain.cpp actually uses (check the file — the window handle global and the class's access to it; if the stub's surrounding code reaches the HWND differently, follow that). Keep the "honest stub" comment block above it updated to say it is now implemented.

- [ ] **Step 2: Build (Release x64), commit**

```bash
git -C C:/Source2026/AAE_publish add aae/system/window/winmain.cpp
git -C C:/Source2026/AAE_publish commit -m "feat(vk): implement Win32PresentSurface::CreateVkSurface via runtime-loaded vkCreateWin32SurfaceKHR"
```

---

### Task 3: winmain skips GL bring-up under Vulkan

**Files:**
- Modify: `aae/system/window/winmain.cpp` (~:1276-1292)

- [ ] **Step 1: Early renderer read**

Near the other early-config reads in wWinMain (before window creation is fine, before :1279 is required), add a helper + call. winmain already uses the ini machinery (`GenerateFinalWindowSetup` reads `[main] starting_monitor`), so mirror that pattern:

```cpp
// ---------------------------------------------------------------------------
// EarlyRendererIsVulkan
// The GL context must be skipped BEFORE config.renderer is populated (that
// happens later, inside run_game's setup_config). Read the same two sources
// the dispatch reads - [main] renderer in aae.ini, then a -renderer cmdline
// override - so the window layer and the dispatch always agree.
// ---------------------------------------------------------------------------
static bool EarlyRendererIsVulkan(void)
{
	bool vulkan = false;
	const char* r = get_config_string("main", "renderer", "opengl");
	if (r && strcmp(r, "vulkan") == 0)
		vulkan = true;

	// Cmdline override, same precedence as the dispatch.
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv)
	{
		for (int i = 1; i < argc - 1; ++i)
		{
			if (_wcsicmp(argv[i], L"-renderer") == 0)
			{
				if (_wcsicmp(argv[i + 1], L"vulkan") == 0)      vulkan = true;
				else if (_wcsicmp(argv[i + 1], L"opengl") == 0) vulkan = false;
			}
		}
		LocalFree(argv);
	}
	return vulkan;
}
```

Check what winmain includes for `get_config_string` (iniFile.h) and whether the ini path is already set at this point (it is for `starting_monitor` — place this read in the same region or later). `CommandLineToArgvW` needs `<shellapi.h>` — check if already included.

- [ ] **Step 2: Gate the GL bring-up**

Replace winmain.cpp:1276-1289 region:

```cpp
	const bool wantVulkan = EarlyRendererIsVulkan();
	g_glContextCreated = false;

	if (!wantVulkan)
	{
		// useCoreProfile = true: request a forward-compatible OpenGL core context.
		if (!InitOpenGLContext(false, false, true)) {
			LOG_ERROR("Failed to initialize OpenGL");
			return -1;
		}
		g_glContextCreated = true;

		// Present one black frame before the window is visible (see note above).
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glchain_swap_buffers();
	}
	else
	{
		LOG_INFO("Renderer=vulkan: skipping OpenGL context creation (window layer)");
		// Keep the first-visible-pixels-are-black invariant without GL: paint
		// the hidden window's client area once with GDI.
		RECT rc{};
		GetClientRect(g_hWnd, &rc);
		HDC dc = GetDC(g_hWnd);
		if (dc)
		{
			FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
			ReleaseDC(g_hWnd, dc);
		}
	}
```

Add `static bool g_glContextCreated = false;` near winmain's other statics. NOTE the swap call in the GL branch changes from the dispatch `GLSwapBuffers()` to `glchain_swap_buffers()` directly — this is window-layer GL bring-up code, same rule as `Win32PresentSurface::SwapBuffers` (and it removes the Plan-1-noted hazard at this line for good).

Then audit winmain for OTHER GL calls that would run in a Vulkan session (grep winmain.cpp for `gl[A-Z]`, `wgl`, `GLSwapBuffers`, `SetvSync`, `InitOpenGLContext`, and the GL teardown at exit): each site gets guarded with `if (g_glContextCreated)`. Report the list of guarded sites. `Win32PresentSurface::SwapBuffers()` keeps calling `glchain_swap_buffers()` unconditionally — it's internally hDC-guarded and only the GL chain calls it.

- [ ] **Step 3: Build (Release x64), commit**

```bash
git -C C:/Source2026/AAE_publish add aae/system/window/winmain.cpp
git -C C:/Source2026/AAE_publish commit -m "feat(vk): winmain skips GL context creation when renderer=vulkan"
```

---

### Task 4: Real `vkchain_*` frame loop

**Files:**
- Modify: `aae/aae/aae_video_vk/vulkan_renderer.cpp` (replace the Plan 1 stubs)
- Modify: `aae/aae/config.h` + `aae/aae/config.cpp` (add `int vk_validation;` read from `[main] vk_validation`, default 0 — same pattern as `renderer`)

- [ ] **Step 1: Add the `vk_validation` config field** (config.h next to `renderer`; config.cpp next to the renderer read: `config.vk_validation = get_config_int("main", "vk_validation", 0);`)

- [ ] **Step 2: Rewrite vulkan_renderer.cpp**

```cpp
// ===========================================================================
// vulkan_renderer.cpp - Vulkan chain orchestration (Phase 4a Plan 2).
//
// Owns the VkContext and maps the dispatch entry points onto the sys_vk
// frame loop (spec sec. 3.4):
//   vkchain_set_render   -> VK_BeginFrame (acquire, open pass, clear)
//   vkchain_render       -> record draws (nothing yet; Plans 3-6 fill this in)
//   vkchain_swap_buffers -> VK_EndFrame (submit + present)
// A failed begin (resize, minimize, OUT_OF_DATE) recreates the swapchain and
// skips the rest of that frame; s_frameOpen keeps end-of-frame honest.
// ===========================================================================
#include "vulkan_renderer.h"
#include "sys_log.h"
#include "sys_vk.h"
#include "sys_window.h"
#include "config.h"

static VkContext g_vk;
static bool      s_initialized = false;
static bool      s_frameOpen = false;
static uint32_t  s_imageIndex = 0;

int vkchain_init(void)
{
	if (s_initialized)
		return 1;   // re-entrant like glchain_init: run_game calls per load

	IPresentSurface* present = GetSystemWindow().Presentation();
	if (!present)
	{
		LOG_ERROR("vkchain_init: no presentation surface (headless backend?)");
		return 0;
	}

	const bool validation = (config.vk_validation != 0);
	if (!VK_Init(g_vk, *present, validation, /*vsync=*/true))
	{
		LOG_ERROR("vkchain_init: VK_Init failed");
		VK_Shutdown(g_vk);
		return 0;
	}

	s_initialized = true;
	LOG_INFO("vkchain_init: Vulkan chain online (validation=%d)", validation ? 1 : 0);
	return 1;
}

void vkchain_shutdown(void)
{
	if (!s_initialized)
		return;
	VK_Shutdown(g_vk);
	s_initialized = false;
	s_frameOpen = false;
}

void vkchain_set_render(void)
{
	if (!s_initialized || s_frameOpen)
		return;
	if (!VK_BeginFrame(g_vk, s_imageIndex))
	{
		VK_RecreateSwapchain(g_vk);
		return;             // skip this frame; next tick re-acquires
	}
	s_frameOpen = true;
}

void vkchain_render(void)
{
	// Plan 2: the frame pass opened by VK_BeginFrame clears to the gate
	// color; there is nothing to record yet. Raster (Plan 3), post/artwork
	// (Plan 4), vector (Plan 5) and GUI (Plan 6) record here.
}

void vkchain_swap_buffers(void)
{
	if (!s_initialized || !s_frameOpen)
		return;
	s_frameOpen = false;
	if (!VK_EndFrame(g_vk, s_imageIndex))
		VK_RecreateSwapchain(g_vk);
}

void vkchain_set_vsync(bool enabled)
{
	if (!s_initialized)
		return;
	if (g_vk.vsync == enabled)
		return;
	g_vk.vsync = enabled;
	VK_RecreateSwapchain(g_vk);   // waits device idle internally
}

void vkchain_on_window_resize(int newW, int newH)
{
	(void)newW; (void)newH;
	if (!s_initialized)
		return;
	VK_RecreateSwapchain(g_vk);   // extent re-queried from the surface caps
}

// --- Plans 3-6 fill these in -----------------------------------------------
void vkchain_gui_points_init(int) {}
void vkchain_gui_points_draw(const GuiPointVertex*, int, float) {}
void vkchain_gui_points_shutdown(void) {}
void vkchain_vector_hard_clear(void) {}
void vkchain_init_raster_overlay(void) {}
void vkchain_shutdown_raster_overlay(void) {}
int  vkchain_get_error(void) { return 0; }
```

Check the exact names/behaviors against the imported sys_vk.h (e.g. `VK_RecreateSwapchain` return type, whether `VK_BeginFrame` handles minimized/zero-extent by returning false — it does in the donor; a failed recreate while minimized just means the next tick retries). If `vkchain_set_render` is called while a recreate keeps failing (minimized), the chain stays idle safely — confirm no tight-loop logging (donor logs "deferring" once per attempt; if that spams the log when minimized, rate-limit by only logging on state change and note it in the report).

- [ ] **Step 3: Build Release + Debug x64, commit**

```bash
git -C C:/Source2026/AAE_publish add aae/aae/aae_video_vk/vulkan_renderer.cpp aae/aae/config.h aae/aae/config.cpp
git -C C:/Source2026/AAE_publish commit -m "feat(vk): real vkchain frame loop - VK_BeginFrame/EndFrame wired to dispatch, vk_validation option"
```

---

### Task 5: Vulkan leak guards in the emu core

**Files:**
- Modify: `aae/aae/acommon.cpp`, `aae/aae/cpu_code/cpu_control.cpp`, `aae/aae/inptport.cpp` (three representative core TUs)

- [ ] **Step 1: Add the guard to each file, near the top after its includes**

```c
/* Phase 4 boundary check (spec sec. 3.5): the emulation core must never see
   Vulkan headers. Mirrors the _WINDOWS_ leak guard idiom from Phase 1. */
#ifdef VULKAN_H_
#error "vulkan.h leaked into the emulation core"
#endif
```

(vendored vulkan.h defines `VULKAN_H_`; verify with a grep of `aae/system/3rdparty/vulkan/vulkan.h`.) Check whether these files carry the Phase 1 `_WINDOWS_` guard and place the new one adjacent; if a different set of TUs carries those guards, use THAT set instead and report which.

- [ ] **Step 2: Build (guards must compile clean — proving the boundary holds), commit**

```bash
git -C C:/Source2026/AAE_publish add aae/aae/acommon.cpp aae/aae/cpu_code/cpu_control.cpp aae/aae/inptport.cpp
git -C C:/Source2026/AAE_publish commit -m "chore(vk): VULKAN_H_ leak guards in core TUs"
```

---

### Task 6: User-run gate (agents stop here)

**Files:** none — the user runs these after copying the fresh exe to the asset tree:

```bash
cp C:/Source2026/AAE_publish/aae/x64/Release/aae.exe C:/Source2026/AAE_publish/x64/Release/aae.exe
```

- [ ] **1. Vulkan boots:** `aae.exe asteroid -renderer vulkan` — NO fallback popup; a **dark blue** window appears (the Plan 2 gate color — the game itself is invisible until Plan 5). Audio plays (emulation runs under the blank screen). ESC exits cleanly, no crash on shutdown.
- [ ] **2. Log evidence** (`systemlog.txt`): `vkchain_init: Vulkan chain online`, swapchain creation lines (extent + presentMode), no ERROR lines during steady state.
- [ ] **3. Window ops under Vulkan:** resize the window (blue area follows, no crash), toggle borderless fullscreen, minimize + restore (no crash, no error spam while minimized).
- [ ] **4. Second game load:** ESC to exit is fine for asteroid; also run `aae.exe -renderer vulkan` (GUI — blank blue, expected), then quit. No popup, no crash.
- [ ] **5. GL regression:** `aae.exe asteroid` (default GL) — identical to Plan 1 gate. Also `aae.exe pacman`.
- [ ] **6. Validation spot-check (optional but valuable):** add `vk_validation=1` under `[main]` in aae.ini, run asteroid with `-renderer vulkan`, check the log for validation output (needs the validation layers present on the system — if the log says the layer is unavailable, that's fine, remove the setting and note it).

---

## Self-review notes (done at write time)

- **Spec coverage:** implements spec §3.4 (frame mapping), §3.5 (platform-neutral sys_vk + loader bootstrap + leak guards), §5 (`vk_validation`), §6 (vendored headers/runtime loader carry through), §7 Phase 4a windows-surface work. Raster/vector/GUI deliberately absent (Plans 3-6).
- **Placeholder scan:** all code steps carry full code; the "check against the imported file" instructions are drift guards against the 2660-line donor, with exact donor line anchors.
- **Type consistency:** `VK_Init(ctx, IPresentSurface&, bool, bool)` consistent across Tasks 1/4; `s_frameOpen` handshake between set_render/swap_buffers matches the dispatch call order in `aae_emulator.cpp` (`set_render` :1546 → `render` :1569 → `GLSwapBuffers` :1581); `g_glContextCreated` only in winmain.
- **Known deferred items:** `SetvSync` under Vulkan is reachable from the GUI menu — wired in Task 4; `end_gl` has no callers (pre-existing) so `vkchain_shutdown` is reached via `emu_end`-equivalent paths only — verify during Task 4 whether AAE calls `end_gl` at exit (Plan 1 found zero callers; if truly never called, note that VK teardown happens at process exit — acceptable for Plan 2, flagged for Plan 6 cleanup).
