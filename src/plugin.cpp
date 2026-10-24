// src/plugin.cpp — StruckKit: the CLAP plugin (host glue + DSP) for the modal drum kit, driven by
// the vendored PolygraphModular VoiceManager (the same engine the self-tests exercise). The voice
// is DrumKit → Final (see buildDefaultPatch): each note's pitch class picks a drum, and every param
// is a DrumKit module item.
//
//   MIDI events  → uw::ump (MIDI 1.0 → 2.0) → pm::UmpMessage → VoiceManager (polyphony / stealing)
//   params       → VoiceManager::setParameter() (propagates to every voice, live)
//   process()    → sample-accurate, chunked VoiceManager::process()
//
// ---------------------------------------------------------------------------------------------
// WHY THIS IS RAW CLAP AND NOT dpf_add_plugin
// ---------------------------------------------------------------------------------------------
// StruckKit used to be built with dpf_add_plugin. DPF's generated CLAP wrapper sizes the editor
// itself, and badly: it forwards the UI's aspect-ratio lock to the host and rewrites every size the
// host asks for (so REAPER's window and the editor fall out of step on resize), sends the host a
// resize request of its own, and on macOS converts sizes with a scale factor it caches once —
// often from the wrong screen — which opens the editor at half size on Retina. Like StruckSynth and
// PlainOldSynth, StruckKit therefore implements the clap_plugin_entry itself and uses DPF only for
// the GUI (UIExporter + NanoVG, see src/UI/StruckKitUI.cpp), whose gui extension sizes the editor
// in host units. It also takes MIDI 2.0 UMP directly, which DPF's wrapper drops.

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "StruckKitParams.h"

#include "ParamAddressMacros.h"
#include "VoiceManager.hpp"
#include "pm_addresses.h"
#include "pm_midi.h"
#include "pm_params.h"
#include "plugin_gui.h"

#ifndef STRUCKKIT_VERSION_STRING
#define STRUCKKIT_VERSION_STRING "0.0.0"
#endif

