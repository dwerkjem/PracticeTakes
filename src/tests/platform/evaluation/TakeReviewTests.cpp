#include <catch2/catch_test_macros.hpp>

#include "platform/evaluation/TakeReview.h"

#include <memory>
#include <vector>

using namespace evaluation;

namespace
{
constexpr std::int64_t quarter = 22050; // At 120 bpm and 44.1 kHz.

// A score of one 4/4 bar of quarters per part, at 120 bpm.
[[nodiscard]] std::shared_ptr<score::Score>
quarters(const std::vector<std::vector<score::Pitch>>& parts)
{
    auto source = std::make_shared<score::Score>();
    source->tempoMap = score::TempoMap::build({{0, 120.0}});
    for (const auto& pitches : parts)
    {
        score::Part part;
        score::Measure measure;
        measure.nominalDuration = 4 * score::ticksPerQuarterNote;
        measure.attributes.time = score::TimeSignature{4, 4};
        score::Voice voice;
        for (std::size_t beat = 0; beat < pitches.size(); ++beat)
        {
            score::ScoreEvent event;
            event.kind = score::EventKind::note;
            event.onset = static_cast<score::Tick>(beat) * score::ticksPerQuarterNote;
            event.duration = score::ticksPerQuarterNote;
            event.notes.push_back(
                score::Note{pitches[beat], pitches[beat], std::nullopt, std::nullopt});
            voice.events.push_back(event);
        }
        measure.voices.push_back(voice);
        part.measures.push_back(measure);
        source->parts.push_back(part);
    }
    return source;
}

[[nodiscard]] score::Pitch pitchOf(score::Step step, int octave, int alter = 0)
{
    return score::pitchFromSpelling(step, alter, octave);
}

// A keyboard take of `source`, pressing `keys` at their frames.
[[nodiscard]] take::Take keyboardTake(
    std::shared_ptr<score::Score> source,
    const std::vector<std::pair<std::int64_t, int>>& keys)
{
    take::Take recorded;
    auto table = std::make_shared<transport::ClockTable>();
    transport::buildTable(*table, *source, 100, 44100.0);
    recorded.score = std::move(source);
    recorded.table = table;
    recorded.usedKeyboard = true;
    for (const auto& [frame, key] : keys)
    {
        take::KeyboardEvent event;
        event.frame = frame;
        event.event.kind = midi::MidiEventKind::noteOn;
        event.event.noteNumber = key;
        event.event.velocity = 70;
        recorded.keyboard.push_back(event);
    }
    return recorded;
}
} // namespace

TEST_CASE("a key is named with sharps and its octave", "[evaluation][review]")
{
    CHECK(keyName(60) == "C4");
    CHECK(keyName(61) == "C#4");
    CHECK(keyName(21) == "A0");
    CHECK(keyName(0) == "C-1");
    CHECK(keyName(127) == "G9");
}

TEST_CASE("a review lists every result in time order, extras included", "[evaluation][review]")
{
    // G Ab G G, played G G, a stray G#5 between beats 2 and 3, B4 for Ab4
    // 50 ms late, and the last note not at all.
    using score::Step;
    const take::Take recorded = keyboardTake(
        quarters(
            {{pitchOf(Step::g, 4), pitchOf(Step::g, 4), pitchOf(Step::a, 4, -1),
              pitchOf(Step::g, 4)}}),
        {{0, 67}, {quarter, 67}, {quarter + quarter / 2, 80}, {2 * quarter + 2205, 71}});

    const NoteReport report = judgeNotes(recorded, 0);
    const std::vector<ReviewRow> rows = reviewRows(recorded, report);
    REQUIRE(rows.size() == 5);
    CHECK(rows[0].bar == 0);
    CHECK(rows[0].beat == 0);
    CHECK(rows[1].beat == 1);
    CHECK(rows[2].beat == 1);
    CHECK(rows[3].beat == 2);
    CHECK(rows[4].beat == 3);
    CHECK(report.results[2].outcome == NoteOutcome::wrong);
    CHECK(report.results[2].playedMidi == std::optional<int>{71});
    CHECK(report.results[2].timing == TimingVerdict::late);
    CHECK(report.results[3].outcome == NoteOutcome::missed);
    CHECK(writtenNameOf(recorded, report.results[2]) == "Ab4");

    CHECK_FALSE(rows[0].mistake);
    CHECK(rows[2].mistake);
    CHECK(rows[2].extra == std::optional<std::size_t>{0});
    CHECK(rows[3].result == std::optional<std::size_t>{2});
    CHECK(rows[3].tick == 2.0 * score::ticksPerQuarterNote);
    CHECK(rows[4].mistake);

    CHECK(momentsOf(report).late == 1);
}

TEST_CASE("a chord's early or late counts once", "[evaluation][review]")
{
    NoteReport report;
    for (int note = 0; note < 3; ++note)
    {
        NoteResult result;
        result.tick = 0;
        result.outcome = NoteOutcome::correct;
        result.timing = TimingVerdict::late;
        report.results.push_back(result);
    }
    report.summary.correct = 3;
    CHECK(momentsOf(report).late == 1);
    CHECK(momentsOf(report).onTime == 0);
    CHECK(momentsOf(NoteReport{}).late == 0);
}

TEST_CASE("a take is reviewed against the part it matches best", "[evaluation][review]")
{
    using score::Step;
    // A voice (part 0) and a piano (part 1); the pianist played the piano part.
    const auto source = quarters(
        {{pitchOf(Step::e, 5), pitchOf(Step::f, 5), pitchOf(Step::g, 5), pitchOf(Step::a, 5)},
         {pitchOf(Step::c, 4), pitchOf(Step::e, 4), pitchOf(Step::g, 4), pitchOf(Step::c, 5)}});
    const take::Take pianist =
        keyboardTake(source, {{0, 60}, {quarter, 64}, {2 * quarter, 67}, {3 * quarter, 72}});
    const ReviewedPart reviewed = bestPartFor(pianist);
    CHECK(reviewed.part == 1);
    CHECK(reviewed.report.summary.correct == 4);
    CHECK(matchScore(reviewed.report) == 12);

    // Two identical parts: the earlier one.
    const auto twins = quarters(
        {{pitchOf(Step::c, 4), pitchOf(Step::c, 4)}, {pitchOf(Step::c, 4), pitchOf(Step::c, 4)}});
    CHECK(bestPartFor(keyboardTake(twins, {{0, 60}, {quarter, 60}})).part == 0);

    // No score, nothing judged.
    const ReviewedPart nothing = bestPartFor(take::Take{});
    CHECK(nothing.part == 0);
    CHECK(nothing.report.results.empty());
}
