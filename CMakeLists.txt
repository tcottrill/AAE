# =============================================================================
# CMakeLists.txt - AAE (arcade emulator)
#
# Added ALONGSIDE the existing Visual Studio vcxproj files, replacing nothing.
# aae/aae.vcxproj remains the primary, verified Windows build. This file exists
# so a Linux (Steam Machine / Raspberry Pi) build can be stood up in Phase 3b
# without also having to invent build plumbing on a machine we cannot reach
# from here.
#
# Source lists below are copy-pasted out of aae/aae_core.vcxproj (88 files,
# 86 plus machine_state.cpp added in Task 6, plus cpu_code/ccpu.cpp moved in
# from aae.vcxproj in a later task - see the notes above AAE_CORE_SOURCES) and
# aae/aae.vcxproj (47 files) - NOT globbed. A file(GLOB) would silently
# let new files join whichever target picked them up first and the core/exe
# split (the whole point of Phase 1-2's compiler-enforced boundary) would
# drift without anyone noticing. If you add a source file, add it to the
# vcxproj AND to the matching list below.
#
# Three targets:
#   aae_core     - STATIC library, the 88 emulation-core translation units.
#                  Its include directories deliberately omit
#                  aae/aae/aae_video, aae/aae/gui and aae/system/window - see
#                  the comment above AAE_CORE_INCLUDE_DIRS.
#   aae          - the executable, links aae_core, adds the OSD-layer include
#                  directories for itself only.
#   aae_headless - Task 6's headless/test harness: links aae_core with no
#                  aae_video/gui/window at all, supplies a NullWindow and a
#                  handful of OSD stubs (aae/headless/), runs N frames of a
#                  vector driver and counts vectors emitted through
#                  add_line() - the proof the core runs with no display,
#                  which is the Teensy target's precondition.
# =============================================================================

cmake_minimum_required(VERSION 3.20)
project(aae LANGUAGES CXX C)

# -----------------------------------------------------------------------------
# Warning level, applied to all three targets below (aae_core, aae,
# aae_headless - C sources like glew.c/miniz.c included). Left unset, cl.exe
# silently falls back to its own default of /W1, well below the Level3 (/W3)
# both aae/aae.vcxproj and aae/aae_core.vcxproj set explicitly - so the CMake
# build was emitting only 1 of the 6 warnings MSBuild produces (C4018 is
# level 3, C4101 is level 4; both were silently suppressed). This exists to
# keep CMake's warning output at parity with the vcxproj so a regression
# can't hide in the Linux build, since CMake is the only build system there.
#
# Plain add_compile_options(/W3) rather than CMAKE_CXX_FLAGS: neither
# CMAKE_CXX_FLAGS nor CMAKE_C_FLAGS carries a pre-existing /W flag here (CMake
# stopped injecting its own default /W3 once CMP0092 went NEW, which it is by
# default at our required CMake >= 3.20), so there is nothing for this /W3 to
# collide with. Appending /W3 onto an /W1 already present in those flags
# variables is what triggers MSVC's D9025 "overriding '/W1' with '/W3'" -
# verified that warning does NOT appear in the build output.
#
# -Wall (not -Werror) for GCC/Clang, the Linux path: the six warnings below
# are pre-existing and must not become hard build failures.
# -----------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall)
endif()

