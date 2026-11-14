// Stereo vectorscope.
//
// M/S (goniometer): vertical = mid (L+R), horizontal = side (R-L), rotated 45 deg,
//   so a mono signal is a vertical line, L-only leans up-left, R-only up-right and
//   a polarity-inverted signal is a horizontal line.
// L/R (Lissajous XY): x = left, y = right; mono is the rising diagonal.
//
// Samples are accumulated additively into a phosphor image (bilinear splats in
// High quality, single pixels in Standard) at the physical pixel resolution.
// Only frames that arrived since the previous update are drawn; persistence
// handles the history.

#include "AnalyzerViews.h"
#include <ambient/VisualAnalysis.h>

namespace ambient::ui
{
using namespace theme;

namespace
{
const juce::Identifier kDisplay { "vs_display" }, kScale { "vs_scale" }, kPersist { "vs_persist" }, kIntensity { "vs_intensity" };
constexpr float kInvSqrt2 = 0.70710678f;

class StereoView : public AnalyzerView
{
public:
    StereoView(viz::VisualizerHub& h, UiSettings& s)
        : AnalyzerView(h, s),
          displayChip("Display", { "M / S", "L / R" }, int(s.get(kDisplay, 0))),
          scaleChip("Scale", { "Auto", "0 dB", "-6 dB", "-12 dB", "-18 dB", "-24 dB" }, int(s.get(kScale, 0))),
          persistChip("Persist", 0.0f, 0.95f, float(s.get(kPersist, 0.75)), 0.75f,
                      [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; }),
          intensityChip("Intensity", 0.1f, 4.0f, float(s.get(kIntensity, 1.0)), 1.0f,
                        [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; }, true)
    {
        for (auto* c : chips()) addAndMakeVisible(c);
        addAndMakeVisible(corr);

        displayChip.onChoice   = [this](int v) { settings.set(kDisplay, v); layer.clear(); repaint(); };
        scaleChip.onChoice     = [this](int v) { settings.set(kScale, v); layer.clear(); repaint(); };
        persistChip.onValue    = [this](float v) { settings.set(kPersist, v); };
        intensityChip.onValue  = [this](float v) { settings.set(kIntensity, v); };
        displayChip.setTooltip("M/S goniometer (mono = vertical) or L/R Lissajous (mono = diagonal)");
        scaleChip.setTooltip("Full-scale radius. Auto follows the signal peak.");
    }

    void activated() override { layer.clear(); }

    void refresh(bool fresh, float dt) override
    {
        if (! fresh) return;
        const auto& st = hub.getStereoStats();
        stats = st;
        corr.setCorrelation(st.correlation, dt);

        // Auto scale: fast attack, slow release on the channel peak.
        const int n = hub.getVectorLength();
        const float* L = hub.getVectorLeft();
        const float* R = hub.getVectorRight();
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) pk = juce::jmax(pk, std::abs(L[i]), std::abs(R[i]));
        autoPeak = pk > autoPeak ? pk : autoPeak * std::pow(0.5f, dt / 1.5f);
        autoPeak = juce::jmax(autoPeak, 0.0316f); // floor at -30 dBFS

        if (layer.getImage().isValid())
        {
            layer.fade(persistChip.getValue(), dt);
            plot(L, R, n);
        }
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const auto list = chips();
        const int rows = chipRowsNeeded(r.getWidth(), list);
        auto strip = r.removeFromBottom(rows * 26 + (rows - 1) * 3);
        r.removeFromBottom(8);
        layoutChips(strip, list, 26);

        // Square scope + info column (right when wide enough, else below).
        const int infoW = 190;
        if (r.getWidth() - infoW - 10 >= r.getHeight() * 0.8f)
        {
            info = r.removeFromRight(infoW);
            r.removeFromRight(10);
        }
        else
        {
            info = r.removeFromBottom(juce::jmin(118, r.getHeight() / 3));
            r.removeFromBottom(8);
        }
        screen = r;
        const int side = juce::jmin(r.getWidth(), r.getHeight());
        square = r.withSizeKeepingCentre(side, side).reduced(10);

        auto inf = info.reduced(0, 2);
        corrArea = inf.removeFromTop(64);
        corr.setBounds(corrArea.withTrimmedTop(20).withHeight(34));
        rest = inf;
    }

