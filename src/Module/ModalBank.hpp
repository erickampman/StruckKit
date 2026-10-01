//
//  ModalBank.hpp
//
//  A bank of independently damped two-pole resonators — one per vibrational
//  mode — excited by an arbitrary input signal.
//
//  Deliberately framework-free: no Module.hpp, no KPBuffPair, no
//  AUParameterAddress, no AudioToolbox. The PolygraphModular module and the
//  CLAP plugin are both thin wrappers over this. Keep it that way.
//
//  Units are Hz and seconds. PM's octave-relative-to-A440 pitch convention is
//  the wrapper's problem, not this class's.
//
//  See ModalSynthesis.md in the PolygraphModular repo for the design rationale.
//

#ifndef ModalBank_hpp
#define ModalBank_hpp

#include "ModalMaterials.hpp"

class ModalBank {
public:
    static constexpr int kMaxModes = 64;

    ModalBank();

    // --- configuration ------------------------------------------------------
    // All setters are block-rate. They mark state dirty; coefficients are
    // recomputed lazily on the next processBlock. None of them allocate.

    void setSampleRate(double sr);
    void reset();                       // flush filter state; no allocation

    // Force coefficient computation now. Coefficients are otherwise computed
    // lazily at the top of processBlock, which means activeModes() and
    // modeFrequency() report nothing until audio has been pulled through.
    // Call this after configuring if you intend to query before processing.
    void prepare();

    // Force a coefficient recompute. Needed when the material's CONTENTS change
    // under a stable pointer — procedural generators fill one reused buffer, so
    // setMaterial's pointer check cannot see a Fibonacci->Prime switch.
    void invalidate() { mDirty = true; }

    void setMaterial(const ModalMaterial& m);
    void setFundamental(float hz);      // Hz — wrapper converts from PM pitch
    void setModeCount(int n);           // clamped to material count and kMaxModes
    void setStretch(float s);           // inharmonicity: f_i = f0 * ratio^(1+s)
    void setDecayT60(float seconds);    // T60 of mode 0
    void setDampTilt(float t);          // HF damping; 0 = metal, high = wood
    void setBrightness(float b);        // amplitude tilt, -1..+1

    // Optional non-monotonic damping: a Gaussian bump in log-frequency space
    // that makes a BAND of modes ring longer (positive) or shorter (negative)
    // than the ratio^(-tilt) law alone would give. Amount is in octaves of
    // decay time, so +2 is 4x longer at the centre and -2 is 4x shorter.
    // Amount 0 disables it entirely and costs nothing.
    //
    // Physically readable in both directions: a band that radiates efficiently
    // loses energy faster, and a band that couples poorly to the radiator
    // rings longer. Beyond modest settings it is frankly non-physical, which
    // is the interesting part — no object sustains a mid band while its
    // fundamental dies.
    void setDampBellAmount(float octaves);   // -4..4, 0 = off
    void setDampBellCentre(float octaves);   // octaves above the fundamental
    void setDampBellWidth(float octaves);    // Gaussian sigma, in octaves
    void setStrikePosition(float p);    // 0..1; 0.5 cancels even modes

    // The strike-position law above is the 1-D one, |sin(pi * ratio * p)|, which is right for
    // strings and bars. 2-D objects (membranes, plates) have their own mode shapes, so a caller
    // that bakes its own excitation weights into the material's `amp` field turns this off.
    void setPositionWeighting(bool on);
    void setSpread(float s);            // 0 = mono, 1 = modes spread L/R

    // --- audio --------------------------------------------------------------
    // excitation may be null, meaning "no input this block" — the common case
    // for a bank ringing down. Requiring a buffer of zeroes for that would be
    // wasteful, since ring-down is most of a percussion note's life.

    void processBlock(const float* excitation, float* outL, float* outR, int n);

    // --- introspection ------------------------------------------------------

    bool  isSilent(float threshold = 1e-5f) const;
    int   activeModes() const { return mModeCount; }
    float modeFrequency(int i) const;   // post-stretch, in Hz; for verification

    // Effective decay of mode i, recovered from the FILTER COEFFICIENTS rather
    // than recomputed from the parameters. That makes it a genuine check: it
    // reports what the resonator is actually doing, including any clamping.
    float modeT60(int i) const;

private:
    void recomputeIfDirty();
    void retuneState(int i, float oldA1, float oldA2);
    void flushDenormals();

    // Structure-of-arrays so the mode loop can vectorize. Left scalar for now:
    // correctness first, and the whole working set is ~2KB, well inside L1.
    // Revisit only if the benchmark says to.
    alignas(16) float mA1  [kMaxModes];
    alignas(16) float mA2  [kMaxModes];
    alignas(16) float mGain[kMaxModes];
    alignas(16) float mY1  [kMaxModes];
    alignas(16) float mY2  [kMaxModes];
    alignas(16) float mPanL[kMaxModes];
    alignas(16) float mPanR[kMaxModes];

    const ModalMaterial* mMaterial;

    double mSampleRate;
    float  mFundamental;
    int    mModeCount;
    int    mRequestedModes;
    float  mStretch;
    float  mDecayT60;
    float  mDampTilt;
    float  mBrightness;
    float  mDampBellAmount;
    float  mDampBellCentre;
    float  mDampBellWidth;
    float  mStrikePos;
    float  mSpread;
    bool   mPositionWeighting;

    bool   mDirty;
};

#endif /* ModalBank_hpp */
