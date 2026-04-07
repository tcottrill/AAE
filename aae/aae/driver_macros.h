
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

// ==== Video split: core + screen =============================================
#define AAE_DRIVER_VIDEO_CORE(fps_, vblank_dur_, vattr_, rotation_) \
    (fps_), (vblank_dur_), (vattr_), (rotation_),

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

// ==== MAME .lay file artwork (new system) ====================================
//
// These macros emit the layoutFile, artworkDir, and defaultView fields
// at the end of the AAEDriver initializer. They must appear after
// AAE_DRIVER_NVRAM / AAE_DRIVER_NVRAM_NONE and before AAE_DRIVER_END.
//
// AAE_DRIVER_LAYOUT(file, dir, view)
//   Use for drivers that have a MAME .lay artwork file.
//   file - relative path to .lay XML, e.g. "artwork\\invaders\\default.lay"
//   dir  - directory containing artwork PNGs, e.g. "artwork\\invaders"
//   view - default view name to activate, e.g. "Upright_Artwork"
//
// AAE_DRIVER_LAYOUT_NONE()
//   Use for drivers that have no layout artwork (sets all three to nullptr).
//
#define AAE_DRIVER_LAYOUT(file_, view_) \
    (file_),  (view_),

#define AAE_DRIVER_LAYOUT_NONE() \
    nullptr, nullptr,

// One MachineCPU entry (field order must match your MachineCPU exactly)
#define AAE_CPU_ENTRY(type_, freq_, div_, ipf_, inttype_, cb_, r8_, w8_, pr_, pw_, r16_, w16_) \
    { (r8_), (w8_), (pr_), (pw_), (r16_), (w16_), (type_), (freq_), (div_), (ipf_), (inttype_), (cb_), nullptr }

// Same as AAE_CPU_ENTRY but with a post-init callback that fires after the
// CPU core is created. Use for bank-switch setup, opfetch mode, etc.
#define AAE_CPU_ENTRY_EX(type_, freq_, div_, ipf_, inttype_, cb_, r8_, w8_, pr_, pw_, r16_, w16_, post_init_) \
    { (r8_), (w8_), (pr_), (pw_), (r16_), (w16_), (type_), (freq_), (div_), (ipf_), (inttype_), (cb_), (post_init_) }

#define AAE_CPU_NONE_ENTRY() \
    { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, CPU_NONE, 0, 0, 0, 0, nullptr, nullptr }

// Emit the entire cpu array POSITIONALLY (no .cpu = ... designator!)
// This must appear exactly where the cpu field belongs in AAEDriver's initializer.
#define AAE_DRIVER_CPUS(e0, e1, e2, e3) \
    { e0, e1, e2, e3 },

// ==== Epilog =================================================================
#define AAE_DRIVER_END() };

