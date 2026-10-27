// StruckKitUI.cpp — the drum-kit front end. A KitMap legend (which key plays which drum, for the
// selected key map) plus seven grouped panels of reusable Knob controls, one per drum family
// (Crash / Snare / Toms / Hats / Hand Drums / Percussion) and Master. Each panel's title names the drum, so knobs are
// captioned by just their control. Master also carries the key-map dropdown (StruckKit / GM) and
// the Voices dropdown (10 / 16 / 20 / 32).

// Before DPF's headers: CoreFoundation's MacTypes.h declares a global Point, ambiguous with DGL's
// once DPF's using-declarations are in scope.
#ifndef _WIN32
#include <pwd.h>                             // presetDir: the account's home folder
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>   // the idle timer when the host has none (startIdleTimer)
#endif

#include "DistrhoUI.hpp"

#include "src/DistrhoUIInternal.hpp" // DPF's UIExporter (DPF_ROOT_DIR/distrho is on the include path)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Dropdown.hpp"
#include "EmbeddedResources.hpp" // build-time codegen: Unlikelyware logo PNG + help page bytes
#include "GeneratedPresets.h"     // build-time codegen from Presets/*.json (see tools/gen_presets.py)
#include "GroupBox.hpp"
#include "HeaderButton.hpp"
#include "HelpButton.hpp"
#include "Knob.hpp"
#include "Logo.hpp"
#include "OpenExternal.hpp"
#include "PopupMenu.hpp"
#include "PresetKeys.hpp"         // param -> preset-JSON key map, shared with the dumper
#include "PresetDump.hpp"         // dev-only JSON patch dumper (no-op unless STRUCKKIT_PRESET_DUMP=1)
#include "PresetFile.hpp"         // Save / Load: the patch as preset JSON

#include "CPPTypes.hpp"            // DEFAULT_SAMPLE_RATE (the UIExporter's nominal rate below)
#include "DistrhoPluginInfo.h"
#include "StruckKitParams.h"
#include "plugin_gui.h"            // the C seam to src/plugin.cpp (the raw CLAP plugin)

START_NAMESPACE_DISTRHO

using namespace struckkit;  // params (kParams, ParamIndex)

using DGL_NAMESPACE::Color;
using DGL_NAMESPACE::Dropdown;
using DGL_NAMESPACE::GroupBox;
using DGL_NAMESPACE::HeaderButton;
using DGL_NAMESPACE::HelpButton;
using DGL_NAMESPACE::Knob;
using DGL_NAMESPACE::Logo;
using DGL_NAMESPACE::PopupMenu;

// Drum-family accent palette, reused for the panels, their knobs, and the KitMap keys.
static inline Color colMaster() { return Color(150, 166, 182); }
static inline Color colKick()   { return Color(214, 110, 96);  }
static inline Color colCrash()  { return Color(214, 168, 92);  }
static inline Color colSnare()  { return Color(222, 206, 120); }
static inline Color colToms()   { return Color(120, 190, 140); }
static inline Color colHats()   { return Color(168, 140, 214); }
static inline Color colHand()   { return Color(196, 136, 96);  }
static inline Color colPerc()   { return Color(110, 178, 200); }

// --------------------------------------------------------------------------------------------
// KitMap — which key plays which drum (DrumKitModule.cpp's drumForNote). For the StruckKit map, its
// two octaves side by side, since that map repeats every 24 notes; for General MIDI, the note
// numbers by drum. Keys and names are tinted by drum family to match the panels.
class KitMap : public NanoSubWidget
{
public:
    explicit KitMap(Widget* parent) : NanoSubWidget(parent)
    {
        fHasFont = loadSharedResources();
    }

    void setKeyMap(int map)
    {
        if (map == fKeyMap) return;
        fKeyMap = map;
        repaint();
    }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        const float s = std::max(0.5f, h / 170.0f); // text scale, tracks the cell height

        beginPath(); roundedRect(0, 0, w, h, 6); fillColor(Color(46, 24, 29)); fill();
        beginPath(); roundedRect(0.5f, 0.5f, w - 1, h - 1, 6);
        strokeColor(Color(84, 50, 58)); strokeWidth(1.0f); stroke();

        if (fKeyMap == 1) { drawGM(w, h, s); return; }

        const float padX = 12 * s, padTop = 22 * s, capH = 22 * s, mid = 14 * s;
        const float ow = (w - 2 * padX - mid) * 0.5f;
        const float ky = padTop, kh = h - padTop - capH - 6 * s;
        if (ow <= 20 || kh <= 20) return;

        // Kit octave. White keys: C D E F G A B; black keys sit after C, D, F, G, A.
        static const Key kKitWhite[7] = {
            {"Kick", 0}, {"Kick 2", 0}, {"Snare", 1}, {"Rimshot", 1},
            {"Floor Tom", 2}, {"Mid Tom", 2}, {"Hi Tom", 2},
        };
        static const Key kKitBlack[5] = {
            {"Crash 1", 3}, {"Crash 2", 3}, {"Open", 4}, {"Half", 4}, {"Closed", 4},
        };
        // Percussion octave.
        static const Key kPercWhite[7] = {
            {"Tumba", 5}, {"Conga", 5}, {"Mute", 5}, {"Hembra", 5},
            {"Macho", 5}, {"Timb Lo", 5}, {"Timb Hi", 5},
        };
        static const Key kPercBlack[5] = {
            {"Cowbell", 6}, {"Claves", 6}, {"Block", 6}, {"Tri Mute", 6}, {"Triangle", 6},
        };
        drawOctave(padX, ky, ow, kh, s, kKitWhite, kKitBlack, "Kit   36-47, 60-71, ...");
        drawOctave(padX + ow + mid, ky, ow, kh, s, kPercWhite, kPercBlack, "Percussion   48-59, 72-83, ...");

        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(11 * s);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(Color(150, 154, 162));
            text(w * 0.5f, h - capH * 0.5f - 2 * s,
                 "Repeats every two octaves. Hats choke each other; so do the triangles.", nullptr);
        }
    }

