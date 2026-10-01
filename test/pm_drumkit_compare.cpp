// pm_drumkit_compare — A/B of DrumKit's body models (Body param: Additive vs Modal) for every
// family that has been ported to ModalBank. Both models consume the same noise sequence for the
// attack layers, so any difference is the body itself.
//
// Checks, per drum, at full velocity (where the strike burst is a near-impulse and the two models
// should agree):
//   - the RMS envelope (10 ms windows) tracks within kEnvTolDb wherever it is above -60 dB;
//   - the energy at each body mode frequency (after the pitch glide has settled) agrees within
//     kModeTolDb.
// At low velocity the modal strike is deliberately softer-edged (it rolls the upper modes off),
// so only the report is printed there.
//
// Usage:  pm-drumkit-compare [out-dir]
// With an out-dir, also writes <family>_additive.wav / <family>_modal.wav (32-bit float stereo)
// playing the same sequence, for listening.

#include "VoiceManager.hpp"
#include "pm_addresses.h"
#include "pm_midi.h"
#include "pm_params.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static int g_fails = 0;
#define CHECK(cond, msg)                                                                           \
   do {                                                                                            \
      if (!(cond)) {                                                                               \
         std::printf("FAIL: %s\n", (msg));                                                         \
         ++g_fails;                                                                                \
      }                                                                                            \
   } while (0)

static constexpr double   kSR = 48000.0;
static constexpr uint32_t kN  = 512;

static constexpr double kEnvTolDb  = 1.5;
static constexpr double kModeTolDb = 2.0;

struct Stereo { std::vector<float> L, R; };

static void setVMParam(VoiceManager& vm, MIDIParamItem item, float v) {
   vm.setVoiceManagerParameter(MODULE_PARAM_ADDRESS(ModuleParamKindMIDI, ModuleInstance1, item), v);
}
static void setKit(VoiceManager& vm, DrumKitParamItem item, float v) {
   vm.setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindDrumKit, ModuleInstance1, item), v);
}

static std::unique_ptr<VoiceManager> makeKit(int bodyModel) {
   auto vm = std::make_unique<VoiceManager>(kSR, kN);
   setVMParam(*vm, MIDIParamItemVoiceCount, 16);
   setVMParam(*vm, MIDIParamItemChannelFilterValue, 0);
   setVMParam(*vm, MIDIParamItemGroupFilterValue, 0);
   setVMParam(*vm, MIDIParamItemVoiceStealAlgorithm, VoiceStealAlgorithmOldest);
   setVMParam(*vm, MIDIParamItemFreeRun, 0);
   setVMParam(*vm, MIDIParamItemLegato, 0);
   setKit(*vm, DrumKitParamItemBodyModel, (float)bodyModel);
   return vm;
}

// A hit: note + 16-bit velocity at a time offset (s).
struct Hit { double at; uint8_t note; uint16_t vel; };

static Stereo renderSequence(int bodyModel, const std::vector<Hit>& hits, double seconds,
                             const std::vector<std::pair<DrumKitParamItem, float>>& params = {}) {
   auto vm = makeKit(bodyModel);
   for (const auto& p : params) setKit(*vm, p.first, p.second);
   const size_t total = (size_t)(seconds * kSR);
   Stereo out;
   out.L.assign(total, 0.0f);
   out.R.assign(total, 0.0f);
   size_t next = 0;
   for (size_t pos = 0; pos < total;) {
      // Split blocks at hit times so each strike lands on its exact sample (as the plugin does).
      size_t end = std::min(total, pos + kN);
      if (next < hits.size()) {
         const size_t hitPos = (size_t)(hits[next].at * kSR);
         if (hitPos <= pos) {
            vm->handleMIDIMessage(pm::makeNoteOn(hits[next].note, hits[next].vel));
            vm->handleMIDIMessage(pm::makeNoteOff(hits[next].note));   // drums ignore note length
            ++next;
            continue;
         }
         end = std::min(end, hitPos);
      }
      vm->process((uint32_t)(end - pos), &out.L[pos], &out.R[pos]);
      pos = end;
   }
   return out;
}

