#include "Backdrop.h"
#include <cmath>

namespace ambient::ui
{
using namespace theme;

namespace
{
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

/** Superellipse ring (architectural "rounded square") with layered sinusoidal warp. */
juce::Path warpedRing(juce::Point<float> c, float rx, float ry, float depth, int points)
{
    juce::Path p;
    for (int i = 0; i <= points; ++i)
    {
        const float u = float(i) / float(points);
        const float a = u * kTwoPi;
        const float ca = std::cos(a), sa = std::sin(a);
        // superellipse exponent 2/n with n = 4.2 -> squarish, soft corners
        const float ex = 2.0f / 4.2f;
        const float sx = std::copysign(std::pow(std::abs(ca), ex), ca);
        const float sy = std::copysign(std::pow(std::abs(sa), ex), sa);
        const float warp = 1.0f + 0.030f * std::sin(6.0f * a + depth * 9.0f)
                                + 0.018f * std::sin(14.0f * a - depth * 5.3f)
                                + 0.010f * std::sin(23.0f * a + depth * 2.1f);
        const juce::Point<float> pt { c.x + sx * rx * warp, c.y + sy * ry * warp };
        if (i == 0) p.startNewSubPath(pt); else p.lineTo(pt);
    }
    p.closeSubPath();
    return p;
}
} // namespace

// ============================================================================
void Backdrop::renderWindow(juce::Graphics& g, float w, float h, GraphicsQuality q)
{
    const bool hq = q == GraphicsQuality::High;
    g.fillAll(bg);

    const juce::Point<float> vp { w * 0.40f, h * 0.50f }; // vanishing point

    // Atmospheric haze
    {
        juce::ColourGradient haze(uv.withAlpha(0.10f), vp.x, vp.y, uv.withAlpha(0.0f), vp.x + w * 0.55f, vp.y, true);
        g.setGradientFill(haze);
        g.fillRect(0.0f, 0.0f, w, h);
        juce::ColourGradient mag(magenta.withAlpha(0.05f), w * 0.92f, h * 0.95f, magenta.withAlpha(0.0f), w * 0.55f, h * 0.55f, true);
        g.setGradientFill(mag);
        g.fillRect(0.0f, 0.0f, w, h);
    }

    // Radial ribs (tunnel structure)
    const int ribs = 32;
    for (int i = 0; i < ribs; ++i)
    {
        const float a = kTwoPi * float(i) / float(ribs) + 0.07f;
        const juce::Point<float> d { std::cos(a), std::sin(a) };
        const float r0 = h * 0.06f, r1 = std::hypot(w, h);
        juce::ColourGradient rib(uv.withAlpha(0.0f), vp.x + d.x * r0, vp.y + d.y * r0,
                                 uv.withAlpha(0.05f), vp.x + d.x * r1 * 0.5f, vp.y + d.y * r1 * 0.5f, false);
        g.setGradientFill(rib);
        g.drawLine({ vp + d * r0, vp + d * r1 }, 1.0f);
    }

    // Warped contour rings receding into the tunnel
    const int rings = hq ? 34 : 22;
    const int pts = hq ? 240 : 120;
    for (int i = 0; i < rings; ++i)
    {
        const float t = float(i) / float(rings - 1);  // 0 far -> 1 near
        const float s = std::pow(t, 2.1f);
        const float rx = juce::jmap(s, w * 0.035f, w * 1.05f);
        const float ry = juce::jmap(s, h * 0.035f, h * 1.10f);
        auto ring = warpedRing(vp, rx, ry, t, pts);
        const auto col = uv.interpolatedWith(magenta, juce::jlimit(0.0f, 1.0f, t * 1.2f - 0.1f));
        g.setColour(col.withAlpha(0.025f + 0.065f * t));
        g.strokePath(ring, juce::PathStrokeType(i % 5 == 0 ? 1.4f : 0.8f));
        if (i == rings / 2 || i == rings * 3 / 4)
        {
            g.setColour(lime.withAlpha(0.045f));
            g.strokePath(ring, juce::PathStrokeType(0.8f));
        }
    }

    // Digital grain + dot lattice
    juce::Random rng(0xA3B1E);
    const int grains = int(w * h / (hq ? 260.0f : 520.0f));
    for (int i = 0; i < grains; ++i)
    {
        const float x = rng.nextFloat() * w, y = rng.nextFloat() * h;
        g.setColour((rng.nextFloat() < 0.15f ? uvBright : text).withAlpha(0.012f + 0.02f * rng.nextFloat()));
        g.fillRect(x, y, 1.0f, 1.0f);
    }

    // Vignette
    juce::ColourGradient vig(juce::Colours::transparentBlack, w * 0.5f, h * 0.5f,
                             juce::Colours::black.withAlpha(0.55f), 0.0f, 0.0f, true);
    g.setGradientFill(vig);
    g.fillRect(0.0f, 0.0f, w, h);
}

// ============================================================================
void Backdrop::renderHeader(juce::Graphics& g, float w, float h, juce::Point<float> focus, GraphicsQuality q)
{
    // FROZEN: the approved header artwork keeps its original colours, independent of the panel theme.
    static const juce::Colour hdrBg { 0xff0a0a0d }, hdrBgRaised { 0xff111116 }, hdrEdge { 0xff292932 }, hdrMetal { 0xff8d8c99 };
    const bool hq = q == GraphicsQuality::High;
    juce::ColourGradient base(hdrBgRaised, 0.0f, 0.0f, hdrBg, 0.0f, h, false);
    g.setGradientFill(base);
    g.fillRect(0.0f, 0.0f, w, h);

    // Interference contour lines. Density mask: present behind the brand and the
    // right-hand master section, quieter through the middle where the text lives.
    auto mask = [w](float x)
    {
        const float u = x / w;
        const float left  = std::exp(-std::pow((u - 0.10f) / 0.16f, 2.0f));
        const float right = std::exp(-std::pow((u - 0.97f) / 0.20f, 2.0f));
        return 0.32f + 0.68f * juce::jmax(left, right);
    };

    const int lines = hq ? 26 : 16;
    const float step = hq ? 2.0f : 4.0f;
    for (int i = 0; i < lines; ++i)
    {
        const float t = float(i) / float(lines - 1);
        const float base_y = h * (0.10f + 0.85f * t);
        const bool signal = (i == lines / 3) || (i == (lines * 2) / 3);
        juce::Path p;
        for (float x = 0.0f; x <= w + step; x += step)
        {
            const float u = x / w;
            const float y = base_y
                          + h * 0.16f * std::sin(u * 9.0f + t * 4.0f)
                          + h * 0.07f * std::sin(u * 23.0f - t * 7.0f)
                          + h * 0.04f * std::sin(u * 51.0f + t * 13.0f) * std::sin(u * 3.0f);
            if (x == 0.0f) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        // Stroke in vertical slices so the alpha mask can vary along x.
        const int slices = 24;
        for (int sIdx = 0; sIdx < slices; ++sIdx)
        {
            const float x0 = w * float(sIdx) / slices, x1 = w * float(sIdx + 1) / slices;
            juce::Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(juce::Rectangle<float>(x0, 0.0f, x1 - x0 + 1.0f, h).toNearestInt());
            const float m = mask(0.5f * (x0 + x1));
            const auto col = signal ? lime : uv.interpolatedWith(magenta, t * 0.6f);
            g.setColour(col.withAlpha((signal ? 0.34f : 0.10f + 0.13f * (1.0f - t)) * m));
            g.strokePath(p, juce::PathStrokeType(signal ? 1.0f : 0.8f));
        }
    }

    // Circuit traces on the right: orthogonal runs with 45-degree bends and nodes.
    juce::Random rng(0xC1C1);
    for (int i = 0; i < (hq ? 9 : 6); ++i)
    {
        float x = w - rng.nextFloat() * w * 0.05f;
        float y = h * (0.15f + 0.7f * rng.nextFloat());
        juce::Path tr;
        tr.startNewSubPath(x, y);
        const float run1 = w * (0.03f + 0.06f * rng.nextFloat());
        x -= run1; tr.lineTo(x, y);
        const float dy = (rng.nextBool() ? 1.0f : -1.0f) * h * (0.08f + 0.12f * rng.nextFloat());
        x -= std::abs(dy); y += dy; tr.lineTo(x, y);
        x -= w * (0.02f + 0.05f * rng.nextFloat()); tr.lineTo(x, y);
        g.setColour(hdrMetal.withAlpha(0.10f));
        g.strokePath(tr, juce::PathStrokeType(1.0f));
        g.setColour((i % 3 == 0 ? lime : uvBright).withAlpha(0.35f));
        g.fillEllipse(x - 1.6f, y - 1.6f, 3.2f, 3.2f);
    }

    // Brand glow plate behind the logo
    if (! focus.isOrigin())
    {
        juce::ColourGradient glow(uv.withAlpha(0.22f), focus.x, focus.y, uv.withAlpha(0.0f), focus.x + h * 1.2f, focus.y, true);
        g.setGradientFill(glow);
        g.fillRect(0.0f, 0.0f, w, h);
    }

    // Luminous lower hdrEdge
    juce::ColourGradient edgeLine(uv.withAlpha(0.0f), 0.0f, 0.0f, uv.withAlpha(0.0f), w, 0.0f, false);
    edgeLine.addColour(0.12, uv.withAlpha(0.65f));
    edgeLine.addColour(0.55, magenta.withAlpha(0.35f));
    edgeLine.addColour(0.92, lime.withAlpha(0.35f));
    g.setGradientFill(edgeLine);
    g.fillRect(0.0f, h - 1.0f, w, 1.0f);
    edgeLine.multiplyOpacity(0.25f);
    g.setGradientFill(edgeLine);
    g.fillRect(0.0f, h - 4.0f, w, 3.0f);
}

// ============================================================================
void Backdrop::draw(juce::Graphics& g, Region region, juce::Rectangle<int> area, GraphicsQuality q,
                    juce::Point<float> focus)
{
    if (area.isEmpty()) return;
    auto& c = region == Region::Header ? header : window;

    // HQ renders at the physical pixel density of the context (crisp on HiDPI);
    // Standard renders at 1x and lets the GPU/rasteriser scale it.
    const float phys = q == GraphicsQuality::High
                           ? juce::jlimit(1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor())
                           : 1.0f;

    if (c.image.isNull() || c.w != area.getWidth() || c.h != area.getHeight()
        || std::abs(c.scale - phys) > 0.01f || c.quality != int(q) || c.focus != focus)
    {
        c.w = area.getWidth(); c.h = area.getHeight(); c.scale = phys; c.quality = int(q); c.focus = focus;
        c.image = juce::Image(juce::Image::RGB, juce::jmax(1, juce::roundToInt(c.w * phys)),
                              juce::jmax(1, juce::roundToInt(c.h * phys)), true);
        juce::Graphics ig(c.image);
        ig.addTransform(juce::AffineTransform::scale(phys));
        if (region == Region::Header) renderHeader(ig, float(c.w), float(c.h), focus, q);
        else                          renderWindow(ig, float(c.w), float(c.h), q);
    }

    g.setImageResamplingQuality(q == GraphicsQuality::High ? juce::Graphics::highResamplingQuality
                                                           : juce::Graphics::mediumResamplingQuality);
    g.drawImage(c.image, area.toFloat());
}
} // namespace ambient::ui
