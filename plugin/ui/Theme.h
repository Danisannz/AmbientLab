#pragma once
// AMBIENTLAB visual identity - "cyberpunk psychedelic / underground modular".
// Single source of truth for colour, type and metrics: every component reads
// from here so future engine panels inherit the same language.
//
// Colour roles:
//   ultraviolet  -> active state, selection, parameter values
//   lime / acid  -> signal activity, audio flow, modulation
//   magenta      -> warnings, phase problems, peak-hold, sparse accents
//   graphite     -> everything else

#include "Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ambient::ui::theme
{
// ---- Surfaces ------------------------------------------------------------------
inline const juce::Colour bg        { 0xff0a0a0d }; // near-black
inline const juce::Colour bgRaised  { 0xff111116 }; // graphite
inline const juce::Colour bgDeep    { 0xff060608 }; // analyzer screens
inline const juce::Colour panel     { 0xff141419 }; // card body (bottom)
inline const juce::Colour panelTop  { 0xff1a1a21 }; // card body (top)
inline const juce::Colour panelHdr  { 0xff1d1d25 }; // card header strip
inline const juce::Colour edge      { 0xff292932 }; // borders
inline const juce::Colour edgeHi    { 0xff4b4b58 }; // metallic highlight
inline const juce::Colour metal     { 0xff8d8c99 }; // fine metallic details
inline const juce::Colour grid      { 0xff1b1b23 }; // display grid
inline const juce::Colour gridHi    { 0xff2a2a35 }; // major grid lines

// ---- Text ------------------------------------------------------------------------
inline const juce::Colour text      { 0xffe6e4ec };
inline const juce::Colour textDim   { 0xff9b98a8 };
inline const juce::Colour textFaint { 0xff605d6e };

// ---- Accents ---------------------------------------------------------------------
inline const juce::Colour uv        { 0xff9145ff }; // ultraviolet (active)
inline const juce::Colour uvBright  { 0xffb98aff };
inline const juce::Colour uvDeep    { 0xff35176b };
inline const juce::Colour magenta   { 0xffd62a9d }; // deep magenta
inline const juce::Colour magentaDeep { 0xff5c0f45 };
inline const juce::Colour acid      { 0xff8cff2e }; // acid green
inline const juce::Colour lime      { 0xffc7f53e }; // lime (signal)
inline const juce::Colour limeDeep  { 0xff4f6e12 };
inline const juce::Colour warn      { 0xfff2a33a };
inline const juce::Colour clip      { 0xffff3b5c };

// Back-compat aliases used by earlier code
inline const juce::Colour purple     = uv;
inline const juce::Colour purpleDeep = uvDeep;

// ---- Metrics (logical pixels; the editor scales the whole layout) ---------------------
inline constexpr float cornerRadius = 4.0f;
inline constexpr int   headerHeight = 80;
inline constexpr int   tabHeight    = 36;
inline constexpr int   footerHeight = 118;
inline constexpr int   cardHeader   = 28;
inline constexpr int   gap          = 8;

// ---- Type --------------------------------------------------------------------------
enum class Weight { Regular, Medium, SemiBold };

/** Space Grotesk - headings, titles, tabs, wordmark. */
inline juce::Font heading(float size, bool bold = false, float tracking = 0.0f)
{
    const auto& f = Fonts::get();
    return juce::Font(juce::FontOptions().withTypeface(bold ? f.groteskBold : f.groteskMedium)
                                         .withHeight(size).withKerningFactor(tracking));
}

/** IBM Plex Sans - labels, values, menus, status. */
inline juce::Font body(float size, Weight w = Weight::Regular, float tracking = 0.0f)
{
    const auto& f = Fonts::get();
    auto tf = w == Weight::SemiBold ? f.plexSemiBold : (w == Weight::Medium ? f.plexMedium : f.plexRegular);
    return juce::Font(juce::FontOptions().withTypeface(tf).withHeight(size).withKerningFactor(tracking));
}

/** Small technical caption (uppercase, light tracking). Never below 10 px. */
inline juce::Font caption(float size = 10.5f)
{
    return body(juce::jmax(10.0f, size), Weight::Medium, 0.06f);
}

// Back-compat helpers used by earlier code
inline juce::Font font(float size, bool bold = false) { return body(size, bold ? Weight::SemiBold : Weight::Regular); }
inline juce::Font labelFont(float size) { return caption(size); }

inline float textWidth(const juce::Font& f, const juce::String& s)
{
    return juce::GlyphArrangement::getStringWidth(f, s);
}
} // namespace ambient::ui::theme
