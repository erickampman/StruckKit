// StruckKitUI.cpp — the drum-kit front end. A KitMap legend (which key plays which drum, for the
// selected key map) plus five grouped panels of reusable Knob controls, one per drum family
// (Crash / Snare / Toms / Hats) and Master. Each panel's title names the drum, so knobs are
// captioned by just their control. Master also carries the key-map dropdown (PMU / GM).

#include "DistrhoUI.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Dropdown.hpp"
#include "EmbeddedResources.hpp" // build-time codegen: Unlikelyware logo PNG bytes
#include "GeneratedPresets.h"     // build-time codegen from Presets/*.json (see tools/gen_presets.py)
#include "GroupBox.hpp"
#include "Knob.hpp"
#include "Logo.hpp"
#include "PopupMenu.hpp"
#include "PresetKeys.hpp"         // param -> preset-JSON key map, shared with the dumper
#include "PresetDump.hpp"         // dev-only JSON patch dumper (no-op unless STRUCKKIT_PRESET_DUMP=1)

#include "StruckKitParams.h"

START_NAMESPACE_DISTRHO

using namespace struckkit;  // params (kParams, ParamIndex)

using DGL_NAMESPACE::Color;
using DGL_NAMESPACE::Dropdown;
using DGL_NAMESPACE::GroupBox;
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

// --------------------------------------------------------------------------------------------
// KitMap — which key plays which drum (DrumKitModule.cpp's drumForNote). For the PMU map, one
// octave of keyboard, since that map repeats every octave; for General MIDI, the note numbers by
// drum. Keys and names are tinted by drum family to match the panels.
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

        beginPath(); roundedRect(0, 0, w, h, 6); fillColor(Color(28, 31, 37)); fill();
        beginPath(); roundedRect(0.5f, 0.5f, w - 1, h - 1, 6);
        strokeColor(Color(58, 62, 70)); strokeWidth(1.0f); stroke();

        if (fKeyMap == 1) { drawGM(w, h, s); return; }

        const float padX = 12 * s, padTop = 10 * s, capH = 22 * s;
        const float kx = padX, kw = w - 2 * padX;
        const float ky = padTop, kh = h - padTop - capH - 6 * s;
        if (kw <= 20 || kh <= 20) return;

        // White keys: C D E F G A B. Labels sit at the bottom, below where black keys reach.
        static const struct { const char* name; int family; } kWhite[7] = {
            {"Kick", 0}, {"Kick 2", 0}, {"Snare", 1}, {"Rimshot", 1},
            {"Floor Tom", 2}, {"Mid Tom", 2}, {"Hi Tom", 2},
        };
        const float ww = kw / 7.0f;
        for (int i = 0; i < 7; ++i)
        {
            const float x = kx + i * ww;
            Color c = familyColor(kWhite[i].family); c.alpha = 0.30f;
            beginPath(); rect(x + 1, ky, ww - 2, kh); fillColor(Color(214, 218, 224)); fill();
            beginPath(); rect(x + 1, ky, ww - 2, kh); fillColor(c); fill();
            label(x + ww * 0.5f, ky + kh - 10 * s, kWhite[i].name, Color(30, 32, 38), s);
        }

        // Black keys sit on the boundary after C, D, F, G, A (white-key index + 1).
        static const struct { int after; const char* name; int family; } kBlack[5] = {
            {1, "Crash 1", 3}, {2, "Crash 2", 3}, {4, "Open", 4}, {5, "Half", 4}, {6, "Closed", 4},
        };
        const float bw = ww * 0.62f, bh = kh * 0.58f;
        for (const auto& b : kBlack)
        {
            const float cx = kx + b.after * ww;
            Color c = familyColor(b.family); c.alpha = 0.85f;
            beginPath(); rect(cx - bw * 0.5f, ky, bw, bh); fillColor(Color(24, 26, 30)); fill();
            beginPath(); rect(cx - bw * 0.5f + 2, ky, bw - 4, bh - 2); fillColor(c); fill();
            label(cx, ky + bh - 10 * s, b.name, Color(20, 22, 26), s);
        }

        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(11 * s);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(Color(150, 154, 162));
            text(w * 0.5f, h - capH * 0.5f - 2 * s,
                 "Repeats every octave. Open / Half / Closed hats choke each other.", nullptr);
        }
    }

