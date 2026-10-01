// pm_voicemanager_test — polyphony off Apple. A 3-note chord allocates 3 voices and produces
// audio; releasing all notes frees the voices and decays to silence. This is the layer that
// makes the synth polyphonic (a single Voice is inherently one note).

#include "VoiceManager.hpp"
#include "pm_addresses.h"
#include "pm_midi.h"
#include "pm_params.h"

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

static void setVMParam(VoiceManager& vm, MIDIParamItem item, float v) {
   vm.setVoiceManagerParameter(MODULE_PARAM_ADDRESS(ModuleParamKindMIDI, ModuleInstance1, item), v);
}

int main() {
   const double sr = 48000.0;
   const uint32_t N = 512;

   VoiceManager vm(sr, N);
   // VoiceManager's params are not zero-initialised — set sane defaults (the plugin does too).
   setVMParam(vm, MIDIParamItemVoiceCount, 8);
   setVMParam(vm, MIDIParamItemChannelFilterValue, 0);
   setVMParam(vm, MIDIParamItemGroupFilterValue, 0);
   setVMParam(vm, MIDIParamItemVoiceStealAlgorithm, 0);
   setVMParam(vm, MIDIParamItemFreeRun, 0);
   setVMParam(vm, MIDIParamItemLegato, 0);

   float L[512], R[512];
   auto render = [&]() {
      std::memset(L, 0, sizeof(L));
      std::memset(R, 0, sizeof(R));
      vm.process(N, L, R);
   };
   auto peak = [&]() {
      float p = 0.0f;
      for (uint32_t i = 0; i < N; ++i)
         p = std::fmax(p, std::fabs(L[i]));
      return p;
   };

   // Play a C-major chord: three distinct notes → three voices.
   vm.handleMIDIMessage(pm::makeNoteOn(60, 0x8000));
   vm.handleMIDIMessage(pm::makeNoteOn(64, 0x8000));
   vm.handleMIDIMessage(pm::makeNoteOn(67, 0x8000));
   CHECK(vm.activeCount() == 3, "3 voices active for a 3-note chord");

   float chordPeak = 0.0f;
   for (int b = 0; b < 20; ++b) {
      render();
      chordPeak = std::fmax(chordPeak, peak());
   }
   CHECK(chordPeak > 0.05f, "chord produces audio");

   // Release all notes → voices ring down, then free on silence. A struck drum keeps sounding
   // after note-off (its own decay IS the envelope), so a voice only goes Inactive once its
   // output falls below the 1e-5 RMS silence threshold. With the DrumKit patch, C/E/G are kick,
   // snare and low tom, all gone well inside the 600 blocks (~6.4 s @ 48 kHz) rendered here.
   vm.handleMIDIMessage(pm::makeNoteOff(60));
   vm.handleMIDIMessage(pm::makeNoteOff(64));
   vm.handleMIDIMessage(pm::makeNoteOff(67));
   for (int b = 0; b < 600; ++b)
      render();
   CHECK(peak() < chordPeak * 0.1f, "chord decays to silence after release");
   CHECK(vm.activeCount() == 0, "all voices released");

   std::printf("VoiceManager: chordPeak=%.3f active-after-release=%u\n", chordPeak,
               vm.activeCount());
   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
