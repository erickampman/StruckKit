// PresetKeys.hpp — the plugin-param ↔ preset-JSON-field mapping, shared by the dev-only JSON
// dumper (PresetDump.hpp) and the always-on preset loader (GeneratedPresets.h resolution in
// StruckKitUI.cpp). Kept in its own always-compiled header (not gated by STRUCKKIT_PRESET_DUMP)
// since preset loading must work in every build, not just dev builds.

#ifndef STRUCKKIT_PRESETKEYS_HPP
#define STRUCKKIT_PRESETKEYS_HPP

#include "StruckKitParams.h"

namespace struckkit {

struct PresetKey { const char* module; const char* item; };

// One entry per param, index-aligned to ParamIndex. One logical module per drum family plus
// Master. Items are stable preset-file keys — they only need to agree between the dumper and the
// loader, not with any DSP serialization.
static constexpr PresetKey kPresetKeys[kParamCount] = {
   /*Level*/         {"Master", "level"},
   /*KeyMap*/        {"Master", "keyMap"},
   /*CrashSize*/     {"Crash",  "size1"},
   /*CrashSize2*/    {"Crash",  "size2"},
   /*CrashThin*/     {"Crash",  "thin"},
   /*CrashDecay*/    {"Crash",  "decay"},
   /*CrashStroke*/   {"Crash",  "stroke"},
   /*SnareTune*/     {"Snare",  "tune"},
   /*SnareRing*/     {"Snare",  "ring"},
   /*SnarePosition*/ {"Snare",  "position"},
   /*SnareTension*/  {"Snare",  "tension"},
   /*SnareWires*/    {"Snare",  "wires"},
   /*TomTune*/       {"Toms",   "tune"},
   /*TomRing*/       {"Toms",   "ring"},
   /*TomPosition*/   {"Toms",   "position"},
   /*TomReso*/       {"Toms",   "reso"},
   /*HatSize*/       {"Hats",   "size"},
   /*HatThin*/       {"Hats",   "thin"},
   /*HatSizzle*/     {"Hats",   "sizzle"},
   /*CongaTune*/     {"HandDrums", "congaTune"},
   /*BongoTune*/     {"HandDrums", "bongoTune"},
   /*TimbaleTune*/   {"HandDrums", "timbaleTune"},
   /*HandRing*/      {"HandDrums", "ring"},
   /*PercTune*/      {"Percussion", "tune"},
   /*PercDecay*/     {"Percussion", "decay"},
};

} // namespace struckkit

#endif // STRUCKKIT_PRESETKEYS_HPP
