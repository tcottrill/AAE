# Vulkan Phase 4a — Plan 3: Raster Game Path

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Raster games render visibly under `renderer=vulkan`: the driver's `main_bitmap` pixels reach the swapchain through the Bosconian donor `Fpoly` renderer, with correct MAME orientation (vertical pacman), aspect-fit letterboxing, and live resize.

**Architecture:** Extract the backend-neutral pixel-emit loop out of `raster_poly_update` (orientation flags + pen lookup, currently welded to the GL `Fpoly`) into a sink-parameterized function both chains share. Import the Bosconian VK `Fpoly` as `FpolyVK` (renamed — AAE already has a GL class named `Fpoly`), give it an optional viewport-rect override for letterboxing, and drive it from `vkchain_render` inside the frame pass Plan 2 opens. Direct-to-swapchain, Bosconian-style; the offscreen RenderTarget + CRT effects + artwork restructure is Plan 4 (spec §4.1's final ordering) — this plan gets game pixels on screen first.

**Donors:** `C:\Source2026\Bosconian\Bosconian\sys_graphics\fast_poly.{h,cpp}` (VK Fpoly: UBO set0/binding0 mat4 via `MakeOrtho(0,W,0,H)`, vertex vec2+unorm color, per-frame mapped VBO with `EnsureVBOCapacity`, records into the already-open frame pass, `flipViewportY` negative-height viewport). Reference for coordinate conventions: `C:\Source2026\Bosconian\emulator.cpp` `vk_blit_scrbitmap` — the PROVEN working blit: `py = (gameH - 1 - (row - vis.min_y)) * vid_scale` with `flipViewportY = true`.

**Verified anchors (2026-08-01):**
- `raster_poly_update` — `aae/aae/aae_video/opengl_renderer.cpp:181-~260`: walks `main_bitmap` over `Machine->drv->visible_area`, applies `ORIENTATION_SWAP_XY`/`FLIP_X`/`FLIP_Y` (flip AFTER swap, against dst extents), `osd_get_pen(Machine->pens[c], ...)`, submits `sc->addPoly(...)` scaled by `vid_scale`. Called only from `glchain_render`'s raster branch (:975).
- AAE's GL `Fpoly` — `aae/aae/vidhrdwr/fast_poly.{h,cpp}` — class name COLLIDES with the donor's; global-scope names in both headers must be diffed and donor ones renamed (`Fpoly`→`FpolyVK`, `_fpdata`→ check, `FastPolyVKCreateInfo` → check AAE's GL header for name overlap).
- `vulkan_renderer.cpp` statics: `g_vk` (VkContext), `s_frameOpen`, `s_imageIndex`; frame cmd buffer = `g_vk.cmdBuffers[g_vk.frameIndex]` (verify member names in the imported sys_vk.h).
- Plan 2 leaves `VK_BeginFrame`'s clear color at gate-blue `{0.02f, 0.05f, 0.20f, 1.0f}` — this plan changes it to black (game pixels are the proof now; borders must be black like GL).
- Shaders `fast_poly_vk.vert/.frag` already compile to `$(OutDir)shaders\vk\` (Plan 1's CustomBuild rule). The vert expects UBO `Globals { mat4 uProj; }` set 0 binding 0; vertex loc0 vec2 pos, loc1 vec4 color (R8G8B8A8_UNORM).
- `vid_scale` — global float, `aae_emulator.cpp:200` region sets it; emit loop multiplies by it. With viewport-fit letterboxing the value only affects sub-pixel resolution, not final size.
- CMake source-count drift check currently expects 50 (grew by 1 in Plan 2); implementer recounts rather than trusting stated numbers.

**Build command:** `MSBuild.exe aae/aae.vcxproj -p:Configuration=Release -p:Platform=x64 -v:q -nologo` from repo root (canonical MSBuild `"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"`). Pass = exit 0, no new warnings beyond the known five. **Agents never launch aae.exe.** Final build must postdate the last source edit — verify exe timestamps in BOTH output trees (`aae/x64/Release` from vcxproj builds, `x64/Release` if the .sln is built) before reporting.

---

### Task 1: Extract the backend-neutral raster emit loop

**Files:**
- Create: `aae/aae/aae_video/raster_emit.h`
- Create: `aae/aae/aae_video/raster_emit.cpp`
- Modify: `aae/aae/aae_video/opengl_renderer.cpp` (`raster_poly_update` becomes a thin wrapper)
- Modify: `aae/aae.vcxproj` + filters + `CMakeLists.txt` (register; recount drift check)

Pure refactor-by-move — the GL path must be bit-identical.

- [x] **Step 1: Create `raster_emit.h`**

```cpp
#pragma once
// ===========================================================================
// raster_emit.h - backend-neutral raster pixel emitter (Phase 4a Plan 3).
//
// Walks Machine->drv->visible_area of main_bitmap, applies the MAME
// orientation flags (SWAP_XY first, then FLIP_X/FLIP_Y against destination
// extents), converts pens to RGBA via osd_get_pen, and emits one scaled
// quad per pixel into the caller's sink. Both render chains share this
// loop; only the sink (GL Fpoly vs FpolyVK) and the Y direction differ.
// ===========================================================================
#include <stdint.h>

// One quad per visible pixel: top-left x/y in output space, edge size, RGBA.
typedef void (*RasterPolySink)(void* user, float x, float y, float size, uint32_t rgba);

// Post-orientation destination dimensions in SOURCE pixels (before vid_scale).
// Returns 0 if no machine/bitmap is available.
int raster_dst_dims(int* outW, int* outH);

// yFlip = 0: y grows downward (GL chain's Y-down raster ortho, today's
// behavior). yFlip = 1: y is flipped to bottom-left origin, matching the
// Bosconian vk_blit_scrbitmap convention consumed with flipViewportY=true.
void raster_emit_polys(RasterPolySink sink, void* user, int yFlip);
```

- [x] **Step 2: Create `raster_emit.cpp` by MOVING the loop**

Move the body of `raster_poly_update` (opengl_renderer.cpp:181 onward — the whole orientation walk) into `raster_emit_polys`, changing ONLY:
- the guard: keep the `!Machine || !Machine->drv || !main_bitmap` early-outs but drop the `!sc` check (sink replaces it);
- the emit site: where the original calls `sc->addPoly(x * vid_scale, y * vid_scale, vid_scale, MAKE_RGBA(...))` (read the exact original tail — match it), call instead:

```cpp
			float fy = (float)y;
			if (yFlip)
				fy = (float)(dstH - 1 - y);
			sink(user, (float)x * vid_scale, fy * vid_scale, vid_scale, rgba);
```

where `rgba` is built exactly as the original builds its color argument. `raster_dst_dims` returns the same `dstW`/`dstH` the loop computes (factor the computation so both use one path). Includes: whatever `raster_poly_update` needed (check the original's dependencies: `aae_mame_driver.h`, `osd_video.h`, `colordefs.h`, the `main_bitmap` extern, `vid_scale` extern) — but NO GL headers and NO `fast_poly.h`.

- [x] **Step 3: Shrink `raster_poly_update` to a wrapper in opengl_renderer.cpp**

```cpp
// ---------------------------------------------------------------------------
// raster_poly_update
// Backend-neutral emit loop moved to raster_emit.cpp (Phase 4a Plan 3);
// this wrapper feeds it into the GL Fpoly exactly as before (Y-down ortho,
// so no flip).
// ---------------------------------------------------------------------------
static void GlRasterSink(void* user, float x, float y, float size, uint32_t rgba)
{
	((Fpoly*)user)->addPoly(x, y, size, rgba);
}

void raster_poly_update(void)
{
	if (!sc)
		return;
	raster_emit_polys(GlRasterSink, sc, /*yFlip=*/0);
}
```

Match the original addPoly argument types exactly (read AAE's GL `Fpoly::addPoly` signature in `aae/aae/vidhrdwr/fast_poly.h` — if it takes the color as a different type/order, adapt the sink to preserve identical behavior).

- [x] **Step 4: Register, build both configs, GL-identical check**

Register raster_emit.cpp in vcxproj (+filters) and CMakeLists (recount the drift check yourself). Build Release + Debug x64: exit 0, no new warnings. This is a pure move — any behavior change is a bug.

- [x] **Step 5: Commit**

```bash
git -C C:/Source2026/AAE_publish add aae/aae/aae_video/raster_emit.h aae/aae/aae_video/raster_emit.cpp aae/aae/aae_video/opengl_renderer.cpp aae/aae.vcxproj aae/aae.vcxproj.filters CMakeLists.txt
git -C C:/Source2026/AAE_publish commit -m "refactor(video): extract backend-neutral raster emit loop from raster_poly_update"
```

---

### Task 2: Import the donor VK Fpoly as `FpolyVK`

**Files:**
- Create: `aae/aae/aae_video_vk/fast_poly_vk.h` (from `C:\Source2026\Bosconian\Bosconian\sys_graphics\fast_poly.h`)
- Create: `aae/aae/aae_video_vk/fast_poly_vk.cpp` (from `C:\Source2026\Bosconian\Bosconian\sys_graphics\fast_poly.cpp`)
- Modify: vcxproj + filters + CMakeLists (register; recount)

- [x] **Step 1: Copy donor files verbatim, then apply ONLY these edits**

1. **Collision renames.** Diff the donor header's global-scope names against AAE's GL `aae/aae/vidhrdwr/fast_poly.h` and rename every collision in the imported files with a `VK` suffix. At minimum `Fpoly` → `FpolyVK`; check `_fpdata`, `FastPolyVKCreateInfo`, and any file-scope helpers. Report the full rename list.
2. **Viewport override for letterboxing.** Add to the class:

```cpp
    // Optional viewport/scissor override (letterboxed aspect fit). When set,
    // Render uses this rect instead of the full swapchain. Coordinates are
    // swapchain pixels, y-down (Vulkan viewport space).
    void SetViewportRect(int x, int y, int w, int h)
    {
        m_vpX = x; m_vpY = y; m_vpW = w; m_vpH = h; m_vpOverride = true;
    }
    void ClearViewportRect() { m_vpOverride = false; }
```

with members `int m_vpX = 0, m_vpY = 0, m_vpW = 0, m_vpH = 0; bool m_vpOverride = false;`. In `Render`, where the donor builds `vp`/`sc` from `ctx.swapchainExtent`, use the override when set (keep the `flipViewportY` negative-height handling — apply it to the override rect the same way: `vp.y = m_vpY + m_vpH; vp.height = -(float)m_vpH;` when flipping). Scissor gets the same rect (positive height always).
3. Includes resolve via the existing include dirs (`sys_vk.h` is on the path). ASCII-only comments; keep donor comments (especially the frame-pass contract comment at Render — it documents why no barriers/passes are opened there).

- [x] **Step 2: Register in builds, compile both configs (nothing calls it yet), commit**

```bash
git -C C:/Source2026/AAE_publish add aae/aae/aae_video_vk/fast_poly_vk.h aae/aae/aae_video_vk/fast_poly_vk.cpp aae/aae.vcxproj aae/aae.vcxproj.filters CMakeLists.txt
git -C C:/Source2026/AAE_publish commit -m "feat(vk): import Bosconian VK Fpoly as FpolyVK with viewport-rect override"
```

---

### Task 3: Wire the raster path into `vkchain_*`

**Files:**
- Modify: `aae/aae/aae_video_vk/vulkan_renderer.cpp`
- Modify: `aae/system/graphics/vk/sys_vk.cpp` (clear color blue → black, one line)

- [x] **Step 1: Clear color to black**

In `VK_BeginFrame`, change the Plan 2 gate color to `{0.0f, 0.0f, 0.0f, 1.0f}` and update the comment: game pixels prove rendering from Plan 3 on; borders stay black like the GL chain.

- [x] **Step 2: Raster wiring in vulkan_renderer.cpp**

Add includes `"fast_poly_vk.h"` and `"raster_emit.h"`, statics, and a sink:

```cpp
static FpolyVK  g_fpoly;
static bool     s_fpolyInit = false;
static int      s_rasterW = 0;     // post-orientation dims, source pixels
static int      s_rasterH = 0;

static void VkRasterSink(void* user, float x, float y, float size, uint32_t rgba)
{
	((FpolyVK*)user)->addPoly(x, y, size, rgba);
}

static bool GameIsRaster(void)
{
	return Machine && Machine->gamedrv &&
		!(Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR);
}
```

(Adapt `addPoly`'s argument order/types to the imported `FpolyVK` — check the header.)

In `vkchain_init`, after the vector-list block, initialize for raster games:

```cpp
	if (GameIsRaster() && !s_fpolyInit)
	{
		if (raster_dst_dims(&s_rasterW, &s_rasterH))
		{
			FastPolyVKCreateInfo ci{};   // use the imported (possibly renamed) type
			ci.vertSpvPath = "shaders/vk/fast_poly_vk.vert.spv";
			ci.fragSpvPath = "shaders/vk/fast_poly_vk.frag.spv";
			ci.flipViewportY = true;
			// CORRECTED (Task 1 finding): the shared emit loop outputs UNSCALED
			// source-pixel coords with size = config.prescale, exactly like the
			// GL path. The ortho therefore spans the post-orientation source
			// dims - no vid_scale anywhere.
			if (g_fpoly.Init(g_vk, s_rasterW, s_rasterH, &ci))
			{
				s_fpolyInit = true;
			}
			else
			{
				LOG_ERROR("vkchain_init: FpolyVK init failed; raster game will show black");
			}
		}
	}
```

`vid_scale` is the existing global float — find its extern declaration (aae_mame_driver.h or opengl_renderer.h) and include accordingly. NOTE: `raster_dst_dims` needs `Machine` populated — verify `vkchain_init` (via `init_gl` at aae_emulator.cpp:920) runs after `Machine->gamedrv` is set for the current game (glchain_init's vector_start at the same point relies on the same thing, so it does — confirm and note). Also handle game switching: `vkchain_init`'s re-entrant path must re-check — if a raster game follows a vector game in the same session, `s_fpolyInit` is false and this block must run; if raster follows raster with DIFFERENT dims, Shutdown + re-Init. Implement:

```cpp
	// On re-entry (game switch), rebuild the raster renderer if the game
	// shape changed or the previous game was not raster.
	int newW = 0, newH = 0;
	if (GameIsRaster() && raster_dst_dims(&newW, &newH))
	{
		if (s_fpolyInit && (newW != s_rasterW || newH != s_rasterH))
		{
			g_fpoly.Shutdown(g_vk);
			s_fpolyInit = false;
		}
		...init as above when !s_fpolyInit...
	}
```

(Restructure cleanly rather than duplicating the init block — one helper `EnsureRasterRenderer()` called from both the fresh-init and re-entrant paths, mirroring `EnsureVectorList()`.)

In `vkchain_render`, before the drain calls:

```cpp
	if (s_frameOpen && s_fpolyInit && GameIsRaster())
	{
		raster_emit_polys(VkRasterSink, &g_fpoly, /*yFlip=*/1);

		// Aspect-fit letterbox: fit the post-orientation game rect into the
		// swapchain, centered. Natural pixel aspect for Plan 3; the layout
		// system's aspect overrides arrive with Plan 4.
		const float gameAspect = (float)s_rasterW / (float)s_rasterH;
		const int sw = (int)g_vk.swapchainExtent.width;
		const int sh = (int)g_vk.swapchainExtent.height;
		int vw = sw, vh = (int)(sw / gameAspect + 0.5f);
		if (vh > sh) { vh = sh; vw = (int)(sh * gameAspect + 0.5f); }
		g_fpoly.SetViewportRect((sw - vw) / 2, (sh - vh) / 2, vw, vh);

		g_fpoly.Render(g_vk, g_vk.cmdBuffers[g_vk.frameIndex],
			s_imageIndex, g_vk.frameIndex, false, 0.0f, 0.0f, 0.0f, 0.0f);
	}
```

(Verify the imported `FpolyVK::Render` parameter list and the `VkContext` member names — `cmdBuffers`, `frameIndex`, `swapchainExtent` — against the actual headers; the header wins.)

In `vkchain_shutdown`: `if (s_fpolyInit) { g_fpoly.Shutdown(g_vk); s_fpolyInit = false; }` BEFORE `VK_Shutdown` (device must still exist). Also reset `s_rasterW/H`.

- [x] **Step 3: sRGB color note (do NOT implement, document only)**

The swapchain is sRGB; pen colors are sRGB-authored bytes fed as UNORM vertex colors — the same as the working Bosconian setup, so ship it identically. IF the user's gate reports washed-out/too-bright colors vs the GL side-by-side, the fix is a CPU sRGB→linear conversion on the pen RGB in `VkRasterSink` (8-bit LUT). Add this as a comment at the sink.

- [x] **Step 4: Build Release + Debug x64 (fresh timestamps verified), commit**

```bash
git -C C:/Source2026/AAE_publish add aae/aae/aae_video_vk/vulkan_renderer.cpp aae/system/graphics/vk/sys_vk.cpp
git -C C:/Source2026/AAE_publish commit -m "feat(vk): raster game path - FpolyVK renders main_bitmap into the frame pass with aspect-fit letterbox"
```

---

### Task 4: User-run gate (agents stop here)

The exe AND the compiled shaders must both reach the asset tree:

```bash
cp C:/Source2026/AAE_publish/aae/x64/Release/aae.exe C:/Source2026/AAE_publish/x64/Release/aae.exe
mkdir -p C:/Source2026/AAE_publish/x64/Release/shaders/vk
cp C:/Source2026/AAE_publish/aae/x64/Release/shaders/vk/*.spv C:/Source2026/AAE_publish/x64/Release/shaders/vk/
```

- [x] **1. pacman renders:** `aae.exe pacman -renderer vulkan` — the maze is VISIBLE, portrait-oriented (not sideways/mirrored), letterboxed with black bars, playable with correct input. Compare colors against a `renderer=opengl` run side by side — report if VK looks washed-out/brighter (triggers the documented sRGB fix).
- [x] **2. A second raster game:** `aae.exe galaga -renderer vulkan` (or another raster title you have ROMs for) — renders and plays.
- [x] **3. Resize + fullscreen under Vulkan while pacman runs:** the game stays aspect-correct and centered at every size; minimize/restore stable.
- [x] **4. Vector games unaffected:** `aae.exe asteroid -renderer vulkan`, `aae.exe tempest -renderer vulkan` — still stable (blue... now BLACK window + audio; vector pixels arrive in Plan 5).
- [x] **5. GL regression:** `aae.exe pacman` (default GL) — identical to before this plan (the Task 1 refactor touched the GL path).

---

## Self-review notes (done at write time)

- **Spec coverage:** spec §4.1 steps 1 (Fpoly renders the quads) and 4 (aspect-fit present, natural aspect) land here in direct-to-swapchain form; steps 2-3 (CRT passes, artwork composite) and the RenderTarget restructure are Plan 4 by explicit decision, stated in Architecture.
- **Placeholder scan:** all code steps carry code; "verify against the header" instructions are drift guards with named files.
- **Type consistency:** `raster_emit_polys(sink, user, yFlip)` consistent across Tasks 1/3; `FpolyVK` naming consistent; `raster_dst_dims` used by both init and render paths.
- **Known risks called out:** class-name collisions (Task 2 diff step), sRGB appearance (gate check + documented fix), game-switch re-init (EnsureRasterRenderer), shader files must be copied to the asset tree (gate instructions).