private:
    struct Key { const char* name; int family; };

    void drawOctave(float kx, float ky, float kw, float kh, float s,
                    const Key white[7], const Key black[5], const char* title)
    {
        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(11 * s);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(Color(200, 204, 212));
            text(kx + 2 * s, ky - 9 * s, title, nullptr);
        }
        // White keys. Labels sit at the bottom, below where black keys reach.
        const float ww = kw / 7.0f;
        for (int i = 0; i < 7; ++i)
        {
            const float x = kx + i * ww;
            Color c = familyColor(white[i].family); c.alpha = 0.30f;
            beginPath(); rect(x + 1, ky, ww - 2, kh); fillColor(Color(214, 218, 224)); fill();
            beginPath(); rect(x + 1, ky, ww - 2, kh); fillColor(c); fill();
            label(x + ww * 0.5f, ky + kh - 10 * s, white[i].name, Color(30, 32, 38), s);
        }
        // Black keys sit on the boundary after C, D, F, G, A (white-key index + 1).
        static const int kAfter[5] = { 1, 2, 4, 5, 6 };
        const float bw = ww * 0.66f, bh = kh * 0.58f;
        for (int i = 0; i < 5; ++i)
        {
            const float cx = kx + kAfter[i] * ww;
            Color c = familyColor(black[i].family); c.alpha = 0.85f;
            beginPath(); rect(cx - bw * 0.5f, ky, bw, bh); fillColor(Color(24, 26, 30)); fill();
            beginPath(); rect(cx - bw * 0.5f + 2, ky, bw - 4, bh - 2); fillColor(c); fill();
            label(cx, ky + bh - 10 * s, black[i].name, Color(20, 22, 26), s, 0.85f);
        }
    }

    // General MIDI: "name note" pairs by drum family, in two columns (kit | percussion). Keys GM
    // defines that StruckKit has no model for (clap, tambourine, maracas, ...) are silent and not
    // listed.
    void drawGM(float w, float h, float s)
    {
        if (!fHasFont) return;
        struct Item { const char* name; const char* keys; };
        struct Row  { int family; Item items[4]; };
        static const Row kKit[] = {
            { 0, { {"Kick", "36"}, {"Kick 2", "35"} } },
            { 1, { {"Snare", "38 40"}, {"Rimshot", "37"} } },
            { 2, { {"Floor", "41 43"}, {"Mid", "45 47"}, {"High", "48 50"} } },
            { 4, { {"Closed", "42 44"}, {"Open", "46"} } },
            { 3, { {"Crash 1", "49 52"}, {"Crash 2", "57 55"}, {"Ride", "51 59"}, {"Bell", "53"} } },
        };
        static const Row kPerc[] = {
            { 5, { {"Congas", "64 63"}, {"Mute", "62"} } },
            { 5, { {"Bongos", "61 60"}, {"Timbales", "66 65"} } },
            { 6, { {"Cowbell", "56"}, {"Agogo", "68 67"} } },
            { 6, { {"Claves", "75"}, {"Blocks", "77 76"} } },
            { 6, { {"Triangle", "81"}, {"Mute", "80"} } },
        };
        const float padX = 14 * s, top = 12 * s, capH = 22 * s, mid = 18 * s;
        const float halfW = (w - 2 * padX - mid) * 0.5f;
        drawGMColumn(kKit, 5, padX, top, halfW, h - top - capH, 4, s);
        drawGMColumn(kPerc, 5, padX + halfW + mid, top, halfW, h - top - capH, 4, s);
        fontSize(11 * s);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(Color(150, 154, 162));
        text(w * 0.5f, h - capH * 0.5f - 2 * s,
             "General MIDI note numbers. Hats choke each other; so do the triangles.", nullptr);
    }

    template <typename Row>
    void drawGMColumn(const Row* rows, int count, float x0, float y0, float w, float h, int cols, float s)
    {
        const float rowH = h / count;
        const float colW = w / cols;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        for (int r = 0; r < count; ++r)
        {
            const float y = y0 + (r + 0.5f) * rowH;
            for (int i = 0; i < 4 && rows[r].items[i].name; ++i)
            {
                const float x = x0 + i * colW;
                fontSize(11.5f * s);
                fillColor(familyColor(rows[r].family));
                text(x, y, rows[r].items[i].name, nullptr);
                DGL_NAMESPACE::Rectangle<float> b;
                textBounds(x, y, rows[r].items[i].name, nullptr, b);
                fontSize(10.5f * s);
                fillColor(Color(200, 204, 212));
                text(b.getX() + b.getWidth() + 5 * s, y, rows[r].items[i].keys, nullptr);
            }
        }
    }

    static Color familyColor(int f)
    {
        switch (f)
        {
        case 0:  return colKick();
        case 1:  return colSnare();
        case 2:  return colToms();
        case 3:  return colCrash();
        case 5:  return colHand();
        case 6:  return colPerc();
        default: return colHats();
        }
    }

    void label(float x, float y, const char* t, Color c, float s, float size = 1.0f)
    {
        if (!fHasFont) return;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(10.5f * s * size);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(c);
        text(x, y, t, nullptr);
    }

    bool fHasFont = false;
    int  fKeyMap  = 0;   // 0 = StruckKit, 1 = GM

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitMap)
};

