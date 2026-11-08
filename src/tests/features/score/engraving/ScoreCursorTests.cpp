#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/ScoreCursor.h"
#include "features/score/engraving/ScoreLayoutEngine.h"

#include "support/ScoreFixtures.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace engraving;
using Catch::Approx;
using score::Tick;
using testing::score::notesOf;

namespace
{
constexpr Tick quarter = score::ticksPerQuarterNote;
constexpr Tick bar = quarter * 4;

[[nodiscard]] score::Pitch pitch(score::Step step, int octave)
{
    return score::pitchFromSpelling(step, 0, octave);
}

[[nodiscard]] score::ScoreEvent noteAt(Tick onset, Tick duration, score::Step step, int staff = 1)
{
    score::ScoreEvent event;
    event.kind = score::EventKind::note;
    event.onset = onset;
    event.duration = duration;
    event.staff = staff;
    event.notes = notesOf({pitch(step, staff == 1 ? 4 : 3)});
    return event;
}

[[nodiscard]] score::ScoreEvent restAt(Tick onset, Tick duration)
{
    score::ScoreEvent event;
    event.kind = score::EventKind::rest;
    event.onset = onset;
    event.duration = duration;
    return event;
}

[[nodiscard]] score::Voice voiceOf(int number, std::vector<score::ScoreEvent> events)
{
    score::Voice voice;
    voice.number = number;
    voice.events = std::move(events);
    return voice;
}

[[nodiscard]] score::Measure measureOf(std::size_t index, std::vector<score::Voice> voices)
{
    score::Measure measure;
    measure.index = index;
    measure.printedNumber = std::to_string(index + 1);
    measure.nominalDuration = bar;
    measure.start = static_cast<Tick>(index) * bar;
    measure.voices = std::move(voices);
    if (index == 0)
    {
        measure.attributes.clefs = {score::Clef{"G", 2, 0, 1}};
        measure.attributes.key = score::KeySignature{0, "major"};
        measure.attributes.time = score::TimeSignature{4, 4};
    }
    return measure;
}

[[nodiscard]] score::Part partOf(std::string id, std::vector<score::Measure> measures)
{
    score::Part part;
    part.id = id;
    part.name = id;
    part.measures = std::move(measures);
    return part;
}

[[nodiscard]] std::shared_ptr<const score::Score> scoreOf(std::vector<score::Part> parts)
{
    auto built = std::make_shared<score::Score>();
    built->metadata.workTitle = "A Cursor Test";
    built->parts = std::move(parts);
    return built;
}

[[nodiscard]] LayoutOptions wide()
{
    LayoutOptions options;
    options.availableWidth = 400.0;
    return options;
}

// Four quarters, C D E F, in one bar.
[[nodiscard]] score::Measure fourQuarters(std::size_t index)
{
    return measureOf(
        index, {voiceOf(
                   1, {noteAt(0, quarter, score::Step::c), noteAt(quarter, quarter, score::Step::d),
                       noteAt(quarter * 2, quarter, score::Step::e),
                       noteAt(quarter * 3, quarter, score::Step::f)})});
}
} // namespace

TEST_CASE("a system's time anchors are where its noteheads are", "[score][engraving][cursor]")
{
    const auto source = scoreOf({partOf("P1", {fourQuarters(0)})});
    const ScoreLayout layout = layOutScore(source, wide());
    REQUIRE(layout.systems.size() == 1);
    const System& system = layout.systems.front();

    // Four onsets and the bar's end.
    REQUIRE(system.timeAnchors.size() == 5);
    REQUIRE(system.noteheads.size() == 4);
    for (std::size_t index = 0; index < 4; ++index)
    {
        CHECK(system.timeAnchors.at(index).tick == static_cast<Tick>(index) * quarter);
        CHECK(system.timeAnchors.at(index).x == Approx(system.noteheads.at(index).x));
    }
    CHECK(system.timeAnchors.back().tick == bar);
    CHECK(system.timeAnchors.back().x == Approx(system.width));
    CHECK(std::is_sorted(
        system.timeAnchors.begin(), system.timeAnchors.end(),
        [](const TimeAnchor& left, const TimeAnchor& right) { return left.x < right.x; }));

    CHECK(system.staffParts == std::vector<std::size_t>{0});
}

TEST_CASE("a downbeat is the note after the barline, not the barline", "[score][engraving][cursor]")
{
    const auto source = scoreOf({partOf("P1", {fourQuarters(0), fourQuarters(1)})});
    const ScoreLayout layout = layOutScore(source, wide());
    REQUIRE(layout.systems.size() == 1);
    const System& system = layout.systems.front();

    // One anchor per tick: bar 2's downbeat is its first note.
    REQUIRE(system.timeAnchors.size() == 9);
    const auto downbeat = std::find_if(
        system.timeAnchors.begin(), system.timeAnchors.end(),
        [](const TimeAnchor& anchor) { return anchor.tick == bar; });
    REQUIRE(downbeat != system.timeAnchors.end());
    CHECK(downbeat->x == Approx(system.noteheads.at(4).x));
}

