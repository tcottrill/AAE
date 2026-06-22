# Vector Beam Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace AAE's deprecated fixed-function vector beam rasterizer (`GL_LINES`/`GL_POINTS` via client arrays, `glLineWidth`, `GL_LINE_SMOOTH`) with a modern shader-based renderer that matches `GL_LINES` at 1.0–3.5 width, joins segments cleanly, and renders identically at 4K and 1280×1024.

**Architecture:** A new self-contained renderer (rewritten `aae_video/vector_draw.*`) owns its VAO/VBOs + GLSL programs and draws instanced, coverage-AA'd butt-capped beams + round joins + procedural shots into `fbo1/img1a`. It runs behind an F5 A/B toggle against the old path for calibration, then becomes the default. The downstream glow/trail/composite pipeline and the GL compatibility context are unchanged; the new code uses zero deprecated GL. Resolution independence is a final 2× supersample of the existing 1024-logical space.

**Tech Stack:** C++17, OpenGL (GLEW, compatibility context), GLSL `#version 330 core`, `aae::math` (vec2/mat4/ortho), MSBuild / Visual Studio 2022.

**Spec:** [docs/superpowers/specs/2026-06-22-vector-beam-renderer-design.md](../specs/2026-06-22-vector-beam-renderer-design.md)

---

## Verification model (read first)

This is GL rendering in a legacy codebase with **no unit-test harness**. Forcing
unit tests here would be dishonest. Each task is verified by:

1. **Build** — `msbuild aae.sln /p:Configuration=Release /p:Platform=x64 /m`
   from a *Developer PowerShell for VS 2022* (or Build → Rebuild in the IDE).
   Output: `x64\Release\aae.exe`. Expected: `Build succeeded, 0 Error(s)`.
2. **Run + observe** — launch `x64\Release\aae.exe`, start the named game, and
   visually confirm the described result, toggling **F5** to A/B against the old
   path. The old path stays the default until the cutover (Phase 5), so any
   regression is one keypress away from comparison.
3. **Commit** — small, frequent commits per task.

Pure-logic pieces (the `joinPrev` connectivity rule, the darkest-first sort) are
verified by reasoning + the visual checks that exercise them; they are noted where
they occur.

**Branch first.** `main` carries unrelated uncommitted changes. Before Task 1.1:

```bash
git checkout -b feature/vector-beam-renderer
git add docs/superpowers/specs/2026-06-22-vector-beam-renderer-design.md docs/superpowers/plans/2026-06-22-vector-beam-renderer.md
git commit -m "docs: vector beam renderer spec + plan"
```

---

## File structure

| File | Responsibility | Action |
|------|----------------|--------|
| `aae/aae/aae_video/vector_draw.h` | Beam renderer public API + vertex structs | Rewrite |
| `aae/aae/aae_video/vector_draw.cpp` | Shaders, VAO/VBOs, add/clear/draw | Rewrite |
| `aae/aae/aae_video/mame_vector.cpp` | Producer: emit `beam_*` with `joinPrev` | Modify `vector_update()` |
| `aae/aae/aae_video/opengl_renderer.cpp` | `beam_init`, render-path toggle, proj, SSAA | Modify |
| `aae/aae/aae_video/opengl_renderer.h` | `extern bool g_beam_legacy` | Modify |
| `aae/aae/vidhrdwr/emu_vector_draw.cpp` | `cache_clear()` also clears beam lists; retire `draw_all` (Phase 5) | Modify |
| `aae/aae/aae_video/gl_fbo.cpp` / `.h` | `g_ssaa`, supersized `fbo1`/`fbo4` (Phase 6) | Modify |
| `aae/aae/aae_emulator.cpp` | F5 toggles `g_beam_legacy` | Modify |

`modulate_color()` (declared in `emu_vector_draw.h`) is reused for the per-beam
color so the color path stays identical; it is not moved until Phase 5.

---

## PHASE 1 — Coverage-matched beam lines behind an F5 toggle (@1024)

### Task 1.1: Create the beam renderer module (lines working; joins/shots stubbed)

**Files:**
- Create: `aae/aae/aae_video/vector_draw.h`
- Create: `aae/aae/aae_video/vector_draw.cpp`

> The dormant instanced-SDF `vector_draw.*` is fully replaced. If git shows the old
> versions, overwrite them entirely with the content below.

- [ ] **Step 1: Write the header**

`aae/aae/aae_video/vector_draw.h`:

```cpp
#pragma once
#ifndef VECTOR_DRAW_H
#define VECTOR_DRAW_H

#include "sys_gl.h"
#include "colordefs.h"     // rgb_t
#include "MathUtils.h"     // aae::math::vec2 / mat4

// Per-segment beam (butt-capped, coverage-AA rectangle).
struct BeamLine {
    aae::math::vec2 p0;
    aae::math::vec2 p1;
    float           half;   // half-width, logical units
    rgb_t           color;  // packed RGBA (a = 0xff); coverage supplies edge alpha
};

// Round join disc placed at an interior shared vertex (radius == beam half-width).
struct BeamJoin {
    aae::math::vec2 center;
    float           half;
    rgb_t           color;
};

// Procedural shot/fire point (radial core + halo in the shader).
struct BeamShot {
    aae::math::vec2 pos;
    float           size;
    rgb_t           color;
};

// ssaa = supersample factor of the bound render target (1 in Phase 1, 2 in Phase 6).
void beam_init(int ssaa);
void beam_shutdown();
void beam_set_ssaa(int ssaa);   // updates the AA feather if the factor changes

// Mirrors the old add_line/add_tex signatures so the producer change is minimal.
// joinPrev == true draws a round join at p0 (the shared vertex with the previous
// connected segment).
void beam_add_line(float sx, float sy, float ex, float ey,
                   int intensity, rgb_t col, bool joinPrev);
void beam_add_shot(float ex, float ey, int intensity, rgb_t col);

void beam_clear();
void beam_draw_all(const aae::math::mat4& proj);

#endif // VECTOR_DRAW_H
```

