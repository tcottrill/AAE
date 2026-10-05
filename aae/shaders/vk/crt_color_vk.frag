// crt_color_vk.frag - VK port of the GL colorMonitorFrag
// (shader_definitions.h:445). Color raster games only.
//
// The fragment MATH IS VERBATIM from the GL source; only the uniform plumbing
// and the shadow mask's fragment-coordinate ORIGIN changed. Uniform-for-uniform:
//   uTex          -> set 0 binding 0 (the game RT, full mip chain)
//   uSrcSize      -> pc.p0.xy
//   uLodBias      -> pc.p0.z   (log2(config.prescale) - see crt_mono_vk.frag)
//   uBlurH        -> pc.p0.w
//   uBlurV        -> pc.p1.x
//   uConverge     -> pc.p1.y
//   uHalation     -> pc.p1.z
//   uHalRadius    -> pc.p1.w
//   uScanline     -> pc.p2.x
//   uContrast     -> pc.p2.y
//   uBright       -> pc.p2.z
//   uSaturation   -> pc.p2.w
//   uMaskType     -> pc.p3.x  (int in GL; carried as float, compared exactly -
//                              the recorder writes 0.0/1.0/2.0)
//   uMaskStrength -> pc.p3.y
//   uMaskScale    -> pc.p3.z
// (pc.tint is mono-pass-only and unread here.)
//
// Mask origin: GL evaluates the mask at raw gl_FragCoord because its monitor
// pass renders into fbo_mono, which is resized every frame to the ON-SCREEN
// game rectangle - so its (0,0) is the game rect's corner and 1 fragment = 1
// screen pixel ("pixel-exact output sizing"). The VK pass draws the monitor
// quad DIRECTLY onto the swapchain at the letterbox rect (same 1 fragment =
// 1 screen pixel, one resample fewer), so the rect origin is subtracted here
// to put the mask phase back on the game rect corner. pc.tsize.zw carries it.
// Documented deviation: GL's fbo_mono origin is the rect's BOTTOM-left and
// this one is the TOP-left, so for the dot-triad / slot-mask types the mask's
// vertical phase can differ by a sub-triad offset. Pitch is identical, and
// the aperture-grille type (the default) has no vertical term at all.
#version 450

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 tsize;   // targetW, targetH, fragOffX, fragOffY
    vec4 uvrect;
    vec4 p0;      // srcW, srcH, lodBias, blurH
    vec4 p1;      // blurV, converge, halation, halRadius
    vec4 p2;      // scanline, contrast, bright, saturation
    vec4 p3;      // maskType, maskStrength, maskScale, softPhosphor
    vec4 tint;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;

// Shadow-mask phosphor dots on a triangular (delta-gun) lattice: the
// band-limited sum of three cosines, +3 on the lattice points, -1.5 between
// them, 0 on average. Components approaching Nyquist fade toward that
// average, so a pitch too fine for the display degrades to "no mask"
// instead of moire. Integrated over the pixel box (sinc terms).
float softPhosphorDots(vec2 q, vec2 qdx, vec2 qdy) {
    const float tau = 6.28318530718;
    vec3 phase = tau * vec3(q.x - q.y / 1.732050808,
                            q.x + q.y / 1.732050808,
                            2.0 * q.y / 1.732050808);
    // Explicit first-order gradients: differentiating q again would nest
    // derivatives through outputSize, which GLSL leaves undefined.
    vec3 dx = tau * vec3(qdx.x - qdx.y / 1.732050808,
                         qdx.x + qdx.y / 1.732050808,
                         2.0 * qdx.y / 1.732050808);
    vec3 dy = tau * vec3(qdy.x - qdy.y / 1.732050808,
                         qdy.x + qdy.y / 1.732050808,
                         2.0 * qdy.y / 1.732050808);
    vec3 freq = max(abs(dx), abs(dy)) / tau;
    vec3 fade = vec3(1.0) - smoothstep(vec3(0.30), vec3(0.5), freq);
    vec3 hx = max(abs(dx) * 0.5, vec3(0.00001));
    vec3 hy = max(abs(dy) * 0.5, vec3(0.00001));
    return dot(cos(phase), fade * (sin(hx) / hx) * (sin(hy) / hy));
}

vec3 softPhosphor(vec3 col, vec2 uv, float strength, float scale) {
    // Screen-relative pitch: identical tube density at 1080p and 2160p.
    // Derivatives measure the actual game rectangle, including letterboxing.
    vec2 outputSize = vec2(
        1.0 / max(length(vec2(dFdx(uv.x), dFdy(uv.x))), 0.000001),
        1.0 / max(length(vec2(dFdx(uv.y), dFdy(uv.y))), 0.000001));
    // Dot pitch in output px at 1080p: MASK SIZE 1 = 3 px, +1.5 px per step.
    // From ~4.5 px (size 2) up the lattice is fully resolved at 1080p; finer
    // pitches fade toward no mask rather than alias.
    float pitch = max((1.5 + 1.5 * scale) * outputSize.y / 1080.0, 0.001);
    vec2 q = uv * outputSize / pitch;
    vec2 qdx = dFdx(uv) * outputSize / pitch;
    vec2 qdy = dFdy(uv) * outputSize / pitch;
    vec3 lattice = vec3(softPhosphorDots(q, qdx, qdy),
                        softPhosphorDots(q - vec2(0.5, 0.288675135), qdx, qdy),
                        softPhosphorDots(q - vec2(0.0, 0.577350269), qdx, qdy));
    // Per-gun transmittance: 1 on that gun's own dots, 0 between them,
    // 1/3 on average.
    vec3 dots = clamp((lattice + 1.5) / 4.5, 0.0, 1.0);
    // Blend toward the mask by strength (sqrt gives the low end of the
    // control a usable range), then restore the average light the mask
    // removes. Bright strokes clip at the dot centres and go dark between
    // them, so saturated vectors break into triads as on a real tube; dim
    // strokes keep their average brightness. A fully faded lattice is 1/3
    // everywhere, which the gain maps back to exactly the unmasked image.
    float depth = sqrt(clamp(strength, 0.0, 1.0));
    vec3  mask  = mix(vec3(1.0), dots, depth);
    float gain  = 1.0 / (1.0 - depth * (2.0 / 3.0));
    return clamp(clamp(col, 0.0, 1.0) * mask * gain, 0.0, 1.0);
}


void main(){
    if (pc.p3.w != 0.0) {
        fragColor = vec4(softPhosphor(texture(uTex, vUV).rgb, vUV,
                                     pc.p3.y, pc.p3.z), 1.0);
        return;
    }
    vec2  uSrcSize      = pc.p0.xy;
    float uLodBias      = pc.p0.z;
    float uBlurH        = pc.p0.w;
    float uBlurV        = pc.p1.x;
    float uConverge     = pc.p1.y;
    float uHalation     = pc.p1.z;
    float uHalRadius    = pc.p1.w;
    float uScanline     = pc.p2.x;
    float uContrast     = pc.p2.y;
    float uBright       = pc.p2.z;
    float uSaturation   = pc.p2.w;
    int   uMaskType     = int(pc.p3.x);
    float uMaskStrength = pc.p3.y;
    float uMaskScale    = pc.p3.z;

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
        vec2  mp = (gl_FragCoord.xy - pc.tsize.zw) / max(uMaskScale, 1.0);
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
