cmake_minimum_required(VERSION 3.17)

# C is needed for DPF's pugl / dgl sources.
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

add_executable(pm-modalbank-test test/pm_modalbank_test.cpp)
target_link_libraries(pm-modalbank-test PRIVATE pm_framework)

# A/B of DrumKit's Additive vs Modal bodies; pass an output dir to also write WAVs for listening.
add_executable(pm-drumkit-compare test/pm_drumkit_compare.cpp)
target_link_libraries(pm-drumkit-compare PRIVATE pm_framework)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    # pm-selftest only pulls in light headers → fully strict (optionally -Werror in CI).
    target_compile_options(pm-selftest PRIVATE -Wall -Wextra)
    if(STRUCKKIT_WERROR)
        target_compile_options(pm-selftest PRIVATE -Werror)
    endif()
    # tests that pull in vendored headers → relaxed (unused params in inline methods).
    foreach(t pm-module-test pm-sinosc-test pm-ifscore-test pm-adsr-test pm-modules-test pm-factory-test
              pm-voice-test pm-voicemanager-test pm-modroute-test pm-drumkit-test pm-drumkit-compare pm-modalbank-test)
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

add_test(NAME pm-drumkit-compare COMMAND pm-drumkit-compare)
set_tests_properties(pm-drumkit-compare PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-modalbank COMMAND pm-modalbank-test)
set_tests_properties(pm-modalbank PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")
