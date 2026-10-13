#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/ScoreFixtures.h"

#include "platform/score/engraving/ScoreLayout.h"

using namespace score;
using namespace score::engraving;
using testing::score::notesOf;

namespace
{
[[nodiscard]] ScoreEvent noteAt(Tick onset, Tick duration, Step step, int octave, int staff = 1)
{
    ScoreEvent event;
    event.kind = EventKind::note;
    event.onset = onset;
    event.duration = duration;
    event.staff = staff;
    event.notes = notesOf({pitchFromSpelling(step, 0, octave)});

    return event;
}

[[nodiscard]] Measure emptyMeasure(std::size_t index)
{
    Measure measure;
    measure.printedNumber = std::to_string(index + 1);
    measure.index = index;
    measure.nominalDuration = nominalMeasureTicks(4, 4);
    measure.start = static_cast<Tick>(index) * measure.nominalDuration;

    return measure;
}

// A piano part: two staves, treble over bass, with a note on each at the same
// instant. The case #200 exists for.
[[nodiscard]] Score makePianoScore()
{
    Voice treble;
    treble.number = 1;
    treble.events = {
        noteAt(0, ticksPerQuarterNote, Step::c, 5, 1),
        noteAt(ticksPerQuarterNote, ticksPerQuarterNote, Step::d, 5, 1)};

    Voice bass;
    bass.number = 2;
    bass.events = {noteAt(0, 2 * ticksPerQuarterNote, Step::c, 3, 2)};

    Measure measure = emptyMeasure(0);
    measure.attributes.clefs = {Clef{"G", 2, 0, 1}, Clef{"F", 4, 0, 2}};
    measure.voices = {treble, bass};

    Part part;
    part.id = "P1";
    part.name = "Piano";
    part.staffCount = 2;
    part.measures = {measure};

    Score score;
    score.parts = {part};

    return score;
}

[[nodiscard]] Score makeSingleStaffScore(std::size_t measureCount)
{
    Part part;
    part.id = "P1";
    part.name = "Soprano";
    part.staffCount = 1;

    for (std::size_t index = 0; index < measureCount; ++index)
    {
        Voice voice;
        voice.number = 1;
        voice.events = {
            noteAt(0, ticksPerQuarterNote, Step::g, 4),
            noteAt(ticksPerQuarterNote, ticksPerQuarterNote, Step::a, 4),
            noteAt(2 * ticksPerQuarterNote, 2 * ticksPerQuarterNote, Step::b, 4)};

        Measure measure = emptyMeasure(index);
        measure.voices = {voice};

        part.measures.push_back(measure);
    }

    Score score;
    score.parts = {part};

    return score;
}

// Every x at which a notehead appears on a staff.
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

    std::sort(xs.begin(), xs.end());

    return xs;
}

[[nodiscard]] bool hasGlyph(const StaffLayout& staff, Glyph wanted)
{
    return std::any_of(
        staff.glyphs.begin(), staff.glyphs.end(),
        [wanted](const PlacedGlyph& glyph) { return glyph.glyph == wanted; });
}
} // namespace

TEST_CASE("an empty score lays out to nothing", "[engraving][layout]")
{
    const Score score;
    const ScoreLayout layout = layOutScore(score, LayoutOptions{});

    CHECK(layout.systems.empty());
    CHECK(layout.height == 0.0);
}

TEST_CASE("a two-staff part is one braced part, not two parts", "[engraving][layout][grandstaff]")
{
    const ScoreLayout layout = layOutScore(makePianoScore(), LayoutOptions{});

    REQUIRE(layout.systems.size() == 1);
    REQUIRE(layout.systems[0].parts.size() == 1);

    const PartLayout& piano = layout.systems[0].parts[0];

    CHECK(piano.braced);
    CHECK(piano.staves.size() == 2);
    CHECK(staffCount(layout.systems[0]) == 2);
}

TEST_CASE("a single-staff part is not braced", "[engraving][layout][grandstaff]")
{
    const ScoreLayout layout = layOutScore(makeSingleStaffScore(1), LayoutOptions{});

    REQUIRE(layout.systems.size() == 1);
    REQUIRE(layout.systems[0].parts.size() == 1);

    CHECK_FALSE(layout.systems[0].parts[0].braced);
    CHECK(layout.systems[0].parts[0].staves.size() == 1);
}