// --------------------------------------------------------------------------------------------
class StruckKitUI : public UI,
                    public Knob::Callback,
                    public Dropdown::Callback,
                    public PopupMenu::Callback
{
public:
    StruckKitUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        // 0.8x minimum, any aspect ratio: layout() scales with the width and fits the rows to the
        // height. Only UI-initiated resizes see this; the host's go through gui_adjust_size below.
        setGeometryConstraints(kMinWidth, kMinHeight, false, false);
        fHasFont = loadSharedResources();               // DPF's bundled DejaVu Sans for the title

        // DPF is only the GUI layer here: UIExporter set uiData->dspPtr (our clap_plugin_t) before
        // calling createUI(), so the seam in plugin_gui.h is already usable. Reading the values
        // through it means an editor opened on a host-restored patch starts in sync instead of
        // showing the parameter defaults.
        fPlugin = static_cast<const clap_plugin_t*>(getPluginInstancePointer());
        for (uint32_t i = 0; i < kParamCount; ++i)
            fValues[i] = fPlugin != nullptr ? static_cast<float>(sk_param_value(fPlugin, i))
                                            : kParams[i].def;

        fKitMap = std::make_unique<KitMap>(this);
        fKitMap->setKeyMap(static_cast<int>(fValues[kParamKeyMap] + 0.5f));
        fLogo = std::make_unique<Logo>(this, EmbeddedResources::Unlikelyware_Final_ColorData,
                                       EmbeddedResources::Unlikelyware_Final_ColorDataSize);

        // Titles name the instruments, not keys: which key plays what depends on the map, and the
        // KitMap shows that.
        fCrashGroup  = makeGroup("Crashes  (and ride)",             colCrash());
        fSnareGroup  = makeGroup("Snare  (stroke, rimshot)",        colSnare());
        fTomsGroup   = makeGroup("Toms  (16\" floor, 12\", 10\")", colToms());
        fHatsGroup   = makeGroup("Hi-Hats  (open, half, closed)",   colHats());
        fHandGroup   = makeGroup("Hand Drums  (congas, bongos, timbales)", colHand());
        fPercGroup   = makeGroup("Percussion",                      colPerc());
        fMasterGroup = makeGroup("Master",                          colMaster());

        // Build each param control into its panel, in panel order.
        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            const ParamSpec& s = kParams[i];
            GroupBox* g = groupFor(i);
            if (i == kParamKeyMap)
            {
                auto d = std::make_unique<Dropdown>(g, this);
                d->setId(i);
                d->setLabel("Map");
                d->setChoices({ "StruckKit", "GM" });
                d->setIndex(static_cast<int>(fValues[i] + 0.5f));
                g->addControl(d.get());
                fMapDropdown = std::move(d);
                continue;
            }
            if (i == kParamVoices)
            {
                auto d = std::make_unique<Dropdown>(g, this);
                d->setId(i);
                d->setLabel("Voices");
                d->setChoices(std::vector<std::string>(kVoiceChoiceNames, kVoiceChoiceNames + kVoiceChoiceCount));
                d->setIndex(static_cast<int>(fValues[i] + 0.5f));
                g->addControl(d.get());
                fVoicesDropdown = std::move(d);
                continue;
            }
            auto k = std::make_unique<Knob>(g, this);
            k->setId(i);
            k->setLabel(shortLabel(i));
            k->setAccentColor(accentFor(i));
            k->configure(s.min, s.max, s.logarithmic, s.def);
            k->setRealValue(fValues[i], false);
            k->setFormatter(formatterFor(i));
            g->addControl(k.get());
            fKnobs[i] = std::move(k);
        }

        // Preset selector: applies to the whole patch, so it's parented to the UI (like fMenu)
        // and sits in the header strip, not in a GroupBox.
        fPresetDropdown = std::make_unique<Dropdown>(this, this);
        fPresetDropdown->setId(kPresetDropdownId);
        fPresetDropdown->setLabel("Preset");
        {
            std::vector<std::string> names;
            for (int i = 0; i < kPresetCount; ++i) names.push_back(kPresets[i].name);
            fPresetDropdown->setChoices(names);
        }
        fPresetDropdown->setPlaceholder("--");
        fPresetDropdown->setIndex(-1); // no preset selected until the user picks one

        fHelpButton = std::make_unique<HelpButton>(this, [] { openHelp(); });

        // Save / Load the patch as a preset file (PresetFile.hpp), through the system file dialog.
        fSaveButton = std::make_unique<HeaderButton>(this, "Save", [this] { browseForPreset(true); });
        fLoadButton = std::make_unique<HeaderButton>(this, "Load", [this] { browseForPreset(false); });

        fMenu = std::make_unique<PopupMenu>(this, this); // created last → draws on top

        layout();
    }

