//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2025-2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
#pragma once

#include "vector_draw_gl.h"  // draw_textured_shots (referenced in comments below)

// Old Style Blur Shader
// VS
const char* vertText = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uProj;

out vec2 TexCoord;

void main()
{
    TexCoord = aUV;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

// FS
const char* fragText = R"glsl(
#version 330 core

uniform sampler2D colorMap;
uniform float width;
uniform float height;

in vec2 TexCoord;
out vec4 FragColor; // Modern GLSL uses custom output variables instead of gl_FragColor

void main()
{
    float step_w = 1.0 / width;
    float step_h = 1.0 / height;

    vec2 offset[9] = vec2[](
        vec2(-step_w, -step_h), vec2(0.0, -step_h), vec2(step_w, -step_h),
        vec2(-step_w, 0.0),     vec2(0.0, 0.0),     vec2(step_w, 0.0),
        vec2(-step_w, step_h),  vec2(0.0, step_h),  vec2(step_w, step_h)
    );

    float kernel[9] = float[](
        1.0/17.0, 2.0/17.0, 1.0/17.0,
        2.0/17.0, 4.0/17.0, 2.0/17.0,
        1.0/17.0, 2.0/17.0, 1.0/17.0
    );

    vec4 sum = vec4(0.0);
    for (int i = 0; i < 9; i++)
    {
        // texture2D is deprecated in modern GLSL, replaced by texture()
        vec4 tmp = texture(colorMap, TexCoord + offset[i]);
        sum += tmp * kernel[i];
    }

    FragColor = sum * 1.12;
}
)glsl";

// ---------------------------------------------------------------------------
// Dual-filter pyramid blur (Kawase/Bjorge) - the [main] glow_filter=1 path.
//
// Two tiny kernels run over a downsample/upsample chain instead of many taps
// at one resolution. Spread comes from pyramid DEPTH (the 32x32 level spans
// 8x8 glow-buffer pixels per texel), not pass count, which is why the whole
// chain costs ~0.8M taps against the classic path's ~7.7M. Designed at ARM
// for tile-based GPUs: no blending, no mipmap generation, pure overwrites.
//
// Both shaders reuse vertText as their vertex stage.
// ---------------------------------------------------------------------------
const char* dualDownFrag = R"glsl(
#version 330 core

uniform sampler2D uSrc;
uniform vec2 uHalfPixel;   // 0.5/dstSize * spread; diagonal taps land between
                           // source texels so bilinear averages 4 for free

in vec2 TexCoord;
out vec4 FragColor;

void main()
{
    vec3 sum = texture(uSrc, TexCoord).rgb * 4.0;
    sum += texture(uSrc, TexCoord + uHalfPixel).rgb;
    sum += texture(uSrc, TexCoord - uHalfPixel).rgb;
    sum += texture(uSrc, TexCoord + vec2(uHalfPixel.x, -uHalfPixel.y)).rgb;
    sum += texture(uSrc, TexCoord - vec2(uHalfPixel.x, -uHalfPixel.y)).rgb;
    FragColor = vec4(sum * (1.0 / 8.0), 1.0);
}
)glsl";

const char* dualUpFrag = R"glsl(
#version 330 core

uniform sampler2D uSrc;      // the lower (smaller) pyramid level
uniform sampler2D uAdd;      // same-res detail re-injected on the way up
uniform vec2  uHalfPixel;    // 0.5/srcSize * spread
uniform float uAddWeight;    // 0 = pure wide halo; raising it hardens the core
uniform float uGain;         // output gain (only the final pass sets != 1)

in vec2 TexCoord;
out vec4 FragColor;

void main()
{
    vec3 sum;
    sum  = texture(uSrc, TexCoord + vec2(-uHalfPixel.x * 2.0, 0.0)).rgb;
    sum += texture(uSrc, TexCoord + vec2(-uHalfPixel.x,  uHalfPixel.y)).rgb * 2.0;
    sum += texture(uSrc, TexCoord + vec2(0.0,  uHalfPixel.y * 2.0)).rgb;
    sum += texture(uSrc, TexCoord + vec2( uHalfPixel.x,  uHalfPixel.y)).rgb * 2.0;
    sum += texture(uSrc, TexCoord + vec2( uHalfPixel.x * 2.0, 0.0)).rgb;
    sum += texture(uSrc, TexCoord + vec2( uHalfPixel.x, -uHalfPixel.y)).rgb * 2.0;
    sum += texture(uSrc, TexCoord + vec2(0.0, -uHalfPixel.y * 2.0)).rgb;
    sum += texture(uSrc, TexCoord + vec2(-uHalfPixel.x, -uHalfPixel.y)).rgb * 2.0;
    vec3 wide = sum * (1.0 / 12.0);

    // Narrow+wide sum is what reproduces the classic accumulate blur's
    // hot-core/soft-tail shape - a single wide gaussian reads as mush.
    vec3 core = texture(uAdd, TexCoord).rgb * uAddWeight;
    FragColor = vec4((wide + core) * uGain, 1.0);
}
)glsl";

