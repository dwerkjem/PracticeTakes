#pragma once

#include <juce_graphics/juce_graphics.h>

// The embedded music typeface.
//
// Separate from `AppLookAndFeel`, which owns the *UI* typeface, because these
// two faces answer to different things: the interface font is a design choice
// that a theme could reasonably change, and the music font is tied to the
// codepoints in `src/platform/score/engraving/GlyphMetrics.h`. Swapping the
// music face means regenerating that table; swapping the UI face means nothing
// of the sort.
//
// The engraving layer never sees this. It works in staff spaces against the
// metrics table and has no idea a font exists -- that separation is what keeps
// layout testable without a display. This is the paint side.
namespace score::notation
{
// The family name the embedded face is expected to report.
//
// Checked rather than assumed: if the binary data does not reach the target or
// the bytes do not parse, JUCE hands back a fallback face, nothing throws, and
// the first symptom is a page of empty boxes. That is the same quiet failure
// `AppLookAndFeelTests` guards the UI typeface against, and notation has no
// graceful degradation at all -- there is no platform music font to fall back
// to.
[[nodiscard]] const char* embeddedMusicFamilyName() noexcept;

// The parsed Bravura face, created once and shared. Null only if the embedded
// bytes failed to parse, which the tests are there to catch first.
[[nodiscard]] juce::Typeface::Ptr musicTypeface();
} // namespace score::notation
