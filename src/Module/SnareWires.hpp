//
//  SnareWires.hpp
//
//  Snare wires as a contact problem, not a noise generator.
//
//  A set of steel strands is stretched between the strainer and the butt end, across the resonant
//  (bottom) head, and pushed against it. Each strand behaves like a pre-loaded spring:
//
//    - Its tension pulls it towards a rest line a distance p inside the head's rest plane, so at
//      rest it presses on the head (the preload). The restoring force grows with displacement, as
//      a stretched string's does: stiffness k = (2 pi fw)^2, fw the strand's own transverse
//      frequency.
//    - It stays on the head while the head's acceleration away from it is less than the spring can
//      supply. Once the head drops away faster than that (roughly A w^2 > k p, or the head simply
//      retreats past p), the strand is left behind, in flight.
//    - It swings back and lands on the head. The landing is an impact whose strength is the
//      closing speed, and it rings the strand's own bright, many-stranded resonances.
//
//  One strand alone gives a regular tick; a real snare has 20-42, lying at different radii (so they
//  see different head amplitudes) and with slightly different tensions, so their impacts interleave
//  into a dense rattle. This class runs kStrands of them.
//
//  What falls out, without scripting any of it: the rattle is locked to the head's vibration; it
//  grows with how hard the drum is hit; and it stops on its own, fairly abruptly, once the head's
//  motion decays below the threshold. Tension moves the threshold and the strands' own speed:
//  loose strands are lifted by small motions and swing back lazily, so the buzz is long and
//  sloppy; tight ones only leave the head on the first big cycles of a hard hit and snap straight
//  back, a short crisp burst.
//
//  Framework-free, like ModalBank. The caller supplies the resonant head's displacement per sample
//  (positive = out of the drum, towards the wires) and mixes the returned wire sound.
//
//  PROVENANCE: the contact mechanics are physical. The strand count, spreads, wire resonance
//  filters, impact grain length and the tension -> (fw, p) mapping are set by ear, and marked.
//

#ifndef SnareWires_hpp
#define SnareWires_hpp

#include <cmath>
#include <cstdint>

class SnareWires {
public:
    static constexpr int kStrands = 8;

    void setSampleRate(double sr) {
        mSampleRate = sr;
        updateFilters();
        updatePreload();
    }

    // 0 = loose (soft strands, light preload: long sloppy buzz), 1 = tight (stiff strands, heavy
    // preload: short crisp burst, and only a hard hit lifts them at all).
    void setTension(float t) {
        t = std::fmin(1.0f, std::fmax(0.0f, t));
        if (t == mTension) return;
        mTension = t;
        updatePreload();
    }

    void reset() {
        for (Strand& s : mStrands) { s.pos = 0.0; s.vel = 0.0; s.inContact = true; }
        mHeadPrev = 0.0;
        mGrain = 0.0;
        mHp1 = mHp2 = mBpA1 = mBpA2 = mBpB1 = mBpB2 = 0.0;
    }

    // Is the rattle over (no strand in flight, no impact energy left ringing)?
    bool isQuiet(double threshold = 1.0e-6) const {
        for (const Strand& s : mStrands) if (!s.inContact) return false;
        return mGrain < threshold && std::fabs(mHp1) < threshold && std::fabs(mBpA1) < threshold
            && std::fabs(mBpB1) < threshold;
    }

    // One sample. `head` is the resonant head's displacement (out of the drum, towards the wires).
    float process(double head) {
        const double headVel = head - mHeadPrev;
        mHeadPrev = head;

        for (int k = 0; k < kStrands; ++k) {
            Strand& s = mStrands[k];
            // Where this strand lies, the head moves by a fraction of its centre motion.
            const double h  = head * kReach[k];
            const double hv = headVel * kReach[k];

            // The strand's own spring, rest line p inside the head's rest plane, lightly damped.
            // Semi-implicit Euler in per-sample units.
            s.vel += -mK[k] * (s.pos + mPreload[k]) - mDamp[k] * s.vel;
            s.pos += s.vel;

            // Contact: a strand cannot be inside the head (pos >= h). One resting on the head is
            // carried with it; only one that had been thrown clear makes an impact on landing, with
            // strength = closing speed (per second, so independent of sample rate).
            if (s.pos <= h) {
                if (!s.inContact) {
                    const double closing = (hv - s.vel) * mSampleRate;
                    if (closing > 0.0) mGrain += closing * kImpactGain;
                }
                s.pos = h;
                s.vel = hv;
                s.inContact = true;
            } else {
                s.inContact = false;
            }
        }

        // Each impact excites a short grain of noise through the strands' resonances.
        mGrain *= mGrainDecay;
        const double x  = noise() * mGrain;
        const double hp = tick(mHpf, x, mHp1, mHp2);
        const double a  = tick(mBpA, x, mBpA1, mBpA2);
        const double b  = tick(mBpB, x, mBpB1, mBpB2);
        return (float)(0.55 * hp + 0.8 * a + 0.5 * b);
    }

private:
    struct Strand { double pos = 0.0, vel = 0.0; bool inContact = true; };
    struct Biquad { double b0, b1, b2, a1, a2; };

