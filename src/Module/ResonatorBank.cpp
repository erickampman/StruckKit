//
//  ResonatorBank.cpp
//

#include "ResonatorBank.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kLn1000        = 6.907755278982137;
constexpr double kNyquistMargin = 0.98;
constexpr double kMaxPoleRadius = 0.9999995;
constexpr float  kDenormalFloor = 1e-20f;
}

void ResonatorBank::setSampleRate(double sr) {
    if (sr <= 0.0 || sr == mSampleRate) return;
    mSampleRate = sr;
    reset();
    for (int i = 0; i < mCount; ++i) computeCoefficients(i);
}

void ResonatorBank::reset() {
    std::fill_n(mY1, kMaxModes, 0.0f);
    std::fill_n(mY2, kMaxModes, 0.0f);
}

void ResonatorBank::computeCoefficients(int i) {
    const Mode& m = mModes[i];
    const double f = (double)m.freq;
    if (f <= 0.0 || f >= 0.5 * mSampleRate * kNyquistMargin) {
        mA1[i] = mA2[i] = mG1[i] = mG2[i] = mL[i] = mR[i] = mAux[i] = 0.0f;
        return;
    }
    const double w = 2.0 * M_PI * f / mSampleRate;
    const double t = std::max(1.0e-4, (double)m.t60);
    const double r = std::min(std::exp(-kLn1000 / (t * mSampleRate)), kMaxPoleRadius);
    const double s = std::sin(w);
    mA1[i]  = (float)(2.0 * r * std::cos(w));
    mA2[i]  = (float)(-r * r);
    mG1[i]  = (float)(s * (double)m.in1);
    mG2[i]  = (float)(s * (double)m.in2);
    mL[i]   = m.outL;
    mR[i]   = m.outR;
    mAux[i] = m.outAux;
}

void ResonatorBank::setModes(const Mode* modes, int count) {
    count = std::max(0, std::min(count, kMaxModes));
    for (int i = 0; i < count; ++i) {
        mModes[i] = modes[i];
        computeCoefficients(i);
    }
    // Modes past the new count (and the lane padding) are silenced: zero coefficients and gains,
    // and zero state so a later, larger count does not resurrect an old ring.
    const int padded = (count + kLanes - 1) / kLanes * kLanes;
    for (int i = count; i < std::max(padded, mPadded); ++i) {
        mA1[i] = mA2[i] = mG1[i] = mG2[i] = mL[i] = mR[i] = mAux[i] = 0.0f;
        mY1[i] = mY2[i] = 0.0f;
    }
    mCount  = count;
    mPadded = padded;
}

void ResonatorBank::applyLoss(float gain) {
    // Scaling both stored samples by the same factor scales the mode's amplitude and leaves its
    // phase alone (the recursion is linear and homogeneous).
    for (int i = 0; i < mPadded; ++i) {
        mY1[i] *= gain;
        mY2[i] *= gain;
    }
}

void ResonatorBank::process(const float* in1, const float* in2, float* outL, float* outR, float* aux, int n) {
    const int groups = mPadded / kLanes;
    for (int s = 0; s < n; ++s) {
        const float x1 = in1 ? in1[s] : 0.0f;
        const float x2 = in2 ? in2[s] : 0.0f;
        float accL[kLanes] = {}, accR[kLanes] = {}, accA[kLanes] = {};
        for (int gi = 0; gi < groups; ++gi) {
            const int base = gi * kLanes;
            for (int l = 0; l < kLanes; ++l) {
                const int i = base + l;
                const float y = mG1[i] * x1 + mG2[i] * x2 + mA1[i] * mY1[i] + mA2[i] * mY2[i];
                mY2[i] = mY1[i];
                mY1[i] = y;
                accL[l] += y * mL[i];
                accR[l] += y * mR[i];
                accA[l] += y * mAux[i];
            }
        }
        float L = 0.0f, R = 0.0f, A = 0.0f;
        for (int l = 0; l < kLanes; ++l) { L += accL[l]; R += accR[l]; A += accA[l]; }
        outL[s] = L;
        outR[s] = R;
        if (aux) aux[s] = A;
    }
    // Flush denormals every ~256 samples rather than every call: callers that run the bank one
    // sample at a time (contact feedback) would otherwise pay for a second full pass per sample.
    mSinceFlush += n;
    if (mSinceFlush >= 256) {
        mSinceFlush = 0;
        for (int i = 0; i < mPadded; ++i) {
            if (std::fabs(mY1[i]) < kDenormalFloor) mY1[i] = 0.0f;
            if (std::fabs(mY2[i]) < kDenormalFloor) mY2[i] = 0.0f;
        }
    }
}

double ResonatorBank::energy() const {
    double e = 0.0;
    for (int i = 0; i < mCount; ++i) e += (double)mY1[i] * mY1[i] + (double)mY2[i] * mY2[i];
    return e;
}

bool ResonatorBank::isSilent(float threshold) const {
    for (int i = 0; i < mCount; ++i)
        if (std::fabs(mY1[i]) > threshold || std::fabs(mY2[i]) > threshold) return false;
    return true;
}