// Multitexturing Combining Shaders
// VS
const char* texvertText = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uProj;

out vec2 TexCoord0;

void main(void)
{
    TexCoord0 = aUV;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

// FS
const char* texfragText = R"glsl(
#version 330 core

uniform int usefb;
uniform int useglow;
uniform float glowamt;
uniform int brighten; // Note: Maybe can be removed

uniform sampler2D mytex2; // Vectors
uniform sampler2D mytex3; // Glow
uniform sampler2D mytex4; // Feedback

in vec2 TexCoord0;
out vec4 FragColor;

void main(void)
{
    float bval = 1.0;
    vec2 uv = TexCoord0;

    vec4 texval2 = texture(mytex2, uv); // Vectors
    vec4 texval3 = texture(mytex3, uv); // Glow
    vec4 texval4 = texture(mytex4, uv); // Feedback
   
    vec4 result = texval2 * bval;

    if (useglow > 0) result += texval3 * glowamt;
    if (usefb > 0)   result += texval4 * 0.25;

    FragColor = result;
}
)glsl";

// ---------------------------------------------------------
// Basic Texture Shader (Replaces fixed-function texturing)
// ---------------------------------------------------------
const char* basicTexVert = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uProj;

out vec2 TexCoord;

void main()
{
    TexCoord = aUV;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* basicTexFrag = R"glsl(
#version 330 core

uniform sampler2D u_texture;
uniform vec4 uColor;
uniform float uAlphaTest;   // 0 = no test; else discard fragments below this alpha

in vec2 TexCoord;
out vec4 FragColor;

void main()
{
    vec4 c = texture(u_texture, TexCoord) * uColor;
    if (c.a < uAlphaTest) discard;
    FragColor = c;
}
)glsl";

// ---------------------------------------------------------
// Basic Color Shader (Replaces fixed-function colored quads)
// ---------------------------------------------------------
const char* basicColorVert = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uProj;

out vec4 VertColor;

void main()
{
    VertColor = aColor;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* basicColorFrag = R"glsl(
#version 330 core

in vec4 VertColor;
out vec4 FragColor;

void main()
{
    FragColor = VertColor;
}
)glsl";


// ---------------------------------------------------------
// Scanline Multiply Shader
// Tiles a scanline texture over a fullscreen quad using
// GL_REPEAT-style UV math, then multiplies it against the
// existing framebuffer contents via blending (DST_COLOR, ZERO).
// Replaces the fixed-function glBegin/glEnd scanline overlay.
// ---------------------------------------------------------
const char* scanlineMultiplyVert = R"glsl(
#version 330 core

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inTex;

uniform mat4 u_projection;

out vec2 TexCoord;

void main()
{
    gl_Position = u_projection * vec4(inPos, 0.0, 1.0);
    TexCoord    = inTex;
}
)glsl";

const char* scanlineMultiplyFrag = R"glsl(
#version 330 core

uniform sampler2D u_scanTex;

in vec2 TexCoord;
out vec4 FragColor;

void main()
{
    FragColor = texture(u_scanTex, TexCoord);
}
)glsl";


// ---------------------------------------------------------
// Star Point Shader (VBO/VAO point rendering for GUI stars)
// ---------------------------------------------------------
const char* starPointVert = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uProj;

out vec4 VertColor;

void main()
{
    VertColor = aColor;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* starPointFrag = R"glsl(
#version 330 core

in vec4 VertColor;
out vec4 FragColor;

void main()
{
    FragColor = VertColor;
}
)glsl";


// ---------------------------------------------------------
// Textured + per-vertex-color shader
// Replaces the fixed-function GL_MODULATE client-array path used by the legacy
// textured shots (draw_textured_shots): FragColor = texture(uv) * vertexColor.
// ---------------------------------------------------------
const char* texColorVert = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform mat4 uProj;

