// Logo.hpp — the Unlikelyware logo: drawn at a caller-chosen height with width derived from the
// source image's own aspect ratio (never stretched). A click opens unlikelyware.com in the
// system's default browser.

#ifndef STRUCKKIT_LOGO_HPP
#define STRUCKKIT_LOGO_HPP

#include "NanoVG.hpp"
#include "OpenExternal.hpp"

START_NAMESPACE_DGL

class Logo : public NanoSubWidget
{
public:
    Logo(Widget* parent, const uchar* pngData, uint pngDataSize)
        : NanoSubWidget(parent)
    {
        fImage = createImageFromMemory(pngData, pngDataSize, 0);
    }

    // Sizes the widget to `height`, preserving the source image's aspect ratio for the width.
    // Callers pass an already-scaled height (design pixels * host canvas scale), same convention
    // as every other widget in this UI — the image itself is high-resolution source art, not a
    // fixed-size bitmap, so it stays sharp at whatever physical pixel size Retina asks for.
    void setHeightPreservingAspect(float height)
    {
        if (!fImage.isValid())
            return;
        const Size<uint> sz = fImage.getSize();
        const float aspect = sz.getHeight() > 0
                            ? static_cast<float>(sz.getWidth()) / static_cast<float>(sz.getHeight())
                            : 1.0f;
        setSize(static_cast<uint>(height * aspect), static_cast<uint>(height));
    }

protected:
    void onNanoDisplay() override
    {
        if (!fImage.isValid())
            return;
        const float w = getWidth(), h = getHeight();
        const Paint paint = imagePattern(0, 0, w, h, 0.0f, fImage, 1.0f);
        beginPath();
        rect(0, 0, w, h);
        fillPaint(paint);
        fill();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1 || !ev.press)
            return false;
        if (ev.pos.getX() < 0 || ev.pos.getX() >= getWidth()
            || ev.pos.getY() < 0 || ev.pos.getY() >= getHeight())
            return false;
        openWebsite();
        return true;
    }

private:
    NanoImage fImage;

    static void openWebsite() { struckkit::openExternal("https://www.unlikelyware.com"); }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Logo)
};

END_NAMESPACE_DGL

#endif // STRUCKKIT_LOGO_HPP
