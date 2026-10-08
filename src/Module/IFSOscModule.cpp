//
//  IFSOscModule.cpp — IFS fractal wavetable oscillator (IFSynth port of PM's IFSOscModule).
//
//  Differences from the AUv3 original:
//   - Trimmed to the simple v1 params (pitch/fine + the 5 fractal-shape controls + smoothing +
//     iterations). The whole modulation matrix (per-param mod inputs, phase mod) is deferred
//     until IFSynth has mod sources (LFO/Env) and a CV module, so the per-sample mod path is
//     gone — this is purely the wavetable + crossfade path.
//   - Apple GCD background render (dispatch_queue/source) is replaced by a single-threaded,
//     coalesced render at the top of the process block: a shape-param change flags the table
//     dirty; the next process() renders the inactive buffer once (~2048 samples, sub-0.1 ms),
//     swaps, and crossfades. No RT-heap allocation, no threads.
//

#include "IFSOscModule.hpp"
#include "IFSCore.hpp"
#include "PitchConverter.hpp"

#include <algorithm>
#include <cmath>

#include "pm_midi.h"

// Samples over which to crossfade when the wavetable changes.
static constexpr int IFS_CROSSFADE_SAMPLES = 256;

// ------------------------------------------------------------ State
struct IFSOscState {
    float wavetable[2][IFS_WAVETABLE_SIZE];
    int   activeIdx    = 0;   // process reads wavetable[activeIdx]
    bool  tableDirty   = true; // a shape param changed → re-render at next process
    int   crossfadePos = 0;    // counts down from IFS_CROSSFADE_SAMPLES

    IFSShapeParams pendingShape; // written by paramProc, consumed by the render
    IFSShapeParams activeShape;  // shape of the currently active table
    float prevSyncL = 0.0f, prevSyncR = 0.0f; // last sync-input sample (zero-crossing detection)
};

// Advance a phase by one sample; if hard sync is on and the sync input crosses zero (per mode),
// reset the phase sub-sample-accurately to the start of the IFS cycle. prevS is updated in place.
static inline void advanceSync(double& phase, double delta, bool syncOn, OscSyncType type,
                               float& prevS, float curS)
{
    phase += delta;
    if (phase >= 1.0) phase -= 1.0;
    if (syncOn)
    {
        bool reset = false;
        switch (type)
        {
        case OscSyncTypeDown: reset = (prevS > 0.0f) && (curS <= 0.0f); break;
        case OscSyncTypeUp:   reset = (prevS < 0.0f) && (curS >= 0.0f); break;
        case OscSyncTypeBoth: reset = ((prevS > 0.0f) && (curS <= 0.0f))
                                   || ((prevS < 0.0f) && (curS >= 0.0f)); break;
        }
        if (reset) { const float t = prevS / (prevS - curS); phase = (1.0 - t) * delta; }
        prevS = curS;
    }
}

// Snapshot the module's shape params into pendingShape.
static void applyShapeParams(IFSOscState* st, Module* module)
{
    IFSShapeParams& s = st->pendingShape;
    s.ifs1Squash = module->paramByEnum(IFSOscParamItemIFS1Squash);
    s.ifs2Squash = module->paramByEnum(IFSOscParamItemIFS2Squash);
    s.bx         = module->paramByEnum(IFSOscParamItemBx);
    s.by         = module->paramByEnum(IFSOscParamItemBy);
    s.startEndY  = module->paramByEnum(IFSOscParamItemStartEndY);
    s.smoothing  = module->paramByEnum(IFSOscParamItemSmoothing);
    // Parameter is 0-based (picker index): 0 → 3 iterations, 1 → 4 iterations.
    s.iterations = static_cast<int>(module->paramByEnum(IFSOscParamItemIterations)) + 3;
}

// Wavetable lookup with linear interpolation and phase wrapping. phase in [0, 1).
static inline float tableRead(const float* table, double phase)
{
    double pos  = phase * IFS_WAVETABLE_SIZE;
    int    idx  = static_cast<int>(pos);
    float  frac = static_cast<float>(pos - idx);
    int    idx1 = (idx + 1) & (IFS_WAVETABLE_SIZE - 1);
    return table[idx] + frac * (table[idx1] - table[idx]);
}

