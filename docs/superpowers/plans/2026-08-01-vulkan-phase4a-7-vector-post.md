# Vulkan Phase 4a — Plan 7: Vector Post Chain (Plan 5 Task 3)

> Lean plan per user directive: implementer self-review + user gate, no separate review agents.

**Goal:** Vector games under `renderer=vulkan` get the GL chain's SSAA render target, phosphor trails (`config.vectrail`), and glow (`config.vecglow`), with pause showing the retained frame (parity with GL's frozen FBO).

**GL reference (opengl_renderer.cpp):** beams → fbo1 1024x1024 ortho(0,1024,0,1024); trail = img1b→img1c quad, blend `ONE_MINUS_DST_COLOR/SRC_ALPHA`, tint a=0.825/0.86/0.93 (vectrail 1/2/3) or rgb=0.95,a=1 (else); glow = img1b→512 (fragBlur 3x3, *1.12) → 256 (fragBlur) → 4 ping-pong additive (`SRC_ALPHA/ONE`) passes with fshifta/fshiftb offsets, composite samples img3b; fragMulti: `beam + glow*glowamt + trail*0.25` additively into the game_rect quad with one V flip. GUI is excluded from trail/glow in GL.

### Task 1 (inline, staged commits)
- [ ] New `aae/aae/aae_video_vk/vector_post_vk.{h,cpp}`: owns beam RT (1024*ssaa, ssaa=2, RGBA8 mipped), trail RT (1024 mipped, persistent — never cleared except first use/new game), blur RTs 512/256a/256b; a push-constant quad vert (gl_VertexIndex, no VBO) + blur/tex/multi frags (ports of fragBlur/plain/fragMulti); pipelines: blurCopy (no blend), blurAccum (SRC_ALPHA/ONE), trail (ONE_MINUS_DST_COLOR/SRC_ALPHA) against RGBA8, composite (ONE/ONE) lazily against active format; per-frame descriptor ring (ScreenQuadVK pattern). Unused composite slots bind the beam view; useglow/usefb gated by both config AND RT-ready flags (layout-safe).
- [ ] Shaders `aae/shaders/vk/vector_post_vk.vert`, `vector_post_{blur,tex,multi}_vk.frag` + CustomBuild pairs + CMake + vcxproj/filters (recount drift check).
- [ ] Wire `vulkan_renderer.cpp`: NON-GUI vector games move off direct-to-swapchain onto: suspend pass → beam RT (new g_vectorDrawRT instance, RGBA8 format, ssaa=2, proj=ortho(0,1024,0,1024)) → End+mips → trail pass → glow passes → resume → composite quad at the game_rect/letterbox rect (same GuiBeamMap math), V-flipped UV. GUI keeps the gated direct path (g_vectorDraw) untouched. Paused: skip beam/trail/glow update, still composite retained RTs (restores GL pause parity). New game load: trail cleared once (vkchain_init re-entrant path sets a flag).
- [ ] Documented deviations: in-game menu/pause text rides the beam queue so it gets trail/glow under VK (GL draws overlays post-composite); legacy `shots_textured` still absent under VK.
- [ ] Build Release+Debug, verify exe + new spv ON DISK (paste Get-Item output), staged commits.

### Task 2: GATE (user)
- [ ] Copy exe + shaders/vk to asset tree. `asteroid -renderer vulkan`: trails/glow per ini settings, toggle vectrail 0-3 + vecglow in menu, pause shows frozen frame. `tempest`: color glow. GUI unaffected. GL regression on both.
