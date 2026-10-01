//
//  MembraneModes.hpp
//
//  Physics of a struck drum head, as inputs for a ModalBank: which modes a circular membrane has,
//  how hard a strike at a given radius drives each one, and how a two-headed drum's enclosed air
//  couples the batter head to the resonant head.
//
//  Framework-free, like ModalBank: no Module.hpp, no params. DrumKitModule turns these into a
//  mode table per strike.
//
//  PROVENANCE: the mode table is computed, not transcribed (see kMembraneIndex). The strike
//  weights follow from the membrane's mode shapes. The two-head coupling is a lumped two-degree-
//  of-freedom model whose structure is physical but whose constants (air-spring stiffness, head
//  mass ratio, resonant-head tuning) are set by ear, and are marked so where they are defined.
//

#ifndef MembraneModes_hpp
#define MembraneModes_hpp

#include <cmath>

// ---------------------------------------------------------------------------
// Ideal circular membrane, clamped at the rim.
//
// The 2-D wave equation in polar coordinates separates into modes
//
//     u_mn(r, theta) = J_m(j_mn * r / a) * cos(m * theta)
//
// with frequency proportional to j_mn, the n-th positive zero of the Bessel function J_m. m counts
// nodal diameters, n nodal circles (including the rim). The table lists every mode up to about
// 7.5x the fundamental, sorted by frequency. Values computed with scipy.special.jn_zeros (SciPy
// 1.16.3).
//
// Only the m = 0 modes change the volume the head encloses, which is why only they couple to the
// air in a closed drum (see coupleHeads).
// ---------------------------------------------------------------------------
struct MembraneModeIndex { int m; int n; double j; };

static constexpr MembraneModeIndex kMembraneIndex[] = {
    { 0, 1, 2.40483 }, { 1, 1, 3.83171 }, { 2, 1, 5.13562 },
    { 0, 2, 5.52008 }, { 3, 1, 6.38016 }, { 1, 2, 7.01559 },
    { 4, 1, 7.58834 }, { 2, 2, 8.41724 }, { 0, 3, 8.65373 },
    { 5, 1, 8.77148 }, { 3, 2, 9.76102 }, { 6, 1, 9.93611 },
    { 1, 3, 10.17347 }, { 4, 2, 11.06471 }, { 7, 1, 11.08637 },
    { 2, 3, 11.61984 }, { 0, 4, 11.79153 }, { 8, 1, 12.22509 },
    { 5, 2, 12.33860 }, { 3, 3, 13.01520 }, { 1, 4, 13.32369 },
    { 9, 1, 13.35430 }, { 6, 2, 13.58929 }, { 4, 3, 14.37254 },
    { 10, 1, 14.47550 }, { 2, 4, 14.79595 }, { 7, 2, 14.82127 },
    { 0, 5, 14.93092 }, { 11, 1, 15.58985 }, { 5, 3, 15.70017 },
    { 8, 2, 16.03777 }, { 3, 4, 16.22347 }, { 1, 5, 16.47063 },
    { 12, 1, 16.69825 }, { 6, 3, 17.00382 }, { 9, 2, 17.24122 },
    { 4, 4, 17.61597 }, { 13, 1, 17.80144 }, { 2, 5, 17.95982 },
    { 0, 6, 18.07106 },
};
static constexpr int kMembraneIndexCount = (int)(sizeof(kMembraneIndex) / sizeof(kMembraneIndex[0]));

