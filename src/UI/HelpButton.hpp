// HelpButton.hpp — a "?" box with a caption above it, styled like Dropdown so the two sit together
// in the header (the same button as the other Unlikelyware plugins). A click runs the caller's
// action (StruckKitUI opens the embedded help page in the default browser). Like the other custom
// widgets it bounds-checks onMouse (DPF offers the event to every sibling).

#ifndef STRUCKKIT_HELP_BUTTON_HPP
#define STRUCKKIT_HELP_BUTTON_HPP

#include "NanoVG.hpp"

#include <functional>
#include <string>
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

    void setLabel(const std::string& label) { fLabel = label; }
    void setUiScale(float s)
    {
        fUiScale = s;
        fBoxH   = static_cast<int>(26 * s);
        fLabelH = static_cast<int>(16 * s);
    }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth();
        const float by = boxTop(), bh = fBoxH, bw = bh, bx = (w - bw) * 0.5f;

        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            fontSize(13.0f * fUiScale);
            fillColor(Color(176, 182, 190));
            text(w * 0.5f, by - fLabelH + 1.0f, fLabel.c_str(), nullptr);
        }

        beginPath(); roundedRect(bx, by, bw, bh, 4.0f);
        fillColor(fPressed ? Color(48, 52, 60) : Color(34, 37, 43)); fill();
        strokeColor(Color(70, 76, 86)); strokeWidth(1.0f); stroke();

        if (fHasFont)
        {
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fontSize(15.0f * fUiScale);
            fillColor(Color(226, 230, 236));
            text(bx + bw * 0.5f, by + bh * 0.5f, "?", nullptr);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;
        if (!ev.press)
        {
            if (!fPressed)
                return false;
            fPressed = false;
            repaint();
            return true;
        }
        if (!contains(ev.pos))
            return false;
        fPressed = true;
        repaint();
        if (fOnClick) fOnClick();
        return true;
    }

private:
    bool contains(const Point<double>& p) const
    {
        return p.getX() >= 0 && p.getX() < getWidth() && p.getY() >= 0 && p.getY() < getHeight();
    }

    float boxTop() const
    {
        const float rem = getHeight() - (fLabelH + fBoxH);
        return (rem > 0.0f ? rem * 0.5f : 0.0f) + fLabelH;
    }

    std::function<void()> fOnClick;
    std::string           fLabel;
    bool                  fHasFont = false;
    bool                  fPressed = false;
    float                 fUiScale = 1.0f;

    int fBoxH = 26, fLabelH = 16;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpButton)
};

END_NAMESPACE_DGL

#endif // STRUCKKIT_HELP_BUTTON_HPP
