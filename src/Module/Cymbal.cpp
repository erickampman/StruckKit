//
//  Cymbal.cpp
//

#include "Cymbal.hpp"
#include "PlateModes.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// --- material: B20 bronze (textbook values) -------------------------------------------------
constexpr double kYoung   = 1.0e11;   // Pa
constexpr double kDensity = 8600.0;   // kg/m^3
constexpr double kPoisson = 0.33;     // matches PlateModesTable.hpp
// Plate frequency per lam^2:  f = lam^2 h sqrt(E / (12 rho (1 - nu^2))) / (2 pi a^2)
const double kBendingSpeed = std::sqrt(kYoung / (12.0 * kDensity * (1.0 - kPoisson * kPoisson)));
// Membrane (stretching) wave speed, for the dome's ring frequency.
const double kSheetSpeed   = std::sqrt(kYoung / kDensity);

// --- set by ear -----------------------------------------------------------------------------
// Radiation: a plate radiates efficiently once it is not small against the wavelength, so below
// a corner at ka = kRadiationKa (k the air wavenumber, a the radius) each mode is weaker in the
// sound. The corner scales with 1 / size: a big cymbal's low clang carries, a small one's barely.
constexpr double kSpeedOfSound = 343.0;
constexpr double kRadiationKa  = 2.0;
constexpr double kSplit        = 0.004;   // imperfection splitting of m > 0 pairs, +- fraction
constexpr double kMicAngle     = 0.35;    // rad either side of the strike line, for L / R
constexpr double kRimAngle     = 1.9;     // rad: where the cascade / contact watches the rim
constexpr double kMaxFreq      = 18000.0;
constexpr int    kSubBlock     = 32;

// Cascade. N = (rim swing / thickness)^2 with the swing in mm; kDispPerMm converts the bank's rim
// observation to mm (set by ear so a full-velocity edge crash on a 20" x 0.85 mm cymbal blooms
// into its wash within ~100 ms, and a half-velocity one only just starts to).
constexpr double kDispPerMm    = 0.00035;
constexpr double kNMax         = 60.0;
constexpr double kRimSmoothSec = 0.004;
constexpr double kFrontStartHz = 900.0;
constexpr double kFrontRise    = 2.2;     // octaves/s per unit N
constexpr double kFrontFall    = 3.0;     // octaves/s, the front relaxing once forcing fades
constexpr double kDrainRate    = 0.3;    // 1/s per unit N: how fast the nonlinearity moves energy

// Hi-hat contact: drain rate per thickness of mean penetration (1/s), and the share of what it
// drains that comes back as chatter rather than being lost to friction.
constexpr double kContactRate    = 40.0;
constexpr double kContactScatter = 0.6;

inline uint32_t hashMode(uint32_t seed, int m, int n) {
    uint32_t h = seed * 0x9E3779B1u ^ (uint32_t)(m * 0x85EBCA77u) ^ (uint32_t)(n * 0xC2B2AE3Du);
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
inline double unit(uint32_t h) { return (double)(h & 0xFFFFFF) / (double)0x1000000; }   // [0, 1)

} // namespace

bool Cymbal::Config::operator==(const Config& o) const {
    return diameterInch == o.diameterInch && thicknessMm == o.thicknessMm
        && domeRise == o.domeRise && lossFactor == o.lossFactor && maxT60 == o.maxT60 && strikeRadius == o.strikeRadius
        && hat == o.hat && hatGap == o.hatGap && seed == o.seed;
}

void Cymbal::setSampleRate(double sr) {
    if (sr <= 0.0 || sr == mSampleRate) return;
    mSampleRate = sr;
    mBank.setSampleRate(sr);
    if (mConfigured) rebuild();
}

void Cymbal::configure(const Config& c) {
    if (mConfigured && c == mConfig) return;
    mConfig = c;
    mConfigured = true;
    rebuild();
}

void Cymbal::reset() {
    mBank.reset();
    mStrikePos = -1;
    mRimPow = 0.0; mN = 0.0; mFront = std::log2(kFrontStartHz); mForcePower = 0.0;
    mHp = mLp1 = mLp2 = mHpPrevIn = 0.0;
}

double Cymbal::frontHz() const { return std::exp2(mFront); }

