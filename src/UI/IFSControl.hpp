// IFSControl.hpp — the IFS fractal-editor pad: a reusable DPF NanoSubWidget that edits the five
// IFSOsc shape params (split Bx, junction By, endpoints StartEndY, and the two region squashes)
// by direct manipulation, and draws the resultant fractal behind translucent squash rectangles.
//
// The background curve is the *exact* DSP waveform: it calls the same computeIFSWavetable() the
// oscillator renders from (IFSCore), so what you see is what you hear (smoothing + iterations
// included). Interactions mirror the design prototype: the middle dot is 2-D (X = split,
// Y = junction), the endpoints are linked, and dragging up/down in a region sets its squash
// (anchored at the center line, ±1). Reports changes to a Callback keyed by host param id.

#ifndef STRUCKKIT_IFSCONTROL_HPP
#define STRUCKKIT_IFSCONTROL_HPP

#include "NanoVG.hpp"

#include "IFSCore.hpp"

#include <cmath>
#include <cstdint>

START_NAMESPACE_DGL

class IFSControl : public NanoSubWidget
{
public:
    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void ifsDragStarted(uint32_t paramId) = 0;
        virtual void ifsDragFinished(uint32_t paramId) = 0;
        virtual void ifsValueChanged(uint32_t paramId, float value) = 0;
    };

    explicit IFSControl(Widget* parent, Callback* callback)
        : NanoSubWidget(parent),
          fCallback(callback)
    {
        recompute();
    }

    // Host param indices for the five editable shape controls (so callbacks name the right one).
    void setParamIds(uint32_t bx, uint32_t by, uint32_t sEY, uint32_t s1, uint32_t s2)
    {
        fIdBx = bx; fIdBy = by; fIdSEY = sEY; fIdS1 = s1; fIdS2 = s2;
    }

    // Host → control (parameterChanged). Each rebuilds the displayed waveform.
    void setBx(float v)         { fShape.bx = v;         recompute(); }
    void setBy(float v)         { fShape.by = v;         recompute(); }
    void setStartEndY(float v)  { fShape.startEndY = v;  recompute(); }
    void setSquash1(float v)    { fShape.ifs1Squash = v; recompute(); }
    void setSquash2(float v)    { fShape.ifs2Squash = v; recompute(); }
    void setSmoothing(float v)  { fShape.smoothing = v;  recompute(); }
    void setIterations(float paramValue) { fShape.iterations = static_cast<int>(paramValue) + fIterBase; recompute(); }
    // Iterations param index → actual count offset (IFSOsc: +3, IFSLFO: +1).
    void setIterationBase(int base) { fIterBase = base; }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth();
        const float h = getHeight();
        const float splitX = (fShape.bx + 1.0f) * 0.5f * w;
        const float midY   = h * 0.5f;

        // field background
        beginPath(); roundedRect(0, 0, w, h, 5.0f); fillColor(Color(16, 18, 22)); fill();

        // center line + start/end level + split (dashed)
        strokeLine(0, midY, w, midY, Color(255, 255, 255, 26), 1.0f, false);
        strokeLine(0, vToY(fShape.startEndY), w, vToY(fShape.startEndY), Color(255, 255, 255, 80), 1.0f, true);
        strokeLine(splitX, 0, splitX, h, Color(63, 185, 207, 140), 1.0f, true);

        // squash rectangles (translucent backdrop, anchored at center) — drawn BEHIND the curve
        squashRect(0, splitX, fShape.ifs1Squash, midY);
        squashRect(splitX, w, fShape.ifs2Squash, midY);

        // resultant fractal — drawn on top so it stays crisp over the squash rects
        beginPath();
        const int N = static_cast<int>(w);
        for (int i = 0; i <= N; ++i)
        {
            const float x = static_cast<float>(i);
            const int   ti = std::min(IFS_WAVETABLE_SIZE - 1, static_cast<int>(x / w * IFS_WAVETABLE_SIZE));
            const float y = vToY(fTable[ti]);
            i == 0 ? moveTo(x, y) : lineTo(x, y);
        }
        strokeColor(Color(95, 215, 122)); strokeWidth(2.0f); lineJoin(ROUND); stroke();

        // handles
        endpointHandle(0, vToY(fShape.startEndY));
        endpointHandle(w, vToY(fShape.startEndY));
        midHandle(splitX, vToY(fShape.by));
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        if (ev.press)
        {
            // DPF offers every sibling this event regardless of position — reject clicks
            // outside our bounds so they fall through to the other panel controls.
            if (ev.pos.getX() < 0 || ev.pos.getX() >= getWidth()
                || ev.pos.getY() < 0 || ev.pos.getY() >= getHeight())
                return false;
            fTarget = hitTest(ev.pos.getX(), ev.pos.getY());
            if (fTarget == kNone)
                return false;
            fNumActive = 0;
            if (fTarget == kMid) { fActive[fNumActive++] = fIdBx; fActive[fNumActive++] = fIdBy; }
            else                 { fActive[fNumActive++] = targetParamId(fTarget); }
            for (int i = 0; i < fNumActive; ++i) fCallback->ifsDragStarted(fActive[i]);
            apply(ev.pos.getX(), ev.pos.getY());
            return true;
        }

        if (fTarget != kNone)
        {
            for (int i = 0; i < fNumActive; ++i) fCallback->ifsDragFinished(fActive[i]);
            fTarget = kNone;
            fNumActive = 0;
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (fTarget == kNone)
            return false;
        apply(ev.pos.getX(), ev.pos.getY());
        return true;
    }

