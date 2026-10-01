// pm_modalbank_test — the framework-free ModalBank core on its own.
//   - A single mode struck by a unit impulse rings at the table amplitude, at the right pitch,
//     and decays to -60 dB at its T60.
//   - Retuning while ringing keeps amplitude and phase continuous (no energy pumped in by a
//     downward glide, none lost by an upward one), and the new pitch takes effect.
//   - Position weighting can be turned off (2-D bodies supply their own excitation weights).

#include "ModalBank.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static int g_fails = 0;
#define CHECK(cond, msg)                                                                           \
   do {                                                                                            \
      if (!(cond)) {                                                                               \
         std::printf("FAIL: %s\n", (msg));                                                         \
         ++g_fails;                                                                                \
      }                                                                                            \
   } while (0)

static constexpr double kSR = 48000.0;

static const ModalMode     kOneMode[] = { { 1.0f, 1.0f, 1.0f } };
static const ModalMaterial kOne       = { kOneMode, 1, "one" };

static void setupOne(ModalBank& b, float hz, float t60) {
   b.setSampleRate(kSR);
   b.setMaterial(kOne);
   b.setModeCount(1);
   b.setFundamental(hz);
   b.setDecayT60(t60);
   b.setDampTilt(0.0f);
   b.setSpread(0.0f);
   b.setPositionWeighting(false);
}

// Peak |mid| over n samples, continuing the bank's ring (no excitation).
static float ringPeak(ModalBank& b, int n, std::vector<float>* keep = nullptr) {
   std::vector<float> L(n), R(n);
   b.processBlock(nullptr, L.data(), R.data(), n);
   float p = 0.0f;
   for (int i = 0; i < n; ++i) {
      const float m = (L[i] + R[i]) / std::sqrt(2.0f);   // undo the equal-power centre pan
      p = std::fmax(p, std::fabs(m));
      if (keep) keep->push_back(m);
   }
   return p;
}

// Zero crossings per second -> frequency estimate.
static double zcHz(const std::vector<float>& x) {
   int zc = 0;
   for (size_t i = 1; i < x.size(); ++i)
      if ((x[i - 1] < 0.0f) != (x[i] < 0.0f)) ++zc;
   return 0.5 * zc * kSR / (double)x.size();
}

int main() {
   // 1. Unit impulse -> amplitude 1 at 440 Hz, -60 dB at T60.
   {
      ModalBank b;
      setupOne(b, 440.0f, 1.0f);
      std::vector<float> ex(64, 0.0f), L(64), R(64);
      ex[0] = 1.0f;
      b.processBlock(ex.data(), L.data(), R.data(), 64);
      std::vector<float> ring;
      const float p0 = ringPeak(b, 4800, &ring);          // first 100 ms
      CHECK(std::fabs(p0 - 1.0f) < 0.02f, "unit impulse rings at the table amplitude");
      CHECK(std::fabs(zcHz(ring) - 440.0) < 5.0, "rings at the fundamental");
      ringPeak(b, (int)(1.0 * kSR) - 64 - 4800 - 240);     // to just before 1 s
      const float pT60 = ringPeak(b, 480);                 // 10 ms window centred on T60
      const double db = 20.0 * std::log10(pT60 / p0);
      CHECK(db < -55.0 && db > -63.0, "decays ~60 dB at T60");
      std::printf("  impulse: peak=%.3f  f=%.1f Hz  at T60: %.1f dB\n", p0, zcHz(ring), db);
   }

   // 2. Retune while ringing: amplitude continuous across a big down and up glide. Judged
   //    against an identical bank that is NOT retuned, so the natural decay over the glide
   //    cancels out.
   for (float to : { 300.0f, 600.0f }) {
      ModalBank b, ref;
      setupOne(b, 440.0f, 2.0f);
      setupOne(ref, 440.0f, 2.0f);
      std::vector<float> ex(64, 0.0f), L(64), R(64);
      ex[0] = 1.0f;
      b.processBlock(ex.data(), L.data(), R.data(), 64);
      ref.processBlock(ex.data(), L.data(), R.data(), 64);
      ringPeak(b, 4800);
      ringPeak(ref, 4800);
      // Glide in 32-sample steps, as DrumKit does.
      for (int s = 1; s <= 64; ++s) {
         b.setFundamental(440.0f + (to - 440.0f) * (float)s / 64.0f);
         ringPeak(b, 32);
         ringPeak(ref, 32);
      }
      std::vector<float> ring;
      const float after  = ringPeak(b, 4800, &ring);
      const float before = ringPeak(ref, 4800);   // same moment, never retuned
      char msg[96];
      std::snprintf(msg, sizeof(msg), "retune 440->%.0f keeps amplitude", to);
      CHECK(std::fabs(after / before - 1.0f) < 0.01f, msg);
      std::snprintf(msg, sizeof(msg), "retune 440->%.0f reaches the new pitch", to);
      CHECK(std::fabs(zcHz(ring) - to) < 5.0, msg);
      std::printf("  retune 440->%.0f: amplitude %.4f (unretuned %.4f), f=%.1f Hz\n", to, after, before, zcHz(ring));
   }

   // 3. Position weighting: at p = 0.5 the 1-D law cancels mode 2 (ratio 2); off, it doesn't.
   {
      static const ModalMode     kTwo[] = { { 1.0f, 1.0f, 1.0f }, { 2.0f, 1.0f, 1.0f } };
      static const ModalMaterial two    = { kTwo, 2, "two" };
      auto peakOfMode2 = [&](bool weighting) {
         ModalBank b;
         setupOne(b, 200.0f, 2.0f);
         b.setMaterial(two);
         b.setModeCount(2);
         b.setStrikePosition(0.5f);
         b.setPositionWeighting(weighting);
         b.prepare();
         std::vector<float> ex(4800, 0.0f), L(4800), R(4800);
         ex[0] = 1.0f;
         b.processBlock(ex.data(), L.data(), R.data(), 4800);
         // Goertzel at 400 Hz.
         const double w = 2.0 * M_PI * 400.0 / kSR, c = 2.0 * std::cos(w);
         double s1 = 0, s2 = 0;
         for (int i = 0; i < 4800; ++i) { const double s0 = (L[i] + R[i]) + c * s1 - s2; s2 = s1; s1 = s0; }
         return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2));
      };
      const double on = peakOfMode2(true), off = peakOfMode2(false);
      CHECK(on < off * 0.01, "strike at 0.5 cancels mode 2 with weighting on");
      CHECK(off > 1.0, "mode 2 sounds with weighting off");
   }

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails == 0 ? 0 : 1;
}