protected:
    /* -- host -> UI ------------------------------------------------------- */
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount) return;
        fValues[index] = value;
        if (fKnobs[index]) { fKnobs[index]->setRealValue(value, false); fKnobs[index]->repaint(); }
        if (index == kParamKeyMap)
        {
            const int map = static_cast<int>(value + 0.5f);
            if (fMapDropdown) fMapDropdown->setIndex(map);
            if (fKitMap)      fKitMap->setKeyMap(map);
        }
        if (index == kParamVoices && fVoicesDropdown)
            fVoicesDropdown->setIndex(static_cast<int>(value + 0.5f));
    }

    /* -- knob -> host ----------------------------------------------------- */
    void knobDragStarted(Knob* k) override  { editParameter(k->getId(), true); }
    void knobDragFinished(Knob* k) override { editParameter(k->getId(), false); }
    void knobValueChanged(Knob* k, float v) override
    {
        const uint32_t id = k->getId();
        fValues[id] = v;
        setParameterValue(id, v);
    }

    /* -- dropdown + menu (preset selector + key map) ------------------------ */
    void dropdownClicked(Dropdown* d) override
    {
        int x, y, w, h;
        d->boxRect(x, y, w, h);
        fMenu->openFor(d->getId(), d->choices(), d->index(), x, y + h, y, w,
                       static_cast<int>(getHeight()));
    }
    // A click outside the plugin window never reaches us, so treat losing focus to the host (or
    // another app) as "clicked elsewhere" and close any open menu. Clicks inside the window are
    // handled by the menu itself.
    void uiFocus(bool focus, DGL_NAMESPACE::CrossingMode mode) override
    {
        UI::uiFocus(focus, mode);
        if (!focus && fMenu != nullptr)
            fMenu->dismiss();
    }

    void menuSelected(PopupMenu* menu, int index) override
    {
        const uint32_t id = menu->tag();
        if (id == kPresetDropdownId) { applyPreset(index); return; }
        if (id != kParamKeyMap && id != kParamVoices) return;
        const float v = static_cast<float>(index);
        editParameter(id, true);
        setParameterValue(id, v);
        editParameter(id, false);
        parameterChanged(id, v);
    }

    // Apply every param the preset has a value for (looked up by "module.item" key via kPresetKeys —
    // the same mapping PresetFile.hpp uses to write these files). Params the preset doesn't mention
    // keep their current value; values are clamped to the param's range, since a loaded file may
    // come from anywhere. parameterChanged() already syncs the widget for each param, so it's
    // reused here instead of duplicating that dispatch.
    template <typename Find>
    void applyPresetValues(Find find)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            const std::string key = std::string(kPresetKeys[i].module) + "." + kPresetKeys[i].item;
            float v;
            if (!find(key, v)) continue;
            v = std::clamp(v, kParams[i].min, kParams[i].max);
            if (kParams[i].integer) v = std::round(v);
            editParameter(i, true);
            setParameterValue(i, v);
            editParameter(i, false);
            parameterChanged(i, v); // updates fValues[i] and the widget
        }
    }

    void applyPreset(int index)
    {
        if (index < 0 || index >= kPresetCount) return;
        const PresetEntry& preset = kPresets[index];
        applyPresetValues([&](const std::string& key, float& v) {
            for (int j = 0; j < preset.count; ++j)
                if (key == preset.values[j].key) { v = preset.values[j].value; return true; }
            return false;
        });
        if (fPresetDropdown) fPresetDropdown->setIndex(index);
        fPatchName = preset.name; // Save offers it as the file name
    }

    /* -- Save / Load ------------------------------------------------------ */
    // The dialog is asynchronous: openFileBrowser() returns at once and the chosen path arrives in
    // uiFileBrowserSelected(), so fBrowsing remembers which of the two asked. A second click opens
    // a new dialog in place of one still open (DPF closes the old one), so a result that never
    // arrives can't leave the buttons dead.
    enum class Browse { None, Save, Load };

    void browseForPreset(bool saving)
    {
        const std::string dir = presetDir();
        const std::string defaultName = (fPatchName.empty() ? std::string("Untitled") : fPatchName) + ".json";
        FileBrowserOptions opts;
        opts.saving      = saving;
        opts.title       = saving ? "Save StruckKit Patch" : "Load StruckKit Patch";
        opts.startDir    = dir.empty() ? nullptr : dir.c_str();
        opts.defaultName = saving ? defaultName.c_str() : nullptr;
        opts.extension   = saving ? nullptr : "json";
        fBrowsing = saving ? Browse::Save : Browse::Load;
        if (!openFileBrowser(opts))
            fBrowsing = Browse::None;
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        const Browse mode = fBrowsing;
        fBrowsing = Browse::None;
        if (filename == nullptr || filename[0] == '\0') return; // cancelled

        namespace fs = std::filesystem;
        fs::path path(filename);
        if (mode == Browse::Save)
        {
            if (!path.has_extension()) path += ".json";
            const std::string name = path.stem().string();
            if (!writePresetFile(path.string(), fValues, name))
            {
                std::fprintf(stderr, "[StruckKit] could not save %s\n", path.string().c_str());
                showStatus("Couldn't save");
                return;
            }
            showPatchName(name);
        }
        else if (mode == Browse::Load)
        {
            PresetFileData data;
            if (!readPresetFile(path.string(), data))
            {
                std::fprintf(stderr, "[StruckKit] %s is not a StruckKit preset\n",
                             path.string().c_str());
                showStatus("Couldn't load");
                return;
            }
            applyPresetValues([&](const std::string& key, float& v) {
                for (const auto& kv : data.values)
                    if (key == kv.first) { v = kv.second; return true; }
                return false;
            });
            // Named as the factory presets are (tools/gen_presets.py): meta.name, else the file stem.
            const bool hasName = !data.name.empty() && data.name != "Untitled";
            showPatchName(hasName ? data.name : path.stem().string());
        }
        else return;
        fLastDir = path.parent_path().string();
    }

    // A saved or loaded patch is not a factory preset: the menu shows its name, dimmed, with none
    // of the factory entries ticked.
    void showPatchName(const std::string& name)
    {
        fPatchName = name;
        if (fPresetDropdown) { fPresetDropdown->setPlaceholder(name); fPresetDropdown->setIndex(-1); }
    }

    // A failed Save / Load says so in the menu, where the patch name would go: stderr is lost in most
    // hosts. Unlike showPatchName(), it leaves the name Save offers alone.
    void showStatus(const std::string& text)
    {
        if (fPresetDropdown) { fPresetDropdown->setPlaceholder(text); fPresetDropdown->setIndex(-1); }
    }

    // The last folder used, else Documents/StruckKit (made on first use), else the home folder.
    // The home folder is the account's, not $HOME: in a sandboxed host process (Logic runs AUs in
    // one) $HOME is the sandbox's container. There, making the folder fails and the dialog opens on
    // the home folder; the dialog itself grants access to whatever is picked.
    std::string presetDir() const
    {
        if (!fLastDir.empty()) return fLastDir;
        namespace fs = std::filesystem;
        const char* home = nullptr;
#ifndef _WIN32
        if (const passwd* pw = getpwuid(getuid())) home = pw->pw_dir;
#endif
        if (home == nullptr) home = std::getenv("HOME");
        if (home == nullptr) home = std::getenv("USERPROFILE");
        if (home == nullptr) return "";
        std::error_code ec;
        const fs::path dir = fs::path(home) / "Documents" / "StruckKit";
        fs::create_directories(dir, ec);
        return ec ? std::string(home) : dir.string();
    }

    /* -- drawing / layout ------------------------------------------------- */
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        beginPath(); rect(0, 0, w, h); fillColor(Color(32, 16, 20)); fill();

        // Title, just to the right of the brand logo (fTitleX set in layout()).
        if (fHasFont)
        {
            const float s = w / 900.0f;
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fontSize(22 * s);
            fillColor(Color(224, 190, 150));
            text(fTitleX, 20 * s, "StruckKit", nullptr);
            fillColor(Color(150, 154, 162)); fontSize(12 * s);
            text(fTitleX + 112 * s, 22 * s, "modal drum kit", nullptr);
        }
    }
    void onResize(const ResizeEvent& ev) override { UI::onResize(ev); layout(); }

