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
   kParamKeyMap,         // 0 = PMU (pitch class, every octave), 1 = General MIDI
   // --- Crash ---
   kParamCrashSize,      // C# crash diameter
   kParamCrashSize2,     // D# crash diameter
   kParamCrashThin,      // thickness: heavy <-> thin (thin blooms into a wash sooner)
   kParamCrashDecay,     // short <-> long ring
   kParamCrashStroke,    // shoulder on the edge (crash) <-> tip on the bow (ride ping)
   // --- Snare (stroke, rimshot) ---
   kParamSnareTune,      // batter-head tension (pitch)
   kParamSnareRing,      // head damping: muffled <-> open ring
   kParamSnarePosition,  // strike radius: centre <-> edge
   kParamSnareTension,   // snare-wire tension: loose long buzz <-> tight crisp burst
   kParamSnareWires,     // wire level
   // --- Toms (floor, mid, high) ---
   kParamTomTune,        // batter-head tension: all three move together
   kParamTomRing,        // head damping: muffled <-> open ring
   kParamTomPosition,    // strike radius: centre <-> edge
   kParamTomReso,        // resonant-head tuning relative to the batter
   // --- Hi-hats (open, half, closed) ---
   kParamHatSize,        // diameter
   kParamHatThin,        // thickness: heavy <-> thin
   kParamHatSizzle,      // half-open chatter
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
   {"Level",          "level",         "", 0.0f, 2.0f, 1.0f,   false, false},
   {"Key Map",        "keyMap",        "", 0.0f, 1.0f, 0.0f,   false, true },
   // Crash
   {"Crash 1 Size",   "crashSize",     "", 0.0f, 1.0f, 0.778f, false, false},
   {"Crash 2 Size",   "crashSize2",    "", 0.0f, 1.0f, 0.111f, false, false},
   {"Crash Thin",     "crashThin",     "", 0.0f, 1.0f, 0.50f,  false, false},
   {"Crash Decay",    "crashDecay",    "", 0.0f, 1.0f, 0.50f,  false, false},
   {"Crash Stroke",   "crashStroke",   "", 0.0f, 1.0f, 0.20f,  false, false},
   // Snare
   {"Snare Tune",     "snareTune",     "", 0.0f, 1.0f, 0.45f,  false, false},
   {"Snare Ring",     "snareRing",     "", 0.0f, 1.0f, 0.40f,  false, false},
   {"Snare Position", "snarePosition", "", 0.0f, 1.0f, 0.35f,  false, false},
   {"Snare Tension",  "snareTension",  "", 0.0f, 1.0f, 0.50f,  false, false},
   {"Snare Wires",    "snareWires",    "", 0.0f, 1.0f, 0.60f,  false, false},
   // Toms
   {"Tom Tune",       "tomTune",       "", 0.0f, 1.0f, 0.50f,  false, false},
   {"Tom Ring",       "tomRing",       "", 0.0f, 1.0f, 0.65f,  false, false},
   {"Tom Position",   "tomPosition",   "", 0.0f, 1.0f, 0.35f,  false, false},
   {"Tom Reso",       "tomReso",       "", 0.0f, 1.0f, 0.50f,  false, false},
   // Hi-hats
   {"Hat Size",       "hatSize",       "", 0.0f, 1.0f, 0.67f,  false, false},
   {"Hat Thin",       "hatThin",       "", 0.0f, 1.0f, 0.45f,  false, false},
   {"Hat Sizzle",     "hatSizzle",     "", 0.0f, 1.0f, 0.60f,  false, false},
};

// Fixed polyphony: enough for rolls under ringing crashes; the oldest voice is stolen beyond it.
static constexpr float kVoiceCount = 16.0f;

} // namespace struckkit

#endif // STRUCKKIT_PARAMS_H
