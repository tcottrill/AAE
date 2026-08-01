# Vulkan Phase 4a — Plan 4: Raster Post-Processing + Artwork

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Vulkan raster path reaches visual parity with GL: game renders into an offscreen RenderTarget, the scanline-overlay and mono/color CRT shader passes run over it, the MAME layout system composites screen + artwork (backdrop/overlay/bezel) to the swapchain with rotation and aspect handling, and `FpolyVK` gets the per-frame VBO fix from the engine's Vulkan bug catalog.

**Architecture:** Import the SpriteTest `RenderTarget` (VK path only) as the img5a/img5b equivalents — with a mip-chain extension, because the CRT halation samples `textureLod` and `Layout_Render` minifies. Import Bosconian's `ScreenQuadVK` for rect/fullscreen passes. Port the three GL post shaders (scanline multiply, mono CRT, color CRT) and the two layout shaders (single-tex, dual-tex overlay) from their inline GLSL sources to Vulkan GLSL. A small VK texture loader (stb_image → `VK_CreateTextureRGBA8_*`, which already lives in sys_vk) replaces GL `load_texture`/`Layout_LoadTextures` under Vulkan — deliberately NOT the donor `TEX` class (its backend-dispatch machinery is unneeded; sys_vk's texture helpers ARE donor code). Two user gates: Gate A after the RT restructure (pacman identical through the new plumbing), Gate B at parity (CRT + artwork).

