//
//  DrumKitModule.cpp — PMU port of PM's DrumKitModule.mm.
//
//  Note->drum routing and the cross-voice hi-hat choke handshake plus per-drum
//  synthesis (kick / snare / toms / hats / crash). DSP math is carried over
//  verbatim from PM; only the MIDI message type (pm::UmpMessage) and the shared
//  choke state (Module::sharedState -> ChokeGroupState) are PMU-native.
//

#include "DrumKitModule.hpp"
#include "ChokeGroupState.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

// ---- Note -> drum mapping ----

// The drums, one per pitch class. Order is not significant; DrumNone marks
// "voice not yet assigned".
enum DrumVoiceType : int {
    DrumNone = 0,
    DrumKick, DrumKick2,
    DrumSnare, DrumSnare2,
    DrumTomLow, DrumTomMid, DrumTomHigh,
    DrumCrash1, DrumCrash2,
    DrumHatOpen, DrumHatHalf, DrumHatClosed,
};

// Only the three hi-hats choke, and they all share this one group.
static constexpr uint32_t kHatChokeGroup = 1;   // != ChokeGroupState::NoChokeGroup

// note % 12 -> drum. Octave-independent: every C is a kick, every A# a closed
// hat, etc.  White keys = drums, black keys = crashes (C#,D#) and hats (F#,G#,A#).
static inline DrumVoiceType drumForNote(uint8_t noteNumber) {
    switch (noteNumber % 12) {
        case 0:  return DrumKick;        // C
        case 1:  return DrumCrash1;      // C#
        case 2:  return DrumKick2;       // D
        case 3:  return DrumCrash2;      // D#
        case 4:  return DrumSnare;       // E
        case 5:  return DrumSnare2;      // F
        case 6:  return DrumHatOpen;     // F#
        case 7:  return DrumTomLow;      // G
        case 8:  return DrumHatHalf;     // G#
        case 9:  return DrumTomMid;      // A
        case 10: return DrumHatClosed;   // A#
        case 11: return DrumTomHigh;     // B
    }
    return DrumNone;
}

// Choke group a drum belongs to. Only the hats choke; everything else rings.
static inline uint32_t chokeGroupForDrum(DrumVoiceType drum) {
    switch (drum) {
        case DrumHatOpen:
        case DrumHatHalf:
        case DrumHatClosed:
            return kHatChokeGroup;
        default:
            return ChokeGroupState::NoChokeGroup;
    }
}

// ---- Kick synthesis ----

static constexpr double kKickAttackSec = 0.0008;  // ~0.8 ms attack ramp (no thump-click)
static constexpr double kOutScale      = 0.9;     // leave a little headroom

// Bipolar white noise in [-1, 1), for the beater click.
static inline float lcgBipolar(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return (float)((int32_t)(s >> 8) - 8388608) / 8388608.0f;
}

// Kick voicing. The pitch drops exponentially from startFreq to endFreq (the
// classic "thump"); amplitude decays exponentially; a brief noise burst adds
// the beater attack. C (kick) is deep and long, D (kick2) tighter and higher.
struct KickParams {
    double startFreq;   // Hz at the strike
    double endFreq;     // Hz the pitch settles to
    double pitchTau;    // s, pitch-drop time constant
    double ampDecay;    // s, amplitude decay time constant
    double clickTau;    // s, beater-click decay
    float  clickLevel;  // beater-click amount
};

static inline KickParams kickParamsFor(DrumVoiceType drum) {
    if (drum == DrumKick2) {
        return { 230.0, 68.0, 0.026, 0.24, 0.0015, 0.45f };  // D: higher-pitched, tighter
    }
    return     { 170.0, 46.0, 0.034, 0.34, 0.0018, 0.40f };  // C: deeper, longer
}

// ---- Snare synthesis ----

static constexpr double kSnareAttackSec = 0.0005;
static constexpr double kSnarePitchTau  = 0.025;   // body pitch-glide time constant

// RBJ bandpass (constant 0 dB peak gain), normalised, b1 == 0. Processed with a
// transposed-direct-form-II tick using two state variables.
struct BiquadBP { double b0, b2, a1, a2; };

static inline BiquadBP makeBandpass(double fc, double Q, double sr) {
    const double w0    = 2.0 * M_PI * fc / sr;
    const double cw    = std::cos(w0);
    const double sw    = std::sin(w0);
    const double alpha = sw / (2.0 * Q);
    const double a0    = 1.0 + alpha;
    return { alpha / a0, -alpha / a0, (-2.0 * cw) / a0, (1.0 - alpha) / a0 };
}

static inline double biquadTick(const BiquadBP& bp, double x, double& z1, double& z2) {
    const double y = bp.b0 * x + z1;
    z1 = -bp.a1 * y + z2;      // b1 == 0
    z2 =  bp.b2 * x - bp.a2 * y;
    return y;
}

// Snare = modal drum body (a few inharmonic membrane modes) + the wire buzz
// (band-limited noise) + a stick crack. E is the main snare (fuller, longer
// buzz); F is tighter/higher/drier.
struct SnareParams {
    double f0;                 // body fundamental (Hz)
    double modeRatio[3];       // inharmonic membrane ratios
    double modeDecay[3];       // s
    float  modeAmp[3];
    float  bodyLevel;

