//
//  ChokeGroupState.hpp
//
//  First client of ModuleSharedState: cross-voice choke groups for the drum kit.
//  One instance is shared by all voice-copies of a placed drum module.
//
//  A voice that strikes a note in a choke group bumps that group's generation
//  counter; any older voice still sounding in the same group sees a newer
//  generation during process() and applies its own fast release. Mono (K=1)
//  choke: the newest strike owns the group, everyone older releases -- e.g. a
//  closed hi-hat silencing an open one. Sounds that must ring (toms, crashes)
//  use NoChokeGroup and never touch this object.
//
//  The fast-release ENVELOPE is the drum module's job, not the state's: the
//  module authors the hat-close rather than inheriting a voice-steal duck.
//  (PMU port of PM's ChokeGroupState — same design, plain C++.)
//

#ifndef ChokeGroupState_hpp
#define ChokeGroupState_hpp

#include <array>
#include <cstdint>

#include "ModuleSharedState.hpp"

struct ChokeGroupState : ModuleSharedState {
    static constexpr uint32_t NoChokeGroup = 0;   // reserved: "no choke group"
    static constexpr uint32_t MaxChokeGroups = 16;

    void reset() override { generations.fill(0); }

    // Call at note-on for a struck sound in a choke group. Returns the new
    // generation to store; the caller owns the group until another voice
    // triggers it. Returns 0 (no-op) for NoChokeGroup / out-of-range.
    uint64_t triggerChoke(uint32_t group) {
        if (group == NoChokeGroup || group >= MaxChokeGroups) { return 0; }
        return ++generations[group];
    }

    uint64_t chokeGeneration(uint32_t group) const {
        if (group == NoChokeGroup || group >= MaxChokeGroups) { return 0; }
        return generations[group];
    }

    // Given the generation the caller stored from triggerChoke(), has a newer
    // strike in the same group superseded it?
    bool isChoked(uint32_t group, uint64_t ownedGeneration) const {
        return chokeGeneration(group) > ownedGeneration;
    }

    // Monotonic per-group counters.
    std::array<uint64_t, MaxChokeGroups> generations{};
};

// The proc a drum module lists in its ModuleInitSpec (sharedStateCreateProc).
inline ModuleSharedState* CreateChokeGroupState() { return new ChokeGroupState(); }

#endif /* ChokeGroupState_hpp */
