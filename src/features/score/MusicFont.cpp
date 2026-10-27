#include "MusicFont.h"

#include <PracticeTakesFonts.h>

#include <cmath>
#include <cstdint>
#include <optional>

namespace
{
// SMuFL's fixed relation between a font and a staff: one em is four staff
// spaces, whatever the font. It is what makes a music font interchangeable
// at all.
constexpr float staffSpacesPerEm = 4.0f;
} // namespace

MusicFont::MusicFont()
    : typeface(juce::Typeface::createSystemTypefaceFor(
          PracticeTakesFonts::Bravura_otf,
          static_cast<std::size_t>(PracticeTakesFonts::Bravura_otfSize)))
{
    if (typeface == nullptr)
    {
        return;
    }

    // JUCE normalises an outline so that the face's ascent plus descent is
    // 1.0, and `heightToPoints` is the factor from that unit to an em. Which
    // metrics kind is asked for does not matter here: the same kind supplies
    // both the outline and the factor, so it cancels. Portable is chosen
    // because it is the one JUCE promises is the same on every platform.
    const float heightToPoints =
        typeface->getMetrics(juce::TypefaceMetricsKind::portable).heightToPoints;

    if (!(heightToPoints > 0.0f) || !std::isfinite(heightToPoints))
    {
        return;
    }

    const float toStaffSpaces = staffSpacesPerEm / heightToPoints;

    for (const smufl::Glyph glyph : smufl::allGlyphs())
    {
        const std::optional<std::uint32_t> index = typeface->getNominalGlyphForCodepoint(
            static_cast<juce::juce_wchar>(smufl::codepointOf(glyph)));

        if (!index.has_value())
        {
            continue;
        }

        juce::Path path;
        typeface->getOutlineForGlyph(
            juce::TypefaceMetricsKind::portable, static_cast<int>(*index), path);
        path.applyTransform(juce::AffineTransform::scale(toStaffSpaces));

        if (!path.isEmpty())
        {
            outlines.emplace(glyph, std::move(path));
        }
    }
}

bool MusicFont::isLoaded() const
{
    return typeface != nullptr && !outlines.empty();
}

juce::String MusicFont::familyName() const
{
    return typeface == nullptr ? juce::String{} : typeface->getName();
}

const juce::Path& MusicFont::outline(smufl::Glyph glyph) const
{
    const auto found = outlines.find(glyph);

    return found == outlines.end() ? empty : found->second;
}

juce::Rectangle<float> MusicFont::bounds(smufl::Glyph glyph) const
{
    return outline(glyph).getBounds();
}

bool MusicFont::hasGlyph(smufl::Glyph glyph) const
{
    return outlines.find(glyph) != outlines.end();
}