- [ ] **Step 2: Write the implementation (line program live; join/shot draw stubbed)**

`aae/aae/aae_video/vector_draw.cpp`:

```cpp
// -----------------------------------------------------------------------------
// vector_draw.cpp - Modern shader-based vector beam renderer.
// Butt-capped, coverage-AA'd instanced beams + round joins + procedural shots.
// Draws into the currently bound FBO (fbo1/img1a) using an explicit uProj; no
// fixed-function state. See docs/superpowers/specs/2026-06-22-vector-beam-renderer-design.md
// -----------------------------------------------------------------------------
#include "vector_draw.h"
#include "shader_util.h"                 // CompileShader / LinkShaderProgram
#include "aae_mame_driver.h"             // Machine, VECTOR_USES_COLOR
#include "config.h"                      // config.linewidth / gain / fire_point_size
#include "emu_vector_draw.h"             // modulate_color
#include <vector>
#include <algorithm>
#include <cstdint>

using namespace aae::math;

static const float AA_PIXELS = 1.2f;     // edge feather in physical pixels (calibration knob)

static std::vector<BeamLine> g_lines;
static std::vector<BeamJoin> g_joins;
static std::vector<BeamShot> g_shots;

static GLuint vaoLine = 0, vboLine = 0;
static GLuint vaoJoin = 0, vboJoin = 0;
static GLuint vaoShot = 0, vboShot = 0;

static GLuint progLine = 0, progJoin = 0, progShot = 0;

static int   g_ssaa = 1;
static float g_uAA  = AA_PIXELS;         // feather in logical units (= AA_PIXELS / ssaa)

// ----------------------------- shaders --------------------------------------
static const char* vsLine = R"GLSL(
#version 330 core
layout(location=0) in vec2  inP0;
layout(location=1) in vec2  inP1;
layout(location=2) in float inHalf;
layout(location=3) in vec4  inColor;
uniform mat4  uProj;
uniform float uAA;
out vec2  vLocal;   // x = longitudinal [0..len], y = perpendicular
out float vLen;
out float vHalf;
out vec4  vColor;
const vec2 kQuad[4] = vec2[](vec2(0,-1), vec2(1,-1), vec2(0,1), vec2(1,1));
void main() {
    vec2  d   = inP1 - inP0;
    float len = length(d);
    vec2  dir = (len > 0.0001) ? d/len : vec2(1.0,0.0);
    vec2  nrm = vec2(-dir.y, dir.x);
    vec2  q   = kQuad[gl_VertexID];
    float along = q.x * (len + 2.0*uAA) - uAA;       // butt cap + feather past ends
    float perp  = q.y * (inHalf + uAA);
    vec2  pos   = inP0 + dir*along + nrm*perp;
    gl_Position = uProj * vec4(pos, 0.0, 1.0);
    vLocal = vec2(along, perp);
    vLen = len; vHalf = inHalf; vColor = inColor;
}
)GLSL";

static const char* fsLine = R"GLSL(
#version 330 core
in vec2  vLocal;
in float vLen;
in float vHalf;
in vec4  vColor;
uniform float uAA;
out vec4 frag;
void main() {
    // Half-coverage exactly at the geometric edge -> width matches GL_LINES.
    float covPerp = clamp((vHalf - abs(vLocal.y))/uAA + 0.5, 0.0, 1.0);
    float covEnd0 = clamp((vLocal.x)/uAA + 0.5, 0.0, 1.0);
    float covEnd1 = clamp((vLen - vLocal.x)/uAA + 0.5, 0.0, 1.0);
    float cov = covPerp * covEnd0 * covEnd1;
    if (cov <= 0.0) discard;
    frag = vec4(vColor.rgb, vColor.a * cov);   // no pow(): linear coverage
}
)GLSL";

// Join + shot programs (used in Phase 2 / Phase 4). Compiled now so the module is
// self-contained; their draws are stubbed until then.
static const char* vsJoin = R"GLSL(
#version 330 core
layout(location=0) in vec2  inCenter;
layout(location=1) in float inHalf;
layout(location=2) in vec4  inColor;
uniform mat4  uProj;
uniform float uAA;
out vec2  vLocal;
out float vHalf;
out vec4  vColor;
const vec2 kQuad[4] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));
void main() {
    vec2 q = kQuad[gl_VertexID];
    vec2 ext = q * (inHalf + uAA);
    gl_Position = uProj * vec4(inCenter + ext, 0.0, 1.0);
    vLocal = ext; vHalf = inHalf; vColor = inColor;
}
)GLSL";

static const char* fsJoin = R"GLSL(
#version 330 core
in vec2  vLocal;
in float vHalf;
in vec4  vColor;
uniform float uAA;
out vec4 frag;
void main() {
    float cov = clamp((vHalf - length(vLocal))/uAA + 0.5, 0.0, 1.0);
    if (cov <= 0.0) discard;
    frag = vec4(vColor.rgb, vColor.a * cov);
}
)GLSL";

static const char* vsShot = R"GLSL(
#version 330 core
layout(location=0) in vec2  inCenter;
layout(location=1) in float inSize;
layout(location=2) in vec4  inColor;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
const vec2 kQuad[4] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));
void main() {
    vec2 q = kQuad[gl_VertexID];
    gl_Position = uProj * vec4(inCenter + q*inSize, 0.0, 1.0);
    vUV = q*0.5 + 0.5;
    vColor = inColor;
}
)GLSL";

static const char* fsShot = R"GLSL(
#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 frag;
uniform float uCorePower;
uniform float uBloomPower;
uniform float uBloomIntensity;
uniform float uOverdrive;
void main() {
    float br = max(vColor.r, max(vColor.g, vColor.b));
    br = max(0.1, br);
    float d  = distance(vUV, vec2(0.5));
    float g  = clamp(1.0 - d*2.0, 0.0, 1.0);
    float core = pow(g, uCorePower);
    float halo = pow(g, uBloomPower) * (uBloomIntensity * (0.5 + br*0.5));
    float ti = core + halo;
    vec3 hot = vColor.rgb * (uOverdrive * br);
    frag = vec4(hot * ti, ti);
}
)GLSL";

// ----------------------------- helpers --------------------------------------
static GLuint linkProg(const char* vs, const char* fs, const char* label) {
    return LinkShaderProgram(CompileShader(GL_VERTEX_SHADER,   vs, label),
                             CompileShader(GL_FRAGMENT_SHADER, fs, label));
}

static void setAA(int ssaa) {
    g_ssaa = (ssaa < 1) ? 1 : ssaa;
    g_uAA  = AA_PIXELS / (float)g_ssaa;
}

// ----------------------------- lifecycle ------------------------------------
void beam_set_ssaa(int ssaa) { setAA(ssaa); }

void beam_init(int ssaa) {
    setAA(ssaa);

    progLine = linkProg(vsLine, fsLine, "beam_line");
    progJoin = linkProg(vsJoin, fsJoin, "beam_join");
    progShot = linkProg(vsShot, fsShot, "beam_shot");

    // Lines: one instance per segment.
    glGenVertexArrays(1, &vaoLine);
    glGenBuffers(1, &vboLine);
    glBindVertexArray(vaoLine);
    glBindBuffer(GL_ARRAY_BUFFER, vboLine);
    glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, sizeof(BeamLine), (void*)offsetof(BeamLine, p0));    glEnableVertexAttribArray(0); glVertexAttribDivisor(0,1);
    glVertexAttribPointer(1, 2, GL_FLOAT,         GL_FALSE, sizeof(BeamLine), (void*)offsetof(BeamLine, p1));    glEnableVertexAttribArray(1); glVertexAttribDivisor(1,1);
    glVertexAttribPointer(2, 1, GL_FLOAT,         GL_FALSE, sizeof(BeamLine), (void*)offsetof(BeamLine, half));  glEnableVertexAttribArray(2); glVertexAttribDivisor(2,1);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(BeamLine), (void*)offsetof(BeamLine, color)); glEnableVertexAttribArray(3); glVertexAttribDivisor(3,1);

    // Joins.
    glGenVertexArrays(1, &vaoJoin);
    glGenBuffers(1, &vboJoin);
    glBindVertexArray(vaoJoin);
    glBindBuffer(GL_ARRAY_BUFFER, vboJoin);
    glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, sizeof(BeamJoin), (void*)offsetof(BeamJoin, center)); glEnableVertexAttribArray(0); glVertexAttribDivisor(0,1);
    glVertexAttribPointer(1, 1, GL_FLOAT,         GL_FALSE, sizeof(BeamJoin), (void*)offsetof(BeamJoin, half));   glEnableVertexAttribArray(1); glVertexAttribDivisor(1,1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(BeamJoin), (void*)offsetof(BeamJoin, color));  glEnableVertexAttribArray(2); glVertexAttribDivisor(2,1);

    // Shots.
    glGenVertexArrays(1, &vaoShot);
    glGenBuffers(1, &vboShot);
    glBindVertexArray(vaoShot);
    glBindBuffer(GL_ARRAY_BUFFER, vboShot);
    glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, sizeof(BeamShot), (void*)offsetof(BeamShot, pos));   glEnableVertexAttribArray(0); glVertexAttribDivisor(0,1);
    glVertexAttribPointer(1, 1, GL_FLOAT,         GL_FALSE, sizeof(BeamShot), (void*)offsetof(BeamShot, size));  glEnableVertexAttribArray(1); glVertexAttribDivisor(1,1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(BeamShot), (void*)offsetof(BeamShot, color)); glEnableVertexAttribArray(2); glVertexAttribDivisor(2,1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void beam_shutdown() {
    GLuint vaos[] = { vaoLine, vaoJoin, vaoShot };
    GLuint vbos[] = { vboLine, vboJoin, vboShot };
    glDeleteVertexArrays(3, vaos);
    glDeleteBuffers(3, vbos);
    glDeleteProgram(progLine);
    glDeleteProgram(progJoin);
    glDeleteProgram(progShot);
    vaoLine = vaoJoin = vaoShot = vboLine = vboJoin = vboShot = 0;
    progLine = progJoin = progShot = 0;
}

// ----------------------------- producer -------------------------------------
void beam_add_line(float sx, float sy, float ex, float ey,
                   int intensity, rgb_t col, bool joinPrev) {
    rgb_t c = modulate_color(col, intensity, config.gain);
    float half = config.linewidth * 0.5f;
    g_lines.push_back({ vec2(sx, sy), vec2(ex, ey), half, c });
    if (joinPrev)
        g_joins.push_back({ vec2(sx, sy), half, c });
}

void beam_add_shot(float ex, float ey, int intensity, rgb_t col) {
    rgb_t c = modulate_color(col, intensity, config.gain);
    g_shots.push_back({ vec2(ex, ey), (float)config.fire_point_size, c });
}

void beam_clear() {
    g_lines.clear();
    g_joins.clear();
    g_shots.clear();
}

// ----------------------------- draw -----------------------------------------
void beam_draw_all(const mat4& proj) {
    if (g_lines.empty() && g_joins.empty() && g_shots.empty()) return;

    const bool additive = (Machine->drv->video_attributes & VECTOR_USES_COLOR) != 0;

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);

    if (!additive) {
        // Black-and-white: painter's sort, darkest first, so brighter beams
        // occlude darker ones under alpha-over.
        std::sort(g_lines.begin(), g_lines.end(),
            [](const BeamLine& a, const BeamLine& b) {
                return (uint32_t)a.color < (uint32_t)b.color; });
        std::sort(g_joins.begin(), g_joins.end(),
            [](const BeamJoin& a, const BeamJoin& b) {
                return (uint32_t)a.color < (uint32_t)b.color; });
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);   // color: additive
    }

    // Lines.
    if (!g_lines.empty()) {
        glUseProgram(progLine);
        glUniformMatrix4fv(glGetUniformLocation(progLine, "uProj"), 1, GL_FALSE, value_ptr(const_cast<mat4&>(proj)));
        glUniform1f(glGetUniformLocation(progLine, "uAA"), g_uAA);
        glBindBuffer(GL_ARRAY_BUFFER, vboLine);
        glBufferData(GL_ARRAY_BUFFER, g_lines.size()*sizeof(BeamLine), g_lines.data(), GL_STREAM_DRAW);
        glBindVertexArray(vaoLine);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)g_lines.size());
    }

    // Joins + shots: implemented in Phase 2 / Phase 4.

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}
```

