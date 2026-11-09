#include <catch2/catch_test_macros.hpp>

#include "application/testcontrol/SyntheticTake.h"
#include "platform/evaluation/TakeReview.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr score::Tick quarter = score::ticksPerQuarterNote;

// `bars` bars of quarters per part at 100 bpm: a voice, then a piano.
[[nodiscard]] std::shared_ptr<const score::Score> voiceAndPiano(int bars)
{
    auto result = std::make_shared<score::Score>();
    result->metadata.workTitle = "Synthetic";
    result->tempoMap = score::TempoMap::build({{0, 100.0}});
    for (const int base : {72, 48})
    {
        score::Part part;
        for (int index = 0; index < bars; ++index)
        {
            score::Measure measure;
            measure.index = static_cast<std::size_t>(index);
            measure.start = static_cast<score::Tick>(index) * 4 * quarter;
            measure.nominalDuration = 4 * quarter;
            if (index == 0)
            {
                measure.attributes.time = score::TimeSignature{4, 4};
            }
            score::Voice voice;
            for (int beat = 0; beat < 4; ++beat)
            {
                score::ScoreEvent event;
                event.kind = score::EventKind::note;
                event.onset = static_cast<score::Tick>(beat) * quarter;
                event.duration = quarter;
                score::Pitch pitch;
                pitch.midiNoteNumber = base + ((index * 4 + beat) % 12);
                event.notes.push_back(score::Note{pitch, pitch, std::nullopt, std::nullopt});
                voice.events.push_back(event);
            }
            measure.voices.push_back(voice);
            part.measures.push_back(measure);
        }
        result->parts.push_back(part);
    }
    return result;
}
} // namespace

TEST_CASE("a synthetic take has some of every mistake", "[testcontrol][review]")
{
    const take::Take played = testcontrol::syntheticKeyboardTake(voiceAndPiano(12), 1, 10);
    CHECK(played.state() == take::TakeState::stoppedEarly);
    CHECK(played.stoppedInBar == 9);

    const evaluation::ReviewedPart reviewed = evaluation::bestPartFor(played);
    CHECK(reviewed.part == 1);
    const evaluation::NoteSummary& summary = reviewed.report.summary;
    CHECK(summary.correct > summary.wrong + summary.missed);
    CHECK(summary.wrong > 0);
    CHECK(summary.missed > 0);
    CHECK(summary.extra > 0);

    std::size_t early = 0;
    std::size_t late = 0;
    for (const evaluation::NoteResult& result : reviewed.report.results)
    {
        early += result.timing == evaluation::TimingVerdict::early ? 1 : 0;
        late += result.timing == evaluation::TimingVerdict::late ? 1 : 0;
    }
    CHECK(early > 0);
    CHECK(late > 0);

    // Only the bars it played are judged, not the two after it stopped.
    CHECK(reviewed.report.results.size() == 10 * 4);
}

TEST_CASE("a synthetic take of nothing is empty", "[testcontrol][review]")
{
    CHECK(testcontrol::syntheticKeyboardTake(nullptr, 0, 4).keyboard.empty());
    CHECK(testcontrol::syntheticKeyboardTake(voiceAndPiano(2), 5, 4).keyboard.empty());
}

