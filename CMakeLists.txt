cmake_minimum_required(VERSION 3.21)   # clap-wrapper needs 3.21

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
# This must run before project(). An explicit -DCMAKE_MAKE_PROGRAM=... is always left alone, and
# other generators (Ninja) are not affected: CMAKE_MAKE_PROGRAM is their build tool, not make.
if(APPLE AND CMAKE_GENERATOR MATCHES "Makefiles"
   AND (NOT CMAKE_MAKE_PROGRAM OR CMAKE_MAKE_PROGRAM STREQUAL "/usr/bin/make"))
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

# C is needed for DPF's pugl / dgl sources.
project(StruckKit VERSION 0.3.0 LANGUAGES C CXX)

if(APPLE)
    # DPF's DistrhoUI_macOS.mm and clap-wrapper's AUv2 view are Objective-C++. Enabled here rather
    # than left to DPF, so it is on before add_subdirectory(external/clap-wrapper) below.
    enable_language(OBJCXX)
endif()

# Everything linked into the .clap shared module (the vendored engine, the plugin core, DPF's DGL)
# must be position-independent. clang on Linux defaults to PIE and hides this; gcc does not
# ("relocation R_X86_64_PC32 ... recompile with -fPIC"). As StruckSynth.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 20)          # PM framework uses std::numbers / std::span
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(STRUCKKIT_WERROR "Treat warnings as errors (framework + tests)" OFF)
option(STRUCKKIT_BUILD_VST3 "Build a VST3 wrapper around the same plugin via clap-wrapper" ON)
option(STRUCKKIT_BUILD_AUV2 "Build an AUv2 (.component) wrapper via clap-wrapper (macOS only)" ${APPLE})
option(STRUCKKIT_PRESET_DUMP "Dev-only: press 'P' in the UI to dump the patch as JSON to \$HOME" OFF)

# --- uw-core (git submodule): the UMP layer StruckKit shares with StageHand, Umpire and the
# other plugins. Only uw::ump (no CLAP host, JACK or ALSA); namespace uw (pm_midi.h). It builds
# position-independent with hidden symbols, so it links into the .clap safely.
set(UW_CORE_UMP_ONLY ON CACHE BOOL "" FORCE)
set(UW_CORE_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/uw-core)