- [ ] **Step 3: Confirm `vector_draw.cpp` is in the build**

The file already exists in `aae/aae.vcxproj` (the dormant version was compiled). No
project change is needed. If a *new* path were introduced it would have to be added
to `aae.vcxproj` + `.filters` — but here we only rewrote existing files.

- [ ] **Step 4: Build**

Run: `msbuild aae.sln /p:Configuration=Release /p:Platform=x64 /m`
Expected: `Build succeeded. 0 Error(s)`. (Nothing calls the module yet — this only
proves it compiles and links.)

- [ ] **Step 5: Commit**

```bash
git add aae/aae/aae_video/vector_draw.cpp aae/aae/aae_video/vector_draw.h
git commit -m "feat(vector): add modern beam renderer module (lines), not yet wired"
```

### Task 1.2: Add the `g_beam_legacy` A/B toggle (F5)

**Files:**
- Modify: `aae/aae/aae_video/opengl_renderer.cpp` (define global)
- Modify: `aae/aae/aae_video/opengl_renderer.h` (declare extern)
- Modify: `aae/aae/aae_emulator.cpp:~1110` (F5 handler)

- [ ] **Step 1: Define the global** in `opengl_renderer.cpp`, near the other
module-level globals (after the `Fpoly* sc;` line, ~line 80):

