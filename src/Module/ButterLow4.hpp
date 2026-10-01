//
//  ButterLow4.hpp — 4th-order (24 dB/oct) Butterworth low-pass as a cascade of two DF1 biquads.
//  Vendored from PolygraphModular (BaseFramework). Self-contained: carries its own ButterCoeff
//  struct so it doesn't drag in ButterLow.hpp / Pow2CircBuff. Coefficients are computed once per
//  control block (recalc, which calls tan()); the inner loop is pure IIR arithmetic.
//
//  my1/my2 (stage-1 output history) serve double duty: denominator feedback for stage 1 AND
//  numerator feed-forward (x[n-1], x[n-2]) for stage 2.
//

#ifndef STRUCKKIT_BUTTERLOW4_HPP
#define STRUCKKIT_BUTTERLOW4_HPP

#include <cmath>

// One biquad stage's coefficients (b0,b1,b2 feed-forward; a1,a2 feedback).
struct ButterCoeff {
    double b0, b1, b2;
    double a1, a2;
};

// Flat 2-tap history for one channel of the 4th-order cascade.
struct ButterLow4State {
    double ix1 = 0, ix2 = 0;   // stage-1 input history  (x[n-1], x[n-2])
    double my1 = 0, my2 = 0;   // mid (stage-1 output) history
    double oy1 = 0, oy2 = 0;   // final output history

    void clear() { ix1 = ix2 = my1 = my2 = oy1 = oy2 = 0.0; }
};

class ButterLow4 {
public:
    explicit ButterLow4(double sampleRate)
        : sampleRate_(sampleRate), q_(1.0)
    {}

    void setSampleRate(double sr) { sampleRate_ = sr; }
    void reset()                  { state_.clear(); }
    ButterLow4State& getState()   { return state_; }

    // Compute one stage's biquad coefficients. `k` is the stage's Butterworth pole-pair damping
    // (0.765367 / 1.847759); `q` scales it for resonance. Call once per block per stage.
    void recalc(double sampleRate, double cutoff, double k, double q, ButterCoeff* blc)
    {
        // Clamp before tan() to avoid the asymptote at Nyquist.
        if (cutoff > sampleRate * 0.4999) cutoff = sampleRate * 0.4999;

        // Bilinear-transform prewarping: tan(pi*fc/sr) corrects cutoff at high frequencies.
        const double omega        = tan(M_PI * cutoff / sampleRate);
        const double omegaSquared = omega * omega;
        const double M            = k / q;

        const double c0 = 1.0 + M * omega + omegaSquared;   // (K = 1, so KK = 1)
        const double c1 = 2.0 * (omegaSquared - 1.0);
        const double c2 = 1.0 - M * omega + omegaSquared;

        // One reciprocal, three multiplications — not three separate divisions.
        const double inv_c0 = 1.0 / c0;
        const double b00    = omegaSquared * inv_c0;
        blc->b0 = b00;
        blc->b1 = 2.0 * b00;
        blc->b2 = b00;
        blc->a1 = c1 * inv_c0;
        blc->a2 = c2 * inv_c0;
    }

    // Process one sample with pre-computed stage coefficients.
    static inline float processWithCoeff(double in,
                                         const ButterCoeff& blc1,
                                         const ButterCoeff& blc2,
                                         ButterLow4State& st)
    {
        // Stage 1: standard 2nd-order DF1 biquad.
        const double m = blc1.b0 * in
                       + blc1.b1 * st.ix1 + blc1.b2 * st.ix2
                       - blc1.a1 * st.my1 - blc1.a2 * st.my2;

        // Stage 2: m is x[n]; my1/my2 are x[n-1]/x[n-2] for this stage.
        const double y = blc2.b0 * m
                       + blc2.b1 * st.my1 + blc2.b2 * st.my2
                       - blc2.a1 * st.oy1 - blc2.a2 * st.oy2;

        // Shift history.
        st.ix2 = st.ix1;  st.ix1 = in;
        st.my2 = st.my1;  st.my1 = m;
        st.oy2 = st.oy1;  st.oy1 = y;

        return (float)y;
    }

    static constexpr double K_STAGE_1 = 0.765367;
    static constexpr double K_STAGE_2 = 1.847759;

private:
    ButterLow4State state_;
    double          sampleRate_;
    double          q_;
};

#endif // STRUCKKIT_BUTTERLOW4_HPP
