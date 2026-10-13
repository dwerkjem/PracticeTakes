#include "MusicFont.h"

#include <cstddef>

#include <PracticeTakesFonts.h>

namespace score::notation
{
namespace
{
constexpr const char* bravuraFamily = "Bravura";
} // namespace

const char* embeddedMusicFamilyName() noexcept
{
    return bravuraFamily;
}

juce::Typeface::Ptr musicTypeface()
{
    // Created once and shared. Parsing an 870 KB OpenType face on every repaint
    // would be absurd, and the face is immutable once built, so one copy serves
    // every score view and every instance of one.
    static const juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor(
        PracticeTakesFonts::Bravura_otf,
        static_cast<std::size_t>(PracticeTakesFonts::Bravura_otfSize));

    return face;
}
} // namespace score::notation
