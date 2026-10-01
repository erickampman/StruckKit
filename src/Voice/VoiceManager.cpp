//
//  VoiceManager.mm
//
//  Created by Eric Kampman on 4/13/25.
//

#include "pm_midi.h"
#include "pm_addresses.h"
#include "VoiceManager.hpp"

bool VoiceManager::loggingEnabled = false;

VoiceManager::VoiceManager(double sampleRate, uint32_t blockSize)
{
	moduleFactory = std::make_unique<ModuleFactory>(sampleRate, blockSize);
	initialize(sampleRate);
}

void VoiceManager::initialize(double sampleRate) {
//    LOG_VM("VoiceManager::initialize entrypoint");
    referenceVoice = std::make_unique<Voice>(sampleRate);
	voices.reserve(MaxVoices);
	for (size_t i = 0; i < MaxVoices; ++i) {
		auto voice = std::make_unique<Voice>(sampleRate);
		voice->id = static_cast<uint32_t>(voiceIdCounter++);
		// Optional: attach oscillator/envelope here via ModuleFactory
		voices.push_back(std::move(voice));
	}
	initializeVoices(&pitchConverter);
}

void VoiceManager::initializeVoices(PitchConverter *pitchConverter)
{
    referenceVoice->initializeModules(moduleFactory.get(), pitchConverter, &sharedStateRegistry);
    buildDefaultPatch(*referenceVoice);
    
	for (auto& voice : voices) {
		voice->initializeModules(moduleFactory.get(), pitchConverter, &sharedStateRegistry);
		buildDefaultPatch(*voice);
	}
}

void VoiceManager::reset() {
    sustainPedalActive = false;
    pedalSustainedVoiceIDs.clear();
    allNotesOff();
    for (auto& voice : voices) {
        voice->reset();
    }
    sharedStateRegistry.reset();   // soft reset: clears state, keeps objects alive
}

// This occurs at a bad time in initialization. I want to defer this until
// later, with default patches.
#define HAS_DEFAULT_PATCH   1
void VoiceManager::buildDefaultPatch(Voice& voice)
{
#if HAS_DEFAULT_PATCH
    // StruckKit voice (step 2: straight PMU DrumKit port): MIDI note-on -> DrumKit -> Final. The
    // note's pitch class picks the drum; there is no VCA envelope — each drum renders its own decay
    // and the voice frees on silence (VoiceStateManager Active -> Deactivating -> Inactive), so the
    // hit rings out past note-off. Hi-hat choke is cross-voice via DrumKit's shared ChokeGroupState.
    //
    // Strike / ModalBank (and the inherited IFS / Sine / LPF / env modules) stay instantiated but
    // unconnected; step 3 swaps DrumKit's additive drum bodies for ModalBank resonators.
    Module* drumKitModule = voice.getModule(drumKit1Address);
    Module* finalModule   = voice.getModule(finalAddress);

    //                       upstream       -> downstream   (downstream input port)
    ++(finalModule->activeCount);
    // connectModules bumps DrumKit's activeCount, so it receives MIDI note-ons.
    voice.connectModules(drumKitModule, finalModule, finalInputAddress);
#else
    // do nothing
#endif
}

void VoiceManager::handleMIDIMessage(const pm::UmpMessage& message) {
	// Note on (and maybe off) require special handling, for
	// Voice allocation.
    if (VoiceManager::loggingEnabled) {
        std::string descStr = pm::toString(message);
        LOG_VM("VoiceManager::handleMIDIMessage %s ", descStr.c_str());
    }
    
    if (pm::isChannelMessage(message)) {
        uint32_t channel = uint32_t(voiceManagerParamByEnum(MIDIParamItemChannelFilterValue));
        if (channel) {
            channel -= 1;
            if (channel != message.channel) {
                return;
            }
        }
    }
    
    uint32_t group = uint32_t(voiceManagerParamByEnum(MIDIParamItemGroupFilterValue));
    if (group) {
        group -= 1;
        if (message.group != group) {
            return;
        }
    }
    
	if (message.messageType == pm::kUmpTypeChannelVoice2) {
		switch (message.status) {
		case pm::kStatusNoteOn:
			noteOn(message);
            return;
        case pm::kStatusNoteOff:
            noteOff(message);
            return;
        case pm::kStatusControlChange:
            // Intercept CC 64 (sustain pedal) for voice-lifecycle management.
            // Fall through so CCConverter modules also receive it.
            if (message.ccIndex == 64) {
                bool newState = message.ccData >= (UINT32_MAX / 2u + 1u);
                handleSustainPedalChange(newState);
            }
            break;
		default:
			break;
		}
	}

	for (auto& voice : voices) {
		if (voice->wantsMIDIMessage(message, voice->id)) {
			voice->handleMIDIMessage(message);
		}
	}
}

