#include "Fonts.h"
#include <BinaryData.h>

namespace ambient::ui
{
Fonts::Fonts()
{
    using T = juce::Typeface;
    groteskMedium = T::createSystemTypefaceFor(BinaryData::SpaceGroteskMedium_ttf, BinaryData::SpaceGroteskMedium_ttfSize);
    groteskBold   = T::createSystemTypefaceFor(BinaryData::SpaceGroteskBold_ttf,   BinaryData::SpaceGroteskBold_ttfSize);
    plexRegular   = T::createSystemTypefaceFor(BinaryData::IBMPlexSansRegular_ttf, BinaryData::IBMPlexSansRegular_ttfSize);
    plexMedium    = T::createSystemTypefaceFor(BinaryData::IBMPlexSansMedium_ttf,  BinaryData::IBMPlexSansMedium_ttfSize);
    plexSemiBold  = T::createSystemTypefaceFor(BinaryData::IBMPlexSansSemiBold_ttf, BinaryData::IBMPlexSansSemiBold_ttfSize);
}

const Fonts& Fonts::get()
{
    static const Fonts instance; // created on first use (message thread)
    return instance;
}
} // namespace ambient::ui
