// pm_midi.cpp — non-inline helpers for the decoded UMP message (pm_midi.h).

#include "pm_midi.h"

#include <cstdio>

namespace pm {

UmpMessage makeNoteOn(uint8_t note, uint16_t velocity, uint8_t channel, uint8_t group) {
   UmpMessage m;
   m.messageType = ump::MessageType::Midi2ChannelVoice;
   m.group       = group;
   m.status      = ump::Status::NoteOn;
   m.channel     = channel;
   m.note        = note;
   m.velocity    = velocity ? velocity : 1; // a real note-on is non-zero velocity
   m.attributeType = ump::NoteAttribute::None;
   return m;
}

UmpMessage makeNoteOff(uint8_t note, uint8_t channel, uint8_t group) {
   UmpMessage m;
   m.messageType = ump::MessageType::Midi2ChannelVoice;
   m.group       = group;
   m.status      = ump::Status::NoteOff;
   m.channel     = channel;
   m.note        = note;
   m.velocity    = 0;
   m.attributeType = ump::NoteAttribute::None;
   return m;
}

bool containsNoteNumber(const UmpMessage& m, uint8_t& note) {
   if (isNoteOnOrOff(m) || (isChannelVoice2(m) && m.status == ump::Status::PolyPressure)) {
      note = m.note;
      return true;
   }
   return false;
}

UmpMessage fromUmp(const uw::MidiMessage& u) {
   UmpMessage m;
   if (ump::messageType(u) != ump::MessageType::Midi2ChannelVoice)
      return m;
   // MIDI 2.0 channel voice, word 0: [mt:4][group:4][status:4][channel:4][byte2:8][byte1:8]
   m.messageType   = ump::MessageType::Midi2ChannelVoice;
   m.group         = ump::group(u);
   m.status        = ump::status(u);
   m.channel       = ump::channel(u);
   m.note          = ump::noteNumber(u);
   m.attributeType = static_cast<ump::NoteAttribute>(u.words[0] & 0xFF);
   m.velocity      = ump::velocity16(u);
   m.attribute     = u.words[1] & 0xFFFF;
   m.ccIndex       = (u.words[0] >> 8) & 0x7F; // for control change, byte 2 is the index
   m.ccData        = u.words[1];               // and word 1 is the full 32-bit value
   return m;
}

std::optional<UmpMessage> makeNoteOnMsgFromOnMsg(const UmpMessage& m) {
   if (!isChannelVoice2(m) || m.status != ump::Status::NoteOn)
      return std::nullopt;
   return m;
}
std::optional<UmpMessage> makeNoteOffMsgFromOnMsg(const UmpMessage& m) {
   if (!isChannelVoice2(m) || m.status != ump::Status::NoteOn)
      return std::nullopt;
   UmpMessage r = m;
   r.status = ump::Status::NoteOff;
   return r;
}
std::optional<UmpMessage> makeNoteOffMsgFromOffMsg(const UmpMessage& m) {
   if (!isChannelVoice2(m) || m.status != ump::Status::NoteOff)
      return std::nullopt;
   return m;
}

std::string toString(const UmpMessage& m) {
   char buf[96];
   std::snprintf(buf, sizeof(buf), "UMP mt=%u grp=%u st=0x%X ch=%u note=%u vel=%u attr(%u)=0x%X",
                 unsigned(m.messageType), m.group, unsigned(m.status), m.channel, m.note, m.velocity,
                 unsigned(m.attributeType),
                 m.attribute);
   return std::string(buf);
}

} // namespace pm
