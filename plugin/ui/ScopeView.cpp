// Oscilloscope: trigger-aligned stereo waveform display.
//
// Rendering: each channel is one filled envelope built from the interpolated
// min/max of the waveform per pixel column (HQ: physical pixels, Standard:
// logical pixels). This is exact for dense data and traces the line between
// samples when zoomed in. Samples are never drawn as individual objects and the
// cost is bounded by the display width, not the sample count. Audio samples are
// never drawn as individual graphics objects.

#include "AnalyzerViews.h"
#include <ambient/VisualAnalysis.h>

namespace ambient::ui
{
using namespace theme;

namespace
{
enum View { Left = 0, Right, Overlay, Split, Mono };

const juce::Identifier kView { "sc_view" }, kTime { "sc_time" }, kGain { "sc_gain" },
                       kTrig { "sc_trig" }, kLevel { "sc_level" }, kPersist { "sc_persist" };

juce::String fmtMs(float ms)
{
    if (ms < 10.0f) return juce::String(ms, 1) + " ms";
    return juce::String(juce::roundToInt(ms)) + " ms";
}

class ScopeView : public AnalyzerView
{
public:
    ScopeView(viz::VisualizerHub& h, UiSettings& s)
        : AnalyzerView(h, s),
          viewChip("View", { "Left", "Right", "Overlay", "L / R split", "Mono sum" }, int(s.get(kView, int(Overlay)))),
          timeChip("Time", 1.0f, 500.0f, float(s.get(kTime, 20.0)), 20.0f, fmtMs, true),
          gainChip("Gain", 0.25f, 16.0f, float(s.get(kGain, 1.0)), 1.0f,
                   [](float v) { return "x " + juce::String(v, v < 10.0f ? 2 : 1); }, true),
          trigChip("Trigger", { "Free run", "Rising", "Falling" }, int(s.get(kTrig, 1))),
          levelChip("Level", -1.0f, 1.0f, float(s.get(kLevel, 0.0)), 0.0f,
                    [](float v) { return juce::String(v, 2); }),
          persistChip("Persist", 0.0f, 0.95f, float(s.get(kPersist, 0.6)), 0.6f,
                      [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; })
    {
        for (auto* c : chips()) addAndMakeVisible(c);

        viewChip.onChoice  = [this](int v) { settings.set(kView, v); applyTrigger(); layer.clear(); };
        timeChip.onValue   = [this](float v) { settings.set(kTime, v); hub.setScopeTimeMs(v); layer.clear(); repaint(); };
        gainChip.onValue   = [this](float v) { settings.set(kGain, v); layer.clear(); repaint(); };
        trigChip.onChoice  = [this](int v) { settings.set(kTrig, v); applyTrigger(); };
        levelChip.onValue  = [this](float v) { settings.set(kLevel, v); applyTrigger(); repaint(); };
        persistChip.onValue = [this](float v) { settings.set(kPersist, v); };

        trigChip.setTooltip("Trigger edge - aligns the display to a level crossing so waveforms stand still");
        levelChip.setTooltip("Trigger level (in signal units, before display gain)");
        persistChip.setTooltip("Phosphor persistence - how long previous traces remain visible");
    }

    void activated() override
    {
        hub.setScopeTimeMs(timeChip.getValue());
        applyTrigger();
        layer.clear();
    }

    void refresh(bool fresh, float dt) override
    {
        if (! fresh) return; // frozen or no audio: keep the current image
        if (! layer.getImage().isValid()) { repaint(); return; }
        layer.fade(persistChip.getValue(), dt);
        drawTraces();
        repaint(screen);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const std::vector<juce::Component*> list = chips();
        const int rows = chipRowsNeeded(r.getWidth(), list);
        auto strip = r.removeFromBottom(rows * 26 + (rows - 1) * 3);
        r.removeFromBottom(8);
        layoutChips(strip, list, 26);
        screen = r;
        plot = screen.reduced(1).withTrimmedBottom(18); // bottom band for time labels
    }

    void paint(juce::Graphics& g) override
    {
        drawScreen(g, screen.toFloat());
        const float phys = quality == GraphicsQuality::High ? physicalScale(g) : 1.0f;
        if (layer.ensure(plot, phys)) drawTraces();

        drawGrid(g);
        layer.draw(g);
        drawOverlay(g);
    }

private:
    std::vector<juce::Component*> chips()
    {
        return { &viewChip, &timeChip, &gainChip, &trigChip, &levelChip, &persistChip };
    }

    void applyTrigger()
    {
        static const viz::TriggerMode modes[] = { viz::TriggerMode::Free, viz::TriggerMode::Rising, viz::TriggerMode::Falling };
        hub.setTrigger(modes[juce::jlimit(0, 2, trigChip.getChoice())], levelChip.getValue());
        const int v = viewChip.getChoice();
        hub.setTriggerSource(v == Left ? viz::VisualizerHub::TriggerSource::Left
                           : v == Right ? viz::VisualizerHub::TriggerSource::Right
                                        : viz::VisualizerHub::TriggerSource::Mono);
    }

