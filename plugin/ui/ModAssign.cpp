#include "ModAssign.h"
#include "Theme.h"
#include "Fonts.h"
#include "../ModDestinations.h"

namespace ambient::ui::modassign
{

namespace
{
constexpr int kSlots = 32;
juce::String pid(int slot, const char* f) { return "mm." + juce::String(slot) + "." + f; }
float raw(juce::AudioProcessorValueTreeState& s, const juce::String& id) { auto* a = s.getRawParameterValue(id); return a ? a->load() : 0.0f; }
void setPlain(juce::AudioProcessorValueTreeState& s, const juce::String& id, float plain)
{
    if (auto* p = s.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(plain));
        p->endChangeGesture();
    }
}
Assignment read(juce::AudioProcessorValueTreeState& s, int slot)
{
    return { slot, juce::roundToInt(raw(s, pid(slot, "src"))), juce::roundToInt(raw(s, pid(slot, "dst"))), raw(s, pid(slot, "amt")) };
}
juce::String pct(float a) { return (a > 0 ? "+" : "") + juce::String(juce::roundToInt(a * 100.0f)) + " %"; }

/** Popup row: source/destination name + live bipolar amount slider (+ invert / remove). */
struct AmountRow : juce::PopupMenu::CustomComponent
{
    AmountRow(juce::AudioProcessorValueTreeState& st, int slotIn, const juce::String& labelText)
        : juce::PopupMenu::CustomComponent(false), s(st), slot(slotIn), label(labelText)
    {
        slider.setSliderStyle(juce::Slider::LinearBar);
        slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 0, 0);
        slider.textFromValueFunction = [](double v) { return pct(float(v)); };
        slider.setColour(juce::Slider::trackColourId, theme::uv.withAlpha(0.6f));
        slider.setDoubleClickReturnValue(true, 0.0);
        addAndMakeVisible(slider);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(s, pid(slot, "amt"), slider);
        for (auto* b : { &invert, &del }) { addAndMakeVisible(*b); b->setTriggeredOnMouseDown(false); }
        invert.setTooltip("Invert polarity");
        del.setTooltip("Remove this assignment");
        invert.onClick = [this] { setAmount(s, slot, -raw(s, pid(slot, "amt"))); };
        del.onClick = [this]
        {
            remove(s, slot);
            triggerMenuItem();   // closes the menu
        };
    }
    void getIdealSize(int& w, int& h) override { w = 320; h = 26; }
    void resized() override
    {
        auto r = getLocalBounds().reduced(6, 3);
        r.removeFromLeft(110);
        del.setBounds(r.removeFromRight(24));
        r.removeFromRight(2);
        invert.setBounds(r.removeFromRight(30));
        r.removeFromRight(4);
        slider.setBounds(r);
    }
    void paint(juce::Graphics& g) override
    {
        g.setColour(theme::text);
        g.setFont(theme::body(12.5f));
        g.drawText(label, getLocalBounds().reduced(8, 0).withWidth(108), juce::Justification::centredLeft, true);
    }
    juce::AudioProcessorValueTreeState& s;
    int slot;
    juce::String label;
    juce::Slider slider;
    juce::TextButton invert { "+/-" }, del { "x" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

/** Destination submenu grouped by module (used from macro knobs). */
juce::PopupMenu destinationMenu(int base)
{
    struct Group { const char* prefix; const char* title; };
    static constexpr Group groups[] = { { "osc.", "Oscillators" }, { "flt.", "Filter" }, { "env.", "Envelopes" }, { "gr.", "Granular" },
                                        { "dl.", "Delay" }, { "rv.", "Reverb" }, { "sc.", "Sidechain" }, { "out.", "Stereo / output" },
                                        { "route.", "Routing" }, { "lfo.", "LFOs" }, { "seq.", "Sequencer" }, { "global.", "Global" } };
    juce::PopupMenu m;
    for (const auto& gr : groups)
    {
        juce::PopupMenu sub;
        for (int k = 0; k < ambient::moddest::kNumDestinations; ++k)
            if (ambient::moddest::kDestinations[size_t(k)].substr(0, std::strlen(gr.prefix)) == gr.prefix)
                sub.addItem(base + k + 1, ambient::moddest::kDestinationNames[size_t(k)]);
        if (sub.getNumItems() > 0) m.addSubMenu(gr.title, sub);
    }
    return m;
}

juce::PopupMenu sourceMenu(int base)
{
    using namespace ambient::modsrc;
    struct Group { const char* title; int first, last; };
    static constexpr Group groups[] = { { "LFOs", Lfo1, Lfo4 }, { "Macros", Macro1, Macro8 },
                                        { "MIDI / note", Velocity, NoteRandom },
                                        { "Frequency sidechain", ScEnvelope, ScGainReduction }, { "Sequencer", SeqGate, SeqStep } };
    juce::PopupMenu m;
    for (const auto& gr : groups)
    {
        juce::PopupMenu sub;
        for (int k = gr.first; k <= gr.last; ++k) sub.addItem(base + k, kNames[size_t(k)]);
        m.addSubMenu(gr.title, sub);
    }
    return m;
}
} // namespace

int destinationOf(const juce::String& paramID) { return ambient::moddest::indexOf(paramID.toStdString()); }

int macroIndexOf(const juce::String& paramID)
{
    if (! paramID.startsWith("macro.")) return 0;
    const int i = paramID.fromFirstOccurrenceOf("macro.", false, false).getIntValue();
    return (i >= 1 && i <= 8) ? i : 0;
}

std::vector<Assignment> forDestination(juce::AudioProcessorValueTreeState& s, int destination)
{
    std::vector<Assignment> v;
    if (destination <= 0) return v;
    for (int m = 1; m <= kSlots; ++m) { const auto a = read(s, m); if (a.source > 0 && a.destination == destination) v.push_back(a); }
    return v;
}

std::vector<Assignment> forSource(juce::AudioProcessorValueTreeState& s, int source)
{
    std::vector<Assignment> v;
    for (int m = 1; m <= kSlots; ++m) { const auto a = read(s, m); if (a.source == source && a.destination > 0) v.push_back(a); }
    return v;
}

int add(juce::AudioProcessorValueTreeState& s, int source, int destination, float amount)
{
    for (int m = 1; m <= kSlots; ++m)
    {
        const auto a = read(s, m);
        if (a.source == source && a.destination == destination) return m;      // already routed: reuse
    }
    for (int m = 1; m <= kSlots; ++m)
    {
        const auto a = read(s, m);
        if (a.source == 0 || a.destination == 0)
        {
            setPlain(s, pid(m, "src"), float(source));
            setPlain(s, pid(m, "dst"), float(destination));
            setPlain(s, pid(m, "amt"), amount);
            return m;
        }
    }
    return 0;
}

void setAmount(juce::AudioProcessorValueTreeState& s, int slot, float amount) { setPlain(s, pid(slot, "amt"), juce::jlimit(-1.0f, 1.0f, amount)); }

void remove(juce::AudioProcessorValueTreeState& s, int slot)
{
    setPlain(s, pid(slot, "amt"), 0.0f);
    setPlain(s, pid(slot, "dst"), 0.0f);
    setPlain(s, pid(slot, "src"), 0.0f);
}

void rangeFor(juce::AudioProcessorValueTreeState& s, int destination, float& lo, float& hi, bool& any)
{
    lo = hi = 0.0f; any = false;
    for (const auto& a : forDestination(s, destination))
    {
        if (a.amount == 0.0f) continue;
        any = true;
        if (ambient::modsrc::isBipolar(a.source)) { lo -= std::abs(a.amount); hi += std::abs(a.amount); }
        else if (a.amount > 0) hi += a.amount;
        else lo += a.amount;
    }
}

int signature(juce::AudioProcessorValueTreeState& s, int destination, int source)
{
    int sig = 17;
    for (int m = 1; m <= kSlots; ++m)
    {
        const auto a = read(s, m);
        if ((destination > 0 && a.destination == destination) || (source > 0 && a.source == source))
            sig = sig * 31 + m * 7919 + a.source * 131 + a.destination * 17 + juce::roundToInt(a.amount * 1000.0f);
    }
    return sig;
}

void showMenu(juce::AudioProcessorValueTreeState& s, const juce::String& paramID, juce::Component& target)
{
    using namespace ambient::modsrc;
    const int dest = destinationOf(paramID);
    const int macro = macroIndexOf(paramID);
    if (dest <= 0 && macro <= 0) return;                       // not modulatable: no menu
    juce::PopupMenu m;
    const auto* prm = s.getParameter(paramID);
    const juce::String title = prm ? prm->getName(64) : paramID;
    constexpr int kAddBase = 1000, kOpen = 1, kClear = 2;

    if (macro > 0)
    {
        const int src = Macro1 + macro - 1;
        const auto list = forSource(s, src);
        m.addSectionHeader(title + " controls" + (list.empty() ? " nothing yet" : ":"));
        for (const auto& a : list)
            m.addCustomItem(-1, std::make_unique<AmountRow>(s, a.slot, ambient::moddest::kDestinationNames[size_t(a.destination - 1)]), nullptr, "");
        m.addSeparator();
        m.addSubMenu("Assign " + title + " to...", destinationMenu(kAddBase));
        if (! list.empty()) m.addItem(kClear, "Remove all " + title + " assignments");
    }
    else
    {
        const auto list = forDestination(s, dest);
        m.addSectionHeader("Modulate " + title);
        for (const auto& a : list)
            m.addCustomItem(-1, std::make_unique<AmountRow>(s, a.slot, kNames[size_t(a.source)]), nullptr, "");
        if (! list.empty()) m.addSeparator();
        m.addSubMenu("Add modulation source", sourceMenu(kAddBase));
        if (! list.empty()) m.addItem(kClear, "Remove all modulation of " + title);
    }
    m.addSeparator();
    auto* opener = target.findParentComponentOfClass<MatrixOpener>();
    m.addItem(kOpen, "Open modulation matrix...", opener != nullptr);

    juce::Component::SafePointer<juce::Component> safe(&target);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target).withMinimumWidth(320),
        [&s, dest, macro, safe](int r)
        {
            const int src = macro > 0 ? Macro1 + macro - 1 : 0;
            if (r == kOpen && safe != nullptr)
            {
                if (auto* o = safe->findParentComponentOfClass<MatrixOpener>()) o->openModulationMatrix();
            }
            else if (r == kClear)
                for (const auto& a : macro > 0 ? forSource(s, src) : forDestination(s, dest)) remove(s, a.slot);
            else if (r > kAddBase)
            {
                const int picked = r - kAddBase;
                const int slot = macro > 0 ? add(s, src, picked, 0.5f) : add(s, picked, dest, 0.3f);
                if (slot == 0)
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Modulation matrix full",
                        "All 32 matrix slots are in use. Remove an assignment in the Modulation tab first.");
            }
            if (safe != nullptr) safe->repaint();
        });
}
} // namespace ambient::ui::modassign
