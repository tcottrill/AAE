# Vulkan Phase 4a — Plan 6 (pulled forward): GUI / Menu Rendering

> Lean plan per user directive: implementer self-review + user gate, no separate review agents.

**Goal:** The front-end GUI is fully usable under `renderer=vulkan`: menu text, selection, starfield, and game marquee/screenshot images render; navigation works.

**Known state:** GUI driver is VIDEO_TYPE_VECTOR → the beam path (Plan 5) is live for it. Dispatch stubs still empty: `vkchain_gui_points_*` (starfield), `vkchain_vector_hard_clear`. Menu textures draw via GL (`DrawQuad`, texture_handler `fragBasicTex` path) — invisible under VK. Vector-font text (VF / DrawGlyph / Print*) — renderer unknown: investigate whether VF strokes go through beam_add_line (then already visible) or VF's own GL objects (then invisible; port needed).

### Task 1 (one agent, staged commits)
- [ ] INVESTIGATE first (report findings in commit messages): how driver_gui.cpp + menu.cpp draw under GL — VF text path (vector_fonts.cpp: GL-direct or beam?), gui_points (GL point sprites), DrawQuad/marquee textures (texture_handler loads + fragBasicTex quads), glcode_vector_hard_clear_fbo1 semantics (what breaks if no-op under VK?).
- [ ] Minimal VK texture loader (the deferred Plan 4 Task 4 loader, trimmed to GUI needs): stb + zip helpers → VkTexture via sys_vk VK_CreateTextureRGBA8_* + upload. File: aae/aae/aae_video_vk/vk_texture_loader.{h,cpp}.
- [ ] Textured menu quads: route the GUI's textured draws under VK through ScreenQuadVK::RecordRect (already imported; per-frame ring supports 64 rects). Seam at the DrawQuad/menu-image call sites via active_renderer() or a sink, whichever is least invasive — GL path untouched.
- [ ] Starfield: implement vkchain_gui_points_* with a second small FpolyVK instance (points as pointSize quads; per-slot VBOs already solved there) OR a trivial dedicated pipeline — choose the least code.
- [ ] VF text: if beam-fed, verify and done; if GL-direct, route glyph strokes through beam_add_line under VK (vector fonts are line strokes; the beam renderer is live) — least-code path, document.
- [ ] vkchain_vector_hard_clear: no-op with comment if investigation shows it's a GL-FBO hygiene call; else implement equivalent.
- [ ] Build Release+Debug, verify exe + spv timestamps ON DISK (a prior agent misreported — verify with your own Get-Item output pasted in the report), staged commits per piece.

### Task 2: GATE (user)
- [ ] `aae.exe -renderer vulkan` (no game arg → GUI): menu text readable, starfield moving, marquee/screenshot images visible, navigate + launch a game + return, KEY CONFIG rebind. GL GUI regression check.