**Key donors:**
- `C:\SourceEngineAlpha\SpriteTestVulcan28_vulkan_working_copy\sys_render\render_target.h` + `render_target_vk.cpp` (VK RT: COLOR_ATTACHMENT|SAMPLED image, Begin=barrier→ATTACHMENT+`vkCmdBeginRendering` publishing `ctx.activeColorFormat`, End=barrier→SHADER_READ; first-use forces clear)
- `C:\Source2026\Bosconian\Bosconian\sys_graphics\screen_quad_vk.{h,cpp}` (newest ScreenQuadVK; self-contained RecordRect with per-format pipeline variants, 64-slot per-frame VBO+descriptor ring, `OnFrameBegin(fi)`)
- Shaders `screen_quad_rect_vk.vert/.frag` — sources in `C:\Source2026\Bosconian\x64\Release\shaders\`
- The GL shader sources to port — ALL inline in `aae/aae/aae_video/shader_definitions.h`: `scanlineMultiplyVert/Frag` (:195/:212), `monoMonitorVert` (:336), `monoMonitorFrag` (:353-437), `colorMonitorFrag` (:445-563); layout shaders inline in `mame_layout.cpp` `InitLayoutShaders()` (:73-204): shared VS (:80-89, NO matrix — CPU-computed NDC), `fsSingleSrc` (:94-104), `fsDualSrc` (:121-145)

**Verified anchors (2026-08-01, from the GL-chain exploration — treat as the porting contract):**
- GL raster flow (`final_render_raster`, opengl_renderer.cpp:1483-1605): game in `img5a` (fbo_raster, visible_area×prescale) → scanline overlay multiplied IN PLACE into img5a (`DST_COLOR/ZERO` blend, tiled UVs u=rw/scan_x) → mips regenerated on img5a → mono OR color CRT pass into `img5b` (fbo_mono, sized to the on-screen game rect each frame, blend DISABLED) → mips regenerated on img5b → `Layout_Render(view, screenTex, clientW, clientH, vattr)` composites straight to backbuffer (backdrop=alpha blend, screen=ADDITIVE `ONE/ONE` with optional dual-tex overlay multiply, bezel=alpha), rotation via CPU-side NDC corner rotation.
- `mono_monitor_active` = `config.mono_enable && (vattr & VIDEO_TYPE_RASTER_BW)`; `color_monitor_active` = no overlay texture selected && `config.color_enable` && `RASTER_COLOR` (aae_stricmp on raster_effect). Overlay texture and color shader are mutually exclusive.
- Mono uniforms: uSrcSize, uLodBias=log2(prescale), uBlurH/V, uHalation, uHalRadius, uScanline, uContrast, uBright, uTint (k_monoTints P4/P1/P3, opengl_renderer.cpp:1265-1269). Color adds uConverge, uSaturation, uMaskType(int), uMaskStrength, uMaskScale, drops uTint.
- **Mip dependencies are load-bearing:** img5a mips after scanline pass, before CRT (halation textureLod at lod=log2(max(uHalRadius,1))+uLodBias); img5b mips after CRT (layout minifies).
- Scanline overlay texture: `make_single_bitmap(&g_scanrezTex, config.raster_effect, "aae.zip", 0)` — a PNG from the shared aae.zip (opengl_renderer.cpp:382-418); tri-state menu OFF/SHADER/texture (menu.cpp:1000-1050).
- Layout textures: `Layout_LoadTextures` (mame_layout.cpp:510-569) — zip or loose PNG via stb, **flip-Y OFF**, no mips; `load_texture` (texture_handler.cpp:106-219) — **flip-Y ON**, mips. The VK loader must support both flip conventions.
- Layout draw contract (`Layout_Render`, mame_layout.cpp:746-1172): CPU computes NDC quads (`LayoutQuadVertex {px,py,tx,ty}`), rotation = rigid corner rotation (`DrawQuadCorners`), aspect-override sub-viewport, `u_overlayUVXform` maps screen UV into overlay space with out-of-range→white; `g_layoutScreenPxW/H` recorded per frame → drives the CRT RT resize. Layout path is ALWAYS active for raster games (default-screen fallback, :1422-1454).
- Availability flags set by BOTH art paths: `g_artworkAvailable/g_overlayAvailable/g_bezelAvailable/g_artcropAvailable` (mame_layout.cpp:1385-1396 and texture_handler.cpp load_artwork:496-536); VK must set them from its layout load so the menu works.
- FpolyVK single-VBO race (bug catalog entries 3/12): one persistently-mapped VBO memcpy'd at record time — frame N's upload can overwrite what frame N-1's GPU read still consumes. Fix pattern = per-frame-slot VBO arrays + stale-buffer retirement (reference implementations: `UIRendererVK` in the SpriteTest engine, catalog entry 3; the catalog's entry 12 append-discipline variant is not needed while Fpoly renders once per frame — note it in a comment).
- Pipeline-format discipline (donor convention, MOST important): every pipeline must be built against `VK_ActiveColorFormat(ctx)` — the RT passes publish their format; FpolyVK currently builds against `ctx.swapchainFormat` and MUST switch to a format parameter when it starts rendering into the RT.
- `config.raster_effect` aliasing hazard: menu.cpp assigns it from a function-local static std::string's c_str() — the VK path must strdup/copy when caching, never store the pointer.

**Build command:** as Plans 1-3 (canonical MSBuild, Release+Debug x64, no new warnings beyond the known five, exe timestamps must postdate last edit). **Agents never launch aae.exe.** New shaders are added to the Plan 1 CustomBuild rule and must be copied to the asset tree at gates.

**Plan sequence:** Plan 4 of 6. After this: (5) vector path, (6) GUI/overlays/snapshots + full matrix.

---

### Task 1: FpolyVK per-frame VBO fix

**Files:** Modify `aae/aae/aae_video_vk/fast_poly_vk.{h,cpp}`

- [x] Replace the single `m_vbo/m_vboMem/m_mappedVBO/m_vboCapacityVerts` with per-frame-slot arrays sized `VkContext::kFramesInFlight`, plus a per-slot stale-buffer list drained on next visit to the slot (after `VK_BeginFrame`'s fence wait proves the GPU is done). Pattern per bug-catalog entry 3; `EnsureVBOCapacity(ctx, frameIndex, wantVerts)` grows ONLY the current slot, pushing the old buffer to that slot's stale list instead of destroying it immediately. `Render` uploads to and binds `m_vbo[frameIndex]`. `Shutdown` destroys all slots + drains all stale lists. Add a comment noting entry 12 (append discipline) is not needed while Render is called once per frame, and what to do if that changes.
- [x] Add a `VkFormat colorFormat` field to `FastPolyVKCreateInfo` (default `VK_FORMAT_UNDEFINED` = use `ctx.swapchainFormat`, preserving Plan 3 behavior) and build the pipeline against it — Task 3 passes the RT format. (Implemented as a CreateInfo field rather than a separate `Init` parameter, matching the existing `flipViewportY`/`initialCapacityVerts` pattern on the same struct.)
- [x] Build both configs; commit `"fix(vk): FpolyVK per-frame-slot VBOs (bug catalog entry 3) + pipeline color-format parameter"`.

### Task 2: Import RenderTargetVK + ScreenQuadVK

**Files:** Create `aae/aae/aae_video_vk/render_target_vk.{h,cpp}` (from SpriteTest `sys_render/render_target.h` + `render_target_vk.cpp`, VK-only strip), `aae/aae/aae_video_vk/screen_quad_vk.{h,cpp}` (from Bosconian), shaders `aae/shaders/vk/screen_quad_rect_vk.{vert,frag}` (copy from Bosconian `x64/Release/shaders/`); register everything (vcxproj CustomBuild pairs too), recount CMake drift check.

- [ ] Strip the donor RenderTarget to a VK-only `RenderTargetVK` class: keep CreateInfo (width/height/filter/colorFormat), `Init/Shutdown/Resize/Begin(clear,r,g,b,a)/End`, `VK_GetColorView/VK_GetSampler/GetWidth/GetHeight`; drop the GL backend, the `rc` RenderContext indirection (take `VkContext&` + `VkCommandBuffer` directly — match our vkchain calling style), and the compositor class (we composite via layout/ScreenQuad instead). PRESERVE: barrier discipline (ATTACHMENT_OPTIMAL on Begin, SHADER_READ_ONLY on End), `ctx.activeColorFormat` publish/reset, first-use forced clear.
- [ ] EXTEND with mips: CreateInfo gains `int mipLevels` (1 = donor behavior). When >1: image created with full chain + TRANSFER_SRC|TRANSFER_DST usage, sampler with LINEAR_MIPMAP_LINEAR and maxLod, and a new `GenerateMips(ctx, cmd)` method recording the blit-cascade downsample with per-level barriers — use sys_vk's `VK_BuildRGBA8Texture` mip cascade (sys_vk.cpp) as the reference implementation, adapted to run per-frame OUTSIDE a rendering pass (must be called between End() and the next Begin/composite — assert `activeColorFormat == UNDEFINED`).
- [ ] Import Bosconian ScreenQuadVK with collision-check renames if needed; only the self-contained `RecordRect` path + `OnFrameBegin` are required (the legacy ctx-owned fullscreen pipeline path can be dropped if it references the sys_vk functions excluded in Plan 2 — check and report).
- [ ] Build both configs (compile-only); commit `"feat(vk): import RenderTargetVK (with mip chain) + ScreenQuadVK"`.

### Task 3: Restructure the raster path through the RT

**Files:** Modify `aae/aae/aae_video_vk/vulkan_renderer.cpp`, `aae/system/graphics/vk/sys_vk.cpp` (if a frame-pass hook is needed — see below)

- [ ] New flow in the raster branch: `EnsureRasterRenderer` also owns a `RenderTargetVK s_rtGame` at `visible_area(oriented) × config.prescale`, RGBA8_UNORM, mipLevels = full chain. Per frame: **the game RT pass must run BEFORE the swapchain frame pass opens** — restructure: `vkchain_set_render` records the RT pass into the frame's command buffer BEFORE `VK_BeginFrame`'s swapchain pass... IMPORTANT ORDERING PROBLEM: `VK_BeginFrame` acquires AND opens the swapchain pass in one call, and emu rendering (`raster_emit`) happens after set_render. RESOLUTION (implement exactly this): split usage — `vkchain_set_render` calls `VK_BeginFrame` as today (swapchain pass opens, cheap); `vkchain_render` then: (1) `vkCmdEndRendering` the swapchain pass temporarily? NO — instead use the sys_vk escape hatch: add to sys_vk a `VK_SuspendFramePass(ctx, cmd)` / `VK_ResumeFramePass(ctx, cmd, imageIndex)` pair that ends the swapchain dynamic-rendering pass and re-begins it with LOAD_OP_LOAD (attachment already cleared this frame), so subsystem passes (RT render, mip gen) can run mid-frame. Keep both functions small, comment the contract, and set/restore `ctx.activeColorFormat` correctly. In `vkchain_render` (raster): SuspendFramePass → `s_rtGame.Begin(clear black)` → emit+FpolyVK Render into RT (ortho = RT dims, full-RT viewport, pipeline built against RT format) → `s_rtGame.End` → `s_rtGame.GenerateMips` → ResumeFramePass → composite RT→swapchain via ScreenQuadVK::RecordRect into the aspect-fit letterbox rect (reuse the Plan 3 letterbox math; rotation handled in Task 6 by the layout port — for THIS task keep non-rotated RecordRect and note it).
- [ ] FpolyVK now inits with the RT format; ClearViewportRect (full-RT render); its flipViewportY/yFlip conventions re-verified against the RT composite (trace one pixel as in the Plan 3 review — report the trace).
- [ ] Build both configs; commit `"feat(vk): raster renders via mipped RenderTargetVK with suspend/resume frame pass + RecordRect composite"`.
- [ ] **GATE A (user):** pacman + galaga under Vulkan — visually IDENTICAL to Plan 3 (same orientation/aspect/colors, no flicker); resize/fullscreen/minimize; GL regression quick check. Copy exe + shaders/vk to asset tree first.

### Task 4: VK texture loader + scanline overlay pass

**Files:** Create `aae/aae/aae_video_vk/vk_texture_loader.{h,cpp}`; create `aae/shaders/vk/raster_scanline_vk.{vert,frag}`; modify `vulkan_renderer.cpp` (scanline pass + real `vkchain_init_raster_overlay`/`vkchain_shutdown_raster_overlay`); CustomBuild entries.

- [ ] Loader API: `bool VkTex_LoadFromArchiveOrFile(VkContext&, const char* filename, const char* archname, bool flipY, bool mips, bool srgb, VkTexture* out)` — mirrors `load_texture`'s search semantics by REUSING the existing zip helpers (`load_zip_file`/`get_last_zip_file_size` — same ones texture_handler and mame_layout use; verify their header and that they're GL-free) + `stbi_load(_from_memory)` with `stbi_set_flip_vertically_on_load(flipY)`, then `VK_CreateTextureRGBA8_UNORM_FromPixels` / `_SRGB_` (+`VK_BeginUpload/VK_EndUpload` if the sys_vk API requires it — check the imported header). Also `VkTex_MakeSingleBitmap(...)` replicating `make_single_bitmap`'s 4-step search (texture_handler.cpp:324-428 — read it and mirror the path order exactly).
- [ ] `vkchain_init_raster_overlay`: mirror glchain's logic (opengl_renderer.cpp:382-418): bail on NONE/vector; load `config.raster_effect` from "aae.zip" via VkTex_MakeSingleBitmap (flipY matching what the GL path's flip + quad conventions produce on screen — determine by tracing, report); COPY the raster_effect string (aliasing hazard above). Store as a `VkTexture s_scanrezTex` + loaded flag.
- [ ] Scanline pass shaders: port `scanlineMultiplyVert/Frag` (shader_definitions.h:195-227) to Vulkan GLSL 450: vert takes NDC fullscreen quad with tiled UVs computed from push constants {rw, rh, scanTexW, scanTexH}; frag samples and outputs texel; the MULTIPLY is the pipeline blend state `dstColor*src` (`VK_BLEND_FACTOR_DST_COLOR`/`VK_BLEND_FACTOR_ZERO`) matching GL `DST_COLOR/ZERO`. Sampler REPEAT + NEAREST (the GL path sets exactly that). Small dedicated pipeline in vulkan_renderer.cpp or a tiny `scanline_pass_vk` helper — built against the RT format, drawn into `s_rtGame` between the game draw and `End()` (same pass, after FpolyVK — order matters: game first, multiply second).
- [ ] Gate the GL-only `init_raster_overlay` dispatch correctly: the dispatch already routes to vkchain — now it does real work; ensure `aae_emulator.cpp`'s call ordering delivers it after vkchain_init per game (it does — :930).
- [ ] Build both configs; commit `"feat(vk): VK texture loader + scanline overlay multiply pass"`.

### Task 5: CRT shader ports (mono + color)

**Files:** Create `aae/shaders/vk/raster_mono_crt_vk.{vert,frag}`, `aae/shaders/vk/raster_color_crt_vk.frag`; create `aae/aae/aae_video_vk/crt_pass_vk.{h,cpp}`; modify `vulkan_renderer.cpp`; CustomBuild entries.

- [ ] Port `monoMonitorVert` (:336) + `monoMonitorFrag` (:353-437) and `colorMonitorFrag` (:445-563) VERBATIM in math — copy the fragment bodies, adapt only: `#version 450`, explicit locations, `layout(binding=0) uniform sampler2D uTex` → set 0 binding 0 combined sampler, the uniform block → push constants (`layout(push_constant)`) sized ≤128 bytes: pack {vec2 uSrcSize; float uLodBias, uBlurH, uBlurV, uHalation, uHalRadius, uScanline, uContrast, uBright; vec3 uTint(+pad); float uConverge, uSaturation, uMaskStrength, uMaskScale; int uMaskType} — count bytes, if >128 fall back to a per-frame UBO (report which). `gl_FragCoord` usage in the color mask ports as-is. `textureLod` works unchanged (RT has mips as of Task 2).
- [ ] `crt_pass_vk`: owns a `RenderTargetVK s_rtCrt` (the img5b equivalent) resized per frame to the layout screen-pixel size (Task 6 provides `Layout_GetScreenPixelSize` — until Task 6 lands use the letterbox rect size; leave a marked TODO consumed by Task 6), two pipelines (mono/color) built against s_rtCrt's format, blend DISABLED (straight replace, matching GL), fullscreen NDC quad sampling s_rtGame with LINEAR_MIPMAP_LINEAR. After the pass: `s_rtCrt.GenerateMips`. Selection mirrors `mono_monitor_active`/`color_monitor_active` (port the two predicates into vulkan_renderer.cpp using the same config fields + vattr tests, aae_stricmp on raster_effect).
- [ ] Composite source becomes s_rtCrt when a CRT pass ran, else s_rtGame (mirrors `screenTex = img5b` vs img5a).
- [ ] Build both configs; commit `"feat(vk): mono/color CRT shader passes over the game RT"`.