```cpp
// Vector beam renderer A/B toggle: true = legacy GL_LINES draw_all(), false = new
// shader beam renderer. Flipped at runtime with F5. Default legacy until cutover.
bool g_beam_legacy = true;
```

- [ ] **Step 2: Declare it** in `opengl_renderer.h`, next to `g_scanline_override`:

```cpp
extern bool g_beam_legacy;
```

- [ ] **Step 3: Wire F5** in `aae_emulator.cpp`, immediately after the
`OSD_KEY_SHOW_FPS` handler (~line 1112). Add the include `#include "opengl_renderer.h"`
at the top of the file if it is not already present.

```cpp
    // F5 = toggle the modern vector beam renderer vs legacy GL_LINES (A/B compare)
    if (osd_key_pressed_memory(OSD_KEY_F5))
    {
        g_beam_legacy = !g_beam_legacy;
        LOG_INFO("Vector beam renderer: %s", g_beam_legacy ? "LEGACY (GL_LINES)" : "MODERN (shader)");
    }
```

- [ ] **Step 4: Build**

Run: `msbuild aae.sln /p:Configuration=Release /p:Platform=x64 /m`
Expected: `Build succeeded`. F5 now flips a flag and logs; nothing else uses it yet.

- [ ] **Step 5: Commit**

```bash
git add aae/aae/aae_video/opengl_renderer.cpp aae/aae/aae_video/opengl_renderer.h aae/aae/aae_emulator.cpp
git commit -m "feat(vector): add F5 A/B toggle flag for beam renderer"
```

### Task 1.3: Wire the producer, clear, init, and render-path toggle

**Files:**
- Modify: `aae/aae/aae_video/mame_vector.cpp` (`vector_update`)
- Modify: `aae/aae/vidhrdwr/emu_vector_draw.cpp` (`cache_clear`)
- Modify: `aae/aae/aae_video/opengl_renderer.cpp` (`init_gl`, `render`)

- [ ] **Step 1: Init the renderer.** In `opengl_renderer.cpp` `init_gl()`, in the
existing `VIDEO_TYPE_VECTOR` block that calls `vector_start()` (~line 392), add the
include `#include "vector_draw.h"` at the top, then:

```cpp
        if (Machine->gamedrv->video_attributes & VIDEO_TYPE_VECTOR)
        {
            vector_start();
            beam_init(1);          // ssaa = 1 (Phase 1); raised to 2 in Phase 6
        }
```

- [ ] **Step 2: Hook the clear.** In `emu_vector_draw.cpp` add
`#include "vector_draw.h"` at the top, and make `cache_clear()` also clear the beam
lists (this is the single point that covers all ~15 driver call sites):

```cpp
void cache_clear()
{
    texlist.clear();
    linelist.clear();
    beam_clear();          // clear the modern beam lists on the same frame boundary
}
```

- [ ] **Step 3: Branch the producer.** In `mame_vector.cpp`, add
`#include "vector_draw.h"` and `#include "opengl_renderer.h"` (for `g_beam_legacy`)
at the top. Then in `vector_update()`, replace the draw-emit block. The current code
(~line 361) reads:

```cpp
				if (curpoint->intensity != 0 && curpoint->col ) {
					if (!render_clip_line(&coords, &clip))
						if (curpoint->status == VTEX)
						{
							add_tex(coords.x0, coords.y0, curpoint->intensity, curpoint->col);
						}
						else {
							add_line(coords.x0, coords.y0, coords.x1 + .00001f, coords.y1 + .00001f, curpoint->intensity, curpoint->col);
						}
				}
				lastx = curpoint->x;
				lasty = curpoint->y;
```

Replace it with a version that tracks connectivity and emits to the active path.
Add `bool prev_drawn = false;` just before the `for` loop (next to `int lastx = 0, lasty = 0;`).

```cpp
				bool drew_line = false;
				if (curpoint->intensity != 0 && curpoint->col) {
					if (!render_clip_line(&coords, &clip)) {
						if (curpoint->status == VTEX) {
							if (g_beam_legacy)
								add_tex(coords.x0, coords.y0, curpoint->intensity, curpoint->col);
							else
								beam_add_shot(coords.x0, coords.y0, curpoint->intensity, curpoint->col);
						}
						else {
							if (g_beam_legacy)
								add_line(coords.x0, coords.y0, coords.x1 + .00001f, coords.y1 + .00001f, curpoint->intensity, curpoint->col);
							else
								beam_add_line(coords.x0, coords.y0, coords.x1, coords.y1, curpoint->intensity, curpoint->col, prev_drawn);
							drew_line = true;
						}
					}
				}
				// A round join belongs at the NEXT shared vertex only if this
				// segment was actually drawn as a line (shots break the polyline).
				prev_drawn = drew_line;
				lastx = curpoint->x;
				lasty = curpoint->y;
```

> Connectivity rule: `joinPrev` for the current segment is "was the previous
> segment drawn as a line?" — i.e. the shared start vertex was itself a lit
> endpoint. A pen-up (intensity 0), a clipped-out segment, or a shot resets it.
> The `VCLIP` branch above does not touch `prev_drawn`, so a clip change does not
> break a continuous stroke. This is the core of fix #2 and has no unit test; it is
> exercised by the joint checks in Phase 2.

- [ ] **Step 4: Branch the draw.** In `opengl_renderer.cpp` `render()`, replace the
vector branch (~lines 967-971). Current:

```cpp
		if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
		{
			vector_update();  // Test, add conditions or move fully to it.
			draw_all();
			vector_clear_list(); // Test - Move this out of here.
		}
```

with:

```cpp
		if (Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
		{
			vector_update();
			if (g_beam_legacy)
			{
				draw_all();
			}
			else
			{
				aae::math::mat4 proj = aae::math::ortho(0.0f, 1024.0f, 0.0f, 1024.0f);
				beam_draw_all(proj);
			}
			vector_clear_list();
		}
```

- [ ] **Step 5: Build**

Run: `msbuild aae.sln /p:Configuration=Release /p:Platform=x64 /m`
Expected: `Build succeeded`.

- [ ] **Step 6: Run + observe (the first real A/B)**

Launch `x64\Release\aae.exe`, start **Asteroids**. Default is legacy (unchanged).
Press **F5** → log shows `MODERN (shader)` and vectors are now drawn by the new
renderer. Expected at this stage:
- Lines render (white ship, rocks, score).
- They will look *close* to legacy but not yet calibrated; joints are bare butt
  caps (no joins yet — small gaps at sharp corners are expected until Phase 2).
- No crash, no `GL` errors in `aae.log`.

Toggle F5 back and forth to confirm both paths run.

- [ ] **Step 7: Commit**

```bash
git add aae/aae/aae_video/mame_vector.cpp aae/aae/vidhrdwr/emu_vector_draw.cpp aae/aae/aae_video/opengl_renderer.cpp
git commit -m "feat(vector): wire beam renderer behind F5 toggle (lines only)"
```

### Task 1.4: Calibrate line width/brightness to match GL_LINES (fix #1)

**Files:**
- Modify: `aae/aae/aae_video/vector_draw.cpp` (`AA_PIXELS`, and a width bias if needed)

- [ ] **Step 1: A/B at each width.** Set `config.linewidth` (in `aae.ini`, or the
in-game video menu) to 1.0 and launch **Battlezone**. Toggle F5 and compare the new
path against legacy. Repeat at 1.5, 2.0, 2.5, 3.0, 3.5. Note any systematic
difference: too thin/thick (adjust the edge), too dim/bright (adjust feather), fuzzy
(reduce feather).

- [ ] **Step 2: Tune the feather.** `AA_PIXELS` (top of `vector_draw.cpp`) is the
edge softness in physical pixels. Start at `1.2f`. If the new lines read fatter than
legacy, lower toward `0.8f`; if they shimmer/alias, raise toward `1.5f`. The
half-coverage point already sits on the geometric edge, so width should match
without a bias; if a consistent bias remains, apply it where `half` is computed in
`beam_add_line`:

```cpp
    float half = config.linewidth * 0.5f + WIDTH_BIAS;   // WIDTH_BIAS default 0.0f
```

(Add `static const float WIDTH_BIAS = 0.0f;` near `AA_PIXELS` only if needed.)

- [ ] **Step 3: Rebuild + re-compare** after each change:
`msbuild aae.sln /p:Configuration=Release /p:Platform=x64 /m`, relaunch, F5.
Acceptance: across 1.0–3.5, toggling F5 shows no obvious width or brightness shift
on Battlezone (B/W) and Tempest (color).

- [ ] **Step 4: Commit**

```bash
git add aae/aae/aae_video/vector_draw.cpp
git commit -m "tune(vector): calibrate beam coverage to match GL_LINES at 1.0-3.5"
```

---

## PHASE 2 — Connectivity-aware round joins (fix #2, requirement R3)

### Task 2.1: Draw the round-join discs