    void paint(juce::Graphics& g) override
    {
        drawScreen(g, screen.toFloat());
        const float phys = quality == GraphicsQuality::High ? physicalScale(g) : 1.0f;
        layer.ensure(square, phys);

        drawGuides(g);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        layer.draw(g);
        drawLabels(g);
        drawInfo(g);
    }

private:
    std::vector<juce::Component*> chips() { return { &displayChip, &scaleChip, &persistChip, &intensityChip }; }

    float fullScale() const
    {
        static const float fixedDb[] = { 0.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f };
        const int c = scaleChip.getChoice();
        return c == 0 ? autoPeak / 0.9f : juce::Decibels::decibelsToGain(fixedDb[c]);
    }

    /** Maps a stereo sample to [-1, 1]^2 (y up). */
    juce::Point<float> map(float l, float r, float inv) const
    {
        if (displayChip.getChoice() == 0) // M/S
            return { (r - l) * 0.5f * inv,   // side: L-only leans left, R-only right
                     (l + r) * 0.5f * inv }; // mid: L = R = full scale reaches the top vertex
        return { l * inv, r * inv };
    }

    void plot(const float* L, const float* R, int n)
    {
        auto& img = layer.getImage();
        const int W = img.getWidth(), H = img.getHeight();
        if (n <= 0 || W < 4 || H < 4) return;
        const float inv = 1.0f / fullScale();
        const bool hq = quality == GraphicsQuality::High;
        const float half = 0.5f * float(juce::jmin(W, H)) * 0.96f;
        const float cx = 0.5f * float(W), cy = 0.5f * float(H);

        // Brightness per hit, normalised by sample count so density does not blow out.
        const float base = intensityChip.getValue() * 2600.0f / std::sqrt(float(juce::jmax(64, n)));
        const juce::PixelARGB tint = lime.getPixelARGB();

        juce::Image::BitmapData bd(img, juce::Image::BitmapData::readWrite);
        auto add = [&](int x, int y, float w)
        {
            if (x < 0 || y < 0 || x >= W || y >= H) return;
            auto* p = reinterpret_cast<juce::PixelARGB*>(bd.getPixelPointer(x, y));
            const int a = juce::jlimit(0, 255, int(w * base));
            if (a == 0) return;
            // additive in premultiplied space, saturating; hot spots whiten slightly
            const int na = juce::jmin(255, p->getAlpha() + a);
            const auto blend = [a](int c, int t) { return juce::jmin(255, c + (t * a) / 255); };
            p->setARGB(uint8_t(na), uint8_t(blend(p->getRed(), tint.getRed())),
                       uint8_t(blend(p->getGreen(), tint.getGreen())), uint8_t(blend(p->getBlue(), tint.getBlue())));
            // keep premultiplied invariant (colour <= alpha)
            if (p->getRed() > na || p->getGreen() > na || p->getBlue() > na)
                p->setARGB(uint8_t(na), uint8_t(juce::jmin<int>(p->getRed(), na)), uint8_t(juce::jmin<int>(p->getGreen(), na)), uint8_t(juce::jmin<int>(p->getBlue(), na)));
        };

        auto splat = [&](juce::Point<float> q, float w)
        {
            const float px = cx + q.x * half, py = cy - q.y * half;
            if (! hq) { add(int(px), int(py), w); return; }
            // bilinear core + faint halo: reads as a soft phosphor beam
            const int x0 = int(std::floor(px)), y0 = int(std::floor(py));
            const float fx = px - float(x0), fy = py - float(y0);
            add(x0,     y0,     w * (1 - fx) * (1 - fy));
            add(x0 + 1, y0,     w * fx * (1 - fy));
            add(x0,     y0 + 1, w * (1 - fx) * fy);
            add(x0 + 1, y0 + 1, w * fx * fy);
            const float hw = w * 0.12f;
            add(x0 - 1, y0, hw); add(x0 + 2, y0 + 1, hw); add(x0 + 1, y0 - 1, hw); add(x0, y0 + 2, hw);
        };

        auto prev = map(L[0], R[0], inv);
        for (int i = 0; i < n; ++i)
        {
            const auto q = map(L[i], R[i], inv);
            if (hq) // two interpolated sub-samples: continuous traces instead of dotted ones
            {
                splat(prev + (q - prev) * 0.333f, 0.6f);
                splat(prev + (q - prev) * 0.667f, 0.6f);
            }
            splat(q, 1.0f);
            prev = q;
        }
    }