private:
    enum Target { kNone, kMid, kEnd, kS1, kS2 };

    Callback* fCallback;
    IFSShapeParams fShape;                 // defaults from IFSShapeParams{} (match the DSP)
    float fTable[IFS_WAVETABLE_SIZE];
    int fIterBase = 3;                     // IFSOsc iterations offset; IFSLFO sets 1
    Target fTarget = kNone;
    uint32_t fActive[2] = {0, 0};
    int fNumActive = 0;
    uint32_t fIdBx = 0, fIdBy = 0, fIdSEY = 0, fIdS1 = 0, fIdS2 = 0;

    void recompute() { computeIFSWavetable(fShape, fTable); repaint(); }

    // value[-1,1] ↔ y (v=1 at top)
    float vToY(float v) const { return (1.0f - (v + 1.0f) * 0.5f) * getHeight(); }
    float yToV(float y) const { return 1.0f - 2.0f * y / getHeight(); }
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    Target hitTest(double mx, double my) const
    {
        const float w = getWidth();
        const float splitX = (fShape.bx + 1.0f) * 0.5f * w;
        const double dx = mx - splitX, dy = my - vToY(fShape.by);
        if (dx * dx + dy * dy < 13 * 13) return kMid;
        const double sey = vToY(fShape.startEndY);
        if (mx < 22 && std::fabs(my - sey) < 20) return kEnd;
        if (mx > w - 22 && std::fabs(my - sey) < 20) return kEnd;
        return mx < splitX ? kS1 : kS2;
    }

    uint32_t targetParamId(Target t) const
    {
        switch (t) { case kEnd: return fIdSEY; case kS1: return fIdS1; case kS2: return fIdS2; default: return 0; }
    }

    void apply(double mx, double my)
    {
        switch (fTarget)
        {
        case kMid:
            fShape.bx = clampf(static_cast<float>(mx) / getWidth() * 2.0f - 1.0f, -0.98f, 0.98f);
            fShape.by = clampf(yToV(my), -1.0f, 1.0f);
            fCallback->ifsValueChanged(fIdBx, fShape.bx);
            fCallback->ifsValueChanged(fIdBy, fShape.by);
            break;
        case kEnd:
            fShape.startEndY = clampf(yToV(my), -1.0f, 1.0f);
            fCallback->ifsValueChanged(fIdSEY, fShape.startEndY);
            break;
        case kS1:
            fShape.ifs1Squash = clampf(yToV(my), -0.99f, 0.99f);
            fCallback->ifsValueChanged(fIdS1, fShape.ifs1Squash);
            break;
        case kS2:
            fShape.ifs2Squash = clampf(yToV(my), -0.99f, 0.99f);
            fCallback->ifsValueChanged(fIdS2, fShape.ifs2Squash);
            break;
        default: return;
        }
        recompute();
    }

    /* -- drawing helpers -------------------------------------------------- */
    void strokeLine(float x0, float y0, float x1, float y1, Color c, float wdt, bool dashed)
    {
        beginPath();
        moveTo(x0, y0); lineTo(x1, y1);
        strokeColor(c); strokeWidth(wdt);
        (void)dashed; // NanoVG has no dash; solid faint line reads fine here
        stroke();
    }
    void squashRect(float x0, float x1, float s, float yc)
    {
        const float y = vToY(s);
        const float top = std::fmin(yc, y), hgt = std::fabs(y - yc);
        beginPath(); rect(x0, top, x1 - x0, hgt);
        fillColor(s >= 0 ? Color(63, 185, 207, 52) : Color(90, 140, 255, 58)); fill();
        beginPath(); moveTo(x0, y); lineTo(x1, y);
        strokeColor(s >= 0 ? Color(63, 185, 207, 210) : Color(90, 140, 255, 210)); strokeWidth(1.0f); stroke();
    }
    void midHandle(float x, float y)
    {
        beginPath(); circle(x, y, 6); fillColor(Color(226, 232, 238)); fill();
        beginPath(); circle(x, y, 8); strokeColor(Color(63, 185, 207)); strokeWidth(2.0f); stroke();
    }
    void endpointHandle(float x, float y)
    {
        const float cx = clampf(x, 5.0f, getWidth() - 5.0f);
        beginPath(); circle(cx, y, 4.5f); fillColor(Color(226, 232, 238)); fill();
        strokeColor(Color(0, 0, 0, 110)); strokeWidth(1.0f); stroke();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IFSControl)
};

END_NAMESPACE_DGL

#endif // STRUCKKIT_IFSCONTROL_HPP
