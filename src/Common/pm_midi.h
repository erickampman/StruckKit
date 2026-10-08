// pm_midi.h — the PM framework's decoded view of one MIDI 2.0 channel-voice message.
// The UMP itself (message types, status nibbles, note attributes, field layout, MIDI 1.0
// parsing and upconversion) comes from uw-core's uw::ump (external/uw-core), shared with
// StageHand, Umpire and the other plugins. This header only flattens a uw::MidiMessage into
// the fields the DSP modules and the voice state machine read and rewrite.

#ifndef PM_MIDI_H
#define PM_MIDI_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "uw/ump/Ump.h"

namespace pm {

namespace ump = uw::ump;

// A decoded channel-voice message. Always MIDI 2.0 (fromUmp/fromMidi1Bytes upconvert MIDI
// 1.0). CC / pitch-bend / per-note fields will be added as modulation modules are ported.
struct UmpMessage {
   ump::MessageType   messageType   = ump::MessageType::Utility;
   uint8_t            group         = 0;
   ump::Status        status        = ump::Status::NoteOff;
   uint8_t            channel       = 0;
   uint8_t            note          = 0;
   uint16_t           velocity      = 0; // 16-bit
   ump::NoteAttribute attributeType = ump::NoteAttribute::None;
   uint16_t           attribute     = 0;
   uint8_t            ccIndex       = 0; // control change: controller index
   uint32_t           ccData        = 0; // control change: 32-bit value
};

// --- predicates (note-on with velocity 0 counts as note-off, as the voice manager expects) -
inline bool isChannelVoice2(const UmpMessage& m) {
   return m.messageType == ump::MessageType::Midi2ChannelVoice;
}

inline bool isNoteOn(const UmpMessage& m) {
   return isChannelVoice2(m) && m.status == ump::Status::NoteOn && m.velocity != 0;
}
inline bool isNoteOff(const UmpMessage& m) {
   return isChannelVoice2(m) && (m.status == ump::Status::NoteOff ||
                                 (m.status == ump::Status::NoteOn && m.velocity == 0));
}
inline bool isNoteOnOrOff(const UmpMessage& m) {
   return isChannelVoice2(m) &&
          (m.status == ump::Status::NoteOn || m.status == ump::Status::NoteOff);
}

// --- pitch (A440; matches PolygraphModular's MIDINoteToFrequency) -------------------------
inline double noteToFrequency(int note) {
   return (440.0 / 32.0) * std::pow(2.0, (note - 9) / 12.0);
}
// Ratio for the 9-bit fraction of a semitone (equivalent to PM's gRoots512[fraction]).
inline double pitchFractionRatio(uint16_t fraction /*0..511*/) {
   return std::pow(2.0, static_cast<double>(fraction) / 6144.0); // 6144 = 512 * 12
}
// MIDI 2.0 attribute-type-3 (7.9) → Hz.
inline double pitchAttributeToFrequency(uint16_t attribute) {
   const uint8_t  note     = (attribute & 0xFE00) >> 9;
   const uint16_t fraction = (attribute & 0x01FF);
   return noteToFrequency(note) * pitchFractionRatio(fraction);
}

// --- decode (defined in pm_midi.cpp) -------------------------------------------------------
// A MIDI 2.0 channel-voice UMP (MT 0x4) → UmpMessage. Anything else (including MIDI 1.0,
// MT 0x2) leaves messageType as it is and isChannelVoice2() false: upconvert it first.
UmpMessage fromUmp(const uw::MidiMessage& m);

// MIDI 1.0 bytes → UmpMessages, through uw::ump's MIDI 1.0 parser and its MIDI 1.0 → 2.0
// upconverter. One per MIDI 1.0 stream (the DPF wrapper delivers MIDI 1.0 bytes): both keep
// state across messages (running status; bank select and RPN/NRPN selection, which become
// single MIDI 2.0 messages). A MIDI 1.0 note-on with velocity 0 becomes a note-off. Only
// channel-voice messages reach the sink.
class Midi1Decoder {
public:
   template <typename Sink>
   void feed(const uint8_t* bytes, size_t count, Sink&& sink) {
      for (size_t i = 0; i < count; ++i) {
         uw::MidiMessage midi1;
         if (!ump::parseMidi1Byte(bytes[i], fParser, midi1))
            continue;
         uw::MidiMessage midi2[ump::kMaxTranslatedMessages];
         const size_t n = ump::upconvert(midi1, fUpconvert, midi2, ump::kMaxTranslatedMessages);
         for (size_t j = 0; j < n; ++j)
            if (ump::messageType(midi2[j]) == ump::MessageType::Midi2ChannelVoice)
               sink(fromUmp(midi2[j]));
      }
   }

private:
   ump::Midi1ParserState fParser;
   ump::UpconvertState   fUpconvert;
};

// --- constructors / debug (defined in pm_midi.cpp) ----------------------------------------
UmpMessage  makeNoteOn(uint8_t note, uint16_t velocity, uint8_t channel = 0, uint8_t group = 0);
UmpMessage  makeNoteOff(uint8_t note, uint8_t channel = 0, uint8_t group = 0);
bool        containsNoteNumber(const UmpMessage& m, uint8_t& note);
std::string toString(const UmpMessage& m);

// Derive one note message from another (used by the voice state machine for retrigger/steal).
std::optional<UmpMessage> makeNoteOnMsgFromOnMsg(const UmpMessage& m);   // copy if it's a note-on
std::optional<UmpMessage> makeNoteOffMsgFromOnMsg(const UmpMessage& m);  // note-on → matching off
std::optional<UmpMessage> makeNoteOffMsgFromOffMsg(const UmpMessage& m); // copy if it's a note-off

} // namespace pm

#endif // PM_MIDI_H
