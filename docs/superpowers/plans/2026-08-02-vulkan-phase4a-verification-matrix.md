# Vulkan Phase 4a — Verification Matrix

Every feature ported to the Vulkan chain, grouped **by game** so one launch
covers many checks. Run each with `-renderer vulkan`, then the same game with
`-renderer opengl` for the side-by-side. Set `renderer=` in aae.ini or pass it
on the command line.

Legend: [ ] untested · [x] passed · [!] defect (add a line to KNOWN ISSUES)

---

## 1. `pacman` — color raster, no layout

- [ ] Game renders, correct colors/brightness vs GL (UNORM parity)
- [ ] Color CRT monitor: shadow mask visible, mask pitch matches GL
- [ ] Mask type 0/1/2 (aperture / slot / dot triad) via VIDEO menu
- [ ] Scanline overlay on/off  *(known issue: scale)*
- [ ] TAB menu, PAUSED text, exit dialog, FPS counter
- [ ] Pause shows frozen frame with overlays live on top
- [ ] `-ror` / `-rol` / 180: image turns, overlays turn with it
- [ ] `-ror` + CRT on: monitor pass still applies, mask turns with image
- [ ] F12 snapshot: file in `snap/`, upright, correct colors, opaque
      (VK reads back the swapchain; GL's bottom-up flip is deliberately
      NOT applied — an upside-down PNG means the row order regressed)
- [ ] Window resize + minimize/restore

## 2. `invaders` — B/W raster + layout + color gel  *(the headline fix)*

- [ ] Layout composites: backdrop behind, color gel bands, bezel on top
- [ ] **Mono CRT on: beam/halation/tint visible THROUGH the gel** (this was
      dead before `44089b1`, and before that the gel multiplied the raw game)
- [ ] Mono tint presets: P4 white / P1 green / P3 amber
- [ ] Mono blur, halation, contrast, brightness sliders respond
- [ ] `-ror` + mono + gel all at once (the full-stack composition)
- [ ] Cocktail view if you use it
- [ ] vs GL side-by-side  *(known issue: mono differs — capture specifics)*

## 3. `circus` (or `breakout`, `pong`) — B/W raster, NO layout

- [ ] Mono CRT with no gel in the way — the clean mono reference
- [ ] Compare this against GL first when chasing the mono difference:
      isolates the shader from the layout/gel path

## 4. `centiped` — color raster + layout bezel (no gel)

- [ ] Bezel frames the game, correct scale/position
- [ ] `artcrop` on/off, bezel zoom
- [ ] Color CRT + bezel together

## 5. `asteroid` — vector + textured shots + overlay artwork

- [ ] Beams render; ships/rocks correct position and orientation
- [ ] **Textured shots visible** (`shots_textured=1`) with glow/trails
- [ ] `vectrail` 0/1/2/3 — phosphor decay levels
- [ ] `vecglow` — glow smear strength
- [ ] Overlay artwork colorizes the CRT image only, not the backdrop
- [ ] Pause shows the retained frame (not black)
- [ ] TAB menu over the game

## 6. `tempest` — color vector, ROT270 cabinet driver

- [ ] Color beams
- [ ] `-ror` / `-rol` compose correctly on top of the driver's own rotation
      *(known issue: some vector game rotates wrong — identify which here)*

## 7. `armora` / `starcas` — vector backdrop + bezel

- [ ] Backdrop behind beams, bezel on top, alpha-test edge correct
- [ ] `artcrop` cabinet scaling
- [ ] Cinematronics `cineshot.png` textured shots

## 8. Front-end GUI

- [ ] Menu text readable, selection bar aligned
- [ ] Starfield animates
- [ ] Navigate, launch a game, return to GUI, launch a *different* game
      (per-game artwork/layout/shot textures must swap cleanly)
- [ ] KEY CONFIG rebind screen
- [ ] Launch a vector game then a raster game (and vice versa) in one session

## 9. Cross-cutting

- [ ] vsync toggle
- [ ] Alt-Tab / focus loss / minimize while running
- [ ] No validation errors in `systemlog.txt` (`vk_validation=1` in aae.ini)
- [ ] GL chain regression: repeat items 1–8 under `-renderer opengl`

---

## KNOWN ISSUES (deferred to the troubleshooting pass)

1. ~~**Scanline overlay scale**~~ — FIXED (`5fc0bc5`), same root cause as 3.
   *Re-gate at `prescale` 2 and 4.*
2. **Vector game rotation** — at least one vector game rotates incorrectly.
   *Name the game here when reproduced.*
3. ~~**Mono CRT differs from GL**~~ — ROOT CAUSE FOUND + FIXED (`6d7b35f`):
   `config.prescale` was completely inert under Vulkan. The game RT rendered
   at native resolution and `uLodBias` was hardcoded 0, so the CRT beam taps
   reconstructed from a `prescale`x coarser image than GL's, and the scanline
   pattern was squeezed into `1/prescale` as many texels (issue 1).
   *Re-gate at `prescale` 2 and 4 vs GL.*
4. **Vector beam look** — user-reported "needs tweaking" (styling, not a bug).

### Prescale re-gate (new, after `5fc0bc5`)

`prescale` is absent from aae.ini so it defaults to **1**, where behavior is
bit-identical to before the fix — **set `prescale=4` in `[main]` to see any
of this**. Valid range 1..5; an out-of-range value silently becomes 2.

- [ ] `pacman` at prescale 1 vs 4: sharper/finer image under VK, matching GL
- [ ] `invaders` mono CRT at prescale 4: beam reconstruction as fine as GL's
- [ ] Scanline overlay at prescale 4: a real multi-row pattern, not a flat
      multiply (it was collapsing to one texel per period)
- [ ] Prescale 1 still byte-identical to the earlier gate (no regression)

## ACCEPTED DEVIATIONS (documented in code, not defects)

- Front-end GUI does not rotate (GL rotates it only as a side effect of fbo4).
- In-game menu/PAUSED text rides the beam queue on vector games, so it picks
  up trail/glow; GL draws overlays after the composite.
- Fun screens / legacy `FUN_TEX`+`GAME_TEX` draw paths (other than the shot
  sprite) are GL-only.
- Layout textures get mip chains under VK; GL uses plain `GL_LINEAR`.
- Color mask vertical phase can sit a sub-triad off (fragment-origin
  difference); pitch is identical, aperture grille unaffected.
- Toggling the vector color overlay while paused takes effect on unpause.
