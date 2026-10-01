//
//  Voice.hpp
//
//  Created by Eric Kampman on 4/13/25.
//

#ifndef Voice_h
#define Voice_h

#include <chrono>
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

#include "pm_compat.h"
#include "pm_midi.h"

#include <unordered_set>

#include "CPPProcs.hpp"
#include "CPPTypes.hpp"
#include "pm_params.h"

#include "ModuleMap.hpp"
#include "ModuleFactory.hpp"

#include "VoiceStateManager.hpp"

class ModuleSharedStateRegistry;   // ModuleSharedState.hpp — cross-voice shared state (pointer param only)


#define LOG_VOICE(fmt, ...) \
    if (Voice::loggingEnabled) \
        os_log_with_type(logVoice, OS_LOG_TYPE_INFO, fmt, ##__VA_ARGS__)

class PitchConverter;

struct Voice {
	uint32_t id;
    uint8_t currentNoteNumber = 0;
	uint8_t targetNoteNumber = 0;

	uint16_t currentVelocity = 0;
	uint16_t targetVelocity = 0;
	
	// Output buffers
	ModuleFloatBuff* outLeft = nullptr;
	ModuleFloatBuff* outRight = nullptr;
	
	// process czll tracking
	uint64_t processCallID = 0;
    
    // Needed for note stealing
    double sampleRate = 44100.0;
    
    VoiceStateManager voiceStateManager;
	
//	bool pendingDeactivate = false;
//    bool ducking = false;   // either note steal or smoothing at a pitch change.
//    float duckPerSample = 1.0;
//    float duckMultiplier = 1.0;
    
    TimePoint lastUsed;
    
    static bool loggingEnabled;
	
	// the Module pool. Note the ModuleMap OWNS the modules (for THIS voice)
private:
	ModuleMap moduleMap;
//	static std::vector<ModuleInitSpec> moduleSpecs;
public:
    
    Voice(double inSampleRate)
        : voiceStateManager(inSampleRate) { sampleRate = inSampleRate; }
    Module *getModule(AUParameterAddress paramAddress);
    Module *getFinalModule();

	void initializeModules(ModuleFactory *factory, PitchConverter *pitchConverter,
	                       ModuleSharedStateRegistry *sharedStateRegistry);
//	void initializeModule(const AUParameterAddress moduleAddress, const ModuleParamConstantsGroup *constantsGroup);
	
	// currently, connectModules can't fail.
//	void connectModules(AUParameterAddress fromID, AUParameterAddress toID, size_t inputIndex);
	void connectModules(Module *upstreamModule, Module *downstreamModule, AUParameterAddress inputAddress);
    void connectTunnelPairs();
    
	void clearModules() { moduleMap.clear(); }
    
    bool isActive();
    bool wantsMIDIMessage(const pm::UmpMessage& deferredMessage, uint32_t voiceID) const;
    
    void reset();

	// Initialization
//	void noteOn(uint8_t noteNumber, uint8_t velocity);
//	void noteOff();
    void setTimestamp() { lastUsed = Clock::now(); }
    void setSampleRate(double inSampleRate);
    void updateSampleRate(double newSampleRate);
    
	void startSteal(const pm::UmpMessage& deferredMessage); // handle fast kill and voice reassign

	void handleMIDIMessage(const pm::UmpMessage& message);
//	void process(uint32_t frameCount);
    void process(uint32_t frameCount, float *left, float *right);
	void dispatchModuleEvent(ModuleEvent event, void* state);
//	bool dispatchModuleQuery(ModuleQueryProc proc, void* state, ModuleQueryType type);
	void panic();

	bool areAllEnvelopesComplete() { return false; }  // FIXME
	
	bool disconnectConnection(AUParameterAddress fromID, AUParameterAddress toID);
	bool dfsDisconnect(Module *current, Module *upstream, Module *downstream,
					   std::unordered_set<Module*>& visited);
//	bool dfsDisconnect(AUParameterAddress currentID, AUParameterAddress fromID, AUParameterAddress toID, std::unordered_set<AUParameterAddress>& visited);

	bool isSilent(float threshold) const;
    
    float setDuckPerSample();
    
    // Parameters
    void copyParamsFrom(const Voice& source);
    void setParameter(AUParameterAddress address, AUValue value);
    AUValue getParameter(AUParameterAddress address);

    bool canBeStolen() const {
        return voiceStateManager.canBeStolen();
    }
    VoiceState voiceState() { return voiceStateManager.getState(); }
private:
	AUParameterAddress finalModuleID;
};


#endif /* Voice_h */