private:
    // General MIDI: one row per drum family, "name note" pairs. Keys GM defines that StruckKit has
    // no model for (clap, cowbell, Latin percussion, ...) are silent and not listed.
    void drawGM(float w, float h, float s)
    {
        if (!fHasFont) return;
        struct Item { const char* name; const char* keys; };
        struct Row  { int family; Item items[5]; };
        static const Row kRows[] = {
            { 0, { {"Kick", "36"}, {"Kick 2", "35"} } },
            { 1, { {"Snare", "38 40"}, {"Rimshot", "37"} } },
            { 2, { {"Floor", "41 43"}, {"Mid", "45 47"}, {"High", "48 50"} } },
            { 4, { {"Closed", "42 44"}, {"Open", "46"} } },
            { 3, { {"Crash 1", "49 52"}, {"Crash 2", "57 55"}, {"Ride", "51 59"}, {"Bell", "53"} } },
        };
        const int   rows = (int)(sizeof(kRows) / sizeof(kRows[0]));
        const float padX = 14 * s, top = 12 * s, capH = 22 * s;
        const float rowH = (h - top - capH) / rows;
        const float colW = (w - 2 * padX) / 4.0f;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        for (int r = 0; r < rows; ++r)
        {
            const float y = top + (r + 0.5f) * rowH;
            for (int i = 0; i < 5 && kRows[r].items[i].name; ++i)
            {
                const float x = padX + i * colW;
                fontSize(11.5f * s);
                fillColor(familyColor(kRows[r].family));
                text(x, y, kRows[r].items[i].name, nullptr);
                DGL_NAMESPACE::Rectangle<float> b;
                textBounds(x, y, kRows[r].items[i].name, nullptr, b);
                fontSize(10.5f * s);
                fillColor(Color(200, 204, 212));
                text(b.getX() + b.getWidth() + 5 * s, y, kRows[r].items[i].keys, nullptr);
            }
        }
        fontSize(11 * s);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(Color(150, 154, 162));
        text(w * 0.5f, h - capH * 0.5f - 2 * s,
             "General MIDI note numbers. Open and closed hats choke each other.", nullptr);
    }

    static Color familyColor(int f)
    {
        switch (f)
        {
        case 0:  return colKick();
        case 1:  return colSnare();
        case 2:  return colToms();
        case 3:  return colCrash();
        default: return colHats();
        }
    }

    void label(float x, float y, const char* t, Color c, float s)
    {
        if (!fHasFont) return;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(10.5f * s);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(c);
        text(x, y, t, nullptr);
    }

    bool fHasFont = false;
    int  fKeyMap  = 0;   // 0 = PMU, 1 = GM

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
        setGeometryConstraints(864, 512, true, false); // lock the 1080:640 design ratio (0.8x min)
        fHasFont = loadSharedResources();               // DPF's bundled DejaVu Sans for the title

        for (uint32_t i = 0; i < kParamCount; ++i)
            fValues[i] = kParams[i].def;

        fKitMap = std::make_unique<KitMap>(this);
        fLogo = std::make_unique<Logo>(this, EmbeddedResources::Unlikelyware_Final_ColorData,
                                       EmbeddedResources::Unlikelyware_Final_ColorDataSize);

        // Titles name the instruments, not keys: which key plays what depends on the map, and the
        // KitMap shows that.
        fCrashGroup  = makeGroup("Crashes  (and ride)",             colCrash());
        fSnareGroup  = makeGroup("Snare  (stroke, rimshot)",        colSnare());
        fTomsGroup   = makeGroup("Toms  (16\" floor, 12\", 10\")", colToms());
        fHatsGroup   = makeGroup("Hi-Hats  (open, half, closed)",   colHats());
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
                d->setChoices({ "PMU", "GM" });
                d->setIndex(static_cast<int>(s.def));
                g->addControl(d.get());
                fMapDropdown = std::move(d);
                continue;
            }
            auto k = std::make_unique<Knob>(g, this);
            k->setId(i);
            k->setLabel(shortLabel(i));
            k->setAccentColor(accentFor(i));
            k->configure(s.min, s.max, s.logarithmic, s.def);
            k->setRealValue(s.def, false);
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
    void menuSelected(PopupMenu* menu, int index) override
    {
        const uint32_t id = menu->tag();
        if (id == kPresetDropdownId) { applyPreset(index); return; }
        if (id != kParamKeyMap) return;
        const float v = static_cast<float>(index);
        editParameter(id, true);
        setParameterValue(id, v);
        editParameter(id, false);
        parameterChanged(id, v);
    }

    // Apply every param the selected preset has a value for (looked up by "module.item" key via
    // kPresetKeys — the same mapping PresetDump.hpp uses to write these files). Params the preset
    // doesn't mention keep their current value.
    void applyPreset(int index)
    {
        if (index < 0 || index >= kPresetCount) return;
        const PresetEntry& preset = kPresets[index];
        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            const std::string key = std::string(kPresetKeys[i].module) + "." + kPresetKeys[i].item;
            for (int j = 0; j < preset.count; ++j)
            {
                if (key == preset.values[j].key)
                {
                    const float v = preset.values[j].value;
                    editParameter(i, true);
                    setParameterValue(i, v);
                    editParameter(i, false);
                    parameterChanged(i, v); // updates fValues[i] and the widget
                    break;
                }
            }
        }
        if (fPresetDropdown) fPresetDropdown->setIndex(index);
    }

    /* -- drawing / layout ------------------------------------------------- */
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        beginPath(); rect(0, 0, w, h); fillColor(Color(22, 24, 29)); fill();

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

    // Some Linux hosts (notably REAPER via pugl) don't apply the UI's initial size until the
    // event loop starts pumping, so the window loads mis-sized (top gap, bottom row clipped)
    // until a manual resize. Re-assert the default size once on the first idle tick to force
    // the host to apply correct geometry, which drives onResize() -> layout().
    void uiIdle() override
    {
        if (fInitialSizeNudged) return;
        fInitialSizeNudged = true;
        setSize(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT);
    }

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

        // 2 columns × 3 rows under the header: KitMap | Crash, Snare | Toms, Hats | Master.
        // Master (Level + Map) takes part of the bottom row and Hats spreads across the rest.
        const int gridY = hdr;
        const int gridH = H - gridY - M;
        const int rowH  = (gridH - 2 * gap) / 3;
        const int colW  = (W - 2 * M - gap) / 2;
        const int col0  = M, col1 = M + colW + gap;
        const int row0  = gridY, row1 = gridY + rowH + gap, row2 = gridY + 2 * (rowH + gap);

        fKitMap->setAbsolutePos(col0, row0);
        fKitMap->setSize(colW, rowH);

        auto place = [&](GroupBox* g, int x, int y, int w) {
            g->setUiScale(s);
            g->setAbsolutePos(x, y);
            g->setSize(w, rowH);
        };
        const int masterW = colW * 3 / 4;   // Level knob + Map dropdown
        const int masterX = W - M - masterW;
        place(fCrashGroup.get(),  col1, row0, colW);
        place(fSnareGroup.get(),  col0, row1, colW);
        place(fTomsGroup.get(),   col1, row1, colW);
        place(fHatsGroup.get(),   col0, row2, masterX - gap - col0);
        place(fMasterGroup.get(), masterX, row2, masterW);

        // Knob diameter bounded by the row height (room for label + value) and, via relayout, the
        // cell width.
        const float knobD = std::min(74.0f * s, rowH * 0.44f);
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (fKnobs[i]) fKnobs[i]->setDiameter(knobD, s);
        if (fMapDropdown) fMapDropdown->setUiScale(s);
        GroupBox* panels[5] = { fCrashGroup.get(), fSnareGroup.get(), fTomsGroup.get(),
                                fHatsGroup.get(), fMasterGroup.get() };
        for (GroupBox* g : panels) g->relayout();
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
        if (i == kParamLevel || i == kParamKeyMap) return fMasterGroup.get();
        if (i <= kParamCrashStroke)  return fCrashGroup.get();
        if (i <= kParamSnareWires)  return fSnareGroup.get();
        if (i <= kParamTomReso)     return fTomsGroup.get();
        return fHatsGroup.get();
    }

    static Color accentFor(uint32_t i)
    {
        if (i == kParamLevel || i == kParamKeyMap) return colMaster();
        if (i <= kParamCrashStroke)  return colCrash();
        if (i <= kParamSnareWires)  return colSnare();
        if (i <= kParamTomReso)     return colToms();
        return colHats();
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
    std::unique_ptr<GroupBox>      fCrashGroup, fSnareGroup, fTomsGroup, fHatsGroup, fMasterGroup;
    std::unique_ptr<Knob>          fKnobs[kParamCount];
    std::unique_ptr<PopupMenu>     fMenu;
    std::unique_ptr<Logo>          fLogo;
    std::unique_ptr<Dropdown>      fPresetDropdown;
    std::unique_ptr<Dropdown>      fMapDropdown;    // key map: PMU / GM
    float                          fValues[kParamCount];
    bool                           fHasFont = false;
    bool                           fInitialSizeNudged = false;
    int                            fTitleX = 16;

    // Sentinel id for the preset dropdown, past the real param range.
    static constexpr uint32_t kPresetDropdownId = kParamCount;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StruckKitUI)
};

UI* createUI()
{
    return new StruckKitUI();
}

END_NAMESPACE_DISTRHO