#if STRUCKKIT_PRESET_DUMP
    // Dev-only: press 'P' to write the current patch as JSON to $HOME (see PresetDump.hpp). fValues
    // already mirrors every param, so it's the patch snapshot.
    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (ev.press && ev.key == 'p')
        {
            const std::string path = dumpPresetToFile(fValues);
            std::fprintf(stderr, "[StruckKit] patch %s %s\n",
                         path.empty() ? "dump FAILED" : "dumped to", path.c_str());
            return true;
        }
        return UI::onKeyboard(ev);
    }
#endif

private:
    // The help page is embedded (Resources/help.html), so it can't be pointed at in place: write it,
    // plus the logo it references as "Unlikelyware.png", to a temp folder and open that in the
    // default browser. Rewritten on every click so it always matches this build.
    static void openHelp()
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "StruckKit-Help";
        if (ec) return;
        fs::create_directories(dir, ec);
        if (ec) return;

        auto write = [&](const char* name, const unsigned char* data, unsigned int size) {
            std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
            return static_cast<bool>(out);
        };
        write("Unlikelyware.png", EmbeddedResources::Unlikelyware_Final_ColorData,
              EmbeddedResources::Unlikelyware_Final_ColorDataSize);
        if (!write("help.html", EmbeddedResources::helpData, EmbeddedResources::helpDataSize))
            return;
        openExternal((dir / "help.html").string());
    }

    void layout()
    {
        const int W = getWidth(), H = getHeight();
        const float s = W / 1080.0f;
        const int M   = static_cast<int>(16 * s);
        const int hdr = static_cast<int>(52 * s); // header strip: brand + title left, preset right
        const int gap = static_cast<int>(10 * s);

        // Brand logo top-left; the title starts just to its right (used by onNanoDisplay).
        const int logoH = static_cast<int>(24 * s);
        fLogo->setHeightPreservingAspect(static_cast<float>(logoH));
        fLogo->setAbsolutePos(M, static_cast<int>(8 * s));
        fTitleX = M + static_cast<int>(fLogo->getWidth()) + static_cast<int>(12 * s);
        fMenu->setUiScale(s);

        // Preset selector, right-aligned in the header.
        const int presetW = static_cast<int>(200 * s);
        const int presetH = static_cast<int>(44 * s);
        fPresetDropdown->setUiScale(s);
        fPresetDropdown->setSize(static_cast<uint>(presetW), static_cast<uint>(presetH));
        fPresetDropdown->setAbsolutePos(W - M - presetW, static_cast<int>(4 * s));

        // Help "?" just left of the preset selector, centred on the header's title line.
        const int helpD = static_cast<int>(26 * s);
        fHelpButton->setUiScale(s);
        fHelpButton->setSize(static_cast<uint>(helpD), static_cast<uint>(helpD));
        fHelpButton->setAbsolutePos(W - M - presetW - gap - helpD, static_cast<int>(26 * s) - helpD / 2);

        // Save and Load, left of Help, their boxes level with the Preset menu's.
        const int fileW = static_cast<int>(52 * s);
        const int loadX = W - M - presetW - gap - helpD - gap - fileW;
        fLoadButton->setUiScale(s);
        fLoadButton->setSize(static_cast<uint>(fileW), static_cast<uint>(presetH));
        fLoadButton->setAbsolutePos(loadX, static_cast<int>(4 * s));
        fSaveButton->setUiScale(s);
        fSaveButton->setSize(static_cast<uint>(fileW), static_cast<uint>(presetH));
        fSaveButton->setAbsolutePos(loadX - fileW, static_cast<int>(4 * s));

        // Under the header: the KitMap across the full width (two keyboard octaves side by side),
        // then 2 columns × 3 rows of panels: Crash | Snare, Toms | Hats, Hand Drums | Percussion +
        // Master.
        const int gridY = hdr;
        const int gridH = H - gridY - M;
        const int mapH  = gridH * 24 / 100;
        const int rowH  = (gridH - mapH - 3 * gap) / 3;
        const int colW  = (W - 2 * M - gap) / 2;
        const int col0  = M, col1 = M + colW + gap;
        const int row0  = gridY + mapH + gap, row1 = row0 + rowH + gap, row2 = row1 + rowH + gap;

        fKitMap->setAbsolutePos(M, gridY);
        fKitMap->setSize(W - 2 * M, mapH);

        auto place = [&](GroupBox* g, int x, int y, int w) {
            g->setUiScale(s);
            g->setAbsolutePos(x, y);
            g->setSize(w, rowH);
        };
        const int percW = (colW - gap) / 2;   // Tune + Decay
        place(fCrashGroup.get(),  col0, row0, colW);
        place(fSnareGroup.get(),  col1, row0, colW);
        place(fTomsGroup.get(),   col0, row1, colW);
        place(fHatsGroup.get(),   col1, row1, colW);
        place(fHandGroup.get(),   col0, row2, colW);
        place(fPercGroup.get(),   col1, row2, percW);
        place(fMasterGroup.get(), col1 + percW + gap, row2, colW - percW - gap);

        // Knob diameter bounded by the row height (room for label + value) and, via relayout, the
        // cell width.
        const float knobD = std::min(74.0f * s, rowH * 0.44f);
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (fKnobs[i]) fKnobs[i]->setDiameter(knobD, s);
        if (fMapDropdown) fMapDropdown->setUiScale(s);
        if (fVoicesDropdown) fVoicesDropdown->setUiScale(s);
        GroupBox* panels[7] = { fCrashGroup.get(), fSnareGroup.get(), fTomsGroup.get(),
                                fHatsGroup.get(), fHandGroup.get(), fPercGroup.get(),
                                fMasterGroup.get() };
        for (GroupBox* g : panels) g->relayout();

        // Master: Level on the left half, Map over Voices on the right (three across would squeeze
        // the dropdowns in this narrow panel).
        if (fMapDropdown && fVoicesDropdown)
        {
            int x, y, w, h;
            fMasterGroup->contentBounds(x, y, w, h);
            const int half = w / 2;
            fKnobs[kParamLevel]->setSize(static_cast<uint>(half), static_cast<uint>(h));
            fKnobs[kParamLevel]->setAbsolutePos(x, y);
            fMapDropdown->setSize(static_cast<uint>(w - half), static_cast<uint>(h / 2));
            fMapDropdown->setAbsolutePos(x + half, y);
            fVoicesDropdown->setSize(static_cast<uint>(w - half), static_cast<uint>(h - h / 2));
            fVoicesDropdown->setAbsolutePos(x + half, y + h / 2);
        }
    }

    std::unique_ptr<GroupBox> makeGroup(const char* title, Color accent)
    {
        auto g = std::make_unique<GroupBox>(this);
        g->setTitle(title);
        g->setAccentColor(accent);
        return g;
    }

    // Params are laid out family by family in ParamIndex, so each family is a contiguous range.
    GroupBox* groupFor(uint32_t i) const
    {
        if (i == kParamLevel || i == kParamKeyMap || i == kParamVoices) return fMasterGroup.get();
        if (i <= kParamCrashStroke)  return fCrashGroup.get();
        if (i <= kParamSnareWires)  return fSnareGroup.get();
        if (i <= kParamTomReso)     return fTomsGroup.get();
        if (i <= kParamHatSizzle)   return fHatsGroup.get();
        if (i <= kParamHandRing)    return fHandGroup.get();
        return fPercGroup.get();
    }

    static Color accentFor(uint32_t i)
    {
        if (i == kParamLevel || i == kParamKeyMap || i == kParamVoices) return colMaster();
        if (i <= kParamCrashStroke)  return colCrash();
        if (i <= kParamSnareWires)  return colSnare();
        if (i <= kParamTomReso)     return colToms();
        if (i <= kParamHatSizzle)   return colHats();
        if (i <= kParamHandRing)    return colHand();
        return colPerc();
    }

    // The panel title names the drum, so each knob is captioned by just its control (the full
    // "Crash Tone" style names stay on the host-facing params).
    static const char* shortLabel(uint32_t i)
    {
        switch (i)
        {
        case kParamLevel:       return "Level";
        case kParamCrashSize:   return "Size 1";
        case kParamCrashSize2:  return "Size 2";
        case kParamCrashThin:   return "Thin";
        case kParamCrashDecay:  return "Decay";
        case kParamCrashStroke: return "Stroke";
        case kParamSnareTune:     return "Tune";
        case kParamSnareRing:     return "Ring";
        case kParamSnarePosition: return "Position";
        case kParamSnareTension:  return "Tension";
        case kParamSnareWires:    return "Wires";
        case kParamTomTune:     return "Tune";
        case kParamTomRing:     return "Ring";
        case kParamTomPosition: return "Position";
        case kParamTomReso:     return "Reso";
        case kParamHatSize:     return "Size";
        case kParamHatThin:     return "Thin";
        case kParamHatSizzle:   return "Sizzle";
        case kParamCongaTune:   return "Congas";
        case kParamBongoTune:   return "Bongos";
        case kParamTimbaleTune: return "Timbales";
        case kParamHandRing:    return "Ring";
        case kParamPercTune:    return "Tune";
        case kParamPercDecay:   return "Decay";
        default:                return kParams[i].name;
        }
    }

    static std::function<std::string(float)> formatterFor(uint32_t)
    {
        return [](float v) {
            char b[16];
            std::snprintf(b, sizeof(b), "%.2f", v);
            return std::string(b);
        };
    }

    std::unique_ptr<KitMap>        fKitMap;
    std::unique_ptr<GroupBox>      fCrashGroup, fSnareGroup, fTomsGroup, fHatsGroup, fHandGroup, fPercGroup,
                                   fMasterGroup;
    std::unique_ptr<Knob>          fKnobs[kParamCount];
    std::unique_ptr<PopupMenu>     fMenu;
    std::unique_ptr<Logo>          fLogo;
    std::unique_ptr<Dropdown>      fPresetDropdown;
    std::unique_ptr<HelpButton>    fHelpButton;
    std::unique_ptr<HeaderButton>  fSaveButton, fLoadButton;
    Browse                         fBrowsing = Browse::None;
    std::string                    fPatchName;          // the saved / loaded patch, "" for none yet
    std::string                    fLastDir;            // where the last Save / Load went
    std::unique_ptr<Dropdown>      fMapDropdown;    // key map: StruckKit / GM
    std::unique_ptr<Dropdown>      fVoicesDropdown; // polyphony: 10 / 16 / 20 / 32
    const clap_plugin_t*           fPlugin = nullptr; // the raw CLAP plugin, via the seam
    float                          fValues[kParamCount];
    bool                           fHasFont = false;
    int                            fTitleX = 16;