out vec2 TexCoord;
out vec4 VertColor;

void main()
{
    TexCoord  = aUV;
    VertColor = aColor;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* texColorFrag = R"glsl(
#version 330 core

uniform sampler2D u_texture;
uniform float uFadeInner;   // radius (0=center .. 1=edge) of the full-bright core
uniform float uFadeOuter;   // radius where the edge fade reaches zero

in vec2 TexCoord;
in vec4 VertColor;
out vec4 FragColor;

void main()
{
    vec4 c = texture(u_texture, TexCoord) * VertColor;
    // Radial edge fade: full brightness inside uFadeInner, ramping to 0 by
    // uFadeOuter so the square texture/quad boundary disappears on the additive
    // halo. The bright center is untouched (fade = 1 there).
    float d = length(TexCoord - vec2(0.5)) * 2.0;   // 0 center, 1 edge, ~1.41 corner
    float fade = 1.0 - smoothstep(uFadeInner, uFadeOuter, d);
    FragColor = vec4(c.rgb * fade, c.a);
}
)glsl";






// ---------------------------------------------------------
// Mono Monitor Shader (B/W raster games only)
//
// Ported from the PET emulator's CRT_FS_SRC (pet_gl.cpp): single-pass
// B&W/green-screen CRT simulation. All work happens in SOURCE-pixel
// space (the game's native visible area) so the look is identical at
// any window size or prescale:
//   1. Gaussian beam spot  -- 7x3 taps; uBlurH is the video-bandwidth
//      softness along the scanline, uBlurV a touch of vertical spot size.
//   2. Beam overdrive      -- video gain clamped like a saturating phosphor.
//   3. Halation            -- 4 textureLod() taps from the source texture's
//      mip pyramid (fbo_generate_mipmaps before the pass), screen-blended.
//   4. Optional beam ripple keyed to the native raster lines (default 0).
//   5. Black-level lift + phosphor tint multiply.
//
// AAE differences vs the PET original:
//   - uLodBias: the source texture is native size * prescale, so one game
//     pixel spans prescale texels. The halation mip level is computed from
//     the radius in game pixels and shifted by log2(prescale) to land on
//     the equivalent level of the larger texture. 0 at prescale 1.
//   - The scanline ripple uses a 1-row pitch (AAE renders native rows);
//     the PET version assumed its line-doubled 400-row framebuffer.
// ---------------------------------------------------------
const char* monoMonitorVert = R"glsl(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uProj;

out vec2 vUV;