**Files:**
- Modify: `aae/aae/aae_video/vector_draw.cpp` (`beam_draw_all`)

- [ ] **Step 1: Add the join draw** to `beam_draw_all`, immediately after the lines
draw block and before `glBindVertexArray(0)`:

```cpp
    // Joins (round discs at interior shared vertices; radius == beam half-width).
    if (!g_joins.empty()) {
        glUseProgram(progJoin);
        glUniformMatrix4fv(glGetUniformLocation(progJoin, "uProj"), 1, GL_FALSE, value_ptr(const_cast<mat4&>(proj)));
        glUniform1f(glGetUniformLocation(progJoin, "uAA"), g_uAA);
        glBindBuffer(GL_ARRAY_BUFFER, vboJoin);
        glBufferData(GL_ARRAY_BUFFER, g_joins.size()*sizeof(BeamJoin), g_joins.data(), GL_STREAM_DRAW);
        glBindVertexArray(vaoJoin);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)g_joins.size());
    }
```

- [ ] **Step 2: Build**

Run: `msbuild aae.sln /p:Configuration=Release /p:Platform=x64 /m`
Expected: `Build succeeded`.

- [ ] **Step 3: Run + observe (the joint payoff)**

Launch **Asteroids** (ship is a sharp triangle) and **Battlezone** (tank/obstacle
corners). Press F5 to the modern path. Expected:
- Segment corners are now filled — no gaps at the ship's nose or tank corners.
- Joints are **flush, not bulbous**: the disc never extends past the beam width.
- Compare against legacy (F5): legacy shows its `GL_POINTS` dots; modern should look
  cleaner / better-fused. This is the visual confirmation of fix #2.

- [ ] **Step 4: Commit**

```bash
git add aae/aae/aae_video/vector_draw.cpp
git commit -m "feat(vector): connectivity-aware round joins (retires GL_POINTS hack)"
```

---

## PHASE 3 — Black-and-white sort + per-game blend (requirement R2)

The sort and blend selection are already implemented in `beam_draw_all` (Task 1.1,
Step 2): `VECTOR_USES_COLOR` games use additive `GL_SRC_ALPHA, GL_ONE`; B/W games
get the darkest-first `std::sort` then `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`. This
phase is verification only — confirm the requirement holds before cutover.

### Task 3.1: Verify bright-over-dark occlusion and color additivity

- [ ] **Step 1: B/W occlusion.** Launch **Battlezone** (or **Red Baron**). On the
modern path (F5), confirm bright foreground beams render *in front of* dim
background/horizon beams (no dark line punching through a bright one). Toggle F5;
the occlusion ordering should match legacy.

- [ ] **Step 2: Color additivity.** Launch **Tempest** and **Star Wars**. Confirm
overlapping colored beams add brightness (additive look), matching legacy, with no
painter's-sort darkening.

- [ ] **Step 3: Empty-frame safety.** Confirm a blanked vector screen (attract-mode
transitions) doesn't crash — `beam_draw_all` early-returns on empty lists (this is
the fix for the legacy `&linelist[0]` empty-vector UB).

- [ ] **Step 4: Commit** (only if Step 1/2 required a blend tweak; otherwise skip)

```bash
git add aae/aae/aae_video/vector_draw.cpp
git commit -m "verify(vector): B/W painter's sort + color additive parity"
```

---

## PHASE 4 — Procedural shots (requirement R6)

### Task 4.1: Draw procedural shot points

**Files:**
- Modify: `aae/aae/aae_video/vector_draw.cpp` (`beam_draw_all`)

- [ ] **Step 1: Add the shot draw** to `beam_draw_all`, after the joins block. Shots
are always additive regardless of game type:

```cpp
    // Shots (procedural radial core + halo; always additive).
    if (!g_shots.empty()) {
        glUseProgram(progShot);
        glUniformMatrix4fv(glGetUniformLocation(progShot, "uProj"), 1, GL_FALSE, value_ptr(const_cast<mat4&>(proj)));
        glUniform1f(glGetUniformLocation(progShot, "uCorePower"),      6.0f);
        glUniform1f(glGetUniformLocation(progShot, "uBloomPower"),     2.5f);
        glUniform1f(glGetUniformLocation(progShot, "uBloomIntensity"), 0.3f);
        glUniform1f(glGetUniformLocation(progShot, "uOverdrive"),      3.0f);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glBindBuffer(GL_ARRAY_BUFFER, vboShot);
        glBufferData(GL_ARRAY_BUFFER, g_shots.size()*sizeof(BeamShot), g_shots.data(), GL_STREAM_DRAW);
        glBindVertexArray(vaoShot);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)g_shots.size());
    }
```

- [ ] **Step 2: Build**

Run: `msbuild aae.sln /p:Configuration=Release /p:Platform=x64 /m`
Expected: `Build succeeded`.

- [ ] **Step 3: Run + observe**

Launch **Asteroids** and **Asteroids Deluxe** and fire. Expected on the modern path:
- Shots appear as bright procedural points with a soft halo (no texture file used).
- Compare to legacy (F5): the new shots should read as bright/hot as the old
  textured shots. If too dim/bright, tune the four uniform constants in Step 1
  (`uOverdrive` for overall brightness, `uCorePower` for core tightness).

- [ ] **Step 4: Commit**

```bash
git add aae/aae/aae_video/vector_draw.cpp
git commit -m "feat(vector): procedural shot points (no texture)"
```

---

## PHASE 5 — Cutover: make modern default, retire the legacy path

### Task 5.1: Flip the default and remove the dead GL_LINES code