    /** Lanes in plot-local coordinates. */
    std::vector<juce::Rectangle<float>> lanes() const
    {
        auto r = juce::Rectangle<float>(0.0f, 0.0f, float(plot.getWidth()), float(plot.getHeight()));
        if (viewChip.getChoice() == Split)
        {
            auto top = r.removeFromTop(r.getHeight() * 0.5f);
            return { top.reduced(0.0f, 4.0f), r.reduced(0.0f, 4.0f) };
        }
        return { r.reduced(0.0f, 4.0f) };
    }

    void drawTraces()
    {
        const int n = hub.getScopeLength();
        if (n < 2 || ! layer.getImage().isValid()) return;
        auto g = layer.context();
        const bool hq = quality == GraphicsQuality::High;
        const float gain = gainChip.getValue();
        const auto ln = lanes();
        const float* L = hub.getScopeLeft();
        const float* R = hub.getScopeRight();

        switch (viewChip.getChoice())
        {
            case Left:    trace(*g, L, n, ln[0], gain, lime, hq); break;
            case Right:   trace(*g, R, n, ln[0], gain, uvBright, hq); break;
            case Overlay: trace(*g, R, n, ln[0], gain, uvBright.withAlpha(0.85f), hq);
                          trace(*g, L, n, ln[0], gain, lime.withAlpha(0.9f), hq); break;
            case Split:   trace(*g, L, n, ln[0], gain, lime, hq);
                          trace(*g, R, n, ln[1], gain, uvBright, hq); break;
            case Mono:
            {
                monoBuf.resize(size_t(n));
                for (int i = 0; i < n; ++i) monoBuf[size_t(i)] = 0.5f * (L[i] + R[i]);
                trace(*g, monoBuf.data(), n, ln[0], gain, lime, hq);
                break;
            }
            default: break;
        }
    }

    void trace(juce::Graphics& g, const float* data, int n, juce::Rectangle<float> lane, float gain,
               juce::Colour col, bool hq)
    {
        const float scale = hq ? layer.getScale() : 1.0f;
        const int cols = juce::jmax(2, juce::roundToInt(lane.getWidth() * scale));
        const float cy = lane.getCentreY(), half = lane.getHeight() * 0.5f;
        auto yOf = [&](float v) { return juce::jlimit(lane.getY(), lane.getBottom(), cy - v * gain * half); };
        const float px = 1.0f / scale; // one physical pixel in logical units

        // One filled envelope per channel: interpolated min/max per pixel column.
        colMin.resize(size_t(cols)); colMax.resize(size_t(cols));
        viz::interpolatedMinMax(data, size_t(n), size_t(cols), colMin.data(), colMax.data());
        const float dx = lane.getWidth() / float(cols);
        auto envelope = [&](float thickness)
        {
            juce::Path p;
            p.preallocateSpace(cols * 6 + 8);
            for (int c = 0; c < cols; ++c)
            {
                const float x = lane.getX() + (float(c) + 0.5f) * dx;
                const float yt = juce::jmin(yOf(colMax[size_t(c)]), yOf(colMin[size_t(c)]) - thickness);
                if (c == 0) p.startNewSubPath(x, yt); else p.lineTo(x, yt);
            }
            for (int c = cols - 1; c >= 0; --c)
            {
                const float x = lane.getX() + (float(c) + 0.5f) * dx;
                p.lineTo(x, juce::jmax(yOf(colMin[size_t(c)]), yOf(colMax[size_t(c)]) + thickness));
            }
            p.closeSubPath();
            return p;
        };
        if (hq)
        {
            g.setColour(col.withMultipliedAlpha(0.14f)); // soft beam glow
            g.fillPath(envelope(px * 2.5f));
        }
        g.setColour(col);
        g.fillPath(envelope(px * 0.9f));
    }