namespace {

using namespace struckkit;

// ---------------------------------------------------------------------------------------------
// Parameter plumbing
// ---------------------------------------------------------------------------------------------

// CLAP parameter ids. DPF's generated wrapper used the 0-based parameter index as the clap_id
// (DistrhoPluginCLAP.cpp: "info->id = index"), so this raw entry point keeps exactly that mapping:
// renumbering would silently break the automation in every existing host session. Values are keyed
// by symbol in state files, so those stay stable regardless.
inline constexpr uint32_t paramIdForIndex(uint32_t index) { return index; }

inline int paramIndexForId(clap_id id) {
    return id < uint32_t(kParamCount) ? int(id) : -1;
}

inline int paramIndexForSymbol(const char* symbol) {
    for (uint32_t i = 0; i < uint32_t(kParamCount); ++i) {
        if (std::strcmp(kParams[i].symbol, symbol) == 0) return int(i);
    }
    return -1;
}

inline float clampParam(uint32_t index, double value) {
    const ParamSpec& s = kParams[index];
    if (!(value == value)) return s.def;                       // NaN → default
    if (value < double(s.min)) return s.min;
    if (value > double(s.max)) return s.max;
    return float(value);
}

// Dropdown labels for the two choice params, as the UI shows them.
constexpr const char* kKeyMapChoiceNames[] = { "StruckKit", "GM", nullptr };
constexpr const char* kVoiceChoiceList[]   = { kVoiceChoiceNames[0], kVoiceChoiceNames[1],
                                               kVoiceChoiceNames[2], kVoiceChoiceNames[3], nullptr };

const char* const* choicesFor(uint32_t index) {
    if (index == uint32_t(kParamKeyMap)) return kKeyMapChoiceNames;
    if (index == uint32_t(kParamVoices)) return kVoiceChoiceList;
    return nullptr;
}

// A knob drag on the UI thread: begin_gesture → set (repeatedly) → end_gesture. These travel to the
// audio thread (or to params.flush when the plugin is idle) through this lock-free ring and come out
// as real CLAP param events, so host automation records the move instead of it being invisible.
struct GuiEdit {
    enum Kind : uint8_t { Begin = 0, Value, End };
    uint8_t  kind;
    uint32_t index;
    double   value;
};

class GuiEditRing {
public:
    bool push(const GuiEdit& edit) {
        const uint32_t t = tail.load(std::memory_order_relaxed);
        const uint32_t next = (t + 1) & (kCapacity - 1);
        if (next == head.load(std::memory_order_acquire)) return false;   // full: drop, never block
        buffer[t] = edit;
        tail.store(next, std::memory_order_release);
        return true;
    }
    bool pop(GuiEdit& out) {
        const uint32_t h = head.load(std::memory_order_relaxed);
        if (h == tail.load(std::memory_order_acquire)) return false;
        out = buffer[h];
        head.store((h + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    static constexpr uint32_t kCapacity = 512;   // power of two
    GuiEdit buffer[kCapacity];
    std::atomic<uint32_t> head{0};   // consumer
    std::atomic<uint32_t> tail{0};   // producer (UI thread)
};

struct Plugin {
    const clap_host_t* host = nullptr;
    clap_plugin_t      clap{};
    void*              gui = nullptr;          // the shell's per-instance GUI state
    double             sampleRate = DEFAULT_SAMPLE_RATE;
    std::unique_ptr<VoiceManager> voiceManager;
    pm::Midi1Decoder   midi1;                  // MIDI 1.0 bytes → UMP (keeps running status / RPN state)

    std::atomic<double> paramValues[kParamCount];
    std::atomic<bool>   paramDirtyForGui[kParamCount];   // host changed it → tell the open editor
    std::atomic<bool>   guiEditing[kParamCount];         // user is dragging it → don't fight them
    float               appliedValues[kParamCount]{};    // what the engine currently has

    GuiEditRing guiEdits;
    std::string scratch;    // state-save buffer; reused so save() need not allocate

    uint32_t reportedVoiceCount = 0;   // main thread: the voice count the host last heard about
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

void requestMainThread(Plugin* plugin) {
    if (plugin->host != nullptr && plugin->host->request_callback != nullptr)
        plugin->host->request_callback(plugin->host);
}

float paramOf(const Plugin* plugin, uint32_t index) {
    return float(plugin->paramValues[index].load(std::memory_order_relaxed));
}

uint32_t currentVoiceCount(const Plugin* plugin) {
    return uint32_t(voiceCountFor(paramOf(plugin, kParamVoices)));
}

// ---------------------------------------------------------------------------------------------
// Parameter → engine
// ---------------------------------------------------------------------------------------------

// Map a host param index onto its DrumKit module item address.
AUParameterAddress vmAddress(uint32_t paramIndex) {
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
        DrumKitParamItem(0),           // Voices: a VoiceManager setting, see applyParam
    };
    if (paramIndex >= uint32_t(kParamCount) || paramIndex == uint32_t(kParamVoices))
        return 0;
    return MODULE_PARAM_ADDRESS(ModuleParamKindDrumKit, ModuleInstance1, kItems[paramIndex]);
}

// VoiceManager's own parameters (polyphony, MIDI filtering, legato) do not live in the per-voice
// module graph, so they are addressed through the reserved MIDI kind.
void setConfig(Plugin* plugin, MIDIParamItem item, float value) {
    if (plugin->voiceManager)
        plugin->voiceManager->setVoiceManagerParameter(
            MODULE_PARAM_ADDRESS(ModuleParamKindMIDI, ModuleInstance1, item), value);
}

void applyParam(Plugin* plugin, uint32_t index, float value) {
    if (!plugin->voiceManager) return;
    if (index == uint32_t(kParamVoices)) {
        setConfig(plugin, MIDIParamItemVoiceCount, voiceCountFor(value));
        requestMainThread(plugin);   // the voice count changed: tell the host (voice-info)
        return;
    }
    const AUParameterAddress a = vmAddress(index);
    if (a != 0) plugin->voiceManager->setParameter(a, value);
}

// Push every host parameter into the engine and record what the engine now has.
void applyAllParams(Plugin* plugin) {
    for (uint32_t i = 0; i < uint32_t(kParamCount); ++i) {
        const float value = paramOf(plugin, i);
        plugin->appliedValues[i] = value;
        applyParam(plugin, i, value);
    }
}

// Called once per process() before rendering: picks up anything the UI thread changed. Comparing
// against appliedValues keeps the per-block cost at zero when nothing moved.
void applyPendingParams(Plugin* plugin) {
    for (uint32_t i = 0; i < uint32_t(kParamCount); ++i) {
        const float value = paramOf(plugin, i);
        if (value != plugin->appliedValues[i]) {
            plugin->appliedValues[i] = value;
            applyParam(plugin, i, value);
        }
    }
}

void notifyHostValuesChanged(Plugin* plugin) {
    if (plugin->host == nullptr) return;
    const auto* params = static_cast<const clap_host_params_t*>(
        plugin->host->get_extension(plugin->host, CLAP_EXT_PARAMS));
    if (params != nullptr && params->rescan != nullptr)
        params->rescan(plugin->host, CLAP_PARAM_RESCAN_VALUES);
}

// Store a value that originated on the GUI or from a state file (i.e. NOT from the host).
void storeParam(Plugin* plugin, uint32_t index, double value) {
    const float clamped = clampParam(index, value);
    plugin->paramValues[index].store(clamped, std::memory_order_relaxed);
    plugin->paramDirtyForGui[index].store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------------------------
// CLAP events
// ---------------------------------------------------------------------------------------------

// Length of a MIDI 1.0 message from its status byte. A CLAP MIDI event always carries three bytes,
// but feeding the unused ones to the running-status parser would read them as a second message.
uint32_t midi1Length(uint8_t status) {
    if (status < 0x80) return 0;
    switch (status & 0xF0) {
    case 0xC0: case 0xD0: return 2;
    case 0xF0:
        switch (status) {
        case 0xF1: case 0xF3: return 2;
        case 0xF2:            return 3;
        default:              return 1;   // SysEx framing, tune request, realtime
        }
    default: return 3;
    }
}

void feedMidi1(Plugin* plugin, const uint8_t* bytes, uint32_t count) {
    VoiceManager* vm = plugin->voiceManager.get();
    plugin->midi1.feed(bytes, count, [vm](const pm::UmpMessage& m) { vm->handleMIDIMessage(m); });
}

void processEvent(Plugin* plugin, const clap_event_header_t* header) {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) return;

    switch (header->type) {
    // Not advertised (the note port speaks MIDI / MIDI 2.0), but harmless to honour.
    case CLAP_EVENT_NOTE_ON: {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(header);
        if (!plugin->voiceManager || note->key < 0 || note->key > 127) break;
        const double velocity = std::clamp(note->velocity, 0.0, 1.0);
        plugin->voiceManager->handleMIDIMessage(
            pm::makeNoteOn(uint8_t(note->key), uint16_t(std::lround(velocity * 65535.0)),
                           uint8_t(std::max<int16_t>(note->channel, 0) & 0x0F)));
        break;
    }
    case CLAP_EVENT_NOTE_OFF: {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(header);
        if (!plugin->voiceManager || note->key < 0 || note->key > 127) break;
        plugin->voiceManager->handleMIDIMessage(
            pm::makeNoteOff(uint8_t(note->key), uint8_t(std::max<int16_t>(note->channel, 0) & 0x0F)));
        break;
    }
    case CLAP_EVENT_MIDI: {          // MIDI 1.0 bytes → uw::ump's parser + upconverter
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(header);
        if (plugin->voiceManager)
            feedMidi1(plugin, midi->data, midi1Length(midi->data[0]));
        break;
    }
    case CLAP_EVENT_MIDI2: {         // a raw UMP
        const auto* midi = reinterpret_cast<const clap_event_midi2_t*>(header);
        if (!plugin->voiceManager) break;
        const uint32_t w0 = midi->data[0];
        switch (w0 >> 28) {
        case 0x4: {                  // MIDI 2.0 channel voice
            uw::MidiMessage message;
            message.words[0]  = w0;
            message.words[1]  = midi->data[1];
            message.wordCount = 2;
            plugin->voiceManager->handleMIDIMessage(pm::fromUmp(message));
            break;
        }
        case 0x2: {                  // MIDI 1.0 channel voice in a UMP: same path as CLAP_EVENT_MIDI
            const uint8_t bytes[3] = { uint8_t(w0 >> 16), uint8_t(w0 >> 8), uint8_t(w0) };
            feedMidi1(plugin, bytes, midi1Length(bytes[0]));
            break;
        }
        default:
            break;
        }
        break;
    }
    case CLAP_EVENT_PARAM_VALUE: {
        const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
        const int index = paramIndexForId(event->param_id);
        if (index < 0) break;
        const uint32_t i = uint32_t(index);
        const float clamped = clampParam(i, event->value);
        plugin->paramValues[i].store(clamped, std::memory_order_relaxed);
        plugin->paramDirtyForGui[i].store(true, std::memory_order_release);
        if (plugin->voiceManager) {
            plugin->appliedValues[i] = clamped;
            applyParam(plugin, i, clamped);
        }
        requestMainThread(plugin);
        break;
    }
    default:
        break;
    }
}

// GUI edits → CLAP param/gesture events. Called from process() and from params.flush() (those two
// are mutually exclusive per the CLAP spec, which is what makes the SPSC ring sufficient).
void emitGuiEdits(Plugin* plugin, const clap_output_events_t* out) {
    GuiEdit edit;
    while (plugin->guiEdits.pop(edit)) {
        if (edit.index >= uint32_t(kParamCount)) continue;
        if (out == nullptr || out->try_push == nullptr) continue;
        const clap_id id = clap_id(paramIdForIndex(edit.index));

        if (edit.kind == GuiEdit::Value) {
            clap_event_param_value_t event{};
            event.header.size     = sizeof(event);
            event.header.time     = 0;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type     = CLAP_EVENT_PARAM_VALUE;
            event.param_id        = id;
            event.note_id         = -1;
            event.port_index      = -1;
            event.channel         = -1;
            event.key             = -1;
            event.value           = edit.value;
            out->try_push(out, &event.header);
        } else {
            clap_event_param_gesture_t event{};
            event.header.size     = sizeof(event);
            event.header.time     = 0;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type     = (edit.kind == GuiEdit::Begin) ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                                 : CLAP_EVENT_PARAM_GESTURE_END;
            event.param_id        = id;
            out->try_push(out, &event.header);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Plugin lifecycle
// ---------------------------------------------------------------------------------------------

bool plug_init(const clap_plugin_t*) { return true; }

void plug_destroy(const clap_plugin_t* plugin) { delete self(plugin); }

bool plug_activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t) {
    Plugin* p = self(plugin);
    p->sampleRate = sampleRate > 0.0 ? sampleRate : DEFAULT_SAMPLE_RATE;
    p->voiceManager = std::make_unique<VoiceManager>(p->sampleRate, MODULE_BUFF_SIZE);

    // VoiceManager params aren't zero-initialised. A kit is always polyphonic (the Voices param sets
    // the count, pushed with the rest below); no legato (every hit is a fresh strike).
    setConfig(p, MIDIParamItemLegato, 0.0f);
    setConfig(p, MIDIParamItemChannelFilterValue, 0.0f);
    setConfig(p, MIDIParamItemGroupFilterValue, 0.0f);
    setConfig(p, MIDIParamItemVoiceStealAlgorithm, float(VoiceStealAlgorithmOldest));
    setConfig(p, MIDIParamItemFreeRun, 0.0f);

    applyAllParams(p);       // includes the Voices param, so polyphony is set up
    return true;
}

void plug_deactivate(const clap_plugin_t* plugin) { self(plugin)->voiceManager.reset(); }

bool plug_start_processing(const clap_plugin_t*) { return true; }

void plug_stop_processing(const clap_plugin_t*) {}

void plug_reset(const clap_plugin_t* plugin) {
    Plugin* p = self(plugin);
    if (p->voiceManager) p->voiceManager->reset();
}

void plug_on_main_thread(const clap_plugin_t* plugin) {
    Plugin* p = self(plugin);

    // voice-info: CLAP asks for changed() whenever the voice count changes (Voices dropdown).
    const uint32_t voices = currentVoiceCount(p);
    if (voices != p->reportedVoiceCount && p->host != nullptr) {
        p->reportedVoiceCount = voices;
        const auto* info = static_cast<const clap_host_voice_info_t*>(
            p->host->get_extension(p->host, CLAP_EXT_VOICE_INFO));
        if (info != nullptr && info->changed != nullptr) info->changed(p->host);
    }

    if (p->gui == nullptr) return;
    for (uint32_t i = 0; i < uint32_t(kParamCount); ++i) {
        if (!p->paramDirtyForGui[i].exchange(false, std::memory_order_acq_rel)) continue;
        if (p->guiEditing[i].load(std::memory_order_relaxed)) continue;   // the user is dragging it
        sk_gui_push_param(p->gui, i, p->paramValues[i].load(std::memory_order_relaxed));
    }
}

clap_process_status plug_process(const clap_plugin_t* plugin, const clap_process_t* process) {
    Plugin* p = self(plugin);

    const clap_input_events_t* in = process->in_events;
    const uint32_t numEvents = in != nullptr ? in->size(in) : 0;

    emitGuiEdits(p, process->out_events);

    const uint32_t frames = process->frames_count;
    float* left = nullptr;
    float* right = nullptr;
    for (uint32_t i = 0; i < process->audio_outputs_count; ++i) {
        const clap_audio_buffer_t& out = process->audio_outputs[i];
        if (out.data32 == nullptr || out.channel_count < 2) continue;
        left  = out.data32[0];
        right = out.data32[1];
        break;
    }

    // No usable output (or not activated): still consume the events so notes are not lost.
    if (left == nullptr || right == nullptr || !p->voiceManager) {
        for (uint32_t i = 0; i < numEvents; ++i) processEvent(p, in->get(in, i));
        return CLAP_PROCESS_CONTINUE;
    }

    applyPendingParams(p);

    // Sample-accurate render: apply every event at its own frame offset, rendering forward to the
    // next event (capped at one engine chunk) so hits land on the sample the host asked for instead
    // of being quantized to the top of the block. VoiceManager zeroes and sums the voices itself.
    uint32_t nextEvent = 0;
    for (uint32_t offset = 0; offset < frames; ) {
        while (nextEvent < numEvents) {
            const clap_event_header_t* header = in->get(in, nextEvent);
            const uint32_t eventTime = header->time < frames ? header->time : frames - 1;
            if (eventTime > offset) break;
            processEvent(p, header);
            ++nextEvent;
        }

        uint32_t end = frames;
        if (nextEvent < numEvents) {
            const clap_event_header_t* header = in->get(in, nextEvent);
            const uint32_t eventTime = header->time < frames ? header->time : frames - 1;
            if (eventTime > offset && eventTime < end) end = eventTime;
        }
        if (end - offset > MODULE_BUFF_SIZE) end = offset + MODULE_BUFF_SIZE;

        p->voiceManager->process(end - offset, left + offset, right + offset);
        offset = end;
    }

    for (; nextEvent < numEvents; ++nextEvent) processEvent(p, in->get(in, nextEvent));

    return CLAP_PROCESS_CONTINUE;
}

// ---------------------------------------------------------------------------------------------
// audio-ports extension — one stereo output
// ---------------------------------------------------------------------------------------------

uint32_t audio_ports_count(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }

bool audio_ports_get(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
    if (isInput || index != 0) return false;
    info->id = 0;
    std::strncpy(info->name, "Output", sizeof(info->name) - 1);
    info->channel_count = 2;
    info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type     = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t kAudioPorts = { audio_ports_count, audio_ports_get };

// ---------------------------------------------------------------------------------------------
// note-ports extension — one input port, MIDI 1.0 or MIDI 2.0
// ---------------------------------------------------------------------------------------------
// The CLAP note dialect is not advertised (DPF's wrapper did not either): a host using it expects
// CLAP_EVENT_NOTE_END back for every note, and a kit has no use for its per-note expressions.
uint32_t note_ports_count(const clap_plugin_t*, bool isInput) { return isInput ? 1 : 0; }

bool note_ports_get(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info) {
    if (!isInput || index != 0) return false;
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI2;
    info->preferred_dialect  = CLAP_NOTE_DIALECT_MIDI2;
    std::strncpy(info->name, "Notes", sizeof(info->name) - 1);
    return true;
}

const clap_plugin_note_ports_t kNotePorts = { note_ports_count, note_ports_get };

// ---------------------------------------------------------------------------------------------
// params extension
// ---------------------------------------------------------------------------------------------

void formatParamValue(uint32_t index, double value, char* out, uint32_t size) {
    const ParamSpec& spec = kParams[index];
    if (const char* const* choices = choicesFor(index)) {
        int count = 0;
        while (choices[count] != nullptr) ++count;
        const int choice = std::clamp(int(value + 0.5), 0, count - 1);
        std::snprintf(out, size, "%s", choices[choice]);
    } else if (spec.integer) {
        std::snprintf(out, size, "%d", int(value + (value < 0.0 ? -0.5 : 0.5)));
    } else if (spec.unit[0] != '\0') {
        std::snprintf(out, size, "%.4g %s", value, spec.unit);
    } else {
        std::snprintf(out, size, "%.4g", value);
    }
}

uint32_t params_count(const clap_plugin_t*) { return uint32_t(kParamCount); }

bool params_get_info(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
    if (index >= uint32_t(kParamCount)) return false;
    const ParamSpec& spec = kParams[index];
    std::memset(info, 0, sizeof(*info));
    info->id            = clap_id(paramIdForIndex(index));
    info->flags         = CLAP_PARAM_IS_AUTOMATABLE;
    if (spec.integer) info->flags |= CLAP_PARAM_IS_STEPPED;
    if (choicesFor(index) != nullptr) info->flags |= CLAP_PARAM_IS_ENUM;
    info->min_value     = spec.min;
    info->max_value     = spec.max;
    info->default_value = spec.def;
    std::strncpy(info->name, spec.name, sizeof(info->name) - 1);
    std::strncpy(info->module, "StruckKit", sizeof(info->module) - 1);
    return true;
}

bool params_get_value(const clap_plugin_t* plugin, clap_id id, double* out) {
    const int index = paramIndexForId(id);
    if (index < 0) return false;
    *out = self(plugin)->paramValues[index].load(std::memory_order_relaxed);
    return true;
}

bool params_value_to_text(const clap_plugin_t*, clap_id id, double value, char* out, uint32_t size) {
    const int index = paramIndexForId(id);
    if (index < 0) return false;
    formatParamValue(uint32_t(index), value, out, size);
    return true;
}

bool params_text_to_value(const clap_plugin_t*, clap_id id, const char* text, double* out) {
    const int index = paramIndexForId(id);
    if (index < 0) return false;

    // Accept the dropdown label as well as a number, so hosts that round-trip text work.
    if (const char* const* choices = choicesFor(uint32_t(index))) {
        for (int i = 0; choices[i] != nullptr; ++i) {
            if (std::strcmp(choices[i], text) == 0) { *out = double(i); return true; }
        }
    }
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text) return false;
    *out = parsed;
    return true;
}

void params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out) {
    Plugin* p = self(plugin);
    if (in != nullptr) {
        const uint32_t count = in->size(in);
        for (uint32_t i = 0; i < count; ++i) processEvent(p, in->get(in, i));
    }
    emitGuiEdits(p, out);
}

const clap_plugin_params_t kParamsExt = {
    params_count, params_get_info, params_get_value,
    params_value_to_text, params_text_to_value, params_flush,
};

// ---------------------------------------------------------------------------------------------
// state extension
// ---------------------------------------------------------------------------------------------
// A tiny line-based text format rather than JSON, so the DSP side has no parser dependency:
//     StruckKit 1
//     <symbol> <value>
//     ...
// Parameters are keyed by symbol, so adding/reordering parameters can never misinterpret an old
// state file; unknown symbols are ignored.

bool streamWrite(const clap_ostream_t* stream, const void* data, uint64_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (uint64_t done = 0; done < size; ) {
        const int64_t written = stream->write(stream, bytes + done, size - done);
        if (written <= 0) return false;
        done += uint64_t(written);
    }
    return true;
}

bool state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    Plugin* p = self(plugin);
    p->scratch.clear();
    p->scratch += "StruckKit 1\n";
    char line[128];
    for (uint32_t i = 0; i < uint32_t(kParamCount); ++i) {
        std::snprintf(line, sizeof(line), "%s %.9g\n", kParams[i].symbol,
                      p->paramValues[i].load(std::memory_order_relaxed));
        p->scratch += line;
    }
    return streamWrite(stream, p->scratch.data(), p->scratch.size());
}

// Host sessions saved while StruckKit was built with dpf_add_plugin carry DPF's auto-generated
// state blob — NUL-separated tokens, because DPF writes "\xff" separators and then replaces them:
//     __dpf_parameters_begin__ \0 symbol \0 value \0 ... __dpf_parameters_end__ \0 \xfe
// DPF emitted that even with DISTRHO_PLUGIN_WANT_STATE 0, so parse it and keep those sessions
// restoring correctly across the switch to the raw entry point. Returns true if anything matched.
bool stateLoadDpfParameters(Plugin* p, const std::string& text) {
    size_t pos = text.find("__dpf_parameters_begin__");
    if (pos == std::string::npos) return false;
    pos += std::strlen("__dpf_parameters_begin__");
    if (pos < text.size() && text[pos] == '\0') ++pos;

    bool any = false;
    while (pos < text.size()) {
        const size_t keyEnd = text.find('\0', pos);
        if (keyEnd == std::string::npos) break;
        const std::string key = text.substr(pos, keyEnd - pos);
        pos = keyEnd + 1;
        if (key.empty() || key == "__dpf_parameters_end__") break;

        const size_t valueEnd = text.find('\0', pos);
        if (valueEnd == std::string::npos) break;
        const std::string value = text.substr(pos, valueEnd - pos);
        pos = valueEnd + 1;

        const int index = paramIndexForSymbol(key.c_str());
        if (index >= 0) {
            storeParam(p, uint32_t(index), std::strtod(value.c_str(), nullptr));
            any = true;
        }
    }
    return any;
}

bool state_load(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    Plugin* p = self(plugin);

    std::string text;
    char buffer[512];
    for (;;) {
        const int64_t read = stream->read(stream, buffer, sizeof(buffer));
        if (read < 0) return false;
        if (read == 0) break;
        text.append(buffer, size_t(read));
    }

    // Empty data, or data that is not ours, is a failure — not a silent "loaded defaults".
    // Idempotent hosts and validators both rely on this distinction.
    const size_t firstBreak = text.find('\n');
    const std::string header = text.substr(0, firstBreak == std::string::npos ? text.size() : firstBreak);
    if (header.rfind("StruckKit", 0) != 0) {
        if (!stateLoadDpfParameters(p, text)) return false;
        notifyHostValuesChanged(p);
        requestMainThread(p);
        return true;
    }

    // Only store the values here. state_load runs on the main thread, and applying them would
    // rewire the voice graph under a running process(). If the plugin is active,
    // applyPendingParams() picks them up at the top of the next block; if not, activate() applies
    // everything.
    size_t pos = firstBreak == std::string::npos ? text.size() : firstBreak + 1;
    while (pos < text.size()) {
        size_t lineEnd = text.find('\n', pos);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        const std::string line = text.substr(pos, lineEnd - pos);
        pos = lineEnd + 1;

        const size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        const int index = paramIndexForSymbol(line.substr(0, space).c_str());
        if (index < 0) continue;                       // unknown symbol: ignore, stay compatible
        storeParam(p, uint32_t(index), std::strtod(line.c_str() + space + 1, nullptr));
    }

    notifyHostValuesChanged(p);
    requestMainThread(p);
    return true;
}

const clap_plugin_state_t kStateExt = { state_save, state_load };

// ---------------------------------------------------------------------------------------------
// gui + timer extensions (the actual UI lives in the GUI shell)
// ---------------------------------------------------------------------------------------------

void plug_on_timer(const clap_plugin_t* plugin, clap_id timerId) {
    sk_gui_on_timer(plugin, timerId);
}

const clap_plugin_timer_support_t kTimerExt = { plug_on_timer };

// ---------------------------------------------------------------------------------------------
// voice-info
// ---------------------------------------------------------------------------------------------
// Tells the host how many voices to expect. The engine allocates MAX_VOICES up front, so the
// Voices param only changes how many the VoiceManager hands out.
bool voice_info_get(const clap_plugin_t* plugin, clap_voice_info_t* info) {
    Plugin* p = self(plugin);
    const uint32_t voices = currentVoiceCount(p);
    p->reportedVoiceCount = voices;
    info->voice_count    = voices;
    info->voice_capacity = uint32_t(kVoiceCounts[kVoiceChoiceCount - 1]);
    info->flags          = 0;
    return true;
}

const clap_plugin_voice_info_t kVoiceInfoExt = { voice_info_get };

const void* plug_get_extension(const clap_plugin_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS)    == 0) return &kAudioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS)     == 0) return &kNotePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS)         == 0) return &kParamsExt;
    if (std::strcmp(id, CLAP_EXT_STATE)          == 0) return &kStateExt;
    if (std::strcmp(id, CLAP_EXT_GUI)            == 0) return sk_gui_extension();
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)  == 0) return &kTimerExt;
    if (std::strcmp(id, CLAP_EXT_VOICE_INFO)     == 0) return &kVoiceInfoExt;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// descriptor + factory + entry
