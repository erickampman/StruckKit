//
//  SinOscillatorModule.mm
//
//  Created by Eric Kampman on 4/11/25.
//

#include "Module.hpp"
#include <cmath>
#include <numbers>

#include "pm_midi.h"
#include "PitchConverter.hpp"
#include "SinOscillatorModule.hpp"


#define MOD_PHASE_ATTENUATOR    0.5

static constexpr double TWO_PI = 2.0 * std::numbers::pi_v<double>;

// Wrap a phase to [0, 1).
static inline double wrapPhase(double x) { return x - std::floor(x); }

// PolyBLEP residual for an amplitude-step discontinuity (used by the sawtooth wrap).
// t = phase in [0,1), dt = phase increment per sample.
static inline double polyBlep(double t, double dt) {
    if (t < dt)            { t /= dt;            return t + t - t * t - 1.0; }
    else if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

// PolyBLAMP residual for a slope (first-derivative) discontinuity (used by the triangle corners).
static inline double polyBlamp(double t, double dt) {
    if (t < dt)            { t = t / dt - 1.0;       return -1.0 / 3.0 * t * t * t; }
    else if (t > 1.0 - dt) { t = (t - 1.0) / dt + 1.0; return  1.0 / 3.0 * t * t * t; }
    return 0.0;
}

// Naive (non-band-limited) waveform value at normalized phase ph in [0,1). Used for the
// hard-sync amplitude step, where we want the ideal discontinuity magnitude.
static inline float naiveWave(int wf, double ph) {
    switch (wf) {
        case OscWaveformTriangle: return (float)(1.0 - 4.0 * std::fabs(ph - 0.5));
        case OscWaveformSaw:      return (float)(2.0 * ph - 1.0);
        default:                  return (float)std::sin(ph * TWO_PI);
    }
}

// Band-limited waveform value at phase ph in [0,1) with phase increment dt.
static inline float blWave(int wf, double ph, double dt) {
    switch (wf) {
        case OscWaveformTriangle: {
            // Naive triangle: trough (-1) at ph=0, peak (+1) at ph=0.5. Slope flips by +8 per
            // unit phase at the trough and -8 at the peak; PolyBLAMP rounds both corners.
            double v = 1.0 - 4.0 * std::fabs(ph - 0.5);
            double p2 = ph + 0.5; if (p2 >= 1.0) p2 -= 1.0;
            v +=  8.0 * dt * polyBlamp(ph, dt);
            v += -8.0 * dt * polyBlamp(p2, dt);
            return (float)v;
        }
        case OscWaveformSaw:
            return (float)((2.0 * ph - 1.0) - polyBlep(ph, dt));
        default:
            return (float)std::sin(ph * TWO_PI);
    }
}

/// Previous sync-input samples, needed for zero-crossing detection across blocks.
/// Allocated in InitSinOscillator, freed in DeinitSinOscillator, zeroed in ResetSinOscillator.
struct SinOscSyncState {
    float prevSyncL     = 0.0f;
    float prevSyncR     = 0.0f;
    float blepResidualL = 0.0f;  ///< Carry-over BLEP correction for the next sample, left
    float blepResidualR = 0.0f;  ///< Carry-over BLEP correction for the next sample, right
};

// Init: set default frequency (A440) via deltaOmega
const void InitSinOscillator(uint32_t /*count*/, Module* module) {
    LOG_MODULE("InitSinOscillator entrypoint", module->id);
	double hz = 440.0;
    module->mBaseDeltaOmegaL = hz * module->mFrequencyScale; // hz / sampleRate
    module->mOmegaL = 0.0;
    module->mBaseDeltaOmegaR = hz * module->mFrequencyScale; // hz / sampleRate
    module->mOmegaR = 0.0;
    module->state = new SinOscSyncState();
}

// Free the sync state allocated in Init.
const void DeinitSinOscillator(Module* module) {
    delete static_cast<SinOscSyncState*>(module->state);
    module->state = nullptr;
}

// The infrastructure has already rescaled mBaseDeltaOmega by the SR ratio before
// calling this proc, and the process loop recalculates deltaL/R from pitch params
// every sample. No additional work needed here.
const void SampleRateSinOscillator(Module* /*module*/, double /*newSampleRate*/)
{
}

// Reset previous sync samples so the first crossing after a voice reset is not spurious.
const void ResetSinOscillator(Module* module) {
    if (module->state) {
        auto* s = static_cast<SinOscSyncState*>(module->state);
        s->prevSyncL     = 0.0f;
        s->prevSyncR     = 0.0f;
        s->blepResidualL = 0.0f;
        s->blepResidualR = 0.0f;
    }
}

void MIDISinOscillator(const pm::UmpMessage& message, Module* module) {
	// Check for Note On and non-zero velocity

    // if pitchInput then the input parameter is controlling the frequency, not noteOn/noteOFf messages.
    if (!module->isInputConnected(SinOscParamItemPitchInput)) {
        if (message.status == pm::ump::Status::NoteOn && message.velocity > 0) {
            double freq = 0.0;

            if (message.attributeType == pm::ump::NoteAttribute::Pitch7_9) { // MIDI 2.0 NoteOn with pitch attribute
                LOG_MODULE("MIDISinOscillator Note On with pitch attribute", module->id);
                uint16_t attr = message.attribute;
                uint8_t note = (attr & 0xFE00) >> 9;
                uint16_t fraction = attr & 0x01FF;

                if (fraction >= 512) fraction = 511; // Clamp for safety

                double baseFreq = pm::noteToFrequency(note);
                freq = baseFreq * pm::pitchFractionRatio(fraction);
            }
            else {
                // Standard 7-bit MIDI 1.0 note number
                LOG_MODULE("MIDISinOscillator Note On Standard 7-bit MIDI 1.0 note number", module->id);
                freq = pm::noteToFrequency(message.note);
            }

            module->mBaseDeltaOmegaL = freq * module->mFrequencyScale;
            module->mBaseDeltaOmegaR = freq * module->mFrequencyScale;
            module->mOmegaL = 0.0;
            module->mOmegaR = 0.0;
        }
    }
}

// Process block: fills module->output buffers
const KPBuffPair* ProcessSinOscillator(uint64_t processCallID, uint32_t count, Module* module) {
//    LOG_MODULE("ProcessSinOscillator entrypoint", module->id);
    float* outL = module->output.first->data;
    float* outR = module->output.second->data;
    double freqScale = module->mFrequencyScale;

    // returns either a real buffer pair or a const ptr to an all zeroes buffer pair.
    // if the latter it will produce A440, since that's what A440's pitch value is.
    auto inputPair     = module->retrieveInput(SinOscParamItemPitchInput, processCallID, count);
    auto modPitchPair  = module->retrieveInput(SinOscParamItemModPitch,   processCallID, count);
    auto modPitch2Pair = module->retrieveInput(SinOscParamItemModPitch2,  processCallID, count);
    auto modPhasePair  = module->retrieveInput(SinOscParamItemModPhase,   processCallID, count);
    auto modSyncPair   = module->retrieveInput(SinOscParamItemModSync,    processCallID, count);

    AUValue freqParam      = module->paramByEnum(SinOscParamItemPitch);
    AUValue fineParam      = module->paramByEnum(SinOscParamItemPitchFine);
    AUValue fmLevelParam   = module->paramByEnum(SinOscParamItemFreqModLevel);
    AUValue fmLevel2Param  = module->paramByEnum(SinOscParamItemFreqModLevel2);
    AUValue pmLevelParam   = module->paramByEnum(SinOscParamItemPhaseModLevel);

    // Freq is in semitones. Pitch is in 1 per octave. Convert.
    freqParam *= SEMITONES_TO_PITCH;
    // fine is in cents. Pitch is in 1 per octave. Convert.
    fineParam *= CENTS_TO_PITCH;
    // Stereo detune (cents → octaves), split ±half: L flattened, R sharpened. Keeps the sine's
    // L/R apart so it can drive per-channel hard sync (and preserves the detune shimmer).
    const AUValue detuneHalf = 0.5f * module->paramByEnum(SinOscParamItemDetune) * (float)CENTS_TO_PITCH;
    // NB: the sine outputs at unity — its audible mix level is applied downstream by a Level
    // module, so the (raw) sine still drives the IFS hard-sync input when its mix level is 0.

    // Sync configuration — read once per block.
    bool hasSyncInput = module->isInputConnected(SinOscParamItemModSync);
    OscSyncType syncType = static_cast<OscSyncType>((int)module->paramByEnum(SinOscParamItemModSyncType));

    // Waveform: 0 = Sine, 1 = Triangle, 2 = Saw (band-limited). Read once per block.
    int waveform = (int)(module->paramByEnum(SinOscParamItemWaveform) + 0.5f);

    auto pitchConverter = module->pitchConverter;
    float *ilptr, *irptr;
    float *opl;

    float *mlptr, *mrptr;
    float *m2lptr, *m2rptr;
    float *nlptr, *nrptr;
    float *slptr, *srptr;   // sync input pointers

    double& phaseL = module->mOmegaL;
    double& phaseR = module->mOmegaR;
    double& deltaL = module->mBaseDeltaOmegaL;
    double& deltaR = module->mBaseDeltaOmegaR;

    auto* syncState = static_cast<SinOscSyncState*>(module->state);
    float& prevSyncL     = syncState->prevSyncL;
    float& prevSyncR     = syncState->prevSyncR;
    float& blepResidualL = syncState->blepResidualL;
    float& blepResidualR = syncState->blepResidualR;

    for (ilptr  = inputPair->first->data,    opl   = ilptr + count,
         irptr  = inputPair->second->data,
         mlptr  = modPitchPair->first->data,   mrptr  = modPitchPair->second->data,
         m2lptr = modPitch2Pair->first->data,  m2rptr = modPitch2Pair->second->data,
         nlptr  = modPhasePair->first->data,   nrptr  = modPhasePair->second->data,
         slptr  = modSyncPair->first->data,    srptr  = modSyncPair->second->data;
         ilptr < opl;
         ++ilptr, ++irptr, ++mlptr, ++mrptr, ++m2lptr, ++m2rptr,
         ++nlptr, ++nrptr, ++slptr, ++srptr, ++outL, ++outR)
    {
        deltaL = pitchConverter->pitchToFrequency(*ilptr + freqParam + fineParam - detuneHalf + *mlptr * fmLevelParam + *m2lptr * fmLevel2Param) * freqScale;
        deltaR = pitchConverter->pitchToFrequency(*irptr + freqParam + fineParam + detuneHalf + *mrptr * fmLevelParam + *m2rptr * fmLevel2Param) * freqScale;

        // Raw (band-limited) waveform value before any sync BLEP residual is applied.
        const double pmL = *nlptr * MOD_PHASE_ATTENUATOR * pmLevelParam;
        const double pmR = *nrptr * MOD_PHASE_ATTENUATOR * pmLevelParam;
        const double phpL = wrapPhase(phaseL + pmL);
        const double phpR = wrapPhase(phaseR + pmR);
        float rawSampleL = blWave(waveform, phpL, deltaL);
        float rawSampleR = blWave(waveform, phpR, deltaR);

        // Apply carry-over BLEP residual from the previous sync reset, then clear it.
        float sampleL = rawSampleL + blepResidualL;
        float sampleR = rawSampleR + blepResidualR;
        blepResidualL = 0.0f;
        blepResidualR = 0.0f;

        phaseL += deltaL;
        if (phaseL >= 1.0) phaseL -= 1.0;
        phaseR += deltaR;
        if (phaseR >= 1.0) phaseR -= 1.0;

        if (hasSyncInput) {
            // Hard sync: reset phase on zero crossings of the sync input.
            // The natural wrap above still runs every cycle, so the oscillator
            // continues at its own pitch when sync fires less often than the
            // natural period. When sync fires more often, the natural wrap is
            // preempted — which is correct classic hard-sync behaviour.
            float sL = *slptr;
            float sR = *srptr;
            bool syncResetL = false, syncResetR = false;

            switch (syncType) {
                case OscSyncTypeDown:
                    syncResetL = (prevSyncL > 0.0f) && (sL <= 0.0f);
                    syncResetR = (prevSyncR > 0.0f) && (sR <= 0.0f);
                    break;
                case OscSyncTypeUp:
                    syncResetL = (prevSyncL < 0.0f) && (sL >= 0.0f);
                    syncResetR = (prevSyncR < 0.0f) && (sR >= 0.0f);
                    break;
                case OscSyncTypeBoth:
                    syncResetL = ((prevSyncL > 0.0f) && (sL <= 0.0f)) || ((prevSyncL < 0.0f) && (sL >= 0.0f));
                    syncResetR = ((prevSyncR > 0.0f) && (sR <= 0.0f)) || ((prevSyncR < 0.0f) && (sR >= 0.0f));
                    break;
            }

            if (syncResetL) {
                float tL = prevSyncL / (prevSyncL - sL);
                phaseL = (1.0 - tL) * deltaL;
                // PolyBLEP: 2-tap correction for the amplitude step at the sync point.
                // step = (waveform value after reset) - (waveform value before reset), using the
                // naive waveform so the step is the ideal discontinuity for any waveform.
                // Current sample gets a fraction of the step; the remainder is carried
                // to the next sample via blepResidualL.
                float stepL   = naiveWave(waveform, wrapPhase(phaseL + pmL)) - naiveWave(waveform, phpL);
                float omtL    = 1.0f - tL;
                sampleL      += omtL * omtL * 0.5f * stepL;
                blepResidualL = -tL * tL * 0.5f * stepL;
            }
            if (syncResetR) {
                float tR = prevSyncR / (prevSyncR - sR);
                phaseR = (1.0 - tR) * deltaR;
                float stepR   = naiveWave(waveform, wrapPhase(phaseR + pmR)) - naiveWave(waveform, phpR);
                float omtR    = 1.0f - tR;
                sampleR      += omtR * omtR * 0.5f * stepR;
                blepResidualR = -tR * tR * 0.5f * stepR;
            }

            prevSyncL = sL;
            prevSyncR = sR;
        }

        *outL = sampleL;
        *outR = sampleR;
    }

    return &module->output;
}
