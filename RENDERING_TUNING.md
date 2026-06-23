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
