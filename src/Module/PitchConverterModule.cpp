//
//  PitchConverterModule.mm
//
//  Created by Eric Kampman on 5/10/25.
//

#include "PitchConverter.hpp"

#include "PitchConverterModule.hpp"

struct PitchConverterHelperStruct {
    float curValue;
    
    explicit PitchConverterHelperStruct()
        : curValue(0) {}
};

const void InitPitchConverter(uint32_t count, Module* module)
{
    module->state = new PitchConverterHelperStruct();
}
const void DeinitPitchConverter(Module* module)
{
    delete static_cast<PitchConverterHelperStruct*>(module->state);
    module->state = nullptr;
}

const KPBuffPair* ProcessPitchConverter(uint64_t processCallID, uint32_t count, Module* module)
{
    auto* state = static_cast<PitchConverterHelperStruct*>(module->state);
    float* outL = module->output.first->data;
    float* outR = module->output.second->data;

    std::fill_n(outL, count, state->curValue);
    std::fill_n(outR, count, state->curValue);

    return &module->output;
}

void MIDIPitchConverter(const pm::UmpMessage& message, Module* module)
{
    auto* state = static_cast<PitchConverterHelperStruct*>(module->state);

    switch (message.status) {
        case pm::ump::Status::NoteOn:
        {
            uint8_t noteNum = message.note;
            const auto attributeType = message.attributeType;
            uint16_t attribute = message.attribute;

            switch (attributeType) {
                case pm::ump::NoteAttribute::None:
                    state->curValue = module->pitchConverter->midiToPitch(noteNum);
                    break;
                case pm::ump::NoteAttribute::Pitch7_9:
                    // needs testing!!!
                    state->curValue = module->pitchConverter->Pitch79ToOctaveOffset(attribute);
                    break;
                // not handled
                case pm::ump::NoteAttribute::ManufacturerSpecific:
                case pm::ump::NoteAttribute::ProfileSpecific:
                    break;
            }
        }
            break;
        case pm::ump::Status::NoteOff:
        default:
            break;

    }
}