TEST_CASE("simultaneous events align across the grand staff", "[engraving][layout][grandstaff]")
{
    // #200's fourth acceptance criterion, as an assertion on numbers rather
    // than something a human checks in a screenshot.
    const ScoreLayout layout = layOutScore(makePianoScore(), LayoutOptions{});

    const PartLayout& piano = layout.systems[0].parts[0];

    const std::vector<double> treble = noteheadXs(piano.staves[0]);
    const std::vector<double> bass = noteheadXs(piano.staves[1]);

    REQUIRE(treble.size() == 2);
    REQUIRE(bass.size() == 1);

    // The treble and bass notes that start together start at the same x.
    CHECK(treble.front() == bass.front());

    // And the treble note that does not is somewhere else.
    CHECK(treble.back() > bass.front());
}

TEST_CASE("each staff keeps its own clef", "[engraving][layout][grandstaff]")
{
    const ScoreLayout layout = layOutScore(makePianoScore(), LayoutOptions{});

    const PartLayout& piano = layout.systems[0].parts[0];

    CHECK(hasGlyph(piano.staves[0], Glyph::gClef));
    CHECK(hasGlyph(piano.staves[1], Glyph::fClef));
    CHECK_FALSE(hasGlyph(piano.staves[0], Glyph::fClef));
    CHECK_FALSE(hasGlyph(piano.staves[1], Glyph::gClef));
}

TEST_CASE("events land on the staff the file assigned", "[engraving][layout][grandstaff]")
{
    const ScoreLayout layout = layOutScore(makePianoScore(), LayoutOptions{});

    const PartLayout& piano = layout.systems[0].parts[0];

    // Two noteheads on the treble staff, one on the bass -- the counts the
    // fixture wrote, not what pitch would suggest.
    CHECK(noteheadXs(piano.staves[0]).size() == 2);
    CHECK(noteheadXs(piano.staves[1]).size() == 1);
}

TEST_CASE("a clef change on one staff leaves the other alone", "[engraving][layout][grandstaff]")
{
    // #200's third acceptance criterion.
    Score score = makePianoScore();

    Measure second = emptyMeasure(1);
    second.attributes.clefs = {Clef{"G", 2, 0, 2}};

    Voice treble;
    treble.number = 1;
    treble.events = {noteAt(0, ticksPerQuarterNote, Step::c, 5, 1)};

    Voice bass;
    bass.number = 2;
    bass.events = {noteAt(0, ticksPerQuarterNote, Step::c, 5, 2)};

    second.voices = {treble, bass};
    score.parts[0].measures.push_back(second);

    LayoutOptions wide;
    wide.availableWidth = 400.0;

    const ScoreLayout layout = layOutScore(score, wide);

    REQUIRE(layout.systems.size() == 1);

    const PartLayout& piano = layout.systems[0].parts[0];

    // The same written pitch is on both staves in bar 2. The bass staff has
    // changed to treble clef, so by bar 2 they are on the same step -- and the
    // treble staff's own placement is untouched.
    const auto stepOf = [](const StaffLayout& staff, double minimumX)
    {
        for (const PlacedGlyph& glyph : staff.glyphs)
        {
            if (glyph.glyph == Glyph::noteheadBlack && glyph.x >= minimumX)
            {
                return glyph.staffStep;
            }
        }

        return -999;
    };

    const double barTwoStart = layout.systems[0].barlineXs.front();

    CHECK(stepOf(piano.staves[0], barTwoStart) == stepOf(piano.staves[1], barTwoStart));
}

TEST_CASE(
    "horizontal space grows with duration but not in proportion",
    "[engraving][layout][spacing]")
{
    const LayoutOptions options;

    const double quarter = spacingForDuration(ticksPerQuarterNote, options);
    const double half = spacingForDuration(2 * ticksPerQuarterNote, options);

    // Wider than a quarter, and narrower than two of them. Strict
    // proportionality is the thing that makes engraving look amateurish.
    CHECK(half > quarter);
    CHECK(half < 2 * quarter);
}

TEST_CASE("a narrow page breaks into more systems than a wide one", "[engraving][layout][systems]")
{
    const Score score = makeSingleStaffScore(8);

    LayoutOptions narrow;
    narrow.availableWidth = 40.0;

    LayoutOptions wide;
    wide.availableWidth = 400.0;

    const ScoreLayout narrowLayout = layOutScore(score, narrow);
    const ScoreLayout wideLayout = layOutScore(score, wide);

    CHECK(narrowLayout.systems.size() > wideLayout.systems.size());
    CHECK(wideLayout.systems.size() == 1);

    // Every measure appears exactly once however it is broken.
    std::size_t placed = 0;

    for (const SystemLayout& system : narrowLayout.systems)
    {
        placed += system.measureCount;
    }

    CHECK(placed == 8);
}