static double toDb(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

// RMS of the mid channel over [t0, t0 + len).
static double rmsAt(const Stereo& s, double t0, double len) {
   const size_t a = (size_t)(t0 * kSR), b = std::min(s.L.size(), (size_t)((t0 + len) * kSR));
   double acc = 0.0;
   for (size_t i = a; i < b; ++i) {
      const double m = 0.5 * ((double)s.L[i] + (double)s.R[i]);
      acc += m * m;
   }
   return (b > a) ? std::sqrt(acc / (double)(b - a)) : 0.0;
}

// Goertzel magnitude of the mid channel at `hz` over [t0, t0 + len), Hann-windowed.
static double toneAt(const Stereo& s, double hz, double t0, double len) {
   const size_t a = (size_t)(t0 * kSR), n = (size_t)(len * kSR);
   const double w = 2.0 * M_PI * hz / kSR, c = 2.0 * std::cos(w);
   double s1 = 0.0, s2 = 0.0;
   for (size_t i = 0; i < n && a + i < s.L.size(); ++i) {
      const double win = 0.5 * (1.0 - std::cos(2.0 * M_PI * (double)i / (double)(n - 1)));
      const double x = 0.5 * ((double)s.L[a + i] + (double)s.R[a + i]) * win;
      const double s0 = x + c * s1 - s2;
      s2 = s1; s1 = s0;
   }
   return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (0.25 * (double)n);
}

static void writeWav(const std::string& path, const Stereo& s) {
   FILE* f = std::fopen(path.c_str(), "wb");
   if (!f) { std::printf("  (could not write %s)\n", path.c_str()); return; }
   const uint32_t frames = (uint32_t)s.L.size(), dataBytes = frames * 2 * 4;
   auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
   auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
   std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
   std::fwrite("fmt ", 1, 4, f); u32(16); u16(3 /*IEEE float*/); u16(2);
   u32((uint32_t)kSR); u32((uint32_t)kSR * 8); u16(8); u16(32);
   std::fwrite("data", 1, 4, f); u32(dataBytes);
   for (uint32_t i = 0; i < frames; ++i) { std::fwrite(&s.L[i], 4, 1, f); std::fwrite(&s.R[i], 4, 1, f); }
   std::fclose(f);
   std::printf("  wrote %s\n", path.c_str());
}

// ---- Toms ------------------------------------------------------------------------------------

// Mirror of DrumKitModule.cpp's tom voicing, just enough to know where the modes sit.
static double tomF0(uint8_t note, float tune) {
   const double ratio = (note % 12 == 7) ? 0.71 : (note % 12 == 9) ? 1.00 : 1.34;
   return 90.0 * std::pow(240.0 / 90.0, (double)tune) * ratio;
}
static constexpr double kTomRatios[5] = { 1.0, 1.59, 2.14, 2.30, 2.65 };

static void compareTom(const char* name, uint8_t note, uint16_t vel, bool strict,
                       const std::vector<std::pair<DrumKitParamItem, float>>& params, float tune) {
   const std::vector<Hit> hit = { { 0.0, note, vel } };
   const Stereo a = renderSequence(0, hit, 2.5, params);
   const Stereo m = renderSequence(1, hit, 2.5, params);

   // Envelope, 10 ms windows, while the additive render is above -60 dB of its peak.
   double peakRms = 0.0;
   for (double t = 0.0; t < 2.4; t += 0.01) peakRms = std::max(peakRms, rmsAt(a, t, 0.01));
   double worstEnv = 0.0, worstAt = 0.0;
   for (double t = 0.0; t < 2.4; t += 0.01) {
      const double ra = rmsAt(a, t, 0.01);
      if (toDb(ra / peakRms) < -60.0) break;
      const double d = std::fabs(toDb(rmsAt(m, t, 0.01)) - toDb(ra));
      if (d > worstEnv) { worstEnv = d; worstAt = t; }
   }

   // Mode energies once the glide has settled (it is ~1% sharp by 0.18 s).
   const double f0 = tomF0(note, tune);
   double worstMode = 0.0;
   char modes[160]; int off = 0;
   for (double r : kTomRatios) {
      const double ta = toneAt(a, f0 * r, 0.20, 0.20), tm = toneAt(m, f0 * r, 0.20, 0.20);
      const double d = toDb(tm) - toDb(ta);
      worstMode = std::max(worstMode, std::fabs(d));
      off += std::snprintf(modes + off, sizeof(modes) - off, " %+5.1f", d);
   }

   std::printf("  %-26s vel %5.2f  env worst %4.1f dB @%3.0f ms   modes (dB, modal-additive):%s\n",
               name, vel / 65535.0, worstEnv, worstAt * 1000.0, modes);
   if (strict) {
      char msg[128];
      std::snprintf(msg, sizeof(msg), "%s: envelope within %.1f dB", name, kEnvTolDb);
      CHECK(worstEnv <= kEnvTolDb, msg);
      std::snprintf(msg, sizeof(msg), "%s: mode energies within %.1f dB", name, kModeTolDb);
      CHECK(worstMode <= kModeTolDb, msg);
   }
}

int main(int argc, char** argv) {
   const std::string outDir = (argc > 1) ? argv[1] : "";

   std::printf("Toms (Additive vs Modal):\n");
   struct { const char* name; uint8_t note; } toms[3] = {
      { "low tom", 55 }, { "mid tom", 57 }, { "high tom", 59 },
   };
   const float defTune = 0.49f;
   for (const auto& t : toms) {
      compareTom(t.name, t.note, 0xFFFF, true, {}, defTune);
      compareTom(t.name, t.note, 0x5000, false, {}, defTune);   // soft: report only
   }
   // Knob extremes, full velocity.
   compareTom("mid tom, tune 0 decay 1",  57, 0xFFFF, true,
              { { DrumKitParamItemTomTune, 0.0f }, { DrumKitParamItemTomDecay, 1.0f } }, 0.0f);
   compareTom("mid tom, tune 1 decay 0",  57, 0xFFFF, true,
              { { DrumKitParamItemTomTune, 1.0f }, { DrumKitParamItemTomDecay, 0.0f } }, 1.0f);
   compareTom("mid tom, bright 0",        57, 0xFFFF, true, { { DrumKitParamItemTomBright, 0.0f } }, defTune);
   compareTom("mid tom, bright 1",        57, 0xFFFF, true, { { DrumKitParamItemTomBright, 1.0f } }, defTune);

   if (!outDir.empty()) {
      // Listening sequence: G A B hard, G A B soft, then a 16th-note roll on the mid tom that
      // crescendos (the modal head keeps ringing between strokes; the additive one restarts).
      std::vector<Hit> seq;
      double t = 0.0;
      for (uint8_t n : { 55, 57, 59 }) { seq.push_back({ t, n, 0xFFFF }); t += 0.9; }
      for (uint8_t n : { 55, 57, 59 }) { seq.push_back({ t, n, 0x5000 }); t += 0.9; }
      for (int i = 0; i < 16; ++i) {
         seq.push_back({ t, 57, (uint16_t)(0x3000 + i * 0x0C00) });
         t += 0.09;
      }
      const double len = t + 1.5;
      writeWav(outDir + "/toms_additive.wav", renderSequence(0, seq, len));
      writeWav(outDir + "/toms_modal.wav",    renderSequence(1, seq, len));
   }

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails == 0 ? 0 : 1;
}