    static double tick(const Biquad& f, double x, double& z1, double& z2) {
        const double y = f.b0 * x + z1;
        z1 = f.b1 * x - f.a1 * y + z2;
        z2 = f.b2 * x - f.a2 * y;
        return y;
    }

    double noise() {
        mRng = mRng * 1664525u + 1013904223u;
        return (double)((int32_t)(mRng >> 8) - 8388608) / 8388608.0;
    }

    static Biquad bandpass(double fc, double q, double sr) {
        const double w = 2.0 * M_PI * fc / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
        return { al / a0, 0.0, -al / a0, -2.0 * c / a0, (1.0 - al) / a0 };
    }
    static Biquad highpass(double fc, double q, double sr) {
        const double w = 2.0 * M_PI * fc / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
        return { (1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - al) / a0 };
    }

    // Set by ear: the wires' sound is bright and dense; a broad high-pass for the hiss of many
    // strands plus two soft resonances for the coils' colour.
    void updateFilters() {
        mHpf = highpass(1800.0, 0.7, mSampleRate);
        mBpA = bandpass(4200.0, 0.9, mSampleRate);
        mBpB = bandpass(7500.0, 1.1, mSampleRate);
        mGrainDecay = std::exp(-1.0 / (kGrainSec * mSampleRate));
    }

    // Set by ear: tension raises the strands' own frequency and their preload together, as
    // tightening the strainer does. Displacements are in the head units DrumKit feeds in (~0.1 at
    // a full-velocity stroke).
    void updatePreload() {
        const double fw = 60.0 * std::pow(5.0, (double)mTension);     // Hz: 60 .. 300
        const double p  = 0.0015 * std::pow(10.0, (double)mTension);  // 0.0015 .. 0.015
        for (int k = 0; k < kStrands; ++k) {
            const double w = 2.0 * M_PI * fw * kTuneSpread[k] / mSampleRate;
            mK[k]       = w * w;
            mDamp[k]    = 2.0 * 0.15 * w;                               // zeta = 0.15
            mPreload[k] = p * kPreloadSpread[k];
        }
    }

    // Set by ear: strands lie across the middle of the head, so they see between about half and all
    // of its centre motion; their tensions differ by about +-20% and their preloads by +-30%.
    static constexpr double kReach[kStrands]         = { 1.00, 0.95, 0.88, 0.82, 0.75, 0.68, 0.61, 0.55 };
    static constexpr double kTuneSpread[kStrands]    = { 0.84, 1.13, 0.95, 1.21, 1.00, 0.88, 1.07, 0.80 };
    static constexpr double kPreloadSpread[kStrands] = { 0.70, 1.20, 0.85, 1.30, 1.00, 0.78, 1.12, 0.93 };

    static constexpr double kImpactGain = 1.0 / 400.0;   // set by ear: per (unit/s) of closing speed
    static constexpr double kGrainSec   = 0.0012;        // set by ear: one impact's noise grain

    double   mSampleRate = 48000.0;
    float    mTension    = -1.0f;   // forces the first setTension through
    double   mK[kStrands] = {}, mDamp[kStrands] = {}, mPreload[kStrands] = {};
    Strand   mStrands[kStrands];
    double   mHeadPrev = 0.0;
    double   mGrain = 0.0, mGrainDecay = 0.0;
    Biquad   mHpf{}, mBpA{}, mBpB{};
    double   mHp1 = 0, mHp2 = 0, mBpA1 = 0, mBpA2 = 0, mBpB1 = 0, mBpB2 = 0;
    uint32_t mRng = 0x5EED5A1Eu;
};

#endif /* SnareWires_hpp */
