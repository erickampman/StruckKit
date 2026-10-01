// Slider.hpp — a reusable compact vertical fader for DPF NanoVG UIs (the depth control on the
// mod-input side). Labelled, with a live value readout; bipolar-aware (fills from the zero line
// when the range crosses 0). Bind to a param with Widget::setId(); reports via the Callback.

#ifndef STRUCKKIT_SLIDER_HPP
#define STRUCKKIT_SLIDER_HPP

#include "NanoVG.hpp"

#include <cstdio>
#include <functional>
#include <string>

START_NAMESPACE_DGL

class Slider : public NanoSubWidget
{
public:
    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void sliderDragStarted(Slider*)  {}
        virtual void sliderDragFinished(Slider*) {}
        virtual void sliderValueChanged(Slider* slider, float value) = 0;
    };

    explicit Slider(Widget* parent, Callback* callback)
        : NanoSubWidget(parent), fCallback(callback)
    {
        fHasFont = loadSharedResources();
    }

    void setLabel(const std::string& label) { fLabel = label; }
    void setRange(float min, float max)     { fMin = min; fMax = max; }
    // Scales font size AND the reserved label/readout margins together — scaling font alone
    // is what causes text to overflow into the track above/below it.
    void setUiScale(float s)                { fUiScale = s; }
    void setValue(float v, bool sendCb = false)
    {
        fValue = clampf(v, fMin, fMax);
        repaint();
        if (sendCb) fCallback->sliderValueChanged(this, fValue);
    }
    float value() const { return fValue; }
    void setFormatter(std::function<std::string(float)> fn) { fFormat = std::move(fn); }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        const float tx = w * 0.5f;
        const float top = 18.0f * fUiScale, bot = h - 16.0f * fUiScale; // reserve label (top) + readout (bottom)

        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            fontSize(13.0f * fUiScale);
            fillColor(Color(176, 182, 190));
            text(tx, 1.0f, fLabel.c_str(), nullptr);
        }

        // track
        beginPath(); roundedRect(tx - 3, top, 6, bot - top, 3.0f);
        fillColor(Color(34, 37, 43)); fill();

        // fill from the zero line (or bottom) to the handle
        const float y0 = valToY(clampf(0.0f, fMin, fMax), top, bot);
        const float yv = valToY(fValue, top, bot);
        beginPath(); rect(tx - 3, std::fmin(y0, yv), 6, std::fabs(yv - y0));
        fillColor(Color(63, 185, 207)); fill();

        // handle
        beginPath(); roundedRect(tx - 9, yv - 4, 18, 8, 3.0f);
        fillColor(Color(226, 232, 238)); fill();
        strokeColor(Color(18, 20, 24)); strokeWidth(1.0f); stroke();

        if (fHasFont)
        {
            const std::string s = fFormat ? fFormat(fValue) : defaultFormat(fValue);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            fontSize(12.0f * fUiScale);
            fillColor(Color(226, 230, 236));
            text(tx, bot + 1.0f, s.c_str(), nullptr);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;
        if (ev.press)
        {
            if (ev.pos.getX() < 0 || ev.pos.getX() >= getWidth()
                || ev.pos.getY() < 0 || ev.pos.getY() >= getHeight())
                return false;
            fDragging = true;
            fCallback->sliderDragStarted(this);
            applyY(ev.pos.getY());
            return true;
        }
        if (fDragging)
        {
            fDragging = false;
            fCallback->sliderDragFinished(this);
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (!fDragging)
            return false;
        applyY(ev.pos.getY());
        return true;
    }

private:
    Callback*   fCallback;
    std::string fLabel;
    std::function<std::string(float)> fFormat;
    float fMin = -1.0f, fMax = 1.0f, fValue = 0.0f;
    bool  fDragging = false;
    bool  fHasFont = false;
    float fUiScale = 1.0f;

    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    float valToY(float v, float top, float bot) const
    {
        const float frac = (fMax > fMin) ? (v - fMin) / (fMax - fMin) : 0.0f;
        return bot - frac * (bot - top);
    }
    void applyY(double y)
    {
        const float top = 18.0f * fUiScale, bot = getHeight() - 16.0f * fUiScale;
        const float frac = clampf(static_cast<float>((bot - y) / (bot - top)), 0.0f, 1.0f);
        fValue = fMin + frac * (fMax - fMin);
        fCallback->sliderValueChanged(this, fValue);
        repaint();
    }

    static std::string defaultFormat(float v)
    {
        char b[16]; std::snprintf(b, sizeof(b), "%+.2f", v); return b;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Slider)
};

END_NAMESPACE_DGL

#endif // STRUCKKIT_SLIDER_HPP
