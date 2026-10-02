// pm_drumkit_render — a listening and measuring bench for the physically voiced drums. Not a CTest
// (it asserts nothing); it renders and reports, so voicing changes can be judged by numbers as
// well as by ear.
//
// Usage:  pm-drumkit-render [out-dir]
//
// For each snare variant it prints the peak, the decay (time to -20 / -40 / -60 dB), how long the
// wires rattle and how loud they are against the body, the energy split across frequency bands
// over the attack, and the strongest partials in the body. With an out-dir it writes snare_*.wav (32-bit float stereo).

#include "VoiceManager.hpp"
#include "pm_addresses.h"
#include "pm_midi.h"
#include "pm_params.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

static constexpr double   kSR = 48000.0;
static constexpr uint32_t kN  = 512;

struct Stereo { std::vector<float> L, R; };
struct Hit    { double at; uint8_t note; uint16_t vel; };
using Params = std::vector<std::pair<DrumKitParamItem, float>>;

static void setVM(VoiceManager& vm, MIDIParamItem item, float v) {
   vm.setVoiceManagerParameter(MODULE_PARAM_ADDRESS(ModuleParamKindMIDI, ModuleInstance1, item), v);
}

static Stereo render(const std::vector<Hit>& hits, double seconds, const Params& params) {
   VoiceManager vm(kSR, kN);
   setVM(vm, MIDIParamItemVoiceCount, 16);
   setVM(vm, MIDIParamItemChannelFilterValue, 0);
   setVM(vm, MIDIParamItemGroupFilterValue, 0);
   setVM(vm, MIDIParamItemVoiceStealAlgorithm, VoiceStealAlgorithmOldest);
   setVM(vm, MIDIParamItemFreeRun, 0);
   setVM(vm, MIDIParamItemLegato, 0);
   for (const auto& p : params)
      vm.setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindDrumKit, ModuleInstance1, p.first), p.second);

   const size_t total = (size_t)(seconds * kSR);
   Stereo out;
   out.L.assign(total, 0.0f);
   out.R.assign(total, 0.0f);
   size_t next = 0;
   for (size_t pos = 0; pos < total;) {
      size_t end = std::min(total, pos + kN);
      if (next < hits.size()) {
         const size_t hitPos = (size_t)(hits[next].at * kSR);
         if (hitPos <= pos) {
            vm.handleMIDIMessage(pm::makeNoteOn(hits[next].note, hits[next].vel));
            vm.handleMIDIMessage(pm::makeNoteOff(hits[next].note));
            ++next;
            continue;
         }
         end = std::min(end, hitPos);
      }
      vm.process((uint32_t)(end - pos), &out.L[pos], &out.R[pos]);
      pos = end;
   }
   return out;
}

static double mid(const Stereo& s, size_t i) { return 0.5 * ((double)s.L[i] + (double)s.R[i]); }

static double rms(const Stereo& s, double t0, double len) {
   const size_t a = (size_t)(t0 * kSR), b = std::min(s.L.size(), (size_t)((t0 + len) * kSR));
   double acc = 0.0;
   for (size_t i = a; i < b; ++i) acc += mid(s, i) * mid(s, i);
   return (b > a) ? std::sqrt(acc / (double)(b - a)) : 0.0;
}

static double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

// Time for the 5 ms RMS envelope to fall `drop` dB below its peak.
static double decayTo(const Stereo& s, double drop) {
   double peak = 0.0;
   for (double t = 0.0; t < 0.1; t += 0.001) peak = std::max(peak, rms(s, t, 0.005));
   for (double t = 0.0; t + 0.005 < s.L.size() / kSR; t += 0.001)
      if (t > 0.005 && db(rms(s, t, 0.005) / peak) < -drop) return t;
   return s.L.size() / kSR;
}

