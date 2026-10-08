//
//  CVMixerModule.cpp — see header. Sum or multiply the enabled inputs (LFO, Env, CC).
//

#include "CVMixerModule.hpp"

#include <cstdint>

#include "pm_midi.h"

struct CVMixerState {
    float ccValue = 0.0f; // latest value of the selected CC, normalised 0..1
};

const void InitCVMixerModule(uint32_t /*count*/, Module* module)
{
    module->state = new CVMixerState();
}

const void DeinitCVMixerModule(Module* module)
{
    delete static_cast<CVMixerState*>(module->state);
    module->state = nullptr;
}

void MIDICVMixerModule(const pm::UmpMessage& message, Module* module)
{
    auto* st = static_cast<CVMixerState*>(module->state);
    if (!st) return;
    if (pm::isChannelVoice2(message) && message.status == pm::ump::Status::ControlChange) {
        const int ccNum = static_cast<int>(module->paramByEnum(CVMixerParamItemCCNumber));
        if (static_cast<int>(message.ccIndex) == ccNum)
            st->ccValue = static_cast<float>(message.ccData) / 4294967296.0f; // 32-bit → 0..1
    }
}

const KPBuffPair* ProcessCVMixerModule(uint64_t processCallID, uint32_t count, Module* module)
{
    auto* st = static_cast<CVMixerState*>(module->state);

    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    const bool enLFO = module->paramByEnum(CVMixerParamItemEnableLFO) > 0.5f;
    const bool enEnv = module->paramByEnum(CVMixerParamItemEnableEnv) > 0.5f;
    const bool enCC  = module->paramByEnum(CVMixerParamItemEnableCC) > 0.5f;
    const bool mult  = module->paramByEnum(CVMixerParamItemMode)     > 0.5f;

    // An input contributes only if it's enabled AND actually patched (avoids a stray unconnected
    // input zeroing a multiply).
    const bool lfoActive = enLFO && module->isInputConnected(CVMixerParamItemLFOInput);
    const bool envActive = enEnv && module->isInputConnected(CVMixerParamItemEnvInput);
    const float ccVal    = st ? st->ccValue : 0.0f;

    auto lfoPair = module->retrieveInput(CVMixerParamItemLFOInput, processCallID, count);
    auto envPair = module->retrieveInput(CVMixerParamItemEnvInput, processCallID, count);
    const float* lfo = lfoPair->first->data;
    const float* env = envPair->first->data;

    for (uint32_t i = 0; i < count; ++i) {
        float out;
        if (!mult) {
            out = 0.0f;
            if (lfoActive) out += lfo[i];
            if (envActive) out += env[i];
            if (enCC)      out += ccVal;
        } else {
            float prod = 1.0f;
            bool any = false;
            if (lfoActive) { prod *= lfo[i]; any = true; }
            if (envActive) { prod *= env[i]; any = true; }
            if (enCC)      { prod *= ccVal;  any = true; }
            out = any ? prod : 0.0f; // nothing enabled → no modulation, not identity
        }
        outL[i] = out;
        outR[i] = out;
    }

    return &module->output;
}
