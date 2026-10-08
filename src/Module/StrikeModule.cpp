//
//  StrikeModule.mm
//

#include "StrikeModule.hpp"
#include <cmath>
#include <algorithm>
#include <cstdint>

// Longest possible contact, in seconds, at Hardness = 1, in WALL-CLOCK mode. A
// single-sample impulse (Hardness 0) is the hardest, brightest strike —
// identical to a Delta Impulse. Softening lengthens the contact, which rolls off
// the high modes, exactly as a softer mallet does physically.
static constexpr double kMaxContactSeconds = 0.020;

// NOTE-RELATIVE mode: the softest strike (Hardness 1) spans this many PERIODS of
// the played note, so the burst's shape relative to the fundamental is identical
// at every pitch — even level and timbre across the keyboard. Kept below 2 so
// the fundamental stays inside the excitation's main lobe (first null at
// 2*f0/periods) even at maximum softness; larger would start to null it.
static constexpr double kMaxContactPeriods = 1.6;

// Absolute ceiling on note-relative contact, so a very low note cannot demand an
// absurdly long burst as f0 -> 0. Generous; only bites below ~25 Hz.
static constexpr double kMaxNoteRelSeconds = 0.10;

// Equal-tempered MIDI note number -> Hz. Used only to size the contact window,
// so the exact reference tuning is immaterial.
static inline double midiNoteHz(float note) {
    return 440.0 * std::exp2(((double)note - 69.0) / 12.0);
}

struct StrikeState {
    double   sampleRate = 44100.0;

    // Active burst. pos == -1 means idle.
    int      pos      = -1;
    int      burstLen = 1;
    float    amp      = 0.0f;
    float    scale    = 0.0f;   // per-sample gain: amp / window-norm (see trigger)
    int      type     = 0;      // 0 = Mallet (bump), 1 = Noise burst

    // Pending MIDI trigger, consumed at the top of the next process block.
    bool     pendingTrigger = false;
    float    pendingVel     = 1.0f;
    float    pendingNote    = 60.0f;   // MIDI note number of the pending trigger

    // Gate-input edge detection and noise.
    float    prevGate = 0.0f;
    uint32_t rng      = 0x9E3779B9u;
};

static inline float clampf(float v, float lo, float hi) {
    return std::fmax(lo, std::fmin(hi, v));
}

// Bipolar white noise in [-1, 1).
static inline float lcgBipolar(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return (float)((int32_t)(s >> 8) - 8388608) / 8388608.0f;
}

const void InitStrikeModule(uint32_t /*count*/, Module* module)
{
    auto* st = new StrikeState();
    st->sampleRate = module->mSampleRate;
    module->state = st;
}

const void DeinitStrikeModule(Module* module)
{
    delete static_cast<StrikeState*>(module->state);
    module->state = nullptr;
}

const void ResetStrikeModule(Module* module)
{
    if (auto* st = static_cast<StrikeState*>(module->state)) {
        st->pos = -1;
        st->pendingTrigger = false;
        st->prevGate = 0.0f;
    }
}

const void SampleRateStrikeModule(Module* module, double newSampleRate)
{
    if (auto* st = static_cast<StrikeState*>(module->state)) {
        st->sampleRate = newSampleRate;
    }
}

// Note-on schedules a trigger; the burst itself is started in the process block
// so it can read the current Hardness (including its mod input). Note-off does
// nothing — a strike does not sustain.
void MIDIStrikeModule(const pm::UmpMessage& message, Module* module)
{
    auto* st = static_cast<StrikeState*>(module->state);
    if (!st) return;
    if (message.status == pm::ump::Status::NoteOn && message.velocity > 0) {
        st->pendingVel     = (float)message.velocity / 65535.0f;   // MIDI 2.0 16-bit
        st->pendingNote    = (float)message.note;                  // sizes note-relative contact
        st->pendingTrigger = true;
    }
}

