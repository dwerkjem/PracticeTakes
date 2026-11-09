#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/ReviewMarks.h"
#include "features/score/engraving/ScoreLayoutEngine.h"

#include "support/ScoreFixtures.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

using namespace engraving;
using Catch::Approx;
using score::Tick;

namespace
{
constexpr Tick quarter = score::ticksPerQuarterNote;

[[nodiscard]] score::ScoreEvent noteAt(Tick onset, score::Step step)
{
    score::ScoreEvent event;
    event.kind = score::EventKind::note;
    event.onset = onset;
    event.duration = quarter;
    event.staff = 1;
    event.notes = testing::score::notesOf({score::pitchFromSpelling(step, 0, 4)});
    return event;
}

// One part per entry, each one bar of C D E F.
[[nodiscard]] std::shared_ptr<const score::Score> scoreOf(std::size_t parts)
{
    auto built = std::make_shared<score::Score>();
    for (std::size_t index = 0; index < parts; ++index)
    {
        score::Measure measure;
        measure.printedNumber = "1";
        measure.nominalDuration = 4 * quarter;
        measure.attributes.clefs = {score::Clef{"G", 2, 0, 1}};
        measure.attributes.key = score::KeySignature{0, "major"};
        measure.attributes.time = score::TimeSignature{4, 4};
        score::Voice voice;
        voice.number = 1;
        voice.events = {
            noteAt(0, score::Step::c), noteAt(quarter, score::Step::d),
            noteAt(2 * quarter, score::Step::e), noteAt(3 * quarter, score::Step::f)};
        measure.voices.push_back(voice);
        score::Part part;
        part.id = "P" + std::to_string(index + 1);
        part.name = part.id;
        part.measures.push_back(measure);
        built->parts.push_back(part);
    }
    return built;
}

[[nodiscard]] evaluation::NoteResult resultAt(
    std::size_t part,
    std::size_t event,
    evaluation::NoteOutcome outcome,
    evaluation::TimingVerdict timing = evaluation::TimingVerdict::onTime)
{
    evaluation::NoteResult result;
    result.part = part;
    result.ref = score::NoteRef{0, 0, event, 0};
    result.tick = static_cast<Tick>(event) * quarter;
    result.outcome = outcome;
    result.timing = timing;
    return result;
}

[[nodiscard]] LayoutOptions wide()
{
    LayoutOptions options;
    options.availableWidth = 400.0;
    return options;
}
} // namespace

TEST_CASE("each judged note is marked on its own notehead", "[score][engraving][review]")
{
    const ScoreLayout layout = layOutScore(scoreOf(1), wide());
    REQUIRE(layout.systems.size() == 1);

    evaluation::NoteReport report;
    report.results = {
        resultAt(0, 0, evaluation::NoteOutcome::correct),
        resultAt(0, 1, evaluation::NoteOutcome::wrong, evaluation::TimingVerdict::late),
        resultAt(0, 2, evaluation::NoteOutcome::missed, evaluation::TimingVerdict::absent),
        resultAt(0, 3, evaluation::NoteOutcome::pitchNotJudged, evaluation::TimingVerdict::early)};
    report.results[1].playedMidi = 64;

    const ReviewMarks marks = reviewMarksFor(layout, report, {});
    REQUIRE(marks.notes.size() == 4);
    const System& system = layout.systems.front();
    for (const NoteMark& mark : marks.notes)
    {
        CHECK(mark.system == 0);
        const score::NoteRef& source = system.noteheads.at(mark.notehead).source;
        switch (mark.pitch)
        {
        case PitchMark::correct:
            CHECK(source.eventIndex == 0);
            CHECK(mark.timing == TimingMark::none);
            break;
        case PitchMark::wrong:
            CHECK(source.eventIndex == 1);
            CHECK(mark.timing == TimingMark::late);
            CHECK(mark.playedMidi == 64);
            break;
        case PitchMark::missed:
            CHECK(source.eventIndex == 2);
            break;
        case PitchMark::none:
            CHECK(source.eventIndex == 3);
            CHECK(mark.timing == TimingMark::early);
            break;
        case PitchMark::sharp:
        case PitchMark::flat:
        case PitchMark::uncertain:
            FAIL("a keyboard result is not marked for intonation");
            break;
        }
    }
    CHECK(marks.extras.empty());
}