bool VoiceManager::freeRunning() {
    AUValue freeRun = voiceManagerParamByEnum(MIDIParamItemFreeRun);
    AUValue voiceCount = voiceManagerParamByEnum(MIDIParamItemVoiceCount);
    return (freeRun >= 0.5f && voiceCount == 1);
}

void VoiceManager::noteOn(const pm::UmpMessage& msg) {
    LOG_VM("VoiceManager::noteOn entrypoint");
    uint32_t maxVoices = uint32_t(voiceManagerParamByEnum(MIDIParamItemVoiceCount));

    // needed?
    if (freeRunning()) {
        Voice* voice = voices[0].get();
        // Optionally: voice->copyParamsFrom(*referenceVoice); if needed
        voice->handleMIDIMessage(msg);  // retune / retrigger as you see fit
        return;
    }

    // Legato mode: when in mono (1 voice), skip the steal path and route
    // the NoteOn directly to the currently active voice.  The ADSR/ADR
    // modules decide independently (via their own legato param) whether
    // to retrigger the envelope or let it continue.
    uint32_t legatoMode = uint32_t(voiceManagerParamByEnum(MIDIParamItemLegato));
    if (legatoMode > 0) {
        for (auto& v : voices) {
            if (v && v->isActive()) {
                v->handleMIDIMessage(msg);
                return;
            }
        }
        // No active voice: fall through to normal allocation below.
    }

    Voice* voice = findVoiceForMessage(msg);
    bool stolen = false;
    if (!voice) {
        voice = findFreeVoice();
    }
	if (!voice) {
		voice = stealVoice(msg); // implement envelope-aware stealing
		if (!voice) return;   // out of voices
        stolen = true;
	}
    if (stolen) {
        // If this voice was pedal-sustained, cancel the deferred NoteOff —
        // startSteal() also clears pedalSustained on the VoiceStateManager.
        auto it = std::find(pedalSustainedVoiceIDs.begin(), pedalSustainedVoiceIDs.end(), voice->id);
        if (it != pedalSustainedVoiceIDs.end()) {
            pedalSustainedVoiceIDs.erase(it);
        }
        voice->startSteal(msg);
    }
    voice->copyParamsFrom(*referenceVoice);
	voice->handleMIDIMessage(msg);
}

void VoiceManager::noteOff(const pm::UmpMessage& msg) {
    LOG_VM("VoiceManager::noteOff entrypoint");

    // FreeRun and legato modes bypass sustain pedal logic:
    // FreeRun ignores note lifecycle entirely; legato has its own held-note stack.
    bool bypass = freeRunning() ||
                  (uint32_t(voiceManagerParamByEnum(MIDIParamItemLegato)) > 0);

    for (auto& voice : voices) {
        if (!voice->wantsMIDIMessage(msg, voice->id)) continue;

        if (!bypass && sustainPedalActive) {
            // Pedal held — defer the NoteOff. Only add to list if not already there
            // (guards against duplicate entries if multiple buffers send the same NoteOff).
            if (!voice->voiceStateManager.isPedalSustained()) {
                voice->voiceStateManager.setPedalSustained(true);
                pedalSustainedVoiceIDs.push_back(voice->id);
            }
        } else {
            voice->handleMIDIMessage(msg);
        }
    }
}

void VoiceManager::handleSustainPedalChange(bool newState) {
    if (newState == sustainPedalActive) return;
    sustainPedalActive = newState;

    if (!newState && !pedalSustainedVoiceIDs.empty()) {
        // Pedal released — dispatch deferred NoteOffs to all sustained voices.
        for (uint32_t voiceID : pedalSustainedVoiceIDs) {
            for (auto& voice : voices) {
                if (voice->id == voiceID && voice->voiceStateManager.isPedalSustained()) {
                    voice->voiceStateManager.setPedalSustained(false);
                    pm::UmpMessage noteOff = pm::makeNoteOff(voice->currentNoteNumber);
                    voice->handleMIDIMessage(noteOff);
                }
            }
        }
        pedalSustainedVoiceIDs.clear();
    }
}

