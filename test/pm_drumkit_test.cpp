// pm_drumkit_test — the DrumKit voice end to end through VoiceManager (the plugin's patch:
// DrumKit -> Final). Every pitch class sounds a drum with finite output; voices free after
// note-off once the hit has rung out; and the cross-voice hi-hat choke (ChokeGroupState via
// ModuleSharedState) cuts a ringing open hat when a closed hat is struck.

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
   static const char* kNames[12] = {
      "C kick", "C# crash1", "D kick2", "D# crash2", "E snare", "F snare2",
      "F# open hat", "G low tom", "G# half hat", "A mid tom", "A# closed hat", "B high tom",
   };

   // 1. Every pitch class sounds, stays finite, rings out after note-off, and frees its voice.
   for (int pc = 0; pc < 12; ++pc) {
      auto vm = makeKit();
      const uint8_t note = (uint8_t)(48 + pc);
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
      std::printf("  %-14s peak=%.3f\n", kNames[pc], hit);
   }

   // 2. Same pitch class in another octave is the same drum (mapping is note % 12).
   {
      const float lo = peakWindow(36, 0, 4);   // C2 kick
      const float hi = peakWindow(72, 0, 4);   // C5 kick
      CHECK(std::fabs(lo - hi) < 1e-4f, "kit repeats every octave");
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
      CHECK(gmPeak(36) > 0.5f * peakWindow(48, 0, 4), "GM 36 is the PMU kick");   // C = kick
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