// ---------------------------------------------------------------------------------------------

const char* kFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t kDescriptor = {
    .clap_version = CLAP_VERSION_INIT,
    .id           = "com.unlikelyware.struckkit",   // unchanged from the DPF build: sessions find it
    .name         = "StruckKit",
    .vendor       = "Unlikelyware",
    .url          = "https://unlikelyware.com/struckkit",
    .manual_url   = "",
    .support_url  = "",
    .version      = STRUCKKIT_VERSION_STRING,
    .description  = "A modal-synthesis drum kit built on the PolygraphModular voice engine.",
    .features     = kFeatures,
};

const clap_plugin_t* factory_create(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (std::strcmp(id, kDescriptor.id) != 0) return nullptr;

    auto* p = new (std::nothrow) Plugin();
    if (p == nullptr) return nullptr;
    p->host = host;
    for (uint32_t i = 0; i < uint32_t(kParamCount); ++i) {
        p->paramValues[i].store(kParams[i].def, std::memory_order_relaxed);
        p->paramDirtyForGui[i].store(false, std::memory_order_relaxed);
        p->guiEditing[i].store(false, std::memory_order_relaxed);
        p->appliedValues[i] = kParams[i].def;
    }
    p->clap = clap_plugin_t{
        .desc             = &kDescriptor,
        .plugin_data      = p,
        .init             = plug_init,
        .destroy          = plug_destroy,
        .activate         = plug_activate,
        .deactivate       = plug_deactivate,
        .start_processing = plug_start_processing,
        .stop_processing  = plug_stop_processing,
        .reset            = plug_reset,
        .process          = plug_process,
        .get_extension    = plug_get_extension,
        .on_main_thread   = plug_on_main_thread,
    };
    return &p->clap;
}

