# Vulkan Phase 4b — Linux + Raspberry Pi 5

> Lean plan. Phase 4a (Windows) shipped and merged to main (`d80ef7b`).

**Goal:** `renderer=vulkan` works on Linux — first on the WSL2/X11 dev box, then
the Steam Machine, then the Pi 5 (the original motivation: Mesa v3d tops out
near GL/GLES 3.1, below the GL chain's `#version 330 core` shaders).

**What already exists (Phase 3a-3c):** `ISystemWindow`/`IPresentSurface`, an X11
window (`linux_window.cpp`), GLX presentation (`glx_present.cpp`), evdev input,
ALSA audio, and a CMake build that already lists **all 11** `aae_video_vk/*.cpp`
plus `system/graphics/vk/sys_vk.cpp` and the vk include dir. The whole VK chain
is platform-neutral apart from the two gaps below.

### Task 1: make the Linux build produce a working VK chain
- [x] **X11 Vulkan surface.** `GlxPresentSurface::CreateVkSurface` /
      `RequiredVkInstanceExtensions` were Phase-4 stubs returning failure/0.
      Implement per the Win32 pattern (winmain.cpp:933-980): runtime `dlopen`
      of `libvulkan.so.1`, resolve `vkGetInstanceProcAddr`, call
      `vkCreateXlibSurfaceKHR` with the `Display*`/`Window` the class already
      holds from `Attach()`. No link against libvulkan (matches spec sec. 6 /
      the Windows no-`vulkan-1.lib` rule).
- [x] **Shader compilation on Linux.** The `.spv` files are produced by
      MSBuild `CustomBuild` entries, which exist only in `aae.vcxproj` — the
      CMake build never compiled them, so a Linux VK run would find no
      `shaders/vk/*.spv` and fail at pipeline creation. Add an explicit
      per-shader `glslc` rule (listed, NOT globbed, per this repo's rule that
      source lists are never globbed) writing next to the binary.
- [ ] Build on the WSL2 dev box; fix whatever the Linux compiler finds that
      MSVC accepted.

### Task 2: GATE (user) — dev box
- [ ] `./aae asteroid -renderer vulkan` under WSLg/X11. Expect: it may fail at
      device selection if the WSL ICD is inadequate — that is informative, not
      a defeat. `renderer=opengl` must still work (no regression).

### Task 3: real hardware
- [ ] Steam Machine (radeonsi): full verification matrix under Vulkan.
- [ ] Pi 5 (Mesa v3d): the target that needs this. Expect surprises —
      v3d is a tiler; the beam RT is 2048² with a full mip chain and the post
      chain does many full-screen passes. Watch memory and fill cost.

**Constraints carried forward:** a copied binary cannot work (glibc 2.43 dev box
vs ~2.37 SteamOS) — build on the target. UNORM everywhere, never `*_SRGB`.
The GL chain must not regress on either platform.
