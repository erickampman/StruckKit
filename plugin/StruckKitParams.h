// StruckKitParams.h — the plugin's parameter table, shared by the DSP and UI. One master Level plus
// each drum family's "finalized" controls, carried over from PMU's DrumKit module. Every param
// routes to the matching DrumKit module item (see the DSP's vmAddress); all but Level are 0..1
// knobs that DrumKitModule.cpp maps onto real ranges. Defaults mirror drumKitConstants in
// pm_param_data.cpp.

#ifndef STRUCKKIT_PARAMS_H
#define STRUCKKIT_PARAMS_H

namespace struckkit {

enum ParamIndex {
   // --- Master ---
   kParamLevel = 0,      // master output gain
   // --- Crash (C#, D#) ---
   kParamCrashSwell,     // bloom time: how late the highs open up
   kParamCrashTone,      // metal ring <-> noise wash
   kParamCrashDecay,     // ring length
   kParamCrashBright,    // how bright the bloom opens up
   // --- Snare (E, F) ---
   kParamSnareTune,      // head pitch
   kParamSnareBody,      // wires <-> body balance
   kParamSnareBuzz,      // body->wire coupling
   kParamSnareSnares,    // wire decay
   // --- Toms (G, A, B) ---
   kParamTomTune,        // pitch; low/mid/high transpose together
   kParamTomDecay,       // ring length
   kParamTomBright,      // upper-mode tilt (de-tone)
   kParamTomThump,       // attack skin/stick punch
   // --- Hi-hats (F#, G#, A#) ---
   kParamHatTune,        // overall hat pitch
   kParamHatBalance,     // metal ring <-> noise sizzle
   kParamHatBright,      // sizzle high-pass cutoff
   // --- Body model (temporary A/B while the modal port lands) ---
   kParamBodyModel,      // 0 = Additive (PMU's sines), 1 = Modal (ModalBank)
   kParamCount
};

struct ParamSpec {
   const char* name;
   const char* symbol;
   const char* unit;
   float       min;
   float       max;
   float       def;
   bool        logarithmic;
   bool        integer;
};

static constexpr ParamSpec kParams[kParamCount] = {
   // Master
   {"Level",        "level",       "", 0.0f, 2.0f, 1.0f,  false, false},
   // Crash
   {"Crash Swell",  "crashSwell",  "", 0.0f, 1.0f, 0.50f, false, false},
   {"Crash Tone",   "crashTone",   "", 0.0f, 1.0f, 0.60f, false, false},
   {"Crash Decay",  "crashDecay",  "", 0.0f, 1.0f, 0.66f, false, false},
   {"Crash Bright", "crashBright", "", 0.0f, 1.0f, 0.86f, false, false},
   // Snare
   {"Snare Tune",   "snareTune",   "", 0.0f, 1.0f, 0.47f, false, false},
   {"Snare Body",   "snareBody",   "", 0.0f, 1.0f, 0.35f, false, false},
   {"Snare Buzz",   "snareBuzz",   "", 0.0f, 1.0f, 0.40f, false, false},
   {"Snares",       "snareSnares", "", 0.0f, 1.0f, 0.55f, false, false},
   // Toms
   {"Tom Tune",     "tomTune",     "", 0.0f, 1.0f, 0.49f, false, false},
   {"Tom Decay",    "tomDecay",    "", 0.0f, 1.0f, 0.50f, false, false},
   {"Tom Bright",   "tomBright",   "", 0.0f, 1.0f, 0.50f, false, false},
   {"Tom Thump",    "tomThump",    "", 0.0f, 1.0f, 0.50f, false, false},
   // Hi-hats
   {"Hat Tune",     "hatTune",     "", 0.0f, 1.0f, 0.52f, false, false},
   {"Hat Balance",  "hatBalance",  "", 0.0f, 1.0f, 0.65f, false, false},
   {"Hat Bright",   "hatBright",   "", 0.0f, 1.0f, 0.45f, false, false},
   // Body model
   {"Body",         "bodyModel",   "", 0.0f, 1.0f, 1.0f,  false, true },
};

// Fixed polyphony: enough for rolls under ringing crashes; the oldest voice is stolen beyond it.
static constexpr float kVoiceCount = 16.0f;

} // namespace struckkit

#endif // STRUCKKIT_PARAMS_H
