//
//  Voice.mm
//
//  Created by Eric Kampman on 4/19/25.
//

#include <memory>

#include "pm_midi.h"

#include "Voice.hpp"
#include "ModuleFactory.hpp"
#include "pm_addresses.h"
#include "ModuleInitSpecs.hpp"
#include "ModuleSharedState.hpp"

//#include "SinOscillatorModule.hpp"
//#include "LPFilterModule.hpp"
//#include "ADRModule.hpp"
//#include "ADSRModule.hpp"
//#include "CombineModule.hpp"
//#include "QuadCombineModule.hpp"
//#include "FinalModule.hpp"
//#include "PitchConverterModule.hpp"
//#include "SlewModule.hpp"

bool Voice::loggingEnabled = false;

#if 0
/* Add to these as modules are defined. */
static const std::vector<ModuleInitSpec> moduleInitSpecs = {
    { pitchConverter1Address, &pitchConverterModuleConstantsGroup, InitPitchConverter, DeinitPitchConverter, ProcessPitchConverter, MIDIPitchConverter, nullptr, nullptr },
    { slew1Address, &slewModuleConstantsGroup, InitSlewModule, DeinitSlewModule, ProcessSlewModule, nullptr, nullptr, nullptr },
	{ sinOsc1Address, &sinOsc1ConstantsGroup, InitSinOscillator, DeinitSinOscillator, ProcessSinOscillator, MIDISinOscillator, nullptr, nullptr },
    { lpfAddress, &lpfConstantsGroup, InitLPFilterModule, DeinitLPFilterModule, ProcessLPFilterModule, MIDIProcLPFilterModule, nullptr, nullptr },
	{ expEnvAddress, &expEnvConstantsGroup, InitADRModule, DeinitADRModule, ProcessADRModule, MIDIProcADRModule, ParamADR, nullptr },
	{ adsr1Address, &adsrConstantsGroup, InitADSRModule, DeinitADSRModule, ProcessADSRModule, MIDIProcADSRModule, ParamADSR, nullptr },
    { combine1Address, &combineConstantsGroup, nullptr, nullptr, ProcessCombineModule, nullptr, nullptr, nullptr },
    { quadMonoCombine1Address, &quadMonoCombinerConstantsGroup, nullptr, nullptr, ProcessQuadCombineModule, nullptr, nullptr, nullptr },
	{ finalAddress, &finalConstantsGroup, InitFinalModule, DeinitFinalModule, ProcessFinalModule, MIDIFinalModule, nullptr, nullptr },
};
#endif /* 0 */

void Voice::initializeModules(ModuleFactory *factory, PitchConverter *pitchConverter,
                              ModuleSharedStateRegistry *sharedStateRegistry)
{
    auto moduleInitSpecs = getModuleInitSpecs();
    moduleMap.reserveModules(moduleInitSpecs.size());

 	for (const auto& spec : moduleInitSpecs) {
		auto module = factory->createModule(spec.id,
											spec.constantsGroup,
											spec.initProc,
											spec.deinitProc,
											spec.processProc,
											spec.midiProc,
                                            spec.paramProc,
                                            spec.resetProc,
                                            spec.sampleRateProc
                                            );
        if (module->pitchConverter == nullptr) {
            module->setPitchConverter(pitchConverter);  // this one's global
        }

        // Cross-voice shared object: one per PLACED module (keyed by base address, identical across
        // voices), so every voice's copy of this module sees the same object. See ModuleSharedState.hpp.
        if (spec.sharedStateCreateProc && sharedStateRegistry) {
            module->sharedState =
                sharedStateRegistry->getOrCreate(MODULE_BASE_ADDRESS(spec.id),
                                                 spec.sharedStateCreateProc);
        }
        
        // not really cool to set here, but saves on redundancy
        setSampleRate(factory->sampleRate);

        // Done in module initializerion
//		if (spec.initProc) {
//			spec.initProc(1, module.get());
//		}

		moduleMap.add(spec.id, std::move(module));
	}
    
    connectTunnelPairs();
    
    // This will always be the last module. No exceptions.
    // FIXME -- make sure it can't be detached.
    finalModuleID = finalAddress;
}