void Cymbal::rebuild() {
    const Config& c = mConfig;
    const double a  = 0.5 * c.diameterInch * 0.0254;      // radius, m
    const double h  = c.thicknessMm * 1e-3;
    const double cp = h * kBendingSpeed / (2.0 * M_PI * a * a);      // Hz per lam^2
    const double Rc = a / (2.0 * c.domeRise);                         // cap radius of curvature
    const double fRing = kSheetSpeed / (2.0 * M_PI * Rc);
    const double fMax  = std::min(kMaxFreq, 0.45 * mSampleRate);
    const double fRad  = kRadiationKa * kSpeedOfSound / (2.0 * M_PI * a);
    // The mode shapes are normalised on a unit disc of unit mass; a real cymbal's response to the
    // same impulse scales as 1 / its mass (rho h pi a^2). The rim swing — which drives the cascade
    // and the hi-hat contact — carries that, relative to the 18" x 0.85 mm cymbal those were
    // calibrated on, so small and thin cymbals swing further and wash sooner, as real ones do.
    // (The outputs need no such factor: the level is normalised below.)
    const double aRef = 0.5 * 18.0 * 0.0254;
    const double massScale = (0.85 * aRef * aRef) / (c.thicknessMm * a * a);

    // Dome (shallow-shell) correction, form set by ear: curvature adds stretching stiffness,
    // f^2 = f_plate^2 + g f_ring^2, felt fully by modes that cannot bend without stretching
    // (axisymmetric, many circles) and much less by the inextensional low diametral ones.
    auto freqOf = [&](int m, int n, double lam) {
        const double fp = cp * lam * lam;
        const double nn = (double)(n + 1);
        const double g  = nn * nn / (nn * nn + 0.25 * m * m);
        return std::sqrt(fp * fp + g * fRing * fRing);
    };

    // --- every mode below fMax: the exact table, then semiclassical ones above it ---
    struct Cand { double f; int m; int n; double lam; int table; };
    std::vector<Cand> all;
    all.reserve(4096);
    for (int k = 0; k < kPlateModeCount; ++k) {
        const PlateModeIndex& p = kPlateModes[k];
        const double f = freqOf(p.m, p.n, p.lam);
        if (f < fMax) all.push_back({ f, p.m, p.n, p.lam, k });
    }
    for (int m = 0; ; ++m) {
        bool any = false;
        for (int n = 0; ; ++n) {
            const double lam = Plate::wkbLambda(m, n);
            if (lam < kPlateTableLamMax) continue;               // the table has it
            const double f = freqOf(m, n, lam);
            if (f >= fMax) break;
            all.push_back({ f, m, n, lam, -1 });
            any = true;
        }
        if (!any && (double)m > kPlateTableLamMax) break;        // no mode of this order fits
    }
    std::sort(all.begin(), all.end(), [](const Cand& x, const Cand& y) { return x.f < y.f; });
    mLowest = all.empty() ? 0.0 : all.front().f;

    // --- fit the budget: every mode up to half the bank, then every k-th above ---
    // Each m > 0 mode is a degenerate pair (cos and sin orientations), so it costs two resonators.
    auto cost = [](const Cand& x) { return x.m == 0 ? 1 : 2; };
    const int budget = ResonatorBank::kMaxModes;
    int total = 0;
    for (const Cand& x : all) total += cost(x);
    size_t split = all.size();
    int stride = 1;
    if (total > budget) {
        int used = 0;
        split = 0;
        while (split < all.size() && used + cost(all[split]) <= budget / 2) used += cost(all[split++]);
        int rest = 0;
        for (size_t i = split; i < all.size(); ++i) rest += cost(all[i]);
        stride = (rest + (budget - used) - 1) / (budget - used);
        stride = std::max(stride, 1);
    }

    // --- per-mode gains ---
    std::vector<ResonatorBank::Mode> modes;
    modes.reserve(budget);
    const double rStrike = std::min(c.strikeRadius, 0.999);
    double irEnergy = 0.0;
    for (size_t i = 0; i < all.size(); ++i) {
        const bool sampled = i >= split;
        if (sampled && (i - split) % (size_t)stride != 0) continue;
        const Cand& x = all[i];
        if ((int)modes.size() + cost(x) > budget) break;

        // Each kept mode above the split stands in for `stride` of its neighbours.
        const double dens = sampled ? std::sqrt((double)stride) : 1.0;
        const double shapeStrike = (x.table >= 0) ? Plate::tableShape(kPlateModes[x.table], rStrike)
                                                  : Plate::wkbShape(x.m, x.lam, rStrike);
        const double shapeRim    = (x.table >= 0) ? Plate::tableShape(kPlateModes[x.table], 1.0)
                                                  : Plate::wkbShape(x.m, x.lam, 0.999);
        const double rad   = (x.f / fRad) / std::sqrt(1.0 + (x.f / fRad) * (x.f / fRad));
        const double t60   = std::max(0.02, std::min(c.maxT60, 2.2 / (c.lossFactor * x.f)));
        const double omega = 2.0 * M_PI * x.f;
        const uint32_t hsh = hashMode(c.seed, x.m, x.n);
        // Rim observation is displacement (velocity / omega), which is what contact and the
        // nonlinearity respond to; the outputs are velocity, which is what radiates.
        const double rimDisp = massScale * shapeRim / omega;

        if (x.m == 0) {
            ResonatorBank::Mode md;
            md.freq = (float)x.f; md.t60 = (float)t60;
            md.in1  = (float)shapeStrike;
            md.in2  = c.hat ? (float)(shapeRim * dens) : (float)(dens * ((hsh & 1) ? 1.0 : -1.0));
            md.outL = md.outR = (float)(rad * dens);
            md.outAux = (float)rimDisp;
            modes.push_back(md);
            irEnergy += (double)md.in1 * md.in1 * md.outL * md.outL;
        } else {
            // The pair's orientation psi and splitting come from the plate's imperfections.
            const double psi = 2.0 * M_PI * unit(hsh);
            const double eps = kSplit * (2.0 * unit(hsh >> 7 ^ 0x5A5A5A) - 1.0);
            const double mm  = (double)x.m;
            for (int part = 0; part < 2; ++part) {
                // Shapes cos(m (theta - psi)) and sin(m (theta - psi)); the stroke is at theta = 0.
                auto ang = [&](double theta) {
                    const double ph = mm * (theta - psi);
                    return part == 0 ? std::cos(ph) : std::sin(ph);
                };
                ResonatorBank::Mode md;
                md.freq = (float)(x.f * (part == 0 ? 1.0 - eps : 1.0 + eps));
                md.t60  = (float)t60;
                md.in1  = (float)(shapeStrike * ang(0.0));
                md.in2  = c.hat ? (float)(shapeRim * ang(kRimAngle) * dens)
                                : (float)(dens * (((hsh >> (part + 1)) & 1) ? 1.0 : -1.0) * 0.7071);
                md.outL = (float)(rad * dens * ang(+kMicAngle));
                md.outR = (float)(rad * dens * ang(-kMicAngle));
                md.outAux = (float)(rimDisp * ang(kRimAngle));
                modes.push_back(md);
                irEnergy += 0.5 * (double)md.in1 * md.in1 * ((double)md.outL * md.outL + (double)md.outR * md.outR);
            }
        }
    }
    mBank.setModes(modes.data(), (int)modes.size());

    // Cascade bookkeeping: forcing below the front reaches the modes below it, so keep the running
    // sum of their input-2 gains squared, in frequency order, to know how much energy a given
    // forcing level delivers.
    mModeFreq.resize(modes.size());
    mCumIn2.resize(modes.size());
    double acc = 0.0;
    for (size_t i = 0; i < modes.size(); ++i) {
        mModeFreq[i] = modes[i].freq;
        acc += (double)modes[i].in2 * modes[i].in2;
        mCumIn2[i] = acc;
    }
    // Level: a unit stroke rings with unit total energy whatever the size, thickness or strike
    // point (those are timbre, not volume).
    mOutNorm = (irEnergy > 0.0) ? 1.0 / std::sqrt(irEnergy) : 0.0;
}

