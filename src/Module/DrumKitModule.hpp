//
//  DrumKitModule.hpp
//
//  One MIDI-driven module that renders a whole drum kit. Each voice gets its own
//  copy, stores the struck note in per-voice state, and sounds the single drum
//  that the note's pitch class selects:
//
//     C=kick   D=kick2   E=snare   F=snare2   G/A/B=low/mid/high tom
//     C#=crash1   D#=crash2   F#=open hat   G#=half hat   A#=closed hat
//
//  Mapping is by pitch class (note % 12), so the kit repeats in every octave.
//
//  CHOKE: the three hi-hats (F#/G#/A#) share ONE choke group. Because voices are
//  isolated patch copies, the choke runs through the shared ChokeGroupState (see
//  ModuleSharedState.hpp) that the module declares via CreateChokeGroupState in
//  its ModuleInitSpec (sharedStateCreateProc): a new hi-hat bumps the group's
//  generation at note-on; an older hi-hat voice sees the newer generation in
//  process() and fast-releases. Crashes, toms, kick and snare use NoChokeGroup
//  and ring freely.
//
//  (PMU port of PM's DrumKitModule.)
//

#ifndef DrumKitModule_hpp
#define DrumKitModule_hpp

#include "Module.hpp"
#include "pm_midi.h"

extern const void        InitDrumKitModule(uint32_t count, Module* module);
extern const void        DeinitDrumKitModule(Module* module);
extern const void        ResetDrumKitModule(Module* module);
extern void              MIDIDrumKitModule(const pm::UmpMessage& message, Module* module);
extern const KPBuffPair* ProcessDrumKitModule(uint64_t processCallID, uint32_t count, Module* module);

#endif /* DrumKitModule_hpp */