# -----------------------------------------------------------------------------
# aae_core: 88 files from aae/aae_core.vcxproj's <ClCompile> list (86 as of
# Task 5, plus aae/machine_state.cpp added in Task 6 - see the comment in
# that file for why Machine's storage needed to move into the core).
# vcxproj paths are relative to aae/, so aae\cpu_code\cpu_6502.cpp becomes
# aae/aae/cpu_code/cpu_6502.cpp from the repo root.
#
# cpu_code/ccpu.cpp (the portable Cinematronics CPU core used by tempest/
# quantum/qb3/cinematronics_driver.cpp) moved here from AAE_COMMON_SOURCES in
# a later task: it is a pure CPU core like cpu_6502.cpp/cpu_z80.cpp, not OSD
# code, and its only OSD-looking include - "opengl_renderer.h" - was dead
# weight (nothing in the file used it; the file-scope __glew_h__ regression
# guard added in the GL-header-scrub work only needed the file's *existing*
# includes checked, not a new one added on purpose - see that plan's Task 9).
# Removing the unused include let it compile clean under aae_core's
# restricted include path, confirmed with a standalone cl.exe invocation
# using only aae_core's -I list before moving it here.
# -----------------------------------------------------------------------------
set(AAE_CORE_SOURCES
    aae/aae/cpu_code/cpu_6502.cpp
    aae/aae/cpu_code/cpu_control.cpp
    aae/aae/cpu_code/cpu_i8039.cpp
    aae/aae/cpu_code/cpu_i8080.cpp
    aae/aae/cpu_code/cpu_i8085.cpp
    aae/aae/cpu_code/cpu_lr35902.cpp
    aae/aae/cpu_code/cpu_m6809.cpp
    aae/aae/cpu_code/cpu_m68000.cpp
    aae/aae/cpu_code/ccpu.cpp
    aae/aae/cpu_code/cpu_z80.cpp
    aae/aae/cpu_code/timer.cpp
    aae/aae/cpu_code/z80fmly.cpp
    aae/aae/drivers/asteroid.cpp
    aae/aae/drivers/aztarac.cpp
    aae/aae/drivers/bosco.cpp
    aae/aae/drivers/bwidow.cpp
    aae/aae/drivers/bzone.cpp
    aae/aae/drivers/cchasm.cpp
    aae/aae/drivers/centiped.cpp
    aae/aae/drivers/foodf.cpp
    aae/aae/drivers/gaplus.cpp
    aae/aae/drivers/pong.cpp
    aae/aae/drivers/breakout.cpp
    aae/aae/drivers/dkong.cpp
    aae/aae/drivers/gyruss.cpp
    aae/aae/drivers/cinematronics_driver.cpp
    aae/aae/drivers/circus.cpp
    aae/aae/drivers/digdug.cpp
    aae/aae/drivers/galaga.cpp
    aae/aae/drivers/galaxian_driver.cpp
    aae/aae/drivers/invaders.cpp
    aae/aae/drivers/jrpacman.cpp
    aae/aae/drivers/llander.cpp
    aae/aae/drivers/mappy.cpp
    aae/aae/drivers/milliped.cpp
    aae/aae/drivers/missile.cpp
    aae/aae/drivers/omegrace.cpp
    aae/aae/drivers/pacman.cpp
    aae/aae/drivers/phoenix.cpp
    aae/aae/drivers/quantum.cpp
    aae/aae/drivers/rallyx.cpp
    aae/aae/drivers/segag80.cpp
    aae/aae/drivers/superpac.cpp
    aae/aae/drivers/vertigo.cpp
    aae/aae/drivers/vicdual.cpp
    aae/aae/drivers/xevious.cpp
    aae/aae/drivers/yiear.cpp
    aae/aae/driver_registry.cpp
    aae/aae/fileio/aae_fileio.cpp
    aae/aae/game_list.cpp
    aae/aae/inptport.cpp
    aae/aae/machine/74148.cpp
    aae/aae/machine/earom.cpp
    aae/aae/machine/mathbox.cpp
    aae/aae/machine/pacplus_decode.cpp
    aae/aae/machine/pit8253.cpp
    aae/aae/machine/SegaG80crypt.cpp
    aae/aae/machine/slapstic.cpp
    aae/aae/machine/starwars_machine.cpp
    aae/aae/machine_state.cpp
    aae/aae/memory.cpp
    aae/aae/os_input.cpp
    aae/aae/sndhrdwr/aae_pokey.cpp
    aae/aae/sndhrdwr/astrocade_sound.cpp
    aae/aae/sndhrdwr/ay8910.cpp
    aae/aae/sndhrdwr/cinematronics_sound.cpp
    aae/aae/sndhrdwr/dac.cpp
    aae/aae/sndhrdwr/exidy440_sound.cpp
    aae/aae/sndhrdwr/okim6295_loader.cpp
    aae/aae/sndhrdwr/phoenix_audio.cpp
    aae/aae/sndhrdwr/pleiads_audio.cpp
    aae/aae/sndhrdwr/sn76477.cpp
    aae/aae/sndhrdwr/starwars_snd.cpp
    aae/aae/sndhrdwr/tms36xx.cpp
    aae/aae/sndhrdwr/tms5220.cpp
    aae/aae/vidhrdwr/aae_avg.cpp
    aae/aae/vidhrdwr/centiped_vid.cpp
    aae/aae/vidhrdwr/astrocde_video.cpp
    aae/aae/vidhrdwr/cinematronics_video.cpp
    aae/aae/vidhrdwr/galaxian_vid.cpp
    aae/aae/vidhrdwr/gaplus_video.cpp
    aae/aae/vidhrdwr/mappy_video.cpp
    aae/aae/vidhrdwr/old_mame_raster.cpp
    aae/aae/vidhrdwr/old_mame_vecsim_dvg.cpp
    aae/aae/vidhrdwr/rallyx_vid.cpp
    aae/aae/vidhrdwr/SegaG80vid.cpp
    aae/aae/vidhrdwr/vertigo_video.cpp
    aae/aae/vidhrdwr/xevious_video.cpp
)
list(LENGTH AAE_CORE_SOURCES _aae_core_count)
if(NOT _aae_core_count EQUAL 88)
    message(FATAL_ERROR "aae_core source list has ${_aae_core_count} entries, expected 88 - it has drifted from aae/aae_core.vcxproj")
endif()

# -----------------------------------------------------------------------------
# aae (executable): 47 files from aae/aae.vcxproj's <ClCompile> list, split
# into a platform-neutral part and a per-OS backend part. (cpu_code/ccpu.cpp
# used to be the 48th; it moved to AAE_CORE_SOURCES - see the note there.)
#
# The split point is "does this file #include a Windows-only header
# unconditionally (no #ifdef guard)". Verified file-by-file against the
# actual #include lines, not guessed from directory names:
#   - system/window/{winmain,windows_util,win10_win11_required_code}.cpp,
#     system/input/{rawinput,Joystick}.cpp and system/util/wintimer.cpp are
#     the Win32 implementations of the Phase 3a/2 sys_window / sys_input /
#     timer contracts - exactly the sketch in the task brief.
#   - system/audio/xaudio2_backend.cpp is the IAudioBackend Win32
#     implementation (also per the brief).
#   - system/audio/audio_3d.cpp was NOT in the brief's sketch but belongs in
#     the same bucket: it unconditionally includes <xaudio2.h>/<x3daudio.h>
#     (or the redist equivalents under WIN7BUILD) with no portability guard,
#     so it cannot even be parsed on Linux. See the Phase 3b note below.
#
# NOT moved into the platform split, despite also having unguarded Windows
# calls, because they don't yet have a Phase 3a-defined interface to swap
# against (unlike window/input/audio/timer) - flagging these for Phase 3b
# instead of inventing an unrequested abstraction here:
#   - system/graphics/sys_gl.cpp        (WGL context creation via wglew.h)
#   - system/graphics/sys_texture.cpp   (includes win32/win32_private.h)
#   - system/util/path_helper.cpp       (GetModuleFileName)
#   - system/util/sys_fileio.cpp        (GetModuleFileName)
#   - system/util/sys_log.cpp           (Windows console coloring)
#   - system/audio/mixer.cpp            (hardcodes `new XAudio2Backend`
#                                         instead of going through a
#                                         platform-selected factory)
# -----------------------------------------------------------------------------
set(AAE_COMMON_SOURCES
    aae/aae/aae_emulator.cpp
    aae/aae/aae_video/gl_fbo.cpp
    aae/aae/aae_video/gl_shader.cpp
    aae/aae/aae_video/gl_texturing.cpp
    aae/aae/aae_video/mame_layout.cpp
    aae/aae/aae_video/mame_vector.cpp
    aae/aae/aae_video/opengl_renderer.cpp
    aae/aae/aae_video/raster_emit.cpp
    aae/aae/aae_video/renderer_dispatch.cpp
    aae/aae/aae_video/vector_draw.cpp
    aae/aae/aae_video/vector_fonts.cpp
    aae/aae/aae_video_vk/crt_post_vk.cpp
    aae/aae/aae_video_vk/fast_poly_vk.cpp
    aae/aae/aae_video_vk/gpu_profiler_vk.cpp
    aae/aae/aae_video_vk/layout_vk.cpp
    aae/aae/aae_video_vk/render_target_vk.cpp
    aae/aae/aae_video_vk/screen_quad_vk.cpp
    aae/aae/aae_video_vk/shot_draw_vk.cpp
    aae/aae/aae_video_vk/snapshot_vk.cpp
    aae/aae/aae_video_vk/vector_draw_vk.cpp
    aae/aae/aae_video_vk/vector_post_vk.cpp
    aae/aae/aae_video_vk/vk_texture_loader.cpp
    aae/aae/aae_video_vk/vulkan_renderer.cpp
    aae/aae/acommon.cpp
    aae/aae/config.cpp
    aae/aae/drivers/astrocade.cpp
    aae/aae/drivers/mhavoc.cpp
    aae/aae/drivers/starwars.cpp
    aae/aae/drivers/tempest.cpp
    aae/aae/drivers/warlord.cpp
    aae/aae/fileio/mame_fileio.cpp
    aae/aae/fileio/texture_handler.cpp
    aae/aae/gui/driver_gui.cpp
    aae/aae/led_service_handler.cpp
    aae/aae/menu.cpp
    aae/aae/os_basic.cpp
    aae/aae/sndhrdwr/galsnd_stream.cpp
    aae/aae/sndhrdwr/namco.cpp
    aae/aae/sndhrdwr/segag80snd.cpp
    aae/aae/vidhrdwr/emu_vector_draw.cpp
    aae/aae/vidhrdwr/fast_poly.cpp
    aae/aae/vidhrdwr/osd_video.cpp
    aae/system/3rdparty/miniz.c
    aae/system/3rdparty/tinyxml2.cpp
    aae/system/audio/mixer.cpp
    aae/system/graphics/sys_gl.cpp
    aae/system/graphics/sys_texture.cpp
    aae/system/graphics/texrect.cpp
    aae/system/graphics/vk/sys_vk.cpp
    aae/system/util/iniFile.cpp
    aae/system/util/path_helper.cpp
    aae/system/util/sys_fileio.cpp
    aae/system/util/sys_log.cpp
)