public:
    // Smallest editor: 0.8x the 1080 x 800 design size.
    static constexpr uint kMinWidth  = 864;
    static constexpr uint kMinHeight = 640;

private:
    // Sentinel id for the preset dropdown, past the real param range.
    static constexpr uint32_t kPresetDropdownId = kParamCount;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StruckKitUI)
};

UI* createUI()
{
    return new StruckKitUI();
}

// ================================================================================================
// CLAP `gui` extension
// ================================================================================================
// Modelled on StruckSynth's and PlainOldSynth's UI: gui_create() only allocates a holder, because
// UIExporter cannot be constructed until the host hands us a parent window in set_parent(). DPF is
// used purely as the GUI layer — the plugin itself is the raw CLAP plugin in src/plugin.cpp, so
// parameter traffic crosses the C seam in plugin_gui.h rather than DPF's plugin side.
//
// Sizing is the reason this exists (see plugin.cpp's header). The host owns the editor size: it is
// kept in host units, the host's requests are accepted as they are (only clamped to the minimum),
// and the plugin never asks the host for a size of its own.

namespace {

struct GuiState {
    const clap_plugin_t* plugin = nullptr;
    std::unique_ptr<UIExporter> exporter;
    uintptr_t parentWindow = 0;
    // The editor size in the HOST's units — logical points on macOS (CLAP cocoa), pixels on X11.
    // This, not the UIExporter's pixel size, is the source of truth: the pixel size is derived from
    // it with the live scale (applySize), so it can be re-derived when the window changes screens.
    uint32_t width  = DISTRHO_UI_DEFAULT_WIDTH;
    uint32_t height = DISTRHO_UI_DEFAULT_HEIGHT;
    double appliedScale = 0.0;   // the scale the UIExporter's pixel size was last derived with
    bool timerRegistered = false;
    clap_id timerId = 0;
#ifdef __APPLE__
    CFRunLoopTimerRef ownTimer = nullptr;   // when the host has no timer-support (startIdleTimer)
#endif
};

#ifdef __APPLE__
extern "C" double sk_gui_backing_scale(uintptr_t nsView);   // BackingScale_mac.mm
#endif

// Host units → UIExporter pixels. On macOS this is the backing scale of the window the editor is in
// RIGHT NOW, deliberately not UIExporter::getScaleFactor(): DPF caches that once, possibly from the
// wrong screen, and never updates it, while pugl maps mouse positions with the live value — see
// BackingScale_mac.mm. On X11, CLAP sizes are already physical pixels, so there is nothing to convert
// (matching DPF's own CLAP wrapper, which scales on macOS only).
double liveScale(const GuiState* gui)
{
#ifdef __APPLE__
    return sk_gui_backing_scale(gui->parentWindow);
#else
    (void)gui;
    return 1.0;
#endif
}

// Size the UIExporter to gui->width x gui->height host units at the live scale. pugl re-reads the
// screen's scale while doing this and re-sends the pixel size, so it also re-syncs DPF after a move
// between screens even when the size in points did not change.
void applySize(GuiState* gui)
{
    if (!gui->exporter) return;
    const double scale = liveScale(gui);
    gui->appliedScale = scale;
    gui->exporter->setWindowSizeFromHost(uint(std::lround(gui->width  * scale)),
                                         uint(std::lround(gui->height * scale)));
}

#ifdef __APPLE__
constexpr const char* kWindowApi = CLAP_WINDOW_API_COCOA;
#else
constexpr const char* kWindowApi = CLAP_WINDOW_API_X11;
#endif

// The UI's idle (sk_gui_on_timer) runs on a host timer. Without idle, DPF never collects the file
// dialog's result (Load and Save do nothing) and the UI's own uiIdle() never runs. clap-wrapper's
// AUv2 host has no timers (its register_timer returns false), so on macOS the UI then runs its own
// on the main run loop, in the common modes so it keeps firing while a menu or sheet is up.
void startIdleTimer(GuiState* gui)
{
    if (gui->timerRegistered) return;
    const clap_host_t* host = sk_plugin_host(gui->plugin);
    const auto* timer = static_cast<const clap_host_timer_support_t*>(
        host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
    if (timer != nullptr && timer->register_timer(host, 16 /* ~60fps */, &gui->timerId)) {
        gui->timerRegistered = true;
        return;
    }
#ifdef __APPLE__
    if (gui->ownTimer != nullptr) return;
    CFRunLoopTimerContext context = { 0, gui, nullptr, nullptr, nullptr };
    gui->ownTimer = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 0.016, 0.016,
                                         0, 0,
                                         [](CFRunLoopTimerRef, void* info) {
                                             sk_gui_on_timer(static_cast<GuiState*>(info)->plugin, 0);
                                         },
                                         &context);
    if (gui->ownTimer != nullptr)
        CFRunLoopAddTimer(CFRunLoopGetMain(), gui->ownTimer, kCFRunLoopCommonModes);
#endif
}

void stopIdleTimer(GuiState* gui)
{
    if (gui->timerRegistered) {
        const clap_host_t* host = sk_plugin_host(gui->plugin);
        const auto* timer = static_cast<const clap_host_timer_support_t*>(
            host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
        if (timer != nullptr) timer->unregister_timer(host, gui->timerId);
        gui->timerRegistered = false;
    }
#ifdef __APPLE__
    if (gui->ownTimer != nullptr) {
        CFRunLoopTimerInvalidate(gui->ownTimer);
        CFRelease(gui->ownTimer);
        gui->ownTimer = nullptr;
    }
#endif
}

bool gui_is_api_supported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && std::strcmp(api, kWindowApi) == 0;
}

bool gui_get_preferred_api(const clap_plugin_t*, const char** api, bool* isFloating)
{
    *api = kWindowApi;
    *isFloating = false;
    return true;
}

bool gui_create(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!gui_is_api_supported(plugin, api, isFloating)) return false;
    auto* gui = new GuiState();
    gui->plugin = plugin;
    sk_plugin_set_gui(plugin, gui);   // plugin.cpp owns the pointer and hands it back to us
    return true;
}