    double noiseFc;            // wire-noise band A centre (Hz)
    double noiseQ;
    double noiseFc2;           // band B centre (brighter "air")
    double noiseQ2;
    float  noiseLevel2;        // band B level, relative to band A
    double noiseDecay;         // s
    float  noiseLevel;         // overall wire level

    double crackTau;           // s, stick transient
    float  crackLevel;
};

[[maybe_unused]] static inline SnareParams snareParamsFor(DrumVoiceType drum) {
    SnareParams p;
    // First circular-membrane modes are inharmonic: 1, ~1.59, ~2.14.
    p.modeRatio[0] = 1.0;  p.modeRatio[1] = 1.59;  p.modeRatio[2] = 2.14;
    if (drum == DrumSnare2) {
        // F: tighter, higher, drier (rimshot-leaning).
        p.f0 = 236.0;
        p.modeDecay[0] = 0.14; p.modeDecay[1] = 0.09; p.modeDecay[2] = 0.06;
        p.modeAmp[0] = 1.0f;   p.modeAmp[1] = 0.55f;  p.modeAmp[2] = 0.35f;
        p.bodyLevel = 0.42f;
        p.noiseFc = 4200.0; p.noiseQ = 0.9; p.noiseFc2 = 7200.0; p.noiseQ2 = 0.7;
        p.noiseLevel2 = 0.55f; p.noiseDecay = 0.12; p.noiseLevel = 0.85f;
        p.crackTau = 0.0009; p.crackLevel = 0.35f;
    } else {
        // E: main snare — fuller body, longer wire buzz.
        p.f0 = 190.0;
        p.modeDecay[0] = 0.19; p.modeDecay[1] = 0.12; p.modeDecay[2] = 0.08;
        p.modeAmp[0] = 1.0f;   p.modeAmp[1] = 0.6f;   p.modeAmp[2] = 0.4f;
        p.bodyLevel = 0.5f;
        p.noiseFc = 3600.0; p.noiseQ = 0.8; p.noiseFc2 = 6500.0; p.noiseQ2 = 0.7;
        p.noiseLevel2 = 0.5f; p.noiseDecay = 0.17; p.noiseLevel = 0.8f;
        p.crackTau = 0.0011; p.crackLevel = 0.3f;
    }
    return p;
}

// ---- Tom synthesis ----

static constexpr int    kMaxModes     = 24;   // toms use 5; cymbals use up to ~22
static constexpr double kTomAttackSec = 0.0006;
static constexpr double kTomPitchTau  = 0.04;   // pitch-glide time constant

// Tom = pitched modal membrane, but kept deliberately un-tonal: several
// inharmonic modes (so no single pitch dominates), a downward pitch glide at
// the attack, and a short low-passed skin "thump" all blur the sense of a clear
// musical note. G/A/B = low/mid/high.
struct TomParams {
    double f0;
    int    numModes;
    double modeRatio[kMaxModes];
    double modeDecay[kMaxModes];   // s
    float  modeAmp[kMaxModes];
    float  bodyLevel;

    double pitchDrop;              // fractional pitch rise at t=0 (glides to f0)
    double pitchTau;               // s

    double thumpFc;                // low skin-contact "pock"
    double thumpQ;
    double thumpDecay;             // s
    float  thumpLevel;

    double crackTau;               // s, faint stick definition
    float  crackLevel;
};

[[maybe_unused]] static inline TomParams tomParamsFor(DrumVoiceType drum) {
    TomParams p;
    p.numModes = 5;
    // Ideal circular-membrane modes (inharmonic).
    p.modeRatio[0] = 1.0;  p.modeRatio[1] = 1.59; p.modeRatio[2] = 2.14;
    p.modeRatio[3] = 2.30; p.modeRatio[4] = 2.65; p.modeRatio[5] = 2.92;
    // Upper modes kept prominent at onset so the hit reads as a drum, not a
    // note — the fundamental is NOT allowed to dominate.
    p.modeAmp[0] = 0.9f; p.modeAmp[1] = 0.8f; p.modeAmp[2] = 0.65f;
    p.modeAmp[3] = 0.5f; p.modeAmp[4] = 0.4f; p.modeAmp[5] = 0.3f;
    p.bodyLevel  = 0.5f;
    p.pitchDrop  = 0.18;   // ~2.8 semitones of downward glide
    p.pitchTau   = 0.045;
    p.thumpQ     = 0.7;
    p.crackTau   = 0.0010;
    p.crackLevel = 0.18f;

    if (drum == DrumTomHigh) {          // B
        p.f0 = 196.0;
        const double d[5] = { 0.26, 0.18, 0.13, 0.10, 0.08 };
        for (int k = 0; k < 5; ++k) { p.modeDecay[k] = d[k]; }
        p.thumpFc = 1400.0; p.thumpDecay = 0.018; p.thumpLevel = 0.35f;
    } else if (drum == DrumTomMid) {    // A
        p.f0 = 146.0;
        const double d[5] = { 0.34, 0.23, 0.16, 0.12, 0.09 };
        for (int k = 0; k < 5; ++k) { p.modeDecay[k] = d[k]; }
        p.thumpFc = 1100.0; p.thumpDecay = 0.022; p.thumpLevel = 0.40f;
    } else {                            // G, low tom
        p.f0 = 104.0;
        const double d[5] = { 0.46, 0.30, 0.21, 0.15, 0.11 };
        for (int k = 0; k < 5; ++k) { p.modeDecay[k] = d[k]; }
        p.thumpFc = 850.0;  p.thumpDecay = 0.026; p.thumpLevel = 0.45f;
    }
    return p;
}