### Task 6: Layout composite port

**Files:** Create `aae/aae/aae_video_vk/layout_vk.{h,cpp}`; create `aae/shaders/vk/layout_single_vk.{vert,frag}` + `aae/shaders/vk/layout_dual_vk.frag`; modify `aae/aae/aae_video/mame_layout.cpp` (backend-split hooks), `vulkan_renderer.cpp`, `aae/aae/aae_emulator.cpp` (ungate `Layout_LoadForGame` for VK); CustomBuild entries.

- [ ] Split `mame_layout.cpp` minimally: the PARSING + geometry + view/flag logic is backend-neutral and stays; the GL-only parts are `Layout_LoadTextures` (stb→glTexImage), `InitLayoutShaders`, and `Layout_Render`'s GL draw calls. Add a narrow seam: `Layout_LoadTextures` routes on `active_renderer()` — GL branch unchanged, VK branch loads the same stb pixels into `VkTexture`s via the Task 4 loader (flip-Y OFF to match this loader's convention) storing handles in a parallel VK texture table; `Layout_FreeTextures` frees the right backend's set. `Layout_Render` routes at the top: VK branch calls `LayoutVK_Render(view, srcView, srcSampler, winW, winH, vattr)` in layout_vk.cpp. The CPU-side NDC/rotation/aspect code MOVES to shared helpers both branches call (extract, don't duplicate — same discipline as raster_emit). Report the exact split.
- [ ] `layout_vk`: shaders ported from `InitLayoutShaders` (VS :80-89 pass-through NDC; `fsSingleSrc` :94-104; `fsDualSrc` :121-145 with `u_overlayMode`+`u_overlayUVXform` as push constants); three pipeline blend variants against the swapchain format: alpha (backdrop/bezel), ADDITIVE `ONE/ONE` (screen), and dual-tex uses additive too (matches GL: screen draw is additive whether single or dual). Vertex = `{px,py,tx,ty}` quads streamed per draw (ScreenQuadVK's per-frame ring is the reference; a small dedicated ring here is fine). Draws happen INSIDE the resumed swapchain frame pass (after ResumeFramePass), replacing the Task 3 RecordRect composite — rotation now comes free from the shared corner math.
- [ ] Ungate `Layout_LoadForGame` at aae_emulator.cpp (:955-957) for VK (remove the RENDERER_OPENGL condition — both backends now load; the inner texture load routes). Availability flags (mame_layout.cpp:1385-1396) now work under VK. `load_artwork` (legacy art_tex path) STAYS gated — layout covers raster composite; legacy art is vector/Plan 6 territory (note in code).
- [ ] CRT RT sizing switches to the real `Layout_GetScreenPixelSize` (clears Task 5's TODO).
- [ ] Build both configs; commit `"feat(vk): MAME layout composite - shared NDC geometry, VK single/dual-tex pipelines, VK layout textures"`.

### Task 7: GATE B (user)

Copy exe + `shaders/vk/*.spv` to the asset tree, then:
- [ ] pacman + galaga: identical to Gate A, now composited by the layout path (default screen). Aspect override configs (`game_aspect`) behave like GL.
- [ ] A BW raster game with `mono_enable=1` (e.g. one of the VIDEO_TYPE_RASTER_BW titles you use for the mono shader) — CRT effect matches GL side by side (blur/halation/scanline/tint).
- [ ] A color raster game with `color_enable=1` and `raster_effect=NONE` — color CRT matches GL (convergence/mask).
- [ ] `raster_effect=scanlines.png` (or aperture4x6.png) — scanline overlay matches GL.
- [ ] warlords — overlay artwork composites correctly (dual-tex path).
- [ ] A system-rotated config (`-ror` on a raster game) — rotation correct under VK.
- [ ] Resize/fullscreen/minimize on the CRT-enabled game; GL regression pass on the same titles.

---

## Self-review notes (write time)

- Spec §4.1 fully covered by Tasks 3-6 (RT, CRT passes, layout composite, aspect/rotation present); §4.4's "donor TEX" replaced by the leaner sys_vk-helper loader — deviation documented in Architecture with rationale.
- The suspend/resume frame-pass hook (Task 3) is the one new sys_vk surface — kept minimal and commented; alternative designs (restructuring VK_BeginFrame into acquire-only) were rejected to keep Plans 1-3 behavior untouched.
- Known deferred: rotated UI overlays (fbo4 path) and render_ui_overlays → Plan 6; legacy `load_artwork` art_tex path → Plan 5/6; F12 snapshot → Plan 6.
- Every task names its reference implementation for the hard parts (mip cascade → VK_BuildRGBA8Texture; per-slot VBOs → catalog entry 3 / UIRendererVK; search order → texture_handler.cpp lines).
