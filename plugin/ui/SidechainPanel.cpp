#include "SidechainPanel.h"
#include "Mascot.h"
#include "../PluginProcessor.h"
#include "../Parameters.h"
#include <ambient/ParamIDs.h>
#include <ambient/dsp/Svf.h>
#include <ambient/VisualAnalysis.h>

namespace ambient::ui
{
using namespace theme;
namespace scid = ambient::ids::sc;

namespace
{
constexpr float kFMin = 20.0f, kFMax = 5000.0f;
constexpr float kSpecTop = 0.0f, kSpecBottom = -84.0f, kGrRange = 24.0f;

juce::String bandParamId(int band, int field) { return juce::String(scid::bandId(band, scid::BandField(field))); }

juce::String fmtHz(float f) { return f >= 1000.0f ? juce::String(f / 1000.0f, 2) + " kHz" : juce::String(juce::roundToInt(f)) + " Hz"; }
}

// ============================================================================
// SidechainGraph
// ============================================================================
SidechainGraph::SidechainGraph(AmbientProcessor& p) : proc(p)
{
    for (auto* a : { &refSpec, &outSpec })
    {
        a->setOrder(13);       // ~6 Hz bins at 48 kHz: kick / bass fundamentals resolvable
        a->setAveraging(0.6f);
        a->setPeakDecay(20.0f);
    }
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

SidechainGraph::~SidechainGraph() = default;

juce::RangedAudioParameter* SidechainGraph::bandParam(int band, int field) const
{
    return proc.apvts.getParameter(bandParamId(band, field));
}

void SidechainGraph::tick(float dt, bool analyse)
{
    const auto& t = proc.sidechainTelemetry();
    for (int b = 0; b < 4; ++b)
    {
        bandFreq[size_t(b)] = t.bandFreq[size_t(b)].load(std::memory_order_relaxed);
        bandRed[size_t(b)] = t.bandReductionDb[size_t(b)].load(std::memory_order_relaxed);
        bandQ[size_t(b)] = proc.apvts.getRawParameterValue(bandParamId(b, scid::BQ))->load();
        bandOn[size_t(b)] = proc.apvts.getRawParameterValue(bandParamId(b, scid::BEnable))->load() > 0.5f;
        if (bandFreq[size_t(b)] < 1.0f) bandFreq[size_t(b)] = proc.apvts.getRawParameterValue(bandParamId(b, scid::BFreq))->load();
    }
    refPresent = t.referencePresent.load(std::memory_order_relaxed);
    maxRed = t.maxReductionDb.load(std::memory_order_relaxed);
    fullBand = proc.apvts.getRawParameterValue(id(scid::fullBand))->load() > 0.5f;
    automatic = proc.apvts.getRawParameterValue(id(scid::automatic))->load() > 0.5f;
    rangeLo = proc.apvts.getRawParameterValue(id(scid::rangeLow))->load();
    rangeHi = proc.apvts.getRawParameterValue(id(scid::rangeHigh))->load();
    noSignalTime = refPresent ? 0.0f : noSignalTime + dt;

    if (analyse)
    {
        const double sr = proc.currentSampleRate.load();
        const int n = refSpec.getSize();
        scratchL.resize(size_t(n)); scratchR.resize(size_t(n)); mono.resize(size_t(n));
        auto run = [&](const StereoCaptureRing& ring, viz::SpectrumAnalyser& a)
        {
            a.setSampleRate(sr);
            if (! ring.readLatest(n, scratchL.data(), scratchR.data())) return;
            for (int i = 0; i < n; ++i) mono[size_t(i)] = 0.5f * (scratchL[size_t(i)] + scratchR[size_t(i)]);
            a.process(mono.data(), dt);
        };
        if (refPresent) run(proc.refRing, refSpec);
        run(proc.captureRing, outSpec);

        const int pts = juce::jlimit(64, 1024, plot.getWidth() / (quality == GraphicsQuality::High ? 1 : 2));
        refCurve.resize(size_t(pts)); outCurve.resize(size_t(pts));
        refSpec.renderLogCurve(pts, kFMin, kFMax, 1.0f / 6.0f, false, refCurve.data());
        outSpec.renderLogCurve(pts, kFMin, kFMax, 1.0f / 6.0f, false, outCurve.data());
    }
    repaint();
}

void SidechainGraph::resized() { plot = getLocalBounds().reduced(1).withTrimmedBottom(16).withTrimmedRight(30); }

float SidechainGraph::xOf(float f) const { return float(plot.getX()) + float(plot.getWidth()) * viz::logPos(juce::jlimit(kFMin, kFMax, f), kFMin, kFMax); }
float SidechainGraph::fOfX(float x) const { return viz::logFreq(juce::jlimit(0.0f, 1.0f, (x - float(plot.getX())) / float(plot.getWidth())), kFMin, kFMax); }
float SidechainGraph::ySpec(float db) const
{
    const float t = (juce::jlimit(kSpecBottom, kSpecTop, db) - kSpecBottom) / (kSpecTop - kSpecBottom);
    return float(plot.getBottom()) - t * float(plot.getHeight());
}
float SidechainGraph::yGr(float grDb) const // 0 dB at the top, -24 dB at 60 % of the height
{
    return float(plot.getY()) + juce::jlimit(0.0f, 1.0f, -grDb / kGrRange) * float(plot.getHeight()) * 0.6f;
}

float SidechainGraph::reductionAt(float f) const
{
    if (fullBand) return -maxRed;
    const double sr = proc.currentSampleRate.load();
    float db = 0.0f;
    for (int b = 0; b < 4; ++b)
        if (bandRed[size_t(b)] > 0.001f)
            db += dsp::Bell::magnitudeDb(sr, bandFreq[size_t(b)], bandQ[size_t(b)], bandRed[size_t(b)], f);
    return db;
}

int SidechainGraph::bandAt(juce::Point<float> p) const
{
    int best = -1; float bestD = 18.0f;
    for (int b = 0; b < 4; ++b)
    {
        const float d = std::abs(p.x - xOf(bandFreq[size_t(b)]));
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void SidechainGraph::mouseDown(const juce::MouseEvent& e)
{
    dragging = bandAt(e.position);
    if (dragging >= 0)
    {
        selected = dragging;
        if (onBandSelected) onBandSelected(dragging);
        if (auto* p = bandParam(dragging, scid::BFreq)) p->beginChangeGesture();
    }
}

void SidechainGraph::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    // Dragging a band takes manual control (the user always has the last word).
    if (automatic)
        if (auto* a = proc.apvts.getParameter(id(scid::automatic))) a->setValueNotifyingHost(0.0f);
    if (auto* p = bandParam(dragging, scid::BFreq))
        p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(20.0f, 2000.0f, fOfX(e.position.x))));
}

void SidechainGraph::mouseUp(const juce::MouseEvent&)
{
    if (dragging >= 0) if (auto* p = bandParam(dragging, scid::BFreq)) p->endChangeGesture();
    dragging = -1;
}

void SidechainGraph::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = bandAt(e.position) >= 0 ? bandAt(e.position) : selected;
    if (auto* p = bandParam(b, scid::BQ))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->getValue() + w.deltaY * 0.08f));
        p->endChangeGesture();
    }
}