// ---- Hat synthesis ----

static constexpr double kHatAttackSec = 0.0003;
static constexpr double kHatChokeTau  = 0.006;   // ~6 ms fast-release on choke

// General (5-coeff) biquad, for the noise high-pass. TDF-II tick.
struct Biquad { double b0, b1, b2, a1, a2; };

static inline Biquad makeHighpass(double fc, double Q, double sr) {
    const double w0    = 2.0 * M_PI * fc / sr;
    const double cw    = std::cos(w0);
    const double sw    = std::sin(w0);
    const double alpha = sw / (2.0 * Q);
    const double a0    = 1.0 + alpha;
    return { (1.0 + cw) / 2.0 / a0, -(1.0 + cw) / a0, (1.0 + cw) / 2.0 / a0,
             (-2.0 * cw) / a0, (1.0 - alpha) / a0 };
}

static inline Biquad makeLowpass(double fc, double Q, double sr) {
    const double w0    = 2.0 * M_PI * fc / sr;
    const double cw    = std::cos(w0);
    const double sw    = std::sin(w0);
    const double alpha = sw / (2.0 * Q);
    const double a0    = 1.0 + alpha;
    return { (1.0 - cw) / 2.0 / a0, (1.0 - cw) / a0, (1.0 - cw) / 2.0 / a0,
             (-2.0 * cw) / a0, (1.0 - alpha) / a0 };
}

static inline double biquadTickG(const Biquad& f, double x, double& z1, double& z2) {
    const double y = f.b0 * x + z1;
    z1 = f.b1 * x - f.a1 * y + z2;
    z2 = f.b2 * x - f.a2 * y;
    return y;
}

// Build a DENSE, irregular, inharmonic partial set spanning [low, high]. Many
// closely-and-unevenly-spaced modes beat together into shimmering metal with no
// clear pitch — the sparse 6-partial cluster read as a tuned chord. Ratios are
// log-spread with a per-step jitter so none line up; amplitudes roll off toward
// the top and are normalised so total level is ~constant regardless of count.
static inline int fillCymbalPartials(double low, double high, int n,
                                     double* freq, float* amp) {
    if (n > kMaxModes) { n = kMaxModes; }
    if (n < 1)         { n = 1; }
    const double avgStep = (n > 1) ? std::pow(high / low, 1.0 / (n - 1)) : 1.0;
    uint32_t s = 0xC0FFEEu;
    double f  = low;
    double ss = 0.0;
    for (int k = 0; k < n; ++k) {
        freq[k] = f;
        const float a = (float)(1.0 / (1.0 + 0.10 * k));   // gentle top rolloff
        amp[k] = a;
        ss += (double)a * (double)a;
        s = s * 1664525u + 1013904223u;                    // irregular step ±12%
        const double jit = 0.88 + ((double)((s >> 8) & 0xFFFF) / 65535.0) * 0.24;
        f *= avgStep * jit;
    }
    const float norm = (ss > 0.0) ? (float)(1.0 / std::sqrt(ss)) : 1.0f;
    for (int k = 0; k < n; ++k) { amp[k] *= norm; }
    return n;
}

// Hi-hat = high-passed noise "sizzle" + a DENSE inharmonic metal bank spanning
// low-mids to highs. Open/half/closed differ only in decay. F#/G#/A# share a
// choke group: a newer hat fast-releases the older.
struct HatParams {
    double partialLow, partialHigh;   // dense inharmonic metal band
    int    numPartials;
    float  metalLevel;

    double noiseHpFc, noiseHpQ;
    float  noiseLevel;

    double decay;                     // s — the open/half/closed difference
    double crackTau;
    float  crackLevel;
};

[[maybe_unused]] static inline HatParams hatParamsFor(DrumVoiceType drum) {
    HatParams p;
    p.partialLow = 520.0; p.partialHigh = 12000.0; p.numPartials = 28;
    p.metalLevel = 0.20f;
    p.noiseHpFc  = 1200.0; p.noiseHpQ = 0.707; p.noiseLevel = 0.6f;
    p.crackTau   = 0.0006; p.crackLevel = 0.2f;

    if (drum == DrumHatOpen)      { p.decay = 0.40;  }   // F#
    else if (drum == DrumHatHalf) { p.decay = 0.14;  }   // G#
    else                          { p.decay = 0.045; }   // A#, closed
    return p;
}

// ---- Crash synthesis ----

static constexpr double kCrashAttackSec = 0.0004;

// Map a 0..1 knob to a real range (linear or log).
static inline double mapLin(float v, double lo, double hi) { return lo + (double)v * (hi - lo); }
static inline double mapExp(float v, double lo, double hi) { return lo * std::pow(hi / lo, (double)v); }

// Crash = a bigger, longer cymbal: dense inharmonic metal shimmer (long decay)
// over a faster high-passed noise "wash". No choke — crashes ring out. C#/D#
// are a larger/lower and a smaller/brighter cymbal.
struct CrashParams {
    double partialLow, partialHigh;
    int    numPartials;
    float  metalLevel;
    double metalDecay;         // s — the long shimmer
    double noiseHpFc, noiseHpQ; // noise band: high-pass...
    double noiseLpFc, noiseLpQ; // ...and low-pass, so the wash is band-limited
    float  noiseLevel;
    double noiseDecay;         // s — faster initial wash
    double crackTau;
    float  crackLevel;
};

