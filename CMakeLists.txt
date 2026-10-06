cmake_minimum_required(VERSION 3.17)

# --- macOS: target OS and architectures (must be set before project()) ------
# Without a deployment target the binary targets whatever macOS it was built on and may refuse to
# load on older systems. StruckKit supports macOS 15 and later. Both are cache vars, so -D
# overrides them.
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version")
option(STRUCKKIT_MACOS_UNIVERSAL "macOS: build one universal binary (arm64 + x86_64)" OFF)
if(STRUCKKIT_MACOS_UNIVERSAL)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()

# --- macOS: the make that recursive rules use must not live under a path with a space ---
# CMake's Unix Makefiles generator drives recursive make through $(MAKE), and GNU Make 3.81
# splits that on whitespace before it execs it. /usr/bin/make is only a shim: it re-execs the make
# inside the selected developer directory, and the real make then reports *that* path as $(MAKE).
# So an Xcode installed under a name containing a space — e.g. "/Applications/Xcode 26.3.app" —
# breaks every recursive rule CMake emits, starting with the compiler check inside project(),
# which dies with `make: /Applications/Xcode: No such file or directory` before any of our code is
# even looked at. Pick a make whose own path is space-free instead. (The other way out is to point
# the active developer directory at a space-free install: `sudo xcode-select -s /Applications/Xcode.app`.)
# This must run before project(). An explicit -DCMAKE_MAKE_PROGRAM=... is always left alone.
if(APPLE AND (NOT CMAKE_MAKE_PROGRAM OR CMAKE_MAKE_PROGRAM STREQUAL "/usr/bin/make"))
    execute_process(COMMAND /usr/bin/xcrun --find make
        OUTPUT_VARIABLE _struckkit_active_make OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE _struckkit_xcrun_result)
    if(_struckkit_xcrun_result EQUAL 0 AND _struckkit_active_make MATCHES " ")
        set(_struckkit_make_replaced FALSE)
        foreach(_struckkit_make_candidate
                "/Library/Developer/CommandLineTools/usr/bin/make"
                "/Applications/Xcode.app/Contents/Developer/usr/bin/make")
            if(EXISTS "${_struckkit_make_candidate}")
                set(CMAKE_MAKE_PROGRAM "${_struckkit_make_candidate}" CACHE FILEPATH
                    "Program used to build from makefiles" FORCE)
                message(STATUS "macOS: '${_struckkit_active_make}' contains a space, which GNU Make "
                               "cannot recurse through; using '${_struckkit_make_candidate}' instead")
                set(_struckkit_make_replaced TRUE)
                break()
            endif()
        endforeach()
        if(NOT _struckkit_make_replaced)
            message(WARNING
                "macOS: the active developer directory's make ('${_struckkit_active_make}') contains "
                "a space, which GNU Make cannot recurse through, and no space-free make was found. "
                "The build will fail; select a developer directory without spaces, e.g. "
                "`sudo xcode-select -s /Applications/Xcode.app`.")
        endif()
    endif()
endif()

# C is needed for DPF's pugl / dgl sources (Objective-C++ on macOS, which CMake enables itself).
project(StruckKit VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)          # PM framework uses std::numbers / std::span
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(STRUCKKIT_WERROR "Treat warnings as errors (framework + tests)" OFF)
option(STRUCKKIT_PRESET_DUMP "Dev-only: press 'P' in the UI to dump the patch as JSON to \$HOME" OFF)

