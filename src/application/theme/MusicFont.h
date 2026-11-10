#pragma once

#include <juce_graphics/juce_graphics.h>

#include <map>

#include "SmuflGlyphs.h"

// The music font notation is drawn with, as outlines in staff spaces.
//
// Bravura is embedded rather than looked up by name, for the reason IBM Plex
// is (see tools/packaging/fonts/README.md) and one more: a notation glyph has
// no fallback. A missing UI typeface draws text in the platform sans; a missing
// music font draws a Private Use Area codepoint as nothing at all.
//
// **The unit is the whole point.** SMuFL fixes a font's em at four staff
// spaces, so a glyph's size on a staff is a property of the font standard and
// not a choice -- and the layout already works in staff spaces. This class
// converts once, at load, from JUCE's normalised outline units to staff spaces,
// and nothing downstream ever sees a font size: the painter scales by the same
// pixels-per-space factor it scales the staff by, so zoom is still one number.
//
// Each glyph keeps SMuFL's own origin: on the baseline, at the left edge of
// the glyph's advance. For a notehead the baseline is the staff position the
// note sits on; for a clef it is the line the clef names; for a rest it is the
// line the rest is placed against. So the layout's existing positions are
// already the right anchor, and placing a glyph is one translation.
//
// Uses modular JUCE includes, never `JuceHeader.h` -- that umbrella is
// generated for the application target only, and this class is tested.
class MusicFont
{
  public:
    // Loads the embedded font and converts every glyph `smufl::allGlyphs`
    // names. Converting them all up front rather than on first use keeps
    // `outline` const and free of a cache that paint would mutate.
    MusicFont();

    // Whether the embedded data parsed. False would mean the binary-data
    // target was built without the font; MusicFontTests exists so that is a
    // failing test rather than a blank staff.
    [[nodiscard]] bool isLoaded() const;

    // The family the loaded face reports, so a test can tell "a font loaded"
    // from "the right font loaded".
    [[nodiscard]] juce::String familyName() const;

    // The glyph's outline in staff spaces, y growing downwards, origin at the
    // glyph's SMuFL origin. Empty when the font has no such glyph.
    [[nodiscard]] const juce::Path& outline(smufl::Glyph glyph) const;

    // The outline's bounds, in the same units. Empty for an absent glyph.
    [[nodiscard]] juce::Rectangle<float> bounds(smufl::Glyph glyph) const;

    // Whether the font drew anything for this glyph.
    [[nodiscard]] bool hasGlyph(smufl::Glyph glyph) const;

  private:
    juce::Typeface::Ptr typeface;
    std::map<smufl::Glyph, juce::Path> outlines;
    juce::Path empty;
};