void VoiceManager::allNotesOff() {
    // Clear pedal state so no deferred NoteOffs linger after all-notes-off.
    sustainPedalActive = false;
    pedalSustainedVoiceIDs.clear();

	pm::UmpMessage msg = pm::makeNoteOff(0);

	for (auto& voice : voices) {
        voice->voiceStateManager.setPedalSustained(false);
		if (voice->isActive()) {
			for (uint8_t curNote = 0; curNote <= 127; ++curNote) {
				msg.note = curNote;
				voice->handleMIDIMessage(msg);
			}
		}
	}
}

void VoiceManager::panic() {
    // Read the current Group and Channel filter settings.
    // Values: 0 = no filter, 1-16 maps to group/channel 0-15.
    uint32_t groupFilter   = uint32_t(voiceManagerParamByEnum(MIDIParamItemGroupFilterValue));
    uint32_t channelFilter = uint32_t(voiceManagerParamByEnum(MIDIParamItemChannelFilterValue));

    // If a filter is active use that specific group/channel; otherwise default to 0.
    uint8_t group   = (groupFilter   > 0) ? uint8_t(groupFilter   - 1) : 0;
    uint8_t channel = (channelFilter > 0) ? uint8_t(channelFilter - 1) : 0;

    // Send Note Off for every possible note number, targeting every voice directly
    // so the Group/Channel filter in handleMIDIMessage() is intentionally bypassed.
    for (uint8_t note = 0; note <= 127; ++note) {
        pm::UmpMessage msg = pm::makeNoteOff(note, channel, group);
        for (auto& voice : voices) {
            voice->handleMIDIMessage(msg);
        }
    }
}

void VoiceManager::drainCommandQueue()
{
    VoiceCommand cmd;
    while (commandQueue.pop(cmd)) {
        switch (cmd.type) {

        case VoiceCommandType::Connect: {
            for (auto& voice : voices) {
                Module *up = voice->getModule(cmd.upstreamID);
                Module *dn = voice->getModule(cmd.downstreamID);
                voice->connectModules(up, dn, cmd.inputAddress);
            }
            break;
        }

        case VoiceCommandType::ClearInput: {
            uint64_t baseAddr = MODULE_BASE_ADDRESS(cmd.inputAddress);
            uint8_t inputID   = static_cast<uint8_t>(MODULE_ITEM_FROM_ADDRESS(cmd.inputAddress));
            for (auto& voice : voices) {
                Module *mod = voice->getModule(baseAddr);
                if (mod) {
                    // Decrement upstream's activeCount before clearing the slot,
                    // completing the compound op that connectModules started.
                    Module* upstream = mod->inputs[inputID].upstream;
                    if (upstream && upstream->activeCount > 0) {
                        --(upstream->activeCount);
                    }
                    mod->removeInput(inputID);
                }
            }
            break;
        }
        }
    }
}

void VoiceManager::process(uint32_t frameCount, float *left, float *right)
{
    // Apply any pending connection changes before processing voices.
    drainCommandQueue();

    // necessary?
    std::fill_n(left, frameCount, 0.f);
    std::fill_n(right, frameCount, 0.f);

    uint32_t maxVoices = uint32_t(voiceManagerParamByEnum(MIDIParamItemVoiceCount));
    
    for (size_t i = 0; i < voices.size(); ++i) {
        auto& v = voices[i];
        if (!v) continue;
        
        bool active = false;
        
        if (freeRunning() && i == 0) {  // only voice[0] potentially is free-runnning
            active = true;
        } else {
            if (v->isActive()) { active = true; }
        }

        if (active) {
            v->process(frameCount, left, right);
        }
	}
}

//uint32_t VoiceManager::activeCount() {
//    uint32_t activeCount = 0;
//    for (auto& voice : voices) {
//        if (voice->isActive()) {
//            activeCount++;
//        }
//    }
//    return activeCount;
//}
uint32_t VoiceManager::activeCount()
{
    uint32_t activeCount = 0;
    bool freeRun = freeRunning();

    for (size_t i = 0; i < voices.size(); ++i) {
        const auto& v = voices[i];
        if (!v) {
            continue;
        }

        if (v->isActive()) {
            ++activeCount;
        } else if (freeRun && i == 0) {
            // Voice 0 is considered logically active in free-run mode
            ++activeCount;
        }
    }

    return activeCount;
}