void SidechainGraph::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(bgDeep);
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
    const auto pr = plot.toFloat();

    // grid
    for (float dec = 10.0f; dec <= 1000.0f; dec *= 10.0f)
        for (int k = 1; k <= 9; ++k)
        {
            const float f = dec * float(k);
            if (f < kFMin || f > kFMax) continue;
            g.setColour(k == 1 ? gridHi : grid);
            g.drawVerticalLine(juce::roundToInt(xOf(f)), pr.getY(), pr.getBottom());
        }
    g.setFont(body(10.5f));
    for (float f : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f })
    {
        const auto t = f >= 1000.0f ? juce::String(juce::roundToInt(f / 1000.0f)) + "k" : juce::String(juce::roundToInt(f));
        g.setColour(textFaint);
        g.drawText(t, juce::Rectangle<float>(juce::jlimit(pr.getX(), pr.getRight() - 30.0f, xOf(f) - 15.0f), pr.getBottom() + 1.0f, 30.0f, 14.0f),
                   f == 20.0f ? juce::Justification::centredLeft : juce::Justification::centred);
    }
    for (float db : { 0.0f, -6.0f, -12.0f, -24.0f }) // reduction scale (right)
    {
        const float y = yGr(db);
        g.setColour(db == 0.0f ? gridHi : grid);
        g.drawHorizontalLine(juce::roundToInt(y), pr.getX(), pr.getRight());
        g.setColour(uvBright.withAlpha(0.7f));
        g.drawText(juce::String(int(db)), juce::Rectangle<float>(pr.getRight() + 3.0f, juce::jmax(pr.getY(), y - 7.0f), 28.0f, 14.0f), juce::Justification::centredLeft);
    }

    // Auto search range
    if (automatic)
    {
        const float x0 = xOf(rangeLo), x1 = xOf(rangeHi);
        g.setColour(lime.withAlpha(0.05f));
        g.fillRect(juce::Rectangle<float>(x0, pr.getY(), x1 - x0, pr.getHeight()));
        g.setColour(lime.withAlpha(0.35f));
        const float dashes[] = { 3.0f, 4.0f };
        g.drawDashedLine({ x0, pr.getY(), x0, pr.getBottom() }, dashes, 2, 1.0f);
        g.drawDashedLine({ x1, pr.getY(), x1, pr.getBottom() }, dashes, 2, 1.0f);
        g.setFont(caption(10.0f));
        g.drawText("AUTO RANGE", juce::Rectangle<float>(x0 + 4.0f, pr.getBottom() - 16.0f, 90.0f, 14.0f), juce::Justification::centredLeft);
    }

    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(plot);

    auto curvePath = [&](const std::vector<float>& c)
    {
        juce::Path p;
        const int n = int(c.size());
        for (int i = 0; i < n; ++i)
        {
            const float x = pr.getX() + pr.getWidth() * float(i) / float(juce::jmax(1, n - 1));
            const float y = ySpec(c[size_t(i)]);
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        return p;
    };

    // reference (kick / bass) - magenta area
    if (refCurve.size() > 1 && refPresent)
    {
        auto p = curvePath(refCurve);
        juce::Path area(p);
        area.lineTo(pr.getRight(), pr.getBottom() + 2.0f); area.lineTo(pr.getX(), pr.getBottom() + 2.0f); area.closeSubPath();
        juce::ColourGradient fill(magenta.withAlpha(0.30f), 0.0f, pr.getY(), magenta.withAlpha(0.03f), 0.0f, pr.getBottom(), false);
        g.setGradientFill(fill);
        g.fillPath(area);
        g.setColour(magenta.withAlpha(0.9f));
        g.strokePath(p, juce::PathStrokeType(1.3f, juce::PathStrokeType::beveled));
    }
    // AmbientLab output - lime line
    if (outCurve.size() > 1)
    {
        g.setColour(lime.withAlpha(0.85f));
        g.strokePath(curvePath(outCurve), juce::PathStrokeType(1.3f, juce::PathStrokeType::beveled));
    }

    // band widths + markers
    for (int b = 0; b < 4; ++b)
    {
        const float f = bandFreq[size_t(b)], q = juce::jmax(0.3f, bandQ[size_t(b)]);
        const float bwOct = 2.0f / std::log(2.0f) * std::asinh(1.0f / (2.0f * q));
        const float x0 = xOf(f * std::pow(2.0f, -bwOct * 0.5f)), x1 = xOf(f * std::pow(2.0f, bwOct * 0.5f));
        const bool sel = b == selected, on = bandOn[size_t(b)];
        if (on)
        {
            g.setColour(uv.withAlpha(sel ? 0.10f : 0.05f));
            g.fillRect(juce::Rectangle<float>(x0, pr.getY(), x1 - x0, pr.getHeight()));
        }
        g.setColour((on ? uvBright : textFaint).withAlpha(sel ? 0.8f : 0.4f));
        const float dashes[] = { 2.0f, 3.0f };
        g.drawDashedLine({ xOf(f), pr.getY(), xOf(f), pr.getBottom() }, dashes, 2, 1.0f);
    }

    // live reduction curve (exact bell responses from current band state)
    {
        const int n = juce::jmax(64, plot.getWidth() / 2);
        juce::Path gr, area;
        for (int i = 0; i < n; ++i)
        {
            const float x = pr.getX() + pr.getWidth() * float(i) / float(n - 1);
            const float y = yGr(reductionAt(fOfX(x)));
            if (i == 0) { gr.startNewSubPath(x, y); area.startNewSubPath(x, yGr(0.0f)); }
            else gr.lineTo(x, y);
            area.lineTo(x, y);
        }
        area.lineTo(pr.getRight(), yGr(0.0f));
        area.closeSubPath();
        g.setColour(uv.withAlpha(0.30f));
        g.fillPath(area);
        if (quality == GraphicsQuality::High)
        {
            g.setColour(uvBright.withAlpha(0.18f));
            g.strokePath(gr, juce::PathStrokeType(4.0f, juce::PathStrokeType::beveled));
        }
        g.setColour(uvBright);
        g.strokePath(gr, juce::PathStrokeType(1.8f, juce::PathStrokeType::beveled));
    }

    // band handles
    for (int b = 0; b < 4; ++b)
    {
        const float x = xOf(bandFreq[size_t(b)]);
        const float y = juce::jmax(yGr(-bandRed[size_t(b)]), pr.getY() + 10.0f);
        const bool sel = b == selected, on = bandOn[size_t(b)];
        auto c = juce::Rectangle<float>(16.0f, 16.0f).withCentre({ x, y });
        g.setColour(on ? (sel ? uv : bgRaised) : bgDeep);
        g.fillEllipse(c);
        g.setColour(on ? uvBright : textFaint);
        g.drawEllipse(c.reduced(0.5f), sel ? 1.6f : 1.0f);
        g.setFont(body(10.5f, Weight::SemiBold));
        g.setColour(on ? juce::Colours::white : textFaint);
        g.drawText(juce::String(b + 1), c, juce::Justification::centred);
        if (sel && on)
        {
            const auto lbl = fmtHz(bandFreq[size_t(b)]) + (bandRed[size_t(b)] > 0.05f ? "  -" + juce::String(bandRed[size_t(b)], 1) + " dB" : juce::String());
            const auto f = body(11.0f, Weight::Medium);
            const float w = textWidth(f, lbl) + 10.0f;
            auto box = juce::Rectangle<float>(w, 16.0f).withPosition(x + 11.0f, y - 8.0f);
            if (box.getRight() > pr.getRight()) box.setX(x - 11.0f - w);
            g.setColour(bgDeep.withAlpha(0.85f));
            g.fillRoundedRectangle(box, 3.0f);
            g.setColour(text);
            g.setFont(f);
            g.drawText(lbl, box, juce::Justification::centred);
        }
    }

    // legend
    g.setFont(caption(10.0f));
    auto lg = juce::Rectangle<float>(pr.getRight() - 330.0f, pr.getBottom() - 17.0f, 330.0f, 13.0f); // bottom-right: clear of band handles
    auto tag = [&](const juce::String& t, juce::Colour c)
    {
        g.setColour(c);
        g.fillRect(lg.removeFromLeft(10.0f).withSizeKeepingCentre(10.0f, 2.0f));
        const float w = textWidth(caption(10.0f), t) + 6.0f;
        g.drawText(t, lg.removeFromLeft(w).withTrimmedLeft(4.0f), juce::Justification::centredLeft);
        lg.removeFromLeft(10.0f);
    };
    tag("SIDECHAIN", magenta);
    tag("AMBIENTLAB OUT", lime);
    tag(fullBand ? "FULL-BAND REDUCTION" : "REDUCTION", uvBright);

    // no-signal guidance
    if (noSignalTime > 0.5f)
    {
        const auto msg = juce::String("No sidechain signal - route a Kick/Bass channel to the AmbientLab sidechain input");
        const auto f = body(12.0f);
        auto box = juce::Rectangle<float>(juce::jmin(pr.getWidth() - 20.0f, textWidth(f, msg) + 24.0f), 26.0f)
                       .withCentre({ pr.getCentreX(), pr.getCentreY() + 10.0f });
        g.setColour(bgDeep.withAlpha(0.85f));
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(box, 4.0f, 1.0f);
        g.setColour(textDim);
        g.setFont(f);
        g.drawFittedText(msg, box.reduced(8.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
}

// ============================================================================
// SidechainPanel
// ============================================================================
void SidechainPanel::GrMeter::set(float v, float dt)
{
    const float prev = db, prevHold = hold;
    db = v > db ? v : juce::jmax(v, db - dt * 30.0f);
    if (db >= hold) { hold = db; holdT = 1.2f; }
    else if ((holdT -= dt) <= 0.0f) hold = juce::jmax(db, hold - dt * 12.0f);
    if (std::abs(prev - db) > 0.02f || std::abs(prevHold - hold) > 0.02f) repaint();
}

void SidechainPanel::GrMeter::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto lbl = r.removeFromBottom(14.0f);
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(r, 2.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(r.reduced(0.5f), 2.0f, 1.0f);
    auto in = r.reduced(2.0f);
    const float h = in.getHeight() * juce::jlimit(0.0f, 1.0f, db / 24.0f); // grows downward from the top
    juce::ColourGradient grad(uv, 0.0f, in.getY(), magenta, 0.0f, in.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRect(in.withHeight(h));
    if (hold > 0.05f)
    {
        g.setColour(text);
        g.fillRect(in.getX(), in.getY() + in.getHeight() * juce::jlimit(0.0f, 1.0f, hold / 24.0f) - 0.75f, in.getWidth(), 1.5f);
    }
    g.setColour(textFaint);
    g.setFont(caption(9.5f));
    g.drawText("GR", lbl, juce::Justification::centred);
}

SidechainPanel::SidechainPanel(AmbientProcessor& p, UiSettings& s) : proc(p), settings(s), graph(p)
{
    addAndMakeVisible(graph);
    miniMascot = std::make_unique<Mascot>();
    addAndMakeVisible(*miniMascot);

    enableButton.setLitWhenOn(true);
    enableButton.setTooltip("Frequency sidechain on/off");
    enableAtt = std::make_unique<BA>(proc.apvts, id(scid::enable), enableButton);

    auto knob = [this](std::string_view pid, const juce::String& name, bool bipolar = false)
    {
        auto k = std::make_unique<RotaryKnob>(proc.apvts, juce::String(pid.data(), pid.size()), name, bipolar);
        k->setCompact(true);
        addAndMakeVisible(*k);
        globals.push_back(std::move(k));
    };
    knob(scid::inputGain, "Input", true);
    knob(scid::sensitivity, "Sensitivity");
    knob(scid::threshold, "Threshold", true);
    knob(scid::depth, "Depth");
    knob(scid::mix, "Mix");
    knob(scid::rangeLow, "Range lo");
    knob(scid::rangeHigh, "Range hi");

    std::pair<PillToggle*, std::string_view> toggles[] = { { &autoT, scid::automatic }, { &fullT, scid::fullBand },
                                                           { &listenT, scid::listen }, { &deltaT, scid::delta } };
    for (auto& [t, pid] : toggles)
    {
        addAndMakeVisible(*t);
        toggleAtts.push_back(std::make_unique<BA>(proc.apvts, juce::String(pid.data(), pid.size()), *t));
    }
    autoT.setTooltip("Auto: bands follow the detected Kick/Bass peaks inside the range. Dragging a band switches to manual.");
    fullT.setTooltip("Conventional full-band ducking (the whole signal follows the strongest band)");
    listenT.setTooltip("Monitor the sidechain reference");
    deltaT.setTooltip("Hear only what the sidechain removes");

    addAndMakeVisible(bandSel);
    bandSel.onChange = [this](int b) { selectBand(b); };
    graph.onBandSelected = [this](int b) { bandSel.setSelected(b, false); selectBand(b); };
    graph.setMascot(miniMascot.get());

    static const char* names[] = { "Freq", "Q", "Threshold", "Max cut", "Attack", "Release" };
    for (int b = 0; b < 4; ++b)
    {
        bandOn[size_t(b)] = std::make_unique<BypassButton>("Band " + juce::String(b + 1));
        bandOn[size_t(b)]->setLitWhenOn(true);
        bandOn[size_t(b)]->setTooltip("Band " + juce::String(b + 1) + " on/off");
        addChildComponent(*bandOn[size_t(b)]);
        bandOnAtts.push_back(std::make_unique<BA>(proc.apvts, bandParamId(b, scid::BEnable), *bandOn[size_t(b)]));
        for (int f = 1; f <= 6; ++f)
        {
            auto k = std::make_unique<RotaryKnob>(proc.apvts, bandParamId(b, f), names[f - 1]);
            k->setCompact(true);
            if (f == scid::BFreq) // turning a frequency takes manual control, like dragging in the graph
                k->getSlider().onDragStart = [this]
                {
                    if (auto* a = proc.apvts.getParameter(id(scid::automatic)))
                        if (a->getValue() > 0.5f) a->setValueNotifyingHost(0.0f);
                };
            addChildComponent(*k);
            bandKnobs[size_t(b)].push_back(std::move(k));
        }
    }
    addAndMakeVisible(scMeter);
    addAndMakeVisible(grMeter);

    selectBand(juce::jlimit(0, 3, int(settings.get("sc_band", 0))));
    bandSel.setSelected(selected, false);
}

SidechainPanel::~SidechainPanel() = default;

void SidechainPanel::selectBand(int b)
{
    selected = b;
    settings.set("sc_band", b);
    for (int i = 0; i < 4; ++i)
    {
        bandOn[size_t(i)]->setVisible(i == b);
        for (auto& k : bandKnobs[size_t(i)]) k->setVisible(i == b);
    }
    graph.setSelectedBand(b);
    resized();
}

void SidechainPanel::setQuality(GraphicsQuality q) { graph.setQuality(q); }

void SidechainPanel::tick(float dt, bool visible)
{
    const auto& t = proc.sidechainTelemetry();
    const bool enabled = proc.apvts.getRawParameterValue(id(scid::enable))->load() > 0.5f;
    miniMascot->tick(dt, t.transientCount.load(std::memory_order_relaxed), t.lastTransientStrength.load(std::memory_order_relaxed),
                     enabled && t.referencePresent.load(std::memory_order_relaxed), t.maxReductionDb.load(std::memory_order_relaxed));
    if (! visible) return;
    const float pk = t.referencePeak.load(std::memory_order_relaxed);
    scMeter.setLevels(pk, pk, dt);
    grMeter.set(t.maxReductionDb.load(std::memory_order_relaxed), dt);
    graph.tick(dt, true);
}

void SidechainPanel::resized()
{
    auto r = getLocalBounds();
    const int rowH = RotaryKnob::heightFor(34, true);
    rowB = r.removeFromBottom(rowH);
    r.removeFromBottom(6);
    rowA = r.removeFromBottom(rowH);
    r.removeFromBottom(8);
    graph.setBounds(r);
    miniMascot->setBounds(juce::Rectangle<int>(46, 54).withPosition(r.getRight() - 30 - 52, r.getY() + 6));
    miniMascot->setVisible(r.getHeight() >= 140); // never crowd a short graph

    // Row A: SC meter | global knobs | toggles 2x2 | GR meter
    {
        auto a = rowA;
        scMeter.setBounds(a.removeFromLeft(12).withTrimmedTop(14).withTrimmedBottom(14));
        a.removeFromLeft(6);
        grMeter.setBounds(a.removeFromRight(14));
        a.removeFromRight(8);
        auto tg = a.removeFromRight(juce::jmin(200, a.getWidth() / 3));
        const int th = 24;
        auto grid = tg.withSizeKeepingCentre(tg.getWidth(), th * 2 + 6);
        auto top = grid.removeFromTop(th); grid.removeFromTop(6);
        const int tw = (tg.getWidth() - 6) / 2;
        autoT.setBounds(top.removeFromLeft(tw)); top.removeFromLeft(6); fullT.setBounds(top);
        listenT.setBounds(grid.removeFromLeft(tw)); grid.removeFromLeft(6); deltaT.setBounds(grid);
        a.removeFromRight(8);
        const int kw = a.getWidth() / int(globals.size());
        for (auto& k : globals) k->setBounds(a.removeFromLeft(kw));
    }
    // Row B: band selector + on/off | band knobs
    {
        auto b = rowB;
        auto sel = b.removeFromLeft(juce::jmin(170, b.getWidth() / 4));
        auto selRow = sel.withSizeKeepingCentre(sel.getWidth(), 26);
        bandOn[size_t(selected)]->setBounds(selRow.removeFromRight(26));
        selRow.removeFromRight(6);
        bandSel.setBounds(selRow);
        b.removeFromLeft(10);
        const int kw = b.getWidth() / 6;
        for (auto& k : bandKnobs[size_t(selected)]) k->setBounds(b.removeFromLeft(kw));
    }
}

void SidechainPanel::paint(juce::Graphics& g)
{
    // hairline separating the global row from the band editor
    g.setColour(edge);
    g.drawHorizontalLine(rowB.getY() - 3, float(rowB.getX()), float(rowB.getRight()));
    g.setFont(caption(10.0f));
    g.setColour(textFaint);
    g.drawText("BAND", juce::Rectangle<int>(rowB.getX(), rowB.getY(), 60, 14), juce::Justification::topLeft);
}
} // namespace ambient::ui
