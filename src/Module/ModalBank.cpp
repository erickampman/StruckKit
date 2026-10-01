//
//  ModalBank.cpp
//
//  Implemented from the governing equations, not transcribed from any
//  reference implementation. Derivations are inline so the reasoning is
//  checkable and so nobody later has to guess where a constant came from.
//

#include "ModalBank.hpp"

#include <cmath>
#include <algorithm>

namespace {

// ln(1000). A 60 dB drop is an amplitude ratio of 10^-3, so decaying to T60
// means r^(T60*fs) = 10^-3, hence r = exp(-ln(1000) / (T60*fs)).
constexpr double kLn1000 = 6.907755278982137;

// Below this, filter state is flushed to zero. Denormal arithmetic can cost
// orders of magnitude on some hardware, and a bank with a long T60 spends most
// of its life in the tail approaching zero — exactly where denormals live.
constexpr float kDenormalFloor = 1e-20f;

// Modes above this fraction of Nyquist are muted rather than clamped. Letting
// w exceed pi would wrap cos(w) and produce a mode at the wrong frequency,
// which is worse than a missing one.
constexpr double kNyquistMargin = 0.98;

// Upper bound on pole radius. Must admit the full DecayT60 range (60 s) at
// every supported sample rate while keeping r*r safely below 1.0 in float.
constexpr double kMaxPoleRadius = 0.9999995;

inline float clampf(float v, float lo, float hi) {
    return std::fmax(lo, std::fmin(hi, v));
}

} // namespace

ModalBank::ModalBank()
    : mMaterial(&kModalMaterials[ModalMaterialString])
    , mSampleRate(44100.0)
    , mFundamental(220.0f)
    , mModeCount(0)
    , mRequestedModes(kMaxModes)
    , mStretch(0.0f)
    , mDecayT60(1.0f)
    , mDampTilt(0.9f)
    , mBrightness(0.0f)
    , mDampBellAmount(0.0f)
    , mDampBellCentre(2.0f)
    , mDampBellWidth(0.4f)
    , mStrikePos(0.25f)
    , mSpread(0.0f)
    , mPositionWeighting(true)
    , mDirty(true)
{
    reset();
}

void ModalBank::setSampleRate(double sr) {
    if (sr <= 0.0 || sr == mSampleRate) return;
    mSampleRate = sr;
    // Coefficients are sample-rate dependent, and old state was accumulated at
    // the old rate — carrying it across would be meaningless, not merely stale.
    reset();
    mDirty = true;
}

void ModalBank::reset() {
    std::fill_n(mY1, kMaxModes, 0.0f);
    std::fill_n(mY2, kMaxModes, 0.0f);
}

void ModalBank::setMaterial(const ModalMaterial& m) {
    if (mMaterial == &m) return;
    mMaterial = &m;
    mDirty = true;
}

void ModalBank::setFundamental(float hz) {
    hz = clampf(hz, 0.1f, 20000.0f);
    if (hz == mFundamental) return;
    mFundamental = hz;
    mDirty = true;
}

void ModalBank::setModeCount(int n) {
    n = std::max(1, std::min(n, kMaxModes));
    if (n == mRequestedModes) return;
    mRequestedModes = n;
    mDirty = true;
}

void ModalBank::setStretch(float s) {
    s = clampf(s, -0.5f, 1.0f);
    if (s == mStretch) return;
    mStretch = s;
    mDirty = true;
}

void ModalBank::setDecayT60(float seconds) {
    seconds = clampf(seconds, 0.01f, 60.0f);
    if (seconds == mDecayT60) return;
    mDecayT60 = seconds;
    mDirty = true;
}

void ModalBank::setDampTilt(float t) {
    t = clampf(t, 0.0f, 3.0f);
    if (t == mDampTilt) return;
    mDampTilt = t;
    mDirty = true;
}

void ModalBank::setBrightness(float b) {
    b = clampf(b, -1.0f, 1.0f);
    if (b == mBrightness) return;
    mBrightness = b;
    mDirty = true;
}

void ModalBank::setStrikePosition(float p) {
    p = clampf(p, 0.0f, 1.0f);
    if (p == mStrikePos) return;
    mStrikePos = p;
    mDirty = true;
}

void ModalBank::setPositionWeighting(bool on) {
    if (on == mPositionWeighting) return;
    mPositionWeighting = on;
    mDirty = true;
}

void ModalBank::setSpread(float s) {
    s = clampf(s, 0.0f, 1.0f);
    if (s == mSpread) return;
    mSpread = s;
    mDirty = true;
}

void ModalBank::setDampBellAmount(float octaves) {
    octaves = clampf(octaves, -4.0f, 4.0f);
    if (octaves == mDampBellAmount) return;
    mDampBellAmount = octaves;
    mDirty = true;
}

void ModalBank::setDampBellCentre(float octaves) {
    octaves = clampf(octaves, 0.0f, 7.0f);
    if (octaves == mDampBellCentre) return;
    mDampBellCentre = octaves;
    mDirty = true;
}

