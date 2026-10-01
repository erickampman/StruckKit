//
//  Module.h
//
//  Created by Eric Kampman on 4/8/25.
//

#ifndef Module_h
#define Module_h

#include <atomic>
#include <vector>
#include <cstdint>
#include <utility>
#include <functional>
#include <algorithm>

#include "pm_compat.h"
#include "pm_midi.h"

#include "CPPTypes.hpp"
#include "ModuleMap.hpp"
#include "ModuleGraph.hpp"
#include "pm_params.h"
#include "ParamAddressMacros.h"
#include "KPBuffPairHelper.hpp"
// #import "PitchConverter.hpp"


struct Module;
struct Voice;
class PitchConverter;

// need a bunch for EDODiscretizer. Kind of a waste, since most are booleans,
// but translating between bitfields and the underlying float values would get
// kind of hairy if I were to go that route. 
constexpr int MODULE_MAX_PARAMS = 50;

typedef const KPBuffPair* (*ModuleProcessProc)(uint64_t processCallID, uint32_t count, Module* module);
typedef void (*ModuleMIDIProc)(const pm::UmpMessage& message, Module* module);
typedef const void (*ModuleInitProc)(uint32_t count, Module* module);
typedef const void (*ModuleDeinitProc)(Module* module);
typedef const void (*ModuleParamProc)(Module* module, AUParameterAddress address, AUValue value);
typedef const void (*ModuleResetProc)(Module* module);
typedef const void (*ModuleSampleRateProc)(Module* module, double newSampleRate);


inline bool loggingEnabledForID(AUParameterAddress) { return false; }

