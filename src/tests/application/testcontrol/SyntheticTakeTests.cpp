#include <catch2/catch_test_macros.hpp>

#include "application/testcontrol/SyntheticTake.h"
#include "platform/evaluation/TakeReview.h"

#include <memory>
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