    void drawGuides(juce::Graphics& g)
    {
        const auto sq = square.toFloat();
        const auto c = sq.getCentre();
        const float half = sq.getWidth() * 0.5f * 0.96f;
        const bool ms = displayChip.getChoice() == 0;

        // outer circle + full-scale boundary of possible |L|,|R| <= FS
        g.setColour(grid);
        g.drawEllipse(juce::Rectangle<float>(half * 2, half * 2).withCentre(c), 1.0f);
        g.drawEllipse(juce::Rectangle<float>(half, half).withCentre(c), 1.0f);

        juce::Path fs;
        if (ms) // diamond: vertices at M = +-1 (top/bottom) and S = +-1 (sides)
        {
            fs.startNewSubPath(c.x, c.y - half);
            fs.lineTo(c.x + half, c.y);
            fs.lineTo(c.x, c.y + half);
            fs.lineTo(c.x - half, c.y);
            fs.closeSubPath();
        }
        else fs.addRectangle(juce::Rectangle<float>(half * 2, half * 2).withCentre(c));
        g.setColour(gridHi);
        g.strokePath(fs, juce::PathStrokeType(1.0f));

        // axes and L / R diagonals
        const float d = half * kInvSqrt2;
        g.setColour(grid.brighter(0.3f));
        const float dashes[] = { 3.0f, 4.0f };
        if (ms)
        {
            g.drawDashedLine({ c.x - d, c.y - d, c.x + d, c.y + d }, dashes, 2, 1.0f);
            g.drawDashedLine({ c.x + d, c.y - d, c.x - d, c.y + d }, dashes, 2, 1.0f);
        }
        else
        {
            g.drawDashedLine({ c.x - half, c.y + half, c.x + half, c.y - half }, dashes, 2, 1.0f);
        }
        g.setColour(gridHi.brighter(0.25f));
        g.drawLine(c.x - half, c.y, c.x + half, c.y, 1.0f);
        g.drawLine(c.x, c.y - half, c.x, c.y + half, 1.0f);

        // clear centre reference
        g.setColour(text.withAlpha(0.7f));
        g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre(c));
    }

    void drawLabels(juce::Graphics& g)
    {
        const auto sq = square.toFloat();
        const auto c = sq.getCentre();
        const float half = sq.getWidth() * 0.5f * 0.96f;
        const bool ms = displayChip.getChoice() == 0;
        g.setFont(heading(12.0f, false, 0.05f));
        auto at = [&](const juce::String& t, float x, float y, juce::Colour col)
        {
            g.setColour(col);
            g.drawText(t, juce::Rectangle<float>(40.0f, 16.0f).withCentre({ x, y }), juce::Justification::centred);
        };
        if (ms)
        {
            const float d = half * kInvSqrt2 + 12.0f;
            at("M", c.x, c.y - half - 9.0f, textDim);
            at("L", c.x - d, c.y - d, lime);
            at("R", c.x + d, c.y - d, uvBright);
            at("-S", c.x - half - 4.0f, c.y - 10.0f, textFaint);
            at("+S", c.x + half + 4.0f, c.y - 10.0f, textFaint);
        }
        else
        {
            at("L", c.x + half - 8.0f, c.y + 12.0f, lime);
            at("R", c.x + 12.0f, c.y - half + 8.0f, uvBright);
        }
        g.setFont(body(10.5f));
        g.setColour(textFaint);
        const auto fsDb = juce::Decibels::gainToDecibels(fullScale());
        g.drawText("FS " + juce::String(fsDb, 1) + " dB" + (scaleChip.getChoice() == 0 ? "  AUTO" : ""),
                   screen.toFloat().reduced(8.0f, 6.0f), juce::Justification::topLeft);
    }

