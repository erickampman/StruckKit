//
//  ModalBankModule.mm
//

#include "ModalBankModule.hpp"
#include "ModalBank.hpp"          // the framework-free core
#include "ModalGenerators.hpp"    // procedural mode series
#include "PitchConverter.hpp"

#include <algorithm>

// Module state is the core instance plus, for procedural materials, a buffer
// holding the generated mode table. That buffer is filled on demand and its
// pointer handed to the core; because the pointer is stable across generators,
// switching between two generators needs bank.invalidate() to force recompute.
struct ModalBankState {
    ModalBank     bank;
    ModalMode     genBuffer[ModalBank::kMaxModes];
    ModalMaterial genMaterial{ nullptr, 0, "Gen" };
    int           lastMaterial = -1;   // which material index genBuffer holds
};

// Piano B and Power k shaping parameters. No AU parameter is exposed for these
// yet, so the module uses the same sensible defaults ModeTests opens on.
static constexpr float kPianoB = 0.0003f;   // middle of the keyboard
static constexpr float kPowerK = 1.5f;      // closely matches a free-free bar

// Select the material for this block. Static tables come straight from
// kModalMaterials; generators are (re)filled into the state buffer only when
// the selected material actually changes, then the bank is invalidated so the
// stable buffer pointer is not mistaken for unchanged contents.
static void selectMaterial(Module* module, ModalBankState* st, int materialIdx)
{
    materialIdx = std::clamp(materialIdx, 0, (int)ModalBankMaterialCount - 1);

    if (materialIdx < (int)MODAL_BANK_FIRST_GENERATOR) {
        st->bank.setMaterial(kModalMaterials[materialIdx]);
        st->lastMaterial = materialIdx;
        return;
    }

    if (materialIdx != st->lastMaterial) {
        const ModalSeriesID series =
            (ModalSeriesID)(materialIdx - MODAL_BANK_FIRST_GENERATOR);
        const float param = (series == ModalSeriesPiano) ? kPianoB
                          : (series == ModalSeriesPower) ? kPowerK : 0.0f;
        const int n = generateModalSeries(series, param,
                                          ModalBank::kMaxModes,
                                          st->genBuffer, ModalBank::kMaxModes);
        st->genMaterial.modes = st->genBuffer;
        st->genMaterial.count = n;
        st->lastMaterial = materialIdx;

        // The buffer pointer handed to the core is stable across generators, so
        // a generator->generator switch would otherwise look unchanged. Force it
        // only here, on the block the material actually changed — not every
        // block, which would recompute all coefficients continuously.
        st->bank.setMaterial(st->genMaterial);
        st->bank.invalidate();
    } else {
        st->bank.setMaterial(st->genMaterial);   // no-op if already selected
    }
}

const void InitModalBankModule(uint32_t /*count*/, Module* module)
{
    auto* st = new ModalBankState();
    st->bank.setSampleRate(module->mSampleRate);
    module->state = st;
}

const void DeinitModalBankModule(Module* module)
{
    delete static_cast<ModalBankState*>(module->state);
    module->state = nullptr;
}

const void ResetModalBankModule(Module* module)
{
    // Voice allocation only — NOT note-on. Re-striking a ringing bank means
    // adding excitation to a system already in motion; resetting here would
    // click and destroy the ring-down. See ModalSynthesis.md.
    if (auto* st = static_cast<ModalBankState*>(module->state)) {
        st->bank.reset();
    }
}

const void SampleRateModalBankModule(Module* module, double newSampleRate)
{
    if (auto* st = static_cast<ModalBankState*>(module->state)) {
        st->bank.setSampleRate(newSampleRate);
    }
}

// Read a control-style input as a single per-block value. The mod ports here
// (pitch, decay, brightness, strike) all carry slow control signals, and the
// core recomputes coefficients per block regardless, so a per-sample read would
// buy nothing. Returns 0 when nothing is patched.
//
// The LAST sample, not the first. A new note's pitch reaches the bank through
// the Slew, which moves at most 0.21 octave a sample even at its fastest (its
// 0.1 ms-per-octave floor), so sample 0 still holds about the voice's previous
// pitch. Read there, the whole first block (the strike included) rang at the
// old note's pitch and retuned a block later: a pitch blip and a level change
// on every strike that changed note, and on a voice's first note (from 0
// octaves, A440). The last sample is where the Slew has arrived.
static inline float readControl(Module* module, uint8_t key,
                                uint64_t processCallID, uint32_t count)
{
    if (!module->isInputConnected(key) || count == 0) return 0.0f;
    const KPBuffPair* p = module->retrieveInput(key, processCallID, count);
    return p->first->data[count - 1];
}