# --- PM framework port -----------------------------------------------------
# Vendored PolygraphModular framework + our shim/MIDI layer (over uw::ump), as a static lib.
# Compiled with relaxed warnings (vendored code, its own style); our glue and tests stay strict.
# PIC so it can be linked into the shared .clap module.
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
target_link_libraries(pm_framework PUBLIC uw::ump)
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
# res2c embeds a whole folder and names symbols by basename without extension, so help.md (the
# authoring source for help.html) would collide with help.html. Stage everything but *.md.
file(GLOB STRUCKKIT_RESOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Resources/*")
list(FILTER STRUCKKIT_RESOURCE_FILES EXCLUDE REGEX "\\.md$")
set(STRUCKKIT_RESOURCES_STAGE "${CMAKE_BINARY_DIR}/resources-staged")
add_custom_command(
    OUTPUT "${STRUCKKIT_RESOURCES_HPP}" "${STRUCKKIT_RESOURCES_CPP}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${STRUCKKIT_RESOURCES_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STRUCKKIT_RESOURCES_STAGE}"
    COMMAND ${CMAKE_COMMAND} -E copy ${STRUCKKIT_RESOURCE_FILES} "${STRUCKKIT_RESOURCES_STAGE}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/dpf/utils/res2c.py"
            EmbeddedResources "${STRUCKKIT_RESOURCES_STAGE}" "${CMAKE_BINARY_DIR}/generated"
    DEPENDS ${STRUCKKIT_RESOURCE_FILES} "${CMAKE_SOURCE_DIR}/dpf/utils/res2c.py"
    COMMENT "Embedding Resources/* as compiled-in data")
add_custom_target(generate_resources DEPENDS "${STRUCKKIT_RESOURCES_HPP}" "${STRUCKKIT_RESOURCES_CPP}")

# --- DPF is used ONLY for the GUI ------------------------------------------
# UIExporter + NanoVG + pugl. The CLAP entry point is ours (src/plugin.cpp), because DPF's generated
# CLAP wrapper mis-sizes the editor — see that file's header. Same layout as StruckSynth.
add_subdirectory(dpf)
# dgl-opengl is normally created by dpf_add_plugin()'s UI_TYPE branch; call the same helper
# directly. SHARED_RESOURCES=TRUE embeds DGL's DejaVu font (fontFace(NANOVG_DEJAVU_SANS_TTF)).
dpf__add_dgl_opengl(TRUE FALSE FALSE)
# StruckKit's own namespace (UmpireDAW's docs/decisions.md, "DPF fork"): on macOS pugl names its
# Objective-C classes after it, so two DPF binaries with the same one in a host collide.
target_compile_definitions(dgl-opengl PUBLIC DGL_NAMESPACE=StruckKitDGL)
# DGL is vendored, so its warnings are not ours to fix: gcc's "<cstdbool> is deprecated" #warning
# from pugl.hpp under C++17+, and a stringop-overflow false positive in nanovg's stb_image.h.
target_compile_options(dgl-opengl PRIVATE -w)

# --- struckkit_core: the whole plugin (DSP + host glue + GUI shell) as one static archive ----
# Linked into the .clap output below. Hidden visibility for our own objects, so clap_entry — marked
# CLAP_EXPORT in plugin.cpp — is the only symbol this target exports (the vendored engine and DGL
# libraries linked in keep their own default visibility).
add_library(struckkit_core STATIC
    src/plugin.cpp
    src/UI/StruckKitUI.cpp
    "${STRUCKKIT_RESOURCES_CPP}"              # generated: the logo bytes (see above)
    "${DPF_ROOT_DIR}/distrho/src/DistrhoUI.cpp"
    "${DPF_ROOT_DIR}/distrho/src/DistrhoUtils.cpp")
set_target_properties(struckkit_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)
target_include_directories(struckkit_core PUBLIC
    src                               # plugin_gui.h (the DSP <-> GUI seam)
    plugin                            # DistrhoPluginInfo.h + StruckKitParams.h
    external/clap/include             # <clap/clap.h> (the raw entry point)
    "${CMAKE_BINARY_DIR}/generated"   # GeneratedPresets.h + EmbeddedResources.hpp
    "${DPF_ROOT_DIR}/distrho")        # DistrhoUI.hpp / DistrhoUIInternal.hpp
target_compile_definitions(struckkit_core PUBLIC
    DISTRHO_PLUGIN_TARGET_CLAP        # DistrhoUIPrivateData.hpp keys DISTRHO_UI_USES_SIZE_REQUEST off it
    DISTRHO_IS_STANDALONE=0
    STRUCKKIT_VERSION_STRING="${PROJECT_VERSION}")
# The UI's IFSControl pad draws the true DSP waveform via IFSCore (computeIFSWavetable), so the GUI
# shell needs pm_framework as well as the DSP side.
target_link_libraries(struckkit_core PUBLIC pm_framework dgl-opengl)
add_dependencies(struckkit_core generate_presets generate_resources)
# These two reach pugl.hpp through DPF's private UI headers; mute only its <cstdbool> #warning.
set_source_files_properties(src/UI/StruckKitUI.cpp "${DPF_ROOT_DIR}/distrho/src/DistrhoUI.cpp"
    PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU>:-Wno-cpp>")
dpf__add_plugin_specific_ui_sources(struckkit_core FALSE)   # macOS: DistrhoUI_macOS.mm
if(APPLE)
    # The live Retina scale of the host window (see the file's header comment).
    target_sources(struckkit_core PRIVATE src/UI/BackingScale_mac.mm)
endif()
# Dev-only JSON patch dumper (keystroke → file); compiled out of release builds.
if(STRUCKKIT_PRESET_DUMP)
    target_compile_definitions(struckkit_core PRIVATE STRUCKKIT_PRESET_DUMP=1)
endif()

# --- the .clap output ------------------------------------------------------
# A MODULE with no sources of its own would be rejected by CMake, and simply linking the archive is
# not enough: nothing references clap_entry, so the linker would drop the object that defines it.
# Hence the empty placeholder plus a whole-archive force-load. The VST3 and AUv2 wrappers below link
# the same way.
function(struckkit_force_load_whole_archive TARGET_NAME LIB_NAME)
    if(APPLE)
        target_link_options(${TARGET_NAME} PRIVATE "-Wl,-force_load,$<TARGET_FILE:${LIB_NAME}>")
    elseif(UNIX)
        target_link_options(${TARGET_NAME} PRIVATE
            "-Wl,--whole-archive" "$<TARGET_FILE:${LIB_NAME}>" "-Wl,--no-whole-archive")
    endif()
endfunction()

set(STRUCKKIT_EMPTY_SOURCE "${CMAKE_BINARY_DIR}/generated/struckkit_empty.c")
set(_struckkit_empty_content "typedef int struckkit_empty_translation_unit_marker;\n")
set(_struckkit_empty_existing "")
if(EXISTS "${STRUCKKIT_EMPTY_SOURCE}")
    file(READ "${STRUCKKIT_EMPTY_SOURCE}" _struckkit_empty_existing)
endif()
if(NOT _struckkit_empty_existing STREQUAL _struckkit_empty_content)
    file(WRITE "${STRUCKKIT_EMPTY_SOURCE}" "${_struckkit_empty_content}")
endif()

# The target keeps the name the DPF build gave it (StruckKit-clap), and the output stays at
# bin/StruckKit.clap, so the signing and symlink steps below — and anyone's habits — are unchanged.
add_library(StruckKit-clap MODULE "${STRUCKKIT_EMPTY_SOURCE}")
target_link_libraries(StruckKit-clap PRIVATE struckkit_core)
struckkit_force_load_whole_archive(StruckKit-clap struckkit_core)
if(APPLE)
    set_target_properties(StruckKit-clap PROPERTIES
        OUTPUT_NAME "StruckKit"
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.unlikelyware.struckkit"
        MACOSX_BUNDLE_BUNDLE_NAME "StruckKit"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
else()
    set_target_properties(StruckKit-clap PROPERTIES
        OUTPUT_NAME "StruckKit"
        PREFIX "" SUFFIX ".clap"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
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

# --- VST3 / AUv2 via the vendored clap-wrapper, wrapping the very same CLAP ---------------------
# Purely additive: neither block touches the .clap target above. Unlike the .clap, these are not
# installed on every build; run the install-vst3 / install-auv2 targets.
if(STRUCKKIT_BUILD_VST3 OR STRUCKKIT_BUILD_AUV2)
    if(NOT CLAP_SDK_ROOT)
        set(CLAP_SDK_ROOT "${CMAKE_SOURCE_DIR}/external/clap" CACHE PATH "" FORCE)
    endif()
    add_subdirectory(external/clap-wrapper)
endif()

# Ad-hoc sign a wrapper bundle after each build, for the same reason as the .clap above (a
# universal binary's linker signature fails `codesign --verify`).
function(struckkit_adhoc_sign_bundle TARGET_NAME)
    if(APPLE AND STRUCKKIT_CODESIGN_EXECUTABLE)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND "${STRUCKKIT_CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none
                "$<TARGET_BUNDLE_DIR:${TARGET_NAME}>"
            COMMENT "Ad-hoc signing ${TARGET_NAME}"
            VERBATIM)
    endif()
endfunction()

if(STRUCKKIT_BUILD_VST3)
    if(NOT VST3_SDK_ROOT)
        set(VST3_SDK_ROOT "${CMAKE_SOURCE_DIR}/external/vst3sdk" CACHE PATH "" FORCE)
    endif()

    add_library(StruckKit_vst3 MODULE "${STRUCKKIT_EMPTY_SOURCE}")
    if(UNIX AND NOT APPLE)
        # clap-wrapper's Linux branch tests a misspelled TCLP_RESOURCE_DIRECTORY, which is never
        # set, so it always warns that RESOURCE_DIRECTORY is unsupported. Defining it empty
        # silences that; we pass no RESOURCE_DIRECTORY.
        set(TCLP_RESOURCE_DIRECTORY "")
    endif()
    target_link_libraries(StruckKit_vst3 PRIVATE struckkit_core)
    struckkit_force_load_whole_archive(StruckKit_vst3 struckkit_core)
    target_add_vst3_wrapper(
        TARGET StruckKit_vst3
        OUTPUT_NAME "StruckKit"
        BUNDLE_IDENTIFIER "com.unlikelyware.struckkit.vst3"
        BUNDLE_VERSION "${PROJECT_VERSION}")
    if(TARGET base-sdk-vst3 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # The VST3 SDK still uses std::wstring_convert, deprecated since C++17.
        target_compile_options(base-sdk-vst3 PRIVATE -Wno-deprecated-declarations)
    endif()
    struckkit_adhoc_sign_bundle(StruckKit_vst3)

    if(APPLE)
        set(STRUCKKIT_VST3_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
        set(STRUCKKIT_VST3_ARTIFACT "$<TARGET_BUNDLE_DIR:StruckKit_vst3>")
    else()
        # $<TARGET_FILE> would be the .so inside the bundle; hosts want the bundle folder, which
        # clap-wrapper builds under <build>/Release (or Debug).
        set(STRUCKKIT_VST3_DIR "$ENV{HOME}/.vst3")
        set(STRUCKKIT_VST3_ARTIFACT
            "${CMAKE_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/StruckKit.vst3")
    endif()
    add_custom_target(install-vst3
        COMMAND ${CMAKE_COMMAND} -E make_directory "${STRUCKKIT_VST3_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${STRUCKKIT_VST3_DIR}/StruckKit.vst3"
        COMMAND ${CMAKE_COMMAND} -E create_symlink "${STRUCKKIT_VST3_ARTIFACT}" "${STRUCKKIT_VST3_DIR}/StruckKit.vst3"
        DEPENDS StruckKit_vst3
        VERBATIM
        COMMENT "Symlinking StruckKit.vst3 into ${STRUCKKIT_VST3_DIR}")
endif()

if(STRUCKKIT_BUILD_AUV2)
    # AUv2 is an Apple-only format, and clap-wrapper's target_add_auv2_wrapper simply does not exist
    # off Apple platforms. Say so plainly instead of failing with an unknown-command error.
    if(NOT APPLE)
        message(FATAL_ERROR
            "STRUCKKIT_BUILD_AUV2 is macOS-only (AUv2 is an Apple plugin format). "
            "Reconfigure with -DSTRUCKKIT_BUILD_AUV2=OFF.")
    endif()
    # clap-wrapper fetches Apple's AudioUnitSDK itself (CPM) unless pointed at a local copy with
    # -DAUDIOUNIT_SDK_ROOT=...
    if(NOT AUDIOUNIT_SDK_ROOT)
        set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON)
    endif()

    add_library(StruckKit_auv2 MODULE "${STRUCKKIT_EMPTY_SOURCE}")
    target_link_libraries(StruckKit_auv2 PRIVATE struckkit_core)
    struckkit_force_load_whole_archive(StruckKit_auv2 struckkit_core)
    # "StKt" (StruckKit) as the subtype, under Unlikelyware's own "nLKL" manufacturer code.
    target_add_auv2_wrapper(
        TARGET StruckKit_auv2
        OUTPUT_NAME "StruckKit"
        BUNDLE_IDENTIFIER "com.unlikelyware.struckkit.auv2"
        BUNDLE_VERSION "${PROJECT_VERSION}"
        MANUFACTURER_NAME "Unlikelyware"
        MANUFACTURER_CODE "nLKL"
        SUBTYPE_CODE "StKt"
        INSTRUMENT_TYPE "aumu")
    struckkit_adhoc_sign_bundle(StruckKit_auv2)

    # Copied, not symlinked: AU hosts don't reliably find a symlinked component.
    set(STRUCKKIT_AUV2_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
    add_custom_target(install-auv2
        COMMAND ${CMAKE_COMMAND} -E make_directory "${STRUCKKIT_AUV2_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${STRUCKKIT_AUV2_DIR}/StruckKit.component"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<TARGET_BUNDLE_DIR:StruckKit_auv2>" "${STRUCKKIT_AUV2_DIR}/StruckKit.component"
        DEPENDS StruckKit_auv2
        VERBATIM
        COMMENT "Copying StruckKit.component into ${STRUCKKIT_AUV2_DIR}")
endif()

# --- self-tests (the framework; headless, no DAW) --------------------------
add_executable(pm-selftest test/pm_selftest.cpp)
target_link_libraries(pm-selftest PRIVATE pm_framework)

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
