//
//  FinalModule.mm
//
//  Created by Eric Kampman on 4/13/25.
//
#include <cmath>

#include "FinalModule.hpp"

struct FinalState {
    // DC blocker state
    float x1L = 0.f, y1L = 0.f;
    float x1R = 0.f, y1R = 0.f;

    // coefficient for 1st-order DC HPF (y[n] = x[n]-x[n-1] + R*y[n-1])
    float dcR = 0.999f;
    float fc  = 10.0f;     // cutoff in Hz
    double lastSR = 0.0;   // to detect SR changes

    // simple one-pole param smoothers. They start at the first block's values (primed), not at 1:
    // starting at 1 made every voice's first note play 1/Level louder (+3 dB at the default 0.7)
    // for the smoother's ~20 ms, so a voice's first note was louder than its later ones.
    float levelZ = 1.0f;
    float clipZ  = 1.0f;
    bool  primed = false;
    static constexpr float aLevel = 0.001f;
    static constexpr float aClip  = 0.001f;

    static inline float coeff(double sr, float cutoffHz) {
        if (sr <= 0.0) sr = 48000.0;
        return std::exp(-2.0 * M_PI * cutoffHz / sr);
    }

    inline void setSampleRate(double sr) {
        lastSR = sr;
        dcR = coeff(sr, fc);
    }

    inline float smoothLevel(float target) {
        levelZ += aLevel * (target - levelZ);
        return levelZ;
    }
    inline float smoothClip(float target) {
        clipZ  += aClip  * (target - clipZ);
        return clipZ;
    }
};

const void InitFinalModule(uint32_t /*count*/, Module* module)
{
    auto* st = new FinalState();
    st->setSampleRate(1.0 / module->mFrequencyScale);
    module->state = st;
}

const void DeinitFinalModule(Module* module)
{
    delete static_cast<FinalState*>(module->state);
    module->state = nullptr;
}

const void SampleRateFinalModule(Module* module, double newSampleRate)
{
    auto* st = static_cast<FinalState*>(module->state);
    if (!st) return;
    st->setSampleRate(newSampleRate);
}

const KPBuffPair* ProcessFinalModule(uint64_t processCallID, uint32_t count, Module* module) {

    module->clearOutputs();

    const KPBuffPair* inputBuffer = nullptr;
    Module* mp = nullptr;

    if (0 != module->retrieveInput(FinalOutputParamItemInput, &inputBuffer, processCallID, count, &mp))
        return &module->output;
    if (!inputBuffer || !inputBuffer->first || !inputBuffer->second)
        return &module->output;

    auto* st = static_cast<FinalState*>(module->state);
    if (!st) return &module->output;

    // Parameters (from module)
    const AUValue level     = module->paramByEnum(FinalOutputParamItemLevel);
    const AUValue spread    = module->paramByEnum(FinalOutputParamItemSpread);
    const AUValue clipLevel = module->paramByEnum(FinalOutputParamItemClipLevel);
    if (!st->primed) { st->levelZ = level; st->clipZ = clipLevel; st->primed = true; }
    const bool filterDC     = (module->paramByEnum(FinalOutputParamItemFilterDC) >= 0.5f);

    float* lp   = inputBuffer->first->data;
    float* rp   = inputBuffer->second->data;
    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    float* end = lp + count;

    while (lp < end) {
        const float lvl = st->smoothLevel(level);
        const float clip = st->smoothClip(clipLevel);

        float L = *lp++;
        float R = *rp++;

        // 1) DC block
        if (filterDC) {
            const float yL = (L - st->x1L) + st->dcR * st->y1L;
            const float yR = (R - st->x1R) + st->dcR * st->y1R;
            st->x1L = L; st->y1L = yL;
            st->x1R = R; st->y1R = yR;
            L = yL; R = yR;
        }

        // 2) Level
        L *= lvl;
        R *= lvl;

        // 3) Spread (M/S)
        {
            const float M = 0.5f * (L + R);
            float S = 0.5f * (L - R);
            S *= spread;
            L = M + S;
            R = M - S;
        }

        // 4) Clip (hard ceiling)
        const float hi = clip;
        const float lo = -clip;
        L = std::clamp(L, lo, hi);
        R = std::clamp(R, lo, hi);

        *outL++ = L;
        *outR++ = R;
    }

    return &module->output;
}

void MIDIFinalModule(const pm::UmpMessage& /*message*/, Module* /*module*/) {
}
