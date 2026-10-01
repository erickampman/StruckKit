// pm_voice_test — the first full PolygraphModular VOICE rendering a note on Linux.
// Builds the v1 patch (PitchConverter → Slew → SinOsc → Combine ×ADSR → Final), sends a MIDI
// note-on, and checks audio comes out; then note-off and checks it decays. This is also the
// first end-to-end exercise of Combine/Final/Slew with real signal flowing through them.

#include "ModuleFactory.hpp"
#include "PitchConverter.hpp"
#include "Voice.hpp"
#include "pm_addresses.h"
#include "pm_midi.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int g_fails = 0;
#define CHECK(cond, msg)                                                                           \
   do {                                                                                            \
      if (!(cond)) {                                                                               \
         std::printf("FAIL: %s\n", (msg));                                                         \
         ++g_fails;                                                                                \
      }                                                                                            \
   } while (0)

// Wire the fixed v1 patch (what VoiceManager::buildDefaultPatch will do).
static void wirePatch(Voice& v) {
   Module* pc = v.getModule(pitchConverter1Address);
   Module* slew = v.getModule(slew1Address);
   Module* osc = v.getModule(sinOsc1Address);
   Module* env = v.getModule(adsr1Address);
   Module* comb = v.getModule(combine1Address);
   Module* fin = v.getModule(finalAddress);
   v.connectModules(comb, fin, finalInputAddress);           // Combine → Final
   v.connectModules(osc, comb, combine1Input1Address);       // SinOsc → Combine in1
   v.connectModules(env, comb, combine1Input2Address);       // ADSR   → Combine in2 (VCA)
   v.connectModules(slew, osc, sinOsc1PitchInputAddress);    // Slew   → SinOsc pitch
   v.connectModules(pc, slew, slew1InputAddress);            // PitchConv → Slew
}

static float blockPeak(const float* b, uint32_t n) {
   float p = 0.0f;
   for (uint32_t i = 0; i < n; ++i)
      p = std::fmax(p, std::fabs(b[i]));
   return p;
}

int main() {
   const double sr = 48000.0;
   const uint32_t N = 512;

   ModuleFactory factory(sr, N);
   PitchConverter pitchConv(440.0);
   Voice v(sr);
   v.initializeModules(&factory, &pitchConv, nullptr);
   wirePatch(v);

   float L[512], R[512];
   // Per-block contract (as VoiceManager does): zero the mix buffer, then the voice writes
   // into it while active. An inactive/silent voice leaves it at zero.
   auto renderBlock = [&]() {
      std::memset(L, 0, sizeof(L));
      std::memset(R, 0, sizeof(R));
      v.process(N, L, R);
   };

   // Note on → the voice should sound.
   v.handleMIDIMessage(pm::makeNoteOn(69, 0x8000)); // A4
   float peak = 0.0f;
   for (int b = 0; b < 20; ++b) {
      renderBlock();
      peak = std::fmax(peak, blockPeak(L, N));
   }
   CHECK(v.isActive(), "voice is active after note-on");
   CHECK(peak > 0.05f, "voice renders audio on note-on");

   // Note off → the voice should decay toward silence.
   v.handleMIDIMessage(pm::makeNoteOff(69));
   for (int b = 0; b < 120; ++b)
      renderBlock();
   const float tail = blockPeak(L, N);
   CHECK(tail < peak * 0.25f, "voice decays after note-off");

   std::printf("Voice: peak=%.3f tail=%.4f\n", peak, tail);
   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
