//
//  DrumKitModule.cpp — StruckKit's kit: PMU's DrumKit routing with physically modelled drums.
//
//  Note->drum routing (the StruckKit pitch-class map, or General MIDI) and the cross-voice hi-hat choke
//  handshake come from PMU's DrumKitModule. The drums themselves are physical models, voiced for
//  an authentic acoustic sound rather than to match PMU:
//
//      snare  two-headed membrane model with snare wires (MembraneModes.hpp, SnareWires.hpp)
//      toms   the same two-headed model without wires, with tension-modulation pitch bend
//      crash  cymbal plate model with the nonlinear cascade (Cymbal.hpp); ride in the GM map
//      hats   cymbal plate model with the two-cymbal contact
//      kick   PMU's additive kick — a swept sine; still to be modelled
//      hand   congas, bongos, timbales: single-headed open drums on the same membrane physics
//      perc   cowbell, agogo, claves, wood blocks, triangle: struck bars and bells on the modal bank
//

#include "DrumKitModule.hpp"
#include "ChokeGroupState.hpp"
#include "MembraneModes.hpp"
#include "Cymbal.hpp"
#include "ModalBank.hpp"
#include "SnareWires.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

// ---- Note -> drum mapping ----

// The drums. Order is not significant; DrumNone marks "voice not yet assigned" (and a key the
// current map leaves silent).
enum DrumVoiceType : int {
    DrumNone = 0,
    DrumKick, DrumKick2,
    DrumSnare, DrumSnare2,            // Snare2 is the rimshot
    DrumTomLow, DrumTomMid, DrumTomHigh,
    DrumCrash1, DrumCrash2,
    DrumHatOpen, DrumHatHalf, DrumHatClosed,
    DrumRide, DrumRideBell,           // General MIDI map only
    DrumCongaLow, DrumCongaHigh, DrumCongaMute,
    DrumBongoLow, DrumBongoHigh,
    DrumTimbaleLow, DrumTimbaleHigh,
    DrumCowbell, DrumClaves, DrumWoodBlockHigh,
    DrumTriangleMute, DrumTriangleOpen,
    DrumAgogoHigh, DrumAgogoLow, DrumWoodBlockLow,   // General MIDI map only
};

enum DrumKeyMap : int { DrumKeyMapStruckKit = 0, DrumKeyMapGM = 1 };

// The three hi-hats choke each other, and so do the two triangles (a mute is the hand closing on
// a ringing triangle). != ChokeGroupState::NoChokeGroup.
static constexpr uint32_t kHatChokeGroup      = 1;
static constexpr uint32_t kTriangleChokeGroup = 2;

// StruckKit map: two octaves that repeat every 24 notes, anchored so C1 (36) and middle C (60) start
// the kit octave: 36-47, 60-71, ... are the kit, 48-59, 72-83, ... the percussion. In both, white
// keys are drums; in the kit the black keys are crashes (C#,D#) and hats (F#,G#,A#), in the
// percussion the small metal and wood instruments.
static inline DrumVoiceType drumForNotePercussion(int pitchClass) {
    switch (pitchClass) {
        case 0:  return DrumCongaLow;     // C   tumba, open tone
        case 1:  return DrumCowbell;      // C#
        case 2:  return DrumCongaHigh;    // D   conga, open tone
        case 3:  return DrumClaves;       // D#
        case 4:  return DrumCongaMute;    // E   conga, muted
        case 5:  return DrumBongoLow;     // F   hembra
        case 6:  return DrumWoodBlockHigh;// F#
        case 7:  return DrumBongoHigh;    // G   macho
        case 8:  return DrumTriangleMute; // G#
        case 9:  return DrumTimbaleLow;   // A
        case 10: return DrumTriangleOpen; // A#
        case 11: return DrumTimbaleHigh;  // B
    }
    return DrumNone;
}

