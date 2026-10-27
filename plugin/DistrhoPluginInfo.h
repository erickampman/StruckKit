// DistrhoPluginInfo.h — compile-time description of the StruckKit plugin for DPF.
//
// IMPORTANT: the DISTRHO_PLUGIN_* *identity* macros below are INERT. StruckKit does NOT use
// dpf_add_plugin: it implements the CLAP entry point itself in src/plugin.cpp (see that file's
// header for why — chiefly that DPF's generated wrapper mis-sizes the editor).
//
// The DISTRHO_UI_* macros DO bind: DPF is used here purely as the GUI layer (NanoVG widgets driven
// through UIExporter), exactly as StruckSynth and PlainOldSynth do it.

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND       "Unlikelyware"
#define DISTRHO_PLUGIN_NAME        "StruckKit"
#define DISTRHO_PLUGIN_URI         "https://unlikelyware.com/struckkit"
#define DISTRHO_PLUGIN_CLAP_ID     "com.unlikelyware.struckkit"
#define DISTRHO_PLUGIN_CLAP_FEATURES "instrument", "synthesizer", "stereo"
#define DISTRHO_PLUGIN_BRAND_ID    Unlk
#define DISTRHO_PLUGIN_UNIQUE_ID   Stkt

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_PLUGIN_IS_SYNTH        1   // note input + CLAP "instrument"; enables MIDI in
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_NUM_INPUTS      0
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0
#define DISTRHO_PLUGIN_WANT_STATE      0
// Gates UI::getPluginInstancePointer() (DistrhoUI.hpp), which the GUI shell uses to recover the
// clap_plugin_t* UIExporter was constructed with. Inert for the raw CLAP plugin itself.
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_UI_USE_NANOVG          1   // knobs/labels drawn with NanoVG vector primitives
#define DISTRHO_UI_USER_RESIZABLE      1
#define DISTRHO_UI_FILE_BROWSER        1   // Save / Load's file dialog (UI::openFileBrowser)
#define DISTRHO_UI_DEFAULT_WIDTH       1080
#define DISTRHO_UI_DEFAULT_HEIGHT      800   // full-width key map over 2 cols × 3 rows of panels

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