# --- PM framework port -----------------------------------------------------
# Vendored PolygraphModular framework + our shim/MIDI layer, as a static lib. Compiled with
# relaxed warnings (vendored code, its own style); our glue and tests stay strict. PIC so it
# can be linked into the shared .clap module.
add_library(pm_framework STATIC
    src/Common/pm_midi.cpp
    src/Common/pm_param_data.cpp
    src/Module/SinOscillatorModule.cpp
    src/Module/IFSCore.cpp
    src/Module/IFSOscModule.cpp
    src/Module/LPFilterModule.cpp
    src/Module/LevelModule.cpp
    src/Module/IFSEnvModule.cpp
    src/Module/IFSLFOModule.cpp
    src/Module/CVMixerModule.cpp
    src/Module/ADSRHelper.cpp
    src/Module/ADSRModule.cpp
    src/Module/CombineModule.cpp
    src/Module/FinalModule.cpp
    src/Module/SlewModule.cpp
    src/Module/PitchConverterModule.cpp
    src/Module/ModalBankModule.cpp
    src/Module/ModalBank.cpp
    src/Module/StrikeModule.cpp
    src/Module/DrumKitModule.cpp
    src/Module/ResonatorBank.cpp
    src/Module/Cymbal.cpp
    src/Module/ModuleFactory.cpp
    src/Voice/ModuleMap.cpp
    src/Voice/pm_module_specs.cpp
    src/Voice/VoiceStateManager.cpp
    src/Voice/Voice.cpp
    src/Voice/VoiceManager.cpp)
set_target_properties(pm_framework PROPERTIES POSITION_INDEPENDENT_CODE ON)
# All framework folders on the include path so the vendored `#include "X.hpp"` resolve
# across Module/ Voice/ Common/ (mirrors PolygraphModular's layout).
target_include_directories(pm_framework PUBLIC src/Common src/Module src/Voice)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(pm_framework PRIVATE -Wall -Wno-unknown-pragmas -Wno-unused-parameter)
endif()

# --- presets: Presets/*.json compiled in as a C++ array, not loaded at runtime -------------
# Avoids any runtime JSON parser in the plugin and doesn't depend on CLAP's own preset-discovery
# extension — the UI presents its own dropdown. Adding/removing a preset file requires
# reconfiguring (CONFIGURE_DEPENDS re-globs on build, but a brand new file still needs `cmake -B
# build -S .` rerun once to pick up the new match).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(GLOB STRUCKKIT_PRESET_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Presets/*.json")
set(STRUCKKIT_GENERATED_PRESETS_H "${CMAKE_BINARY_DIR}/generated/GeneratedPresets.h")
add_custom_command(
    OUTPUT "${STRUCKKIT_GENERATED_PRESETS_H}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/gen_presets.py"
            ${STRUCKKIT_PRESET_FILES} -o "${STRUCKKIT_GENERATED_PRESETS_H}"
    DEPENDS ${STRUCKKIT_PRESET_FILES} "${CMAKE_SOURCE_DIR}/tools/gen_presets.py"
    COMMENT "Generating embedded presets from Presets/*.json")
add_custom_target(generate_presets DEPENDS "${STRUCKKIT_GENERATED_PRESETS_H}")

