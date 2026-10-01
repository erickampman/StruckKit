// pm_snare_test — the physics the snare is built from, checked piece by piece.
//   MembraneModes: Bessel accuracy, the table's frequencies are zeros of J_m, a centre strike
//                  drives only axisymmetric modes, the two-head coupling solves its eigenproblem.
//   SnareWires:    silent when the head is still (no fizz from resting contact), silent below the
//                  lift-off threshold, louder with more head motion, and tighter wires stop sooner.

#include "MembraneModes.hpp"
#include "SnareWires.hpp"

#include <cmath>
#include <cstdio>

static int g_fails = 0;
#define CHECK(cond, msg)                                                                           \
   do {                                                                                            \
      if (!(cond)) {                                                                               \
         std::printf("FAIL: %s\n", (msg));                                                         \
         ++g_fails;                                                                                \
      }                                                                                            \
   } while (0)

static constexpr double kSR = 48000.0;

// Drive the wires with a decaying sinusoidal head motion; return RMS output and the time (s) of
// the last sample louder than 1% of the peak.
struct WireRun { double rms; double lastLoud; };
static WireRun runWires(float tension, double amp, double hz = 232.0, double t60 = 0.3) {
   SnareWires w;
   w.setSampleRate(kSR);
   w.setTension(tension);
   w.reset();
   const int n = (int)(1.0 * kSR);
   static float out[48000];
   double peak = 0.0, sumsq = 0.0;
   for (int i = 0; i < n; ++i) {
      const double a = amp * std::exp(-6.907755 * i / (t60 * kSR));
      out[i] = w.process(a * std::sin(2.0 * M_PI * hz * i / kSR));
      peak = std::fmax(peak, std::fabs(out[i]));
      sumsq += (double)out[i] * out[i];
   }
   double last = 0.0;
   for (int i = 0; i < n; ++i)
      if (std::fabs(out[i]) > 0.01 * peak) last = i / kSR;
   return { std::sqrt(sumsq / n), last };
}

int main() {
   using namespace Membrane;

   // --- Bessel functions -----------------------------------------------------------------------
   CHECK(std::fabs(besselJ(0, 1.0) - 0.7651976866) < 1e-9, "J0(1)");
   CHECK(std::fabs(besselJ(1, 2.5) - 0.4970941025) < 1e-9, "J1(2.5)");
   CHECK(std::fabs(besselJ(3, 10.0) - 0.0583793793) < 1e-8, "J3(10)");
   double worstZero = 0.0;
   for (int k = 0; k < kMembraneIndexCount; ++k)
      worstZero = std::fmax(worstZero, std::fabs(besselJ(kMembraneIndex[k].m, kMembraneIndex[k].j)));
   CHECK(worstZero < 2e-5, "every table entry is a zero of its J_m");
   bool sorted = true;
   for (int k = 1; k < kMembraneIndexCount; ++k) sorted = sorted && kMembraneIndex[k].j > kMembraneIndex[k - 1].j;
   CHECK(sorted, "table sorted by frequency");
   std::printf("  Bessel: worst |J_m(j_mn)| over the table = %.1e\n", worstZero);

   // --- strike weights -------------------------------------------------------------------------
   bool centreOnlyM0 = true;
   for (int k = 0; k < kMembraneIndexCount; ++k) {
      const double w = strikeWeight(kMembraneIndex[k], 0.0);
      if (kMembraneIndex[k].m == 0) centreOnlyM0 = centreOnlyM0 && std::fabs(w) > 0.1;
      else                          centreOnlyM0 = centreOnlyM0 && std::fabs(w) < 1e-12;
   }
   CHECK(centreOnlyM0, "a centre strike drives only the m = 0 modes");
   CHECK(std::fabs(strikeWeight(kMembraneIndex[0], 0.9999)) < 1e-3, "a strike at the rim barely drives (0,1)");
   CHECK(std::fabs(strikeWeight(kMembraneIndex[1], 0.5)) > std::fabs(strikeWeight(kMembraneIndex[1], 0.1)),
         "(1,1) is driven harder off-centre than near it");

   // --- two-head coupling ----------------------------------------------------------------------
   {
      const double wr = 1.5, mu = 0.6, ka = 0.5;
      CoupledMode p[2];
      coupleHeads(wr, mu, ka, p);
      bool ok = p[0].ratio < p[1].ratio;
      for (int i = 0; i < 2; ++i) {
         const double lam = p[i].ratio * p[i].ratio;
         // Both rows of (K - lambda M) v = 0.
         const double r0 = (1.0 + ka - lam) * p[i].b - ka * p[i].s;
         const double r1 = -ka * p[i].b + (mu * wr * wr + ka - mu * lam) * p[i].s;
         ok = ok && std::fabs(r0) < 1e-9 && std::fabs(r1) < 1e-9;
         ok = ok && std::fabs(p[i].b * p[i].b + mu * p[i].s * p[i].s - 1.0) < 1e-9;   // mass-normalised
      }
      // Lower mode: heads move together (b, s same sign, as b is into the drum and s out of it).
      ok = ok && (p[0].b * p[0].s > 0.0) && (p[1].b * p[1].s < 0.0);
      CHECK(ok, "coupled pair solves the eigenproblem, mass-normalised, in-phase below anti-phase");
      std::printf("  coupled pair: %.3f (b %.2f s %.2f), %.3f (b %.2f s %.2f) x batter f0\n",
                  p[0].ratio, p[0].b, p[0].s, p[1].ratio, p[1].b, p[1].s);
   }

   // --- snare wires ----------------------------------------------------------------------------
   {
      const WireRun still = runWires(0.5f, 0.0);
      CHECK(still.rms == 0.0, "wires resting on a still head make no sound");

      const WireRun tiny = runWires(0.5f, 0.0005);
      CHECK(tiny.rms < 1e-6, "head motion below the lift-off threshold leaves the wires silent");

      const WireRun soft = runWires(0.5f, 0.02), hard = runWires(0.5f, 0.1);
      CHECK(hard.rms > soft.rms * 1.5, "a harder hit rattles the wires harder");

      const WireRun loose = runWires(0.0f, 0.1), tight = runWires(1.0f, 0.1);
      CHECK(loose.lastLoud > tight.lastLoud * 2.0, "loose wires rattle on much longer than tight ones");
      std::printf("  wires: rms soft %.4f hard %.4f; rattle lasts loose %.0f ms, default %.0f ms, tight %.0f ms\n",
                  soft.rms, hard.rms, 1000 * loose.lastLoud, 1000 * hard.lastLoud, 1000 * tight.lastLoud);
   }

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails == 0 ? 0 : 1;
}