**Files:**
- Modify: `aae/aae/aae_video/opengl_renderer.cpp` (`g_beam_legacy` default; remove `GL_LINE_SMOOTH`/`glLineWidth` reliance)
- Modify: `aae/aae/vidhrdwr/emu_vector_draw.cpp` / `.h` (remove `draw_all`, `add_line`, `add_tex`; keep `modulate_color`, `cache_clear`)
- Modify: `aae/aae/aae_video/mame_vector.cpp` (drop the `g_beam_legacy` branches; emit only `beam_*`)

> Do this only after Phases 1–4 pass A/B on Battlezone, Asteroids, Tempest, Star
> Wars, Red Baron.

- [ ] **Step 1: Flip the default.** In `opengl_renderer.cpp`:

```cpp
bool g_beam_legacy = false;   // modern beam renderer is now the default
```

- [ ] **Step 2: Build + soak.** Build and play all five test games on the default
(no F5). Confirm everything looks right with the modern path as default.

```bash
git add aae/aae/aae_video/opengl_renderer.cpp
git commit -m "feat(vector): default to modern beam renderer"
```

- [ ] **Step 3: Remove the legacy draw path.** In `emu_vector_draw.cpp` delete
`draw_all()`, `add_line()`, `add_tex()`, `add_tex`'s helpers (`cache_texpoint`,
`cache_tex_color`, `set_tex_shot_alpha_scale`), `sort_lines_by_color()`, and the
`linelist`/`texlist` globals. **Keep** `modulate_color()` and `cache_clear()` (now
just `beam_clear()` + any remaining list clears). Remove the matching declarations
from `emu_vector_draw.h`, keeping `modulate_color` and `cache_clear`. Update
`mame_vector.cpp` `vector_update()` to drop the `if (g_beam_legacy)` branches and
call only `beam_add_line` / `beam_add_shot`.

- [ ] **Step 4: Remove fixed-function line state.** In `opengl_renderer.cpp`
`init_gl()`, delete the now-dead vector calls: `glEnable(GL_LINE_SMOOTH)`,
`glEnable(GL_POINT_SMOOTH)`, `glLineWidth(config.linewidth)`,
`glPointSize(config.pointsize)` (lines ~371-376). (Leave general blend/hint state.)

- [ ] **Step 5: Build + verify no regressions**

Run: `msbuild aae.sln /p:Configuration=Release /p:Platform=x64 /m`
Expected: `Build succeeded`, and the five test games render identically to Step 2
(the legacy path and F5 toggle are gone; only the modern renderer remains).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor(vector): remove legacy GL_LINES draw_all and fixed-function line state"
```

> Optional follow-up commit: remove the F5 handler in `aae_emulator.cpp` and
> `g_beam_legacy` if you no longer want the toggle. Keeping it (always modern) is
> harmless; removing it is tidier. Author's call.

---

## PHASE 6 — Fixed 2× supersample for resolution independence (requirement R4)

Render the vectors + final composite at 2048² (2× of the 1024 logical space) while
keeping every coordinate in 1024-logical and **leaving the glow FBOs (512/256)
untouched**. This isolates the change to `fbo1`, `fbo4`, their viewports, and the
beam feather.

### Task 6.1: Add the SSAA factor and a supersampled render-target ortho helper

**Files:**
- Modify: `aae/aae/aae_video/gl_fbo.cpp` / `.h` (`g_ssaa`, supersized `fbo1`/`fbo4`)
- Modify: `aae/aae/aae_video/opengl_renderer.cpp` (`set_ortho_ss` helper)

- [ ] **Step 1: Declare the factor.** In `gl_fbo.h`, after the dimension externs:

```cpp
// Supersample factor for fbo1/fbo4 (the crisp vector + composite targets).
// Glow FBOs (fbo2/fbo3) are intentionally left at base size.
extern int g_ssaa;
```

In `gl_fbo.cpp`, define it near the dimension constants:

```cpp
int g_ssaa = 2;   // 2x supersample (1024 logical -> 2048 physical)
```

- [ ] **Step 2: Supersize fbo1 and fbo4 only.** In `gl_fbo.cpp` `fbo_init()`,
multiply the `fbo1` and `fbo4` attachment dimensions by `g_ssaa`. Leave `fbo2`/`fbo3`
as-is:

```cpp
    create_fbo(fbo1, {
        { &img1a, { width*g_ssaa,  height*g_ssaa  }, false },
        { &img1b, { width*g_ssaa,  height*g_ssaa  }, false },
        { &img1c, { width*g_ssaa,  height*g_ssaa  }, false }
    });
    // fbo2, fbo3 unchanged (512 / 256)...
    create_fbo(fbo4, {
        { &img4a, { width*g_ssaa,  height*g_ssaa  }, true },
        { &img4b, { width*g_ssaa,  height*g_ssaa  }, true }
    });
```

- [ ] **Step 3: Add `set_ortho_ss`.** In `opengl_renderer.cpp`, beside `set_ortho`
(and declare it in `opengl_renderer.h`). It keeps the logical ortho but sets a
physical (supersampled) viewport:

```cpp
// Logical ortho over a supersampled viewport, for the fbo1/fbo4 targets.
void set_ortho_ss(GLint logicalW, GLint logicalH)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glViewport(0, 0, logicalW * g_ssaa, logicalH * g_ssaa);
    glOrtho(0, logicalW, 0, logicalH, -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}
```

Add `#include "gl_fbo.h"` if needed for `g_ssaa`.

- [ ] **Step 4: Build** (no behavior change wired yet beyond the larger buffers)

Run: `msbuild aae.sln /p:Configuration=Release /p:Platform=x64 /m`
Expected: `Build succeeded`.

- [ ] **Step 5: Commit**

```bash
git add aae/aae/aae_video/gl_fbo.cpp aae/aae/aae_video/gl_fbo.h aae/aae/aae_video/opengl_renderer.cpp aae/aae/aae_video/opengl_renderer.h
git commit -m "feat(render): add g_ssaa + supersampled fbo1/fbo4 + set_ortho_ss helper"
```