Voice* VoiceManager::findFreeVoice() {
    uint32_t max = voiceManagerParamByEnum(MIDIParamItemVoiceCount);
    if (activeCount() < max) {
        for (auto& voice : voices) {
            if (!voice->isActive())
                return voice.get();
        }
    }
    return nullptr;
}

// TODO: implement more advanced stealing (e.g. quietest, oldest)
Voice* VoiceManager::stealVoice(const pm::UmpMessage& msg) {
    Voice *stolen = nullptr;
    
//    for (auto& voice : voices) {
//        if (voice->voiceStateManager.canBeStolen()) {
//            stolen = voice.get(); // safe to reuse
//            LOG_VM("VoiceManager::stealVoice canBeStolen id(%d)", voice->id);
//            break;
//        }
//    }
//    if (!stolen) {
        uint8_t algorithm = uint8_t(voiceManagerParamByEnum(MIDIParamItemVoiceStealAlgorithm));
        switch (algorithm) {
        case VoiceStealAlgorithmOldest:
            stolen = oldestVoice();
            break;
        case VoiceStealAlgorithmLowest:
            stolen = lowestVoice();
            break;
        case VoiceStealAlgorithmHighest:
            stolen = highestVoice();
            break;
        case VoiceStealAlgorithmClosest:
            stolen = closest(msg);
            break;
        case VoiceStealAlgorithmFurthest:
            stolen = furthest(msg);
            break;
        }
        if (stolen) {
            LOG_VM("VoiceManager::stealVoice voice id(%d)", stolen->id);
        }
//    }
    return stolen;
}


Voice* VoiceManager::oldestVoice() {
    Voice* oldestVoice = nullptr;
    TimePoint oldestTime = Clock::now();

    for (auto& voice : voices) {
        if (!voice->voiceStateManager.canBeStolen()) continue;

        if (voice->lastUsed < oldestTime) {
            oldestTime = voice->lastUsed;
            oldestVoice = voice.get();
        }
    }
    return oldestVoice;
}
Voice* VoiceManager::lowestVoice() {
    Voice* ret = nullptr;

    for (auto& voice : voices) {
        if (!voice->voiceStateManager.canBeStolen()) continue;

        if (!ret) {
            ret = voice.get();
        } else {
            uint8_t currentNote = voice->currentNoteNumber;
            if (currentNote) {
                if (voice->currentNoteNumber < ret->currentNoteNumber) {
                    ret = voice.get();
                }
            }
        }
    }
    return ret;
}
Voice* VoiceManager::highestVoice() {
    Voice* ret = nullptr;

    for (auto& voice : voices) {
        if (!voice->voiceStateManager.canBeStolen()) continue;

        if (!ret) {
            ret = voice.get();
        } else {
            uint8_t currentNote = voice->currentNoteNumber;
            if (currentNote) {
                if (voice->currentNoteNumber > ret->currentNoteNumber) {
                    ret = voice.get();
                }
            }
        }
    }
    return ret;
}

Voice* VoiceManager::closest(const pm::UmpMessage& msg) {
    Voice* ret = nullptr;

    if (!pm::isNoteOn(msg)) {
        return ret;
    }
    uint8_t msgNote = msg.note;
    
    for (auto& voice : voices) {
        if (!voice->voiceStateManager.canBeStolen()) continue;

        if (!ret) {
            ret = voice.get();
        } else {
            uint8_t currentNote = voice->currentNoteNumber;
            if (currentNote) {
                uint8_t newDist = abs(voice->currentNoteNumber - msgNote);
                uint8_t oldDist = abs(ret->currentNoteNumber - msgNote);
                
                if (newDist < oldDist) {
                    ret = voice.get();
                }
            }
        }
    }
    return ret;
}

Voice* VoiceManager::furthest(const pm::UmpMessage& msg) {
    Voice* ret = nullptr;

    if (!pm::isNoteOn(msg)) {
        return ret;
    }
    uint8_t msgNote = msg.note;
    
    for (auto& voice : voices) {
        if (!voice->voiceStateManager.canBeStolen()) continue;

        if (!ret) {
            ret = voice.get();
        } else {
            uint8_t currentNote = voice->currentNoteNumber;
            if (currentNote) {
                uint8_t newDist = abs(voice->currentNoteNumber - msgNote);
                uint8_t oldDist = abs(ret->currentNoteNumber - msgNote);
                
                if (newDist > oldDist) {
                    ret = voice.get();
                }
            }
        }
    }
    return ret;
}


