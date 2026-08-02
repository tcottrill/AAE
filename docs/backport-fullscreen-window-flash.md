# Backport note: white window-frame flash when launching fullscreen (Win32)

Self-contained writeup for applying this fix to an older AAE tree.
Reference commit in the current tree: `0cb6cd4`, single file
`aae/system/window/winmain.cpp`, +44 / −32.

---

## Symptom

Launching fullscreen (from the command line, or with fullscreen configured)
briefly shows a **bordered 4:3 window** before the game appears. The flash reads
as "brilliant white" and is very noticeable against a dark game.

## Root cause — ordering, not painting

The instinctive diagnosis is "the client area isn't being cleared". That is
almost certainly **already handled** in your tree: the window class is
registered with `hbrBackground = GetStockObject(BLACK_BRUSH)`, `WM_ERASEBKGND`
returns 1, and a black frame is presented before the window is shown.

What actually flashes is the **DWM non-client frame** — the title bar and
border — which is light in the default Windows theme. You cannot paint that
away from inside the client area, because it isn't the client area.

It appears because of the startup ordering:

1. `GenerateFinalWindowSetup(forceWindowed = true)` — the window is created
   **windowed on purpose**, even when fullscreen was requested.
2. The window is created (hidden), the GL/VK context is set up, a black frame
   is presented.
3. **`ShowWindow()` — the windowed, bordered window becomes visible.**
4. Steps that capture `windowedRect` (via `GetWindowRect`) and client size
   (via `GetClientRect`) — needed so ALT+ENTER can restore to a sane window.
5. **`ToggleBorderlessFullscreen()` — only now does it go fullscreen.**

Between (3) and (5) a real bordered window is on screen. That gap is the flash.

The `forceWindowed = true` is **not** a bug — the restore path needs a valid
windowed rectangle captured before any fullscreen toggle. The bug is that the
window is *shown* during that phase.

## The fix

Keep the create-windowed-then-toggle logic exactly as-is. **Defer
`ShowWindow`/`UpdateWindow` until after the fullscreen toggle.** The first
frame the user ever sees is then already borderless and full-screen.

### Preconditions to check in the older tree

1. **The window must be created hidden.** Confirm `WS_VISIBLE` is *not* in the
   style passed to `CreateWindowExW` (in the current tree it appears nowhere in
   `winmain.cpp`). If your older tree creates the window visible, deferring
   `ShowWindow` achieves nothing — remove `WS_VISIBLE` first.
2. Confirm the same shape exists: created windowed → shown → rects captured →
   fullscreen toggled. If your tree already creates the window fullscreen
   directly, this fix does not apply.

### Change

Delete the `ShowWindow`/`UpdateWindow` pair from its position *before* the
rect-capture steps, and re-insert it immediately *after* the fullscreen toggle:

```cpp
    // ... Step 4: capture windowedRect  (GetWindowRect)
    // ... Step 5: capture client size   (GetClientRect)

    // Step 6: apply fullscreen if requested
    g_windowSetup.borderlessFullscreen = false;
    if (requestedFullscreen)
        GetSystemWindow().ToggleBorderlessFullscreen();

    // Step 6b: NOW show the window - the first time it is ever visible.
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    // Assume focus because we just showed the window.
    g_windowSetup.isFocused = true;
```

Move the `isFocused = true` assignment with it — it is asserting "we just
showed the window", so it belongs next to the show.

### Why this is safe

Everything between the old and new show positions works on a **hidden** window:

| Call | Hidden-window safe? |
|---|---|
| `GetWindowRect` | yes |
| `GetClientRect` | yes |
| `SetWindowLong(GWL_STYLE/EXSTYLE)` | yes |
| `SetWindowPos` | yes |

`ToggleBorderlessFullscreen` uses only those, and makes no visibility
assumptions (no `ShowWindow`, no `IsWindowVisible`). Verify that in your tree
before applying — it is the one real risk.

## Also worth checking: a duplicated show block

In the current tree there was a **leftover duplicate** sitting between the two
positions: a second `glClearColor`/`glClear`/`GLSwapBuffers` followed by another
`ShowWindow`/`UpdateWindow`/`isFocused`. The black-frame present it repeated
already happens earlier. If your older tree has the same duplication, delete
it — startup should contain exactly **one** show sequence.

(In the current tree that duplicate also ran GL calls unconditionally, which
became a real problem once a Vulkan path existed with no GL context. An
older GL-only tree would not crash on it, but it is still dead work.)

## Adapting to a pre-Vulkan tree

The reference commit carries a `if (wantVulkan) { ...GDI black fill... }` block
along with the show. **Drop it entirely** on a GL-only tree — its purpose is to
paint the client area black when no swapchain image has been presented yet,
which cannot arise without Vulkan. The GL path already presents a black frame
before the show.

So on an older tree the moved block is just:

```cpp
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    g_windowSetup.isFocused = true;
```

## Verification

- Launch fullscreen from the command line → **no windowed frame flash**.
- Launch windowed → window appears normally, correct size and position.
- ALT+ENTER to fullscreen and back → restores to the correct windowed size.
  (This is the one that would catch a mistake: it depends on `windowedRect`
  being captured correctly while the window was hidden.)
- Multi-monitor, if applicable: fullscreen should land on the intended monitor.
