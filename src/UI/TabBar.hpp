// TabBar.hpp — a compact horizontal tab selector for DPF NanoVG UIs. Sits in a panel's title
// strip and switches which of several overlaid panels is shown (see StruckKitUI's tab groups). The
// UI owns the show/hide of the actual content; this widget just reports the selected index via
// the Callback. Identify which group fired with setId().

#ifndef STRUCKKIT_TABBAR_HPP
#define STRUCKKIT_TABBAR_HPP

#include "NanoVG.hpp"

#include <cstdint>
#include <string>
#include <vector>

START_NAMESPACE_DGL

class TabBar : public NanoSubWidget
{
public:
    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void tabSelected(TabBar* bar, int index) = 0;
    };

    TabBar(Widget* parent, Callback* callback)
        : NanoSubWidget(parent), fCallback(callback) { fHasFont = loadSharedResources(); }

    void setId(uint32_t id)                       { fId = id; }
    uint32_t getId() const                        { return fId; }
    void setTabs(const std::vector<std::string>& t) { fTabs = t; }
    void setAccentColor(Color c)                  { fAccent = c; }
    void setActive(int i)                         { if (i != fActive) { fActive = i; repaint(); } }
    int  active() const                           { return fActive; }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        const int n = static_cast<int>(fTabs.size());
        if (n <= 0)
            return;
        const float tw = w / n;
        for (int i = 0; i < n; ++i)
        {
            const float x = i * tw;
            const bool act = (i == fActive);
            beginPath(); roundedRect(x + 1.5f, 1.5f, tw - 3.0f, h - 3.0f, 4.0f);
            if (act)
            {
                Color fill = fAccent; fill.alpha = 0.22f;
                fillColor(fill); this->fill();
                strokeColor(fAccent); strokeWidth(1.0f); stroke();
            }
            else
            {
                fillColor(Color(40, 44, 51)); this->fill();
                strokeColor(Color(62, 66, 74)); strokeWidth(1.0f); stroke();
            }
            if (fHasFont)
            {
                fontFace(NANOVG_DEJAVU_SANS_TTF);
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                fontSize(h * 0.5f);
                fillColor(act ? fAccent : Color(150, 156, 164));
                text(x + tw * 0.5f, h * 0.5f + 1.0f, fTabs[i].c_str(), nullptr);
            }
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1 || !ev.press)
            return false;
        if (ev.pos.getX() < 0 || ev.pos.getX() >= getWidth()
            || ev.pos.getY() < 0 || ev.pos.getY() >= getHeight())
            return false;
        const int n = static_cast<int>(fTabs.size());
        if (n <= 0)
            return false;
        int i = static_cast<int>(ev.pos.getX() / (getWidth() / static_cast<double>(n)));
        if (i < 0) i = 0; else if (i >= n) i = n - 1;
        if (i != fActive) { fActive = i; repaint(); }
        fCallback->tabSelected(this, i);
        return true;
    }

private:
    Callback*                fCallback;
    std::vector<std::string> fTabs;
    int                      fActive = 0;
    uint32_t                 fId = 0;
    Color                    fAccent { 120, 190, 205 };
    bool                     fHasFont = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TabBar)
};

END_NAMESPACE_DGL

#endif // STRUCKKIT_TABBAR_HPP
