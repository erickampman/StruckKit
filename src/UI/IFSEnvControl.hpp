// IFSEnvControl.hpp — the IFS envelope editor pad: the 3-transform sibling of IFSControl. Two
// interior dots (Blue, then Green — kept ordered) and three region squashes, over a curve that is
// the exact DSP envelope shape (IFSCore::computeIFSEnvWavetable). Interior X is a straight [0,1]
// position (not the oscillator's [-1,1] Bx). Unlike the oscillator/LFO pads, an envelope always
// starts and ends at 0.0 — there is no start/end handle. Reuses IFSControl::Callback (paramId +
// value); the host param ids for the seven editable controls are set by the UI.

#ifndef STRUCKKIT_IFSENVCONTROL_HPP
#define STRUCKKIT_IFSENVCONTROL_HPP

#include "NanoVG.hpp"

#include "IFSControl.hpp" // for IFSControl::Callback (shared)
#include "IFSCore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

START_NAMESPACE_DGL

class IFSEnvControl : public NanoSubWidget
{
public:
    using Callback = IFSControl::Callback;

    explicit IFSEnvControl(Widget* parent, Callback* callback)
        : NanoSubWidget(parent), fCallback(callback)
    {
        fShape.startEndY = 0.0f; // envelopes always start and end at 0 (no handle to change it)
        recompute();
    }

    void setParamIds(uint32_t blueX, uint32_t blueY, uint32_t greenX, uint32_t greenY,
                     uint32_t s1, uint32_t s2, uint32_t s3)
    {
        fIdBlueX = blueX; fIdBlueY = blueY; fIdGreenX = greenX; fIdGreenY = greenY;
        fIdS1 = s1; fIdS2 = s2; fIdS3 = s3;
    }

    void setBlueX(float v)     { fShape.blueX = v;      recompute(); }
    void setBlueY(float v)     { fShape.blueY = v;      recompute(); }
    void setGreenX(float v)    { fShape.greenX = v;     recompute(); }
    void setGreenY(float v)    { fShape.greenY = v;     recompute(); }
    void setSquash1(float v)   { fShape.ifs1Squash = v; recompute(); }
    void setSquash2(float v)   { fShape.ifs2Squash = v; recompute(); }
    void setSquash3(float v)   { fShape.ifs3Squash = v; recompute(); }
    void setSmoothing(float v) { fShape.smoothing = v;  recompute(); }
    void setIterations(float paramValue) { fShape.iterations = static_cast<int>(paramValue) + 1; recompute(); }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        const float bxX = fShape.blueX  * w;
        const float gxX = fShape.greenX * w;
        const float midY = h * 0.5f;

        beginPath(); roundedRect(0, 0, w, h, 5.0f); fillColor(Color(16, 18, 22)); fill();

        // The envelope's fixed 0 baseline is the mid line (start/end are pinned to 0).
        strokeLine(0, midY, w, midY, Color(255, 255, 255, 80));
        strokeLine(bxX, 0, bxX, h, Color(80, 150, 235, 130));
        strokeLine(gxX, 0, gxX, h, Color(95, 200, 130, 130));

        squashRect(0,   bxX, fShape.ifs1Squash, midY);
        squashRect(bxX, gxX, fShape.ifs2Squash, midY);
        squashRect(gxX, w,   fShape.ifs3Squash, midY);

        // envelope curve
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