static inline CrashParams crashParamsFor(DrumVoiceType drum) {
    CrashParams p;
    // Bigger, lower, denser than the hats (a crash is a large cymbal).
    p.metalLevel = 0.32f;
    p.noiseHpQ = 0.707; p.noiseLpQ = 0.707; p.crackTau = 0.0007; p.crackLevel = 0.25f;
    if (drum == DrumCrash2) {           // D#: smaller, brighter, shorter
        p.partialLow = 600; p.partialHigh = 12000.0; p.numPartials = 24;
        p.metalDecay = 0.9; p.noiseLevel = 0.50f; p.noiseDecay = 0.28;
        // Bright, open, high band.
        p.noiseHpFc = 1000.0; p.noiseLpFc = 9000.0;
    } else {                            // C#: larger, lower, longer
        p.partialLow = 350.0; p.partialHigh = 11000.0; p.numPartials = 24;
        p.metalDecay = 1.2; p.noiseLevel = 0.55f; p.noiseDecay = 0.35;
        // Dark, lower, mid-focused band.
        p.noiseHpFc = 450.0; p.noiseLpFc = 3200.0;
    }
    return p;
}

// ---- State ----

struct DrumKitState {
    double sampleRate = 44100.0;

    // The drum this voice is currently sounding (set at note-on).
    DrumVoiceType drum = DrumNone;

    // Pending note-on, consumed at the top of the next process block.
    bool  pendingTrigger = false;
    float pendingVel     = 1.0f;   // 0..1
    uint8_t pendingNote  = 60;

    // Choke bookkeeping for THIS voice.
    uint32_t chokeGroup   = ChokeGroupState::NoChokeGroup;
    uint64_t ownedChokeGen = 0;    // generation this voice claimed at its strike
    bool     choked        = false; // superseded by a newer hit in the group

    // Active-voice synthesis state.
    bool     playing = false;
    uint64_t agePos  = 0;          // samples since the current strike
    double   phase   = 0.0;        // kick body oscillator phase (radians)
    float    velAmp  = 1.0f;       // velocity-scaled amplitude latched at strike
    uint32_t rng     = 0x1234567u; // noise generator
    float    chokeGain = 1.0f;     // hi-hat fast-release multiplier (1 → 0 on choke)

    // Modal-body phases (snare + toms) and the noise bandpass filter states.
    double   modePhase[kMaxModes] = { 0.0 };
    double   nz1 = 0.0, nz2 = 0.0;   // band A / tom thump
    double   nz3 = 0.0, nz4 = 0.0;   // band B

    // Crash bloom: two cascaded one-pole LPFs.
    double   lp1 = 0.0, lp2 = 0.0;
    // Snare: smoothed body-motion follower for the wire coupling.
    double   bodyEnvF = 0.0;
};

static inline void resetVoiceDSP(DrumKitState* st) {
    st->phase = 0.0;
    for (int k = 0; k < kMaxModes; ++k) { st->modePhase[k] = 0.0; }
    st->nz1 = st->nz2 = st->nz3 = st->nz4 = 0.0;
    st->chokeGain = 1.0f;
    st->lp1 = st->lp2 = 0.0;
    st->bodyEnvF = 0.0;
}

// ---- Lifecycle ----

const void InitDrumKitModule(uint32_t /*count*/, Module* module)
{
    auto* st = new DrumKitState();
    st->sampleRate = module->mSampleRate;
    module->state = st;
}

const void DeinitDrumKitModule(Module* module)
{
    delete static_cast<DrumKitState*>(module->state);
    module->state = nullptr;
}

const void ResetDrumKitModule(Module* module)
{
    if (auto* st = static_cast<DrumKitState*>(module->state)) {
        st->drum = DrumNone;
        st->pendingTrigger = false;
        st->chokeGroup = ChokeGroupState::NoChokeGroup;
        st->ownedChokeGen = 0;
        st->choked = false;
        st->playing = false;
        st->agePos = 0;
        resetVoiceDSP(st);
    }
}

// ---- MIDI ----

// Note-on assigns this voice a drum and, for hi-hats, claims the choke group so
// any older hi-hat voice will fast-release. The strike itself is started in the
// process block. Note-off is ignored: a drum hit does not sustain.
void MIDIDrumKitModule(const pm::UmpMessage& message, Module* module)
{
    auto* st = static_cast<DrumKitState*>(module->state);
    if (!st) { return; }

    if (!pm::isNoteOn(message)) {   // note-on with non-zero velocity only
        return;
    }

    st->pendingNote    = (uint8_t)message.note;
    st->pendingVel     = (float)message.velocity / 65535.0f;   // MIDI 2.0 16-bit
    st->pendingTrigger = true;

    st->drum       = drumForNote(st->pendingNote);
    st->chokeGroup = chokeGroupForDrum(st->drum);
    st->choked     = false;

    // Claim the choke group so older voices in it release. triggerChoke is a
    // no-op for NoChokeGroup, so non-hats simply skip this.
    if (st->chokeGroup != ChokeGroupState::NoChokeGroup) {
        if (auto* choke = static_cast<ChokeGroupState*>(module->sharedState)) {
            st->ownedChokeGen = choke->triggerChoke(st->chokeGroup);
        }
    }
}

// ---- Process ----