TEST_CASE("a synthetic sung take is judged by its pattern", "[testcontrol][review][intonation]")
{
    using evaluation::IntonationVerdict;
    using testcontrol::SungNote;

    const take::Take sung = testcontrol::syntheticSungTake(voiceAndPiano(6), 0, 4);
    CHECK(sung.state() == take::TakeState::stoppedEarly);
    CHECK(sung.usedMicrophone);
    REQUIRE_FALSE(sung.audio.empty());

    const evaluation::NoteReport report = evaluation::judgeNotes(sung, 0);
    REQUIRE(report.results.size() == 16);
    for (std::size_t index = 0; index < report.results.size(); ++index)
    {
        const evaluation::NoteResult& result = report.results[index];
        INFO("note " << index);
        switch (testcontrol::sungNoteAt(index))
        {
        case SungNote::inTune:
            CHECK(result.outcome == evaluation::NoteOutcome::correct);
            REQUIRE(result.intonation.has_value());
            CHECK(result.intonation->verdict == IntonationVerdict::inTune);
            CHECK(result.intonation->score > 0.9);
            break;
        case SungNote::sharp:
            CHECK(result.outcome == evaluation::NoteOutcome::correct);
            REQUIRE(result.intonation.has_value());
            CHECK(result.intonation->verdict == IntonationVerdict::sharp);
            CHECK(std::abs(result.intonation->cents - testcontrol::syntheticSharpCents) < 3.0);
            break;
        case SungNote::flat:
            REQUIRE(result.intonation.has_value());
            CHECK(result.intonation->verdict == IntonationVerdict::flat);
            CHECK(std::abs(result.intonation->cents + testcontrol::syntheticSharpCents) < 3.0);
            break;
        case SungNote::wrong:
            CHECK(result.outcome == evaluation::NoteOutcome::wrong);
            CHECK(result.playedMidi == result.writtenMidi + 2);
            break;
        case SungNote::unsure:
            CHECK(result.outcome == evaluation::NoteOutcome::pitchNotJudged);
            REQUIRE(result.intonation.has_value());
            CHECK(result.intonation->verdict == IntonationVerdict::uncertain);
            break;
        case SungNote::missed:
            CHECK(result.outcome == evaluation::NoteOutcome::missed);
            break;
        }
    }
    CHECK(report.summary.sharp == 2);
    CHECK(report.summary.flat == 2);
    CHECK(report.summary.uncertain == 1);
    CHECK(report.summary.inTune == 9);
    CHECK(
        evaluation::summaryLine(report) == "9 in tune \xC2\xB7 2 sharp \xC2\xB7 2 flat \xC2\xB7 1 "
                                           "wrong \xC2\xB7 1 missed \xC2\xB7 1 uncertain");

    // The rows say it in words, and a sharp or flat note is a mistake.
    const std::vector<evaluation::ReviewRow> rows = evaluation::reviewRows(sung, report);
    const auto rowWith = [&rows](const std::string& text) -> const evaluation::ReviewRow*
    {
        for (const evaluation::ReviewRow& row : rows)
        {
            if (row.text.find(text) != std::string::npos)
            {
                return &row;
            }
        }
        return nullptr;
    };
    REQUIRE(rowWith(" \xC2\xA2 sharp") != nullptr);
    CHECK(rowWith(" \xC2\xA2 sharp")->mistake);
    REQUIRE(rowWith(" \xC2\xA2 flat") != nullptr);
    REQUIRE(rowWith("in tune") != nullptr);
    CHECK_FALSE(rowWith("in tune")->mistake);
    CHECK(rowWith("wrong, sang ") != nullptr);
    REQUIRE(rowWith("pitch uncertain") != nullptr);
    CHECK_FALSE(rowWith("pitch uncertain")->mistake);

    // A sung take picks the part it was sung from, as a played one does.
    CHECK(evaluation::bestPartFor(sung).part == 0);
}

TEST_CASE(
    "a stricter tolerance turns a slightly sharp note sharp",
    "[testcontrol][review][intonation]")
{
    // Every note 15 cents sharp: sung at A4 = 440 * 2^(15/1200), judged at 440.
    const double sharpReference = 440.0 * std::pow(2.0, 15.0 / 1200.0);
    take::Take sung =
        testcontrol::syntheticSungTake(voiceAndPiano(3), 0, 2, 44100.0, sharpReference);
    sung.referenceHz = 440.0;

    evaluation::IntonationSettings strict;
    strict.toleranceCents = 10.0;
    const evaluation::NoteReport loose = evaluation::judgeNotes(sung, 0);
    const evaluation::NoteReport tight = evaluation::judgeNotes(sung, 0, {}, strict);
    CHECK(loose.summary.sharp < tight.summary.sharp);
    CHECK(tight.summary.inTune == 0);
}