    void drawInfo(juce::Graphics& g)
    {
        // Correlation
        auto ca = corrArea.toFloat();
        g.setFont(caption(10.5f));
        g.setColour(textFaint);
        g.drawText("CORRELATION", ca.withHeight(16.0f), juce::Justification::centredLeft);
        g.setFont(body(13.0f, Weight::Medium));
        g.setColour(corr.getDisplayed() < 0.0f ? magenta : text);
        g.drawText(juce::String(corr.getDisplayed(), 2), ca.withHeight(16.0f), juce::Justification::centredRight);

        auto r = rest.toFloat();
        const auto image = viz::classifyStereo(stats);

        // Phase / image reading
        const bool bad = image == viz::StereoImage::PhaseIssue || image == viz::StereoImage::Inverted;
        const auto col = bad ? magenta : (image == viz::StereoImage::Wide ? warn : (image == viz::StereoImage::Silent ? textFaint : lime));
        auto ph = r.removeFromTop(34.0f);
        g.setColour(col.withAlpha(0.12f));
        g.fillRoundedRectangle(ph.reduced(0.0f, 3.0f), 3.0f);
        g.setColour(col.withAlpha(0.7f));
        g.drawRoundedRectangle(ph.reduced(0.5f, 3.5f), 3.0f, 1.0f);
        g.setColour(col);
        g.setFont(body(12.0f, Weight::Medium));
        g.drawFittedText(viz::describe(image), ph.reduced(8.0f, 3.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.85f);
        r.removeFromTop(6.0f);

        auto bar = [&](const juce::String& lbl, const juce::String& val, float from, float to, float v, bool centred, juce::Colour c)
        {
            if (r.getHeight() < 30.0f) return;
            auto row = r.removeFromTop(32.0f);
            auto top = row.removeFromTop(15.0f);
            g.setFont(caption(10.5f));
            g.setColour(textFaint);
            g.drawText(lbl, top, juce::Justification::centredLeft);
            g.setFont(body(12.0f, Weight::Medium));
            g.setColour(text);
            g.drawText(val, top, juce::Justification::centredRight);
            auto b = row.removeFromTop(8.0f);
            g.setColour(juce::Colours::black);
            g.fillRoundedRectangle(b, 2.0f);
            const float t = juce::jlimit(0.0f, 1.0f, (v - from) / (to - from));
            const float x0 = centred ? b.getCentreX() : b.getX();
            const float x1 = b.getX() + b.getWidth() * t;
            g.setColour(c);
            g.fillRect(juce::Rectangle<float>(juce::jmin(x0, x1), b.getY() + 1.0f, std::abs(x1 - x0) + 1.0f, b.getHeight() - 2.0f));
            if (centred) { g.setColour(text.withAlpha(0.6f)); g.fillRect(b.getCentreX() - 0.5f, b.getY(), 1.0f, b.getHeight()); }
        };
        bar("WIDTH", juce::String(juce::roundToInt(stats.width * 100.0f)) + " %", 0.0f, 1.0f, stats.width, false,
            stats.width > 0.6f ? magenta : uvBright);
        const auto balTxt = std::abs(stats.balance) < 0.01f ? juce::String("C")
                          : (stats.balance < 0.0f ? "L " : "R ") + juce::String(juce::roundToInt(std::abs(stats.balance) * 100.0f));
        bar("BALANCE", balTxt, -1.0f, 1.0f, stats.balance, true, lime);
        auto db = [](float lin) { const float d = juce::Decibels::gainToDecibels(lin, -100.0f); return d <= -99.0f ? juce::String("-inf") : juce::String(d, 1); };
        bar("RMS  L", db(stats.rmsL) + " dB", -60.0f, 0.0f, juce::Decibels::gainToDecibels(stats.rmsL, -100.0f), false, lime);
        bar("RMS  R", db(stats.rmsR) + " dB", -60.0f, 0.0f, juce::Decibels::gainToDecibels(stats.rmsR, -100.0f), false, uvBright);
    }

    ControlChip displayChip, scaleChip, persistChip, intensityChip;
    CorrelationMeter corr;
    PersistenceLayer layer;
    viz::StereoStats stats;
    float autoPeak = 0.25f;
    juce::Rectangle<int> screen, square, info, corrArea, rest;
};
} // namespace

std::unique_ptr<AnalyzerView> createStereoView(viz::VisualizerHub& h, UiSettings& s)
{
    return std::make_unique<StereoView>(h, s);
}
} // namespace ambient::ui
