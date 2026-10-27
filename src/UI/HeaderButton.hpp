// HeaderButton.hpp — a captioned box with a word in it, styled like Dropdown and HelpButton so they
// sit together in the header. A click calls the owner's callback (Save and Load use it).

#ifndef STRUCKKIT_HEADERBUTTON_HPP
#define STRUCKKIT_HEADERBUTTON_HPP

#include "NanoVG.hpp"

#include <functional>
#include <string>

START_NAMESPACE_DGL

class HeaderButton : public NanoSubWidget
{
public:
    HeaderButton(Widget* parent, std::string text, std::function<void()> onClick)
        : NanoSubWidget(parent), fText(std::move(text)), fOnClick(std::move(onClick))
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
        const float by = boxTop(), bh = fBoxH, bx = 2.0f * fUiScale, bw = w - 2.0f * bx;

        if (fHasFont && !fLabel.empty())
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
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fontSize(13.0f * fUiScale);
            fillColor(Color(226, 230, 236));
            text(bx + bw * 0.5f, by + bh * 0.5f, fText.c_str(), nullptr);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        // DPF offers every sibling this event regardless of position — only claim our own area.
        if (ev.button != 1)
            return false;
        const bool inside = ev.pos.getX() >= 0 && ev.pos.getX() < getWidth()
                         && ev.pos.getY() >= 0 && ev.pos.getY() < getHeight();
        if (!ev.press)
        {
            if (!fPressed)
                return false;
            fPressed = false;
            repaint();
            // On release, not press: the dialog it opens would otherwise take the release and
            // leave the box drawn pressed.
            if (inside && fOnClick) fOnClick();
            return true;
        }
        if (!inside)
            return false;
        fPressed = true;
        repaint();
        return true;
    }

private:
    float boxTop() const
    {
        const float rem = getHeight() - (fLabelH + fBoxH);
        return (rem > 0.0f ? rem * 0.5f : 0.0f) + fLabelH;
    }

    std::string           fText;
    std::function<void()> fOnClick;
    std::string           fLabel;
    bool                  fHasFont = false;
    bool                  fPressed = false;
    float                 fUiScale = 1.0f;

    int fBoxH = 26, fLabelH = 16;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeaderButton)
};

END_NAMESPACE_DGL

#endif // STRUCKKIT_HEADERBUTTON_HPP