// called from VoiceManagerBridge.
void VoiceManager::connectModules(uint64_t upstreamID, uint64_t downstreamID, AUParameterAddress inputAddress)
{
    // referenceVoice is only ever accessed from the main thread, so it is safe
    // to mutate its Module::inputs directly here.
    Module *upstreamModule = referenceVoice->getModule(upstreamID);
    Module *downstreamModule = referenceVoice->getModule(downstreamID);
    referenceVoice->connectModules(upstreamModule, downstreamModule, inputAddress);

    // Polyphonic voices are processed on the render thread, so we must not
    // mutate their Module::inputs here.  Post a command to the SPSC queue;
    // the render thread will drain it at the top of process().
    VoiceCommand cmd;
    cmd.type         = VoiceCommandType::Connect;
    cmd.upstreamID   = upstreamID;
    cmd.downstreamID = downstreamID;
    cmd.inputAddress = inputAddress;
    commandQueue.push(cmd);
}

Voice* VoiceManager::findVoiceForMessage(const pm::UmpMessage& msg) {
    
    if (pm::isNoteOn(msg)) {
        uint32_t max = voiceManagerParamByEnum(MIDIParamItemVoiceCount);
        if (activeCount() >= max) {
            return nullptr;
        }
    }
	for (auto& voice : voices) {
        if (voice->wantsMIDIMessage(msg, 9999)) {
            return voice.get();
        }
	}
	return nullptr;
}

// Parameters

AUValue VoiceManager::voiceManagerParamAtIndex(size_t index)
{
    return params[index];
//        return params_[index];
}

AUValue VoiceManager::voiceManagerParamByEnum(NSUInteger paramEnum) {
    size_t index = PARAM_ENUM_TO_INDEX(paramEnum);
    return voiceManagerParamAtIndex(index);
}

void VoiceManager::setVoiceManagerParameter(AUParameterAddress address, AUValue value) {
//    if (!isVoiceManagerModule(address)) {
//        return;
//    }
    uint8_t item = MODULE_ITEM_FROM_ADDRESS(address);
    params[item - 1] = value;
}

/*
    The below are params distributed to modules
 */

void VoiceManager::setParameter(AUParameterAddress address, AUValue value)
{
    referenceVoice->setParameter(address, value);
    for (auto& vp : voices) {
        vp->setParameter(address, value);
    }

}
AUValue VoiceManager::getParameter(AUParameterAddress address) {
    return referenceVoice->getParameter(address);
}

void VoiceManager::setModSource(AUParameterAddress destInput, AUParameterAddress source)
{
    clearInput(destInput);                 // disconnect current source (safe if none)
    if (source != 0)                        // 0 = None
        connectModules(source, MODULE_BASE_ADDRESS(destInput), destInput);
}

void VoiceManager::clearInput(AUParameterAddress address)
{
    uint64_t baseAddr = MODULE_BASE_ADDRESS(address);
    uint8_t inputID = static_cast<uint8_t>(MODULE_ITEM_FROM_ADDRESS(address));

    // referenceVoice is main-thread-only; mutate directly.
    Module *module = referenceVoice->getModule(baseAddr);
    if (module) {
        // Decrement the upstream module's activeCount before the slot is
        // cleared, mirroring what Voice::connectModules did on connect.
        // (Module::removeInput has this commented out as "Done in Voice".)
        Module* upstream = module->inputs[inputID].upstream;
        if (upstream && upstream->activeCount > 0) {
            --(upstream->activeCount);
        }
        module->removeInput(inputID);
    }

    // Zero the AUParameter value so the state is persisted correctly.
    setParameter(address, 0);

    // Defer the per-voice removeInput to the render thread via the command queue.
    VoiceCommand cmd;
    cmd.type         = VoiceCommandType::ClearInput;
    cmd.upstreamID   = 0;
    cmd.downstreamID = 0;
    cmd.inputAddress = address;
    commandQueue.push(cmd);
}

void VoiceManager::updateSampleRate(double newSampleRate) {
    moduleFactory->updateSampleRate(newSampleRate); // if needed

    referenceVoice->updateSampleRate(newSampleRate);
    for (auto& voice : voices) {
        voice->updateSampleRate(newSampleRate);
    }
}


//bool VoiceManager::isVoiceManagerModule(AUParameterAddress address) {
//    uint8_t moduleKind = MODULE_KIND_FROM_ADDRESS(address);
//    switch (moduleKind) {
//    case ModuleParamKindMIDI:
//        return true;
//    default:
//        return false;
//    }
//}
//
