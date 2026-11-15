#pragma once
// Direct modulation assignment (right-click on a knob). Reads and writes the SAME matrix
// parameters (mm.N.src / dst / amt) as the Modulation tab - there is no second engine.
// Message thread only. Changes go through host gestures (automation / undo aware).
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <vector>

namespace ambient::ui::modassign
{
struct Assignment { int slot; int source; int destination; float amount; };   // slot 1..32

int destinationOf(const juce::String& paramID);                                  // 0 = not modulatable
int macroIndexOf(const juce::String& paramID);                                   // 1..8, 0 = not a macro
std::vector<Assignment> forDestination(juce::AudioProcessorValueTreeState&, int destination);
std::vector<Assignment> forSource(juce::AudioProcessorValueTreeState&, int source);
int add(juce::AudioProcessorValueTreeState&, int source, int destination, float amount);   // slot or 0 if full
void setAmount(juce::AudioProcessorValueTreeState&, int slot, float amount);
void remove(juce::AudioProcessorValueTreeState&, int slot);

/** Modulation range around the current value (normalised offsets), for drawing. */
void rangeFor(juce::AudioProcessorValueTreeState&, int destination, float& lo, float& hi, bool& any);
/** Signature of the routing touching a destination / source (cheap change detection for repaint). */
int signature(juce::AudioProcessorValueTreeState&, int destination, int source);

/** Context menu for a knob (destination) or a macro knob (source). */
void showMenu(juce::AudioProcessorValueTreeState&, const juce::String& paramID, juce::Component& target);

/** Implemented by the editor; found from the clicked knob (works with several plugin instances). */
struct MatrixOpener { virtual ~MatrixOpener() = default; virtual void openModulationMatrix() = 0; };
} // namespace ambient::ui::modassign