static inline DrumVoiceType drumForNoteStruckKit(uint8_t noteNumber) {
    const int k = ((int)noteNumber + 12) % 24;
    if (k >= 12) return drumForNotePercussion(k - 12);
    switch (k) {
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

// General MIDI percussion map (GM1 key numbers). Kit pieces StruckKit has map to themselves;
// GM's variants map to the nearest piece (a pedal hat is a closed hat, splash and china are the
// smaller and larger crash, side stick is the rimshot). Percussion it has no model for — clap,
// tambourine, maracas, cabasa, guiro, whistles, cuica, vibraslap — stays silent.
static inline DrumVoiceType drumForNoteGM(uint8_t noteNumber) {
    switch (noteNumber) {
        case 35: return DrumKick2;       // Acoustic Bass Drum
        case 36: return DrumKick;        // Bass Drum 1
        case 37: return DrumSnare2;      // Side Stick
        case 38: return DrumSnare;       // Acoustic Snare
        case 40: return DrumSnare;       // Electric Snare
        case 41: return DrumTomLow;      // Low Floor Tom
        case 42: return DrumHatClosed;   // Closed Hi-Hat
        case 43: return DrumTomLow;      // High Floor Tom
        case 44: return DrumHatClosed;   // Pedal Hi-Hat
        case 45: return DrumTomMid;      // Low Tom
        case 46: return DrumHatOpen;     // Open Hi-Hat
        case 47: return DrumTomMid;      // Low-Mid Tom
        case 48: return DrumTomHigh;     // Hi-Mid Tom
        case 49: return DrumCrash1;      // Crash Cymbal 1
        case 50: return DrumTomHigh;     // High Tom
        case 51: return DrumRide;        // Ride Cymbal 1
        case 52: return DrumCrash1;      // Chinese Cymbal
        case 53: return DrumRideBell;    // Ride Bell
        case 55: return DrumCrash2;      // Splash Cymbal
        case 57: return DrumCrash2;      // Crash Cymbal 2
        case 56: return DrumCowbell;     // Cowbell
        case 59: return DrumRide;        // Ride Cymbal 2
        case 60: return DrumBongoHigh;   // Hi Bongo
        case 61: return DrumBongoLow;    // Low Bongo
        case 62: return DrumCongaMute;   // Mute Hi Conga
        case 63: return DrumCongaHigh;   // Open Hi Conga
        case 64: return DrumCongaLow;    // Low Conga
        case 65: return DrumTimbaleHigh; // High Timbale
        case 66: return DrumTimbaleLow;  // Low Timbale
        case 67: return DrumAgogoHigh;   // High Agogo
        case 68: return DrumAgogoLow;    // Low Agogo
        case 75: return DrumClaves;      // Claves
        case 76: return DrumWoodBlockHigh; // Hi Wood Block
        case 77: return DrumWoodBlockLow;  // Low Wood Block
        case 80: return DrumTriangleMute;  // Mute Triangle
        case 81: return DrumTriangleOpen;  // Open Triangle
        default: return DrumNone;
    }
}

static inline DrumVoiceType drumForNote(uint8_t noteNumber, int keyMap) {
    return (keyMap == DrumKeyMapGM) ? drumForNoteGM(noteNumber) : drumForNoteStruckKit(noteNumber);
}

// Choke group a drum belongs to. Only the hats and the triangles choke; everything else rings.
static inline uint32_t chokeGroupForDrum(DrumVoiceType drum) {
    switch (drum) {
        case DrumHatOpen:
        case DrumHatHalf:
        case DrumHatClosed:
            return kHatChokeGroup;
        case DrumTriangleMute:
        case DrumTriangleOpen:
            return kTriangleChokeGroup;
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

// ---- Filters ----

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

// ---- Knob mapping ----

// Map a 0..1 knob to a real range (linear or log).
static inline double mapLin(float v, double lo, double hi) { return lo + (double)v * (hi - lo); }
static inline double mapExp(float v, double lo, double hi) { return lo * std::pow(hi / lo, (double)v); }

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

    // Noise bandpass filter states (stick tick / rim crack).
    double   nz1 = 0.0, nz2 = 0.0;
    double   nz3 = 0.0, nz4 = 0.0;

    // Toms: the head's dense upper modes, as band-split noise (see renderTom).
    static constexpr int kResidualBands = 8;
    double   residZ[kResidualBands][2] = {};


    // Modal body: this voice's resonator, the per-strike mode table it rings, and the strike
    // burst that excites it. The bank is NOT cleared on a re-strike of the same drum: a new hit
    // adds energy to a head that is still ringing, as a real one does.
    ModalBank     bank;
    ModalMode     bodyModes[ModalBank::kMaxModes] = {};
    ModalMaterial bodyMaterial { bodyModes, 0, "DrumBody" };
    DrumVoiceType bankDrum    = DrumNone;   // drum whose ring the bank currently holds
    int           strikePos   = -1;         // sample within the burst; -1 = idle
    int           strikeLen   = 1;
    float         strikeScale = 0.0f;

    // Snare only: a second bank ringing the same strike, observed at the resonant head instead of
    // the batter (just the air-coupled pair has motion there), whose displacement drives the
    // wires. The strike weights are cached per strike radius (they cost a Bessel series each).
    ModalBank     wireBank;
    ModalMode     wireModes[2] = {};
    ModalMaterial wireMaterial { wireModes, 2, "SnareHead" };
    SnareWires    wires;
    double        cachedRho = -1.0;
    double        strikeWeights[kMembraneIndexCount] = {};

    // Timbales: the metal shell's ring modes, on wireBank (unused by every drum but the snare).
    ModalMode     shellModes[8] = {};
    ModalMaterial shellMaterial { shellModes, 0, "TimbaleShell" };

    // Crashes and hi-hats: the plate model (see Cymbal.hpp). Rebuilt only when its configuration
    // changes; like the drum banks it keeps ringing through a re-strike of the same cymbal.
    Cymbal        cymbal;
};

static inline void resetVoiceDSP(DrumKitState* st) {
    st->phase = 0.0;
    st->nz1 = st->nz2 = st->nz3 = st->nz4 = 0.0;
    for (auto& z : st->residZ) { z[0] = z[1] = 0.0; }
    st->chokeGain = 1.0f;
}

// ---- Modal bodies ----

static constexpr int    kModalSubBlock = 32;     // pitch-glide update granularity, samples
static constexpr double kLn1000        = 6.907755278982137;   // T60 = tau * ln(1000)
static constexpr float  kModalSpread   = 0.3f;   // modes fanned a little across the field
static constexpr float  kModalPanComp  = 1.41421356f;   // undo the -3 dB of equal-power centre pan
static constexpr float  kModalSilence  = 2.0e-5f;       // bank state below this = rung out

// Strike contact time. A hard hit is a near-impulse (flat spectrum); a soft one is a longer
// Hann bump that rolls the upper modes off — velocity changes timbre, not just level, which the
// additive bodies could not do. A snare stick's tip on a tight head is a briefer contact than on a
// tom's looser one (set by ear, ~0.25-1 ms vs ~0.3-1.2 ms); a rimshot drives the stick into head and rim at once, so it
// is always hard.
// A rimshot also lands harder: the stick is driven into head and rim together, with the full arm
// behind it (force set by ear).
static inline void contactRange(DrumVoiceType drum, double& hardSec, double& softSec, double& force) {
    force = 1.0;
    switch (drum) {
        case DrumSnare:  hardSec = 0.00025; softSec = 0.0010; break;
        case DrumSnare2: hardSec = 0.00025; softSec = 0.00025; force = 2.3; break;
        // Hands are soft and heavy next to a stick: fingers on a conga stay in contact a few ms,
        // the bongo's quick finger snap less. Timbales, cowbell, agogo and blocks are stick on
        // something hard; claves are wood on wood, the triangle a steel beater on steel (set by ear).
        case DrumCongaLow: case DrumCongaHigh: case DrumCongaMute:
                         hardSec = 0.0006;  softSec = 0.0025; break;
        case DrumBongoLow: case DrumBongoHigh:
                         hardSec = 0.0004;  softSec = 0.0015; break;
        case DrumCowbell: case DrumAgogoHigh: case DrumAgogoLow:
        case DrumWoodBlockHigh: case DrumWoodBlockLow:
                         hardSec = 0.00015; softSec = 0.0006; break;
        case DrumClaves: hardSec = 0.00012; softSec = 0.0003; break;
        case DrumTriangleMute: case DrumTriangleOpen:
                         hardSec = 0.00005; softSec = 0.00015; break;
        default:         hardSec = 0.0003;  softSec = 0.0012; break;   // toms, timbales: stick on a head
    }
}

// Start the strike burst. Its area is velAmp (times the drum's force), so the bank's low modes are
// driven in proportion to velocity; contact length only shapes the highs.
static inline void startStrike(DrumKitState* st) {
    double hardSec, softSec, force;
    contactRange(st->drum, hardSec, softSec, force);
    const double contact = softSec + (hardSec - softSec) * (double)st->velAmp;
    st->strikeLen   = std::max(1, (int)std::lround(contact * st->sampleRate));
    const double area = (st->strikeLen <= 1) ? 1.0 : 0.5 * (double)st->strikeLen;
    st->strikeScale = (float)((double)st->velAmp * force / area);
    st->strikePos   = 0;
}

// Next n samples of the strike burst (zeros once it is spent).
static inline void renderStrike(DrumKitState* st, float* ex, int n) {
    for (int i = 0; i < n; ++i) {
        float v = 0.0f;
        if (st->strikePos >= 0) {
            const int L = st->strikeLen;
            const float w = (L <= 1)
                ? 1.0f
                : 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * ((float)st->strikePos + 0.5f) / (float)L));
            v = st->strikeScale * w;
            if (++st->strikePos >= L) { st->strikePos = -1; }
        }
        ex[i] = v;
    }
}

// ---- Drum heads ----

// Fill the body bank's mode table for a two-headed drum observed at the batter: the (0,1) mode
// replaced by the pair it forms with the resonant head through the enclosed air (MembraneModes.hpp,
// coupleHeads), then every other batter mode, each weighted by its shape at the strike radius.
// Ratios are relative to the batter's in-vacuo (0,1) frequency, so the bank's fundamental is that
// frequency and the pair lands wherever the coupling puts it.
//
// m = 0 modes move the head like a piston and radiate as monopoles, so they are louder in the
// sound (monopoleGain) and lose their energy to the air sooner (axisymDamp, a T60 factor). Each
// pair mode can carry an extra T60 factor (e.g. snare wires damping the resonant head). Returns the
// table's total squared amplitude, which ModalBank normalises away.
static inline void cacheStrikeWeights(DrumKitState* st, double rho) {
    if (rho == st->cachedRho) return;
    for (int k = 0; k < kMembraneIndexCount; ++k) {
        st->strikeWeights[k] = Membrane::strikeWeight(kMembraneIndex[k], rho);
    }
    st->cachedRho = rho;
}

static double fillHeadModes(DrumKitState* st, double rho, const Membrane::CoupledMode pair[2],
                            const double pairExtraDamp[2], double monopoleGain, double axisymDamp) {
    cacheStrikeWeights(st, rho);
    const double j01 = kMembraneIndex[0].j;
    double energy = 0.0;
    int nb = 0;
    for (int i = 0; i < 2; ++i) {
        const double amp = st->strikeWeights[0] * pair[i].b * pair[i].b * monopoleGain;
        st->bodyModes[nb++] = { (float)pair[i].ratio, (float)amp, (float)(axisymDamp * pairExtraDamp[i]) };
        energy += amp * amp;
    }
    for (int k = 1; k < kMembraneIndexCount && nb < ModalBank::kMaxModes; ++k) {
        const MembraneModeIndex& m = kMembraneIndex[k];
        const double amp = st->strikeWeights[k] * ((m.m == 0) ? monopoleGain : 1.0);
        st->bodyModes[nb++] = { (float)(m.j / j01), (float)amp, (float)((m.m == 0) ? axisymDamp : 1.0) };
        energy += amp * amp;
    }
    st->bodyMaterial.count = nb;
    return energy;
}

// A single-headed drum open at the bottom (conga, bongo, timbale): no enclosed air, so no coupled
// pair; every mode is the batter's own. The (0,1) mode is the one that pumps air down the shell
// and out of the open end, so it loses its energy quickly (axisymDamp), which is why such a drum's
// pitch is heard in its (1,1) mode rather than its lowest.
static double fillSingleHeadModes(DrumKitState* st, double rho, double monopoleGain, double axisymDamp) {
    cacheStrikeWeights(st, rho);
    double energy = 0.0;
    int nb = 0;
    for (int k = 0; k < kMembraneIndexCount && nb < ModalBank::kMaxModes; ++k) {
        const MembraneModeIndex& m = kMembraneIndex[k];
        const double amp = st->strikeWeights[k] * ((m.m == 0) ? monopoleGain : 1.0);
        st->bodyModes[nb++] = { (float)(m.j / kMembraneIndex[0].j), (float)amp, (float)((m.m == 0) ? axisymDamp : 1.0) };
        energy += amp * amp;
    }
    st->bodyMaterial.count = nb;
    return energy;
}

// ---- Residual: the head's modes above the table ----
//
// The mode table stops at ~7.5 f0 (40 modes, the bank holds 64), but a head keeps having modes all
// the way up, and ever more of them: by Weyl's law the count below ratio r grows as r^2, here
// (j01 r)^2 / 8 (half the full count, since a strike on one diameter drives only the cos(m theta)
// twin of each degenerate pair; that gives the table's 40 at 7.5). Without them a drum is a cluster
// of low sines with nothing above ~1-2 kHz, the "pong" of a lowpassed synth tom.
//
// Up there the modes are too dense to hear one by one, so they are rendered as what their sum
// sounds like: noise, split into bands, each band decaying at the bank's own damping law for its
// frequency (T60 r^-tilt) and as loud as the modes it stands for. Its level follows from the table
// rather than by ear: a band holding dN modes, each ringing at the amplitude of the table's top
// modes (A^2 = mean top amp^2 / table energy, as ModalBank normalises), has variance A^2 dN / 2;
// dN per Hz is j01^2 r / (4 f0); and the strike's contact spectrum (a Hann bump, so a soft hit's
// longer contact darkens it, as in the bank) and force scale it the same way they scale the modes.

static constexpr double kResidualTopHz = 12000.0;
static constexpr int    kResidualTopModes = 12;   // table modes averaged for the top amplitude
// Two departures from the table's physics up here, both set by ear. Radiation: the head's waves
// (34-72 m/s) are far slower than sound, so a high mode's many out-of-phase patches largely cancel
// in the air; the table, all ~1 radiators, ignores this, and down there it barely matters. Damping:
// the head's losses climb faster with frequency than the table's tilt, so a stick's bright attack is
// gone within a few tens of milliseconds.
static constexpr double kResidualRadiation = 0.35;  // amplitude factor
static constexpr double kResidualDampTilt  = 1.6;   // T60 ~ r^-tilt above the table's top

// Magnitude spectrum of a Hann contact of length T at frequency f, normalised to 1 at DC.
static inline double hannContactGain(double f, double T) {
    const double x = f * T;
    if (x < 1e-6) return 1.0;
    if (std::fabs(x - 1.0) < 1e-6) return 0.5;
    return std::fabs(std::sin(M_PI * x) / (M_PI * x * (1.0 - x * x)));
}

// Plan the residual bands for this block: log-spaced from the table's top to kResidualTopHz.
// Returns the number of bands (0 when the table already reaches the top).
static int setupResidual(DrumKitState* st, double f0, double tableEnergy, double t60, double dampTilt,
                         BiquadBP bp[], double gain[], double tau[]) {
    const double sr    = st->sampleRate;
    const double j01   = kMembraneIndex[0].j;
    const double rTop  = kMembraneIndex[kMembraneIndexCount - 1].j / j01;
    const double fLo   = f0 * rTop;
    const double fHi   = std::min(kResidualTopHz, 0.45 * sr);
    const int    nb    = st->bodyMaterial.count;
    if (fLo * 1.1 >= fHi || tableEnergy <= 0.0 || nb < kResidualTopModes) return 0;

    // m >= 1 modes only: the residual is almost all of them, and the table's m = 0 entries carry
    // the monopole boost (their damping factor is the one that differs from 1).
    double topAmp2 = 0.0;
    int    topCount = 0;
    for (int i = nb - 1; i >= 0 && topCount < kResidualTopModes; --i) {
        if (st->bodyModes[i].damp != 1.0f) continue;
        topAmp2 += (double)st->bodyModes[i].amp * st->bodyModes[i].amp;
        ++topCount;
    }
    if (topCount == 0) return 0;
    const double modeAmp2 = topAmp2 / topCount / tableEnergy * kResidualRadiation * kResidualRadiation;

    const double contact = (double)st->strikeLen / sr;
    const double drive   = (double)st->strikeScale * (st->strikeLen <= 1 ? 1.0 : 0.5 * (double)st->strikeLen);
    const int    n       = DrumKitState::kResidualBands;
    const double bwOct   = std::log2(fHi / fLo) / n;
    const double q       = std::sqrt(std::exp2(bwOct)) / (std::exp2(bwOct) - 1.0);
    // Uniform noise in [-1, 1) has variance 1/3; through the 0 dB-peak bandpass, about pi B / (3 sr).
    for (int k = 0; k < n; ++k) {
        const double fc  = fLo * std::exp2(bwOct * (k + 0.5));
        const double bw  = fc / q;
        const double r   = fc / f0;
        const double dN  = j01 * j01 * r * bw / (4.0 * f0);
        const double var = modeAmp2 * dN * 0.5;
        // The contact spectrum, averaged across the band (it has nulls a single point could hit).
        double h = 0.0;
        for (int p = 0; p < 5; ++p) h += hannContactGain(fc * std::exp2(bwOct * (p - 2) / 4.0), contact);
        bp[k]   = makeBandpass(fc, q, sr);
        gain[k] = std::sqrt(var / (M_PI * bw / (3.0 * sr))) * (h / 5.0) * drive;
        // The table's tilt law up to its top, steeper beyond it.
        tau[k]  = t60 * std::pow(rTop, -dampTilt) * std::pow(r / rTop, -kResidualDampTilt) / kLn1000;
    }
    return n;
}

// One sample of the residual at time t after the strike, rising over the contact like the force.
static inline double renderResidual(DrumKitState* st, int n, const BiquadBP bp[], const double gain[],
                                    const double tau[], double t, double contactSec) {
    if (n == 0) return 0.0;
    const double x    = (double)lcgBipolar(st->rng);
    const double rise = (contactSec > 0.0) ? std::min(1.0, t / contactSec) : 1.0;
    double y = 0.0;
    for (int k = 0; k < n; ++k) {
        const double env = std::exp(-t / tau[k]);
        if (env < 1e-5) continue;   // bands are ordered by frequency, but tau need not be monotone
        y += biquadTick(bp[k], x, st->residZ[k][0], st->residZ[k][1]) * gain[k] * env;
    }
    return y * rise;
}

// ---- Snare ----
//
// A two-headed drum with wires on the bottom head, built from the physics in MembraneModes.hpp
// and SnareWires.hpp rather than voiced to match anything:
//
//   batter head   the ideal membrane's modes up to ~7.5 f0, each driven by its mode shape at the
//                 strike radius (centre = round and dark, out towards the edge = ring and
//                 overtones);
//   air + bottom  the (0,1) mode is replaced by the pair it forms with the resonant head through
//                 the enclosed air, and that pair is what moves the wires;
//   wires         a contact model on the resonant head's motion: they rattle in step with it,
//                 harder with a harder hit, and stop on their own when the head can no longer
//                 lift them;
//   stick         the stick's contact time follows velocity, and a faint broadband tick covers
//                 the head's modes above the table and the stick's own impact.
//
// E is a normal stroke at the Position knob's radius; F is a rimshot: struck at the edge, always
// a hard contact, with the stick's crack on the rim.

// Set by ear (see coupleHeads): the snare-side head is thinner than the batter and conventionally
// tuned well above it; a shallow shell makes the air spring stiff.
static constexpr double kSnareResonantRatio = 1.5;    // wr / wb
static constexpr double kSnareResonantMass  = 0.6;    // mu
static constexpr double kSnareAirSpring     = 0.5;    // ka / wb^2

// Radiation. Axisymmetric (m = 0) modes move the head like a piston and radiate as monopoles;
// every m >= 1 mode has as much head moving out as in, so heard from any distance it partly
// cancels itself (dipole, quadrupole, ...). That is why a drum's low end is its m = 0 modes. Two
// consequences, both set by ear in size: m = 0 modes are louder in the sound than their share of
// the head's motion, and they lose their energy to the air sooner (the short life of a drum's
// (0,1) "thud").
static constexpr double kSnareMonopoleGain = 2.2;
static constexpr double kSnareAxisymDamp   = 0.7;
// Set by ear: higher modes lose energy faster in general; wires pressing on the resonant head
// damp whatever moves it, in proportion to that head's share of the mode's energy (mu s^2), more
// so the tighter they are.
static constexpr float  kSnareDampTilt     = 0.8f;
static inline double snareWireDamping(double tension) { return 0.3 + 0.9 * tension; }

static constexpr double kSnareRimshotRho  = 0.92;
static constexpr double kSnarePitchDrop   = 0.04;   // tension modulation at a full-velocity hit
static constexpr double kSnarePitchTau    = 0.025;
static constexpr double kSnareBodyLevel   = 0.42;
static constexpr double kSnareWireLevel   = 0.65;   // at Wires = 1
static constexpr double kStickTickTau     = 0.0004; // s
static constexpr double kStickTickLevel   = 0.10;
static constexpr double kRimCrackTau      = 0.003;  // s
static constexpr double kRimCrackLevel    = 0.40;

static void renderSnare(DrumKitState* st, Module* module, float level,
                        float* outL, float* outR, uint32_t count) {
    const double sr      = st->sampleRate;
    const bool   rimshot = (st->drum == DrumSnare2);

    // --- controls ---
    const double f0      = mapExp(module->getParamValue(DrumKitParamItemSnareTune), 150.0, 300.0);  // batter (0,1), in vacuo
    const double t60     = mapExp(module->getParamValue(DrumKitParamItemSnareRing), 0.12, 1.5);
    const double pos     = (double)module->getParamValue(DrumKitParamItemSnarePosition);
    const float  tension = module->getParamValue(DrumKitParamItemSnareTension);
    const double wiresLv = mapLin(module->getParamValue(DrumKitParamItemSnareWires), 0.0, kSnareWireLevel);
    const double rho     = rimshot ? kSnareRimshotRho : 0.05 + 0.85 * pos;

    // --- mode tables ---
    Membrane::CoupledMode pair[2];
    Membrane::coupleHeads(kSnareResonantRatio, kSnareResonantMass, kSnareAirSpring, pair);
    const double wireDamp = snareWireDamping(tension);
    double pairDamp[2];
    for (int i = 0; i < 2; ++i) {
        pairDamp[i] = 1.0 / (1.0 + wireDamp * kSnareResonantMass * pair[i].s * pair[i].s);
    }
    const double bodyEnergy = fillHeadModes(st, rho, pair, pairDamp, kSnareMonopoleGain, kSnareAxisymDamp);
    const int    nb         = st->bodyMaterial.count;

    // The wires see the resonant head: just the coupled pair moves it.
    for (int i = 0; i < 2; ++i) {
        st->wireModes[i] = { (float)pair[i].ratio, (float)(st->strikeWeights[0] * pair[i].b * pair[i].s),
                             (float)(kSnareAxisymDamp * pairDamp[i]) };
    }

    // ModalBank normalises each bank's amplitudes to unit energy. The body should be normalised
    // (position is timbre, not level), but the wire drive must keep its size RELATIVE to the body,
    // or the separation threshold would mean different things at different strike radii.
    double wireEnergy = 0.0;
    for (int i = 0; i < 2; ++i) wireEnergy += (double)st->wireModes[i].amp * st->wireModes[i].amp;
    const double wireScale = (bodyEnergy > 0.0) ? std::sqrt(wireEnergy / bodyEnergy) : 0.0;
    const double headScale = wireScale * 1.41421356;   // undo the wire bank's centre pan (spread 0)

    for (ModalBank* b : { &st->bank, &st->wireBank }) {
        b->invalidate();   // tables change under stable pointers
        b->setDecayT60((float)t60);
        b->setDampTilt(kSnareDampTilt);
        b->setBrightness(0.0f);
    }
    st->bank.setMaterial(st->bodyMaterial);
    st->bank.setModeCount(nb);
    st->wireBank.setMaterial(st->wireMaterial);
    st->wireBank.setModeCount(2);
    st->wires.setTension(tension);

    // --- render ---
    const float bodyGain = (float)(kSnareBodyLevel * kOutScale) * level * kModalPanComp;
    const float wireGain = (float)(wiresLv * kOutScale) * level;
    const BiquadBP tickBP = makeBandpass(rimshot ? 3200.0 : 5000.0, rimshot ? 1.4 : 0.6, sr);
    const double   tickTau   = rimshot ? kRimCrackTau : kStickTickTau;
    const double   tickLevel = rimshot ? kRimCrackLevel : kStickTickLevel;
    const double   drop      = kSnarePitchDrop * (double)st->velAmp * (double)st->velAmp;

    float ex[kModalSubBlock], bL[kModalSubBlock], bR[kModalSubBlock], wL[kModalSubBlock], wR[kModalSubBlock];
    double t = (double)st->agePos / sr;

    for (uint32_t base = 0; base < count; base += kModalSubBlock) {
        const int n = (int)std::min<uint32_t>(kModalSubBlock, count - base);
        const double tMid  = ((double)st->agePos + 0.5 * n) / sr;
        const float  fNow  = (float)(f0 * (1.0 + drop * std::exp(-tMid / kSnarePitchTau)));
        st->bank.setFundamental(fNow);
        st->wireBank.setFundamental(fNow);

        renderStrike(st, ex, n);
        st->bank.processBlock(ex, bL, bR, n);
        st->wireBank.processBlock(ex, wL, wR, n);

        for (int i = 0; i < n; ++i) {
            t = (double)st->agePos / sr;
            const double head = (double)wL[i] * headScale;
            const float  wire = st->wires.process(head) * wireGain;

            double tick = 0.0;
            if (t < 8.0 * tickTau) {
                tick = biquadTick(tickBP, lcgBipolar(st->rng), st->nz1, st->nz2)
                     * std::exp(-t / tickTau) * tickLevel * st->velAmp;
            }
            const float hit = (float)(tick * level * kOutScale);

            outL[base + i] = bL[i] * bodyGain + wire + hit;
            outR[base + i] = bR[i] * bodyGain + wire + hit;
            st->agePos++;
        }
    }

    if (st->strikePos < 0 && t > 0.3 && st->bank.isSilent(kModalSilence)
        && st->wireBank.isSilent(kModalSilence) && st->wires.isQuiet()) {
        st->playing = false;
    }
}

// ---- Toms ----
//
// The snare's two-headed drum without the wires (see Drum heads above): batter modes driven by
// their shapes at the strike radius, the (0,1) pair formed with the resonant head through the air,
// and monopole radiation. What makes a tom a tom rather than a pitched snare:
//
//   size     a 16" floor tom, 12" and 10" rack toms (G / A / B). An ideal membrane's frequency is
//            (wave speed) / radius, so one Tune knob (the heads' tension) moves all three together
//            and keeps their size-given intervals; drummers do tune smaller toms relatively tighter,
//            which the per-size tension offsets reproduce (set by ear);
//   shell    deep shells hold more air, so the air spring between the heads is softer than a
//            snare's — weaker coupling, a closer pair, longer sustain;
//   reso     the resonant (bottom) head's tuning against the batter, a real drummer's control:
//            near unison the two heads trade energy and sustain longest; apart, the pair splits
//            and the note gets shorter and more "tuned";
//   bend     the head's tension rises with how far it is stretched, so a hard stroke starts sharp
//            and falls as the vibration dies — the tom's "doom". Tension modulation goes as the
//            square of the amplitude, so the bend scales with velocity^2 and decays at twice the
//            rate the (0,1) pair does.

struct TomSize { double radiusInch; double tensionOffset; double airSpring; double ringScale; };
static inline TomSize tomSize(DrumVoiceType drum) {
    //                                    radius  tension  air spring  ring (set by ear)
    if (drum == DrumTomLow) return {        8.0,    1.00,     0.18,     1.4 };   // 16" floor
    if (drum == DrumTomMid) return {        6.0,    1.08,     0.24,     1.0 };   // 12" rack
    return                         {        5.0,    1.18,     0.28,     0.8 };   // 10" rack
}

// Effective wave speed (m/s) on the heads, from Tune: f(0,1) = 2.405 c / (2 pi a). "Effective"
// because a real head also has to move the air on both sides of it, which lowers its modes a
// great deal (most of all the low ones); the range is set by ear to land typical tunings, the 16"
// floor tom's (0,1) at about 65 .. 135 Hz and the 10" at about 125 .. 255 Hz.
static constexpr double kTomWaveSpeedLo   = 34.0;
static constexpr double kTomWaveSpeedHi   = 72.0;
static constexpr double kTomResonantMass  = 1.0;    // same weight of head top and bottom
static constexpr double kTomMonopoleGain  = 2.8;
// The batter head is the damped one (the stick, any muffling: what Ring sets); the resonant head
// rings almost free. A mode loses energy from each head in proportion to its share of energy there
// (b^2 batter, mu s^2 resonant), so modes that put more of themselves into the resonant head last
// longer — which is why a tom rings longest with its heads tuned near each other (set by ear).
static constexpr double kTomResoSustain   = 3.0;    // resonant head's T60 relative to the batter's
static constexpr double kTomAxisymDamp    = 0.6;
static constexpr float  kTomDampTilt      = 0.6f;
static constexpr double kTomBend          = 0.07;   // fractional pitch rise of a full stroke (set by ear)
static constexpr double kTomBodyLevel     = 0.42;
static constexpr double kTomTickTau       = 0.0005;
static constexpr double kTomTickLevel     = 0.06;

static void renderTom(DrumKitState* st, Module* module, float level,
                      float* outL, float* outR, uint32_t count) {
    const double  sr   = st->sampleRate;
    const TomSize size = tomSize(st->drum);

    // --- controls ---
    const double c    = mapExp(module->getParamValue(DrumKitParamItemTomTune), kTomWaveSpeedLo, kTomWaveSpeedHi)
                      * size.tensionOffset;
    const double a    = size.radiusInch * 0.0254;
    const double f0   = 2.40483 * c / (2.0 * M_PI * a);          // batter (0,1), in vacuo
    const double t60  = mapExp(module->getParamValue(DrumKitParamItemTomRing), 0.25, 2.5) * size.ringScale;
    const double rho  = 0.05 + 0.85 * (double)module->getParamValue(DrumKitParamItemTomPosition);
    const double reso = mapExp(module->getParamValue(DrumKitParamItemTomReso), 0.75, 1.5);   // wr / wb

    // --- mode table ---
    Membrane::CoupledMode pair[2];
    Membrane::coupleHeads(reso, kTomResonantMass, size.airSpring, pair);
    double resoSustain[2];
    for (int i = 0; i < 2; ++i) {
        const double shareBatter = pair[i].b * pair[i].b;
        const double shareReso   = kTomResonantMass * pair[i].s * pair[i].s;
        // Loss rates add: 1 / T60 = share_b / T60_b + share_r / (kTomResoSustain T60_b).
        resoSustain[i] = 1.0 / (shareBatter + shareReso / kTomResoSustain);
    }
    const double bodyEnergy = fillHeadModes(st, rho, pair, resoSustain, kTomMonopoleGain, kTomAxisymDamp);
    st->bank.setMaterial(st->bodyMaterial);
    st->bank.invalidate();                      // table contents change under a stable pointer
    st->bank.setModeCount(st->bodyMaterial.count);
    st->bank.setDecayT60((float)t60);
    st->bank.setDampTilt(kTomDampTilt);
    st->bank.setBrightness(0.0f);

    // Bend: amplitude^2 of the (0,1) pair decays with time constant T60 / (2 ln 1000).
    const double pairT60 = t60 * kTomAxisymDamp * std::pow(pair[0].ratio, -(double)kTomDampTilt);
    const double bendTau = pairT60 / (2.0 * kLn1000);
    const double bend    = kTomBend * (double)st->velAmp * (double)st->velAmp;

    const float    bodyGain = (float)(kTomBodyLevel * kOutScale) * level * kModalPanComp;
    const BiquadBP tickBP   = makeBandpass(4000.0, 0.7, sr);

    // Upper modes, above the table (see Residual above).
    BiquadBP residBP[DrumKitState::kResidualBands];
    double   residGain[DrumKitState::kResidualBands], residTau[DrumKitState::kResidualBands];
    const int nr = setupResidual(st, f0, bodyEnergy, t60, (double)kTomDampTilt, residBP, residGain, residTau);
    const double residScale = kTomBodyLevel * kOutScale * (double)level;
    const double contactSec = (double)st->strikeLen / sr;

    float ex[kModalSubBlock], bL[kModalSubBlock], bR[kModalSubBlock];
    double t = (double)st->agePos / sr;

    for (uint32_t base = 0; base < count; base += kModalSubBlock) {
        const int n = (int)std::min<uint32_t>(kModalSubBlock, count - base);
        const double tMid = ((double)st->agePos + 0.5 * n) / sr;
        st->bank.setFundamental((float)(f0 * (1.0 + bend * std::exp(-tMid / bendTau))));

        renderStrike(st, ex, n);
        st->bank.processBlock(ex, bL, bR, n);

        for (int i = 0; i < n; ++i) {
            t = (double)st->agePos / sr;
            double tick = 0.0;
            if (t < 8.0 * kTomTickTau) {
                tick = biquadTick(tickBP, lcgBipolar(st->rng), st->nz1, st->nz2)
                     * std::exp(-t / kTomTickTau) * kTomTickLevel * st->velAmp;
            }
            const float hit = (float)(tick * level * kOutScale
                                      + renderResidual(st, nr, residBP, residGain, residTau, t, contactSec) * residScale);
            outL[base + i] = bL[i] * bodyGain + hit;
            outR[base + i] = bR[i] * bodyGain + hit;
            st->agePos++;
        }
    }

    // The residual's slowest band lives far shorter than the (0,1) pair, so the bank decides.
    if (st->strikePos < 0 && t > 0.1 && st->bank.isSilent(kModalSilence)) { st->playing = false; }
}

// ---- Hand drums: congas, bongos, timbales ----
//
// Single-headed drums open at the bottom (see fillSingleHeadModes): the head's own modes, the
// residual above them, and the tension-modulation bend the toms have. What tells them apart:
//
//   congas    tall staves, rawhide-like heads (high-frequency loss: a steep damping tilt), played
//             with the fingers near the edge, so the m >= 1 modes carry the tone. The mute is the
//             open conga with the hand left on the head: everything dies within ~0.1 s;
//   bongos    small and tight, the same hand on a smaller head: higher, shorter, snappier;
//   timbales  thin plastic heads on metal shells, played with sticks: a longer ring, a gentler
//             tilt, and the shell itself rings (a handful of inharmonic shell modes on wireBank).
//
// Each family's Tune knob is head tension, moving its drums together and keeping their sizes'
// interval. The ranges put typical tunings' open tones (the (1,1) mode, ~1.59 f0) at: tumba
// ~150-220 Hz, conga ~215-320, hembra ~350-510, macho ~490-720, timbales ~270-510 (set by ear).

struct HandDrumSpec {
    double f0Lo, f0Hi;      // batter (0,1) in vacuo across the Tune knob, Hz
    double rho;             // strike radius
    double ringScale;       // T60 relative to Hand Ring's
    float  tilt;            // damping tilt
    double monopoleGain;
    double axisymDamp;
    double bend;            // fractional pitch rise of a full stroke
    double level;
    double shellHz;         // 0 = no shell ring (timbales only)
};

static inline HandDrumSpec handDrumSpec(DrumVoiceType drum) {
    switch (drum) {
        //                                 f0 range      rho   ring  tilt  mono  axis  bend   level shell
        case DrumCongaLow:    return {  95.0, 140.0,  0.82,  1.3,  1.0f, 2.0,  0.3,  0.03,  0.47,    0.0 };
        case DrumCongaMute:   return { 135.0, 200.0,  0.78,  0.08, 1.6f, 2.0,  0.3,  0.02,  0.85,    0.0 };
        case DrumBongoLow:    return { 220.0, 320.0,  0.88,  0.7,  0.9f, 1.6,  0.35, 0.04,  0.65,    0.0 };
        case DrumBongoHigh:   return { 310.0, 450.0,  0.88,  0.6,  0.9f, 1.6,  0.35, 0.04,  0.72,    0.0 };
        case DrumTimbaleLow:  return { 170.0, 250.0,  0.60,  1.4,  0.5f, 2.0,  0.4,  0.05,  0.33,  950.0 };
        case DrumTimbaleHigh: return { 220.0, 320.0,  0.60,  1.3,  0.5f, 2.0,  0.4,  0.05,  0.33, 1080.0 };
        default:              return { 135.0, 200.0,  0.82,  1.2,  1.0f, 2.0,  0.3,  0.03,  0.63,    0.0 };   // conga
    }
}

static inline bool isHandDrum(DrumVoiceType d) {
    return d == DrumCongaLow || d == DrumCongaHigh || d == DrumCongaMute
        || d == DrumBongoLow || d == DrumBongoHigh || d == DrumTimbaleLow || d == DrumTimbaleHigh;
}

static inline DrumKitParamItem handTuneParam(DrumVoiceType d) {
    if (d == DrumBongoLow || d == DrumBongoHigh)     return DrumKitParamItemBongoTune;
    if (d == DrumTimbaleLow || d == DrumTimbaleHigh) return DrumKitParamItemTimbaleTune;
    return DrumKitParamItemCongaTune;
}

// A thin metal shell's ring modes, relative to its lowest. APPROXIMATE: the shape of the series
// (inharmonic, rising a little faster than linear) is a cylindrical shell's, but the values and
// amplitudes are set by ear, pending measurements of a real timbale.
static constexpr ModalMode kTimbaleShellModes[] = {
    { 1.00f, 1.00f, 1.0f }, { 1.47f, 0.80f, 1.0f }, { 2.09f, 0.60f, 1.0f },
    { 2.81f, 0.45f, 1.0f }, { 3.62f, 0.30f, 1.0f }, { 4.55f, 0.20f, 1.0f },
};
static constexpr int    kTimbaleShellCount = (int)(sizeof(kTimbaleShellModes) / sizeof(kTimbaleShellModes[0]));
static constexpr double kTimbaleShellLevel = 0.12;   // against the head (set by ear)
static constexpr double kTimbaleShellT60   = 1.4;    // at Hand Ring's centre

static void renderHandDrum(DrumKitState* st, Module* module, float level,
                           float* outL, float* outR, uint32_t count) {
    const double       sr   = st->sampleRate;
    const HandDrumSpec spec = handDrumSpec(st->drum);

    // --- controls ---
    const double f0       = mapExp(module->getParamValue(handTuneParam(st->drum)), spec.f0Lo, spec.f0Hi);
    const float  ringKnob = module->getParamValue(DrumKitParamItemHandRing);
    const double t60      = mapExp(ringKnob, 0.3, 2.5) * spec.ringScale;

    // --- head ---
    const double bodyEnergy = fillSingleHeadModes(st, spec.rho, spec.monopoleGain, spec.axisymDamp);
    st->bank.setMaterial(st->bodyMaterial);
    st->bank.invalidate();
    st->bank.setModeCount(st->bodyMaterial.count);
    st->bank.setDecayT60((float)t60);
    st->bank.setDampTilt(spec.tilt);
    st->bank.setBrightness(0.0f);

    // --- shell (timbales) ---
    const bool shell = spec.shellHz > 0.0;
    if (shell) {
        std::copy(std::begin(kTimbaleShellModes), std::end(kTimbaleShellModes), st->shellModes);
        st->shellMaterial.count = kTimbaleShellCount;
        st->wireBank.setMaterial(st->shellMaterial);
        st->wireBank.invalidate();
        st->wireBank.setModeCount(kTimbaleShellCount);
        st->wireBank.setDecayT60((float)(kTimbaleShellT60 * mapExp(ringKnob, 0.5, 2.0)));
        st->wireBank.setDampTilt(0.3f);
        st->wireBank.setBrightness(0.0f);
        st->wireBank.setFundamental((float)spec.shellHz);
    }

    // Bend as the toms': the (1,1) carries the pitch here, so follow its decay.
    const double pitchT60 = t60 * std::pow(kMembraneIndex[1].j / kMembraneIndex[0].j, -(double)spec.tilt);
    const double bendTau  = pitchT60 / (2.0 * kLn1000);
    const double bend     = spec.bend * (double)st->velAmp * (double)st->velAmp;

    const float  bodyGain  = (float)(spec.level * kOutScale) * level * kModalPanComp;
    const float  shellGain = (float)(spec.level * kTimbaleShellLevel * kOutScale) * level * kModalPanComp;

    BiquadBP residBP[DrumKitState::kResidualBands];
    double   residGain[DrumKitState::kResidualBands], residTau[DrumKitState::kResidualBands];
    const int    nr         = setupResidual(st, f0, bodyEnergy, t60, (double)spec.tilt, residBP, residGain, residTau);
    const double residScale = spec.level * kOutScale * (double)level;
    const double contactSec = (double)st->strikeLen / sr;

    float ex[kModalSubBlock], bL[kModalSubBlock], bR[kModalSubBlock], sL[kModalSubBlock], sR[kModalSubBlock];
    double t = (double)st->agePos / sr;

    for (uint32_t base = 0; base < count; base += kModalSubBlock) {
        const int n = (int)std::min<uint32_t>(kModalSubBlock, count - base);
        const double tMid = ((double)st->agePos + 0.5 * n) / sr;
        st->bank.setFundamental((float)(f0 * (1.0 + bend * std::exp(-tMid / bendTau))));

        renderStrike(st, ex, n);
        st->bank.processBlock(ex, bL, bR, n);
        if (shell) st->wireBank.processBlock(ex, sL, sR, n);

        for (int i = 0; i < n; ++i) {
            t = (double)st->agePos / sr;
            const float hit = (float)(renderResidual(st, nr, residBP, residGain, residTau, t, contactSec) * residScale);
            float l = bL[i] * bodyGain + hit, r = bR[i] * bodyGain + hit;
            if (shell) { l += sL[i] * shellGain; r += sR[i] * shellGain; }
            outL[base + i] = l;
            outR[base + i] = r;
            st->agePos++;
        }
    }

    if (st->strikePos < 0 && t > 0.05 && st->bank.isSilent(kModalSilence)
        && (!shell || st->wireBank.isSilent(kModalSilence))) {
        st->playing = false;
    }
}

// ---- Percussion: cowbell, agogo, claves, wood blocks, triangle ----
//
// Struck idiophones: a mode table rung by the strike burst, no head and no residual (their modes
// are sparse enough to hold in the bank). Perc Tune scales all their sizes together (+-5
// semitones), Perc Decay their ring (x0.5 .. x2).
//
//   cowbell, agogo  a bent-sheet bell and a cone bell: two strong low modes about a fifth apart,
//                   the cowbell's "clank", then weaker inharmonic ones. APPROXIMATE, set by ear
//                   pending measurement; the agogo bells use the same series, smaller and freer;
//   claves          a hardwood rod: the free-free bar (ModalMaterials kBarModes), but held so that
//                   only its first mode really rings, and wood kills the rest;
//   wood blocks     a slotted hollow block: one strong cavity-coupled mode and a few weak ones,
//                   very short. APPROXIMATE, set by ear;
//   triangle        a bent steel rod: free-free bar ratios (bL = (2n+1) pi / 2 beyond the first
//                   few roots) out to ~15 kHz, each split into a close pair (the rod bends in and
//                   out of its plane at almost the same frequency, which is the shimmer). A thin
//                   rod radiates its low modes poorly (it is far smaller than their wavelength), so
//                   amplitude rises up the series, as sqrt(ratio) (set by ear): the 180 Hz
//                   fundamental is barely there, and the sound is its 1-10 kHz partials. The mute is the hand closing on it, and chokes an open triangle.

struct PercSpec {
    const ModalMode* modes;   // null = the triangle, generated
    int              count;
    double           f0;      // Hz at Perc Tune centre
    double           t60;     // s at Perc Decay centre
    float            tilt;
    double           level;
};

static constexpr ModalMode kCowbellModes[] = {
    { 1.00f, 1.00f, 1.0f }, { 1.50f, 0.90f, 1.0f }, { 2.07f, 0.35f, 1.0f },
    { 2.62f, 0.30f, 1.0f }, { 3.30f, 0.20f, 1.0f }, { 4.25f, 0.15f, 1.0f }, { 5.50f, 0.10f, 1.0f },
};
static constexpr ModalMode kClavesModes[] = {
    { 1.0000f, 1.00f, 1.0f }, { 2.7565f, 0.15f, 1.0f }, { 5.4039f, 0.05f, 1.0f },
};
static constexpr ModalMode kWoodBlockModes[] = {
    { 1.00f, 1.00f, 1.0f }, { 1.92f, 0.35f, 1.0f }, { 2.85f, 0.25f, 1.0f }, { 3.90f, 0.15f, 1.0f },
};
#define PERC_TABLE(t) t, (int)(sizeof(t) / sizeof(t[0]))

static inline PercSpec percSpec(DrumVoiceType drum) {
    switch (drum) {
        //                                              f0      t60    tilt   level
        case DrumAgogoHigh:     return { PERC_TABLE(kCowbellModes),   1000.0, 0.90,  0.5f,  0.30 };
        case DrumAgogoLow:      return { PERC_TABLE(kCowbellModes),    760.0, 1.00,  0.5f,  0.30 };
        case DrumClaves:        return { PERC_TABLE(kClavesModes),    2500.0, 0.25,  1.5f,  0.55 };
        case DrumWoodBlockHigh: return { PERC_TABLE(kWoodBlockModes), 1150.0, 0.09,  1.2f,  0.52 };
        case DrumWoodBlockLow:  return { PERC_TABLE(kWoodBlockModes),  820.0, 0.10,  1.2f,  0.52 };
        case DrumTriangleOpen:  return { nullptr, 0,                    180.0, 4.00,  0.15f, 0.22 };
        case DrumTriangleMute:  return { nullptr, 0,                    180.0, 0.12,  0.15f, 0.22 };
        default:                return { PERC_TABLE(kCowbellModes),    560.0, 0.35,  0.6f,  0.38 };   // cowbell
    }
}
#undef PERC_TABLE

static inline bool isPerc(DrumVoiceType d) {
    return d == DrumCowbell || d == DrumAgogoHigh || d == DrumAgogoLow || d == DrumClaves
        || d == DrumWoodBlockHigh || d == DrumWoodBlockLow || d == DrumTriangleMute || d == DrumTriangleOpen;
}

static constexpr double kTriangleSplit    = 0.004;   // in/out-of-plane pair, fractional (set by ear)
static constexpr double kTriangleTopRatio = 85.0;    // ~15 kHz at the centre tuning
static constexpr double kTriangleChokeTau = 0.03;    // s

// The triangle's table: free-free bar roots, each mode as a close pair. Returns the mode count.
static int fillTriangleModes(ModalMode* out, int maxModes) {
    static constexpr double kRoots[] = { 4.73004, 7.85320, 10.99561, 14.13717 };
    int n = 0;
    for (int k = 0; n + 2 <= maxModes; ++k) {
        const double bl    = (k < 4) ? kRoots[k] : (2.0 * k + 3.0) * M_PI / 2.0;
        const double ratio = (bl / kRoots[0]) * (bl / kRoots[0]);
        if (ratio > kTriangleTopRatio) break;
        const float amp = (float)std::sqrt(ratio);   // radiation rising with frequency
        out[n++] = { (float)(ratio * (1.0 - kTriangleSplit)), amp, 1.0f };
        out[n++] = { (float)(ratio * (1.0 + kTriangleSplit)), amp, 1.0f };
    }
    return n;
}

static void renderPerc(DrumKitState* st, Module* module, float level,
                       float* outL, float* outR, uint32_t count) {
    const PercSpec spec  = percSpec(st->drum);
    const double   tune  = mapExp(module->getParamValue(DrumKitParamItemPercTune), 0.7491535, 1.3348399);  // +-5 st
    const double   decay = mapExp(module->getParamValue(DrumKitParamItemPercDecay), 0.5, 2.0);

    int nb;
    if (spec.modes) {
        nb = std::min(spec.count, ModalBank::kMaxModes);
        std::copy(spec.modes, spec.modes + nb, st->bodyModes);
    } else {
        nb = fillTriangleModes(st->bodyModes, ModalBank::kMaxModes);
    }
    st->bodyMaterial.count = nb;
    st->bank.setMaterial(st->bodyMaterial);
    st->bank.invalidate();
    st->bank.setModeCount(nb);
    st->bank.setFundamental((float)(spec.f0 * tune));
    st->bank.setDecayT60((float)(spec.t60 * decay));
    st->bank.setDampTilt(spec.tilt);
    st->bank.setBrightness(0.0f);

    const float gain = (float)(spec.level * kOutScale) * level * kModalPanComp;
    const float chokePerSample = (float)std::exp(-1.0 / (st->sampleRate * kTriangleChokeTau));
    float ex[kModalSubBlock], bL[kModalSubBlock], bR[kModalSubBlock];

    for (uint32_t base = 0; base < count; base += kModalSubBlock) {
        const int n = (int)std::min<uint32_t>(kModalSubBlock, count - base);
        renderStrike(st, ex, n);
        st->bank.processBlock(ex, bL, bR, n);
        for (int i = 0; i < n; ++i) {
            if (st->choked) st->chokeGain *= chokePerSample;
            outL[base + i] = bL[i] * gain * st->chokeGain;
            outR[base + i] = bR[i] * gain * st->chokeGain;
        }
        st->agePos += (uint64_t)n;
    }

    if (st->strikePos < 0 && (st->bank.isSilent(kModalSilence) || (st->choked && st->chokeGain < 1.0e-4f))) {
        st->playing = false;
    }
}

// ---- Cymbals: crashes, ride and hi-hats ----
//
// Plate models (Cymbal.hpp); the knobs are physical quantities:
//
//   crashes  Crash 1 / Crash 2 Size (each cymbal's diameter), Thin (thickness: thinner cymbals
//            swing further for the same hit, so they bloom into a wash sooner), Decay (the
//            bronze's loss factor), Stroke (stick shoulder on the edge, the crash, through to the
//            tip on the bow, a ride-like ping). The two are different cymbals, not one cymbal at
//            two sizes: Crash 2 is also thinner and more domed, which lifts its low clang well
//            above Crash 1's;
//   ride     (GM map) a big, heavy cymbal played with the tip on the bow, or on the bell; it
//            shares the crashes' Decay;
//   hats     Size, Thin, and Sizzle: how close the half-open cymbals sit, i.e. how much they
//            chatter. Open never touches; closed is clamped.

static constexpr double kCrashMinInch    = 13.0;    // Crash 1 / 2 Size knobs span 13" .. 22"
static constexpr double kCrashMaxInch    = 22.0;
static constexpr double kCrash1Dome      = 0.07;    // profile rise / radius (set by ear)
static constexpr double kCrash2Dome      = 0.11;
static constexpr double kCrash2Thinner   = 0.9;     // (thinner alone would LOWER its pitch)
static constexpr double kCrashMaxT60     = 6.0;     // felts and stand
static constexpr double kRideInch        = 21.0;
static constexpr double kRideHeavier     = 1.7;     // times the crash thickness
static constexpr double kRideDome        = 0.06;
static constexpr double kRideLonger      = 0.7;     // times the crash loss factor
static constexpr double kRideBowRadius   = 0.62;
static constexpr double kRideBellRadius  = 0.12;
static constexpr double kHatLoss         = 0.0003;
static constexpr double kHatOpenMaxT60   = 4.0;
static constexpr double kHatHalfMaxT60   = 1.5;
static constexpr double kHatClosedMaxT60 = 0.12;    // clamped together: the "chick"
static constexpr double kHatOpenGap      = 50.0;    // thicknesses: never touches
static constexpr double kHatStrikeRadius = 0.7;     // stick tip on the top cymbal's bow
static constexpr double kHatChokeTau     = 0.006;   // ~6 ms fast-release when a newer hat chokes us
// Output levels, set by ear so full-velocity strokes peak just under full scale.
static constexpr double kCrashLevel      = 1.2;
static constexpr double kRideLevel       = 0.7;
static constexpr double kTipTrim         = 0.35;    // a tip stroke's sharper transient peaks higher
static constexpr double kHatLevel        = 0.55;

static inline bool isHiHat(DrumVoiceType d) {
    return d == DrumHatOpen || d == DrumHatHalf || d == DrumHatClosed;
}
static inline bool isRide(DrumVoiceType d) { return d == DrumRide || d == DrumRideBell; }
static inline bool isCymbal(DrumVoiceType d) {
    return d == DrumCrash1 || d == DrumCrash2 || isRide(d) || isHiHat(d);
}

// The cymbal's configuration and the stick contact time (velocity 0 -> soft, 1 -> hard) from the
// current knobs.
static void cymbalSetup(Module* module, DrumVoiceType drum, Cymbal::Config& c,
                        double& softSec, double& hardSec) {
    if (isHiHat(drum)) {
        c.hat          = true;
        c.diameterInch = mapLin(module->getParamValue(DrumKitParamItemHatSize), 12.0, 15.0);
        c.thicknessMm  = mapExp(module->getParamValue(DrumKitParamItemHatThin), 1.4, 0.8);
        c.lossFactor   = kHatLoss;
        c.strikeRadius = kHatStrikeRadius;
        c.seed         = 37;
        const double sizzle = (double)module->getParamValue(DrumKitParamItemHatSizzle);
        if (drum == DrumHatOpen)      { c.hatGap = kHatOpenGap;                  c.maxT60 = kHatOpenMaxT60; }
        else if (drum == DrumHatHalf) { c.hatGap = 0.05 + 1.2 * (1.0 - sizzle); c.maxT60 = kHatHalfMaxT60; }
        else                          { c.hatGap = 0.0;                          c.maxT60 = kHatClosedMaxT60; }
        softSec = 0.0003;
        hardSec = 0.0001;
        return;
    }
    const double thick = mapExp(module->getParamValue(DrumKitParamItemCrashThin), 1.2, 0.6);
    const double loss  = mapExp(module->getParamValue(DrumKitParamItemCrashDecay), 0.0006, 0.00009);
    c.hat    = false;
    c.maxT60 = kCrashMaxT60;
    if (isRide(drum)) {
        c.diameterInch = kRideInch;
        c.thicknessMm  = thick * kRideHeavier;
        c.domeRise     = kRideDome;
        c.lossFactor   = loss * kRideLonger;
        c.strikeRadius = (drum == DrumRideBell) ? kRideBellRadius : kRideBowRadius;
        c.seed         = 51;
        softSec        = 0.0002;
        hardSec        = 0.00008;
        return;
    }
    const bool   two    = (drum == DrumCrash2);
    const double stroke = (double)module->getParamValue(DrumKitParamItemCrashStroke);
    const float  size   = module->getParamValue(two ? DrumKitParamItemCrashSize2 : DrumKitParamItemCrashSize);
    c.diameterInch = mapLin(size, kCrashMinInch, kCrashMaxInch);
    c.thicknessMm  = thick * (two ? kCrash2Thinner : 1.0);
    c.domeRise     = two ? kCrash2Dome : kCrash1Dome;
    c.lossFactor   = loss;
    c.strikeRadius = 0.95 + (0.6 - 0.95) * stroke;
    c.seed         = two ? 23 : 11;
    softSec        = 0.0006  + (0.00015 - 0.0006)  * stroke;
    hardSec        = 0.00035 + (0.00008 - 0.00035) * stroke;
}

static void strikeCymbal(DrumKitState* st, Module* module) {
    Cymbal::Config c;
    double softSec, hardSec;
    cymbalSetup(module, st->drum, c, softSec, hardSec);
    st->cymbal.configure(c);
    st->cymbal.strike(st->velAmp, softSec + (hardSec - softSec) * (double)st->velAmp, 1.0);
}

static void renderCymbal(DrumKitState* st, Module* module, float level,
                         float* outL, float* outR, uint32_t count) {
    Cymbal::Config c;
    double softSec, hardSec;
    cymbalSetup(module, st->drum, c, softSec, hardSec);
    st->cymbal.configure(c);            // no-op unless a knob moved
    st->cymbal.process(outL, outR, (int)count);

    const bool  hat  = isHiHat(st->drum);
    double familyLevel = hat ? kHatLevel : isRide(st->drum) ? kRideLevel : kCrashLevel;
    if (st->drum == DrumCrash1 || st->drum == DrumCrash2)
        familyLevel *= 1.0 - kTipTrim * (double)module->getParamValue(DrumKitParamItemCrashStroke);
    const float gain = (float)(familyLevel * kOutScale) * level;
    const float chokePerSample = (float)std::exp(-1.0 / (st->sampleRate * kHatChokeTau));
    for (uint32_t i = 0; i < count; ++i) {
        // Choke: a newer hi-hat (the pedal closing, or another stroke) fast-releases this one.
        if (st->choked) st->chokeGain *= chokePerSample;
        outL[i] *= gain * st->chokeGain;
        outR[i] *= gain * st->chokeGain;
    }
    st->agePos += count;
    if (st->cymbal.isQuiet() || (st->choked && st->chokeGain < 1.0e-4f)) st->playing = false;
}

// ---- Lifecycle ----

const void InitDrumKitModule(uint32_t /*count*/, Module* module)
{
    auto* st = new DrumKitState();
    st->sampleRate = module->mSampleRate;
    st->bank.setSampleRate(st->sampleRate);
    st->bank.setPositionWeighting(false);   // 1-D strike law doesn't fit a membrane
    st->bank.setSpread(kModalSpread);
    st->wireBank.setSampleRate(st->sampleRate);
    st->wireBank.setPositionWeighting(false);
    st->wireBank.setSpread(0.0f);
    st->wires.setSampleRate(st->sampleRate);
    st->cymbal.setSampleRate(st->sampleRate);
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
        st->bank.reset();
        st->wireBank.reset();
        st->wires.reset();
        st->cymbal.reset();
        st->bankDrum = DrumNone;
        st->strikePos = -1;
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

    st->drum = drumForNote(st->pendingNote, module->getParamValueAsInt(DrumKitParamItemKeyMap));
    if (st->drum == DrumNone) {      // a key this map leaves silent
        st->pendingTrigger = false;
        st->playing = false;
        return;
    }
    st->pendingTrigger = true;

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

        // A different drum on this voice (a steal) must not inherit the old one's ring.
        if (st->drum != st->bankDrum) {
            st->bank.reset();
            st->wireBank.reset();
            st->wires.reset();
            st->cymbal.reset();
            st->bankDrum = st->drum;
        }
        startStrike(st);
        if (isCymbal(st->drum)) strikeCymbal(st, module);
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
        renderSnare(st, module, level, outL, outR, count);
    } else if (st->playing && isTom) {
        renderTom(st, module, level, outL, outR, count);
    } else if (st->playing && isCymbal(st->drum)) {
        renderCymbal(st, module, level, outL, outR, count);
    } else if (st->playing && isHandDrum(st->drum)) {
        renderHandDrum(st, module, level, outL, outR, count);
    } else if (st->playing && isPerc(st->drum)) {
        renderPerc(st, module, level, outL, outR, count);
    } else {
        // Idle / unassigned voice.
        std::fill_n(outL, count, 0.0f);
        std::fill_n(outR, count, 0.0f);
    }

    return &module->output;
}
