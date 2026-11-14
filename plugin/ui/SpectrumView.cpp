// Spectrum analyzer view (reuses the tested SpectrumAnalyser via the hub).
// Level convention: see viz::SpectrumChannel - dBFS of the selected signal,
// full-scale sine = 0 dB.

#include "AnalyzerViews.h"
#include <ambient/VisualAnalysis.h>

namespace ambient::ui
{
using namespace theme;

namespace
{
const juce::Identifier kChan { "sp_channel" }, kFft { "sp_fft" }, kAvg { "sp_avg" }, kSmooth { "sp_smooth" },
                       kRange { "sp_range" }, kHold { "sp_hold" }, kDecay { "sp_decay" }, kPeaks { "sp_peaks" };

constexpr float kFMin = 20.0f;
const int   kOrders[] = { 10, 11, 12, 13, 14 };
const float kSmoothOct[] = { 0.0f, 1.0f / 24.0f, 1.0f / 12.0f, 1.0f / 6.0f, 1.0f / 3.0f };
const float kRanges[] = { 60.0f, 90.0f, 120.0f };

juce::String fmtHz(float f)
{
    if (f >= 1000.0f) return juce::String(f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String(juce::roundToInt(f)) + " Hz";
}

juce::String noteName(float f)
{
    if (f <= 0.0f) return {};
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float midi = 69.0f + 12.0f * std::log2(f / 440.0f);
    const int n = juce::roundToInt(midi);
    const int cents = juce::roundToInt((midi - float(n)) * 100.0f);
    const int idx = ((n % 12) + 12) % 12;
    return juce::String(names[idx]) + juce::String(n / 12 - 1) + (cents == 0 ? "" : (cents > 0 ? " +" : " ") + juce::String(cents) + "c");
}

class SpectrumView : public AnalyzerView
{
public:
    SpectrumView(viz::VisualizerHub& h, UiSettings& s)
        : AnalyzerView(h, s),
          chanChip("Channel", { "Left", "Right", "Stereo", "Mid", "Side" }, int(s.get(kChan, 2))),
          fftChip("FFT", { "1k", "2k", "4k", "8k", "16k" }, int(s.get(kFft, 2))),
          avgChip("Average", 0.0f, 0.95f, float(s.get(kAvg, 0.7)), 0.7f,
                  [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; }),
          smoothChip("Smooth", { "Off", "1/24 oct", "1/12 oct", "1/6 oct", "1/3 oct" }, int(s.get(kSmooth, 2))),
          rangeChip("Range", { "60 dB", "90 dB", "120 dB" }, int(s.get(kRange, 1))),
          holdChip("Peak hold", { "Off", "On" }, int(s.get(kHold, 1))),
          decayChip("Decay", 1.0f, 60.0f, float(s.get(kDecay, 12.0)), 12.0f,
                    [](float v) { return juce::String(v, v < 10.0f ? 1 : 0) + " dB/s"; }, true),
          peaksChip("Peak find", { "Off", "On" }, int(s.get(kPeaks, 1)))
    {
        for (auto* c : chips()) addAndMakeVisible(c);

        chanChip.onChoice   = [this](int v) { settings.set(kChan, v); apply(); };
        fftChip.onChoice    = [this](int v) { settings.set(kFft, v); apply(); };
        avgChip.onValue     = [this](float v) { settings.set(kAvg, v); hub.setAveraging(v); };
        smoothChip.onChoice = [this](int v) { settings.set(kSmooth, v); rebuildCurves(); repaint(); };
        rangeChip.onChoice  = [this](int v) { settings.set(kRange, v); repaint(); };
        holdChip.onChoice   = [this](int v) { settings.set(kHold, v); hub.resetPeaks(); repaint(); };
        decayChip.onValue   = [this](float v) { settings.set(kDecay, v); hub.setPeakDecay(v); };
        peaksChip.onChoice  = [this](int v) { settings.set(kPeaks, v); repaint(); };

        chanChip.setTooltip("Stereo shows L and R as two separately calibrated curves. Mid = (L+R)/2, Side = (L-R)/2.");
        fftChip.setTooltip("FFT size: larger = finer frequency resolution, slower response");
        avgChip.setTooltip("Averaging (power domain) between updates");
        smoothChip.setTooltip("Fractional-octave smoothing of the displayed curve");
        decayChip.setTooltip("Peak-hold fall rate");
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    }

    void activated() override { apply(); }

    void refresh(bool fresh, float) override
    {
        if (! fresh) return;
        rebuildCurves();
        repaint(screen);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const auto list = chips();
        const int rows = chipRowsNeeded(r.getWidth(), list);
        auto strip = r.removeFromBottom(rows * 26 + (rows - 1) * 3);
        r.removeFromBottom(8);
        layoutChips(strip, list, 26);
        screen = r;
        plot = screen.reduced(1).withTrimmedBottom(18).withTrimmedRight(34); // freq labels below, dB at right
        rebuildCurves();
    }

    void mouseMove(const juce::MouseEvent& e) override { hover = plot.contains(e.getPosition()) ? e.position : juce::Point<float>(-1, -1); repaint(screen); }
    void mouseExit(const juce::MouseEvent&) override  { hover = { -1, -1 }; repaint(screen); }

    void paint(juce::Graphics& g) override
    {
        drawScreen(g, screen.toFloat());
        const float phys = physicalScale(g);
        if (std::abs(phys - lastPhys) > 0.01f) { lastPhys = phys; rebuildCurves(); }

        drawGrid(g);
        {
            juce::Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(plot);
            const bool stereo = hub.getSpectrumChannel() == viz::SpectrumChannel::Stereo;
            if (holdChip.getChoice() == 1)
            {
                drawCurve(g, peakA, magenta.withAlpha(0.75f), false, 1.0f);
                if (stereo) drawCurve(g, peakB, magenta.withAlpha(0.45f), false, 1.0f);
            }
            if (stereo)
            {
                drawCurve(g, curveB, uvBright, true, 1.4f);
                drawCurve(g, curveA, lime, false, 1.4f);
            }
            else drawCurve(g, curveA, lime, true, 1.5f);
        }
        drawPeak(g);
        drawLegend(g);
        drawHover(g);
    }

private:
    std::vector<juce::Component*> chips()
    {
        return { &chanChip, &fftChip, &avgChip, &smoothChip, &rangeChip, &holdChip, &decayChip, &peaksChip };
    }

    void apply()
    {
        hub.setSpectrumChannel(viz::SpectrumChannel(juce::jlimit(0, 4, chanChip.getChoice())));
        hub.setFftOrder(kOrders[juce::jlimit(0, 4, fftChip.getChoice())]);
        hub.setAveraging(avgChip.getValue());
        hub.setPeakDecay(decayChip.getValue());
        rebuildCurves();
        repaint();
    }

    float fMax() const { return float(juce::jmin(20000.0, hub.getSampleRate() * 0.5)); }
    float bottomDb() const { return -kRanges[juce::jlimit(0, 2, rangeChip.getChoice())]; }

    float xOf(float f) const { return float(plot.getX()) + float(plot.getWidth()) * viz::logPos(f, kFMin, fMax()); }
    float yOf(float db) const
    {
        const float t = (db - bottomDb()) / (0.0f - bottomDb());
        return float(plot.getBottom()) - t * float(plot.getHeight());
    }

    void rebuildCurves()
    {
        if (plot.isEmpty()) return;
        // HQ: one point per physical pixel. Standard: one point per two logical pixels.
        const int pts = quality == GraphicsQuality::High
                            ? juce::jlimit(64, 4096, juce::roundToInt(float(plot.getWidth()) * lastPhys))
                            : juce::jlimit(32, 1024, plot.getWidth() / 2);
        const float sm = kSmoothOct[juce::jlimit(0, 4, smoothChip.getChoice())];
        const bool stereo = hub.getSpectrumChannel() == viz::SpectrumChannel::Stereo;
        for (auto* v : { &curveA, &curveB, &peakA, &peakB }) v->resize(size_t(pts));
        hub.spectrum().renderLogCurve(pts, kFMin, fMax(), sm, false, curveA.data());
        hub.spectrum().renderLogCurve(pts, kFMin, fMax(), sm, true, peakA.data());
        if (stereo)
        {
            hub.spectrumRight().renderLogCurve(pts, kFMin, fMax(), sm, false, curveB.data());
            hub.spectrumRight().renderLogCurve(pts, kFMin, fMax(), sm, true, peakB.data());
        }
    }

    void drawCurve(juce::Graphics& g, const std::vector<float>& c, juce::Colour col, bool fill, float width)
    {
        const int n = int(c.size());
        if (n < 2) return;
        const float x0 = float(plot.getX()), w = float(plot.getWidth());
        const float yBottom = float(plot.getBottom()) + 2.0f;
        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float x = x0 + w * float(i) / float(n - 1);
            const float y = juce::jmin(yBottom, yOf(c[size_t(i)]));
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        if (fill)
        {
            juce::Path area(p);
            area.lineTo(x0 + w, yBottom);
            area.lineTo(x0, yBottom);
            area.closeSubPath();
            juce::ColourGradient grad(uv.withAlpha(quality == GraphicsQuality::High ? 0.38f : 0.30f), 0.0f, float(plot.getY()),
                                      uvDeep.withAlpha(0.05f), 0.0f, float(plot.getBottom()), false);
            g.setGradientFill(grad);
            g.fillPath(area);
        }
        if (quality == GraphicsQuality::High)
        {
            g.setColour(col.withMultipliedAlpha(0.16f));
            g.strokePath(p, juce::PathStrokeType(width * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(col);
        g.strokePath(p, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawGrid(juce::Graphics& g)
    {
        const auto pr = plot.toFloat();
        // frequency: minor lines at 2..9 x decade, major at decades
        for (float dec = 10.0f; dec <= 10000.0f; dec *= 10.0f)
            for (int k = 1; k <= 9; ++k)
            {
                const float f = dec * float(k);
                if (f < kFMin || f > fMax()) continue;
                g.setColour(k == 1 ? gridHi : grid);
                g.drawVerticalLine(juce::roundToInt(xOf(f)), pr.getY(), pr.getBottom());
            }
        // level: 10 dB steps (20 dB labels on 120 dB range)
        const float bottom = bottomDb();
        const int labelStep = bottom < -100.0f ? 20 : 10;
        g.setFont(body(10.5f));
        for (int db = 0; db >= int(bottom); db -= 10)
        {
            const float y = yOf(float(db));
            g.setColour(db == 0 ? gridHi.brighter(0.2f) : grid);
            g.drawHorizontalLine(juce::roundToInt(y), pr.getX(), pr.getRight());
            if ((-db) % labelStep == 0)
            {
                g.setColour(textFaint);
                const float ly = juce::jlimit(pr.getY(), pr.getBottom() - 30.0f, y - 7.0f); // keep clear of edges / "dBFS"
                g.drawText(juce::String(db), juce::Rectangle<float>(pr.getRight() + 4.0f, ly, 30.0f, 14.0f), juce::Justification::centredLeft);
            }
        }
        // frequency labels
        const float labels[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        for (float f : labels)
        {
            if (f > fMax() * 1.001f) continue;
            const float x = xOf(f);
            const auto t = f >= 1000.0f ? juce::String(juce::roundToInt(f / 1000.0f)) + "k" : juce::String(juce::roundToInt(f));
            const float w = 34.0f;
            const float lx = juce::jlimit(pr.getX(), pr.getRight() - w, x - w * 0.5f);
            g.setColour(textFaint);
            g.drawText(t, juce::Rectangle<float>(lx, pr.getBottom() + 2.0f, w, 15.0f),
                       f == labels[0] ? juce::Justification::centredLeft : juce::Justification::centred);
        }
        g.setColour(textFaint);
        g.drawText("dBFS", juce::Rectangle<float>(pr.getRight() + 2.0f, pr.getBottom() + 2.0f, 34.0f, 15.0f), juce::Justification::centredLeft);
    }

    void drawPeak(juce::Graphics& g)
    {
        if (peaksChip.getChoice() == 0) return;
        auto& a = hub.spectrum();
        const float f = a.findPeakFrequency(kFMin);
        if (f < kFMin || f > fMax()) return;
        const int bin = juce::jlimit(0, a.getNumBins() - 1, juce::roundToInt(f / a.binFrequency(1)));
        const float db = a.getAverageDb()[size_t(bin)];
        if (db < bottomDb() + 6.0f) return; // nothing meaningful above the floor
        const float x = xOf(f), y = yOf(db);
        g.setColour(text);
        g.drawEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({ x, y }), 1.3f);
        const auto lbl = fmtHz(f) + "   " + noteName(f) + "   " + juce::String(db, 1) + " dB";
        const auto fnt = body(11.5f, Weight::Medium);
        const float w = textWidth(fnt, lbl) + 14.0f;
        auto box = juce::Rectangle<float>(w, 20.0f).withPosition(x + 8.0f, y - 26.0f);
        if (box.getRight() > float(plot.getRight())) box.setX(x - 8.0f - w);
        if (box.getY() < float(plot.getY()) + 2.0f) box.setY(y + 8.0f);
        g.setColour(bgDeep.withAlpha(0.85f));
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(box, 3.0f, 1.0f);
        g.setColour(text);
        g.setFont(fnt);
        g.drawText(lbl, box, juce::Justification::centred);
    }

    void drawLegend(juce::Graphics& g)
    {
        const auto pr = plot.toFloat();
        g.setFont(caption(10.5f));
        auto at = juce::Rectangle<float>(pr.getRight() - 210.0f, pr.getY() + 5.0f, 204.0f, 14.0f);
        auto tag = [&](const juce::String& t, juce::Colour c)
        {
            const float w = textWidth(caption(10.5f), t) + 18.0f;
            auto r = at.removeFromRight(w);
            g.setColour(c);
            g.fillRect(r.removeFromLeft(10.0f).withSizeKeepingCentre(10.0f, 2.0f));
            g.drawText(t, r.withTrimmedLeft(4.0f), juce::Justification::centredLeft);
            at.removeFromRight(8.0f);
        };
        if (holdChip.getChoice() == 1) tag("PEAK", magenta);
        switch (hub.getSpectrumChannel())
        {
            case viz::SpectrumChannel::Stereo: tag("R", uvBright); tag("L", lime); break;
            case viz::SpectrumChannel::Left:   tag("LEFT", lime); break;
            case viz::SpectrumChannel::Right:  tag("RIGHT", lime); break;
            case viz::SpectrumChannel::Mid:    tag("MID (L+R)/2", lime); break;
            case viz::SpectrumChannel::Side:   tag("SIDE (L-R)/2", lime); break;
        }
        g.setColour(textFaint);
        g.setFont(body(10.5f));
        const float res = float(hub.getSampleRate() / hub.spectrum().getSize());
        g.drawText(juce::String(res, 1) + " Hz/bin", pr.withHeight(18.0f).translated(6.0f, 2.0f), juce::Justification::centredLeft);
    }

    void drawHover(juce::Graphics& g)
    {
        if (hover.x < 0.0f) return;
        const float t = (hover.x - float(plot.getX())) / float(plot.getWidth());
        const float f = viz::logFreq(juce::jlimit(0.0f, 1.0f, t), kFMin, fMax());
        const int idx = juce::jlimit(0, int(curveA.size()) - 1, juce::roundToInt(t * float(curveA.size() - 1)));
        const float db = curveA.empty() ? -200.0f : curveA[size_t(idx)];
        g.setColour(text.withAlpha(0.35f));
        g.drawVerticalLine(juce::roundToInt(hover.x), float(plot.getY()), float(plot.getBottom()));
        const auto lbl = fmtHz(f) + "  " + noteName(f) + "  " + (db < bottomDb() ? juce::String("< ") + juce::String(int(bottomDb())) : juce::String(db, 1)) + " dB";
        const auto fnt = body(11.5f);
        const float w = textWidth(fnt, lbl) + 12.0f;
        auto box = juce::Rectangle<float>(w, 18.0f).withPosition(hover.x + 8.0f, float(plot.getBottom()) - 24.0f);
        if (box.getRight() > float(plot.getRight())) box.setX(hover.x - 8.0f - w);
        g.setColour(panelHdr);
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(text);
        g.setFont(fnt);
        g.drawText(lbl, box, juce::Justification::centred);
    }

    ControlChip chanChip, fftChip, avgChip, smoothChip, rangeChip, holdChip, decayChip, peaksChip;
    juce::Rectangle<int> screen, plot;
    std::vector<float> curveA, curveB, peakA, peakB;
    float lastPhys = 1.0f;
    juce::Point<float> hover { -1.0f, -1.0f };
};
} // namespace

std::unique_ptr<AnalyzerView> createSpectrumView(viz::VisualizerHub& h, UiSettings& s)
{
    return std::make_unique<SpectrumView>(h, s);
}
} // namespace ambient::ui