void main()
{
    vUV = aUV;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* monoMonitorFrag = R"glsl(
#version 330 core

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uTex;
uniform vec2  uSrcSize;    // game visible area in NATIVE pixels (oriented)
uniform float uLodBias;    // log2(prescale): game-px radius -> texture mip level
uniform float uBlurH;      // horizontal spot sigma, source px
uniform float uBlurV;      // vertical spot sigma, source px
uniform float uHalation;   // glow strength 0..1
uniform float uHalRadius;  // glow radius, source px
uniform float uScanline;   // beam ripple strength 0..1 (0 = off)
uniform float uContrast;   // video gain (>=1); overdrive fattens strokes via saturation
uniform float uBright;     // black-level lift 0..0.25 (misadjusted-tube glow)
uniform vec3  uTint;       // phosphor tint (1,1,1 = white P4)

void main(){
    vec2 px = 1.0 / uSrcSize;

    // 1) Gaussian spot (7 horizontal x 3 vertical taps, source-pixel space).
    //    Near-zero sigma degenerates to the plain (bilinear) sample.
    //
    //    Tap spacing scales with sigma (min 0.35 px) instead of the PET's
    //    fixed 1 px comb. A Gaussian narrower than its tap spacing cannot be
    //    sampled: fixed 1 px taps turn small sigmas into "identity + faint
    //    replica exactly 1 px over" instead of a soft edge. With spacing ~=
    //    sigma the 7 taps always span +-3 sigma, so the blur is a true
    //    Gaussian across the whole knob range (sub-pixel softening at 0.2,
    //    wide beam at 2.5) sampled from the bilinear-reconstructed image.
    float sh = max(uBlurH, 0.02);
    float sv = max(uBlurV, 0.02);
    float dx = max(sh, 0.35);   // horizontal tap spacing, source px
    float dy = max(sv, 0.35);   // vertical tap spacing, source px
    vec3  acc  = vec3(0.0);
    float wsum = 0.0;
    for (int i = -3; i <= 3; ++i) {
        float ox = float(i) * dx;
        float wx = exp(-0.5 * ox * ox / (sh*sh));
        for (int j = -1; j <= 1; ++j) {
            float oy = float(j) * dy;
            float w = wx * exp(-0.5 * oy * oy / (sv*sv));
            acc  += w * texture(uTex, vUV + vec2(ox, oy) * px).rgb;
            wsum += w;
        }
    }
    vec3 col = acc / wsum;

    // 1b) Beam overdrive: video gain, clamped like a saturating phosphor.
    //     Gain pushes the spot's dim skirt past full-white, so strokes get
    //     FATTER while the clamp keeps their edges hard (fat, not blurry).
    //     Clamp before halation: the screen blend needs values <= 1.
    col = min(col * uContrast, vec3(1.0));

    // 2) Halation: cheap wide blur from the mip pyramid, screen blend so the
    //    glow brightens darks without clipping whites.
    if (uHalation > 0.0) {
        float lod = log2(max(uHalRadius, 1.0)) + uLodBias;
        vec2  o   = px * uHalRadius * 0.5;
        vec3 glow = textureLod(uTex, vUV + vec2(-o.x, -o.y), lod).rgb
                  + textureLod(uTex, vUV + vec2( o.x, -o.y), lod).rgb
                  + textureLod(uTex, vUV + vec2(-o.x,  o.y), lod).rgb
                  + textureLod(uTex, vUV + vec2( o.x,  o.y), lod).rgb;
        glow *= 0.25;
        col = 1.0 - (1.0 - col) * (1.0 - glow * uHalation);
    }

    // 3) Optional soft beam ripple, 1-row pitch: beam centers sit at row
    //    centers (fbY = k + 0.5), gaps at the row boundaries.
    if (uScanline > 0.0) {
        float fbY = vUV.y * uSrcSize.y;
        float w   = 0.5 + 0.5 * cos(6.2831853 * (fbY - 0.5)); // 1 at centers, 0 in gaps
        col *= 1.0 - uScanline * (1.0 - w);
    }

    // 3b) Black-level lift, before tint so the raised background glows in the
    //     phosphor color rather than gray.
    col += uBright;

    // 4) Phosphor tint (monochrome image -> green screen / amber / B&W).
    col *= uTint;
    fragColor = vec4(col, 1.0);
}
)glsl";

// ---------------------------------------------------------------------------
// Color CRT monitor ("80s arcade RGB monitor"). Shares monoMonitorVert.
// Same beam/halation/scanline pipeline as the mono shader, plus RGB
// misconvergence, saturation, and shadow-mask emulation (aperture grille,
// slot mask, or dot triad) evaluated in output-pixel space.
// ---------------------------------------------------------------------------
const char* colorMonitorFrag = R"glsl(
#version 330 core

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uTex;
uniform vec2  uSrcSize;      // game visible area in NATIVE pixels (oriented)
uniform float uLodBias;      // log2(prescale): game-px radius -> texture mip level
uniform float uBlurH;        // horizontal spot sigma, source px
uniform float uBlurV;        // vertical spot sigma, source px
uniform float uConverge;     // horizontal RGB misconvergence, source px (R left, B right)
uniform float uHalation;     // glow strength 0..1
uniform float uHalRadius;    // glow radius, source px
uniform float uScanline;     // scanline strength 0..1 (0 = off)
uniform float uContrast;     // video gain (>=1)
uniform float uBright;       // black-level lift 0..0.25
uniform float uSaturation;   // 0 = grayscale, 1 = neutral, 2 = overdriven
uniform int   uMaskType;     // 0 = aperture grille, 1 = slot mask, 2 = dot triad
uniform float uMaskStrength; // 0..1: how dark the "wrong" phosphors get
uniform float uMaskScale;    // width of one phosphor stripe in OUTPUT px