void gui_destroy(const clap_plugin_t* plugin)
{
    auto* gui = static_cast<GuiState*>(sk_plugin_get_gui(plugin));
    sk_plugin_set_gui(plugin, nullptr);
    if (gui == nullptr) return;

    stopIdleTimer(gui);
    delete gui;   // the UIExporter is destroyed here, which tears the UI down
}

// The UIExporter's edit/set callbacks. `ptr` is whatever was passed as UIExporter's first argument —
// we pass the clap_plugin_t, so these forward straight into the seam.
void onEditParam(void* ptr, uint32_t index, bool started)
{
    const auto* plugin = static_cast<const clap_plugin_t*>(ptr);
    if (started) sk_param_begin_gesture(plugin, index);
    else         sk_param_end_gesture(plugin, index);
}

void onSetParam(void* ptr, uint32_t index, float value)
{
    sk_param_set_from_gui(static_cast<const clap_plugin_t*>(ptr), index, double(value));
}

bool gui_set_scale(const clap_plugin_t*, double) { return false; }   // logical size API (cocoa): no-op

bool gui_get_size(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    auto* gui = static_cast<GuiState*>(sk_plugin_get_gui(plugin));
    if (gui == nullptr) return false;
    // Host units, straight from the source of truth. Converting the UIExporter's pixel size back
    // with DPF's cached scale is what made the host allocate a container of the wrong size.
    *width  = gui->width;
    *height = gui->height;
    return true;
}