TEST_CASE("a sung note is marked sharp, flat or uncertain", "[score][engraving][review]")
{
    const ScoreLayout layout = layOutScore(scoreOf(1), wide());
    REQUIRE(layout.systems.size() == 1);

    evaluation::NoteReport report;
    report.results = {
        resultAt(0, 0, evaluation::NoteOutcome::correct),
        resultAt(0, 1, evaluation::NoteOutcome::correct),
        resultAt(0, 2, evaluation::NoteOutcome::correct),
        resultAt(0, 3, evaluation::NoteOutcome::pitchNotJudged)};
    const std::array<evaluation::IntonationVerdict, 4> verdicts{
        evaluation::IntonationVerdict::inTune, evaluation::IntonationVerdict::sharp,
        evaluation::IntonationVerdict::flat, evaluation::IntonationVerdict::uncertain};
    const std::array<double, 4> cents{4.0, 30.0, -40.0, 0.0};
    for (std::size_t index = 0; index < verdicts.size(); ++index)
    {
        evaluation::Intonation intonation;
        intonation.verdict = verdicts.at(index);
        intonation.cents = cents.at(index);
        report.results[index].intonation = intonation;
    }

    const ReviewMarks marks = reviewMarksFor(layout, report, {});
    REQUIRE(marks.notes.size() == 4);
    const System& system = layout.systems.front();
    for (const NoteMark& mark : marks.notes)
    {
        switch (system.noteheads.at(mark.notehead).source.eventIndex)
        {
        case 0:
            CHECK(mark.pitch == PitchMark::correct);
            CHECK(mark.cents == 4.0);
            break;
        case 1:
            CHECK(mark.pitch == PitchMark::sharp);
            CHECK(mark.cents == 30.0);
            break;
        case 2:
            CHECK(mark.pitch == PitchMark::flat);
            CHECK(mark.cents == -40.0);
            break;
        default:
            CHECK(mark.pitch == PitchMark::uncertain);
            CHECK_FALSE(mark.cents.has_value());
            break;
        }
    }
}

TEST_CASE("another part's results mark nothing in this one", "[score][engraving][review]")
{
    // Two identical parts: a result for the second marks only the second's
    // staff.
    const ScoreLayout layout = layOutScore(scoreOf(2), wide());
    REQUIRE(layout.systems.size() == 1);
    evaluation::NoteReport report;
    report.results = {resultAt(1, 0, evaluation::NoteOutcome::correct)};

    const ReviewMarks marks = reviewMarksFor(layout, report, {});
    REQUIRE(marks.notes.size() == 1);
    const NoteheadGlyph& head = layout.systems.front().noteheads.at(marks.notes.front().notehead);
    CHECK(layout.systems.front().staffParts.at(static_cast<std::size_t>(head.staff)) == 1);
}

TEST_CASE("an extra note is marked at its time", "[score][engraving][review]")
{
    const ScoreLayout layout = layOutScore(scoreOf(1), wide());
    REQUIRE(layout.systems.size() == 1);
    evaluation::NoteReport report;
    report.extra = {evaluation::ExtraNote{0, 71}, evaluation::ExtraNote{0, std::nullopt}};

    // Halfway between beats 2 and 3; the second has no tick given.
    const ReviewMarks marks = reviewMarksFor(layout, report, {1.5 * quarter});
    REQUIRE(marks.extras.size() == 1);
    const System& system = layout.systems.front();
    const double expected = (system.noteheads.at(1).x + system.noteheads.at(2).x) / 2.0;
    CHECK(marks.extras.front().x == Approx(expected));
    CHECK(marks.extras.front().midi == 71);
}