// set to true in Module.mm gModuleLoggingEnabled for particular modules
#define LOG_MODULE(fmt, addr, ...) \
    if (loggingEnabledForID(addr)) \
        os_log_with_type(logVoiceManager, OS_LOG_TYPE_INFO, fmt, ##__VA_ARGS__)


enum class ModuleEvent {
	NoteStolen,
	// Future: SustainPedal, Panic, AllNotesOff, etc.
};

typedef void (*ModuleEventProc)(Module* module, ModuleEvent event, void* state);

enum class ModuleQueryType {
	IsEnvelopeComplete,
	// Future: IsSilent, HasModulation, etc.
};

typedef bool (*ModuleQueryProc)(Module* module, void* state, ModuleQueryType type);

// Cross-voice shared state (see ModuleSharedState.hpp). A module type that needs one object shared
// across all voices' copies of a placement declares this proc in its ModuleInitSpec; the registry
// creates one per placed module and injects it as Module::sharedState below.
struct ModuleSharedState;
typedef ModuleSharedState* (*ModuleSharedStateCreateProc)();


struct InputSlot {
//	AUParameterAddress moduleID;	// I don't think I need this.
	KPBuffPair* buffer = nullptr;
	Module* upstream = nullptr;
	
	InputSlot() = default;
	~InputSlot() = default;
};

// Inputs must be configured before processing begins.
// Buffers must point to persistent upstream module outputs.


struct Module {
	friend struct Voice;
	Module(AUParameterAddress id,
		   double sampleRate = 44100.0,
		   uint32_t blockSize = 512,
           const ModuleParamConstantsGroup *constantsGroup = 0,
		   ModuleInitProc initProc = 0, ModuleDeinitProc deinitProc = 0,
		   ModuleProcessProc processProc = 0, ModuleMIDIProc midiProc = 0,
           ModuleParamProc paramProc = 0,
           ModuleResetProc resetProc = 0,
           ModuleSampleRateProc sampleRateProc = 0,
           
           // may not implement these
		   ModuleEventProc eventProc = 0,
           ModuleQueryProc queryProc = 0
           )
	{
		this->id = id;
        this->constantsGroup = constantsGroup;  // not on the heap
		this->processProc = processProc;
		this->midiProc = midiProc;
		this->deinitProc = deinitProc;
		this->eventProc = eventProc;
		this->queryProc = queryProc;
        this->paramProc = paramProc;
        this->resetProc = resetProc;
        this->sampleRateProc = sampleRateProc;
		
		this->paramKind = static_cast<ModuleParamKind>(MODULE_KIND_FROM_ADDRESS(id));
		this->instance = static_cast<ModuleInstance>(MODULE_INSTANCE_FROM_ADDRESS(id));

        mSampleRate = sampleRate;
		mFrequencyScale = 1.0 / sampleRate;
		
        output.first = std::make_unique<ModuleFloatBuff>();
        output.second = std::make_unique<ModuleFloatBuff>();
        
        loopOutput.first = std::make_unique<ModuleFloatBuff>();
        loopOutput.second = std::make_unique<ModuleFloatBuff>();

		if (initProc) {
			(*initProc)(blockSize, this);
		}
	}
	
	~Module() {
//		if (output.first) delete output.first;
//		if (output.second) delete output.second;
		
		if (deinitProc) {
			(*deinitProc)(this);
		}
	}
    
    ModuleParamKind moduleKind() {
        return static_cast<ModuleParamKind>(MODULE_KIND_FROM_ADDRESS(id));
    }
    
    void reset() {
        if (output.first) {
            std::fill_n(output.first->data, MODULE_BUFF_SIZE, 0.f);
        }
        if (output.second) {
            std::fill_n(output.second->data, MODULE_BUFF_SIZE, 0.f);
        }
        
        mOmegaL = mOmegaR = 0.0;
        
        if (resetProc) {
            (*resetProc)(this);
        }
    }
    
    void updateSampleRate(double newSampleRate) {
        mSampleRate = newSampleRate;
        mFrequencyScale = 1.0 / newSampleRate;
        
        if (sampleRateProc) {
            (*sampleRateProc)(this, newSampleRate);
        }
    }
    
    void setPitchConverter(PitchConverter* pc) { pitchConverter = pc; }
	
	void addParams(const ModuleParamConstantsGroup *group) {
		for (int i = 0; i < group->count; ++i) {
			const ModuleParamConstants& item = group->items[i];
            // Items are 1-based; params[] is 0-based (matches paramByEnum / setParameter).
			if (item.item > 0 && item.item <= MODULE_MAX_PARAMS) {
				this->params[item.item - 1] = item.defValue;
			}
			else {
				// print error FIXME
			}
		}
	}
    
    AUValue paramAtIndex(size_t index)
    {
        return params[index];
//        return params_[index];
    }
    
    AUValue paramByEnum(NSUInteger paramEnum) {
        size_t index = PARAM_ENUM_TO_INDEX(paramEnum);
        return paramAtIndex(index);
    }

	
	// NOTE: param is at index paramItem - 1.
	// Params always start at value 1.
	inline AUValue getParamValue(const uint8_t paramItem) const {
		return params[paramItem - 1];
	}
	
	inline int getParamValueAsInt(const uint8_t paramItem) const {
		return static_cast<int>(params[paramItem - 1]);
	}
    
    

private:
	void clearInputs() {
		for (int i = 1; i <= MODULE_MAX_PARAMS; ++i) {
			inputs[i].buffer   = nullptr;
			inputs[i].upstream = nullptr;
		}
	}
	// insertInput sets the slot at [key] unconditionally. No heap allocation.
	void insertInput(uint8_t key, KPBuffPair* upstreamModuleBuffer, Module* upstreamModule) {
		inputs[key].buffer   = upstreamModuleBuffer;
		inputs[key].upstream = upstreamModule;
	}

public:

	int retrieveInput(uint8_t key, const KPBuffPair **buffer, uint64_t processCallID, uint32_t count, Module **module = nullptr)
	{
        InputSlot &slot = inputs[key];
        Module *mp = slot.upstream;
        if (!mp) {
            module = nullptr;
            *buffer = nullptr;
            return -1;
        }
        if (module != nullptr) {
            *module = mp;
        }
        *buffer = innerRetrieveInput(mp, processCallID, count);
        return 0;
	}

    bool hasInput(uint8_t key) {
        return inputs[key].upstream != nullptr;
    }

    const KPBuffPair *retrieveInput(uint8_t key, uint64_t processCallID, uint32_t count) {
        InputSlot &slot = inputs[key];
        const KPBuffPair *ret = innerRetrieveInput(slot.upstream, processCallID, count);
        if (!ret) {
            ret = KPBuffPairHelper::zeroes();
        }
        return ret;
    }
    
    const KPBuffPair *innerRetrieveInput(Module *upstreamModule, uint64_t processCallID, uint32_t count) {
        if (!upstreamModule) { return nullptr; }
        if (upstreamModule->isInProcess) {
            KPBuffPairHelper::copyKPBuffPairContents(&loopOutput, &upstreamModule->output);
            return &loopOutput;;
        }
        const KPBuffPair *ret = nullptr;    // Dealing with loops
        upstreamModule->isInProcess = true;         // potentially
        if (upstreamModule->processProc) {
            if (isAlreadyProcessed(upstreamModule, processCallID)) {
                ret = &upstreamModule->output;
            } else {
                ret = (upstreamModule->processProc)(processCallID, count, upstreamModule);
                upstreamModule->lastProcessCallID = processCallID;
            }
        }
//        upstreamModule->loopOutputIsSet = false;
        upstreamModule->isInProcess = false;
        return ret;
    }
    		
	void removeInput(uint8_t key) {
		inputs[key].upstream = nullptr;
		inputs[key].buffer   = nullptr;
	}

	bool isInputConnected(size_t key) const {
		return inputs[key].buffer != nullptr;
	}

	void clearOutputs() {
		std::fill(std::begin(output.first->data), std::end(output.first->data), 0.0f);
	}
    
    inline bool isAlreadyProcessed(Module* module, uint64_t processCallID) {
        return module->lastProcessCallID == processCallID;
    }
	
	// Every module MUST call this, to keep processCallIDs in sync.
//	inline KPBuffPair *getCachedOutputIfAlreadyProcessed(Module* module, uint64_t processCallID) {
//        if (isAlreadyProcessed(module, processCallID)) {
//			return &module->output;
//		}
//		module->lastProcessCallID = processCallID;
//		return nullptr;
//	}
    
    void copyParamsFrom(const Module& sourceModule) {
        for (int i = 0; i < MODULE_MAX_PARAMS; ++i) {
            params[i] = sourceModule.params[i];
        }
    }
    AUValue getParameter(AUParameterAddress address)
    {
        uint8_t index = MODULE_ITEM_FROM_ADDRESS(address) - 1; // params start at ID = 1
        if (index < MODULE_MAX_PARAMS) {
            return params[index];
        }
        return 0;
    }

	AUParameterAddress id;
	ModuleParamKind paramKind;
	ModuleInstance instance;  // not sure that we need this
    
    static bool loggingEnabled;
    
    const ModuleParamConstantsGroup *constantsGroup;
	
	std::atomic<int> activeCount{0};

	// Flat input slot array. Index 0 is unused; valid keys are 1..MODULE_MAX_PARAMS.
	// All slots are zero-initialised by InputSlot's default constructor.
	// No heap allocation after init — direct array indexing, O(1).
	InputSlot inputs[MODULE_MAX_PARAMS + 1];

    KPBuffPair output = { nullptr, nullptr };
    KPBuffPair loopOutput = { nullptr, nullptr };
//    bool loopOutputIsSet = false; // Doesn't work

	ModuleProcessProc processProc = nullptr;
	ModuleMIDIProc midiProc = nullptr;
	ModuleDeinitProc deinitProc = nullptr;
	
	ModuleEventProc eventProc = nullptr;
	ModuleQueryProc queryProc = nullptr;
    
    ModuleParamProc paramProc = nullptr;
    
    ModuleResetProc resetProc = nullptr;
    
    ModuleSampleRateProc sampleRateProc = nullptr;

	void* state = nullptr; // Internal state, owned by module
	
	float params[MODULE_MAX_PARAMS];
	
	uint64_t lastProcessCallID = 0; // ID of last processed block
    bool isInProcess = false;
		   
    double mOmegaL = 0.0;            // 0 <= mOmega <= 1.0; mostly applies to oscilators
    double mOmegaR = 0.0;            // 0 <= mOmega <= 1.0; mostly applies to oscilators
    double mBaseDeltaOmegaL = 0.0;    // increment per sample to mOmega:  freq / sampleRate (mostly for osc)
    double mBaseDeltaOmegaR = 0.0;    // increment per sample to mOmega:  freq / sampleRate (mostly for osc)
	double mModDeltaOmega = 0.0;    // modulation changes. (mostly for osc)
	double mFrequencyScale = 0.0;	// we do need this -- 1/sampleRate. Div
    double mSampleRate = 44100.0;   // sometimes we need this too

    PitchConverter* pitchConverter = nullptr;

    // Cross-voice shared object for this placement, or null if the module type declares none.
    // Injected by Voice::initializeModules from the VoiceManager's registry (see ModuleSharedState.hpp).
    ModuleSharedState* sharedState = nullptr;

};

#endif /* Module_h */