// Goertzel power of the mid channel at hz over [t0, t0+len), Hann window.
static double tone(const Stereo& s, double hz, double t0, double len) {
   const long   a = (long)std::lround(t0 * kSR);   // may be negative: zero-padded before the hit
   const size_t n = (size_t)(len * kSR);
   const double w = 2.0 * M_PI * hz / kSR, c = 2.0 * std::cos(w);
   double s1 = 0, s2 = 0;
   for (size_t i = 0; i < n; ++i) {
      const long   at  = a + (long)i;
      const double x   = (at >= 0 && at < (long)s.L.size()) ? mid(s, (size_t)at) : 0.0;
      const double win = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (n - 1)));
      const double s0 = x * win + c * s1 - s2;
      s2 = s1; s1 = s0;
   }
   return std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2);
}

// Energy share in octave-ish bands over [t0, t0+len), via a coarse Goertzel scan.
static void bands(const Stereo& s, double t0, double len, double out[5]) {
   static const double edges[6] = { 40, 400, 1200, 3000, 8000, 20000 };
   for (int b = 0; b < 5; ++b) out[b] = 0.0;
   double total = 0.0;
   for (double f = 40.0; f < 20000.0; f *= 1.02) {
      const double p = tone(s, f, t0, len) * f;   // per log-frequency step
      for (int b = 0; b < 5; ++b)
         if (f >= edges[b] && f < edges[b + 1]) out[b] += p;
      total += p;
   }
   for (int b = 0; b < 5; ++b) out[b] = (total > 0) ? 100.0 * out[b] / total : 0.0;
}

// The few strongest spectral peaks below 2 kHz over [t0, t0+len).
static std::string peaks(const Stereo& s, double t0, double len) {
   std::vector<std::pair<double, double>> spec;
   for (double f = 60.0; f < 2000.0; f += 2.0) spec.push_back({ f, tone(s, f, t0, len) });
   std::vector<std::pair<double, double>> pk;
   for (size_t i = 1; i + 1 < spec.size(); ++i)
      if (spec[i].second > spec[i - 1].second && spec[i].second >= spec[i + 1].second)
         pk.push_back(spec[i]);
   std::sort(pk.begin(), pk.end(), [](auto& a, auto& b) { return a.second > b.second; });
   std::string r;
   const double top = pk.empty() ? 1.0 : pk[0].second;
   for (size_t i = 0; i < pk.size() && i < 6; ++i) {
      char b[48];
      std::snprintf(b, sizeof(b), " %4.0fHz(%+.0f)", pk[i].first, 10.0 * std::log10(pk[i].second / top));
      r += b;
   }
   return r;
}

static void writeWav(const std::string& path, const Stereo& s) {
   FILE* f = std::fopen(path.c_str(), "wb");
   if (!f) return;
   const uint32_t frames = (uint32_t)s.L.size(), bytes = frames * 8;
   auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
   auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
   std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVE", 1, 4, f);
   std::fwrite("fmt ", 1, 4, f); u32(16); u16(3); u16(2); u32((uint32_t)kSR); u32((uint32_t)kSR * 8); u16(8); u16(32);
   std::fwrite("data", 1, 4, f); u32(bytes);
   for (uint32_t i = 0; i < frames; ++i) { std::fwrite(&s.L[i], 4, 1, f); std::fwrite(&s.R[i], 4, 1, f); }
   std::fclose(f);
}

static void report(const char* name, uint8_t note, uint16_t vel, const Params& p) {
   const Stereo s = render({ { 0.0, note, vel } }, 2.0, p);
   const Stereo noWires = [&] { Params q = p; q.push_back({ DrumKitParamItemSnareWires, 0.0f }); return render({ { 0.0, note, vel } }, 2.0, q); }();
   // Wire-only signal = full minus no-wires (the body and attack layers are identical).
   Stereo w = s;
   for (size_t i = 0; i < w.L.size(); ++i) { w.L[i] -= noWires.L[i]; w.R[i] -= noWires.R[i]; }
   double peak = 0.0;
   for (size_t i = 0; i < s.L.size(); ++i) peak = std::max(peak, std::fabs(mid(s, i)));
   // Bands over a 200 ms window centred on the hit's first 50 ms, so the attack is fully weighted.
   double b[5];
   bands(s, -0.075, 0.2, b);
   std::printf("  %-22s peak %5.2f  decay -20/-40/-60 dB: %3.0f/%3.0f/%3.0f ms  wires -40 dB: %3.0f ms"
               "  wire/body %+5.1f dB\n",
               name, peak, 1000 * decayTo(s, 20), 1000 * decayTo(s, 40), 1000 * decayTo(s, 60),
               1000 * decayTo(w, 40), db(rms(w, 0.0, 0.15) / rms(noWires, 0.0, 0.15)));
   std::printf("  %-22s bands(0-125ms) <400 %4.1f%%  400-1.2k %4.1f%%  1.2-3k %4.1f%%  3-8k %4.1f%%  >8k %4.1f%%"
               "   body peaks:%s\n",
               "", b[0], b[1], b[2], b[3], b[4], peaks(noWires, 0.02, 0.15).c_str());
}

