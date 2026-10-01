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
   /*Level*/        {"Master", "level"},
   /*CrashSwell*/   {"Crash",  "swell"},
   /*CrashTone*/    {"Crash",  "tone"},
   /*CrashDecay*/   {"Crash",  "decay"},
   /*CrashBright*/  {"Crash",  "bright"},
   /*SnareTune*/    {"Snare",  "tune"},
   /*SnareBody*/    {"Snare",  "body"},
   /*SnareBuzz*/    {"Snare",  "buzz"},
   /*SnareSnares*/  {"Snare",  "snares"},
   /*TomTune*/      {"Toms",   "tune"},
   /*TomDecay*/     {"Toms",   "decay"},
   /*TomBright*/    {"Toms",   "bright"},
   /*TomThump*/     {"Toms",   "thump"},
   /*HatTune*/      {"Hats",   "tune"},
   /*HatBalance*/   {"Hats",   "balance"},
   /*HatBright*/    {"Hats",   "bright"},
   /*BodyModel*/    {"Master", "bodyModel"},
};

} // namespace struckkit

#endif // STRUCKKIT_PRESETKEYS_HPP
