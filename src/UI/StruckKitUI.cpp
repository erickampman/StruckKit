// StruckKitUI.cpp — the drum-kit front end. A KitMap legend (which key plays which drum, for the
// selected key map) plus seven grouped panels of reusable Knob controls, one per drum family
// (Crash / Snare / Toms / Hats / Hand Drums / Percussion) and Master. Each panel's title names the drum, so knobs are
// captioned by just their control. Master also carries the key-map dropdown (StruckKit / GM) and
// the Voices dropdown (10 / 16 / 20 / 32).

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
        setGeometryConstraints(864, 640, true, false); // lock the 1080:800 design ratio (0.8x min)
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
                d->setIndex(static_cast<int>(s.def));
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
                d->setIndex(static_cast<int>(s.def));
                g->addControl(d.get());
                fVoicesDropdown = std::move(d);
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
    std::unique_ptr<Dropdown>      fMapDropdown;    // key map: StruckKit / GM
    std::unique_ptr<Dropdown>      fVoicesDropdown; // polyphony: 10 / 16 / 20 / 32
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