void Cymbal::strike(float velocity, double contactSec, double force) {
    velocity = std::max(0.0f, std::min(1.0f, velocity));
    mStrikeLen = std::max(1, (int)std::lround(contactSec * mSampleRate));
    const double area = (mStrikeLen <= 1) ? 1.0 : 0.5 * (double)mStrikeLen;
    mStrikeScale = (float)((double)velocity * force / area);
    mStrikePos = 0;
    if (mFront <= 0.0) mFront = std::log2(kFrontStartHz);
}

void Cymbal::renderStrike(float* ex, int n) {
    for (int i = 0; i < n; ++i) {
        float v = 0.0f;
        if (mStrikePos >= 0) {
            const int L = mStrikeLen;
            const float w = (L <= 1) ? 1.0f
                : 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * ((float)mStrikePos + 0.5f) / (float)L));
            v = mStrikeScale * w;
            if (++mStrikePos >= L) mStrikePos = -1;
        }
        ex[i] = v;
    }
}

void Cymbal::process(float* outL, float* outR, int n) {
    if (!mConfigured) {
        std::fill_n(outL, n, 0.0f);
        std::fill_n(outR, n, 0.0f);
        return;
    }
    for (int base = 0; base < n; base += kSubBlock) {
        processSubBlock(outL + base, outR + base, std::min(kSubBlock, n - base));
    }
    const float g = (float)mOutNorm;
    for (int i = 0; i < n; ++i) { outL[i] *= g; outR[i] *= g; }
}

