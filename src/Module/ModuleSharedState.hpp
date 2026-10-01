//
//  ModuleSharedState.hpp
//
//  Base for any state SHARED across the per-voice copies of ONE placed module.
//  (PMU port of PM's ModuleSharedState — same design, plain C++ types instead of
//  Obj-C/AudioToolbox: the registry key is the module's base address as uint64_t.)
//
//  Each Voice owns an isolated copy of every module, so the copies of a given
//  module living in different voices cannot see one another. A module type that
//  needs cross-voice state declares a ModuleSharedStateCreateProc in its
//  ModuleInitSpec. The registry then creates exactly one shared object per
//  PLACED module -- keyed by MODULE_BASE_ADDRESS (kind+instance), which is
//  identical across voices -- and injects that same pointer into the module's
//  copy in every voice. Two separate placements of the same module kind
//  (e.g. drumKit1 and drumKit2) therefore get independent shared objects.
//
//  THREADING: the object is created at init (main thread). All *use* is on the
//  audio render thread, where MIDI events and DSP are interleaved on one thread,
//  so concrete subclasses hold plain data -- no atomics, no locks. reset() is
//  called from VoiceManager::reset.
//
//  This is not a global: the registry is a VoiceManager member, handed to
//  Voice::initializeModules by pointer exactly like PitchConverter.
//

#ifndef ModuleSharedState_hpp
#define ModuleSharedState_hpp

#include <cstdint>
#include <memory>
#include <unordered_map>

struct ModuleSharedState {
    virtual ~ModuleSharedState() = default;

    // Zero transient state WITHOUT destroying the object: modules keep pointers
    // to it, so those must stay valid. Called on reset/panic.
    virtual void reset() {}

    // Called by VoiceManager::process() ONCE per engine chunk, before any voice is processed —
    // the single place a cross-voice module can advance shared per-block state exactly once (not
    // once per active voice). `activeVoices` is how many voices are currently sounding this chunk
    // (Active/Deactivating/Stealing), `frameCount` the chunk length. Default no-op (e.g. the choke
    // group needs no per-block work). The shared LFO uses it to advance its one shared phase and,
    // in voices-active-only mode, to reset the phase when the instrument falls silent.
    virtual void beginBlock(uint32_t /*activeVoices*/, uint32_t /*frameCount*/) {}
};

// The proc a module lists in its ModuleInitSpec to define its shared object.
// (Also typedef'd in Module.hpp so ModuleInitSpec can name it without this header.)
typedef ModuleSharedState* (*ModuleSharedStateCreateProc)();

// Owns one ModuleSharedState per placed module. Lives on VoiceManager.
class ModuleSharedStateRegistry {
public:
    // First caller for a given base address creates the object; the same
    // module's copies in other voices get back the identical pointer.
    ModuleSharedState* getOrCreate(uint64_t baseAddress,
                                   ModuleSharedStateCreateProc createProc) {
        auto it = states.find(baseAddress);
        if (it != states.end()) {
            return it->second.get();
        }
        std::unique_ptr<ModuleSharedState> obj(createProc());
        ModuleSharedState* raw = obj.get();
        states.emplace(baseAddress, std::move(obj));
        return raw;
    }

    // Soft reset: clears transient state, keeps objects alive so the module
    // pointers into them stay valid.
    void reset() {
        for (auto& entry : states) {
            entry.second->reset();
        }
    }

    // Forward the once-per-chunk hook to every shared object (see ModuleSharedState::beginBlock).
    void beginBlock(uint32_t activeVoices, uint32_t frameCount) {
        for (auto& entry : states) {
            entry.second->beginBlock(activeVoices, frameCount);
        }
    }

    // Destroy all shared objects. Only safe when the modules that point at them
    // are also being torn down / rebuilt.
    void clear() {
        states.clear();
    }

private:
    std::unordered_map<uint64_t, std::unique_ptr<ModuleSharedState>> states;
};

#endif /* ModuleSharedState_hpp */