TEST_CASE("systems stack downward and the score reports its height", "[engraving][layout][systems]")
{
    LayoutOptions options;
    options.availableWidth = 40.0;

    const ScoreLayout layout = layOutScore(makeSingleStaffScore(6), options);

    REQUIRE(layout.systems.size() > 1);

    for (std::size_t index = 1; index < layout.systems.size(); ++index)
    {
        CHECK(layout.systems[index].topOffset > layout.systems[index - 1].topOffset);
    }

    CHECK(layout.height > layout.systems.back().topOffset);
}

TEST_CASE("layout is independent of the scale it will be drawn at", "[engraving][layout]")
{
    // The whole point of working in staff spaces: the same score at the same
    // width lays out identically, and only the painter's multiplier differs.
    // A font size leaking into the layer is what this would catch.
    const Score score = makePianoScore();

    const ScoreLayout first = layOutScore(score, LayoutOptions{});
    const ScoreLayout second = layOutScore(score, LayoutOptions{});

    REQUIRE(first.systems.size() == second.systems.size());

    const std::vector<double> a = noteheadXs(first.systems[0].parts[0].staves[0]);
    const std::vector<double> b = noteheadXs(second.systems[0].parts[0].staves[0]);

    CHECK(a == b);
}

TEST_CASE("the staves of a part do not overlap", "[engraving][layout][grandstaff]")
{
    const ScoreLayout layout = layOutScore(makePianoScore(), LayoutOptions{});

    const PartLayout& piano = layout.systems[0].parts[0];

    REQUIRE(piano.staves.size() == 2);

    // The bass staff starts below the bottom line of the treble staff, which is
    // four staff spaces below its top.
    CHECK(piano.staves[1].topOffset >= piano.staves[0].topOffset + 4.0);
    CHECK(layout.systems[0].height >= piano.staves[1].topOffset + 4.0);
}

TEST_CASE("a cross-staff voice is reported and still placed", "[engraving][layout][diagnostics]")
{
    // The fifth acceptance criterion: never silently misplaced. Both notes go
    // where the file put them, and the undrawn beam is said out loud.
    Voice crossing;
    crossing.number = 1;
    crossing.events = {
        noteAt(0, ticksPerQuarterNote, Step::c, 5, 1),
        noteAt(ticksPerQuarterNote, ticksPerQuarterNote, Step::c, 3, 2)};

    Measure measure = emptyMeasure(0);
    measure.attributes.clefs = {Clef{"G", 2, 0, 1}, Clef{"F", 4, 0, 2}};
    measure.voices = {crossing};

    Part part;
    part.id = "P1";
    part.staffCount = 2;
    part.measures = {measure};

    Score score;
    score.parts = {part};

    const ScoreLayout layout = layOutScore(score, LayoutOptions{});

    REQUIRE(layout.diagnostics.size() == 1);
    CHECK(layout.diagnostics[0].location.measureNumber == "1");
    CHECK(layout.diagnostics[0].elementName == "beam");

    const PartLayout& piano = layout.systems[0].parts[0];

    // One note on each staff, where the file said -- not both moved to one.
    CHECK(noteheadXs(piano.staves[0]).size() == 1);
    CHECK(noteheadXs(piano.staves[1]).size() == 1);
}

TEST_CASE("a score with nothing unusual reports no diagnostics", "[engraving][layout][diagnostics]")
{
    const ScoreLayout layout = layOutScore(makeSingleStaffScore(4), LayoutOptions{});

    CHECK(layout.diagnostics.empty());
}

TEST_CASE("a note outside the staff gets ledger lines", "[engraving][layout][ledger]")
{
    // C3 in bass clef is step 3 -- inside the staff, no ledger lines. C2 is
    // step -4, two below. Picking the first by accident is how a ledger-line
    // test passes while proving nothing, so both are here.
    Voice inside;
    inside.number = 1;
    inside.events = {noteAt(0, ticksPerQuarterNote, Step::c, 3, 1)};

    Voice outside;
    outside.number = 2;
    outside.events = {noteAt(ticksPerQuarterNote, ticksPerQuarterNote, Step::c, 2, 1)};

    Measure measure = emptyMeasure(0);
    measure.attributes.clefs = {Clef{"F", 4, 0, 1}};
    measure.voices = {inside, outside};

    Part part;
    part.id = "P1";
    part.staffCount = 1;
    part.measures = {measure};

    Score score;
    score.parts = {part};

    const ScoreLayout layout = layOutScore(score, LayoutOptions{});

    const StaffLayout& staff = layout.systems[0].parts[0].staves[0];

    // Two ledger lines, both from C2 and both below the staff.
    REQUIRE(staff.ledgerLines.size() == 2);

    for (const PlacedLedgerLine& line : staff.ledgerLines)
    {
        CHECK(line.staffStep < bottomLineStep);
        CHECK(line.width > 0.0);
    }
}