namespace Membrane {

// Bessel function of the first kind, J_m(x), by its power series
//
//     J_m(x) = sum_k (-1)^k (x/2)^(2k+m) / (k! (k+m)!)
//
// Used only for x <= j_max ~ 18. The largest term there is ~e^x / (2 pi x) ~ 6e5, so double
// precision keeps ~10 significant digits after cancellation — far more than the float amplitudes
// it feeds need.
inline double besselJ(int m, double x) {
    const double h = 0.5 * x;
    double term = 1.0;
    for (int i = 1; i <= m; ++i) term *= h / (double)i;   // (x/2)^m / m!
    double sum = term;
    const double h2 = h * h;
    for (int k = 1; k < 80; ++k) {
        term *= -h2 / ((double)k * (double)(k + m));
        sum += term;
        if (std::fabs(term) < 1e-17 * std::fabs(sum) && k > (int)h) break;
    }
    return sum;
}

// How strongly a point strike at radius rho (0 = centre, 1 = rim) drives mode (m, n), relative
// to the mode's own size.
//
// A point force excites each mode in proportion to the mode shape at the strike point, divided by
// the mode's norm (its "modal mass"). For the clamped membrane
//
//     integral_0^1 J_m(j r)^2 r dr = J_{m+1}(j)^2 / 2
//
// and the angular integral of cos^2(m theta) is 2 pi for m = 0, pi otherwise. The strike is taken
// on the theta = 0 line, so cos(m theta) = 1. The result is the mode's amplitude per unit impulse,
// up to one constant shared by all modes.
//
// What this reproduces: a dead-centre hit drives only the m = 0 modes (every J_m(0) is zero for
// m > 0), so it sounds round and tom-like; moving out brings in the m >= 1 modes and the head
// sounds brighter and ringier; at the rim every mode shape goes to zero, so the weights fall
// away (a real rimshot gets its edge from the rim, not the head).
inline double strikeWeight(const MembraneModeIndex& mode, double rho) {
    const double shape = besselJ(mode.m, mode.j * rho);
    const double jn1   = besselJ(mode.m + 1, mode.j);
    const double norm2 = 0.5 * jn1 * jn1 * ((mode.m == 0) ? 2.0 * M_PI : M_PI);
    return shape / std::sqrt(norm2);
}

// ---------------------------------------------------------------------------
// Two heads coupled through the enclosed air.
//
// A closed drum's air acts as a spring between its heads, but only for modes that change the
// enclosed volume — the axisymmetric (m = 0) ones. Lumping one such mode of each head into a
// single degree of freedom (batter displacement b into the drum, resonant displacement s out of
// it), the air compression is b - s and the equations of motion are
//
//     M x'' + K x = 0,   M = diag(1, mu),   K = [ wb^2 + ka   -ka           ]
//                                               [ -ka          mu wr^2 + ka  ]
//
// (stiffnesses per unit batter mass: wb, wr the heads' own in-vacuo frequencies, mu the resonant
// head's mass relative to the batter's, ka the air spring). The generalized eigenproblem gives two
// modes, both with motion in both heads, which is the characteristic split low pair of a two-
// headed drum: one where the heads move together and the air is barely squeezed, one where they
// move against each other and the air spring adds its stiffness.
//
// Eigenvectors are mass-normalized (v^T M v = 1), so b^2 + mu s^2 = 1 and mu s^2 is the share of
// the mode's energy in the resonant head — which is what snare wires pressing on that head damp.
// ---------------------------------------------------------------------------
struct CoupledMode {
    double ratio;   // frequency relative to the batter head's in-vacuo frequency wb
    double b;       // batter-head participation (mass-normalized)
    double s;       // resonant-head participation (mass-normalized)
};

// wrRatio = wr / wb, airK = ka / wb^2 (must be > 0: with no air spring the heads are simply
// uncoupled). Returns the two modes, lower first.
inline void coupleHeads(double wrRatio, double mu, double airK, CoupledMode out[2]) {
    const double kb = 1.0 + airK;                       // K[0][0]
    const double ks = mu * wrRatio * wrRatio + airK;    // K[1][1]
    const double kc = -airK;                            // K[0][1] = K[1][0]
    // det(K - lambda M) = 0:  mu lambda^2 - (mu kb + ks) lambda + (kb ks - kc^2) = 0
    const double A = mu, B = -(mu * kb + ks), C = kb * ks - kc * kc;
    const double disc = std::sqrt(std::fmax(0.0, B * B - 4.0 * A * C));
    const double lam[2] = { (-B - disc) / (2.0 * A), (-B + disc) / (2.0 * A) };
    for (int i = 0; i < 2; ++i) {
        // First row of (K - lambda M) v = 0:  (kb - lambda) b + kc s = 0  ->  s = (kb - lambda) b / ka
        double b = 1.0;
        double s = (airK > 0.0) ? (kb - lam[i]) / airK : 0.0;
        const double n = std::sqrt(b * b + mu * s * s);
        out[i] = { std::sqrt(std::fmax(0.0, lam[i])), b / n, s / n };
    }
}

} // namespace Membrane

#endif /* MembraneModes_hpp */
