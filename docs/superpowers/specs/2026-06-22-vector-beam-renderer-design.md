# Vector Beam Renderer — Design Spec

- **Date:** 2026-06-22
- **Status:** Approved (pending spec review)
- **Component:** AAE vector video path (`aae/aae/aae_video`, `aae/aae/vidhrdwr`)

## Summary

Replace the deprecated fixed-function vector beam rasterizer (`emu_vector_draw.cpp`
`draw_all()` — client-side vertex arrays + `GL_LINES`/`GL_POINTS`, `glLineWidth`,
`GL_LINE_SMOOTH`) with a modern, shader-based renderer that:

1. Looks **indistinguishable from `GL_LINES`** across widths **1.0–3.5**, for both
   black-and-white (intensity) and color games.
2. Renders **clean, connectivity-aware round joints** between connected segments,
   retiring the `GL_POINTS`-at-endpoints hack.
3. Is **resolution-independent** — identical look at 4K and at 1280×1024 — via a
   fixed 2× supersample of the existing 1024 logical space (2048² physical).
4. Uses **zero deprecated/fixed-function GL calls**, so it is core-profile-ready
   even though the surrounding engine stays on a compatibility context for now.

The existing FBO post-process (glow downsample/blur, `vectrail` phosphor
persistence, final composite) and the GL context are **unchanged**. We are only
swapping the engine that paints the raw beams into `fbo1`/`img1a`.

## Background — current state

Live per-frame vector path:

1. Vector CPU → `vector_add_point()` buffers raw points
   ([mame_vector.cpp:196](../../../aae/aae/aae_video/mame_vector.cpp)).
2. `render()` calls `vector_update()` → `draw_all()` → `vector_clear_list()`
   ([opengl_renderer.cpp:961](../../../aae/aae/aae_video/opengl_renderer.cpp)).
3. `vector_update()` clips/scales/orients each segment to 1024-logical space and
   emits `add_line()` / `add_tex()` ([mame_vector.cpp:280](../../../aae/aae/aae_video/mame_vector.cpp)).
4. `draw_all()` rasterizes the line/point/shot lists into `fbo1`/`img1a`
   ([emu_vector_draw.cpp:206](../../../aae/aae/vidhrdwr/emu_vector_draw.cpp)).
5. `final_render()` composites trail + glow blur + bezel/overlay and blits to
   screen ([opengl_renderer.cpp:1005](../../../aae/aae/aae_video/opengl_renderer.cpp)).

Step 4 is fully deprecated (client arrays, no VBO, no shader, fixed-function
matrix/color, `glLineWidth` — which most core drivers clamp to 1.0). It only works
because the context is a compatibility profile. Step 5 is mostly modern shader code.

### Why the existing `aae_video/vector_draw.cpp` is not reused

A dormant "modern" renderer exists (`vector_draw.cpp`, instanced SDF capsules) but
is never wired in and has two disqualifying problems the user identified:

- **#1 Width/brightness mismatch.** It drives the capsule core to full alpha and
  applies `pow(alpha, 1.2)`, so a "1.0" capsule reads thicker and brighter than a
  real `GL_LINE` (which is mostly coverage-AA falloff at 1.0px). It cannot match
  `GL_LINES`.
- **#2 Bulbous joints.** Round-capped capsules pile up at shared vertices; its
  `kCapTrim = 0.90` constant is a band-aid for exactly this.

This spec rewrites `vector_draw.*` into the production beam renderer, addressing
both directly.

## Goals

- Pixel-faithful match to `GL_LINES` at widths 1.0–3.5 (calibrated, not bit-exact).
- Clean round joints; no oversized join dots; no bulge.
- Resolution-invariant output; crisp at 4K.
- Preserve the black-and-white painter's sort ("brighter beams occlude darker")
  and the existing per-game blend modes.
- Preserve the entire downstream post-process look (glow, trail, composite).
- No deprecated GL in the new code.

## Non-goals (explicitly out of scope)