TEST_CASE("the cursor moves linearly through a held note", "[score][engraving][cursor]")
{
    const auto source = scoreOf({partOf(
        "P1", {measureOf(
                  0, {voiceOf(
                         1, {noteAt(0, quarter * 2, score::Step::c),
                             noteAt(quarter * 2, quarter * 2, score::Step::e)})})})});
    const ScoreLayout layout = layOutScore(source, wide());
    const System& system = layout.systems.front();
    REQUIRE(system.timeAnchors.size() == 3);

    const auto start = placeOfTick(layout, 0.0);
    const auto halfway = placeOfTick(layout, static_cast<double>(quarter));
    const auto second = placeOfTick(layout, static_cast<double>(quarter * 2));
    REQUIRE(start.has_value());
    REQUIRE(halfway.has_value());
    REQUIRE(second.has_value());

    CHECK(start->x == Approx(system.noteheads.at(0).x));
    CHECK(second->x == Approx(system.noteheads.at(1).x));
    CHECK(halfway->x == Approx((start->x + second->x) / 2.0));

    // Clamped either side.
    CHECK(placeOfTick(layout, -100.0)->x == Approx(start->x));
    CHECK(placeOfTick(layout, static_cast<double>(bar * 10))->x == Approx(system.width));
}

TEST_CASE("a tick on a later system is placed on that system", "[score][engraving][cursor]")
{
    std::vector<score::Measure> measures;
    for (std::size_t index = 0; index < 12; ++index)
    {
        measures.push_back(fourQuarters(index));
    }
    LayoutOptions narrow;
    narrow.availableWidth = 60.0;
    const ScoreLayout layout = layOutScore(scoreOf({partOf("P1", measures)}), narrow);
    REQUIRE(layout.systems.size() > 2);

    const System& second = layout.systems.at(1);
    const Tick firstOfSecond = static_cast<Tick>(second.firstMeasureIndex) * bar;

    // Its downbeat is on it, not at the end of the system before.
    const auto place = placeOfTick(layout, static_cast<double>(firstOfSecond));
    REQUIRE(place.has_value());
    CHECK(place->system == 1);
    CHECK(place->x == Approx(second.timeAnchors.front().x));

    // Just before it is still on the first system, near its end.
    const auto before = placeOfTick(layout, static_cast<double>(firstOfSecond) - 1.0);
    REQUIRE(before.has_value());
    CHECK(before->system == 0);
}

TEST_CASE("a click starts from the onset at or before it", "[score][engraving][cursor]")
{
    const ScoreLayout layout = layOutScore(scoreOf({partOf("P1", {fourQuarters(0)})}), wide());
    const System& system = layout.systems.front();

    CHECK(onsetAtOrBefore(system, 0.0) == 0);
    CHECK(onsetAtOrBefore(system, system.noteheads.at(2).x + 0.1) == quarter * 2);
    CHECK(onsetAtOrBefore(system, system.noteheads.at(2).x - 0.1) == quarter);

    // Past the last note -- even past the final barline -- is the last note.
    CHECK(onsetAtOrBefore(system, system.width + 50.0) == quarter * 3);
}

TEST_CASE("the sounding note and the next two onsets are targets", "[score][engraving][cursor]")
{
    const auto source = scoreOf({partOf("P1", {fourQuarters(0), fourQuarters(1)})});

    const Targets targets = targetsAt(*source, {0}, 10.0, 2);
    REQUIRE(targets.sounding.size() == 1);
    CHECK(targets.sounding.front() == TargetNote{0, score::NoteRef{0, 0, 0, 0}});
    REQUIRE(targets.upcoming.size() == 2);
    CHECK(targets.upcoming.at(0).note.eventIndex == 1);
    CHECK(targets.upcoming.at(1).note.eventIndex == 2);

    // Across a barline.
    const Targets late = targetsAt(*source, {0}, static_cast<double>(quarter * 3) + 1.0, 2);
    REQUIRE(late.upcoming.size() == 2);
    CHECK(late.upcoming.at(0).note.measureIndex == 1);
    CHECK(late.upcoming.at(0).note.eventIndex == 0);

    // No look-ahead: nothing upcoming.
    CHECK(targetsAt(*source, {0}, 10.0, 0).upcoming.empty());

    // A rest sounds nothing.
    const auto resting = scoreOf({partOf("P1", {measureOf(0, {voiceOf(1, {restAt(0, bar)})})})});
    CHECK(targetsAt(*resting, {0}, 10.0, 2).sounding.empty());
}

