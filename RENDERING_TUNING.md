# AAE Rendering Tuning Knobs

Two kinds of knobs control the look of the vector/raster rendering:

1. **Runtime knobs** — change them in the in-game **graphics menu** or in **`aae.ini`**
   (and per-game `.ini` files). No rebuild needed. Saved automatically.
2. **Source constants** — hardcoded tuning values in the C++ source. Changing one
   means **edit the file and rebuild** (Release | x64). These are the values you
   tweak when the runtime knobs don't reach far enough.

File paths below are relative to the source root (the folder with `aae.sln`).

---

## 1. Runtime knobs (menu + aae.ini — no rebuild)

These are global with per-game override (except where noted). Menu item names in CAPS.

| Knob | Menu item | aae.ini key | What it does |
|------|-----------|-------------|--------------|
| Beam line width | (none) | `linewidth` | Thickness of the vector beams. |
| Edge smoothing | `LINE SMOOTHING` | `line_smoothing` | Beam anti-alias feather (softness of beam edges). |
| Corner strength | `BEAM POINTSIZE` | `corner_strength` | Size of the round corner/joint discs where beams meet. |
| Shot style | `VECTOR SHOTS` | `shots_textured` | `0` = procedural shader shots, `1` = legacy textured shots. |
| Fire point size | (none) | `fire_point_size` | Base size of shots/fire points (both styles). |
| Gain | (none) | `gain` | Brightness lift applied to beam colors. |
| Glow / trail | menu | `vecglow` / `vectrail` | Bloom and phosphor-persistence amounts. |
| Death Star FUZZ | (none) | `starwars_fuzz` | Star Wars only. `1` (default) = bloom/white-out the Death Star explosion; `0` = off. Press **F7** in-game to fire the explosion on demand (for watching or tuning it). |

> Tip: most look tuning should be tried here first. The source constants below are
> for shaping things the runtime knobs don't expose.

---

## 2. Source constants (require a rebuild)

### Vector fonts (menu / score / FPS / dialog text)
**File:** `aae/aae/aae_video/vector_fonts.cpp` (near the top, `kFont*`)

| Constant | Default | Effect |
|----------|---------|--------|
| `kFontHalf` | `0.70` | Stroke half-width (text thickness). Higher = bolder text. Scales with the text, so it holds proportions at 1280×1024 and 4K. |
| `kFontAA` | `0.80` | Edge feather (softness). Higher = softer/blurrier strokes, lower = crisper/harder. |
| `kFontEndcap` | `1.0` | Round cap size at stroke ends (× stroke half-width). Rounds the tips of letters. |
| `kFontCorner` | `1.0` | Round joint size where strokes meet (× stroke half-width). |

### Textured shots (e.g. Asteroids Deluxe with artwork — `shots_textured = 1`)
**File:** `aae/aae/vidhrdwr/emu_vector_draw.cpp` (in `draw_textured_shots`, `kShot*`)

These remove the square boundary on the additive halo by fading the edges to
zero. They do **not** dim the bright center.

| Constant | Default | Effect |
|----------|---------|--------|
| `kShotFadeInner` | `0.20` | Radius of the full-bright core (`0` = center … `1` = quad edge). Higher = larger bright area before the fade starts. |
| `kShotFadeOuter` | `1.00` | Radius where the halo fully fades out. `≤ 1.0` keeps it inside the quad; lower = tighter/rounder halo; raise toward the value of inner for a harder edge. |

Quick recipes:
- Still see a faint square? → lower `kShotFadeOuter` to `0.85`.
- Want a bigger bright core? → raise `kShotFadeInner` to `0.35`.
- Want dimmer edges overall? → lower `kShotFadeInner` toward `0.0`.

### Procedural shots (shader shots — `shots_textured = 0`)
**File:** `aae/aae/aae_video/vector_draw.cpp` (in `beam_draw_all`, the `progShot` uniforms)