        dot(bxX, vToY(fShape.blueY),  Color(96, 156, 240));  // blue
        dot(gxX, vToY(fShape.greenY), Color(110, 205, 140)); // green
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
            fTarget = hitTest(ev.pos.getX(), ev.pos.getY());
            if (fTarget == kNone)
                return false;
            fNumActive = 0;
            switch (fTarget)
            {
            case kBlue:  fActive[fNumActive++] = fIdBlueX;  fActive[fNumActive++] = fIdBlueY;  break;
            case kGreen: fActive[fNumActive++] = fIdGreenX; fActive[fNumActive++] = fIdGreenY; break;
            case kS1:    fActive[fNumActive++] = fIdS1;  break;
            case kS2:    fActive[fNumActive++] = fIdS2;  break;
            case kS3:    fActive[fNumActive++] = fIdS3;  break;
            default: break;
            }
            for (int i = 0; i < fNumActive; ++i) fCallback->ifsDragStarted(fActive[i]);
            apply(ev.pos.getX(), ev.pos.getY());
            return true;
        }
        if (fTarget != kNone)
        {
            for (int i = 0; i < fNumActive; ++i) fCallback->ifsDragFinished(fActive[i]);
            fTarget = kNone; fNumActive = 0;
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
    enum Target { kNone, kBlue, kGreen, kS1, kS2, kS3 };

    Callback* fCallback;
    IFSEnvShapeParams fShape;
    float fTable[IFS_WAVETABLE_SIZE];
    Target fTarget = kNone;
    uint32_t fActive[2] = {0, 0};
    int fNumActive = 0;
    uint32_t fIdBlueX = 0, fIdBlueY = 0, fIdGreenX = 0, fIdGreenY = 0;
    uint32_t fIdS1 = 0, fIdS2 = 0, fIdS3 = 0;

    void recompute() { computeIFSEnvWavetable(fShape, fTable); repaint(); }

    float vToY(float v) const { return (1.0f - (v + 1.0f) * 0.5f) * getHeight(); }
    float yToV(float y) const { return 1.0f - 2.0f * y / getHeight(); }
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    Target hitTest(double mx, double my) const
    {
        const float w = getWidth();
        const float bxX = fShape.blueX * w, gxX = fShape.greenX * w;
        if (near(mx, my, bxX, vToY(fShape.blueY)))  return kBlue;
        if (near(mx, my, gxX, vToY(fShape.greenY))) return kGreen;
        if (mx < bxX) return kS1;
        if (mx < gxX) return kS2;
        return kS3;
    }
    static bool near(double mx, double my, double x, double y)
    {
        const double dx = mx - x, dy = my - y;
        return dx * dx + dy * dy < 13 * 13;
    }

    void apply(double mx, double my)
    {
        const float w = getWidth();
        switch (fTarget)
        {
        case kBlue:
            fShape.blueX = clampf(static_cast<float>(mx) / w, 0.01f, fShape.greenX - 0.02f);
            fShape.blueY = clampf(yToV(my), -1.0f, 1.0f);
            fCallback->ifsValueChanged(fIdBlueX, fShape.blueX);
            fCallback->ifsValueChanged(fIdBlueY, fShape.blueY);
            break;
        case kGreen:
            fShape.greenX = clampf(static_cast<float>(mx) / w, fShape.blueX + 0.02f, 0.99f);
            fShape.greenY = clampf(yToV(my), -1.0f, 1.0f);
            fCallback->ifsValueChanged(fIdGreenX, fShape.greenX);
            fCallback->ifsValueChanged(fIdGreenY, fShape.greenY);
            break;
        case kS1: fShape.ifs1Squash = clampf(yToV(my), -0.99f, 0.99f); fCallback->ifsValueChanged(fIdS1, fShape.ifs1Squash); break;
        case kS2: fShape.ifs2Squash = clampf(yToV(my), -0.99f, 0.99f); fCallback->ifsValueChanged(fIdS2, fShape.ifs2Squash); break;
        case kS3: fShape.ifs3Squash = clampf(yToV(my), -0.99f, 0.99f); fCallback->ifsValueChanged(fIdS3, fShape.ifs3Squash); break;
        default: return;
        }
        recompute();
    }

    void strokeLine(float x0, float y0, float x1, float y1, Color c)
    {
        beginPath(); moveTo(x0, y0); lineTo(x1, y1); strokeColor(c); strokeWidth(1.0f); stroke();
    }
    void squashRect(float x0, float x1, float s, float yc)
    {
        const float y = vToY(s);
        const float top = std::fmin(yc, y), hgt = std::fabs(y - yc);
        beginPath(); rect(x0, top, x1 - x0, hgt);
        fillColor(s >= 0 ? Color(63, 185, 207, 48) : Color(90, 140, 255, 54)); fill();
        beginPath(); moveTo(x0, y); lineTo(x1, y);
        strokeColor(s >= 0 ? Color(63, 185, 207, 190) : Color(90, 140, 255, 190)); strokeWidth(1.0f); stroke();
    }
    void dot(float x, float y, Color ring)
    {
        beginPath(); circle(x, y, 6); fillColor(Color(226, 232, 238)); fill();
        beginPath(); circle(x, y, 8); strokeColor(ring); strokeWidth(2.0f); stroke();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IFSEnvControl)
};

END_NAMESPACE_DGL

#endif // STRUCKKIT_IFSENVCONTROL_HPP
