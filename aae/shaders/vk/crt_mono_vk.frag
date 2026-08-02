// crt_mono_vk.frag - VK port of the GL monoMonitorFrag
// (shader_definitions.h:353). B/W raster games only.
//
// The fragment MATH IS VERBATIM from the GL source; only the uniform plumbing
// changed (GLSL 330 loose uniforms -> one 128-byte push block, see
// crt_post_vk.vert for the layout). Uniform-for-uniform:
//   uTex       -> set 0 binding 0 (the game RT, full mip chain)
//   uSrcSize   -> pc.p0.xy   (game visible area in NATIVE pixels, oriented)
//   uLodBias   -> pc.p0.z    (ALWAYS 0 under VK - see below)
//   uBlurH     -> pc.p0.w
//   uBlurV     -> pc.p1.x
//   uHalation  -> pc.p1.z
//   uHalRadius -> pc.p1.w
//   uScanline  -> pc.p2.x
//   uContrast  -> pc.p2.y
//   uBright    -> pc.p2.z
//   uTint      -> pc.tint.rgb
// (pc.p1.y = uConverge and pc.p2.w / pc.p3 are color-pass-only and unread here.)
//
// uLodBias note: GL's source texture is native size * config.prescale, so its
// halation mip level is shifted by log2(prescale). The VK game RT is UNSCALED
// source pixels (prescale is a GL-only supersampling knob - see the s_rtGame
// comment in vulkan_renderer.cpp), so one game pixel is exactly one texel and
// the VK equivalent of uLodBias is 0. The recorder passes 0 unconditionally.
#version 450

layout(set = 0, binding = 0) uniform sampler2D uTex;

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 tsize;
    vec4 uvrect;
    vec4 p0;      // srcW, srcH, lodBias, blurH
    vec4 p1;      // blurV, converge, halation, halRadius
    vec4 p2;      // scanline, contrast, bright, saturation
    vec4 p3;      // maskType, maskStrength, maskScale, unused
    vec4 tint;    // phosphor tint rgb
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;

void main(){
    vec2  uSrcSize   = pc.p0.xy;
    float uLodBias   = pc.p0.z;
    float uBlurH     = pc.p0.w;
    float uBlurV     = pc.p1.x;
    float uHalation  = pc.p1.z;
    float uHalRadius = pc.p1.w;
    float uScanline  = pc.p2.x;
    float uContrast  = pc.p2.y;
    float uBright    = pc.p2.z;
    vec3  uTint      = pc.tint.rgb;

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