// One sub-block: the stroke on input 1; on input 2 the energy that the nonlinear processes moved
// out of the plate's motion in the previous sub-block, coming back in elsewhere in the spectrum.
//
//   cascade  (all cymbals) drains at a rate set by the rim's swing in thicknesses and returns the
//            energy as forcing below the cascade front;
//   contact  (hi-hats) drains at a rate set by how far the rim presses into the bottom cymbal;
//            part of that is friction and is lost, the rest is the chatter, sprayed back across
//            every mode through the rim (input 2 is the rim force on a hat).
//
// Both only ever move or remove energy, so neither can run away.
void Cymbal::processSubBlock(float* outL, float* outR, int len) {
    const double h  = mConfig.thicknessMm;
    const double fs = mSampleRate;
    const double dt = (double)len / fs;
    const bool   hat = mConfig.hat;
    float ex[kSubBlock], force[kSubBlock], aux[kSubBlock];

    // Forcing: white noise high-passed above the struck region and, for the cascade, low-passed at
    // its front. A hat's chatter is broadband.
    const double sigma = std::sqrt(mForcePower);
    const double gLp = 1.0 - std::exp(-2.0 * M_PI * (hat ? kMaxFreq : std::exp2(mFront)) / fs);
    const double gHp = std::exp(-2.0 * M_PI * 300.0 / fs);
    for (int i = 0; i < len; ++i) {
        mRng = mRng * 1664525u + 1013904223u;
        const double w = (double)((int32_t)(mRng >> 8) - 8388608) / 8388608.0;
        const double hp = gHp * (mHp + w - mHpPrevIn);   // one-pole high-pass
        mHpPrevIn = w;
        mHp = hp;
        mLp1 += gLp * (hp - mLp1);
        mLp2 += gLp * (mLp1 - mLp2);
        force[i] = (float)(sigma * mLp2);
    }
    renderStrike(ex, len);
    mBank.process(ex, force, outL, outR, aux, len);

    // The rim: its swing (for the nonlinearity) and, on a hat, how far it presses into the bottom
    // cymbal (aux is rim displacement, positive towards it).
    double p = 0.0, press = 0.0;
    const double gapMm = mConfig.hatGap * h;
    for (int i = 0; i < len; ++i) {
        p += (double)aux[i] * aux[i];
        if (hat) press += std::max(0.0, (double)aux[i] / kDispPerMm - gapMm);
    }
    p /= len;
    press /= (len * h);                                   // mean penetration, in thicknesses
    mRimPow += (1.0 - std::exp(-dt / kRimSmoothSec)) * (p - mRimPow);
    const double swingMm = std::sqrt(mRimPow) / kDispPerMm;
    mN = std::min(kNMax, (swingMm / h) * (swingMm / h));
    mFront += (kFrontRise * mN - kFrontFall) * dt;
    mFront = std::max(std::log2(kFrontStartHz), std::min(std::log2(kMaxFreq), mFront));

    // Drain, then size next sub-block's forcing to return what is not lost (white noise of
    // variance s^2 adds about in2^2 s^2 per sample to each mode it reaches).
    const double gCascade = std::exp(-kDrainRate * mN * dt);
    const double gContact = std::exp(-kContactRate * press * dt);
    const double e0 = mBank.energy();
    const double fromCascade = e0 * (1.0 - gCascade * gCascade);
    const double fromContact = e0 * gCascade * gCascade * (1.0 - gContact * gContact);
    mBank.applyLoss((float)(gCascade * gContact));
    const double returned = fromCascade + kContactScatter * fromContact;
    const double reach = hat ? (mCumIn2.empty() ? 0.0 : mCumIn2.back()) : in2BelowFront();
    mForcePower = (reach > 0.0) ? returned / (reach * (double)kSubBlock) : 0.0;
}

// Sum of input-2 gains squared over the modes below the cascade front.
double Cymbal::in2BelowFront() const {
    if (mModeFreq.empty()) return 0.0;
    const float fc = (float)std::exp2(mFront);
    const auto it = std::upper_bound(mModeFreq.begin(), mModeFreq.end(), fc);
    // Modes are in frequency order up to the +-split of each pair, close enough for a sum.
    const size_t k = (size_t)(it - mModeFreq.begin());
    return (k == 0) ? 0.0 : mCumIn2[k - 1];
}

bool Cymbal::isQuiet() const {
    return mStrikePos < 0 && mBank.isSilent(2.0e-6f) && mForcePower < 1e-14;
}