void ModalBank::setDampBellWidth(float octaves) {
    octaves = clampf(octaves, 0.1f, 4.0f);
    if (octaves == mDampBellWidth) return;
    mDampBellWidth = octaves;
    mDirty = true;
}

void ModalBank::prepare() {
    recomputeIfDirty();
}

float ModalBank::modeFrequency(int i) const {
    if (!mMaterial || i < 0 || i >= mModeCount) return 0.0f;
    const double ratio = std::pow((double)mMaterial->modes[i].ratio,
                                  1.0 + (double)mStretch);
    return (float)(mFundamental * ratio);
}

float ModalBank::modeT60(int i) const {
    if (i < 0 || i >= mModeCount) return 0.0f;
    // a2 = -r*r, so r = sqrt(-a2), and T60 follows by inverting
    // r = exp(-ln(1000)/(T60*fs)).
    const double r = std::sqrt(std::max(0.0, -(double)mA2[i]));
    if (r <= 0.0 || r >= 1.0) return 0.0f;
    return (float)(-kLn1000 / (std::log(r) * mSampleRate));
}

void ModalBank::recomputeIfDirty() {
    if (!mDirty) return;
    mDirty = false;

    // Coefficients the bank is ringing on right now, so the state can be carried across the
    // retune (see retuneState below).
    float oldA1[kMaxModes], oldA2[kMaxModes];
    const int oldCount = mModeCount;
    std::copy_n(mA1, oldCount, oldA1);
    std::copy_n(mA2, oldCount, oldA2);

    if (!mMaterial || mMaterial->count <= 0) {
        mModeCount = 0;
        return;
    }

    mModeCount = std::min(mRequestedModes, mMaterial->count);

    const double fs      = mSampleRate;
    const double nyquist = 0.5 * fs * kNyquistMargin;
    const double stretch = 1.0 + (double)mStretch;

    // Amplitudes are collected first so the bank can be energy-normalised
    // afterwards. Without this, Brightness doubles as a volume control and
    // becomes unusable as a timbral parameter.
    float  amps[kMaxModes];
    double energy = 0.0;

    for (int i = 0; i < mModeCount; ++i) {
        const ModalMode& m = mMaterial->modes[i];

        // Inharmonicity. Stretch of 0 leaves the table untouched; positive
        // values push upper modes sharp, as stiffness does in a real bar or
        // piano string.
        const double ratio = std::pow((double)m.ratio, stretch);
        const double freq  = (double)mFundamental * ratio;

        if (freq >= nyquist || freq <= 0.0) {
            mA1[i] = mA2[i] = mGain[i] = 0.0f;
            amps[i] = 0.0f;
            continue;
        }

        const double w = 2.0 * M_PI * freq / fs;

        // Per-mode decay. ratio^(-tilt) is the whole "material" story: at
        // tilt 0 every mode shares mode 0's T60 (metal), and as tilt rises the
        // upper modes die progressively sooner (wood, skin).
        double t60 = (double)mDecayT60 * (double)m.damp * std::pow(ratio, -(double)mDampTilt);

        // Optional Gaussian bump on top of the monotonic law. Computed in
        // log2(ratio) so "width" means octaves and the shape is symmetric
        // across the spectrum rather than bunched at the bottom.
        if (mDampBellAmount != 0.0f) {
            const double oct = std::log2(ratio);
            const double d   = (oct - (double)mDampBellCentre)
                             / std::max((double)mDampBellWidth, 1.0e-3);
            const double g   = std::exp(-0.5 * d * d);          // 1 at centre, ->0 away
            t60 *= std::pow(2.0, (double)mDampBellAmount * g);  // octaves of decay time
        }

        t60 = std::max(t60, 1.0e-4);

        double r = std::exp(-kLn1000 / (t60 * fs));
        // Strictly inside the unit circle, but only just. The previous bound
        // of 0.99999 silently capped T60 at 14.4 s at 48 kHz while
        // setDecayT60 accepts 60 — every long decay was quietly truncated.
        // This bound admits T60 up to ~288 s at 48 kHz, and r*r stays several
        // float ULPs below 1.0 even at 192 kHz.
        r = std::min(r, kMaxPoleRadius);

        mA1[i] = (float)( 2.0 * r * std::cos(w) );
        mA2[i] = (float)( -r * r );

        // Excitation position. Striking at a node of mode i cancels it; the
        // classic case is p = 0.5 killing every even mode of a string, which
        // hollows the tone out the way a square wave is hollow.
        const double posGain = mPositionWeighting
            ? std::fabs(std::sin(M_PI * ratio * (double)mStrikePos))
            : 1.0;

        // Amplitude tilt. Halved exponent keeps the extremes sane: a mode at
        // ratio 30 would otherwise get a 30x boost at full brightness.
        const double tilt = std::pow(ratio, 0.5 * (double)mBrightness);

        const double a = (double)m.amp * posGain * tilt;
        amps[i] = (float)a;
        energy += a * a;
    }

    // Keep every still-ringing mode at the amplitude and phase it had before the retune.
    const int carried = std::min(oldCount, mModeCount);
    for (int i = 0; i < carried; ++i) {
        retuneState(i, oldA1[i], oldA2[i]);
    }

    const double norm = (energy > 0.0) ? (1.0 / std::sqrt(energy)) : 0.0;

    for (int i = 0; i < mModeCount; ++i) {
        if (mGain[i] == 0.0f && mA1[i] == 0.0f && mA2[i] == 0.0f) continue;

        // H(z) = g / (1 - 2r*cos(w)*z^-1 + r^2*z^-2) has impulse response
        //     h[n] = g * r^n * sin((n+1)w) / sin(w)
        // so setting g = A*sin(w) makes each mode ring at peak amplitude A.
        // That is what makes the table's `amp` field mean what it says.
        const double freq = (double)modeFrequency(i);
        const double w    = 2.0 * M_PI * freq / mSampleRate;
        mGain[i] = (float)((double)amps[i] * norm * std::sin(w));

        // Equal-power pan, modes distributed across the field by index.
        // Decorrelating the modes this way costs two multiplies and sounds
        // better than running two banks.
        const float t   = (mModeCount > 1) ? (float)i / (float)(mModeCount - 1) : 0.5f;
        const float pos = 0.5f + (t - 0.5f) * mSpread;      // 0..1
        const float ang = pos * (float)M_PI * 0.5f;
        mPanL[i] = std::cos(ang);
        mPanR[i] = std::sin(ang);
    }
}