bool gui_can_resize(const clap_plugin_t*) { return true; }

// No aspect-ratio lock, as PlainOldSynth: layout() handles any shape, and a host enforcing a ratio
// on a window that also carries its own chrome (REAPER) is what made resizing misbehave.
bool gui_get_resize_hints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically   = true;
    hints->preserve_aspect_ratio   = false;
    hints->aspect_ratio_width      = 0;
    hints->aspect_ratio_height     = 0;
    return true;
}

// Accept the host's size, held to the minimum; never reshaped otherwise.
bool gui_adjust_size(const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    *width  = std::max<uint32_t>(*width,  StruckKitUI::kMinWidth);
    *height = std::max<uint32_t>(*height, StruckKitUI::kMinHeight);
    return true;
}

bool gui_set_size(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* gui = static_cast<GuiState*>(sk_plugin_get_gui(plugin));
    if (gui == nullptr) return false;
    gui->width  = width;
    gui->height = height;
    applySize(gui);
    return true;
}

bool gui_set_parent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    auto* gui = static_cast<GuiState*>(sk_plugin_get_gui(plugin));
    if (gui == nullptr || std::strcmp(window->api, kWindowApi) != 0) return false;

    // cocoa is a void*, x11 is already an integer handle — hence the different casts.
#ifdef __APPLE__
    gui->parentWindow = reinterpret_cast<uintptr_t>(window->cocoa);
#else
    gui->parentWindow = static_cast<uintptr_t>(window->x11);
#endif

    if (!gui->exporter) {
        gui->exporter.reset(new UIExporter(
            const_cast<clap_plugin_t*>(plugin),   // callbacksPtr → the edit/set callbacks above
            gui->parentWindow,                    // winId: the host's embed target
            DEFAULT_SAMPLE_RATE,                  // the UI does not need the real rate
            onEditParam, onSetParam,
            nullptr, nullptr, nullptr, nullptr,   // state / note / size / file callbacks: unused
            nullptr,                              // bundlePath
            const_cast<clap_plugin_t*>(plugin),   // dspPtr → UI::getPluginInstancePointer()
            0.0,                                  // scaleFactor: 0 = auto
            // kTypeClassic, not kTypeAuto: matches DPF's own CLAP wrapper for this embedded case.
            DGL_NAMESPACE::Application::kTypeClassic));
        // The UIExporter starts at DISTRHO_UI_DEFAULT_WIDTH x HEIGHT *pixels*; put it at the host-unit
        // size instead, at the scale of the window it is actually in.
        applySize(gui);
    }
    // Here as well as in show(): clap-wrapper's AUv2 view never calls show() unless its local patch
    // (patches/clap-wrapper--auv2-gui-show.patch) is applied, and without idle Load / Save get no file.
    startIdleTimer(gui);
    return true;
}

bool gui_set_transient(const clap_plugin_t*, const clap_window_t*) { return false; }   // embedded only

void gui_suggest_title(const clap_plugin_t*, const char*) {}

bool gui_show(const clap_plugin_t* plugin)
{
    auto* gui = static_cast<GuiState*>(sk_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return false;

    startIdleTimer(gui);
    return true;
}

bool gui_hide(const clap_plugin_t* plugin)
{
    auto* gui = static_cast<GuiState*>(sk_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return false;

    stopIdleTimer(gui);
    return true;
}

const clap_plugin_gui_t kGui = {
    .is_api_supported  = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create            = gui_create,
    .destroy           = gui_destroy,
    .set_scale         = gui_set_scale,
    .get_size          = gui_get_size,
    .can_resize        = gui_can_resize,
    .get_resize_hints  = gui_get_resize_hints,
    .adjust_size       = gui_adjust_size,
    .set_size          = gui_set_size,
    .set_parent        = gui_set_parent,
    .set_transient     = gui_set_transient,
    .suggest_title     = gui_suggest_title,
    .show              = gui_show,
    .hide              = gui_hide,
};

} // namespace

extern "C" const clap_plugin_gui_t* sk_gui_extension(void) { return &kGui; }

// Host automation → an open editor. Called on the main thread by plugin.cpp.
extern "C" void sk_gui_push_param(void* guiPtr, uint32_t index, double value)
{
    auto* gui = static_cast<GuiState*>(guiPtr);
    if (gui != nullptr && gui->exporter)
        gui->exporter->parameterChanged(index, float(value));
}

// CLAP timer-support → pump the DPF/pugl event loop.
extern "C" void sk_gui_on_timer(const clap_plugin_t* plugin, clap_id)
{
    auto* gui = static_cast<GuiState*>(sk_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return;
    // The host window moved to a screen with a different backing scale (or the size was set before
    // it was in a window): re-derive the pixel size so drawing and mouse agree again.
    if (liveScale(gui) != gui->appliedScale) applySize(gui);
    gui->exporter->plugin_idle();
}

END_NAMESPACE_DISTRHO
