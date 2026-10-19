// HelpButton.hpp — a round "?" button for the header strip. A click runs the caller's action
// (StruckKitUI opens the embedded help page in the default browser). Like the other custom
// widgets it bounds-checks onMouse (DPF offers the event to every sibling).

#ifndef STRUCKKIT_HELP_BUTTON_HPP
#define STRUCKKIT_HELP_BUTTON_HPP

#include "NanoVG.hpp"

#include <algorithm>
#include <functional>
#include <utility>

START_NAMESPACE_DGL

class HelpButton : public NanoSubWidget
{
public:
    HelpButton(Widget* parent, std::function<void()> onClick)
        : NanoSubWidget(parent), fOnClick(std::move(onClick))
    {
        fHasFont = loadSharedResources();
    }

    void setUiScale(float s) { fUiScale = s; }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        const float r = std::min(w, h) * 0.5f - 1.0f;

        beginPath(); circle(w * 0.5f, h * 0.5f, r);
        fillColor(fHover ? Color(52, 40, 44) : Color(34, 37, 43));
        fill();
        strokeColor(fHover ? Color(224, 190, 150) : Color(70, 76, 86)); strokeWidth(1.0f); stroke();

        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fontSize(r * 1.25f);
            fillColor(Color(224, 190, 150));
            text(w * 0.5f, h * 0.5f + 1.0f * fUiScale, "?", nullptr);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1 || !ev.press || !contains(ev.pos))
            return false;
        if (fOnClick) fOnClick();
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const bool hover = contains(ev.pos);
        if (hover != fHover) { fHover = hover; repaint(); }
        return false;
    }

private:
    bool contains(const Point<double>& p) const
    {
        return p.getX() >= 0 && p.getX() < getWidth() && p.getY() >= 0 && p.getY() < getHeight();
    }

    std::function<void()> fOnClick;
    bool  fHasFont = false;
    bool  fHover   = false;
    float fUiScale = 1.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpButton)
};

END_NAMESPACE_DGL

#endif // STRUCKKIT_HELP_BUTTON_HPP