const KPBuffPair* ProcessDrumKitModule(uint64_t /*processCallID*/, uint32_t count, Module* module)
{
    auto* st = static_cast<DrumKitState*>(module->state);
    if (!st) { return KPBuffPairHelper::zeroes(); }

    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    const double sr    = st->sampleRate;
    const float  level = module->getParamValue(DrumKitParamItemLevel);

    // Begin a strike. The assigned drum and velocity were latched in the MIDI
    // handler; the voice itself starts here so it reads current params.
    if (st->pendingTrigger) {
        st->pendingTrigger = false;
        st->playing = true;
        st->agePos  = 0;
        st->velAmp  = std::max(0.0f, std::min(1.0f, st->pendingVel));
        resetVoiceDSP(st);
    }

    // Cross-voice choke: a newer hi-hat in our group supersedes us.
    if (st->chokeGroup != ChokeGroupState::NoChokeGroup) {
        if (auto* choke = static_cast<ChokeGroupState*>(module->sharedState)) {
            if (choke->isChoked(st->chokeGroup, st->ownedChokeGen)) {
                st->choked = true;
            }
        }
    }

    // ---- Per-drum synthesis --------------------------------------------------
    const bool isKick  = (st->drum == DrumKick  || st->drum == DrumKick2);
    const bool isSnare = (st->drum == DrumSnare || st->drum == DrumSnare2);
    const bool isTom   = (st->drum == DrumTomLow || st->drum == DrumTomMid || st->drum == DrumTomHigh);
    const bool isHat   = (st->drum == DrumHatOpen || st->drum == DrumHatHalf || st->drum == DrumHatClosed);
    const bool isCrash = (st->drum == DrumCrash1 || st->drum == DrumCrash2);

    if (st->playing && isKick) {
        const KickParams kp = kickParamsFor(st->drum);
        const double twoPi = 2.0 * M_PI;

        for (uint32_t i = 0; i < count; ++i) {
            const double t = (double)st->agePos / sr;   // seconds since strike

            // Exponential pitch drop: f(t) = fEnd + (fStart - fEnd) e^{-t/tau}.
            const double f = kp.endFreq + (kp.startFreq - kp.endFreq) * std::exp(-t / kp.pitchTau);

            // Amplitude: short linear attack into an exponential decay.
            const double atk = std::min(1.0, t / kKickAttackSec);
            const double amp = atk * std::exp(-t / kp.ampDecay);

            // Sine body at the swept frequency.
            st->phase += twoPi * f / sr;
            if (st->phase >= twoPi) { st->phase -= twoPi; }
            const double body = std::sin(st->phase) * amp;

            // Beater click: a fast-decaying noise transient at the very start.
            double click = 0.0;
            if (t < 6.0 * kp.clickTau) {
                click = (double)kp.clickLevel * std::exp(-t / kp.clickTau) * lcgBipolar(st->rng);
            }

            const float s = (float)((body + click) * st->velAmp * level * kOutScale);
            outL[i] = s;
            outR[i] = s;

            st->agePos++;

            // Retire the voice once the tail is inaudible so the voice can free.
            if (amp < 1.0e-4 && t > 6.0 * kp.clickTau) {
                st->playing = false;
            }
        }
    } else if (st->playing && isSnare) {
        const double twoPi = 2.0 * M_PI;
        const bool snare2 = (st->drum == DrumSnare2);

        // F is tighter / higher / drier than E. Because F is drier (less wire wash to
        // mask the body) and pitched higher, its body reads as more tonal — so F gets a
        // stronger de-tone: shorter body, bigger glide, and more fundamental trim.
        const double f0Off       = snare2 ? 1.24 : 1.0;
        const double bodyDecOff  = snare2 ? 0.50 : 1.0;   // was 0.72 — kill F's pitched ring faster
        const double noiseDecOff = snare2 ? 0.80 : 1.0;

        // --- finalized snare controls (see DrumKit UI) ---
        const double f0         = mapExp(module->getParamValue(DrumKitParamItemSnareTune),   120.0, 320.0) * f0Off;
        const float  balance    =        module->getParamValue(DrumKitParamItemSnareBody);            // 0 wires .. 1 body
        const double noiseDecay = mapExp(module->getParamValue(DrumKitParamItemSnareSnares), 0.03, 0.4) * noiseDecOff;
        const double buzz       = (double)module->getParamValue(DrumKitParamItemSnareBuzz);           // body->wire coupling
        // fixed voicing (culled knobs):
        const double bodyDecay  = 0.085 * bodyDecOff;  // was 0.13 — shorter so the pitched
                                                       // membrane ring doesn't sustain into a
                                                       // tom "note"; the tail should be wires.
        const double pitchDrop  = snare2 ? 0.20 : 0.10; // real attack glide (was 0.01, a no-op):
                                                       // de-tones the fundamental so the body
                                                       // reads as a hit, not a pitched tom. F
                                                       // (drier/higher) needs a stronger smear.
        const double noiseScale = 1.0;                 // E/F bands already differ
        const double crackLevel = 0.15;                // small fixed stick crack
        const double bodyLevel  = 0.15 + 0.50 * (double)balance;    // always some body...
        const double noiseLevel = 0.85 - 0.40 * (double)balance;    // ...and always some wires

        // Fixed voicing: inharmonic membrane modes + two wire-noise bands.
        static const double modeRatio[3]      = { 1.0, 1.59, 2.14 };
        static const double modeDecayRatio[3] = { 1.0, 0.63, 0.42 };   // higher modes die sooner
        static const double modeAmp[3]        = { 0.80, 0.62, 0.42 };  // thinned fundamental:
                                                       // a snare head is thinner/clangier than a
                                                       // round tom, so lean off mode 0.
        // F leans even further off the fundamental (drier snare exposes the body's pitch).
        const double f0Trim   = snare2 ? 0.65 : 1.0;
        const double bodyAmp[3] = { modeAmp[0] * f0Trim, modeAmp[1], modeAmp[2] };
        // Distinct wire bands per drum: E fuller/lower & broad, F brighter/tighter.
        const BiquadBP bpA = makeBandpass((snare2 ? 4800.0 : 3000.0) * noiseScale, snare2 ? 1.0 : 0.7, sr);
        const BiquadBP bpB = makeBandpass((snare2 ? 8500.0 : 5500.0) * noiseScale, snare2 ? 0.9 : 0.7, sr);
        const double   noiseLevel2 = 0.5;
        const double   crackTau    = 0.001;
        const double   maxDecay    = std::max(bodyDecay, noiseDecay);
        const double   gEnvF       = 1.0 - std::exp(-1.0 / (sr * 0.005));   // ~5 ms motion follower

        for (uint32_t i = 0; i < count; ++i) {
            const double t   = (double)st->agePos / sr;
            const double atk = std::min(1.0, t / kSnareAttackSec);

            // Modal body with a downward pitch glide — the glide de-tones the
            // fundamental so the body reads as a hit, not a pitched note.
            const double glide = 1.0 + pitchDrop * std::exp(-t / kSnarePitchTau);
            double bodyRaw = 0.0;
            for (int k = 0; k < 3; ++k) {
                const double env = std::exp(-t / (bodyDecay * modeDecayRatio[k]));
                st->modePhase[k] += twoPi * (f0 * modeRatio[k] * glide) / sr;
                if (st->modePhase[k] >= twoPi) { st->modePhase[k] -= twoPi; }
                bodyRaw += bodyAmp[k] * env * std::sin(st->modePhase[k]);
            }
            const double body = bodyRaw * bodyLevel * atk;

            // Wire buzz: white noise through two bandpasses, DRIVEN by the head
            // motion (|bodyRaw|) — the wires rattle in sync with the membrane.
            const double nz     = lcgBipolar(st->rng);
            const double bandA  = biquadTick(bpA, nz, st->nz1, st->nz2);
            const double bandB  = biquadTick(bpB, nz, st->nz3, st->nz4);
            st->bodyEnvF += gEnvF * (std::fabs(bodyRaw) - st->bodyEnvF);
            const double motion = std::min(1.0, st->bodyEnvF * 1.8);
            const double drive  = (1.0 - buzz) + buzz * motion;
            const double wires  = (bandA + noiseLevel2 * bandB)
                                * std::exp(-t / noiseDecay) * noiseLevel * drive;

            // Stick crack: brief broadband transient.
            double crack = 0.0;
            if (t < 6.0 * crackTau) {
                crack = crackLevel * std::exp(-t / crackTau) * lcgBipolar(st->rng);
            }

            const float s = (float)((body + wires + crack) * st->velAmp * level * kOutScale);
            outL[i] = s;
            outR[i] = s;

            st->agePos++;

            if (t > 9.5 * maxDecay) { st->playing = false; }
        }
    } else if (st->playing && isTom) {
        const double twoPi = 2.0 * M_PI;

        // G / A / B = low / mid / high: one Tune knob transposes all three,
        // keeping their relative spread and per-tom decay/thump character.
        double tomRatio, decayOff, thumpOff;
        if (st->drum == DrumTomLow)      { tomRatio = 0.71; decayOff = 1.30; thumpOff = 0.85; }
        else if (st->drum == DrumTomMid) { tomRatio = 1.00; decayOff = 1.00; thumpOff = 1.00; }
        else                             { tomRatio = 1.34; decayOff = 0.75; thumpOff = 1.20; }

        // --- finalized tom controls (see DrumKit UI) ---
        const double f0         = mapExp(module->getParamValue(DrumKitParamItemTomTune),  90.0, 240.0) * tomRatio;
        const double bodyDecay  = mapExp(module->getParamValue(DrumKitParamItemTomDecay), 0.08, 0.8)  * decayOff;
        const float  bright     =        module->getParamValue(DrumKitParamItemTomBright);
        const double thumpLevel = mapLin(module->getParamValue(DrumKitParamItemTomThump), 0.0,  0.8);
        // fixed voicing (culled knobs):
        const double pitchDrop  = 0.35;
        const double thumpFc    = 1000.0 * thumpOff;

        // Fixed voicing: inharmonic membrane modes, higher modes die sooner.
        static const double modeRatio[5]      = { 1.0, 1.59, 2.14, 2.30, 2.65 };
        static const double modeDecayRatio[5] = { 1.0, 0.70, 0.52, 0.42, 0.35 };
        const double bodyLevel = 0.55;
        const double crackTau  = 0.001;
        const double crackLevel= 0.12;

        // Mode amplitudes from Bright: higher Bright lifts the upper (inharmonic)
        // modes, spreading energy off the fundamental. Normalised to ~constant RMS.
        double amp[5]; double ss = 0.0;
        const double rolloff = 0.60 + 0.35 * (double)bright;
        for (int k = 0; k < 5; ++k) { amp[k] = std::pow(rolloff, (double)k); ss += amp[k] * amp[k]; }
        const double anorm = (ss > 0.0) ? 1.0 / std::sqrt(ss) : 1.0;
        for (int k = 0; k < 5; ++k) { amp[k] *= anorm; }

        const BiquadBP thump = makeBandpass(thumpFc, 0.7, sr);

        for (uint32_t i = 0; i < count; ++i) {
            const double t   = (double)st->agePos / sr;
            const double atk = std::min(1.0, t / kTomAttackSec);

            // Downward pitch glide — the tom "dooom".
            const double glide = 1.0 + pitchDrop * std::exp(-t / kTomPitchTau);

            double body = 0.0;
            for (int k = 0; k < 5; ++k) {
                const double env = std::exp(-t / (bodyDecay * modeDecayRatio[k]));
                st->modePhase[k] += twoPi * (f0 * modeRatio[k] * glide) / sr;
                if (st->modePhase[k] >= twoPi) { st->modePhase[k] -= twoPi; }
                body += amp[k] * env * std::sin(st->modePhase[k]);
            }
            body *= bodyLevel * atk;

            // Skin thump: a short low-passed noise "pock" at the attack.
            const double nz   = lcgBipolar(st->rng);
            const double pock = biquadTick(thump, nz, st->nz1, st->nz2)
                              * std::exp(-t / 0.02) * thumpLevel;

            double crack = 0.0;
            if (t < 6.0 * crackTau) {
                crack = crackLevel * std::exp(-t / crackTau) * lcgBipolar(st->rng);
            }

            const float s = (float)((body + pock + crack) * st->velAmp * level * kOutScale);
            outL[i] = s;
            outR[i] = s;

            st->agePos++;

            if (t > 9.5 * bodyDecay) { st->playing = false; }
        }
    } else if (st->playing && isHat) {
        const double twoPi = 2.0 * M_PI;
        const double chokePerSample = std::exp(-1.0 / (sr * kHatChokeTau));

        const bool hatOpen = (st->drum == DrumHatOpen);
        const bool hatHalf = (st->drum == DrumHatHalf);
        // Pitch: open lowest; half and closed a little higher.
        const double pitchMul = hatOpen ? 1.0 : (hatHalf ? 1.15 : 1.25);

        // --- finalized hat controls (see DrumKit UI) ---
        const double pitchScale = mapExp(module->getParamValue(DrumKitParamItemHatTune),   0.6, 1.6) * pitchMul;
        const float  balance    =        module->getParamValue(DrumKitParamItemHatBalance);           // 0 metal .. 1 noise
        const double noiseHpFc  = mapExp(module->getParamValue(DrumKitParamItemHatBright), 700.0, 3500.0);
        const double metalLevel = 0.40 * (1.0 - (double)balance);
        const double noiseLevel = 0.80 * (double)balance;
        // fixed open/half/closed lengths (culled knobs):
        const double decay      = hatOpen ? 0.40 : (hatHalf ? 0.14 : 0.045);

        const Biquad hpf = makeHighpass(noiseHpFc, 0.707, sr);
        double freq[kMaxModes]; float amp[kMaxModes];
        const int np = fillCymbalPartials(500.0 * pitchScale, 13000.0, 24, freq, amp);

        // De-pitch (crash-style): low partials die fast + are de-emphasised, so
        // the sustain is dense high shimmer instead of a pitched tone.
        double decayMult[kMaxModes];
        {
            uint32_t s = 0x7A11EDu;
            double ss = 0.0;
            for (int k = 0; k < np; ++k) {
                s = s * 1664525u + 1013904223u;
                const double r = (double)((s >> 8) & 0xFFFF) / 65535.0;
                const double idx = (np > 1) ? (double)k / (double)(np - 1) : 1.0;
                decayMult[k] = (0.20 + 0.80 * idx) * (0.85 + 0.30 * r);
                const double tilt = std::min(1.0, freq[k] / 1500.0);
                amp[k] *= (float)(0.4 + 0.6 * tilt);
                ss += (double)amp[k] * (double)amp[k];
            }
            const float norm = (ss > 0.0) ? (float)(1.0 / std::sqrt(ss)) : 1.0f;
            for (int k = 0; k < np; ++k) { amp[k] *= norm; }
        }

        for (uint32_t i = 0; i < count; ++i) {
            const double t   = (double)st->agePos / sr;
            const double atk = std::min(1.0, t / kHatAttackSec);

            // Metal bank with per-partial decay (lows die fast -> de-pitched).
            double metal = 0.0;
            for (int k = 0; k < np; ++k) {
                st->modePhase[k] += twoPi * freq[k] / sr;
                if (st->modePhase[k] >= twoPi) { st->modePhase[k] -= twoPi; }
                const double menv = std::exp(-t / (decay * decayMult[k]));
                metal += (double)amp[k] * menv * std::sin(st->modePhase[k]);
            }
            metal *= metalLevel;

            // High-passed noise sizzle — the bulk of the hat, with the hat decay.
            const double nz = lcgBipolar(st->rng);
            const double sizzle = biquadTickG(hpf, nz, st->nz1, st->nz2)
                                * std::exp(-t / decay) * noiseLevel;

            // Small fixed stick crack.
            double crack = 0.0;
            if (t < 6.0 * 0.0006) {
                crack = 0.2 * std::exp(-t / 0.0006) * lcgBipolar(st->rng);
            }

            // Choke fast-release: a newer hi-hat superseded us -> ramp to zero.
            if (st->choked) { st->chokeGain *= (float)chokePerSample; }

            const double shaped = (metal + sizzle + crack) * atk * (double)st->chokeGain;
            const float  s = (float)(shaped * st->velAmp * level * kOutScale);
            outL[i] = s;
            outR[i] = s;

            st->agePos++;

            if ((st->choked && st->chokeGain < 1.0e-4f) || t > 9.5 * decay) {
                st->playing = false;
            }
        }
    } else if (st->playing && isCrash) {
        const CrashParams cp = crashParamsFor(st->drum);
        const double twoPi = 2.0 * M_PI;
        const bool   crash2 = (st->drum == DrumCrash2);

        // --- finalized crash controls (see DrumKit UI) ---
        const double bloomTime  = mapLin(module->getParamValue(DrumKitParamItemCrashSwell),  0.0,    0.300);   // s
        const double bloomLo    = 1000.0;                                                                       // fixed dark onset
        const double bloomHi    = mapExp(module->getParamValue(DrumKitParamItemCrashBright), 3000.0, 18000.0); // Hz
        const float  balance    =        module->getParamValue(DrumKitParamItemCrashTone);                     // 0 metal .. 1 noise
        const double metalDecay = mapExp(module->getParamValue(DrumKitParamItemCrashDecay),  0.2,    3.0) * (crash2 ? 0.75 : 1.0);
        const double noiseDecay = metalDecay * 0.30;                                                            // wash tracks the ring
        const double metalLevel = 0.40 * (1.0 - (double)balance);
        const double noiseLevel = 0.70 * (double)balance;

        const Biquad hpf = makeHighpass(cp.noiseHpFc, cp.noiseHpQ, sr);
        const Biquad lpf = makeLowpass(cp.noiseLpFc, cp.noiseLpQ, sr);
        double freq[kMaxModes]; float amp[kMaxModes];
        const int np = fillCymbalPartials(cp.partialLow, cp.partialHigh, cp.numPartials, freq, amp);

        // Per-partial decay + low-end de-emphasis so the SUSTAIN is a
        // decorrelating shimmer, not a pitched chord. Deterministic per note.
        double decayMult[kMaxModes];
        {
            uint32_t s = 0x51ED5EEDu;
            double ss = 0.0;
            for (int k = 0; k < np; ++k) {
                s = s * 1664525u + 1013904223u;
                const double r = (double)((s >> 8) & 0xFFFF) / 65535.0;   // 0..1
                const double idx = (np > 1) ? (double)k / (double)(np - 1) : 1.0;
                decayMult[k] = (0.20 + 0.80 * idx) * (0.85 + 0.30 * r);   // lows die fast, highs ring
                const double tilt = std::min(1.0, freq[k] / 1200.0);     // roll the lowest partials down
                amp[k] *= (float)(0.4 + 0.6 * tilt);
                ss += (double)amp[k] * (double)amp[k];
            }
            const float norm = (ss > 0.0) ? (float)(1.0 / std::sqrt(ss)) : 1.0f;
            for (int k = 0; k < np; ++k) { amp[k] *= norm; }
        }

        for (uint32_t i = 0; i < count; ++i) {
            const double t   = (double)st->agePos / sr;
            const double atk = std::min(1.0, t / kCrashAttackSec);

            // Dense inharmonic metal shimmer, each partial with its own decay.
            double metal = 0.0;
            for (int k = 0; k < np; ++k) {
                st->modePhase[k] += twoPi * freq[k] / sr;
                if (st->modePhase[k] >= twoPi) { st->modePhase[k] -= twoPi; }
                const double env = std::exp(-t / (metalDecay * decayMult[k]));
                metal += (double)amp[k] * env * std::sin(st->modePhase[k]);
            }
            metal *= metalLevel;

            // Band-limited noise wash (HP then LP).
            const double nz    = lcgBipolar(st->rng);
            const double nHp   = biquadTickG(hpf, nz,  st->nz1, st->nz2);
            const double nband = biquadTickG(lpf, nHp, st->nz3, st->nz4);
            const double wash  = nband * std::exp(-t / noiseDecay) * noiseLevel;

            double crack = 0.0;
            if (t < 6.0 * cp.crackTau) {
                crack = (double)cp.crackLevel * std::exp(-t / cp.crackTau) * lcgBipolar(st->rng);
            }

            const double sig = metal + wash + crack;

            // Bloom: a resonance-free LPF whose cutoff sweeps Lo->Hi over
            // bloomTime, so the highs arrive late (the crash swell).
            const double frac = (bloomTime > 0.0) ? std::min(1.0, t / bloomTime) : 1.0;
            const double fc   = bloomLo * std::pow(bloomHi / bloomLo, frac);
            const double g    = 1.0 - std::exp(-twoPi * fc / sr);
            st->lp1 += g * (sig - st->lp1);
            st->lp2 += g * (st->lp1 - st->lp2);

            const float s = (float)(st->lp2 * atk * st->velAmp * level * kOutScale);
            outL[i] = s;
            outR[i] = s;

            st->agePos++;

            if (t > 7.5 * metalDecay) { st->playing = false; }
        }
    } else {
        // Idle / unassigned voice.
        std::fill_n(outL, count, 0.0f);
        std::fill_n(outR, count, 0.0f);
    }

    return &module->output;
}