Module *Voice::getModule(AUParameterAddress paramAddress)
{
	return moduleMap.get(paramAddress);
}

Module *Voice::getFinalModule()
{
    return moduleMap.get(finalAddress);
}

void Voice::connectModules(Module *upstreamModule, Module *downstreamModule, AUParameterAddress inputAddress)
{
    LOG_VOICE("Voice::connectModules address(%p)\n", this);
    LOG_VOICE("Voice::connectModules downstream Module address(%p)\n", downstreamModule);

	// I'm not sure it's worth bothering with a pre-flight, but
	if (!upstreamModule || !downstreamModule) { return; }
	
	KPBuffPair* buffer = &upstreamModule->output;
	uint8_t paramID = static_cast<uint8_t>(MODULE_ITEM_FROM_ADDRESS(inputAddress));
    ++(upstreamModule->activeCount);
	downstreamModule->insertInput(paramID, buffer, upstreamModule);
    
    LOG_VOICE("Voice::connectModules downstream Module id(0x%llx) got input at key(%u)\n", downstreamModule->id, paramID);
}

void Voice::connectTunnelPairs() {
    for (Module* module : moduleMap.allModules()) {
        if (module->moduleKind() == ModuleParamKind::ModuleParamKindTunnelOutput) {
            uint8_t instance = MODULE_INSTANCE_FROM_ADDRESS(module->id);
            AUParameterAddress inModuleAddress = MODULE_PARAM_ADDRESS(ModuleParamKindTunnelInput, instance, 0);
            Module* tunnelInModule = moduleMap.get(inModuleAddress);
            AUParameterAddress inputInputAddress = MODULE_PARAM_FROM_BASE_AND_ITEM(inModuleAddress, TunnelOutParamItemInput);
            if (tunnelInModule) {
                connectModules(tunnelInModule, module, inputInputAddress);
            }
        }
    }
}

bool Voice::disconnectConnection(AUParameterAddress upstreamID, AUParameterAddress downstreamID) {
	std::unordered_set<Module*> visited;
	Module *finalModule = moduleMap.get(finalModuleID);
	Module *upstreamModule = moduleMap.get(upstreamID);
	Module *downstreamModule = moduleMap.get(downstreamID);
	if (finalModule == nullptr || upstreamModule == nullptr || downstreamModule == nullptr) { return false; }
	
	return dfsDisconnect(finalModule, upstreamModule, downstreamModule, visited);
}

bool Voice::dfsDisconnect(Module *current,
						  Module *upstream,
						  Module *downstream,
						  std::unordered_set<Module*>& visited)
{
	if (visited.count(current)) return false;
	visited.insert(current);

	if (current == downstream) {
		bool removed = false;
		for (uint8_t i = 1; i <= MODULE_MAX_PARAMS; ++i) {
			InputSlot& slot = current->inputs[i];
			if (slot.upstream == upstream) {
				current->removeInput(i);
				if (upstream && upstream->activeCount > 0) {
					upstream->activeCount--;
				}
				removed = true;
				break;
			}
		}
		return removed;
	}

	for (uint8_t i = 1; i <= MODULE_MAX_PARAMS; ++i) {
		InputSlot& slot = current->inputs[i];
		if (!slot.upstream) continue;
		if (dfsDisconnect(slot.upstream, upstream, downstream, visited)) {
			return true;
		}
	}
	return false;
}

bool Voice::isActive() {
    VoiceState state = voiceStateManager.getState();
    
    return state != VoiceState::Inactive;
}