- **HDR / brightness headroom.** Deferred to a later session. See *Future Work*.
- **Switching the GL context to a core profile.** That would require porting the
  fonts, GUI, `final_render()`, and `drawTexturedQuad()` off compatibility
  features. The new renderer is written core-clean so a future port is easier, but
  the context stays compatibility now.
- **Any change to the glow/trail/composite pipeline** beyond the texture-size
  enlargement required for supersampling.

## Requirements (hard)

| # | Requirement |
|---|-------------|
| R1 | Look like `GL_LINES` at 1.0–3.5 width, B/W and color. |
| R2 | Full painter's sort for B/W so brighter beams render in front of darker, using the existing alpha-over blend. |
| R3 | Better-than-`GL_POINTS` joints between connected segments. |
| R4 | Identical look at 4K and 1280×1024. |
| R5 | OpenGL 4-class, full shaders, no fixed-function in the new path. |
| R6 | Shots rendered procedurally (shader math, no bound texture). |

## Architecture overview

```
Vector CPU
   │  vector_add_point()                     (unchanged)
   ▼
vector_update()  ── clip / scale / orient ── emits:           (mame_vector.cpp)
   │   beam_add_line(p0, p1, rgba, joinPrev)
   │   beam_add_shot(pos, rgba)
   ▼
Beam renderer  (rewritten vector_draw.*)
   • beam_init()/beam_shutdown()       — VAO/VBOs + shaders
   • beam_clear()                      — per-frame reset
   • beam_draw_all(const mat4& proj)   — sort (B/W), draw lines+joins, points, shots
   ▼
fbo1 / img1a  (2048² physical, 1024 logical)  →  final_render()  (unchanged)
```

The renderer owns three programs and their buffers:

- **Line program** — butt-capped segment quads, coverage-AA matched to `GL_LINES`.
- **Join program** — round-join discs at interior vertices (radius = beam
  half-width). May be folded into the line program as a second instanced draw.
- **Shot program** — procedural radial core+halo (no texture).

## Detailed design

### D1. New module & API (`aae/aae/aae_video/vector_draw.*`, rewritten)

```cpp
struct BeamConfig {
    float width;          // beam width in logical units (from config.linewidth)
    float aa_pixels;      // AA feather in *physical* pixels (~1.0); calibration knob
    bool  additive;       // true = color (GL_ONE), false = B/W (alpha-over + sort)
    // shot tuning (procedural)
    float shot_size, shot_core_power, shot_bloom_power,
          shot_bloom_intensity, shot_overdrive;
};

void beam_init(const BeamConfig&);
void beam_shutdown();
void beam_set_config(const BeamConfig&);   // per-game / per-frame tuning

void beam_add_line(vec2 p0, vec2 p1, rgb_t rgba, bool joinPrev);
void beam_add_shot(vec2 pos, rgb_t rgba);
void beam_clear();
void beam_draw_all(const aae::math::mat4& proj);
```

`rgb_t` packed RGBA is retained. The intensity/gain color math
(`modulate_color()`, the half-color path) is kept and moves into (or is shared
with) this module so the per-beam color is identical to today. **Alpha is coverage,
not brightness** — see D3.

### D2. Producer / feed changes (`mame_vector.cpp::vector_update`)

- Replace `add_line(...)` → `beam_add_line(p0, p1, col, joinPrev)`.
- Replace `add_tex(...)` → `beam_add_shot(pos, col)`.
- `joinPrev` = "the shared start vertex of this segment was itself a lit
  endpoint." Computable trivially: a segment `P[i-1]→P[i]` is drawn iff
  `P[i].intensity != 0`; the next segment `P[i]→P[i+1]` should join at `P[i]` iff
  the previous segment was drawn (track a `bool prev_drawn`). A pen-up (intensity
  0) or list start sets `joinPrev = false` → the next segment begins a new stroke.
- Beam width passed from `config.linewidth` (no longer `glLineWidth`).

### D3. The beam — matching `GL_LINES` (R1, fixes #1)

- Each segment is a **butt-capped quad** expanded in the **vertex shader** from
  `p0`, `p1`, and half-width + AA margin. Thickness is geometry; `glLineWidth` is
  never used.