const KPBuffPair* ProcessStrikeModule(uint64_t processCallID, uint32_t count, Module* module)
{
    auto* st = static_cast<StrikeState*>(module->state);
    if (!st) return KPBuffPairHelper::zeroes();

    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    const int   type       = module->getParamValueAsInt(StrikeParamItemType);
    const float baseHard   = clampf(module->getParamValue(StrikeParamItemHardness), 0.0f, 1.0f);
    const float velToLevel = clampf(module->getParamValue(StrikeParamItemVelToLevel), 0.0f, 1.0f);
    const float velToHard  = clampf(module->getParamValue(StrikeParamItemVelToHardness), 0.0f, 1.0f);
    const float level      = module->getParamValue(StrikeParamItemLevel);
    const float hardModLvl = module->getParamValue(StrikeParamItemHardnessModLevel);

    const bool  gateConnected = module->isInputConnected(StrikeParamItemGateInput);
    const float* gate = gateConnected
        ? module->retrieveInput(StrikeParamItemGateInput, processCallID, count)->first->data
        : nullptr;

    // Hardness modulation is block-rate (read once), which is plenty for a
    // mod-wheel-style "mallet hardness" control.
    float hardMod = 0.0f;
    if (module->isInputConnected(StrikeParamItemHardnessInput)) {
        hardMod = module->retrieveInput(StrikeParamItemHardnessInput, processCallID, count)->first->data[0]
                * hardModLvl;
    }

    const int  contactMode = module->getParamValueAsInt(StrikeParamItemContactMode);
    const int  maxLen      = std::max(1, (int)(kMaxContactSeconds * st->sampleRate));
    const int  noteRelCap  = std::max(1, (int)(kMaxNoteRelSeconds * st->sampleRate));

    // Start (or restart) a burst at velNorm strength. noteHz sizes note-relative
    // contact; pass <= 0 (no pitch, e.g. a Gate trigger) to force wall-clock.
    auto trigger = [&](float velNorm, float noteHz) {
        // Harder hit -> less hardness -> shorter, brighter contact.
        const float effHard = clampf(baseHard * (1.0f - velNorm * velToHard) + hardMod, 0.0f, 1.0f);

        int len;
        if (contactMode == StrikeContactNoteRelative && noteHz > 0.0f) {
            // Contact = a fixed number of periods of the note, so every pitch
            // sits at the same point on the excitation's roll-off: no low-note
            // overload, consistent timbre across the keyboard.
            const double periods = (double)effHard * kMaxContactPeriods;
            len = std::min((int)std::lround(periods * st->sampleRate / (double)noteHz), noteRelCap);
        } else {
            // Fixed contact time, like a real mallet: louder/boomier low,
            // brighter/pingier high.
            len = (int)std::lround((double)effHard * (double)maxLen);
        }
        st->burstLen = std::max(1, len);

        // velToLevel = 0 -> fixed full amplitude; 1 -> fully velocity-scaled.
        const float velScale = (1.0f - velToLevel) + velToLevel * velNorm;
        st->amp  = level * velScale;
        st->type = type;

        // Decouple loudness from contact length so Hardness is a timbre control,
        // not a volume control (the soft-mallet overload that hard-clipped the
        // resonator). The mallet bump is normalised to constant impulse strength
        // (window area, the low-frequency drive that the bank integrates); the
        // noise burst is normalised to constant energy. Both reduce to `amp` at
        // length 1, so the single-sample impulse is unchanged.
        const double area = (st->burstLen <= 1) ? 1.0 : 0.5 * (double)st->burstLen;
        st->scale = (st->type == StrikeTypeNoise)
            ? (float)((double)st->amp / std::sqrt((double)st->burstLen))
            : (float)((double)st->amp / area);

        st->pos  = 0;
    };

    // Consume a pending MIDI trigger — but only when no Gate is patched, so an
    // external trigger source has exclusive control.
    if (st->pendingTrigger) {
        if (!gateConnected) trigger(st->pendingVel, midiNoteHz(st->pendingNote));
        st->pendingTrigger = false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (gateConnected) {
            const float g = gate[i];
            if (g >= 0.5f && st->prevGate < 0.5f) {
                // Gate carries no pitch, so contact falls back to wall-clock.
                trigger(clampf(g, 0.0f, 1.0f), -1.0f);   // gate level doubles as strike strength
            }
            st->prevGate = g;
        }

        float out = 0.0f;
        if (st->pos >= 0 && st->pos < st->burstLen) {
            // Hann bump: at length 1 it is a single-sample impulse (flat
            // spectrum, hardest); wider bumps roll the highs off. The (p+0.5)/L
            // form is used rather than p/(L-1) so the window never degenerates to
            // all-zeros at short lengths (e.g. L == 2).
            const int   L = st->burstLen;
            const float w = (L <= 1)
                ? 1.0f
                : 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * ((float)st->pos + 0.5f) / (float)L));
            const float content = (st->type == 1) ? lcgBipolar(st->rng) * w : w;
            out = st->scale * content;
            if (++st->pos >= L) st->pos = -1;
        }
        outL[i] = out;
        outR[i] = out;
    }

    return &module->output;
}
