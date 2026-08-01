# Vulkan Rendering Chain Design (Phase 4 of the Core/OSD Split)

**Date:** 2026-07-31
**Status:** Approved for planning
**Depends on:** Phase 1 (OSD contract), Phase 3a (window contract), Phase 3b/3c (Linux backends)

---

## 1. Motivation and goal

AAE gains a second, complete rendering chain: **Vulkan 1.3**, living beside the
OpenGL chain, selected at startup by an ini setting. Both the raster and the
vector paths are implemented to **full visual parity** with the GL chain — all
shaders, all rendering: CRT post-effects, glow/phosphor, artwork/layouts,
bezels, GUI, overlays, snapshots.

Targets, in order:

| Stage | Target | Surface | Notes |
|---|---|---|---|
| 4a | Windows x64 | `VK_KHR_win32_surface` | full parity vs GL, verified game matrix |
| 4b | Linux desktop | `VK_KHR_xlib_surface` | Phase-3c X11 window, CMake build |
| 4b | Raspberry Pi 5 | `VK_KHR_xlib_surface` | Mesa V3DV, Vulkan 1.3 required |

The user's directive on record: *"I am not stopping until the complete Vulkan
1.3 rendering chain is complete in Windows and then Linux. That means all
shaders, all rendering."*

### Vulkan baseline

API **1.3** with `dynamicRendering` and `synchronization2` as hard device
requirements, used unconditionally — no 1.2 fallback paths. Pi 5 satisfies
this with a current Mesa (first action on the Pi is `vulkaninfo` to confirm;
the verified minimum Mesa version gets recorded in the docs). No
`VkRenderPass`/`VkFramebuffer` objects anywhere in the chain.

---

## 2. Donor code inventory

Two working donor codebases supply the Vulkan implementation. Nothing is
written from scratch that already exists.

### 2.1 Bosconian mini-port — `C:\Source2026\Bosconian`

The **newest** `sys_vk` conventions and the raster path.

| Component | Files | Notes |
|---|---|---|
| Vulkan core | `Bosconian\sys_graphics\sys_vk.{h,cpp}` | instance/device/swapchain/frame loop; per-image `renderFinished` semaphores; slot-fence + acquire ordering fixes |
| Raster renderer | `Bosconian\sys_graphics\fast_poly.{h,cpp}` | `Fpoly` per-pixel quad renderer; the reference implementation for per-slot append discipline and format-keyed pipeline variant caching |
| Texture system | `Bosconian\sys_graphics\sys_texture.{h,cpp}` | dual-backend `TEX`; VK path incl. swapchain-readback `Snapshot` |
| Screen quad | `Bosconian\sys_graphics\screen_quad_vk.{h,cpp}` | fullscreen + rect passes |

### 2.2 SpriteTest engine — `C:\SourceEngineAlpha\SpriteTestVulcan28_vulkan_working_copy`

The converted vector renderer and the offscreen/composite machinery.

| Component | Files | Notes |
|---|---|---|
| Vector beam renderer | `sys_graphics\vector_draw_vk.{h,cpp}` | `VectorDrawVK`: five pipelines (line add/over, disc max/over, shot); instanced strips; push-constant block `{mat4 uProj; float uAA, uStrength, uPremult, uCorePower, uBloomPower, uBloomIntensity, uOverdrive;}` |
| Beam shaders | `sys_graphics\vector_{line,disc,shot}_vk.{vert,frag}` | GLSL sources checked in |
| Render targets | `sys_render\render_target.{h,cpp}`, `render_target_vk.cpp` | offscreen color RT; `Begin(clear=false)` LOAD path (phosphor persistence); publishes `ctx.activeColorFormat` |
| Compositor | `RenderTargetCompositor` + `render_target_composite.*` | aspect-fit / integer-scale / 90°-step rotation to swapchain |
| Backport plan | `docs\2026-07-05-vector-draw-vk-aae-backport.md` | **read first** — flipViewportY fix, RT integration, and the three pre-ship fixes below |

### 2.3 Known defects to fix during import (from the backport doc)

1. **Append discipline** — `VectorDrawVK::drawBatch` writes at offset 0 with
   `firstInstance=0` and destroys the old buffer immediately on growth; only
   one `Record` per frame is safe. Fix by copying `fast_poly.cpp`'s per-slot
   write-head + stale-buffer retirement pattern.
2. **SSAA feather divide** — GL divides `uAA = line_smoothing / ssaa`; the VK
   path pushes `line_smoothing` raw, so beams on a supersampled RT are ssaa×
   too blurry.
3. **Init idempotence** — second `Init` leaks the pipeline layout + five
   pipelines; guard with `if (m_pipeLayout) Shutdown(ctx);`.