// ------------------------------------------------------------ Lifecycle
const void InitIFSOscModule(uint32_t /*count*/, Module* module)
{
    auto* st = new IFSOscState();

    // Render a valid default table now (params aren't set until addParams(), which runs after
    // this init). tableDirty forces the first process() to re-render from the real params.
    st->pendingShape = st->activeShape = IFSShapeParams{};
    computeIFSWavetable(st->activeShape, st->wavetable[0]);
    std::copy_n(st->wavetable[0], IFS_WAVETABLE_SIZE, st->wavetable[1]);
    st->activeIdx = 0;
    st->crossfadePos = 0;
    st->tableDirty = true;

    // Pitch infrastructure — same pattern as SinOsc.
    double hz = 440.0;
    module->mBaseDeltaOmegaL = hz * module->mFrequencyScale;
    module->mBaseDeltaOmegaR = hz * module->mFrequencyScale;
    module->mOmegaL = 0.0;
    module->mOmegaR = 0.0;

    // The PitchConverter is shared: the voice assigns its global one (SinOsc does the same).
    // This module must not own or free it.
    module->state = st;
}

const void DeinitIFSOscModule(Module* module)
{
    auto* st = static_cast<IFSOscState*>(module->state);
    if (!st) return;
    // Do not touch module->pitchConverter — it's shared, owned by the voice, not this module.
    delete st;
    module->state = nullptr;
}

const void ResetIFSOscModule(Module* module)
{
    module->mOmegaL = 0.0;
    module->mOmegaR = 0.0;
    if (module->state) {
        auto* st = static_cast<IFSOscState*>(module->state);
        st->prevSyncL = st->prevSyncR = 0.0f;
    }
}

const void SampleRateIFSOscModule(Module* /*module*/, double /*newSampleRate*/)
{
    // mFrequencyScale is updated by Module::updateSampleRate() beforehand; the process loop
    // recomputes delta from pitch every sample, and the wavetable is SR-independent. No-op.
}

// ------------------------------------------------------------ Parameter change
const void ParamIFSOscModule(Module* module, AUParameterAddress address, AUValue /*value*/)
{
    auto* st = static_cast<IFSOscState*>(module->state);
    if (!st) return;

    switch (MODULE_ITEM_FROM_ADDRESS(address)) {
        case IFSOscParamItemIFS1Squash:
        case IFSOscParamItemIFS2Squash:
        case IFSOscParamItemBx:
        case IFSOscParamItemBy:
        case IFSOscParamItemStartEndY:
        case IFSOscParamItemSmoothing:
        case IFSOscParamItemIterations:
            // Snapshot the new shape; the next process() renders it (coalesces rapid changes).
            applyShapeParams(st, module);
            st->tableDirty = true;
            break;
        default:
            // Pitch / fine — used directly in the process loop.
            break;
    }
}

// ------------------------------------------------------------ MIDI
void MIDIIFSOscModule(const pm::UmpMessage& message, Module* module)
{
    // If pitch is driven by a connected input, note messages don't set frequency.
    if (module->isInputConnected(IFSOscParamItemPitchInput)) return;

    if (message.status == pm::ump::Status::NoteOn && message.velocity > 0) {
        double freq = 0.0;
        if (message.attributeType == pm::ump::NoteAttribute::Pitch7_9) {
            uint16_t attr     = message.attribute;
            uint8_t  note     = (attr & 0xFE00) >> 9;
            uint16_t fraction = attr & 0x01FF;
            if (fraction >= 512) fraction = 511;
            freq = pm::noteToFrequency(note) * pm::pitchFractionRatio(fraction);
        } else {
            freq = pm::noteToFrequency(message.note);
        }
        module->mBaseDeltaOmegaL = freq * module->mFrequencyScale;
        module->mBaseDeltaOmegaR = freq * module->mFrequencyScale;
        module->mOmegaL = 0.0;
        module->mOmegaR = 0.0;
    }
}

// Read the module's base shape from its params (no modulation applied).
static IFSShapeParams baseShapeFromParams(Module* module)
{
    IFSShapeParams s;
    s.ifs1Squash = module->paramByEnum(IFSOscParamItemIFS1Squash);
    s.ifs2Squash = module->paramByEnum(IFSOscParamItemIFS2Squash);
    s.bx         = module->paramByEnum(IFSOscParamItemBx);
    s.by         = module->paramByEnum(IFSOscParamItemBy);
    s.startEndY  = module->paramByEnum(IFSOscParamItemStartEndY);
    s.smoothing  = module->paramByEnum(IFSOscParamItemSmoothing);
    s.iterations = static_cast<int>(module->paramByEnum(IFSOscParamItemIterations)) + 3;
    return s;
}