TEST_CASE("a tied note sounds but is not a new attack", "[score][engraving][cursor]")
{
    score::ScoreEvent held = noteAt(0, quarter, score::Step::c);
    score::ScoreEvent continued = noteAt(quarter, quarter, score::Step::c);
    held.notes.front().tiedTo = score::NoteRef{0, 0, 1, 0};
    continued.notes.front().tiedFrom = score::NoteRef{0, 0, 0, 0};
    const auto source = scoreOf({partOf(
        "P1", {measureOf(
                  0, {voiceOf(
                         1, {held, continued, noteAt(quarter * 2, quarter, score::Step::d),
                             noteAt(quarter * 3, quarter, score::Step::e)})})})});

    const Targets atStart = targetsAt(*source, {0}, 0.0, 2);
    REQUIRE(atStart.upcoming.size() == 2);
    CHECK(atStart.upcoming.at(0).note.eventIndex == 2);
    CHECK(atStart.upcoming.at(1).note.eventIndex == 3);

    const Targets inTie = targetsAt(*source, {0}, static_cast<double>(quarter) + 5.0, 1);
    REQUIRE(inTie.sounding.size() == 1);
    CHECK(inTie.sounding.front().note.eventIndex == 1);
}

TEST_CASE("only the shown parts have targets", "[score][engraving][cursor]")
{
    const auto source =
        scoreOf({partOf("Voice", {fourQuarters(0)}), partOf("Piano", {fourQuarters(0)})});

    const Targets both = targetsAt(*source, {0, 1}, 10.0, 1);
    CHECK(both.sounding.size() == 2);
    CHECK(both.upcoming.size() == 2);

    const Targets voiceOnly = targetsAt(*source, {0}, 10.0, 1);
    REQUIRE(voiceOnly.sounding.size() == 1);
    CHECK(voiceOnly.sounding.front().part == 0);

    // A layout hiding the voice draws only the piano, and says which part
    // that is.
    LayoutOptions options = wide();
    options.hiddenParts = {PartKey{"Voice", "Voice"}};
    const ScoreLayout layout = layOutScore(source, options);
    CHECK(drawnPartsOf(layout) == std::vector<std::size_t>{1});
}

TEST_CASE("two staves sharing an onset share one anchor", "[score][engraving][cursor]")
{
    score::Measure grand = measureOf(
        0, {voiceOf(1, {noteAt(0, bar, score::Step::c, 1)}),
            voiceOf(
                5, {noteAt(0, quarter * 2, score::Step::c, 2),
                    noteAt(quarter * 2, quarter * 2, score::Step::g, 2)})});
    grand.attributes.clefs = {score::Clef{"G", 2, 0, 1}, score::Clef{"F", 4, 0, 2}};
    score::Part piano = partOf("Piano", {grand});
    piano.staffCount = 2;
    const ScoreLayout layout = layOutScore(scoreOf({piano}), wide());
    REQUIRE(layout.systems.size() == 1);
    const System& system = layout.systems.front();

    // Onsets 0 and a half note, and the bar's end.
    REQUIRE(system.timeAnchors.size() == 3);
    CHECK(system.timeAnchors.at(1).tick == quarter * 2);
    CHECK(system.staffParts == std::vector<std::size_t>{0, 0});
}

TEST_CASE("a multi-bar rest anchors its two ends", "[score][engraving][cursor]")
{
    std::vector<score::Measure> measures{fourQuarters(0)};
    for (std::size_t index = 1; index <= 4; ++index)
    {
        measures.push_back(measureOf(index, {voiceOf(1, {restAt(0, bar)})}));
    }
    measures.push_back(fourQuarters(5));
    const ScoreLayout layout = layOutScore(scoreOf({partOf("P1", measures)}), wide());
    REQUIRE(layout.systems.size() == 1);
    const System& system = layout.systems.front();
    REQUIRE(system.multiRests.size() == 1);

    // The rest's start and end, with nothing between: the cursor crosses the
    // condensed bars at an even pace.
    const auto at = [&system](Tick tick)
    {
        return std::find_if(
            system.timeAnchors.begin(), system.timeAnchors.end(),
            [tick](const TimeAnchor& anchor) { return anchor.tick == tick; });
    };
    CHECK(at(bar) != system.timeAnchors.end());
    CHECK(at(bar * 5) != system.timeAnchors.end());
    CHECK(at(bar * 2) == system.timeAnchors.end());

    const auto middle = placeOfTick(layout, static_cast<double>(bar * 3));
    REQUIRE(middle.has_value());
    CHECK(middle->x > at(bar)->x);
    CHECK(middle->x < at(bar * 5)->x);
}
