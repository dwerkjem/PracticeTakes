#include <catch2/catch_test_macros.hpp>

#include "application/theme/AppLookAndFeel.h"

// The embedded UI typeface has exactly one interesting failure mode, and it is
// a quiet one.
//
// If the binary data does not reach the target, or the bytes do not parse,
// getTypefaceForFont falls back to the platform sans. Nothing throws, nothing
// logs, every test still passes, and the application looks fine -- just not
// like the design. The symptom surfaces weeks later as "the screenshots do not
// match", which is a long way from the cause.
//
// So these check the family name rather than merely that something non-null
// came back. A fallback returns a typeface too.

namespace
{
// Creating a typeface spins up JUCE's font subsystem -- the FreeType library
// wrapper and a list of every face on the machine. Without a runtime to own
// them those singletons are still standing at exit, and JUCE says so: a wall of
// "Leaked objects detected: 2492 instance(s) of class KnownTypeface" that the
// tests pass straight through. Noise a reader learns to skip is worse than
// silence, and LeakSanitizer has to look at it too.
//
// Same shape as ServiceUnderTest in the audio callback tests, for the same
// reason: the runtime is declared first, so it outlives what it owns.
struct LookAndFeelUnderTest
{
    juce::ScopedJuceInitialiser_GUI juceRuntime;
    AppLookAndFeel lookAndFeel;
};
} // namespace

TEST_CASE("the embedded regular typeface is the one we shipped", "[theme][typeface]")
{
    const LookAndFeelUnderTest fixture;
    const auto typeface = fixture.lookAndFeel.regularTypeface();

    REQUIRE(typeface != nullptr);
    CHECK(typeface->getName() == juce::String(AppLookAndFeel::embeddedFamilyName()));
}

TEST_CASE("the embedded bold typeface is the one we shipped", "[theme][typeface]")
{
    const LookAndFeelUnderTest fixture;
    const auto typeface = fixture.lookAndFeel.boldTypeface();

    REQUIRE(typeface != nullptr);
    CHECK(typeface->getName() == juce::String(AppLookAndFeel::embeddedFamilyName()));
}

TEST_CASE("a bold font resolves to a different face than a plain one", "[theme][typeface]")
{
    // Guards the weight mapping itself. Returning the regular face for
    // everything would still pass both checks above, and would look like a
    // font that simply has no bold -- which is what a synthesised faux-bold
    // looks like too.
    LookAndFeelUnderTest fixture;

    const auto plain = fixture.lookAndFeel.getTypefaceForFont(juce::Font(juce::FontOptions(14.0f)));
    const auto bold = fixture.lookAndFeel.getTypefaceForFont(
        juce::Font(juce::FontOptions(14.0f).withStyle("Bold")));

    REQUIRE(plain != nullptr);
    REQUIRE(bold != nullptr);
    CHECK(plain != bold);
}

TEST_CASE("the same face is handed out on every call", "[theme][typeface]")
{
    // getTypefaceForFont runs on every text draw. Parsing 200 KB of font data
    // per call would be a per-paint cost nobody would think to look for, so the
    // faces are built once in the constructor.
    LookAndFeelUnderTest fixture;
    const juce::Font font(juce::FontOptions(14.0f));

    CHECK(
        fixture.lookAndFeel.getTypefaceForFont(font) ==
        fixture.lookAndFeel.getTypefaceForFont(font));
}