// Carry mode i's ringing state across a coefficient change.
//
// The resonator stores its state as the last two outputs, not as an amplitude and phase. While it
// rings freely, y[n] = A r^n sin(n w + phi), so with u = r * y2:
//
//     y1 = A sin(theta)          u = A sin(theta - w)
//  => A cos(theta) = (y1 cos(w) - u) / sin(w)
//
// Reading (y1, y2) with NEW coefficients as if they were old ones gives the wrong amplitude: the
// same pair of samples means a bigger oscillation at a lower w (amplitude ~ 1/sin w), so a
// downward pitch glide would pump energy into a ringing bank (+2.6 dB for the 35% glide a tom
// uses). Instead, recover A cos(theta) with the old (w, r) and rebuild y2 for the new ones, keeping
// y1. Amplitude and phase are then continuous, and the bank can be retuned while it rings.
void ModalBank::retuneState(int i, float oldA1, float oldA2) {
    if (mY1[i] == 0.0f && mY2[i] == 0.0f) return;
    if (oldA2 >= 0.0f || mA2[i] >= 0.0f) return;   // a muted mode on either side: nothing to map

    const double rOld = std::sqrt(-(double)oldA2);
    const double rNew = std::sqrt(-(double)mA2[i]);
    const double cOld = (double)oldA1 / (2.0 * rOld), cNew = (double)mA1[i] / (2.0 * rNew);
    const double sOld = std::sqrt(std::max(0.0, 1.0 - cOld * cOld));
    const double sNew = std::sqrt(std::max(0.0, 1.0 - cNew * cNew));
    if (sOld < 1.0e-6) return;   // too close to DC/Nyquist to resolve phase; leave as is

    const double y1 = (double)mY1[i];
    const double aCos = (y1 * cOld - rOld * (double)mY2[i]) / sOld;   // A cos(theta)
    mY2[i] = (float)((y1 * cNew - aCos * sNew) / rNew);
}

void ModalBank::flushDenormals() {
    for (int i = 0; i < mModeCount; ++i) {
        if (std::fabs(mY1[i]) < kDenormalFloor) mY1[i] = 0.0f;
        if (std::fabs(mY2[i]) < kDenormalFloor) mY2[i] = 0.0f;
    }
}

void ModalBank::processBlock(const float* excitation, float* outL, float* outR, int n) {
    recomputeIfDirty();

    if (mModeCount == 0) {
        std::fill_n(outL, n, 0.0f);
        std::fill_n(outR, n, 0.0f);
        return;
    }

    const int modes = mModeCount;

    for (int s = 0; s < n; ++s) {
        const float x = excitation ? excitation[s] : 0.0f;

        float accL = 0.0f;
        float accR = 0.0f;

        // Mode-inner. The recursion is serial in time so it cannot vectorize
        // across samples, but modes are fully independent, so this is the loop
        // a compiler can widen.
        for (int i = 0; i < modes; ++i) {
            const float y = mGain[i] * x + mA1[i] * mY1[i] + mA2[i] * mY2[i];
            mY2[i] = mY1[i];
            mY1[i] = y;

            accL += y * mPanL[i];
            accR += y * mPanR[i];
        }

        outL[s] = accL;
        outR[s] = accR;
    }

    flushDenormals();
}

bool ModalBank::isSilent(float threshold) const {
    for (int i = 0; i < mModeCount; ++i) {
        if (std::fabs(mY1[i]) > threshold || std::fabs(mY2[i]) > threshold) {
            return false;
        }
    }
    return true;
}