    void drawGrid(juce::Graphics& g)
    {
        const auto pr = plot.toFloat();
        const float gain = gainChip.getValue();
        for (auto lane : lanes())
        {
            lane = lane.translated(pr.getX(), pr.getY());
            const float cy = lane.getCentreY(), half = lane.getHeight() * 0.5f;
            // amplitude lines at 1/4 divisions of full scale (as displayed)
            for (int k = -4; k <= 4; ++k)
            {
                const float y = cy - half * float(k) / 4.0f;
                g.setColour(k == 0 ? gridHi.brighter(0.2f) : (k % 2 == 0 ? gridHi : grid));
                g.drawHorizontalLine(juce::roundToInt(y), pr.getX(), pr.getRight());
            }
            g.setFont(body(10.5f));
            g.setColour(textFaint);
            const float fs = 1.0f / gain; // signal value at the lane edge
            g.drawText("+" + juce::String(fs, fs < 1.0f ? 2 : 1), juce::Rectangle<float>(pr.getX() + 4.0f, lane.getY(), 60.0f, 14.0f), juce::Justification::topLeft);
            g.drawText("-" + juce::String(fs, fs < 1.0f ? 2 : 1), juce::Rectangle<float>(pr.getX() + 4.0f, lane.getBottom() - 14.0f, 60.0f, 14.0f), juce::Justification::bottomLeft);
        }
        // time divisions
        const int divs = 10;
        for (int i = 1; i < divs; ++i)
        {
            const float x = pr.getX() + pr.getWidth() * float(i) / float(divs);
            g.setColour(i == divs / 2 ? gridHi : grid);
            g.drawVerticalLine(juce::roundToInt(x), pr.getY(), pr.getBottom());
        }
        // time labels
        const float ms = float(hub.getScopeLength() * 1000.0 / hub.getSampleRate());
        g.setFont(body(10.5f));
        g.setColour(textFaint);
        const auto band = juce::Rectangle<float>(pr.getX(), pr.getBottom(), pr.getWidth(), 17.0f);
        for (int i = 0; i <= divs; i += 2)
        {
            const float x = band.getX() + band.getWidth() * float(i) / float(divs);
            const auto lbl = i == 0 ? juce::String("0") : fmtMs(ms * float(i) / float(divs));
            const auto just = i == 0 ? juce::Justification::centredLeft : (i == divs ? juce::Justification::centredRight : juce::Justification::centred);
            const float w = 56.0f;
            const float lx = i == 0 ? x + 4.0f : (i == divs ? x - w - 4.0f : x - w * 0.5f);
            g.drawText(lbl, juce::Rectangle<float>(lx, band.getY(), w, band.getHeight()), just);
        }
        g.setColour(textDim);
        g.drawText(fmtMs(ms / float(divs)) + " / div", juce::Rectangle<float>(pr.getCentreX() - 60.0f, pr.getY() + 3.0f, 120.0f, 14.0f),
                   juce::Justification::centred);
    }

    void drawOverlay(juce::Graphics& g)
    {
        const auto pr = plot.toFloat();
        // channel legend
        const int v = viewChip.getChoice();
        g.setFont(caption(10.5f));
        auto legend = juce::Rectangle<float>(pr.getRight() - 120.0f, pr.getY() + 4.0f, 114.0f, 14.0f);
        auto tag = [&](const juce::String& t, juce::Colour c, juce::Rectangle<float> at)
        {
            g.setColour(c);
            g.fillRect(at.removeFromLeft(10.0f).withSizeKeepingCentre(10.0f, 2.0f));
            g.drawText(t, at.withTrimmedLeft(4.0f), juce::Justification::centredLeft);
        };
        if (v == Overlay)
        {
            tag("L", lime, legend.withWidth(40.0f).translated(50.0f, 0.0f));
            tag("R", uvBright, legend.withWidth(40.0f).translated(84.0f, 0.0f));
        }
        else if (v == Split)
        {
            const auto ln = lanes();
            tag("L", lime, legend.withWidth(40.0f).translated(84.0f, 0.0f));
            tag("R", uvBright, legend.withWidth(40.0f).translated(84.0f, ln[1].getY() - 4.0f));
        }
        else
            tag(v == Left ? "L" : (v == Right ? "R" : "MONO  (L+R)/2"), v == Right ? uvBright : lime,
                legend.withTrimmedLeft(v == Mono ? 0.0f : 84.0f));

        // trigger marker
        if (trigChip.getChoice() != 0)
        {
            const float gain = gainChip.getValue();
            for (auto lane : lanes())
            {
                lane = lane.translated(pr.getX(), pr.getY());
                const float y = lane.getCentreY() - levelChip.getValue() * gain * lane.getHeight() * 0.5f;
                if (y < lane.getY() || y > lane.getBottom()) continue;
                juce::Path tri;
                tri.addTriangle(pr.getX(), y - 5.0f, pr.getX(), y + 5.0f, pr.getX() + 7.0f, y);
                g.setColour(uv);
                g.fillPath(tri);
                g.setColour(uv.withAlpha(0.35f));
                const float dashes[] = { 3.0f, 4.0f };
                g.drawDashedLine({ pr.getX() + 8.0f, y, pr.getX() + 60.0f, y }, dashes, 2, 1.0f);
                if (viewChip.getChoice() != Split) break;
            }
        }
    }

    ControlChip viewChip, timeChip, gainChip, trigChip, levelChip, persistChip;
    PersistenceLayer layer;
    juce::Rectangle<int> screen, plot;
    std::vector<float> colMin, colMax, monoBuf;
};
} // namespace

std::unique_ptr<AnalyzerView> createScopeView(viz::VisualizerHub& h, UiSettings& s)
{
    return std::make_unique<ScopeView>(h, s);
}
} // namespace ambient::ui