void main(){
    vec2 px = 1.0 / uSrcSize;

    // 1) Gaussian beam spot (7x3 taps, source-pixel space), sampled per
    //    channel with a horizontal convergence error: R pulled left, B
    //    pushed right, G on axis -- the classic mis-adjusted-yoke fringe.
    float sh = max(uBlurH, 0.02);
    float sv = max(uBlurV, 0.02);
    float dx = max(sh, 0.35);
    float dy = max(sv, 0.35);
    vec2 co = vec2(uConverge * px.x, 0.0);
    vec3  acc  = vec3(0.0);
    float wsum = 0.0;
    for (int i = -3; i <= 3; ++i) {
        float ox = float(i) * dx;
        float wx = exp(-0.5 * ox * ox / (sh*sh));
        for (int j = -1; j <= 1; ++j) {
            float oy = float(j) * dy;
            float w  = wx * exp(-0.5 * oy * oy / (sv*sv));
            vec2 uv  = vUV + vec2(ox, oy) * px;
            acc.r += w * texture(uTex, uv - co).r;
            acc.g += w * texture(uTex, uv).g;
            acc.b += w * texture(uTex, uv + co).b;
            wsum  += w;
        }
    }
    vec3 col = acc / wsum;

    // 2) Beam overdrive, clamped like a saturating phosphor.
    col = min(col * uContrast, vec3(1.0));

    // 3) Halation from the source mip pyramid, screen blend.
    if (uHalation > 0.0) {
        float lod = log2(max(uHalRadius, 1.0)) + uLodBias;
        vec2  o   = px * uHalRadius * 0.5;
        vec3 glow = textureLod(uTex, vUV + vec2(-o.x, -o.y), lod).rgb
                  + textureLod(uTex, vUV + vec2( o.x, -o.y), lod).rgb
                  + textureLod(uTex, vUV + vec2(-o.x,  o.y), lod).rgb
                  + textureLod(uTex, vUV + vec2( o.x,  o.y), lod).rgb;
        glow *= 0.25;
        col = 1.0 - (1.0 - col) * (1.0 - glow * uHalation);
    }

    // 4) Saturation (NTSC luma weights).
    float luma = dot(col, vec3(0.299, 0.587, 0.114));
    col = max(mix(vec3(luma), col, uSaturation), vec3(0.0));

    // 5) Scanlines, 1-row pitch: beam centers at row centers, gaps at row
    //    boundaries. Brighter pixels bloom into the gaps a little (the beam
    //    widens with current), so full-white areas keep more of their light.
    if (uScanline > 0.0) {
        float fbY = vUV.y * uSrcSize.y;
        float w   = 0.5 + 0.5 * cos(6.2831853 * (fbY - 0.5));
        float bloom = mix(1.0, 0.55, luma);   // bright beam -> shallower gaps
        col *= 1.0 - uScanline * bloom * (1.0 - w);
    }

    // 6) Shadow mask, evaluated in OUTPUT pixel space so the triad pitch is
    //    independent of the game resolution. m is the transmittance of the
    //    "wrong" phosphors; the average loss is compensated with a gain so
    //    turning the mask up doesn't just dim the picture (whites clip into
    //    the stripes, which is what a real tube does).
    if (uMaskStrength > 0.0) {
        float m = 1.0 - uMaskStrength;
        vec2  mp = gl_FragCoord.xy / max(uMaskScale, 1.0);
        float xI = mp.x;

        if (uMaskType == 2) {
            // dot triad: alternate phosphor rows shifted by 1.5 stripes
            float row = floor(mp.y);
            xI += (mod(row, 2.0) < 1.0) ? 0.0 : 1.5;
        }

        float sub = mod(floor(xI), 3.0);
        vec3 mask = (sub < 0.5) ? vec3(1.0, m, m)
                  : (sub < 1.5) ? vec3(m, 1.0, m)
                                : vec3(m, m, 1.0);

        if (uMaskType == 1) {
            // slot mask: triad columns staggered vertically, with a thin
            // horizontal slot gap dimming all three phosphors
            float triad = floor(xI / 3.0);
            float stag  = (mod(triad, 2.0) < 1.0) ? 0.0 : 0.5;
            float vph   = fract(mp.y / 6.0 + stag);
            if (vph < 0.15)
                mask *= m;
        }

        float gain = 3.0 / (1.0 + 2.0 * m);   // restore average brightness
        col = min(col * mask * gain, vec3(1.0));
    }

    // 7) Black-level lift last, so the raised background shows the mask too.
    col += uBright;
    fragColor = vec4(col, 1.0);
}
)glsl";