### Task 6.2: Point the fbo1/fbo4 render sites at the supersampled viewport

**Files:**
- Modify: `aae/aae/aae_video/opengl_renderer.cpp`

> Replace `set_ortho(1024,1024)` with `set_ortho_ss(1024,1024)` and the fbo1 clear
> viewport with the supersampled size, **only** at the FBO-pixel-space sites below.
> Do NOT touch the UI virtual-space `1024×768` sites (`set_ortho(1024,768)`,
> `glOrtho(0,1024,0,768)`, `quad_from_center(512,384,1024,768,…)`,
> `VF.Initialize(1024,768)`, `render_ui_overlays(1024,768)`) or the 512/256 glow
> sites. Enumerated fbo1/fbo4 sites (from current line numbers):

| Line | Current | Change to |
|------|---------|-----------|
| ~519 | `glViewport(0, 0, 1024, 1024);` (hard-clear fbo1) | `glViewport(0, 0, 1024*g_ssaa, 1024*g_ssaa);` |
| ~550 | `set_ortho(1024, 1024);` (set_render_fbo4) | `set_ortho_ss(1024, 1024);` |
| ~797 | `set_ortho(1024, 1024);` (render_ui_overlays vector branch → fbo4) | `set_ortho_ss(1024, 1024);` |
| ~914 | `set_ortho(1024, 1024);` (set_render, vector branch → fbo1) | `set_ortho_ss(1024, 1024);` |
| ~1083 | `set_ortho(1024, 1024);` (final_render LAYER 5A → fbo4 img4b) | `set_ortho_ss(1024, 1024);` |

- [ ] **Step 1: Apply the five edits** above. `FS_Rect(0,1024)` and
`drawTexturedQuad(...,0,1024,...)` calls are **unchanged** — they draw in
1024-logical and correctly fill the supersampled viewport.

- [ ] **Step 2: Build**

Run: `msbuild aae.sln /p:Configuration=Release /p:Platform=x64 /m`
Expected: `Build succeeded`.

- [ ] **Step 3: Run + observe (alignment first, then crispness)**

Launch **Battlezone** at a **4K** window, then at **1280×1024**.
- **Alignment:** the game image must still fill the screen rectangle exactly —
  no half-image, no offset, no doubled UI. (A misclassified `set_ortho` site shows
  here as a quarter/quadrant image.) If broken, re-check that only the five sites
  changed and the 512/256/768 sites did not.
- **UI overlays:** pause (P), open the menu, trigger the exit dialog — text must be
  centered and correctly sized (it now renders at 2× into fbo4).
- **Crispness:** at 4K the vectors should be noticeably crisper than the
  pre-Phase-6 build, and the 4K vs 1280 look should match (same image, scaled).

- [ ] **Step 4: Set the beam feather for 2× and re-verify width.** `beam_init(1)` in
`init_gl()` should now pass the real factor so the feather stays ~1 physical pixel:

```cpp
            vector_start();
            beam_init(g_ssaa);     // was beam_init(1)
```

Rebuild, relaunch Asteroids/Battlezone, and re-confirm the 1.0–3.5 width match from
Task 1.4 still holds at 2×. If lines look slightly soft, nudge `AA_PIXELS` down a
touch; if aliased, up.

- [ ] **Step 5: Commit**

```bash
git add aae/aae/aae_video/opengl_renderer.cpp
git commit -m "feat(render): 2x supersample fbo1/fbo4 render sites; beam feather tracks ssaa"
```

### Task 6.3: Confirm the glow/trail look is preserved

- [ ] **Step 1: Glow parity.** With `config.vecglow` on, launch **Tempest** and
**Star Wars**. The glow downsamples from the (now 2048) `img1b` into the unchanged
512/256 glow FBOs, so the glow should look the same as before Phase 6. Compare
against the Phase 5 build if unsure (git stash / rebuild).

- [ ] **Step 2: Trail parity.** With `config.vectrail` on, confirm phosphor
persistence looks unchanged.

- [ ] **Step 3: GL errors.** Check `aae.log` for any `check_gl_error_named`
warnings around `copy_main_img_to_fbo2` / `final_render` (a size mismatch would log
here). Expected: none.

- [ ] **Step 4: Commit** (only if a glow tweak was needed; otherwise skip)

```bash
git add aae/aae/aae_video/opengl_renderer.cpp
git commit -m "verify(render): glow/trail parity at 2x supersample"
```

---

## Final acceptance (maps to spec requirements)

| Req | Check | Where verified |
|-----|-------|----------------|
| R1 — match GL_LINES 1.0–3.5, B/W + color | F5 A/B shows no width/brightness shift | Task 1.4 |
| R2 — B/W painter's sort, bright over dark | Battlezone/Red Baron occlusion | Task 3.1 |
| R3 — better joints than GL_POINTS | Asteroids ship / Battlezone corners flush, no bulge | Task 2.1 |
| R4 — identical at 4K and 1280 | Battlezone at both resolutions, same look, crisp 4K | Task 6.2 |
| R5 — GL4 shaders, no fixed-function in vector path | legacy draw + GL_LINE_SMOOTH/glLineWidth removed | Task 5.1 |
| R6 — procedural shots, no texture | Asteroids shots are shader-drawn | Task 4.1 |
| Glow/trail preserved | Tempest/Star Wars glow + trail unchanged | Task 6.3 |

## Out of scope (future session)

HDR brightness headroom: move `fbo1/2/3/4` to `GL_RGBA16F`, let beams/glow exceed
1.0, and tonemap in `end_render_fbo4`. The beam shaders already emit linear color +
coverage, so this is a contained pipeline-format + tonemap change.
