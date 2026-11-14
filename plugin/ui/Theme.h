#pragma once
// AMBIENTLAB visual identity - single source of truth for colours, type and metrics.
// Every component reads from here so new engine panels stay visually consistent.

#include <juce_gui_basics/juce_gui_basics.h>

namespace ambient::ui::theme
{
// ---- Surfaces (graphite / charcoal) ----------------------------------------
inline const juce::Colour bg        { 0xff121316 }; // window background
inline const juce::Colour bgDeep    { 0xff0c0d0f }; // screens / displays
inline const juce::Colour panel     { 0xff1d1f23 }; // module card body
inline const juce::Colour panelTop  { 0xff23262b }; // card gradient top
inline const juce::Colour panelHdr  { 0xff272a30 }; // card header strip
inline const juce::Colour edge      { 0xff2e3137 }; // borders
inline const juce::Colour edgeHi    { 0xff444851 }; // metallic highlight
inline const juce::Colour grid      { 0xff1f2329 }; // display grid lines

// ---- Text --------------------------------------------------------------------
inline const juce::Colour text      { 0xffdcdde1 };
inline const juce::Colour textDim   { 0xff8b8f98 };
inline const juce::Colour textFaint { 0xff5a5e66 };

// ---- Accents (from the Danisan logo) -----------------------------------------
inline const juce::Colour purple     { 0xffa45cf0 }; // active state, values
inline const juce::Colour purpleDeep { 0xff5b2a8c };
inline const juce::Colour lime       { 0xffb8f246 }; // signal activity, modulation
inline const juce::Colour limeDeep   { 0xff6f9a1e };
inline const juce::Colour warn       { 0xfff2a33a };
inline const juce::Colour clip       { 0xffef4b4b };

// ---- Metrics (at 100% scale; multiply by the editor scale) ---------------------
inline constexpr float cornerRadius = 5.0f;
inline constexpr int   headerHeight = 58;
inline constexpr int   tabHeight    = 34;
inline constexpr int   footerHeight = 104;
inline constexpr int   cardHeader   = 26;
inline constexpr int   gap          = 8;

inline juce::Font font(float size, bool bold = false)
{
    auto f = juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
    return f;
}

/** Small-caps style labels: uppercase, tracked. */
inline juce::Font labelFont(float size)
{
    return font(size, true).withExtraKerningFactor(0.09f);
}
} // namespace ambient::ui::theme