// The strongest spectral peak between lo and hi Hz over [t0, t0+len), to 0.5 Hz.
static double peakHz(const Stereo& s, double lo, double hi, double t0, double len) {
   double best = lo, bestP = -1.0;
   for (double f = lo; f < hi; f += 0.5) {
      const double p = tone(s, f, t0, len);
      if (p > bestP) { bestP = p; best = f; }
   }
   return best;
}

// Toms: level and decay, the main partials, and the pitch bend (the fundamental early in the note
// against late in it).
static void reportTom(const char* name, uint8_t note, uint16_t vel, const Params& p) {
   const Stereo s = render({ { 0.0, note, vel } }, 3.0, p);
   double peak = 0.0;
   for (size_t i = 0; i < s.L.size(); ++i) peak = std::max(peak, std::fabs(mid(s, i)));
   // Follow one partial: the strongest low one late in the note, then the same partial (within
   // +-12%) early on.
   const double late  = peakHz(s, 40.0, 400.0, 0.25, 0.25);
   const double early = peakHz(s, late * 0.97, late * 1.12, 0.0, 0.05);
   std::printf("  %-24s peak %4.2f  decay -20/-40/-60 dB: %4.0f/%4.0f/%4.0f ms  pitch %5.1f -> %5.1f Hz (%+.2f st)\n",
               name, peak, 1000 * decayTo(s, 20), 1000 * decayTo(s, 40), 1000 * decayTo(s, 60),
               early, late, 12.0 * std::log2(early / late));
   std::printf("  %-24s partials:%s\n", "", peaks(s, 0.05, 0.3).c_str());
}

// Percussion octave: peak, decay, the band split over the first 100 ms, and the strongest partials
// early in the note.
static void reportPerc(const char* name, uint8_t note, uint16_t vel, const Params& p) {
   const Stereo s = render({ { 0.0, note, vel } }, 5.0, p);
   double peak = 0.0;
   for (size_t i = 0; i < s.L.size(); ++i) peak = std::max(peak, std::fabs(mid(s, i)));
   double b[5];
   bands(s, 0.0, 0.1, b);
   std::printf("  %-22s peak %4.2f  -20/-40 dB %5.0f/%5.0f ms  <400 %4.1f%% 400-1.2k %4.1f%% 1.2-3k %4.1f%% 3-8k %4.1f%% >8k %4.1f%%\n",
               name, peak, 1000 * decayTo(s, 20), 1000 * decayTo(s, 40), b[0], b[1], b[2], b[3], b[4]);
   std::printf("  %-22s partials:%s\n", "", peaks(s, 0.01, 0.1).c_str());
}

// Cymbals: peak, then per time window the level, spectral centroid and share above 6 kHz — a
// crash's bloom shows as the centroid and the >6k share climbing after the hit.
static void reportCymbal(const char* name, uint8_t note, uint16_t vel, const Params& p) {
   const Stereo s = render({ { 0.0, note, vel } }, 3.0, p);
   double peak = 0.0;
   for (size_t i = 0; i < s.L.size(); ++i) peak = std::max(peak, (double)std::max(std::fabs(s.L[i]), std::fabs(s.R[i])));
   std::printf("  %-26s peak %4.2f  -40 dB at %4.0f ms\n", name, peak, 1000 * decayTo(s, 40));
   static const double win[][2] = { { 0, .01 }, { .01, .05 }, { .05, .15 }, { .15, .4 }, { .4, 1 }, { 1, 2 } };
   std::printf("  %-26s", "");
   for (const auto& w : win) {
      double tot = 0.0, cen = 0.0, hi = 0.0;
      for (double f = 100.0; f < 18000.0; f *= 1.06) {
         const double pw = tone(s, f, w[0], w[1] - w[0]) * f;
         tot += pw; cen += pw * f; if (f > 6000.0) hi += pw;
      }
      std::printf(" [%3.0f-%4.0fms %4.0fdB %5.0fHz %2.0f%%]", 1000 * w[0], 1000 * w[1],
                  db(rms(s, w[0], w[1] - w[0])), tot > 0 ? cen / tot : 0.0, tot > 0 ? 100 * hi / tot : 0.0);
   }
   std::printf("\n");
}

