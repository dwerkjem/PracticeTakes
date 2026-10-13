#include <algorithm>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include "platform/score/engraving/ScoreLayout.h"
#include "platform/score/musicxml/MusicXmlImporter.h"

// The grand staff, from a real file rather than a hand-built score.
//
// The layout tests construct their own two-staff part, which proves the layout
// does the right thing with the input it is given and proves nothing about
// whether a real MusicXML piano part arrives in that shape. This closes that
// gap: import `clean-import.musicxml`, which declares <staves>2</staves> with a
// G clef on staff 1 and an F clef on staff 2, and check what comes out the far
// end.
//
// It is also the only *automated* thing that looks at the grand staff end to
// end. The tool's paint routine is outside this target by design, and what the
// page looks like is now verified by a human against the `score-view-page`
// capture -- which is a reference image and a review question, not a test that
// fails on its own. So this stays the thing that fails a build.
using namespace score;
using namespace score::engraving;
using namespace score::musicxml;

namespace
{
[[nodiscard]] juce::File fixture()
{
    return juce::File(PRACTICE_TAKES_TEST_RESOURCES_DIR)
        .getChildFile("harness-scores")
        .getChildFile("clean-import.musicxml");
}

[[nodiscard]] std::vector<double> noteheadXs(const StaffLayout& staff)
{
    std::vector<double> xs;

    for (const PlacedGlyph& glyph : staff.glyphs)
    {
        if (glyph.glyph == Glyph::noteheadBlack || glyph.glyph == Glyph::noteheadHalf ||
            glyph.glyph == Glyph::noteheadWhole)
        {
            xs.push_back(glyph.x);
        }
    }

    return xs;
}

[[nodiscard]] bool hasGlyph(const StaffLayout& staff, Glyph wanted)
{
    return std::any_of(
        staff.glyphs.begin(), staff.glyphs.end(),
        [wanted](const PlacedGlyph& glyph) { return glyph.glyph == wanted; });
}

// The piano part of the fixture: the one that declares two staves.
[[nodiscard]] const PartLayout* pianoPart(const ScoreLayout& layout, const Score& imported)
{
    if (layout.systems.empty())
    {
        return nullptr;
    }

    for (const PartLayout& part : layout.systems.front().parts)
    {
        if (part.partIndex < imported.parts.size() && imported.parts[part.partIndex].staffCount > 1)
        {
            return &part;
        }
    }

    return nullptr;
}
} // namespace

TEST_CASE("a real two-staff part imports as a grand staff", "[engraving][layout][grandstaff]")
{
    REQUIRE(fixture().existsAsFile());

    const MusicXmlImportResult result = importMusicXmlFile(fixture());

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score != nullptr);

    // The file says two staves. If the importer ever stopped carrying that, the
    // renderer would draw one staff and this is where it would be noticed --
    // which is the failure "it only shows one staff" would look like.
    const auto twoStaffPart = std::find_if(
        result.score->parts.begin(), result.score->parts.end(),
        [](const Part& part) { return part.staffCount == 2; });

    REQUIRE(twoStaffPart != result.score->parts.end());

    LayoutOptions options;
    options.availableWidth = 200.0;

    const ScoreLayout layout = layOutScore(*result.score, options);

    REQUIRE_FALSE(layout.systems.empty());

    const PartLayout* piano = pianoPart(layout, *result.score);

    REQUIRE(piano != nullptr);

    // Two staves, braced, with room between them.
    CHECK(piano->staves.size() == 2);
    CHECK(piano->braced);
    CHECK(piano->braceBottom > piano->braceTop);
    CHECK(piano->staves[1].topOffset > piano->staves[0].topOffset);
}

TEST_CASE("a real grand staff reads in two different clefs", "[engraving][layout][grandstaff]")
{
    const MusicXmlImportResult result = importMusicXmlFile(fixture());

    REQUIRE(succeeded(result.status));

    LayoutOptions options;
    options.availableWidth = 200.0;

    const ScoreLayout layout = layOutScore(*result.score, options);
    const PartLayout* piano = pianoPart(layout, *result.score);

    REQUIRE(piano != nullptr);
    REQUIRE(piano->staves.size() == 2);

    // The file puts a G clef on staff 1 and an F clef on staff 2. Drawing both
    // staves in the same clef is the other way a grand staff goes wrong.
    CHECK(hasGlyph(piano->staves[0], Glyph::gClef));
    CHECK(hasGlyph(piano->staves[1], Glyph::fClef));
}

TEST_CASE("both staves of a real grand staff carry music", "[engraving][layout][grandstaff]")
{
    const MusicXmlImportResult result = importMusicXmlFile(fixture());

    REQUIRE(succeeded(result.status));

    LayoutOptions options;
    options.availableWidth = 200.0;

    const ScoreLayout layout = layOutScore(*result.score, options);

    // Across every system, both staves of the piano part have noteheads. One
    // empty staff is exactly the symptom of events being placed by pitch or by
    // voice instead of by ScoreEvent::staff.
    std::size_t trebleNotes = 0;
    std::size_t bassNotes = 0;

    for (const SystemLayout& system : layout.systems)
    {
        for (const PartLayout& part : system.parts)
        {
            if (part.staves.size() < 2)
            {
                continue;
            }

            trebleNotes += noteheadXs(part.staves[0]).size();
            bassNotes += noteheadXs(part.staves[1]).size();
        }
    }

    CHECK(trebleNotes > 0);
    CHECK(bassNotes > 0);
}