bool Voice::wantsMIDIMessage(const pm::UmpMessage& message, uint32_t voiceID) const {
    return voiceStateManager.wantsMIDIMessage(message, voiceID);
}

void Voice::reset() {
    for (Module* module : moduleMap.allModules()) {
        module->reset();
    }
}

// The VoiceStateManager needs to be called to copy the data
// out of the final buffers and into left and right.
void Voice::process(uint32_t frameCount, float *left, float *right) {
	Module* final = moduleMap.get(finalModuleID);
    if (final && final->processProc) {
        ++processCallID;
        final->processProc(processCallID, frameCount, final);
        
        voiceStateManager.process(frameCount, this, left, right);
        if (!voiceStateManager.isActive()) {
            currentNoteNumber = 0;
        }
    }
}

void Voice::handleMIDIMessage(const pm::UmpMessage& message) {
    if (Voice::loggingEnabled)  {
        LOG_VOICE(">>> Voice::handleMIDIMessage entrypoint id(%d)", id);
        std::string details = pm::toString(message);
        LOG_VOICE("Voice::handleMIDIMessage %s", details.c_str());
    }

    if (pm::isNoteOn(message)) {
        currentNoteNumber = message.note;
    }
    // generally should suppress propagation only if in stealing state
    bool shouldPropagate = voiceStateManager.handleMIDIMessage(message, id);

    if (shouldPropagate) {
        for (Module* module : moduleMap.allModules()) {
            if (module && module->activeCount && module->midiProc) {
                module->midiProc(message, module);
            }
        }
    }
    LOG_VOICE("<<< Voice::handleMIDIMessage exit id(%d)", id);
}

void Voice::setSampleRate(double inSampleRate) {
    sampleRate = inSampleRate;
}

void Voice::startSteal(const pm::UmpMessage& deferredMessage)
{
    setTimestamp();
 
    voiceStateManager.startSteal(deferredMessage);
}

float Voice::setDuckPerSample()
{
    const double duckTime = 0.005;
    
    return 1.0/(sampleRate * duckTime);
    
}

void Voice::dispatchModuleEvent(ModuleEvent event, void* state) {
	for (Module* module : moduleMap.allModules()) {
		if (module && module->activeCount > 0 && module->eventProc) {
			module->eventProc(module, event, state);
		}
	}
}

bool Voice::isSilent(float threshold) const {
	const Module* final = moduleMap.get(finalModuleID);
	if (!final || !final->output.first) return true;

	const float* samples = final->output.first->data;
	float sumSq = 0.0f;
	for (size_t i = 0; i < MODULE_BUFF_SIZE; ++i) {
		sumSq += samples[i] * samples[i];
	}
	float rms = sqrtf(sumSq / MODULE_BUFF_SIZE);
	return rms < threshold;
}

// Parameters

void Voice::copyParamsFrom(const Voice& source) {
    for (Module* module : moduleMap.allModules()) {
        if (!module) continue;
        Module* sourceModule = source.moduleMap.get(module->id);
        if (sourceModule) {
            module->copyParamsFrom(*sourceModule);
        }
    }
}

void Voice::setParameter(AUParameterAddress address, AUValue value)
{
    if (Module *module = moduleMap.get(address)) {
        uint8_t index = MODULE_ITEM_FROM_ADDRESS(address) - 1; // params start at ID = 1
        if (index < MODULE_MAX_PARAMS) {
            module->params[index] = value;
            if (module->paramProc) {
                (module->paramProc)(module, address, value);
            }
        }
    }
}
AUValue Voice::getParameter(AUParameterAddress address)
{
    if (Module *module = moduleMap.get(address)) {
        return module->getParameter(address);
    }
    // FIXME this is an error
    return 0;
}

void Voice::updateSampleRate(double newSampleRate) {
    setSampleRate(newSampleRate);

    for (Module* module : moduleMap.allModules()) {
        if (module) {
            module->updateSampleRate(newSampleRate);
        }
    }
}