int main(int argc, char** argv) {
   const std::string outDir = (argc > 1) ? argv[1] : "";
   const uint8_t E = 64, F = 65;

   std::printf("Snare:\n");
   report("default, vel 1.0", E, 0xFFFF, {});
   report("default, vel 0.5", E, 0x8000, {});
   report("ghost, vel 0.15", E, 0x2600, {});
   report("tension 0 (loose)", E, 0xC000, { { DrumKitParamItemSnareTension, 0.0f } });
   report("tension 1 (tight)", E, 0xC000, { { DrumKitParamItemSnareTension, 1.0f } });
   report("position 0 (centre)", E, 0xC000, { { DrumKitParamItemSnarePosition, 0.0f } });
   report("position 1 (edge)", E, 0xC000, { { DrumKitParamItemSnarePosition, 1.0f } });
   report("ring 0", E, 0xC000, { { DrumKitParamItemSnareRing, 0.0f } });
   report("ring 1", E, 0xC000, { { DrumKitParamItemSnareRing, 1.0f } });
   report("rimshot, vel 1.0", F, 0xFFFF, {});

   std::printf("Toms:\n");
   const uint8_t G = 67, A = 69, B = 71;
   reportTom("floor 16\", vel 1.0", G, 0xFFFF, {});
   reportTom("floor 16\", vel 0.3", G, 0x4C00, {});
   reportTom("rack 12\", vel 1.0", A, 0xFFFF, {});
   reportTom("rack 10\", vel 1.0", B, 0xFFFF, {});
   reportTom("rack 12\", reso 0 (low)", A, 0xFFFF, { { DrumKitParamItemTomReso, 0.0f } });
   reportTom("rack 12\", reso 1 (high)", A, 0xFFFF, { { DrumKitParamItemTomReso, 1.0f } });
   reportTom("rack 12\", centre hit", A, 0xFFFF, { { DrumKitParamItemTomPosition, 0.0f } });
   reportTom("rack 12\", edge hit", A, 0xFFFF, { { DrumKitParamItemTomPosition, 1.0f } });
   reportTom("floor, ring 0", G, 0xFFFF, { { DrumKitParamItemTomRing, 0.0f } });
   reportTom("floor, ring 1", G, 0xFFFF, { { DrumKitParamItemTomRing, 1.0f } });

   std::printf("Percussion octave (vel 1.0):\n");
   static const struct { const char* name; uint8_t note; } kPerc[] = {
      { "tumba (C)", 48 }, { "cowbell (C#)", 49 }, { "conga (D)", 50 }, { "claves (D#)", 51 },
      { "conga mute (E)", 52 }, { "bongo hembra (F)", 53 }, { "wood block (F#)", 54 },
      { "bongo macho (G)", 55 }, { "triangle mute (G#)", 56 }, { "timbale low (A)", 57 },
      { "triangle (A#)", 58 }, { "timbale high (B)", 59 },
   };
   for (const auto& k : kPerc) reportPerc(k.name, k.note, 0xFFFF, {});
   reportPerc("conga (D), vel 0.4", 50, 0x6600, {});
   reportPerc("GM agogo high (67)", 67, 0xFFFF, { { DrumKitParamItemKeyMap, 1.0f } });
   reportPerc("GM agogo low (68)", 68, 0xFFFF, { { DrumKitParamItemKeyMap, 1.0f } });
   reportPerc("GM wood block low (77)", 77, 0xFFFF, { { DrumKitParamItemKeyMap, 1.0f } });

   const uint8_t Cs = 61, Ds = 63, Fs = 66, Gs = 68, As = 70;
   std::printf("Crash contrast (strongest partials, 50-300 ms):\n");
   for (uint8_t n : { Cs, Ds }) {
      const Stereo c = render({ { 0.0, n, 0xC000 } }, 1.0, {});
      double tot = 0.0, cen = 0.0;
      for (double f = 100.0; f < 18000.0; f *= 1.06) { const double pw = tone(c, f, 0.05, 0.25) * f; tot += pw; cen += pw * f; }
      std::printf("  %s: centroid %5.0f Hz, partials:%s\n", n == Cs ? "crash 1" : "crash 2", tot > 0 ? cen / tot : 0.0,
                  peaks(c, 0.05, 0.25).c_str());
   }
   std::printf("Cymbals (window: level, centroid, share above 6 kHz):\n");
   reportCymbal("crash 1 (20\"), vel 1.0", Cs, 0xFFFF, {});
   reportCymbal("crash 1 (20\"), vel 0.5", Cs, 0x8000, {});
   reportCymbal("crash 1 (20\"), vel 0.15", Cs, 0x2600, {});
   reportCymbal("crash 2 (14\"), vel 1.0", Ds, 0xFFFF, {});
   reportCymbal("crash, thin 1", Cs, 0xFFFF, { { DrumKitParamItemCrashThin, 1.0f } });
   reportCymbal("crash, thin 0 (heavy)", Cs, 0xFFFF, { { DrumKitParamItemCrashThin, 0.0f } });
   reportCymbal("crash, stroke 1 (tip/bow)", Cs, 0xB000, { { DrumKitParamItemCrashStroke, 1.0f } });
   reportCymbal("hat open, vel 1.0", Fs, 0xFFFF, {});
   reportCymbal("hat half, vel 1.0", Gs, 0xFFFF, {});
   reportCymbal("hat closed, vel 1.0", As, 0xFFFF, {});
   reportCymbal("hat closed, vel 0.3", As, 0x4C00, {});

   if (!outDir.empty()) {
      // Cymbal study: crash at rising velocity, the 16", a ride-like tip-on-bow pattern, a heavy
      // vs thin crash; then a hi-hat groove (closed eighths, an open hat closed by the pedal, a
      // half-open wash).
      {
         std::vector<Hit> seq;
         double t = 0.0;
         for (uint16_t v : { 0x2600, 0x8000, 0xFFFF }) { seq.push_back({ t, Cs, v }); t += 3.0; }
         seq.push_back({ t, Ds, 0xFFFF }); t += 3.0;
         writeWav(outDir + "/crash_study.wav", render(seq, t + 0.5, {}));

         std::vector<Hit> ride;
         for (int i = 0; i < 16; ++i) ride.push_back({ 0.25 * i, Cs, (uint16_t)((i % 4 == 0) ? 0xC000 : 0x7000) });
         writeWav(outDir + "/crash_tip_on_bow.wav", render(ride, 6.0, { { DrumKitParamItemCrashStroke, 1.0f } }));
         writeWav(outDir + "/crash_thin.wav",  render({ { 0.0, Cs, 0xFFFF } }, 4.0, { { DrumKitParamItemCrashThin, 1.0f } }));
         writeWav(outDir + "/crash_heavy.wav", render({ { 0.0, Cs, 0xFFFF } }, 4.0, { { DrumKitParamItemCrashThin, 0.0f } }));
      }
      {
         std::vector<Hit> hh;
         double t = 0.0;
         for (int bar = 0; bar < 2; ++bar) {
            for (int i = 0; i < 8; ++i) {
               const bool open = (bar == 1 && i == 3);
               hh.push_back({ t, open ? Fs : As, (uint16_t)(i % 2 == 0 ? 0xC000 : 0x7000) });
               t += 0.2;
            }
         }
         for (int i = 0; i < 8; ++i) { hh.push_back({ t, Gs, (uint16_t)(i % 2 == 0 ? 0xC000 : 0x8000) }); t += 0.2; }
         hh.push_back({ t, Fs, 0xE000 }); t += 0.6;
         hh.push_back({ t, As, 0x9000 });   // the pedal closing it
         writeWav(outDir + "/hihat_groove.wav", render(hh, t + 1.0, {}));
      }
   }

   if (!outDir.empty()) {
      // A short groove-free study: single strokes at rising velocity, a rimshot, ghost notes, a
      // roll, then the same backbeat with loose and tight wires and centre vs edge strokes.
      std::vector<Hit> seq;
      double t = 0.0;
      for (uint16_t v : { 0x2600, 0x5000, 0x8000, 0xB000, 0xFFFF }) { seq.push_back({ t, E, v }); t += 0.6; }
      seq.push_back({ t, F, 0xFFFF }); t += 0.8;
      for (int i = 0; i < 8; ++i) { seq.push_back({ t, E, (uint16_t)(i % 4 == 0 ? 0xC000 : 0x2000) }); t += 0.125; }
      t += 0.4;
      for (int i = 0; i < 24; ++i) { seq.push_back({ t, E, (uint16_t)(0x3000 + i * 0x0700) }); t += 0.045; }
      t += 0.8;
      writeWav(outDir + "/snare_study.wav", render(seq, t + 0.5, {}));

      auto backbeat = [&](const Params& p, const std::string& file) {
         std::vector<Hit> b;
         for (int i = 0; i < 4; ++i) b.push_back({ 0.5 * i, E, 0xD000 });
         writeWav(outDir + "/" + file, render(b, 2.5, p));
      };
      backbeat({ { DrumKitParamItemSnareTension, 0.0f } }, "snare_loose.wav");
      backbeat({ { DrumKitParamItemSnareTension, 1.0f } }, "snare_tight.wav");
      backbeat({ { DrumKitParamItemSnarePosition, 0.0f } }, "snare_centre.wav");
      backbeat({ { DrumKitParamItemSnarePosition, 1.0f } }, "snare_edge.wav");
      // Toms: each one hard, then a fill down the kit, then the floor tom soft and hard.
      {
         std::vector<Hit> seq;
         double t = 0.0;
         for (uint8_t n : { B, A, G }) { seq.push_back({ t, n, 0xE000 }); t += 1.2; }
         for (int i = 0; i < 12; ++i) {
            seq.push_back({ t, (uint8_t)(i < 4 ? B : i < 8 ? A : G), (uint16_t)(0x9000 + i * 0x0900) });
            t += 0.11;
         }
         t += 1.5;
         seq.push_back({ t, G, 0x4000 }); t += 1.5;
         seq.push_back({ t, G, 0xFFFF }); t += 2.5;
         writeWav(outDir + "/toms_study.wav", render(seq, t, {}));
      }
      // Percussion: each key of the octave, then a tumbao on the congas with bongo and cowbell.
      {
         std::vector<Hit> seq;
         double t = 0.0;
         for (uint8_t n = 48; n < 60; ++n) { seq.push_back({ t, n, 0xE000 }); t += 0.9; }
         t += 0.5;
         const double e = 0.15;   // eighth notes at 100 bpm
         for (int bar = 0; bar < 4; ++bar) {
            static const struct { int step; uint8_t note; uint16_t vel; } kTumbao[] = {
               { 0, 52, 0x9000 }, { 2, 52, 0x7000 }, { 3, 50, 0xE000 }, { 4, 52, 0x9000 },
               { 6, 50, 0xD000 }, { 7, 48, 0xE000 },
            };
            for (const auto& h : kTumbao) seq.push_back({ t + h.step * e, h.note, h.vel });
            for (int i = 0; i < 8; i += 2) seq.push_back({ t + i * e, 49, (uint16_t)(i == 0 ? 0xE000 : 0xA000) });
            for (int i = 1; i < 8; i += 2) seq.push_back({ t + i * e, 55, 0x9000 });
            t += 8 * e;
         }
         std::sort(seq.begin(), seq.end(), [](const Hit& a, const Hit& b) { return a.at < b.at; });
         writeWav(outDir + "/perc_study.wav", render(seq, t + 2.0, {}));
      }
      std::printf("wrote snare_*.wav, toms_study.wav, perc_study.wav, crash/hihat files to %s\n", outDir.c_str());
   }
   return 0;
}
