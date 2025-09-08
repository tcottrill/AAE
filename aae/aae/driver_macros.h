
#pragma once

// ==== Prolog / core fields ===================================================
#define AAE_DRIVER_BEGIN(sym, shortname, longname) \
    const AAEDriver sym = { (shortname), (longname),

#define AAE_DRIVER_ROM(romset) \
    (romset),

#define AAE_DRIVER_FUNCS(init, run, end) \
    (init), (run), (end),

#define AAE_DRIVER_INPUT(inputs) \
    (inputs),

#define AAE_DRIVER_SAMPLES(samples) \
    (samples),

#define AAE_DRIVER_SAMPLES_NONE() \
    nullptr,

#define AAE_DRIVER_ART(art) \
    (art),

#define AAE_DRIVER_ART_NONE() \
    0,

// ==== CPU arrays (always 4 entries) ==========================================
#define AAE_DRIVER_CPU( \
  t0,f0,d0,p0,i0,h0,  t1,f1,d1,p1,i1,h1,  t2,f2,d2,p2,i2,h2,  t3,f3,d3,p3,i3,h3) \
    /* cpu_type   */ { (t0), (t1), (t2), (t3) }, \
    /* cpu_freq   */ { (f0), (f1), (f2), (f3) }, \
    /* cpu_div    */ { (d0), (d1), (d2), (d3) }, \
    /* intpass_pf */ { (p0), (p1), (p2), (p3) }, \
    /* int_type   */ { (i0), (i1), (i2), (i3) }, \
    /* int_cb     */ { (h0), (h1), (h2), (h3) },

// Convenience wrappers
#define AAE_DRIVER_CPU1(t0,f0,d0,p0,i0,h0) \
    AAE_DRIVER_CPU( \
        t0,f0,d0,p0,i0,h0, \
        CPU_NONE,0,0,0,0,nullptr, \
        CPU_NONE,0,0,0,0,nullptr, \
        CPU_NONE,0,0,0,0,nullptr)

#define AAE_DRIVER_CPU2(t0,f0,d0,p0,i0,h0,  t1,f1,d1,p1,i1,h1) \
    AAE_DRIVER_CPU( \
        t0,f0,d0,p0,i0,h0, \
        t1,f1,d1,p1,i1,h1, \
        CPU_NONE,0,0,0,0,nullptr, \
        CPU_NONE,0,0,0,0,nullptr)

#define AAE_DRIVER_CPU3(t0,f0,d0,p0,i0,h0,  t1,f1,d1,p1,i1,h1,  t2,f2,d2,p2,i2,h2) \
    AAE_DRIVER_CPU( \
        t0,f0,d0,p0,i0,h0, \
        t1,f1,d1,p1,i1,h1, \
        t2,f2,d2,p2,i2,h2, \
        CPU_NONE,0,0,0,0,nullptr)

// ==== Video split: core + screen =============================================
#define AAE_DRIVER_VIDEO_CORE(fps_, vattr_, rotation_) \
    (fps_), (vattr_), (rotation_),

#define AAE_DRIVER_SCREEN(w_, h_, xmin_, xmax_, ymin_, ymax_) \
    (w_), (h_), { (xmin_), (xmax_), (ymin_), (ymax_) },

// ==== Raster decode / palette block (raster-only) ============================
#define AAE_DRIVER_RASTER(gfxdecode_ptr, total_cols, coltab_len, convert_fn) \
    (gfxdecode_ptr), (unsigned)(total_cols), (unsigned)(coltab_len), (convert_fn),

#define AAE_DRIVER_RASTER_NONE() \
    /* vector games */ nullptr, 0u, 0u, nullptr,

// ==== Hiscore hooks ==========================================================
#define AAE_DRIVER_HISCORE(load_fn, save_fn) \
    (load_fn), (save_fn),

#define AAE_DRIVER_HISCORE_NONE() \
    0, nullptr,

// ==== Vector RAM + NVRAM =====================================================
#define AAE_DRIVER_VECTORRAM(start_addr, size_bytes) \
    (start_addr), (unsigned)(size_bytes),

#define AAE_DRIVER_NVRAM(handler_fn) \
    (handler_fn),

#define AAE_DRIVER_NVRAM_NONE() \
    nullptr,

// One MachineCPU entry (field order must match your MachineCPU exactly)
#define AAE_CPU_ENTRY(type_, freq_, div_, ipf_, inttype_, cb_, r8_, w8_, pr_, pw_, r16_, w16_) \
    { (r8_), (w8_), (pr_), (pw_), (r16_), (w16_), (type_), (freq_), (div_), (ipf_), (inttype_), (cb_) }

#define AAE_CPU_NONE_ENTRY() \
    { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, CPU_NONE, 0, 0, 0, 0, nullptr }

// Emit the entire cpu array POSITIONALLY (no .cpu = … designator!)
// This must appear exactly where the cpu field belongs in AAEDriver's initializer.
#define AAE_DRIVER_CPUS(e0, e1, e2, e3) \
    { e0, e1, e2, e3 },

// ==== Epilog =================================================================
#define AAE_DRIVER_END() };


