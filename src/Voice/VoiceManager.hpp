//
//  VoiceModuleManager.hpp
//
//  Created by Eric Kampman on 4/13/25.
//

#ifndef VoiceModuleManager_h
#define VoiceModuleManager_h

#include <vector>
#include <memory>
#include "pm_compat.h"
#include "pm_midi.h"

#include "ModuleFactory.hpp"
#include "Voice.hpp"
#include "PitchConverter.hpp"
#include "ModuleSharedState.hpp"
#include "ConnectionCommandQueue.hpp"


#define LOG_VM(fmt, ...) \
    if (VoiceManager::loggingEnabled) \
        os_log_with_type(logVoiceManager, OS_LOG_TYPE_INFO, fmt, ##__VA_ARGS__)

constexpr size_t MAX_VOICES = 32;

class VoiceManager {
public:
	static constexpr size_t MaxVoices = MAX_VOICES;
		
	VoiceManager(const VoiceManager&) = delete;
	VoiceManager& operator=(const VoiceManager&) = delete;

	// Optionally allow moves if needed
//	VoiceManager(VoiceManager&&) = default;
//	VoiceManager& operator=(VoiceManager&&) = default;

	// ModuleFactory owns blockSize, sampleRate, but
	// putting ModuleFactory in the kernel was problematic.
	VoiceManager(double sampleRate = DEFAULT_SAMPLE_RATE,
                 uint32_t blockSize = MODULE_BUFF_SIZE);
	
	void initialize(double sampleRate);
	void initializeVoices(PitchConverter *pitchConverter);
    
    void reset();

private:
	void initializeModules(Voice& voice, PitchConverter *pitchConverter);
	void buildDefaultPatch(Voice& voice);

public:
	// MIDI Routing
	void handleMIDIMessage(const pm::UmpMessage& message);
	// to have VoiceManager choose the best Voice, use the below for noteOn.
	void noteOn(const pm::UmpMessage& msg);
	void noteOff(const pm::UmpMessage& msg);
	void allNotesOff();
    void panic();

    // Sustain pedal
    void handleSustainPedalChange(bool newState);

    bool freeRunning();
    
    Module *getModule(AUParameterAddress paramAddress);
    // called from VoiceManagerBridge.
    void connectModules(uint64_t upstreamID, uint64_t downstreamID, AUParameterAddress inputAddress);
    void disconnectModule(uint64_t moduleID, int portID);

    // Point a destination mod-input at a source module's output (or disconnect if source == 0).
    // Clears whatever was on the input, then connects the new source, on all voices.
    void setModSource(AUParameterAddress destInput, AUParameterAddress source);

	// DSP block processing
    void process(uint32_t frameCount, float *left, float *right);
//	void process(uint32_t frameCount);
    
    // Parameters
    
    /* voice manager-specific params */
    uint32_t activeCount();
    AUValue voiceManagerParamAtIndex(size_t index);
    AUValue voiceManagerParamByEnum(NSUInteger paramEnum);
    void setVoiceManagerParameter(AUParameterAddress address, AUValue value);

    /* module params */
    void setParameter(AUParameterAddress address, AUValue value);
    AUValue getParameter(AUParameterAddress address);
    void clearInput(AUParameterAddress address);
    
    void updateSampleRate(double newSampleRate);
//    bool isVoiceManagerModule(AUParameterAddress address);

private:
	std::unique_ptr<ModuleFactory> moduleFactory;
    std::unique_ptr<Voice> referenceVoice;
	std::vector<std::unique_ptr<Voice>> voices;
	uint64_t voiceIdCounter = 0;

    PitchConverter pitchConverter;
    ModuleSharedStateRegistry sharedStateRegistry;   // per-placed-module cross-voice state

    float params[MODULE_MAX_PARAMS];    // MIDI and global?

    static bool loggingEnabled;

    // Sustain pedal state
    bool sustainPedalActive = false;
    std::vector<uint32_t> pedalSustainedVoiceIDs;   // voice IDs with deferred NoteOff

    // Lock-free SPSC queue: main thread posts connection commands,
    // render thread drains them at the top of process().
    ConnectionCommandQueue<1024> commandQueue;
    void drainCommandQueue();

	Voice* findFreeVoice();
	Voice* stealVoice(const pm::UmpMessage& msg);
	Voice* findVoiceForMessage(const pm::UmpMessage& msg);
    Voice* oldestVoice();
    Voice* lowestVoice();
    Voice* highestVoice();
    Voice* closest(const pm::UmpMessage& msg);
    Voice* furthest(const pm::UmpMessage& msg);
};

#endif /* VoiceModuleManager_h */
