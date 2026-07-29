# =============================================================================
# CMakeLists.txt - AAE (arcade emulator)
#
# Added ALONGSIDE the existing Visual Studio vcxproj files, replacing nothing.
# aae/aae.vcxproj remains the primary, verified Windows build. This file exists
# so a Linux (Steam Machine / Raspberry Pi) build can be stood up in Phase 3b
# without also having to invent build plumbing on a machine we cannot reach
# from here.
#
# Source lists below are copy-pasted out of aae/aae_core.vcxproj (87 files,
# 86 plus machine_state.cpp added in Task 6 - see the note above
# AAE_CORE_SOURCES) and aae/aae.vcxproj (48 files) - NOT globbed. A
# file(GLOB) would silently
# let new files join whichever target picked them up first and the core/exe
# split (the whole point of Phase 1-2's compiler-enforced boundary) would
# drift without anyone noticing. If you add a source file, add it to the
# vcxproj AND to the matching list below.
#
# Three targets:
#   aae_core     - STATIC library, the 87 emulation-core translation units.
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
# aae_core: 87 files from aae/aae_core.vcxproj's <ClCompile> list (86 as of
# Task 5, plus aae/machine_state.cpp added in Task 6 - see the comment in
# that file for why Machine's storage needed to move into the core).
# vcxproj paths are relative to aae/, so aae\cpu_code\cpu_6502.cpp becomes
# aae/aae/cpu_code/cpu_6502.cpp from the repo root.
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
if(NOT _aae_core_count EQUAL 87)
    message(FATAL_ERROR "aae_core source list has ${_aae_core_count} entries, expected 87 - it has drifted from aae/aae_core.vcxproj")
endif()

# -----------------------------------------------------------------------------
# aae (executable): 48 files from aae/aae.vcxproj's <ClCompile> list, split
# into a platform-neutral part and a per-OS backend part.
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
    aae/aae/aae_video/vector_draw.cpp
    aae/aae/aae_video/vector_fonts.cpp
    aae/aae/acommon.cpp
    aae/aae/config.cpp
    aae/aae/cpu_code/ccpu.cpp
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
    aae/system/3rdparty/glew.c
    aae/system/3rdparty/miniz.c
    aae/system/3rdparty/tinyxml2.cpp
    aae/system/audio/mixer.cpp
    aae/system/graphics/sys_gl.cpp
    aae/system/graphics/sys_texture.cpp
    aae/system/graphics/texrect.cpp
    aae/system/util/iniFile.cpp
    aae/system/util/path_helper.cpp
    aae/system/util/sys_fileio.cpp
    aae/system/util/sys_log.cpp
)

if(WIN32)
    set(AAE_PLATFORM_SOURCES
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
    # here so the structure is settled and 3b only fills in bodies. Steam
    # Machine (Mesa radeonsi) runs GL 4.6, so the existing #version 330 core
    # shaders work unchanged - no Vulkan port is needed for Linux.
    set(AAE_PLATFORM_SOURCES_WANTED
        aae/system/window/linux/linux_window.cpp
        aae/system/input/linux/evdev_input.cpp
        aae/system/audio/linux/alsa_backend.cpp
        aae/system/util/linux/posix_timer.cpp
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

    set(AAE_PLATFORM_DEFINES GLEW_STATIC)
    find_package(OpenGL REQUIRED)
    find_package(ALSA REQUIRED)
    find_package(X11 REQUIRED)
    set(AAE_PLATFORM_LIBS OpenGL::GL ALSA::ALSA X11::X11 pthread dl)
else()
    message(FATAL_ERROR "Unsupported platform - AAE targets Windows and Linux only")
endif()

list(LENGTH AAE_COMMON_SOURCES _aae_common_count)
list(LENGTH AAE_PLATFORM_SOURCES _aae_platform_count)
math(EXPR _aae_exe_count "${_aae_common_count} + ${_aae_platform_count}")
if(WIN32 AND NOT _aae_exe_count EQUAL 48)
    message(FATAL_ERROR "aae executable source count is ${_aae_exe_count} (common=${_aae_common_count} + platform=${_aae_platform_count}), expected 48 on Windows - it has drifted from aae/aae.vcxproj")
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
add_executable(aae ${AAE_COMMON_SOURCES} ${AAE_PLATFORM_SOURCES})

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

target_link_libraries(aae PRIVATE aae_core ${AAE_PLATFORM_LIBS})

if(WIN32)
    # Ship the XAudio2 redist DLL next to aae.exe, same as the vcxproj's
    # NuGet-driven ReferenceCopyLocalPaths does.
    add_custom_command(TARGET aae POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${AAE_XAUDIO2_DLL}" "$<TARGET_FILE_DIR:aae>/xaudio2_9redist.dll"
    )
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
