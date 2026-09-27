// vector_post_multi_vk.frag - port of the GL fragMulti composite
// (shader_definitions.h texfragText): beam frame + glow * glowamt +
// trail feedback * 0.25. The GL "brighten" uniform is dead (bval is a
// constant 1.0 there) and is not ported.
#version 450

layout(set = 0, binding = 0) uniform sampler2D beamMap;   // GL mytex2 (img1b)
layout(set = 0, binding = 1) uniform sampler2D glowMap;   // GL mytex3 (img3b)
layout(set = 0, binding = 2) uniform sampler2D trailMap;  // GL mytex4 (img1c)

layout(push_constant) uniform Push {
    vec4 rect;
    vec4 tsize;
    vec4 uvrect;
    vec4 tint;
    vec4 params;  // x = glowamt, y = usefb (0/1), z = useglow (0/1), w = fuzz
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

// FUZZ tuning - keep in sync with shader_definitions.h texfragText.
const int   kFuzzTaps     = 32;
const float kFuzzRadius   = 0.055;  // halation spread, fraction of the screen
const float kFuzzThreshLo = 0.05;   // blurred-luma where blowout starts
const float kFuzzThreshHi = 0.30;   // blurred-luma of full blowout
const float kFuzzBloom    = 3.0;    // halo energy poured back around hot areas
const float kFuzzWhite    = 1.3;    // how hard driven pixels clip toward white
const float kFuzzVeilLod  = 6.5;    // veil breadth: mip of the one-tap veil sample
const float kFuzzVeil     = 0.30;   // veiling-glare strength around the hot mass

// Radial FENCE, not the shape of the effect. The wash's shape comes from the
// drawn content (see the composite body); this only guarantees the HUD rows
// and the edge turrets can never engage, however bright they are. Real-cab
// footage shows the explosion reaching ~70% of the half-screen, so the fence
// starts well outside that. Aspect stretches the UV-space distance
// horizontally so the fence is a circle on the 4:3 monitor.
const float kFuzzRadialInner = 0.75;
const float kFuzzRadialOuter = 1.05;
const float kFuzzAspect      = 1.3333;

// Where the game converges the Death Star explosion, in beam-texture UV
// (v = 1 is the TOP of the game image in both renderers). NOT the texture
// midpoint: the game draws the explosion below center - the HUD row takes the
// top of the screen - so a (0.5, 0.5) mask washes the top of the rings and
// misses the bottom.
const vec2 kFuzzCenter = vec2(0.50, 0.42);

float fuzz_radial(vec2 uv)
{
    vec2 rv = uv - kFuzzCenter;
    rv.x *= kFuzzAspect;
    return 1.0 - smoothstep(kFuzzRadialInner, kFuzzRadialOuter, length(rv) * 2.0);
}

// Circle-of-confusion defocus: average kFuzzTaps samples spread over a disc of
// the given radius, placed on a golden-angle (Vogel) spiral so they cover the
// disc evenly with no lattice to alias against. Each tap reads the mip level
// whose texel footprint matches the tap SPACING, so neighbouring taps overlap
// and the result is smooth and round.
//
// Sampling one high mip instead - textureLod at level ~6 - is what produces
// square blocks: that is a 16x16 image stretched over the screen, so you see
// its texels. Overlapping low-mip taps are what make it look like a lens.
vec3 fuzz_defocus(sampler2D tex, vec2 uv, float amount)
{
    float radius = kFuzzRadius * amount;
    float texw   = float(textureSize(tex, 0).x);
    float lod    = max(0.0, log2(max(radius * texw, 1.0) / sqrt(float(kFuzzTaps))));

    // Rotate the whole spiral by a per-pixel noise angle (interleaved gradient
    // noise). Without this the taps are COHERENT: every bright edge appears
    // kFuzzTaps times at the same offsets and the rim of the blob turns into a
    // countable polygon of ghost copies. Rotating per pixel decorrelates the
    // copies into fine grain, which reads as defocus.
    float ang = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float cs = cos(ang), sn = sin(ang);
    mat2 rot = mat2(cs, sn, -sn, cs);

    vec3 acc = vec3(0.0);
    for (int i = 0; i < kFuzzTaps; ++i)
    {
        float fi = float(i) + 0.5;
        float r  = sqrt(fi / float(kFuzzTaps));   // even coverage by AREA
        float th = fi * 2.39996323;               // golden angle
        vec2  o  = rot * vec2(cos(th), sin(th)) * (r * radius);
        acc += textureLod(tex, clamp(uv + o, 0.0, 1.0), lod).rgb;
    }
    return acc / float(kFuzzTaps);
}

void main()
{
    // GL RGB8 semantics: all three GL source textures read a=1.0.
    vec3 result = texture(beamMap, vUV).rgb;
    result += texture(glowMap,  vUV).rgb * pc.params.x * pc.params.z;
    result += texture(trailMap, vUV).rgb * 0.25 * pc.params.y;

    // Star Wars Death Star explosion blowout. 0 for every other game, and this
    // branch leaves their output untouched.
    // Content-driven, matched against real-cab footage: this is NOT a region
    // blur. Wherever the DRAWN image is locally bright and dense (the packed
    // explosion circles) the phosphor saturates - blooming outward and
    // clipping to white - while sparse strokes barely move. The wash's shape
    // IS the shape of what the game drew: the big donut keeps its dark hole,
    // an expansion ring stays a colored ring, turrets and HUD stay sharp.
    if (pc.params.w > 0.0)
    {
        // Blurred local energy: one estimate serves as both the halo to pour
        // back and the measure of how overloaded this spot is.
        vec3  soft  = fuzz_defocus(beamMap, vUV, 1.0);
        float e     = dot(soft, vec3(0.299, 0.587, 0.114));
        float drive = pc.params.w * fuzz_radial(vUV)
                    * smoothstep(kFuzzThreshLo, kFuzzThreshHi, e);

        // Halation grows and rounds the hot mass...
        result += soft * (kFuzzBloom * drive);
        // ...then driven pixels clip toward white in proportion to their own
        // brightness (dim fringes keep their color - the hot ring look).
        float w = clamp(dot(result, vec3(0.299, 0.587, 0.114)) * drive * kFuzzWhite, 0.0, 1.0);
        result = mix(result, vec3(1.0), w);

        // Veiling glare: light from the blown-out mass scattering in the glass
        // and the eye - a very wide, faint, source-colored veil. SDR white
        // cannot exceed 1.0, so the surroundings signal the brightness
        // instead. One trilinear tap of a high mip is smooth at this contrast;
        // the log2 term keeps GL (1024) and VK (1024*ssaa) the same breadth.
        // Deliberately NOT gated by the threshold or the fence: glare washes
        // over everything, and it falls off with distance on its own.
        float vlod = kFuzzVeilLod + log2(float(textureSize(beamMap, 0).x) / 1024.0);
        result += textureLod(beamMap, vUV, vlod).rgb * (kFuzzVeil * pc.params.w);
    }

    // GL parity clamp (GL stores this sum into the RGBA8 fbo4, clamping at
    // 1.0, then blits the bytes unchanged). The swapchain is UNORM (sys_vk
    // CreateSwapchain prefers it precisely so raw byte output displays like
    // GL's non-sRGB window); a briefly-shipped srgb_to_linear cancellation
    // here targeted the old forced-SRGB swapchain and was removed with it.
    FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