- The **fragment shader** computes perpendicular distance `d` from the centerline
  and converts to **coverage** (alpha):
  `coverage = clamp((halfWidth + 0.5*aa - d) / aa, 0, 1)` (in physical pixels),
  which reproduces `GL_LINE_SMOOTH`'s ~1px coverage ramp — **including the
  sub-unit peak below ~1.5px** that makes thin lines read faint.
- **No `pow()` brightening.** Output color = `modulate_color` RGB; output alpha =
  coverage. This is the specific fix for #1.
- The AA feather is expressed in **physical pixels at the render target** (derived
  from the projection × supersample scale, or passed as a uniform), so crispness is
  consistent regardless of logical/physical ratio.
- **Calibration:** runtime A/B against real `GL_LINES` at 1.0/1.5/2.0/2.5/3.0/3.5;
  tune `aa_pixels` (and any width bias) until indistinguishable. This is a tuning
  task, not bit-exact (GL_LINE_SMOOTH is implementation-defined).

### D4. Joints — round, connectivity-aware (R3, fixes #2)

- **Interior vertex** (`joinPrev == true`): a **round-join disc** at the shared
  vertex, radius **exactly the beam half-width**, drawn with the same coverage
  shader so it fuses flush. Because the radius is locked to the beam width, it can
  never bulge — the `GL_POINTS` blob today is `config.pointsize != config.linewidth`,
  which this design makes structurally impossible.
- **True endpoint** (stroke start/end): **butt cap** — clean stop, like `GL_LINES`,
  no dot. (Default; trivially switchable to a soft round terminal later if desired.)
- Overlap behavior: under B/W sorted alpha-over with equal color, overlap does not
  brighten (clean). Under color additive, the small join overlap reads as faint
  vertex warmth — consistent with real CRT beam dwell at corners; removable later if
  unwanted.

### D5. Points / shots — procedural (R6)

- The `add_tex` shot path (Asteroids/Deluxe textured shots) becomes a **procedural
  quad**: per shot, a screen-aligned quad with a fragment shader computing a radial
  **core + halo + overdrive** from UV distance (basis: the existing `fs_fire`
  shader in the dormant `vector_draw.cpp`). No `game_tex[0]` binding.
- Tunable via `BeamConfig` (core power, bloom power/intensity, overdrive, size).
- Forced additive (`GL_SRC_ALPHA, GL_ONE`), matching the current shot look.

### D6. Blending & the black-and-white sort (R2)

Preserved exactly from `draw_all()`:

- **Color** (`VECTOR_USES_COLOR`): additive `GL_SRC_ALPHA, GL_ONE`, no sort.
- **B/W:** full painter's sort, darkest → brightest, then
  `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` so brighter beams occlude darker. Port
  `sort_lines_by_color()` ([emu_vector_draw.cpp:41](../../../aae/aae/vidhrdwr/emu_vector_draw.cpp))
  to sort the new segment list; each segment's round join travels with it.
- `fbo1`/`img1a` stays **GL_RGB8** (no alpha) — both blend modes write RGB only, so
  the no-alpha accumulation design is unaffected.

### D7. Resolution — fixed 2× supersample (R4)

**Approach:** keep the entire engine in **1024 logical space** (vectors,
`game_rect`, overlays, bezel, fonts — all unchanged) and render into **2048²
physical** textures/viewports. A single `SSAA = 2` multiplier scales only:

- FBO texture allocation in `gl_fbo.cpp::create_texture` for `fbo1/fbo2/fbo3/fbo4`
  (e.g. `fbo1` 2048², `fbo2` 1024², `fbo3` 512²). The logical-size constants
  `width/height/width2/height2/width3/height3` stay 1024/512/256.
- The **viewport** in the FBO render path must be decoupled from the **ortho**: the
  ortho stays logical (e.g. `glOrtho(0,1024,0,1024)`), the viewport becomes physical
  (`glViewport(0,0,2048,2048)`). Introduce a small helper
  (`set_render_target(logicalW, logicalH)` that sets `glViewport(logicalW*SSAA,
  logicalH*SSAA)` + logical ortho) and use it for the FBO-targeting `set_ortho`
  calls in `opengl_renderer.cpp`.