# --- embedded resources: Resources/* compiled in as raw bytes ----------------------------
# Uses DPF's own res2c.py (ships in the dpf/ submodule) to embed the logo PNG's raw file bytes;
# NanoVG's createImageFromMemory() decodes it at UI-construction time. No filesystem dependency
# on the resource file once built — same reasoning as the preset codegen above.
set(STRUCKKIT_RESOURCES_HPP "${CMAKE_BINARY_DIR}/generated/EmbeddedResources.hpp")
set(STRUCKKIT_RESOURCES_CPP "${CMAKE_BINARY_DIR}/generated/EmbeddedResources.cpp")
file(GLOB STRUCKKIT_RESOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Resources/*")
add_custom_command(
    OUTPUT "${STRUCKKIT_RESOURCES_HPP}" "${STRUCKKIT_RESOURCES_CPP}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/dpf/utils/res2c.py"
            EmbeddedResources "${CMAKE_SOURCE_DIR}/Resources" "${CMAKE_BINARY_DIR}/generated"
    DEPENDS ${STRUCKKIT_RESOURCE_FILES} "${CMAKE_SOURCE_DIR}/dpf/utils/res2c.py"
    COMMENT "Embedding Resources/* as compiled-in data")
add_custom_target(generate_resources DEPENDS "${STRUCKKIT_RESOURCES_HPP}" "${STRUCKKIT_RESOURCES_CPP}")

# --- the plugin: CLAP + OpenGL UI, built with DPF --------------------------
# DPF (vendored submodule) owns the plugin wrapper and generates the .clap from our DSP + UI
# subclasses; the DSP side drives pm_framework's VoiceManager. The four ADSR knobs route to
# the envelope module live.
add_subdirectory(dpf)
dpf_add_plugin(StruckKit
    TARGETS clap
    UI_TYPE opengl
    FILES_DSP
        plugin/StruckKitPlugin.cpp
    FILES_UI
        src/UI/StruckKitUI.cpp)
# So both DSP and UI sources find DistrhoPluginInfo.h + StruckKitParams.h.
target_include_directories(StruckKit PUBLIC plugin)
# The DSP side pulls in the vendored engine.
target_link_libraries(StruckKit-dsp PUBLIC pm_framework)
# The UI's IFSControl pad draws the true DSP waveform via IFSCore (computeIFSWavetable), so it
# links pm_framework too. Static-lib symbol extraction dedups it in the final .clap.
target_link_libraries(StruckKit-ui PUBLIC pm_framework)
# Generated preset array (see the codegen step above) — must exist before StruckKitUI.cpp compiles.
add_dependencies(StruckKit-ui generate_presets)
target_include_directories(StruckKit-ui PRIVATE "${CMAKE_BINARY_DIR}/generated")
# Embedded logo bytes — the generated .cpp defines the data the .hpp declares.
add_dependencies(StruckKit-ui generate_resources)
target_sources(StruckKit-ui PRIVATE "${STRUCKKIT_RESOURCES_CPP}")
# DPF may default to an older standard; the DSP glue includes C++20 framework headers.
set_target_properties(StruckKit-dsp StruckKit-ui PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
# Dev-only JSON patch dumper (keystroke → file); compiled out of release builds.
if(STRUCKKIT_PRESET_DUMP)
    target_compile_definitions(StruckKit-ui PRIVATE STRUCKKIT_PRESET_DUMP=1)
endif()

# --- macOS: ad-hoc sign the .clap bundle ----------------------------------
# ld ad-hoc signs the Mach-O it writes, but only usefully for a thin output: for the universal
# (arm64 + x86_64) binary that STRUCKKIT_MACOS_UNIVERSAL produces, `codesign --verify` rejects the
# linker's signature with "code object is not signed at all" — so the bundle fails the signature
# check even though it still loads. Signing the bundle explicitly yields a signature codesign
# accepts (and one that seals Info.plist, which the linker's does not). Still ad-hoc: fine for a
# local build and for loading here; handing the plugin to other people needs a Developer ID
# signature plus notarization, which is not set up. Must run before the symlink below so the link
# never points at an unsigned bundle.
if(APPLE)
    find_program(STRUCKKIT_CODESIGN_EXECUTABLE codesign)
    if(STRUCKKIT_CODESIGN_EXECUTABLE)
        add_custom_command(TARGET StruckKit-clap POST_BUILD
            COMMAND "${STRUCKKIT_CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none
                "${CMAKE_BINARY_DIR}/bin/StruckKit.clap"
            COMMENT "Ad-hoc signing StruckKit.clap"
            VERBATIM)
    else()
        message(WARNING "codesign not found: StruckKit.clap stays linker-signed, which a "
                        "universal build's signature check rejects")
    endif()
endif()

# --- install: symlink the built .clap into the user CLAP dir ---------------
# Runs automatically after StruckKit-clap is (re)built, so the plugin in the
# user CLAP dir is always a symlink pointing at the freshest build.
# create_symlink replaces any existing file or link, so this is idempotent.
if(APPLE)
    set(STRUCKKIT_CLAP_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
else()
    set(STRUCKKIT_CLAP_DIR "$ENV{HOME}/.clap")
endif()
add_custom_command(TARGET StruckKit-clap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STRUCKKIT_CLAP_DIR}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_BINARY_DIR}/bin/StruckKit.clap"
        "${STRUCKKIT_CLAP_DIR}/StruckKit.clap"
    COMMENT "Symlinking StruckKit.clap into ${STRUCKKIT_CLAP_DIR}"
    VERBATIM)

# --- self-tests (the framework; headless, no DAW) --------------------------
add_executable(pm-selftest test/pm_selftest.cpp)
target_link_libraries(pm-selftest PRIVATE pm_framework)
target_include_directories(pm-selftest PRIVATE external/clap/include) # clap_ump.h decoders

add_executable(pm-module-test test/pm_module_test.cpp)
target_link_libraries(pm-module-test PRIVATE pm_framework)

add_executable(pm-sinosc-test test/pm_sinosc_test.cpp)
target_link_libraries(pm-sinosc-test PRIVATE pm_framework)

add_executable(pm-ifscore-test test/pm_ifscore_test.cpp)
target_link_libraries(pm-ifscore-test PRIVATE pm_framework)

add_executable(pm-adsr-test test/pm_adsr_test.cpp)
target_link_libraries(pm-adsr-test PRIVATE pm_framework)

add_executable(pm-modules-test test/pm_modules_test.cpp)
target_link_libraries(pm-modules-test PRIVATE pm_framework)

add_executable(pm-factory-test test/pm_factory_test.cpp)
target_link_libraries(pm-factory-test PRIVATE pm_framework)

add_executable(pm-voice-test test/pm_voice_test.cpp)
target_link_libraries(pm-voice-test PRIVATE pm_framework)

add_executable(pm-modroute-test test/pm_modroute_test.cpp)
target_link_libraries(pm-modroute-test PRIVATE pm_framework)

add_executable(pm-voicemanager-test test/pm_voicemanager_test.cpp)
target_link_libraries(pm-voicemanager-test PRIVATE pm_framework)

add_executable(pm-drumkit-test test/pm_drumkit_test.cpp)
target_link_libraries(pm-drumkit-test PRIVATE pm_framework)

add_executable(pm-snare-test test/pm_snare_test.cpp)
target_link_libraries(pm-snare-test PRIVATE pm_framework)

add_executable(pm-modalbank-test test/pm_modalbank_test.cpp)
target_link_libraries(pm-modalbank-test PRIVATE pm_framework)

# Listening/measuring bench for the physically voiced drums (not a CTest: it reports, it doesn't
# assert). Pass an output dir to also write WAVs.
add_executable(pm-drumkit-render test/pm_drumkit_render.cpp)
target_link_libraries(pm-drumkit-render PRIVATE pm_framework)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    # pm-selftest only pulls in light headers → fully strict (optionally -Werror in CI).
    target_compile_options(pm-selftest PRIVATE -Wall -Wextra)
    if(STRUCKKIT_WERROR)
        target_compile_options(pm-selftest PRIVATE -Werror)
    endif()
    # tests that pull in vendored headers → relaxed (unused params in inline methods).
    foreach(t pm-module-test pm-sinosc-test pm-ifscore-test pm-adsr-test pm-modules-test pm-factory-test
              pm-voice-test pm-voicemanager-test pm-modroute-test pm-drumkit-test pm-modalbank-test pm-drumkit-render pm-snare-test)
        target_compile_options(${t} PRIVATE -Wall -Wno-unknown-pragmas -Wno-unused-parameter)
    endforeach()
endif()

enable_testing()
add_test(NAME pm COMMAND pm-selftest)
set_tests_properties(pm PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-module COMMAND pm-module-test)
set_tests_properties(pm-module PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-sinosc COMMAND pm-sinosc-test)
set_tests_properties(pm-sinosc PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-ifscore COMMAND pm-ifscore-test)
set_tests_properties(pm-ifscore PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-adsr COMMAND pm-adsr-test)
set_tests_properties(pm-adsr PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-modules COMMAND pm-modules-test)
set_tests_properties(pm-modules PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-factory COMMAND pm-factory-test)
set_tests_properties(pm-factory PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-voice COMMAND pm-voice-test)
set_tests_properties(pm-voice PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-modroute COMMAND pm-modroute-test)
set_tests_properties(pm-modroute PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-voicemanager COMMAND pm-voicemanager-test)
set_tests_properties(pm-voicemanager PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-drumkit COMMAND pm-drumkit-test)
set_tests_properties(pm-drumkit PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")


add_test(NAME pm-modalbank COMMAND pm-modalbank-test)
set_tests_properties(pm-modalbank PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-snare COMMAND pm-snare-test)
set_tests_properties(pm-snare PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")
