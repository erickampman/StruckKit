// pm_selftest — exercises the Phase 1b foundation: the decoded UMP message over uw::ump, the
// MIDI 1.0 decoder, and the vendored AU-free leaf files. Prints "ALL PASS".

#include "CPPTypes.hpp"
#include "KPBuffPairHelper.hpp"
#include "ParamAddressMacros.h"
#include "pm_midi.h"

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

int main() {
   // --- param address (kind<<16 | instance<<8 | item) round-trip ---
   uint32_t addr = MODULE_PARAM_ADDRESS(0x42, 3, 7);
   CHECK(MODULE_KIND_FROM_ADDRESS(addr) == 0x42, "addr kind");
   CHECK(MODULE_INSTANCE_FROM_ADDRESS(addr) == 3, "addr instance");
   CHECK(MODULE_ITEM_FROM_ADDRESS(addr) == 7, "addr item");

   // --- KPBuffPair static buffers ---
   CHECK(KPBuffPairHelper::zeroes()->first->data[0] == 0.0f, "zeroes");
   CHECK(KPBuffPairHelper::ones()->second->data[MODULE_BUFF_SIZE - 1] == 1.0f, "ones");

   // --- UMP predicates ---
   auto on = pm::makeNoteOn(69, 0x8000);
   CHECK(pm::isNoteOn(on) && !pm::isNoteOff(on) && on.note == 69, "makeNoteOn");
   auto off = pm::makeNoteOff(69);
   CHECK(pm::isNoteOff(off) && !pm::isNoteOn(off), "makeNoteOff");
   auto on0 = pm::makeNoteOn(60, 1);
   on0.velocity = 0; // note-on with velocity 0 must read as note-off (spec convention)
   CHECK(pm::isNoteOff(on0), "vel0 == noteOff");

   // --- decode a raw MIDI 2.0 note-on UMP word pair ---
   uw::MidiMessage um;
   um.words[0]  = (0x4u << 28) | (0x9u << 20) | (69u << 8) | 0u;
   um.words[1]  = (0xC000u << 16);
   um.wordCount = 2;
   auto dec = pm::fromUmp(um);
   CHECK(pm::isNoteOn(dec) && dec.note == 69 && dec.velocity == 0xC000, "fromUmp note-on");
   um.words[0] |= uint32_t(pm::ump::NoteAttribute::Pitch7_9);
   um.words[1] |= (uint16_t(69) << 9) | 256;
   auto dec79 = pm::fromUmp(um);
   CHECK(dec79.attributeType == pm::ump::NoteAttribute::Pitch7_9 &&
            dec79.attribute == ((69u << 9) | 256u),
         "fromUmp pitch 7.9 attribute");

   // --- microtonal: attribute type 3 (7.9), note 69 + fraction 256 ---
   uint16_t attr = (uint16_t(69) << 9) | 256;
   double f = pm::pitchAttributeToFrequency(attr);
   double expect = pm::noteToFrequency(69) * std::pow(2.0, 256.0 / 6144.0);
   CHECK(std::fabs(f - expect) < 1e-6, "pitch attribute");

   // --- MIDI 1.0 bytes → UMP (uw::ump parser + upconverter) ---
   pm::Midi1Decoder midi1;
   std::vector<pm::UmpMessage> got;
   auto collect = [&](const pm::UmpMessage& m) { got.push_back(m); };
   const uint8_t noteOn[3] = {0x91, 64, 127};
   midi1.feed(noteOn, 3, collect);
   CHECK(got.size() == 1 && pm::isNoteOn(got[0]) && got[0].note == 64 && got[0].channel == 1 &&
            got[0].velocity == 0xFFFF,
         "midi1 note-on");
   got.clear();
   const uint8_t noteOn0[3] = {0x91, 64, 0};
   midi1.feed(noteOn0, 3, collect);
   CHECK(got.size() == 1 && pm::isNoteOff(got[0]) && got[0].note == 64, "midi1 vel0 → off");
   got.clear();
   const uint8_t sustain[3] = {0xB0, 64, 127};
   midi1.feed(sustain, 3, collect);
   CHECK(got.size() == 1 && got[0].status == pm::ump::Status::ControlChange &&
            got[0].ccIndex == 64 && got[0].ccData == 0xFFFFFFFFu,
         "midi1 CC");
   got.clear();
   const uint8_t clock[1] = {0xF8};
   midi1.feed(clock, 1, collect);
   CHECK(got.empty(), "midi1 clock ignored");

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
