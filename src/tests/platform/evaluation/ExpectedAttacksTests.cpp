#include <catch2/catch_test_macros.hpp>

#include "platform/evaluation/ExpectedAttacks.h"

#include "support/ScoreFixtures.h"

using namespace evaluation;
using score::Tick;
using testing::score::notesOf;

namespace
{
constexpr Tick quarter = score::ticksPerQuarterNote;

[[nodiscard]] score::ScoreEvent noteAt(Tick onset, Tick duration, int midi)
{
    score::ScoreEvent event;
    event.kind = score::EventKind::note;
    event.onset = onset;
    event.duration = duration;
    score::Pitch pitch;
    pitch.midiNoteNumber = midi;
    event.notes = notesOf({pitch});
    return event;
}

[[nodiscard]] score::Score scoreOf(std::vector<std::vector<score::ScoreEvent>> voices, int bars = 1)
{
    score::Score result;
    score::Part part;
    for (int index = 0; index < bars; ++index)
    {
        score::Measure measure;
        measure.index = static_cast<std::size_t>(index);
        measure.start = static_cast<Tick>(index) * 4 * quarter;
        measure.nominalDuration = 4 * quarter;
        if (index == 0)
        {
            for (std::size_t voice = 0; voice < voices.size(); ++voice)
            {
                score::Voice built;
                built.number = static_cast<int>(voice + 1);
                built.events = voices.at(voice);
                measure.voices.push_back(built);
            }
        }
        part.measures.push_back(measure);
    }
    result.parts.push_back(part);
    return result;
}
} // namespace

TEST_CASE("each onset of a part is an expected attack", "[evaluation][attacks]")
{
    const auto source = scoreOf(
        {{noteAt(0, quarter, 60), noteAt(quarter, quarter, 62),
          noteAt(quarter * 2, quarter * 2, 64)}});
    const auto attacks = expectedAttacksOf(source, 0);
    REQUIRE(attacks.size() == 3);
    CHECK(attacks.at(1).tick == quarter);
    CHECK(attacks.at(1).notes.size() == 1);
    CHECK(attacks.at(1).notes.front().midiNoteNumber == 62);
    CHECK(attacks.at(2).notes.front().ref.eventIndex == 2);
}

TEST_CASE("a tied note's continuation is not an attack", "[evaluation][attacks]")
{
    score::ScoreEvent held = noteAt(0, quarter * 2, 60);
    score::ScoreEvent continued = noteAt(quarter * 2, quarter, 60);
    continued.notes.front().tiedFrom = score::NoteRef{0, 0, 0, 0};
    const auto source = scoreOf({{held, continued, noteAt(quarter * 3, quarter, 62)}});

    const auto attacks = expectedAttacksOf(source, 0);
    REQUIRE(attacks.size() == 2);
    CHECK(attacks.at(0).tick == 0);
    CHECK(attacks.at(1).tick == quarter * 3);
}

TEST_CASE("rests and grace notes are not attacks", "[evaluation][attacks]")
{
    score::ScoreEvent rest;
    rest.kind = score::EventKind::rest;
    rest.onset = 0;
    rest.duration = quarter;
    score::ScoreEvent grace = noteAt(quarter, 0, 61);
    grace.isGrace = true;
    const auto source = scoreOf({{rest, grace, noteAt(quarter, quarter, 62)}});

    const auto attacks = expectedAttacksOf(source, 0);
    REQUIRE(attacks.size() == 1);
    CHECK(attacks.front().tick == quarter);
    CHECK(attacks.front().notes.front().midiNoteNumber == 62);
}

TEST_CASE("cue notes are not attacks", "[evaluation][attacks]")
{
    // An ossia is printed for reference; nobody is expected to play it.
    score::ScoreEvent cue = noteAt(0, quarter, 64);
    cue.isCue = true;
    const auto source = scoreOf({{cue, noteAt(quarter, quarter, 62)}});

    const auto attacks = expectedAttacksOf(source, 0);
    REQUIRE(attacks.size() == 1);
    CHECK(attacks.front().notes.front().midiNoteNumber == 62);
}

TEST_CASE("a chord and voices starting together are one moment", "[evaluation][attacks]")
{
    score::ScoreEvent chord = noteAt(0, quarter, 60);
    chord.kind = score::EventKind::chord;
    score::Pitch upper;
    upper.midiNoteNumber = 64;
    chord.notes.push_back(score::Note{upper, upper, std::nullopt, std::nullopt});
    const auto source = scoreOf({{chord}, {noteAt(0, quarter * 2, 48)}});

    const auto attacks = expectedAttacksOf(source, 0);
    REQUIRE(attacks.size() == 1);
    CHECK(attacks.front().notes.size() == 3);
}

TEST_CASE("a part that is not there has no attacks", "[evaluation][attacks]")
{
    CHECK(expectedAttacksOf(scoreOf({{noteAt(0, quarter, 60)}}), 3).empty());
}
