// StruckKitPlugin.cpp — DSP side: DPF Plugin wrapper around the vendored PolygraphModular
// voice engine. DPF hands us MIDI 1.0 events + a parameter interface and generates the CLAP
// (VST3/LV2) wrapper; all the synthesis is the same VoiceManager the self-tests exercise.
//
//   MIDI events  → pm::UmpMessage → VoiceManager (polyphony / stealing / mono)
//   params       → VoiceManager::setParameter() (propagates to every voice, live)
//   run()        → chunked VoiceManager::process()
//
// The voice is DrumKit → Final (see buildDefaultPatch): each note's pitch class picks a drum, and
// every param is a DrumKit module item.

#include "DistrhoPlugin.hpp"

#include <algorithm>
#include <memory>

#include "StruckKitParams.h"

#include "ParamAddressMacros.h"
#include "VoiceManager.hpp"
#include "pm_addresses.h"
#include "pm_midi.h"
#include "pm_params.h"

START_NAMESPACE_DISTRHO

using namespace struckkit;

class StruckKitPlugin : public Plugin
{
public:
    StruckKitPlugin()
        : Plugin(kParamCount, 0, 0) // params, programs, states
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            fValues[i] = kParams[i].def;
        initVoiceManager();
    }

protected:
    /* -- information ------------------------------------------------------ */
    const char* getLabel()       const override { return "StruckKit"; }
    const char* getDescription() const override
    {
        return "A modal-synthesis drum kit built on the PolygraphModular voice engine.";
    }
    const char* getMaker()    const override { return "Unlikelyware"; }
    const char* getHomePage() const override { return "https://unlikelyware.com/struckkit"; }
    const char* getLicense()  const override { return "MIT"; }
    uint32_t    getVersion()  const override { return d_version(0, 1, 0); }
    int64_t     getUniqueId() const override { return d_cconst('S', 't', 'k', 't'); }

    /* -- init ------------------------------------------------------------- */
    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo; // 0 inputs; two output channels form a stereo pair
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= kParamCount)
            return;
        const ParamSpec& s = kParams[index];
        parameter.hints      = kParameterIsAutomatable
                             | (s.logarithmic ? kParameterIsLogarithmic : 0u)
                             | (s.integer     ? kParameterIsInteger     : 0u);
        parameter.name       = s.name;
        parameter.symbol     = s.symbol;
        parameter.unit       = s.unit;
        parameter.ranges.min = s.min;
        parameter.ranges.max = s.max;
        parameter.ranges.def = s.def;
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < kParamCount ? fValues[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kParamCount)
            return;
        fValues[index] = value;
        applyParam(index, value);
    }

    /* -- lifecycle -------------------------------------------------------- */
    void sampleRateChanged(double) override { initVoiceManager(); }
    void activate()               override { if (fVoiceManager) fVoiceManager->reset(); }

    /* -- process ---------------------------------------------------------- */
    void run(const float**, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        float* outL = outputs[0];
        float* outR = outputs[1];

        uint32_t pos = 0, ev = 0;
        while (pos < frames)
        {
            // Dispatch every MIDI event scheduled at or before the current frame.
            for (; ev < midiEventCount && midiEvents[ev].frame <= pos; ++ev)
                dispatchMidi(midiEvents[ev]);

            uint32_t next = frames;
            if (ev < midiEventCount && midiEvents[ev].frame < frames)
                next = midiEvents[ev].frame;

            renderSegment(outL, outR, pos, next);
            pos = next;
        }
    }

private:
    std::unique_ptr<VoiceManager> fVoiceManager;
    float                         fValues[kParamCount];

    // Map a DPF param index onto its DrumKit module item address.
    static AUParameterAddress vmAddress(uint32_t paramIndex)
    {
        static constexpr DrumKitParamItem kItems[kParamCount] = {
            DrumKitParamItemLevel,         DrumKitParamItemKeyMap,
            DrumKitParamItemCrashSize,     DrumKitParamItemCrashSize2,
            DrumKitParamItemCrashThin,     DrumKitParamItemCrashDecay,
            DrumKitParamItemCrashStroke,
            DrumKitParamItemSnareTune,     DrumKitParamItemSnareRing,
            DrumKitParamItemSnarePosition, DrumKitParamItemSnareTension,
            DrumKitParamItemSnareWires,
            DrumKitParamItemTomTune,       DrumKitParamItemTomRing,
            DrumKitParamItemTomPosition,   DrumKitParamItemTomReso,
            DrumKitParamItemHatSize,       DrumKitParamItemHatThin,
            DrumKitParamItemHatSizzle,
            DrumKitParamItemCongaTune,     DrumKitParamItemBongoTune,
            DrumKitParamItemTimbaleTune,   DrumKitParamItemHandRing,
            DrumKitParamItemPercTune,      DrumKitParamItemPercDecay,
        };
        if (paramIndex >= kParamCount)
            return 0;
        return MODULE_PARAM_ADDRESS(ModuleParamKindDrumKit, ModuleInstance1, kItems[paramIndex]);
    }

    void applyParam(uint32_t index, float value)
    {
        if (!fVoiceManager)
            return;
        const AUParameterAddress a = vmAddress(index);
        if (a != 0)
            fVoiceManager->setParameter(a, value);
    }

    // (Re)build the VoiceManager for the current sample rate and push all defaults into it.
    void initVoiceManager()
    {
        fVoiceManager = std::make_unique<VoiceManager>(getSampleRate(), MODULE_BUFF_SIZE);

        // VoiceManager params aren't zero-initialised. A kit is always polyphonic with a fixed
        // voice count; no legato (every hit is a fresh strike).
        setConfig(MIDIParamItemVoiceCount, kVoiceCount);
        setConfig(MIDIParamItemLegato, 0);
        setConfig(MIDIParamItemChannelFilterValue, 0);
        setConfig(MIDIParamItemGroupFilterValue, 0);
        setConfig(MIDIParamItemVoiceStealAlgorithm, VoiceStealAlgorithmOldest);
        setConfig(MIDIParamItemFreeRun, 0);

        // Push every param so the engine matches the exposed parameter values.
        for (uint32_t i = 0; i < kParamCount; ++i)
            applyParam(i, fValues[i]);
    }

    void setConfig(MIDIParamItem item, float v)
    {
        fVoiceManager->setVoiceManagerParameter(
            MODULE_PARAM_ADDRESS(ModuleParamKindMIDI, ModuleInstance1, item), v);
    }

    void dispatchMidi(const MidiEvent& me)
    {
        const uint8_t* d = me.size > MidiEvent::kDataSize ? me.dataExt : me.data;
        const pm::UmpMessage m = pm::fromMidi1Bytes(d[0],
                                                    me.size > 1 ? d[1] : 0,
                                                    me.size > 2 ? d[2] : 0);
        if (pm::isChannelMessage(m))
            fVoiceManager->handleMIDIMessage(m);
    }

    // Render [start,end) by chunking to the module block size; VoiceManager zeroes + sums voices.
    void renderSegment(float* outL, float* outR, uint32_t start, uint32_t end)
    {
        for (uint32_t p = start; p < end;)
        {
            const uint32_t chunk = std::min<uint32_t>(end - p, MODULE_BUFF_SIZE);
            fVoiceManager->process(chunk, outL + p, outR + p);
            p += chunk;
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StruckKitPlugin)
};

Plugin* createPlugin()
{
    return new StruckKitPlugin();
}

END_NAMESPACE_DISTRHO
