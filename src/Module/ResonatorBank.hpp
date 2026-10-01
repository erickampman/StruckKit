//
//  ResonatorBank.hpp
//
//  A lean bank of many two-pole resonators, for bodies that need hundreds of modes (cymbals) where
//  ModalBank's 64 and its per-material machinery do not fit. The caller supplies every mode's
//  frequency, decay and gains directly; the bank only resonates.
//
//  Same resonator as ModalBank (see ModalBank.cpp for the derivation): y[n] = g x[n] + a1 y[n-1] +
//  a2 y[n-2], with a1 = 2 r cos w, a2 = -r^2, g = sin w (so a unit impulse rings mode i at peak
//  amplitude equal to its input gain).
//
//  Two inputs, because a body is driven in more than one way at once: e.g. a stick at one point
//  (input 1) and a force spread over the body or applied somewhere else, like a contact at the rim
//  (input 2). Three outputs: left, right, and an auxiliary observation (e.g. displacement at a
//  contact point) for the caller to drive a nonlinearity with.
//
//  Layout and loop are arranged for the compiler to vectorise across modes without -ffast-math:
//  structure-of-arrays, modes in lanes of kLanes, and per-lane accumulators so no reduction runs
//  across lanes inside the hot loop. ~2.3% of one core per 1024 modes at 48 kHz (SSE2).
//
//  Framework-free, like ModalBank.
//

#ifndef ResonatorBank_hpp
#define ResonatorBank_hpp

class ResonatorBank {
public:
    static constexpr int kMaxModes = 1024;
    static constexpr int kLanes    = 8;

    struct Mode {
        float freq;     // Hz
        float t60;      // s
        float in1;      // gain from input 1
        float in2;      // gain from input 2
        float outL;
        float outR;
        float outAux;
    };

    void setSampleRate(double sr);
    void reset();

    // Define the modes. Modes at or above ~Nyquist are muted. Re-defining modes while ringing keeps
    // each mode's state (call reset() to silence).
    void setModes(const Mode* modes, int count);

    // Lose a fraction of every mode's amplitude now (gain in (0, 1]), keeping phase. Used to drain a
    // body while a nonlinearity moves its energy elsewhere; cheap enough to call every sub-block.
    void applyLoss(float gain);

    // Either input may be null (no drive). aux may be null.
    void process(const float* in1, const float* in2, float* outL, float* outR, float* aux, int n);

    // Sum over modes of y[n-1]^2 + y[n-2]^2: twice the mean squared amplitude of a freely ringing
    // mode, so a proxy for the bank's vibrational energy in the units its input gains use (an
    // impulse of size x into a mode with input gain g adds about (g x)^2).
    double energy() const;

    int  modeCount() const { return mCount; }
    bool isSilent(float threshold) const;

private:
    void computeCoefficients(int i);

    alignas(32) float mA1[kMaxModes]  = {};
    alignas(32) float mA2[kMaxModes]  = {};
    alignas(32) float mG1[kMaxModes]  = {};   // sin(w) * in1
    alignas(32) float mG2[kMaxModes]  = {};   // sin(w) * in2
    alignas(32) float mL[kMaxModes]   = {};
    alignas(32) float mR[kMaxModes]   = {};
    alignas(32) float mAux[kMaxModes] = {};
    alignas(32) float mY1[kMaxModes]  = {};
    alignas(32) float mY2[kMaxModes]  = {};

    Mode   mModes[kMaxModes] = {};
    double mSampleRate = 48000.0;
    int    mCount  = 0;   // modes defined
    int    mPadded = 0;   // rounded up to a whole number of lanes
    int    mSinceFlush = 0;
};

#endif /* ResonatorBank_hpp */