# The bundled GLEW (system/3rdparty/glew.c) ships wglew.h but NOT glxew.h, so
# it cannot be compiled on Linux at all. Windows keeps the bundled copy - it is
# what aae.vcxproj builds and is known-good there - and Linux links the system
# libGLEW instead. This is why glew.c is per-platform rather than in
# AAE_COMMON_SOURCES.
if(WIN32)
    set(AAE_PLATFORM_SOURCES
        aae/system/3rdparty/glew.c
        aae/system/window/winmain.cpp
        aae/system/window/windows_util.cpp
        aae/system/window/win10_win11_required_code.cpp
        aae/system/input/rawinput.cpp
        aae/system/input/Joystick.cpp
        aae/system/audio/xaudio2_backend.cpp
        aae/system/audio/audio_3d.cpp
        aae/system/util/wintimer.cpp
    )
    # WIN7BUILD is intentionally NOT here - the vcxproj only defines it for
    # Release (see the $<CONFIG:Debug> guards below), and USING_XAUDIO2_REDIST
    # only makes sense once WIN7BUILD selects the redist include path, so it
    # travels with WIN7BUILD rather than living in this unconditional list.
    set(AAE_PLATFORM_DEFINES _WINDOWS GLEW_STATIC
        _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
    set(AAE_PLATFORM_LIBS opengl32)

    # aae/aae.vcxproj pulls XAudio2 in via the Microsoft.XAudio2.Redist NuGet
    # package (packages/Microsoft.XAudio2.Redist.1.2.13). Its .targets file
    # adds this include dir, the USING_XAUDIO2_REDIST define (added above),
    # and links release/lib/x64/{xaudio2_9redist,xapobaseredist}.lib; the
    # matching .dll must sit next to aae.exe at runtime. Everything else
    # (winmm, setupapi, glu32, opengl32, dinput8, dxguid, xinput,
    # xinput9_1_0, dwmapi, shcore) is pulled in via #pragma comment(lib, ...)
    # in the sources themselves and needs no CMake-side action.
    set(AAE_XAUDIO2_REDIST_DIR
        "${CMAKE_SOURCE_DIR}/packages/Microsoft.XAudio2.Redist.1.2.13/build/native")
    set(AAE_XAUDIO2_INCLUDE_DIR "${AAE_XAUDIO2_REDIST_DIR}/include")
    set(AAE_XAUDIO2_LIB_DIR "${AAE_XAUDIO2_REDIST_DIR}/release/lib/x64")
    set(AAE_XAUDIO2_DLL "${AAE_XAUDIO2_REDIST_DIR}/release/bin/x64/xaudio2_9redist.dll")
    list(APPEND AAE_PLATFORM_LIBS
        "${AAE_XAUDIO2_LIB_DIR}/xaudio2_9redist.lib"
        "${AAE_XAUDIO2_LIB_DIR}/xapobaseredist.lib")
elseif(UNIX AND NOT APPLE)
    # Linux backend - files do not exist yet; Phase 3b creates them. Listed
    # here so the structure is settled and 3b only fills in bodies.
    #
    # (Phase 3b note, now superseded: this said "no Vulkan port is needed for
    # Linux" because the Steam Machine's radeonsi runs GL 4.6. That holds for
    # the Steam Machine but NOT for the Pi 5, whose Mesa v3d tops out near
    # GL/GLES 3.1 - below the GL chain's #version 330 core shaders. That is
    # why Phase 4b exists; glx_present.cpp now creates a real VkSurfaceKHR.)
    set(AAE_PLATFORM_SOURCES_WANTED
        aae/system/window/linux/linux_window.cpp
        aae/system/input/linux/evdev_input.cpp
        aae/system/audio/linux/alsa_backend.cpp
        aae/system/util/linux/posix_timer.cpp
    )

    # Phase 3c additions that are not "backends" in the EXISTS-guarded sense:
    #   linux_main.cpp    - the entry point and GetSystemWindow/GetWindowSetup,
    #                       i.e. the Linux counterpart of winmain.cpp
    #   glx_present.cpp   - IPresentSurface over GLX
    #   evdev_device.cpp  - one /dev/input/event* node
    #   evdev_keymap.cpp  - KEY_* -> AAEKEY_*
    #   evdev_joystick.cpp- the joystick.h half, sibling of Joystick.cpp on
    #                       Windows (evdev_input.cpp is the sys_input.h half,
    #                       sibling of rawinput.cpp)
    list(APPEND AAE_PLATFORM_SOURCES_WANTED
        aae/system/window/linux/linux_main.cpp
        aae/system/window/linux/glx_present.cpp
        aae/system/input/linux/evdev_device.cpp
        aae/system/input/linux/evdev_keymap.cpp
        aae/system/input/linux/evdev_joystick.cpp
    )

    # Not in the EXISTS-guarded list above: these two exist as of Phase 3b and
    # are unconditional parts of the Linux build.
    #   voice_mixer.cpp   - software voice mixing, no OS calls (ALSA gives one
    #                       PCM stream, not a voice mixer). Reused by Teensy.
    #   audio_3d_null.cpp - positional audio is Phase 3d; this stub logs the
    #                       gap at startup instead of silently centre-panning.
    set(AAE_LINUX_EXTRA_SOURCES
        aae/system/audio/voice_mixer.cpp
        aae/system/audio/linux/audio_3d_null.cpp
    )
    set(AAE_PLATFORM_SOURCES "")
    set(_aae_missing_backends "")
    foreach(_src ${AAE_PLATFORM_SOURCES_WANTED})
        if(EXISTS "${CMAKE_SOURCE_DIR}/${_src}")
            list(APPEND AAE_PLATFORM_SOURCES ${_src})
        else()
            list(APPEND _aae_missing_backends ${_src})
        endif()
    endforeach()
    if(_aae_missing_backends)
        list(JOIN _aae_missing_backends "\n    " _aae_missing_backends_pretty)
        message(WARNING
            "Linux backend(s) not yet implemented (Phase 3b will add these):\n"
            "    ${_aae_missing_backends_pretty}\n"
            "Configuring anyway with the backends that do exist; the 'aae' "
            "target will fail to link until all four are provided.")
    endif()

    list(APPEND AAE_PLATFORM_SOURCES ${AAE_LINUX_EXTRA_SOURCES})

    # NOT GLEW_STATIC here: Windows links the bundled GLEW statically, but
    # Linux links the system libGLEW, which is a shared library. Defining
    # GLEW_STATIC against a shared GLEW gets the declarations' linkage wrong.
    set(AAE_PLATFORM_DEFINES)

    # Looked up optionally, NOT with REQUIRED. These three used to be REQUIRED
    # at project scope, which meant a box without libx11-dev could not
    # CONFIGURE at all - and therefore could not build aae_headless, the one
    # target that needs none of the three. Phase 3b's deliverables are exactly
    # the targets that do not use X11 or GL, so requiring them up front would
    # have blocked the phase on a dependency it never touches.
    #
    # Each target below asks for what it actually needs: aae_headless nothing,
    # aae_audiotest ALSA, and the full aae executable all three (Phase 3c).
    find_package(ALSA)
    find_package(OpenGL)
    find_package(X11)
    find_package(GLEW)   # system GLEW; the bundled one is Windows-only

    set(AAE_PLATFORM_LIBS pthread dl)
    if(OpenGL_FOUND)
        # GLU as well as GL: sys_texture.cpp and texture_handler.cpp still call
        # gluBuild2DMipmaps on the legacy (non-modernGL) texture path. Windows
        # gets it from #pragma comment(lib, "glu32.lib") in sys_gl.cpp, which
        # has no effect under g++, so it has to be named here.
        list(APPEND AAE_PLATFORM_LIBS OpenGL::GL)
        if(TARGET OpenGL::GLU)
            list(APPEND AAE_PLATFORM_LIBS OpenGL::GLU)
        endif()
    endif()
    if(ALSA_FOUND)
        list(APPEND AAE_PLATFORM_LIBS ALSA::ALSA)
    endif()
    # PulseAudio client libraries, optional. When present, the audio backend
    # opens the Pulse/PipeWire server NATIVELY with explicit channel positions
    # (the cubeb/Dolphin model) instead of going through the ALSA compat shim,
    # which is what made 6-channel output unverifiable (silent rears on the
    # Steam Machine). Without them the raw-ALSA path still covers everything -
    # Pi console builds and bare cabinets have no server anyway.
    # pa_strerror lives in libpulse proper, hence both libraries.
    find_library(PULSE_SIMPLE_LIBRARY pulse-simple)
    find_library(PULSE_LIBRARY pulse)
    if(PULSE_SIMPLE_LIBRARY AND PULSE_LIBRARY)
        list(APPEND AAE_PLATFORM_LIBS ${PULSE_SIMPLE_LIBRARY} ${PULSE_LIBRARY})
        add_compile_definitions(AAE_HAVE_PULSE)
        message(STATUS "PulseAudio native output enabled: ${PULSE_SIMPLE_LIBRARY}")
    else()
        message(STATUS "libpulse-simple not found - audio will use raw ALSA only")
    endif()
    if(X11_FOUND)
        list(APPEND AAE_PLATFORM_LIBS X11::X11)
    endif()
    if(GLEW_FOUND)
        list(APPEND AAE_PLATFORM_LIBS GLEW::GLEW)
    endif()
else()
    message(FATAL_ERROR "Unsupported platform - AAE targets Windows and Linux only")
endif()

list(LENGTH AAE_COMMON_SOURCES _aae_common_count)
list(LENGTH AAE_PLATFORM_SOURCES _aae_platform_count)
math(EXPR _aae_exe_count "${_aae_common_count} + ${_aae_platform_count}")
if(WIN32 AND NOT _aae_exe_count EQUAL 56)
    message(FATAL_ERROR "aae executable source count is ${_aae_exe_count} (common=${_aae_common_count} + platform=${_aae_platform_count}), expected 56 on Windows - it has drifted from aae/aae.vcxproj (ccpu.cpp moved to aae_core - see the comment above AAE_CORE_SOURCES; raster_emit.cpp added in Phase 4a Plan 3 Task 1, fast_poly_vk.cpp in Task 2; render_target_vk.cpp + screen_quad_vk.cpp added in Plan 4 Task 2; vector_draw_vk.cpp added in Plan 5 Task 1; vk_texture_loader.cpp added in Plan 6 Task 1)")
endif()

# =============================================================================
# aae_core - static library
# =============================================================================
add_library(aae_core STATIC ${AAE_CORE_SOURCES})

target_compile_features(aae_core PRIVATE cxx_std_17)
set_target_properties(aae_core PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
)

# Deliberately excludes aae/aae/aae_video, aae/aae/gui and aae/system/window -
# this omission is the compiler-enforced core/OSD boundary from Phase 1-2.
# Do not add them here; see the C1083 negative test in the Phase 3a plan.
#
# NOTE: the vcxproj's "./" entry is relative to aae/aae_core.vcxproj's own
# directory (aae/), NOT the repo root where this CMakeLists.txt lives - so it
# translates to "aae" below, not ".". Getting this wrong hides aae/resource.h
# and anything else that lives directly under aae/ (caught by the C1083
# negative test producing the wrong file, and by winmain.cpp failing to find
# resource.h during the real build).
target_include_directories(aae_core PRIVATE
    aae/aae
    aae/aae/sndhrdwr
    aae/aae/vidhrdwr
    aae/aae/machine
    aae/aae/fileio
    aae/aae/drivers
    aae/aae/cpu_code
    aae/system/audio
    aae
    aae/system/graphics
    aae/system/input
    aae/system/font
    aae/system/math
    aae/system/util
    aae/system/3rdparty
    aae/system/platform
    aae/system/project
)

target_compile_definitions(aae_core PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    ${AAE_PLATFORM_DEFINES}
)
if(WIN32)
    # Matches both vcxprojs' <CharacterSet>Unicode</CharacterSet>, which MSBuild
    # turns into /D UNICODE /D _UNICODE. Without it, TCHAR-mapped Win32 macros
    # (e.g. MessageBox -> MessageBoxA) mismatch against wide string literals
    # used in the source (see sys_gl.cpp's OpenGL-version warning dialog).
    target_compile_definitions(aae_core PRIVATE UNICODE _UNICODE)
endif()

if(WIN32)
    # Matches aae_core.vcxproj: WIN7BUILD only in Release, not Debug.
    # (aae_core.vcxproj never references the XAudio2Redist NuGet package, so
    # unlike the aae target below it gets no USING_XAUDIO2_REDIST at all.)
    target_compile_definitions(aae_core PRIVATE $<$<NOT:$<CONFIG:Debug>>:WIN7BUILD>)
endif()

# =============================================================================
# aae - executable
# =============================================================================
# On Linux the aae executable cannot LINK until Phase 3c supplies
# linux_window.cpp and evdev_input.cpp (see the EXISTS check above). Declaring
# it is still useful - the target exists and its sources are configured - but
# it is excluded from the default build so `cmake --build build-linux` does not
# fail on a target this phase knowingly does not deliver. Build it explicitly
# with `--target aae` once 3c lands, and delete this block then.
set(AAE_EXCLUDE_FROM_ALL "")
if(UNIX AND _aae_missing_backends)
    set(AAE_EXCLUDE_FROM_ALL EXCLUDE_FROM_ALL)
endif()

# Fail at CONFIGURE time, with the package name, rather than at link time.
#
# find_package(GLEW/OpenGL/X11) above is deliberately non-REQUIRED so that
# aae_headless can be built on a box with none of them. The cost is that a
# missing library does not stop the configure: the aae target simply omits it
# from target_link_libraries, compiles all 139 objects, and then dies with a
# couple of hundred "undefined reference to `__glewBindBuffer'" lines that say
# nothing about what to install.
#
# That is exactly how the first hermetic build of this project failed. Anything
# that ONLY aae needs is checked here instead, in the same shape as the ALSA
# check aae_audiotest already has.
if(UNIX AND NOT APPLE AND NOT AAE_EXCLUDE_FROM_ALL)
    if(NOT GLEW_FOUND)
        message(FATAL_ERROR
            "The 'aae' target needs GLEW, which was not found.\n"
            "  Debian/Ubuntu:  sudo apt install libglew-dev\n"
            "  Arch/SteamOS:   sudo pacman -S glew\n"
            "  Flatpak:        the manifest builds it as a module - pass "
            "-DCMAKE_PREFIX_PATH=/app so find_package can see it.\n"
            "The bundled 3rdparty GLEW is Windows-only: it has wglew.h but no "
            "glxew.h, so Linux must link the system library.")
    endif()
    if(NOT OpenGL_FOUND)
        message(FATAL_ERROR
            "The 'aae' target needs OpenGL and GLU.\n"
            "  Debian/Ubuntu:  sudo apt install libgl1-mesa-dev libglu1-mesa-dev\n"
            "  Arch/SteamOS:   sudo pacman -S mesa glu")
    endif()
    if(NOT X11_FOUND)
        message(FATAL_ERROR
            "The 'aae' target needs X11.\n"
            "  Debian/Ubuntu:  sudo apt install libx11-dev\n"
            "  Arch/SteamOS:   sudo pacman -S libx11")
    endif()
endif()

add_executable(aae ${AAE_EXCLUDE_FROM_ALL} ${AAE_COMMON_SOURCES} ${AAE_PLATFORM_SOURCES})

# Output the Linux executable into x64/Release, NOT build-linux.
#
# This is not cosmetic. Every path in the program resolves relative to the
# EXECUTABLE's own directory (path_helper.cpp: GetModuleFileNameW on Windows,
# /proc/self/exe on Linux) - so aae.ini, video.ini, roms/, artwork/, hi/ and
# cfg/ are all looked for beside the binary. Building into build-linux/ meant
# none of them were found and every game silently ran with built-in defaults:
# wrong scale, wrong position, and an upside-down GUI, because video.ini drives
# flipping as well as sizing.
#
# The Windows solution build already outputs to x64/Release for exactly this
# reason. Matching it keeps both platforms on one rule instead of needing a
# launcher workaround.
if(UNIX AND NOT APPLE)
    set_target_properties(aae PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/x64/Release")
endif()

if(WIN32)
    target_sources(aae PRIVATE aae/resource.rc aae/app.manifest)
    # Matches the vcxproj's <SubSystem>Windows</SubSystem>: the entry point
    # is wWinMain (system/window/winmain.cpp), not main/wmain, so the target
    # needs the WinMainCRTStartup stub and /SUBSYSTEM:WINDOWS, not the
    # console defaults CMake otherwise links an add_executable() with.
    set_target_properties(aae PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_compile_features(aae PRIVATE cxx_std_17)
set_target_properties(aae PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
)

# Adds back aae_video, gui and window - the OSD layer aae_core must not see.
# Same "./" -> "aae" translation note as aae_core above applies here too.
target_include_directories(aae PRIVATE
    aae/aae/aae_video
    aae/aae
    aae/aae/sndhrdwr
    aae/aae/vidhrdwr
    aae/aae/machine
    aae/aae/gui
    aae/aae/fileio
    aae/aae/drivers
    aae/aae/cpu_code
    aae/system/audio
    aae
    aae/system/graphics
    aae/system/graphics/vk
    aae/system/input
    aae/system/window
    aae/system/font
    aae/system/math
    aae/system/util
    aae/system/3rdparty
    aae/system/platform
    aae/system/project
)

if(WIN32)
    target_include_directories(aae PRIVATE ${AAE_XAUDIO2_INCLUDE_DIR})
endif()

target_compile_definitions(aae PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    ${AAE_PLATFORM_DEFINES}
)
if(WIN32)
    target_compile_definitions(aae PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:WIN7BUILD>
        $<$<NOT:$<CONFIG:Debug>>:USING_XAUDIO2_REDIST>
        UNICODE _UNICODE)
endif()

# -----------------------------------------------------------------------------
# aae_core is a STATIC library made almost entirely of self-registering game
# drivers (see AAE_REGISTER_DRIVER in aae/aae/driver_registry.h): each driver
# .cpp defines a file-scope static object whose constructor calls
# aae::RegisterDriver(). Nothing else in the program ever references that
# object by symbol name - drivers are found purely at runtime, through
# aae::FindDriverByName() / aae::AllDrivers().
#
# Both MSVC's linker and GNU/Clang's ld pull an object out of a static
# archive (.lib/.a) ONLY when something still-unresolved references a symbol
# in it. With nothing outstanding for the driver .obj files specifically,
# ordinary `target_link_libraries(aae PRIVATE aae_core)` lets the linker
# quietly drop every one of them: confirmed by building this exact target and
# running `aae.exe -listallgames`, which came back with only the ~49 games
# whose driver files happen to also be compiled directly into the aae
# executable target (mhavoc/starwars/tempest/warlord/astrocade) and NONE of
# the ~37 driver files that live only in aae_core.lib (asteroid, bzone,
# dkong, pacman, digdug, galaga, centiped, ...).
#
# IMPORTANT: this is NOT a CMake-only bug. A from-scratch rebuild of the real
# aae/aae.vcxproj (MSBuild.exe ... -t:Rebuild) hits the exact same problem -
# it also came back with only 49 games until aae.vcxproj's own Release|x64
# and Debug|x64 Link settings got an equivalent /WHOLEARCHIVE:aae_core.lib
# (see the <AdditionalOptions> comment there for the matching fix). The
# tracked x64/Release/aae.exe binary that ships in this repo predates the
# aae_core.vcxproj split (commit 8937dd4) - it was last rebuilt at commit
# 868ab01, when every driver file was still a loose .obj in one project - so
# it happens to show all 132 games and gave the false impression that
# MSBuild "escapes" this by accident. It does not, once actually rebuilt.
#
# Fix: force the WHOLE aae_core archive into the link, not just the members
# some symbol happens to reference - the standard remedy for self-
# registration living in a static library. CMake 3.24+ has first-class,
# portable support for this via the LINK_LIBRARY genex, which picks the
# right underlying flag per linker (MSVC's /WHOLEARCHIVE:, GNU/Apple ld's
# --whole-archive). Below 3.24 (e.g. an older distro cmake on the Pi/Steam
# Machine), fall back to spelling the flag out by hand per toolchain.
#
# DO NOT delete this thinking it's decorative or redundant with plain
# target_link_libraries - removing it silently drops every driver that lives
# only in aae_core.lib back out of the executable, and since CMake is the
# ONLY build system on Linux, that means the Linux build ships with zero
# playable games (only the handful of drivers compiled directly into the
# exe target survive). The same is true of the MSBuild aae.exe without its
# own /WHOLEARCHIVE fix.
# -----------------------------------------------------------------------------
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    target_link_libraries(aae PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,aae_core> ${AAE_PLATFORM_LIBS})
elseif(MSVC)
    target_link_libraries(aae PRIVATE aae_core ${AAE_PLATFORM_LIBS})
    target_link_options(aae PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:aae_core>")
else()
    # GNU ld / Clang: bracket just aae_core so its transitive dependencies
    # (OpenGL/ALSA/X11/pthread/dl) are not also force-included.
    target_link_libraries(aae PRIVATE
        -Wl,--whole-archive aae_core -Wl,--no-whole-archive
        ${AAE_PLATFORM_LIBS})
endif()

if(WIN32)
    # Ship the XAudio2 redist DLL next to aae.exe, same as the vcxproj's
    # NuGet-driven ReferenceCopyLocalPaths does.
    add_custom_command(TARGET aae POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AAE_XAUDIO2_DLL}" "$<TARGET_FILE_DIR:aae>/xaudio2_9redist.dll"
    )
endif()

# =============================================================================
# Vulkan shaders (Phase 4b).
#
# The VK chain loads "shaders/vk/<name>.spv" relative to the binary at pipeline
# creation time. On Windows those are produced by the <CustomBuild> entries in
# aae/aae.vcxproj - which CMake never sees - so before this rule the CMake
# build produced an aae that could not create a single Vulkan pipeline. Every
# renderer would log "failed to read shaders/vk/..." and fall over.
#
# Listed explicitly, NOT file(GLOB)'d: same rule as the source lists above -
# a glob silently absorbs new files and hides the fact that the vcxproj and
# this file have drifted apart. If you add a shader, add it in BOTH places
# (the vcxproj CustomBuild pair AND this list).
#
# glslc ships with the Vulkan SDK; tools/glslc.exe is the vendored Windows copy
# the vcxproj uses. If it is not found the shaders simply are not built and the
# GL chain is unaffected - so a Linux box without the SDK can still build and
# run `renderer=opengl`, which is what the Steam Machine does today.
# =============================================================================
set(AAE_VK_SHADERS
    crt_color_vk.frag
    crt_mono_vk.frag
    crt_post_vk.vert
    crt_scanline_vk.frag
    fast_poly_vk.frag
    fast_poly_vk.vert
    layout_quad_vk.frag
    screen_quad_rect_vk.frag
    screen_quad_rect_vk.vert
    vector_disc_vk.frag
    vector_disc_vk.vert
    vector_line_vk.frag
    vector_line_vk.vert
    vector_post_blur_vk.frag
    vector_post_multi_vk.frag
    vector_post_tex_vk.frag
    vector_post_vk.vert
    vector_shot_tex_vk.frag
    vector_shot_tex_vk.vert
    vector_shot_vk.frag
    vector_shot_vk.vert
)

find_program(AAE_GLSLC NAMES glslc
             HINTS "${CMAKE_CURRENT_SOURCE_DIR}/aae/tools"
             DOC "glslc (Vulkan SDK) - compiles shaders/vk/*.{vert,frag} to SPIR-V")

if(AAE_GLSLC)
    # Two-step on purpose: add_custom_command(OUTPUT ...) may NOT contain
    # target-dependent generator expressions like $<TARGET_FILE_DIR:aae> -
    # CMake hard-errors at generate time ("No target aae"). So the .spv are
    # compiled to fixed paths in the build tree, and a POST_BUILD step - where
    # the genex IS legal - copies them beside the binary. (This bit only on
    # the flatpak build: the WSL box has no glslc, so the branch never ran.)
    set(AAE_SPV_OUTPUTS "")
    foreach(shader ${AAE_VK_SHADERS})
        set(_src "${CMAKE_CURRENT_SOURCE_DIR}/aae/shaders/vk/${shader}")
        set(_dst "${CMAKE_BINARY_DIR}/shaders/vk/${shader}.spv")
        add_custom_command(
            OUTPUT  "${_dst}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders/vk"
            COMMAND "${AAE_GLSLC}" "${_src}" -o "${_dst}"
            DEPENDS "${_src}"
            COMMENT "glslc ${shader}"
            VERBATIM)
        list(APPEND AAE_SPV_OUTPUTS "${_dst}")
    endforeach()

    add_custom_target(aae_vk_shaders DEPENDS ${AAE_SPV_OUTPUTS})
    add_dependencies(aae aae_vk_shaders)
    add_custom_command(TARGET aae POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/shaders/vk" "$<TARGET_FILE_DIR:aae>/shaders/vk"
        COMMENT "Staging shaders/vk beside the aae binary")
    message(STATUS "Vulkan shaders: will compile with ${AAE_GLSLC}")
else()
    message(STATUS "Vulkan shaders: glslc NOT found - shaders/vk/*.spv will not be "
                   "built, so renderer=vulkan will fail at pipeline creation. "
                   "Install the Vulkan SDK (or set AAE_GLSLC) for a Vulkan-capable build.")
endif()

# =============================================================================
# aae_headless - Task 6. Proves aae_core runs with no window, no OSD, and no
# display anywhere: links aae_core against a NullWindow (Presentation() ==
# nullptr, inherited from ISystemWindow's base) and a handful of OSD stubs,
# then runs N frames of a vector driver and counts vectors emitted through
# add_line(). That is the Teensy 4.1 scenario minus the DACs.
#
# aae/system/3rdparty/miniz.c is added directly (not via aae_core) because
# aae_fileio.cpp's load_roms() (an aae_core file) calls the miniz zip-reader
# API directly rather than through the OSD file layer. It has no OSD
# dependencies of its own - it is a plain zip codec - so compiling the real
# file is simpler and more honest than stubbing 15 mz_zip_* entry points.
# =============================================================================
# aae/aae/drivers/asteroid.cpp and bzone.cpp are ALSO compiled directly as
# aae_headless sources, in addition to (not instead of) being part of
# aae_core.lib. This looks redundant but is the fix for a real discovery
# from this task: AAE_REGISTER_DRIVER's self-registration idiom (each driver
# file has a file-scope static object whose constructor calls
# aae::RegisterDriver()) has NO ordinary symbol that anything else in the
# program references - drivers are looked up by name at runtime through
# aae::FindDriverByName(), never by C++ symbol. A linker only pulls an
# archive (.lib) member when it resolves a currently-outstanding external
# symbol; with nothing outstanding for asteroid.obj/bzone.obj specifically,
# MSVC's classical selective-archive linking drops them from aae_core.lib
# entirely and aae::AllDrivers() comes back empty (verified: FindDriverByName
# failed for every game, dumpbin/archivemembers showed the .obj present in
# the .lib but "-listallgames" against a CMake-built aae.exe - not just
# aae_headless - produced an empty list too, so this is a latent bug in the
# Task 5 CMake build, not something specific to going headless).
#
# The real MSBuild aae.exe happens to avoid this because ALL of AAE_COMMON_
# SOURCES' 48 files - including this project's own driver-adjacent files -
# are passed to the linker as loose .obj, which are always linked regardless
# of reference (only .lib archive members are selectively pulled); aae_core's
# ~40 driver files were never among those 48, so the real exe's own drivers
# working at all was never actually exercising the archive-selection path for
# THIS particular pair of games either - it happens to work there for reasons
# that don't hold for a lean target like aae_headless linking almost nothing
# else directly. Rather than force /WHOLEARCHIVE (tried; pulls ~40 MORE
# unresolved externals from unrelated drivers' shared sound modules -
# namco.cpp, mappy/galaxian/segag80 sound - blowing well past the ~40-symbol
# stop condition) this compiles just the two driver files this test actually
# runs directly into the executable, exactly like AAE_COMMON_SOURCES already
# does for mhavoc.cpp/starwars.cpp/tempest.cpp/warlord.cpp. Their archive
# copies inside aae_core.lib simply go unused; no duplicate-symbol conflict
# results because nothing ever asks the linker to pull them from there.
#
# This is a genuine, previously-undiscovered gap in the self-registration
# design that the Teensy port must solve properly (an explicit compiled-in
# driver table, or per-platform force-linking) rather than route around
# per-target the way this test does. See the Task 6 report.
#
# Re-checked once the `aae` target above got real WHOLE_ARCHIVE force-linking
# (a later task): tried the same fix here (drop asteroid.cpp/bzone.cpp from
# this list, whole-archive aae_core instead) and it makes things WORSE, not
# better - 54 unresolved externals at link time (namco_sh_*, sega_sh_*,
# mappy/galaxian/pengo sound entry points, init_ccpu/ccpu_qb3_*, palette_*,
# osd_modify_pen, ...) because whole-archiving forces in every driver's .obj,
# including ones whose sound/video calls into aae's OSD-side files
# (namco.cpp, segag80snd.cpp, ccpu.cpp - none of which aae_headless links).
# The direct-compile-just-these-two-files approach below has no such problem
# because it never asks the linker to pull anything else out of aae_core.lib.
# Keeping the workaround.
add_executable(aae_headless
    aae/headless/headless_main.cpp
    aae/headless/null_backends.cpp
    aae/aae/drivers/asteroid.cpp
    aae/aae/drivers/bzone.cpp
    aae/system/3rdparty/miniz.c)
target_link_libraries(aae_headless PRIVATE aae_core)
# NOTE: no aae_video, no system/window (only its header, for NullWindow), no
# audio backend, no GL.
target_include_directories(aae_headless PRIVATE
    aae/aae
    aae/aae/sndhrdwr
    aae/aae/vidhrdwr
    aae/aae/machine
    aae/aae/fileio
    aae/aae/drivers
    aae/aae/cpu_code
    aae/system/audio
    aae
    aae/system/graphics
    aae/system/input
    aae/system/window
    aae/system/font
    aae/system/math
    aae/system/util
    aae/system/3rdparty
    aae/system/platform
    aae/system/project
)
target_compile_features(aae_headless PRIVATE cxx_std_17)
set_target_properties(aae_headless PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
)
target_compile_definitions(aae_headless PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    ${AAE_PLATFORM_DEFINES}
)
if(WIN32)
    target_compile_definitions(aae_headless PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:WIN7BUILD>
        UNICODE _UNICODE)
endif()

# =============================================================================
# aae_audiotest - Phase 3b's audible proof.
#
# Links the REAL mixer.cpp against the platform's IAudioBackend, unlike
# aae_headless which stubs the mixer out entirely (aae/headless/
# null_backends.cpp). Without this target a green Linux build would say
# nothing at all about whether audio works - and the voice path has not been
# exercised by anything but a smoke test since Phase 2 rewrote it.
#
# Deliberately does NOT link aae_core: the point is to isolate the audio
# stack, so this pulls in only the mixer, the platform backend, and the
# handful of utility files the mixer needs.
# =============================================================================
set(AAE_AUDIOTEST_SOURCES
    aae/audiotest/audiotest_main.cpp
    aae/system/audio/mixer.cpp
    aae/system/util/sys_log.cpp
    aae/system/util/path_helper.cpp
    aae/system/util/sys_fileio.cpp
    aae/system/util/iniFile.cpp
    aae/system/3rdparty/miniz.c
)
if(WIN32)
    list(APPEND AAE_AUDIOTEST_SOURCES
        aae/system/audio/xaudio2_backend.cpp
        aae/system/audio/audio_3d.cpp)
else()
    list(APPEND AAE_AUDIOTEST_SOURCES
        aae/system/audio/linux/alsa_backend.cpp
        aae/system/audio/voice_mixer.cpp
        aae/system/audio/linux/audio_3d_null.cpp)
endif()

add_executable(aae_audiotest ${AAE_AUDIOTEST_SOURCES})

target_compile_features(aae_audiotest PRIVATE cxx_std_17)
set_target_properties(aae_audiotest PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
)
target_include_directories(aae_audiotest PRIVATE
    aae/aae
    aae
    aae/system/audio
    aae/system/util
    aae/system/3rdparty
    aae/system/math
)
target_compile_definitions(aae_audiotest PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    ${AAE_PLATFORM_DEFINES}
)
if(WIN32)
    target_include_directories(aae_audiotest PRIVATE
        ${AAE_XAUDIO2_INCLUDE_DIR}
        aae/system/audio/linux)
    target_compile_definitions(aae_audiotest PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:WIN7BUILD>
        $<$<NOT:$<CONFIG:Debug>>:USING_XAUDIO2_REDIST>
        UNICODE _UNICODE)
    target_link_libraries(aae_audiotest PRIVATE ${AAE_PLATFORM_LIBS})
else()
    if(NOT ALSA_FOUND)
        message(FATAL_ERROR "aae_audiotest needs ALSA - install libasound2-dev")
    endif()
    target_include_directories(aae_audiotest PRIVATE aae/system/audio/linux)
    target_link_libraries(aae_audiotest PRIVATE ALSA::ALSA pthread)
endif()

# =============================================================================
# aae_uinput_test - synthetic input devices for the evdev backend (Linux only).
#
# The development box is WSL2, which has NO /dev/input directory at all - so
# without this there is nothing for the evdev backend to enumerate and none of
# Phase 3c Milestone B could be exercised before its first run on real
# hardware. /dev/uinput is present, so the devices are fabricated instead.
#
# It has no Windows counterpart by design; Win32 has the real Raw Input stack
# and a hardware pass. Deliberately standalone - it links nothing from AAE,
# because a test harness sharing code with the thing under test can agree with
# it and still both be wrong.
#
# Excluded from ALL: it needs root to run, so building it by default would
# invite running the whole test suite under sudo. Build it explicitly:
#   cmake --build build-linux --target aae_uinput_test
# =============================================================================
if(UNIX AND NOT APPLE)
    add_executable(aae_uinput_test EXCLUDE_FROM_ALL tools/linux/uinput_devices.cpp)
    target_compile_features(aae_uinput_test PRIVATE cxx_std_17)
endif()

# =============================================================================
# aae_inputtest - Phase 3c's observable proof, the twin of aae_audiotest.
#
# Links the REAL evdev backend against the real sys_input.h / joystick.h
# contract and prints every state change. The emulator does not log key
# presses - it responds to them - so without this a working backend and a dead
# one produce identical output on a machine with no display and no hands.
#
# Deliberately does NOT link aae_core: the point is to isolate the input
# stack, exactly as aae_audiotest isolates the audio one.
# =============================================================================
if(UNIX AND NOT APPLE)
    add_executable(aae_inputtest
        aae/inputtest/inputtest_main.cpp
        aae/system/input/linux/evdev_input.cpp
        aae/system/input/linux/evdev_device.cpp
        aae/system/input/linux/evdev_keymap.cpp
        aae/system/input/linux/evdev_joystick.cpp
        aae/system/util/sys_log.cpp
    )
    target_compile_features(aae_inputtest PRIVATE cxx_std_17)
    target_include_directories(aae_inputtest PRIVATE
        aae
        aae/system/input
        aae/system/window
        aae/system/util
        aae/system/math
    )
    target_link_libraries(aae_inputtest PRIVATE pthread)
endif()
