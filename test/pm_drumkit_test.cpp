// pm_drumkit_test — the DrumKit voice end to end through VoiceManager (the plugin's patch:
// DrumKit -> Final). Every key of the StruckKit map's two octaves sounds a drum with finite output;
// voices free after note-off once the hit has rung out; the map repeats every 24 notes; and the
// cross-voice chokes (ChokeGroupState via ModuleSharedState) cut a ringing open hat when a closed
// hat is struck, and an open triangle when a muted one is.

#include "VoiceManager.hpp"
#include "pm_addresses.h"
#include "pm_midi.h"
#include "pm_params.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

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

static void setVMParam(VoiceManager& vm, MIDIParamItem item, float v) {
   vm.setVoiceManagerParameter(MODULE_PARAM_ADDRESS(ModuleParamKindMIDI, ModuleInstance1, item), v);
}

// A VoiceManager configured the way StruckKitPlugin does it.
static std::unique_ptr<VoiceManager> makeKit() {
   auto vm = std::make_unique<VoiceManager>(kSR, kN);
   setVMParam(*vm, MIDIParamItemVoiceCount, 16);
   setVMParam(*vm, MIDIParamItemChannelFilterValue, 0);
   setVMParam(*vm, MIDIParamItemGroupFilterValue, 0);
   setVMParam(*vm, MIDIParamItemVoiceStealAlgorithm, VoiceStealAlgorithmOldest);
   setVMParam(*vm, MIDIParamItemFreeRun, 0);
   setVMParam(*vm, MIDIParamItemLegato, 0);
   return vm;
}

struct Block {
   float L[kN], R[kN];
   float peak = 0.0f;
   bool  finite = true;
};

static void render(VoiceManager& vm, Block& b) {
   std::memset(b.L, 0, sizeof(b.L));
   std::memset(b.R, 0, sizeof(b.R));
   vm.process(kN, b.L, b.R);
   b.peak = 0.0f;
   for (uint32_t i = 0; i < kN; ++i) {
      if (!std::isfinite(b.L[i]) || !std::isfinite(b.R[i])) b.finite = false;
      b.peak = std::fmax(b.peak, std::fmax(std::fabs(b.L[i]), std::fabs(b.R[i])));
   }
}

// Peak over blocks [from, to) after striking `note` (and, optionally, `second` at block `at`).
static float peakWindow(uint8_t note, int from, int to, int second = -1, int at = 0) {
   auto vm = makeKit();
   Block b;
   float p = 0.0f;
   vm->handleMIDIMessage(pm::makeNoteOn(note, 0xC000));
   for (int i = 0; i < to; ++i) {
      if (second >= 0 && i == at)
         vm->handleMIDIMessage(pm::makeNoteOn((uint8_t)second, 0xC000));
      render(*vm, b);
      if (i >= from) p = std::fmax(p, b.peak);
   }
   return p;
}