// ------------------------------------------------------------ Process
const KPBuffPair* ProcessIFSOscModule(uint64_t processCallID, uint32_t count, Module* module)
{
    auto* st = static_cast<IFSOscState*>(module->state);

    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    const AUValue freqParam = module->paramByEnum(IFSOscParamItemPitch)     * (float)SEMITONES_TO_PITCH;
    const AUValue fineParam = module->paramByEnum(IFSOscParamItemPitchFine) * (float)CENTS_TO_PITCH;
    // Stereo detune (cents → octaves), split symmetrically: L is flattened, R is sharpened by
    // half. A constant-cents split gives even width across the keyboard (beats speed up on high
    // notes — the classic detune behaviour).
    const float   detuneHalf = 0.5f * module->paramByEnum(IFSOscParamItemDetune) * (float)CENTS_TO_PITCH;
    const double  freqScale = module->mFrequencyScale;
    auto* pitchConverter    = module->pitchConverter;

    double& phaseL = module->mOmegaL;
    double& phaseR = module->mOmegaR;
    double& deltaL = module->mBaseDeltaOmegaL;
    double& deltaR = module->mBaseDeltaOmegaR;

    auto pitchInputPair = module->retrieveInput(IFSOscParamItemPitchInput, processCallID, count);
    const float* pilPtr = pitchInputPair->first->data;
    const float* pirPtr = pitchInputPair->second->data;

    // Pitch mod (per-sample; wavetable-compatible).
    auto pitchModPair = module->retrieveInput(IFSOscParamItemPitchModInput, processCallID, count);
    const float* pmlPtr = pitchModPair->first->data;
    const float* pmrPtr = pitchModPair->second->data;
    const float pitchModLevel = module->paramByEnum(IFSOscParamItemPitchModLevel);

    // Hard sync (per-channel): the IFS osc is the slave; on a zero crossing of the sync input
    // (the sine master, stereo) the corresponding channel's phase resets, so the L/R detune of
    // the sine drives the L/R sync rate and the stereo width survives. Output mix level too.
    const int  syncMode = static_cast<int>(module->paramByEnum(IFSOscParamItemSyncMode));
    const bool syncOn   = syncMode >= 1 && module->isInputConnected(IFSOscParamItemSyncInput);
    const OscSyncType syncType = syncMode == 1 ? OscSyncTypeUp
                               : syncMode == 2 ? OscSyncTypeDown : OscSyncTypeBoth;
    auto syncPair = module->retrieveInput(IFSOscParamItemSyncInput, processCallID, count);
    const float* syncLPtr = syncPair->first->data;
    const float* syncRPtr = syncPair->second->data;
    const float  levelParam = module->paramByEnum(IFSOscParamItemLevel);

    // Shape mods (per-chunk; switch to the eval path if any is connected).
    const bool bxConn = module->isInputConnected(IFSOscParamItemBxModInput);
    const bool byConn = module->isInputConnected(IFSOscParamItemByModInput);
    const bool s1Conn = module->isInputConnected(IFSOscParamItemSquash1ModInput);
    const bool s2Conn = module->isInputConnected(IFSOscParamItemSquash2ModInput);
    const bool anyShapeMod = bxConn || byConn || s1Conn || s2Conn;

    if (!anyShapeMod) {
        // ---- Wavetable path (coalesced render; cheap) ----
        if (st->tableDirty) {
            st->tableDirty = false;
            applyShapeParams(st, module);
            const int inactive = 1 - st->activeIdx;
            computeIFSWavetable(st->pendingShape, st->wavetable[inactive]);
            st->activeIdx   = inactive;
            st->activeShape = st->pendingShape;
            st->crossfadePos = IFS_CROSSFADE_SAMPLES;
        }
        const int    activeIdx = st->activeIdx;
        const float* activeTbl = st->wavetable[activeIdx];
        const float* prevTbl   = st->wavetable[1 - activeIdx];
        bool crossfading       = st->crossfadePos > 0;

        for (uint32_t i = 0; i < count; ++i) {
            deltaL = pitchConverter->pitchToFrequency(pilPtr[i] + freqParam + fineParam - detuneHalf + pmlPtr[i] * pitchModLevel) * freqScale;
            deltaR = pitchConverter->pitchToFrequency(pirPtr[i] + freqParam + fineParam + detuneHalf + pmrPtr[i] * pitchModLevel) * freqScale;
            float sampleL = tableRead(activeTbl, phaseL);
            float sampleR = tableRead(activeTbl, phaseR);
            if (crossfading) {
                float blend = static_cast<float>(st->crossfadePos) / IFS_CROSSFADE_SAMPLES;
                sampleL += blend * (tableRead(prevTbl, phaseL) - sampleL);
                sampleR += blend * (tableRead(prevTbl, phaseR) - sampleR);
                --st->crossfadePos;
                crossfading = st->crossfadePos > 0;
            }
            outL[i] = sampleL * levelParam; outR[i] = sampleR * levelParam;
            advanceSync(phaseL, deltaL, syncOn, syncType, st->prevSyncL, syncLPtr[i]);
            advanceSync(phaseR, deltaR, syncOn, syncType, st->prevSyncR, syncRPtr[i]);
        }
        return &module->output;
    }

    // ---- Chunked modulated-eval path ----
    // Shape params are modulated at control rate (32-sample chunks): recompute the smoothing
    // vertices once per chunk, then evaluate smoothed per sample. Keeps `smoothing` working.
    const IFSShapeParams base = baseShapeFromParams(module);
    const float bxLevel = module->paramByEnum(IFSOscParamItemBxModLevel);
    const float byLevel = module->paramByEnum(IFSOscParamItemByModLevel);
    const float s1Level = module->paramByEnum(IFSOscParamItemSquash1ModLevel);
    const float s2Level = module->paramByEnum(IFSOscParamItemSquash2ModLevel);
    const float* bxM = module->retrieveInput(IFSOscParamItemBxModInput,      processCallID, count)->first->data;
    const float* byM = module->retrieveInput(IFSOscParamItemByModInput,      processCallID, count)->first->data;
    const float* s1M = module->retrieveInput(IFSOscParamItemSquash1ModInput, processCallID, count)->first->data;
    const float* s2M = module->retrieveInput(IFSOscParamItemSquash2ModInput, processCallID, count)->first->data;

    constexpr uint32_t CHUNK = 32;
    IFSVertices verts;
    for (uint32_t i0 = 0; i0 < count; i0 += CHUNK) {
        const uint32_t i1 = std::min(i0 + CHUNK, count);
        IFSShapeParams ms = base; // modulated shape for this chunk (sampled at its first frame)
        if (bxConn) ms.bx         = std::clamp(base.bx         + bxM[i0] * bxLevel, -0.98f, 0.98f);
        if (byConn) ms.by         = std::clamp(base.by         + byM[i0] * byLevel, -1.00f, 1.00f);
        if (s1Conn) ms.ifs1Squash = std::clamp(base.ifs1Squash + s1M[i0] * s1Level, -0.99f, 0.99f);
        if (s2Conn) ms.ifs2Squash = std::clamp(base.ifs2Squash + s2M[i0] * s2Level, -0.99f, 0.99f);
        precomputeIFSVertices(ms, verts);

        for (uint32_t j = i0; j < i1; ++j) {
            deltaL = pitchConverter->pitchToFrequency(pilPtr[j] + freqParam + fineParam - detuneHalf + pmlPtr[j] * pitchModLevel) * freqScale;
            deltaR = pitchConverter->pitchToFrequency(pirPtr[j] + freqParam + fineParam + detuneHalf + pmrPtr[j] * pitchModLevel) * freqScale;
            outL[j] = evaluateIFSSmoothed(static_cast<float>(phaseL), verts) * levelParam;
            outR[j] = evaluateIFSSmoothed(static_cast<float>(phaseR), verts) * levelParam;
            advanceSync(phaseL, deltaL, syncOn, syncType, st->prevSyncL, syncLPtr[j]);
            advanceSync(phaseR, deltaR, syncOn, syncType, st->prevSyncR, syncRPtr[j]);
        }
    }
    // Force a fresh wavetable render when we next fall back to the non-modulated path.
    st->tableDirty = true;
    return &module->output;
}
