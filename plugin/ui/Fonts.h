#pragma once
// Embedded typefaces (SIL Open Font License 1.1 - see plugin/assets/fonts/OFL-*.txt).
//   Space Grotesk  - display: wordmark, module titles, section headings, tabs.
//   IBM Plex Sans  - text: parameter labels, values, menus, status, information.
// Loaded once from BinaryData; never depends on fonts installed on the system.

#include <juce_graphics/juce_graphics.h>

namespace ambient::ui
{
struct Fonts
{
    static const Fonts& get();

    juce::Typeface::Ptr groteskMedium, groteskBold;
    juce::Typeface::Ptr plexRegular, plexMedium, plexSemiBold;

private:
    Fonts();
};
} // namespace ambient::ui