int main() {
   static const char* kNames[24] = {
      "C kick", "C# crash1", "D kick2", "D# crash2", "E snare", "F snare2",
      "F# open hat", "G low tom", "G# half hat", "A mid tom", "A# closed hat", "B high tom",
      "C tumba", "C# cowbell", "D conga", "D# claves", "E conga mute", "F bongo low",
      "F# wood block", "G bongo high", "G# triangle mute", "A timbale low", "A# triangle", "B timbale high",
   };

   // 1. Every key of both octaves (kit 36-47, percussion 48-59) sounds, stays finite, rings out
   //    after note-off, and frees its voice.
   for (int pc = 0; pc < 24; ++pc) {
      auto vm = makeKit();
      const uint8_t note = (uint8_t)(36 + pc);
      Block b;
      float hit = 0.0f;
      bool finite = true;
      vm->handleMIDIMessage(pm::makeNoteOn(note, 0xC000));
      for (int i = 0; i < 4; ++i) {   // ~43 ms of the attack
         render(*vm, b);
         hit = std::fmax(hit, b.peak);
         finite = finite && b.finite;
      }
      vm->handleMIDIMessage(pm::makeNoteOff(note));
      // Longest default tail is Crash 1: DrumKit retires it at 7.5 x metalDecay, and the default
      // Crash Decay (0.66) maps to ~1.2 s, so ~9 s. Render 10 s.
      for (int i = 0; i < (int)(10.0 * kSR / kN); ++i) {
         render(*vm, b);
         finite = finite && b.finite;
      }
      char msg[96];
      std::snprintf(msg, sizeof(msg), "%s sounds", kNames[pc]);
      CHECK(hit > 0.05f, msg);
      std::snprintf(msg, sizeof(msg), "%s output finite", kNames[pc]);
      CHECK(finite, msg);
      std::snprintf(msg, sizeof(msg), "%s voice freed after ringing out", kNames[pc]);
      CHECK(vm->activeCount() == 0, msg);
      std::printf("  %-17s peak=%.3f\n", kNames[pc], hit);
   }

   // 2. The map repeats every 24 notes: 36 and 84 are the same kick, 48 and 72 the same tumba,
   //    and the octave between differs (36 kick, 48 tumba).
   {
      const float kickLo  = peakWindow(36, 0, 4);
      const float kickHi  = peakWindow(84, 0, 4);
      const float tumbaLo = peakWindow(48, 0, 4);
      const float tumbaHi = peakWindow(72, 0, 4);
      CHECK(std::fabs(kickLo - kickHi) < 1e-4f, "kit repeats every 24 notes");
      CHECK(std::fabs(tumbaLo - tumbaHi) < 1e-4f, "percussion repeats every 24 notes");
      CHECK(std::fabs(kickLo - tumbaLo) > 1e-3f, "kit and percussion octaves differ");
   }

   // 3. Hi-hat choke. An open hat alone is still ringing ~0.5 s in; struck closed hat 2 blocks
   //    after it should cut it (6 ms fast release) and itself be gone by then (45 ms decay).
   const int from = 45, to = 55;   // ~0.47 .. 0.59 s
   const float openAlone   = peakWindow(66, from, to);             // F# open hat
   const float openChoked  = peakWindow(66, from, to, 70, 2);      // + A# closed hat
   CHECK(openAlone > 0.01f, "open hat still ringing at ~0.5 s");
   CHECK(openChoked < openAlone * 0.01f, "closed hat chokes the open hat");

   // 4. Non-hats don't choke: a crash keeps ringing through a closed-hat hit.
   const float crashAlone = peakWindow(61, from, to);              // C# crash
   const float crashHat   = peakWindow(61, from, to, 70, 2);
   CHECK(crashHat > crashAlone * 0.9f, "closed hat does not choke a crash");

   std::printf("DrumKit: openAlone=%.4f openChoked=%.6f crashAlone=%.4f crashWithHat=%.4f\n",
               openAlone, openChoked, crashAlone, crashHat);

   // 4b. Triangles: a muted triangle chokes a ringing open one.
   const float triAlone  = peakWindow(58, from, to);               // A# open triangle
   const float triChoked = peakWindow(58, from, to, 56, 2);        // + G# muted triangle
   CHECK(triAlone > 0.01f, "open triangle still ringing at ~0.5 s");
   CHECK(triChoked < triAlone * 0.01f, "muted triangle chokes the open triangle");
   std::printf("Triangle: open=%.4f choked=%.6f\n", triAlone, triChoked);

   // 5. General MIDI map: GM notes play their drums, keys StruckKit has no model for stay silent,
   //    and the GM open/closed hats still choke each other.
   {
      auto gmPeak = [&](uint8_t note, int second = -1, int at = 0, int from = 0, int to = 4) {
         auto vm = makeKit();
         vm->setParameter(MODULE_PARAM_ADDRESS(ModuleParamKindDrumKit, ModuleInstance1, DrumKitParamItemKeyMap), 1.0f);
         Block b;
         float p = 0.0f;
         vm->handleMIDIMessage(pm::makeNoteOn(note, 0xC000));
         for (int i = 0; i < to; ++i) {
            if (second >= 0 && i == at) vm->handleMIDIMessage(pm::makeNoteOn((uint8_t)second, 0xC000));
            render(*vm, b);
            if (i >= from) p = std::fmax(p, b.peak);
         }
         return p;
      };
      CHECK(gmPeak(36) > 0.05f, "GM 36 (bass drum) sounds");
      CHECK(gmPeak(38) > 0.05f, "GM 38 (snare) sounds");
      CHECK(gmPeak(51) > 0.05f, "GM 51 (ride) sounds");
      CHECK(gmPeak(39) == 0.0f, "GM 39 (hand clap, not modelled) is silent");
      CHECK(std::fabs(gmPeak(36) - peakWindow(36, 0, 4)) < 1e-4f, "GM 36 is the StruckKit-map kick");
      CHECK(std::fabs(gmPeak(64) - peakWindow(48, 0, 4)) < 1e-4f, "GM 64 (low conga) is the StruckKit-map tumba");
      for (uint8_t n : { 56, 60, 61, 62, 63, 65, 66, 67, 68, 75, 76, 77, 80, 81 }) {
         char msg[64];
         std::snprintf(msg, sizeof(msg), "GM %d (percussion) sounds", n);
         CHECK(gmPeak(n) > 0.05f, msg);
      }
      CHECK(gmPeak(54) == 0.0f, "GM 54 (tambourine, not modelled) is silent");
      CHECK(gmPeak(70) == 0.0f, "GM 70 (maracas, not modelled) is silent");
      const float gmOpen   = gmPeak(46, -1, 0, from, to);
      const float gmChoked = gmPeak(46, 42, 2, from, to);
      CHECK(gmChoked < gmOpen * 0.01f, "GM closed hat (42) chokes the open hat (46)");
      std::printf("GM map: kick %.3f snare %.3f ride %.3f clap %.3f; open hat %.4f, choked %.6f\n",
                  gmPeak(36), gmPeak(38), gmPeak(51), gmPeak(39), gmOpen, gmChoked);
   }
   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails == 0 ? 0 : 1;
}