| Uniform | Default | Effect |
|---------|---------|--------|
| `uCorePower` | `6.0` | Sharpness of the bright core. Higher = tighter, sharper point. |
| `uBloomPower` | `2.5` | Falloff of the surrounding bloom. Lower = wider bloom. |
| `uBloomIntensity` | `0.3` | How much bloom is added around the core. |
| `uOverdrive` | `1.5` | Overall brightness multiplier for shots. |

### Death Star FUZZ (Star Wars, `starwars_fuzz = 1`)
**Files:** `aae/aae/aae_video/shader_definitions.h` (in `texfragText`, `kFuzz*`)
and `aae/shaders/vk/vector_post_multi_vk.frag` — **change both**, they are the
GL and Vulkan halves of the same composite and must stay in sync.

The effect is **content-driven** (matched against real-cab footage): wherever
the drawn image is locally bright and dense — the packed explosion circles —
the phosphor saturates, blooming outward and clipping to white. Sparse strokes
(turrets, HUD, a single expansion ring) barely move, so the wash's shape is
the shape of what the game drew. Duration comes from the game's own explosion
state; there is nothing to time, only a look to tune.

| Constant | Default | Effect |
|----------|---------|--------|
| `kFuzzRadius` | `0.055` | Halation spread around hot areas, as a fraction of the screen. Higher = wider soft halo. |
| `kFuzzTaps` | `32` | Samples in the halation disc. Raise if a large radius shows grain; lower for weak GPUs. (The disc is rotated per pixel by noise, so tap ghosts show up as fine grain rather than rings of copies.) |
| `kFuzzThreshLo` | `0.05` | Local (blurred) brightness where blowout starts to engage. Raise if thin lone strokes catch fire. |
| `kFuzzThreshHi` | `0.30` | Local brightness of full blowout. Lower = the core saturates sooner/harder. |
| `kFuzzBloom` | `3.0` | Halo energy poured back around hot areas — how much the hot mass grows and rounds. |
| `kFuzzWhite` | `1.3` | How hard driven pixels clip toward white. Lower keeps more color in the peak (pink/blue core fringes). |
| `kFuzzVeil` | `0.30` | Veiling-glare strength: the wide faint source-colored wash around the hot mass (the "blindingly bright" cue). `0` = off. |
| `kFuzzVeilLod` | `6.5` | Veil breadth (mip level of the veil tap). Higher = broader, more diffuse veil. |
| `kFuzzRadialInner` | `0.75` | Start of the radial FENCE (fraction of the half-screen). Inside it the content alone decides; this is not the shape of the effect. |
| `kFuzzRadialOuter` | `1.05` | Radius where the fence fully blocks the effect — insurance that HUD rows and edge turrets can never engage. |
| `kFuzzAspect` | `1.3333` | Horizontal stretch of the fence distance so it's a circle on the 4:3 monitor. |
| `kFuzzCenter` | `(0.50, 0.42)` | Fence center, in game-image UV (`v = 1` = top): where the game converges the explosion. |

Quick recipes:
- Core not white enough? → raise `kFuzzWhite` to `1.6` or lower `kFuzzThreshHi` to `0.2`.
- Expansion rings catching too much fire? → raise `kFuzzThreshLo` to `0.10`.
- Hot mass too small / doesn't swallow the circles? → raise `kFuzzBloom` to `4.0` or `kFuzzRadius` to `0.08`.
- Want colored fringes at the peak (pinker core)? → lower `kFuzzWhite` to `1.0`.
- Explosion should feel more blinding? → raise `kFuzzVeil` to `0.5`. Background washing out too much? → lower it to `0.15`.

### Beam end-caps
**File:** `aae/aae/aae_video/vector_draw.cpp` (top, `g_endcap`)

| Constant | Default | Effect |
|----------|---------|--------|
| `g_endcap` | `1.0` | Round end-cap size at true line terminations (× beam half-width), e.g. the tips of an `I` or the ends of a `T` crossbar. (Corners between segments use the runtime `corner_strength` instead.) |

---

## Rebuilding after a source change

Open `aae.sln` in Visual Studio, set **Release | x64**, and Build — or from a
shell:

```
msbuild aae.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

The rebuilt `aae.exe` lands in `x64/Release/`.