### 2.4 Convention carried over

Every pipeline in the chain is built against `VK_ActiveColorFormat(ctx)` (the
format of whatever pass is open — swapchain or RT), never `swapchainFormat`
directly, using the lazy per-format variant cache pattern. Dynamic rendering
requires the match; this is the single most important donor convention.

---

## 3. Architecture (Approach A: parallel chain behind a thin dispatch)

Decision on record (user, 2026-07-31): parallel chain; the GL code is not
refactored, not wrapped in classes, and must not regress.

### 3.1 The switch

- `aae.ini` `[main]` gains `renderer=opengl|vulkan` (default `opengl`), read
  once at startup into `config.renderer`. No runtime switching.
- `-renderer <name>` command-line override, following the existing
  cmdline-beats-ini pattern.
- Exactly one chain initializes per run. With `renderer=vulkan`, no GL context
  is created; with `renderer=opengl`, no Vulkan objects exist.

### 3.2 Dispatch — existing names keep working

The emulator core, GUI, and drivers keep calling the names they call today
(`init_gl`, `set_render`, `render`, `GLSwapBuffers`, `SetvSync`,
`emulator_on_window_resize`, `gui_points_*`, `render_ui_overlays`, …).

1. The GL implementations inside `aae/aae/aae_video/` are renamed
   (`init_gl` → `glchain_init`, etc.) — internal renames, invisible outside
   the directory.
2. A new TU `aae/aae/aae_video/renderer_dispatch.cpp` defines the original
   names and routes to `glchain_*` or `vkchain_*` based on the startup choice.

The definitive dispatch surface is enumerated at planning time by the linker:
every function defined in `aae_video/` that is referenced from outside it
(~18 functions by header inspection). `GLSwapBuffers`/`SetvSync` keep their
names (they are the dispatch points) even though the VK route makes the names
literal misnomers; renaming call sites is churn this phase does not take on.

### 3.3 Directory layout

```
aae/system/graphics/vk/     sys_vk.{h,cpp}          Bosconian donor, made platform-neutral (§3.5)
                            sys_texture_vk.{h,cpp}   donor TEX, VK backend only
aae/aae/aae_video_vk/       vulkan_renderer.{h,cpp}  chain orchestration (mirror of opengl_renderer)
                            fast_poly_vk.{h,cpp}     raster path (Bosconian)
                            vector_draw_vk.{h,cpp}   beam renderer (SpriteTest + §2.3 fixes)
                            render_target_vk.{h,cpp} offscreen RT + compositor (SpriteTest)
                            screen_quad_vk.{h,cpp}   fullscreen/rect passes
aae/shaders/vk/             *.vert / *.frag          ALL GLSL sources, canonical home (§6)
```

### 3.4 Frame mapping

The emulator loop keeps its exact shape (`aae_emulator.cpp` is untouched):

| Loop call | GL chain (today) | Vulkan chain |
|---|---|---|
| `set_render()` | bind FBO, set ortho | `VK_BeginFrame`: slot fence, acquire, open pass/RT |
| `render()` | dispatch `final_render` / `final_render_raster` | record game draws, post-effects, artwork, overlays; composite to swapchain |
| `GLSwapBuffers()` | `wglSwapBuffers` | `VK_EndFrame`: barrier, submit (`vkQueueSubmit2`), present |
| `SetvSync(b)` | WGL swap interval | set `ctx.vsync`, recreate swapchain (FIFO vs MAILBOX/IMMEDIATE) |

Swapchain out-of-date at acquire or present → `VK_RecreateSwapchain`, skip the
frame's present; minimized/zero-extent windows defer recreation (donor
behavior). `FrameLimiter` throttling semantics are identical for both chains.

### 3.5 The one core adaptation: `sys_vk` goes platform-neutral

`VK_Init(ctx, HWND)` becomes `VK_Init(ctx, IPresentSurface&)`. The Win32
touch points in the donor core (4 total) are replaced by the window contract
that `sys_window.h` already exposes:

| Donor Win32 usage | Replacement |
|---|---|
| `vkCreateWin32SurfaceKHR(hwnd)` | `IPresentSurface::CreateVkSurface(instance, &surface)` |
| hardcoded instance extension list | `IPresentSurface::RequiredVkInstanceExtensions(&count)` |
| `GetClientRect` extent fallback | `IPresentSurface::GetDrawableSize(&w, &h)` |
| `#include <windows.h>` / `vulkan_win32.h` in `sys_vk.h` | removed; `sys_vk.h` includes only `<vulkan/vulkan.h>` |

This is what makes Phase 4b a windowing task instead of a renderer task.

Leak guard, mirroring the `_WINDOWS_` idiom from Phase 1: core translation
units gain