- The blur shader's `width`/`height` uniforms should use the **physical** texel
  size of the sampled buffer so the kernel step matches actual texels; the glow
  *look* is otherwise preserved because all geometry stays in logical/UV space.

**Do not touch** the UI virtual-space literals (`set_ortho(1024,768)`,
`glOrtho(0,1024,0,768)`, `quad_from_center(512,384,1024,768,…)`,
`VF.Initialize(1024,768)`, `render_ui_overlays(1024,768)`) — that 4:3 canvas is
independent of render resolution. For vector games, the UI-overlay-into-FBO ortho
(`set_ortho(1024,1024)` at [opengl_renderer.cpp:797](../../../aae/aae/aae_video/opengl_renderer.cpp))
uses the new FBO-target helper so it lands at 2048 physical while keeping 1024
logical.

Result: one fixed physical resolution → identical look at every window size; 4K
upscales ~1.9× instead of ~3.75×.

### D8. Integration points (files touched)

**Create / rewrite:**
- `aae/aae/aae_video/vector_draw.cpp` + `.h` — the production beam renderer
  (replaces the dormant instanced-SDF contents).

**Modify:**
- `aae/aae/aae_video/mame_vector.cpp` — `vector_update()`: emit to `beam_*` with
  `joinPrev`; pass `config.linewidth`.
- `aae/aae/aae_video/opengl_renderer.cpp` — `init_gl()`: `beam_init()` + SSAA
  setup; `render()`: build `aae::math::ortho(0,1024,0,1024)` proj, call
  `beam_clear()` / `beam_draw_all(proj)` in place of `draw_all()`; add the
  FBO-target viewport/ortho helper and apply to the FBO-space `set_ortho`/`FS_Rect`
  call sites; set blur uniforms to physical texel size.
- `aae/aae/aae_video/gl_fbo.cpp` — apply `SSAA` to `create_texture` allocation for
  `fbo1/fbo2/fbo3/fbo4`.
- `aae/aae/vidhrdwr/emu_vector_draw.cpp` / `.h` — retire `draw_all()`,
  `add_line()`, `add_tex()` and the client-array GL; keep/move `modulate_color()`
  and the intensity/gain color math into the new module.

**Retire reliance on:** `glLineWidth`, `glPointSize`, `GL_LINE_SMOOTH`,
`GL_POINT_SMOOTH` in `init_gl()` for the vector path.

## Validation plan

Runtime A/B toggle (old `draw_all` vs new `beam_draw_all`) on:

- **Battlezone / Red Baron** — B/W painter's sort, bright-over-dark occlusion.
- **Asteroids / Asteroids Deluxe** — thin lines (1.0–1.5), procedural shots, sharp
  ship joints.
- **Tempest / Star Wars** — color additive, dense geometry.

Checks: width parity 1.0–3.5; joints clean on the Asteroids ship and Battlezone
horizon (no blobs, no gaps); 4K vs 1280 parity; glow/trail look unchanged vs the
pre-change build; no `GL_INVALID_*` from `check_gl_error_named`.

## Risks

- **Coverage match is calibration, not exact** — `GL_LINE_SMOOTH` is
  implementation-defined; "match" means visually indistinguishable after tuning.
- **Viewport/ortho decoupling** — the FBO-target helper must be applied to every
  FBO-space `set_ortho`/`FS_Rect` site (D7 list) and to no UI-space site; a missed
  site mis-scales the composite.
- **Blur texel size** — if the blur uniforms aren't updated to physical size the
  glow softens slightly; cosmetic, easily corrected during validation.

## Future work (not this session)

- **HDR brightness.** Move `fbo1/2/3/4` to a float format (e.g. `GL_RGBA16F`), let
  beams/glow accumulate beyond 1.0, and tonemap on the final blit
  (`end_render_fbo4`). The beam shader already outputs linear color + coverage, so
  it is HDR-ready; this becomes a contained pipeline-format + tonemap change.
- Optional miter-join mode; optional soft round terminals at true endpoints;
  optional per-vertex beam-dwell brightening (the deferred "B" realism step).