const KPBuffPair* ProcessModalBankModule(uint64_t processCallID, uint32_t count, Module* module)
{
    auto* st = static_cast<ModalBankState*>(module->state);
    if (!st) return KPBuffPairHelper::zeroes();

    ModalBank& bank = st->bank;

    // --- pitch: PM octaves -> Hz -------------------------------------------
    // octaves = Pitch (transpose) + PitchModLevel * PitchInput (note tracking).
    // PitchModLevel defaults to 1 (unity tracking) and 0 gives a fixed-pitch
    // body resonator.
    const float pitchParam = module->getParamValue(ModalBankParamItemPitch);
    const float pitchLevel = module->getParamValue(ModalBankParamItemPitchModLevel);
    const float pitchIn    = readControl(module, ModalBankParamItemPitchInput, processCallID, count);
    const float octaves    = pitchParam + pitchLevel * pitchIn;
    bank.setFundamental((float)module->pitchConverter->pitchToFrequency(octaves));

    // --- object ------------------------------------------------------------
    selectMaterial(module, st, module->getParamValueAsInt(ModalBankParamItemMaterial));
    bank.setModeCount(module->getParamValueAsInt(ModalBankParamItemModeCount));
    // Stretch is an INDEX into the shared value table, not a raw float.
    bank.setStretch(kModalStretchValues[
        std::clamp(module->getParamValueAsInt(ModalBankParamItemStretch),
                   0, kModalStretchCount - 1)]);

    // --- decay, with optional modulation -----------------------------------
    const float decayMod = readControl(module, ModalBankParamItemDecayModInput, processCallID, count)
                         * module->getParamValue(ModalBankParamItemDecayModLevel);
    bank.setDecayT60(module->getParamValue(ModalBankParamItemDecayT60) + decayMod);
    bank.setDampTilt(module->getParamValue(ModalBankParamItemDampTilt));
    bank.setDampBellAmount(module->getParamValue(ModalBankParamItemDampBellAmount));
    bank.setDampBellCentre(module->getParamValue(ModalBankParamItemDampBellCentre));
    bank.setDampBellWidth(module->getParamValue(ModalBankParamItemDampBellWidth));

    // --- strike, with optional modulation ----------------------------------
    const float strikeMod = readControl(module, ModalBankParamItemStrikeInput, processCallID, count)
                          * module->getParamValue(ModalBankParamItemStrikeModLevel);
    bank.setStrikePosition(module->getParamValue(ModalBankParamItemStrikePosition) + strikeMod);

    const float brightMod = readControl(module, ModalBankParamItemBrightModInput, processCallID, count)
                          * module->getParamValue(ModalBankParamItemBrightModLevel);
    bank.setBrightness(module->getParamValue(ModalBankParamItemBrightness) + brightMod);

    // --- output ------------------------------------------------------------
    bank.setSpread(module->getParamValue(ModalBankParamItemSpread));

    // Excitation. If nothing is patched we still process with a null exciter so
    // a previously struck bank keeps ringing down — the common case. The core
    // treats null as "no input this block", not silence.
    const float* exc = nullptr;
    if (module->isInputConnected(ModalBankParamItemInput)) {
        const KPBuffPair* in = module->retrieveInput(ModalBankParamItemInput, processCallID, count);
        exc = in->first->data;   // left channel drives the (mono-in) bank
    }

    float* outL = module->output.first->data;
    float* outR = module->output.second->data;
    bank.processBlock(exc, outL, outR, (int)count);

    const float level = module->getParamValue(ModalBankParamItemLevel);
    if (level != 1.0f) {
        for (uint32_t i = 0; i < count; ++i) { outL[i] *= level; outR[i] *= level; }
    }

    return &module->output;
}