```c
#ifdef VULKAN_H_
#error "vulkan.h leaked into the emulation core"
#endif
```

so "no Vulkan headers in the emu core" is a build-time invariant, not a grep.

---

## 4. The two chains' internals

### 4.1 Raster path

Emu side unchanged: drivers render into `main_bitmap`; the orientation/pen
loop (today `raster_poly_update`) is backend-neutral and submits via
`addPoly()`. VK consumption:

1. `Fpoly` renders the quads into an offscreen `RenderTarget` at game
   resolution × prescale.
2. **CRT post-effects** as RT→RT fullscreen passes: the mono-monitor and
   color-monitor pipelines (beam blur H/V, halation, scanlines) port from
   GLSL 330 nearly verbatim (uniforms → push constants/UBO).
3. **Layout/artwork composite**: `mame_layout` output ("these textures at
   these rects") draws via `RecordRect` with VK textures.
4. **Present**: `RenderTargetCompositor` blits to the swapchain with
   aspect-fit / integer-scale and 90° rotation (replacing `screen_rect`'s
   role), covering vertical games on horizontal monitors.

### 4.2 Vector path

The emu-side seam is untouched — `add_line`/`add_tex` exactly as OSD-contract
spec §4.5 defined. VK consumption is `VectorDrawVK` recording into an SSAA
`RenderTarget`, plus:

- **Phosphor persistence / trails** (`vectrail`): RT `Begin(clear=false)`
  LOAD path + a decay pass. First frame force-clears (donor hardening).
- **Glow/halation** (`vecglow`): beam shaders carry per-line core+bloom; the
  screen-space halation blur chain (bright-pass, separable H/V blur,
  composite) is built from `RenderTarget` + screen-quad passes, porting the
  existing GL blur shaders. This is the one subsystem with no VK donor.
- **Textured shots** (`shots_textured=1`): legacy path draws through the VK
  texture system; procedural shots use the donor shot pipeline.
- Vector fonts are line-based and flow through the beam renderer for free.

### 4.3 GUI and overlays

The front-end menu, starfield (`gui_points_*` — a VK point/instanced-quad
pipeline; donor `debug_draw_vk` is the reference), pause/menu/exit-confirm
overlays, and in-game message rendering each get a VK equivalent behind the
same dispatch names.

### 4.4 Textures and snapshots

GL chain keeps `texture_handler`/`sys_texture` untouched. VK chain uses the
donor `TEX` system (VK backend) for artwork PNGs, scanline/overlay textures,
and shot textures; artwork load sites dispatch on the active backend. F12
snapshots use the donor swapchain-readback `TEX::Snapshot`.

---

## 5. Configuration and error handling

- `[main] renderer=opengl|vulkan`, default `opengl`. GL remains the default
  until Vulkan parity is verified; flipping the shipped default is a separate,
  later decision.
- `-renderer <name>` cmdline override.
- `vk_validation=1` (ini, default 0) enables validation layers + debug
  messenger for debugging sessions.
- **Failure policy:** if Vulkan is requested but init fails (no loader, no
  1.3 device, no sRGB surface format), log the specific reason, show the
  standard message popup, and **fall back to the GL chain for the session**.
  The ini is not rewritten — a user who fixes their driver gets Vulkan on the
  next launch.
- Pipeline cache persisted via `VK_CreatePipelineCache`/`VK_SavePipelineCache`
  next to the config files — meaningful startup win on the Pi.

---

## 6. Shaders

All Vulkan GLSL sources live in `aae/shaders/vk/`, committed as **source**.
No hand-compiled `.spv` in the tree (the donor repos' practice of committing
`.spv` with missing sources does not carry over).

Inventory:

| Shader | Source status |
|---|---|
| `vector_line_vk`, `vector_disc_vk`, `vector_shot_vk` | sources exist (SpriteTest `sys_graphics\`) |
| `screen_quad_rect_vk`, `screenquad`, `render_target_composite` | sources exist (SpriteTest `data\shaders\`) |
| `fast_poly_vk` | sources exist (Bosconian `x64\Release\shaders\fast_poly_vk.{vert,frag}`) — copy, don't reconstruct |
| mono CRT, color CRT, scanline, glow blur chain, gui points, overlays | **new ports** from the GL 330 shaders |

Build step:

- **Windows:** vcxproj CustomBuild rule runs
  `%VULKAN_SDK%\Bin\glslangValidator.exe -V` per `.vert`/`.frag` into
  `$(OutDir)shaders\vk\`. Editing a shader rebuilds like any source file.
  The Vulkan SDK include/lib paths are declared **explicitly in the project**
  (the donor's invisible per-machine `.user.props` dependency does not carry
  over).
- **Linux:** CMake `add_custom_command` with `glslangValidator` (or `glslc`).
- Runtime loads `.spv` via the existing `Shader_SetPath` convention.

---

## 7. Platform sequence

### Phase 4a — Windows to full parity

Implement `Win32PresentSurface::CreateVkSurface` for real (its
`RequiredVkInstanceExtensions` list is already correct). All of §3–§6 lands
and is verified on Windows against the GL chain per §9.

### Phase 4b — Linux, then Pi 5

- The Phase-3c X11 window gains a Vulkan present surface
  (`VK_KHR_xlib_surface`) as a sibling of `GlxPresentSurface`; with
  `renderer=vulkan` no GL/GLX context is created at all.
- CMake builds the chain + shader step.
- Verify desktop Linux first, then Pi 5.
- **Pi 5:** confirm via `vulkaninfo` that the installed Mesa reports Vulkan
  1.3 with `dynamicRendering` + `synchronization2`; record the minimum Mesa
  version. Fill-rate is the constraint: SSAA factor and blur-chain resolution
  stay config-tunable, not hardcoded.

---

## 8. Non-goals

- No runtime renderer switching (startup selection only).
- No change to emulation behavior, timing, throttling, or audio.
- No GL-chain refactoring beyond the internal implementation renames in §3.2.
- No renderer-interface abstraction (may be extracted later, from two working
  chains).
- No change to the shipped default renderer in this phase.
- No Teensy work (its `Presentation()` is `nullptr`; the dispatch never
  routes to a renderer there).
- No Wayland surface (X11 covers the Pi 5 desktop today; Wayland is a later
  addition behind the same `IPresentSurface` seam).

---

## 9. Verification

House style: the build plus a runtime pass is the test.

1. **Build:** MSBuild x64 Release exit 0, no new warnings beyond the known
   five. Later: Linux CMake build clean.
2. **Game matrix**, run twice (`renderer=opengl`, `renderer=vulkan`),
   comparing F12 snapshots side by side:

   | Game | Covers |
   |---|---|
   | `asteroid` | B/W vector, glow, trails |
   | `tempest` | color vector |
   | `bzone` | sample-heavy vector |
   | `cchasm` | `add_tex` path |
   | `pacman` | raster, vertical rotation |
   | `warlords` | overlay art + scanline override |
   | one bezel/layout game | artwork composite |

3. **GUI pass:** menu navigation, starfield, KEY CONFIG rebind, pause/exit
   overlays — both renderers.
4. **Window ops** per renderer: resize, borderless-fullscreen toggle, vsync
   on/off, F10 unthrottle, F12 snapshot, minimize/restore (zero-extent
   swapchain path).
5. **GL regression gate:** the `renderer=opengl` runs must be pixel-identical
   to a pre-change build.
6. **Leak guards** (§3.5) compile clean.

---

## 10. Risks

| risk | likelihood | mitigation |
|---|---|---|
| The `final_render` compositing logic has undocumented behaviors (Warlords-style hacks) that the VK re-implementation misses | medium | The game matrix (§9) includes `warlords` and a layout game specifically; visual diff per game before calling parity |
| Pi 5 Mesa in the field reports < 1.3 or lacks a required feature | low | `vulkaninfo` gate at 4b start; minimum Mesa version documented; no code fallback planned (decision on record) |
| Donor `VectorDrawVK` defects beyond the three documented ones | medium | The three fixes come with a known-good reference (`fast_poly`); beam output is visually compared against the GL beam renderer, which shares its math |
| sRGB-only swapchain assumption fails on some Linux driver | low | surface-format check already fails loudly at init → GL fallback path (§5) |
| Two compositing implementations drift over time | accepted | Cost of Approach A, accepted on record; a shared interface may be extracted later from two working chains |

---

## 11. Decisions on record

| decision | value | source |
|---|---|---|
| Switch mechanism | startup-time, `renderer=` in `aae.ini` (+ cmdline override) | user, 2026-07-31 |
| Parity bar | full — "all shaders, all rendering", Windows then Linux | user, 2026-07-31 |
| Vulkan baseline | 1.3, `dynamicRendering` + `synchronization2`, no 1.2 fallback | user, 2026-07-31 ("plan on raspberry pi 5 and 1.3") |
| Pi target | Raspberry Pi 5 | user, 2026-07-31 |
| Architecture | Approach A — parallel chain behind thin dispatch; GL untouched | user, 2026-07-31 |
| Raster VK donor | Bosconian mini-port (newest `sys_vk`/`fast_poly`) | user, 2026-07-31 |
| Vector VK donor | SpriteTest `VectorDrawVK` + backport doc fixes | user, 2026-07-31 |
| Default renderer | stays `opengl` this phase | design, accepted with §5 |
