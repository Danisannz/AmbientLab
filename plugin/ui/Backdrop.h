#pragma once
// Generative "cyberpunk psychedelic" background artwork.
//
// * Window: a receding tunnel of warped contour rings (ultraviolet -> magenta),
//   radial ribs and fine digital grain. Very dim: it only shows in margins/gaps.
// * Header: flowing interference contour lines with a few lime signal lines,
//   circuit traces and a luminous lower edge. Brighter, but kept away from text.
//
// Artwork is deterministic (seeded), rendered with vector drawing into a cached
// image at the *physical* pixel resolution of the target context (High quality),
// or at 1x (Standard). It is re-rendered only when size, scale or quality change,
// so per-frame cost is a single image blit.

#include "Theme.h"
#include "UiSettings.h"

namespace ambient::ui
{
class Backdrop
{
public:
    enum class Region { Window, Header };

    /** Draws the cached artwork for `region` into `area` (logical coordinates).
        `focus` = optional point of interest (header: logo centre). */
    void draw(juce::Graphics&, Region, juce::Rectangle<int> area, GraphicsQuality,
              juce::Point<float> focus = {});

    /** Pure render functions (also used by tests / snapshot tooling). */
    static void renderWindow(juce::Graphics&, float w, float h, GraphicsQuality);
    static void renderHeader(juce::Graphics&, float w, float h, juce::Point<float> focus, GraphicsQuality);

private:
    struct Cache { juce::Image image; int w = 0, h = 0; float scale = 0.0f; int quality = -1; juce::Point<float> focus; };
    Cache window, header;
};
} // namespace ambient::ui