// ---------------------------------------------------------------------------
// Color VECTOR monitor (Wells-Gardner 6100 / Amplifone class). Shares
// monoMonitorVert. Applied on the final backbuffer blit of the vector
// composite (img4a), so gl_FragCoord is true screen pixels and the mask is
// pixel-exact by construction. Two effects, both tube properties:
//  * radial RGB misconvergence -- three guns that never quite meet, clean
//    at screen center, red/blue fringing growing quadratically to the edges
//  * shadow-mask grain -- unlike B/W vector tubes (continuous phosphor),
//    color X-Y monitors were ordinary delta-gun shadow-mask CRTs, so every
//    vector is chopped into RGB phosphor triads
// (The third WG6100 trait, choppy DAC-quantized vectors, is CPU-side: see
// beam_set_quantize in vector_draw.cpp.)
// ---------------------------------------------------------------------------
const char* colorVectorFrag = R"glsl(
#version 330 core

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uTex;
uniform float uGain;         // monitor gain / beam overdrive (>=1), phosphor-clamped
uniform float uConverge;     // misconvergence at the screen edge, in SOURCE (img4a) texels
uniform float uMaskStrength; // 0..1: how dark the "wrong" phosphors get
uniform float uMaskScale;    // phosphor stripe width in WINDOW px
uniform int   uMaskType;     // 0 = aperture grille, 1 = slot mask, 2 = dot triad

void main(){
    // Radial misconvergence: offset grows with the SQUARE of the distance
    // from center (deflection error), R pulled inward, B pushed outward.
    vec2  r   = vUV - vec2(0.5);
    float d   = length(r) * 2.0;                       // 0 center .. ~1 edge
    vec2  dir = (d > 0.0001) ? normalize(r) : vec2(0.0);
    vec2  off = dir * (uConverge / 1024.0) * d * d;    // img4a is 1024x1024

    vec3 col;
    col.r = texture(uTex, vUV - off).r;
    col.g = texture(uTex, vUV).g;
    col.b = texture(uTex, vUV + off).b;

    // Monitor gain: video overdrive clamped like a saturating phosphor
    // (fattens bright strokes, lifts the dim glow -- the "cranked cab" knob).
    col = min(col * uGain, vec3(1.0));

    // Shadow-mask grain in window-pixel space (same construction as the
    // color raster monitor; WG6100 was a delta-gun tube -> dot triad).
    if (uMaskStrength > 0.0) {
        float m  = 1.0 - uMaskStrength;
        vec2  mp = gl_FragCoord.xy / max(uMaskScale, 1.0);
        float xI = mp.x;

        if (uMaskType == 2) {
            float row = floor(mp.y);
            xI += (mod(row, 2.0) < 1.0) ? 0.0 : 1.5;
        }

        float sub = mod(floor(xI), 3.0);
        vec3 mask = (sub < 0.5) ? vec3(1.0, m, m)
                  : (sub < 1.5) ? vec3(m, 1.0, m)
                                : vec3(m, m, 1.0);

        if (uMaskType == 1) {
            float triad = floor(xI / 3.0);
            float stag  = (mod(triad, 2.0) < 1.0) ? 0.0 : 0.5;
            float vph   = fract(mp.y / 6.0 + stag);
            if (vph < 0.15)
                mask *= m;
        }

        float gain = 3.0 / (1.0 + 2.0 * m);
        col = min(col * mask * gain, vec3(1.0));
    }

    fragColor = vec4(col, 1.0);
}
)glsl";


// New 330 Blur Shaders
/*
const char* vertText = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 modelViewProjection;

void main()
{
    TexCoord = aTexCoord;
    gl_Position = modelViewProjection * vec4(aPos, 1.0);
}
)glsl";

const char* fragText = R"glsl(
#version 330 core

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D colorMap;
uniform float width;
uniform float height;

void main()
{
    float step_w = 1.0 / width;
    float step_h = 1.0 / height;

    vec2 offset[9] = vec2[](
        vec2(-step_w, -step_h), vec2(0.0, -step_h), vec2(step_w, -step_h),
        vec2(-step_w, 0.0),     vec2(0.0, 0.0),     vec2(step_w, 0.0),
        vec2(-step_w, step_h),  vec2(0.0, step_h),  vec2(step_w, step_h)
    );

    float kernel[9] = float[](
        1.0/17.0, 2.0/17.0, 1.0/17.0,
        2.0/17.0, 4.0/17.0, 2.0/17.0,
        1.0/17.0, 2.0/17.0, 1.0/17.0
    );

    vec4 sum = vec4(0.0);
    for (int i = 0; i < 9; i++)
    {
        vec4 tmp = texture(colorMap, TexCoord + offset[i]);
        sum += tmp * kernel[i];
    }

    FragColor = sum * 1.12;
}
)glsl";

*/