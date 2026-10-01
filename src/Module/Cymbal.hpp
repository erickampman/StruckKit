//
//  Cymbal.hpp
//
//  A cymbal (or one hi-hat) as a physical model on a ResonatorBank:
//
//    plate      a thin bronze disc with a free edge: up to ~1000 of its modes, low ones exact
//               (PlateModesTable.hpp), the rest semiclassical (PlateModes.hpp). Plate frequencies
//               scale as thickness / radius^2;
//    dome       the cymbal's curvature stiffens the modes that have to stretch the metal, lifting
//               the low ones into the characteristic low clang (shallow-shell correction);
//    strike     each mode is driven by its shape where the stick lands, so a bow or bell stroke
//               reaches far fewer modes than an edge crash; the stick's contact time rounds off
//               the top;
//    damping    bronze has a near-constant loss factor, so T60 ~ 1/frequency, capped by the felts;
//    cascade    the geometric nonlinearity of a thin plate: once the rim swings by a fair fraction
//               of the thickness, energy flows from the struck modes into ever-higher ones (wave
//               turbulence). Modelled as a drain on the plate's motion that re-enters as forcing
//               spread over the modes, up to a front that climbs while the plate is swinging hard.
//               It is what makes a hard crash bloom into a bright wash after the hit while a light
//               tap stays a tonal ping;
//    hi-hat     the second cymbal as a one-sided stop under the rim: closed clamps and damps the
//               plate; half-open lets the rim chatter against it (the sizzle), every contact
//               spraying energy across the modes.
//
//  Framework-free, like ModalBank: DrumKitModule maps its knobs onto a Config.
//
//  PROVENANCE: the plate modes, shapes, frequency scaling and loss law are physical; bronze
//  constants are textbook values. Marked "set by ear": the dome correction's form, the felt cap,
//  radiation corner, mode-splitting spread, the cascade's rates and scale, and the hi-hat contact.
//

#ifndef Cymbal_hpp
#define Cymbal_hpp

#include "ResonatorBank.hpp"

#include <cstdint>
#include <vector>

class Cymbal {
public:
    struct Config {
        double diameterInch  = 18.0;
        double thicknessMm   = 0.85;
        double domeRise      = 0.08;    // profile height / radius: more dome lifts the low modes
        double lossFactor    = 0.00015;  // bronze + mount; T60 = 2.2 / (eta f)
        double maxT60        = 6.0;     // the felts and stand cap the low modes' ring (s)
        double strikeRadius  = 0.95;    // 0 = centre (bell), 1 = rim
        bool   hat           = false;   // a hi-hat top cymbal: rim contact instead of free wash
        double hatGap        = 1.0;     // hats: rim-to-bottom-cymbal gap, in thicknesses (< 0 never
                                        // used; large = open, ~0 = closed, small = half-open)
        uint32_t seed        = 1;       // per-instrument imperfections (mode splitting, orientation)

        bool operator==(const Config& o) const;
    };

    void setSampleRate(double sr);
    void configure(const Config& c);    // rebuilds the modes only if the config changed
    void reset();

    // Start a stroke: velocity 0..1, stick contact time (s), and a force multiplier.
    void strike(float velocity, double contactSec, double force);

    // Render n samples (writes, does not add). n is any length.
    void process(float* outL, float* outR, int n);

    bool isQuiet() const;

    // Introspection for tests and the measuring bench.
    int    modeCount() const { return mBank.modeCount(); }
    double lowestFrequency() const { return mLowest; }
    double nonlinearity() const { return mN; }
    double frontHz() const;

private:
    void rebuild();
    void renderStrike(float* ex, int n);
    void processSubBlock(float* outL, float* outR, int len);
    double in2BelowFront() const;

    double  mSampleRate = 48000.0;
    Config  mConfig;
    bool    mConfigured = false;
    ResonatorBank mBank;
    double  mLowest = 0.0;
    double  mOutNorm = 1.0;

    // stroke
    int     mStrikePos = -1, mStrikeLen = 1;
    float   mStrikeScale = 0.0f;

    // cascade
    double  mRimPow = 0.0;    // smoothed rim displacement power
    double  mN      = 0.0;    // nonlinearity: (rim swing / its scale)^2
    double  mFront  = 0.0;    // log2 of the cascade front frequency
    double  mForcePower = 0.0;   // forcing-noise variance carrying the drained energy back in
    std::vector<float>  mModeFreq;   // per resonator, in frequency order
    std::vector<double> mCumIn2;     // running sum of input-2 gain^2 in that order
    double  mHp = 0.0, mHpPrevIn = 0.0, mLp1 = 0.0, mLp2 = 0.0;   // forcing-noise filters
    uint32_t mRng = 0x0C1A5Bu;
};

#endif /* Cymbal_hpp */