uint32_t factory_count(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* factory_get(const clap_plugin_factory_t*, uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_factory_t kFactory = { factory_count, factory_get, factory_create };

bool entry_init(const char*) { return true; }

void entry_deinit() {}

const void* entry_get_factory(const char* factoryId) {
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) return &kFactory;
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The C seam the GUI shell calls (see src/plugin_gui.h). These live in the same translation unit,
// so the anonymous-namespace Plugin above is directly visible.
// ---------------------------------------------------------------------------------------------
extern "C" {

const clap_host_t* sk_plugin_host(const clap_plugin_t* plugin) { return self(plugin)->host; }

void  sk_plugin_set_gui(const clap_plugin_t* plugin, void* gui) { self(plugin)->gui = gui; }
void* sk_plugin_get_gui(const clap_plugin_t* plugin)           { return self(plugin)->gui; }

uint32_t sk_param_count(void) { return uint32_t(kParamCount); }

double sk_param_value(const clap_plugin_t* plugin, uint32_t index) {
    if (index >= uint32_t(kParamCount)) return 0.0;
    return self(plugin)->paramValues[index].load(std::memory_order_relaxed);
}

void sk_param_begin_gesture(const clap_plugin_t* plugin, uint32_t index) {
    if (index >= uint32_t(kParamCount)) return;
    Plugin* p = self(plugin);
    p->guiEditing[index].store(true, std::memory_order_relaxed);
    p->guiEdits.push({ GuiEdit::Begin, index, 0.0 });
}

void sk_param_set_from_gui(const clap_plugin_t* plugin, uint32_t index, double value) {
    if (index >= uint32_t(kParamCount)) return;
    Plugin* p = self(plugin);
    const float clamped = clampParam(index, value);
    p->paramValues[index].store(clamped, std::memory_order_relaxed);
    p->guiEdits.push({ GuiEdit::Value, index, double(clamped) });
}

void sk_param_end_gesture(const clap_plugin_t* plugin, uint32_t index) {
    if (index >= uint32_t(kParamCount)) return;
    Plugin* p = self(plugin);
    p->guiEditing[index].store(false, std::memory_order_relaxed);
    p->guiEdits.push({ GuiEdit::End, index, 0.0 });
}

} // extern "C"

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init         = entry_init,
    .deinit       = entry_deinit,
    .get_factory  = entry_get_factory,
};
