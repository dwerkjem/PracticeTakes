#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/ScoreFixtures.h"

#include "platform/score/ScoreTraversal.h"

using namespace score;
using testing::score::notesOf;

namespace
{
[[nodiscard]] ScoreEvent noteAt(Tick onset, Tick duration, Step step, int octave)
{
    ScoreEvent event;
    event.kind = EventKind::note;
    event.onset = onset;
    event.duration = duration;
    event.notes = notesOf({pitchFromSpelling(step, 0, octave)});

    return event;
}

// Two voices whose events interleave in time, which is the case a walk that
// takes "the first voice that has a note" gets wrong.
//
// Voice 5 -- not 2, because Voice::number is the file's own numbering and not an
// index -- enters on beat one; voice 1 rests until beat two. A traversal
// ordering by onset must yield voice 5's note first even though voice 1 sorts
// lower, and the tie-break must only decide events that genuinely share an
// onset.
[[nodiscard]] Measure makeInterleavedMeasure()
{
    Voice lower;
    lower.number = 1;
    lower.events = {
        noteAt(ticksPerQuarterNote, ticksPerQuarterNote, Step::e, 4),
        noteAt(2 * ticksPerQuarterNote, ticksPerQuarterNote, Step::f, 4)};

    Voice upper;
    upper.number = 5;
    upper.events = {
        noteAt(0, ticksPerQuarterNote, Step::c, 5),
        noteAt(2 * ticksPerQuarterNote, ticksPerQuarterNote, Step::d, 5)};

    Measure measure;
    measure.printedNumber = "1";
    measure.index = 0;
    measure.start = 0;
    measure.nominalDuration = nominalMeasureTicks(4, 4);
    measure.voices = {lower, upper};

    return measure;
}

[[nodiscard]] Part makeTwoMeasurePart()
{
    Measure first = makeInterleavedMeasure();

    Voice single;
    single.number = 1;
    single.events = {noteAt(0, ticksPerQuarterNote, Step::g, 4)};

    Measure second;
    second.printedNumber = "2";
    second.index = 1;
    second.start = first.nominalDuration;
    second.nominalDuration = first.nominalDuration;
    second.voices = {single};

    Part part;
    part.id = "P1";
    part.measures = {first, second};

    return part;
}
} // namespace

TEST_CASE("an event's absolute onset is the measure start plus its onset", "[score][traversal]")
{
    const Part part = makeTwoMeasurePart();

    const Measure& first = part.measures[0];
    const Measure& second = part.measures[1];

    // In bar 1 the sum is invisible: the measure starts at zero, so a consumer
    // that forgot it would still be right. That is finding 4's whole point, so
    // both bars are checked and the second is the one that matters.
    CHECK(absoluteOnset(first, first.voices[0].events[0]) == ticksPerQuarterNote);
    CHECK(absoluteOnset(second, second.voices[0].events[0]) == second.start);
    CHECK(second.voices[0].events[0].onset == 0);
}

TEST_CASE("measure event order is by onset across voices", "[score][traversal]")
{
    const Measure measure = makeInterleavedMeasure();

    const std::vector<MeasureEventIndex> order = measureEventOrder(measure);

    REQUIRE(order.size() == 4);

    std::vector<Tick> onsets;
    std::vector<int> voiceNumbers;

    for (const MeasureEventIndex& position : order)
    {
        const Voice& voice = measure.voices[position.voiceIndex];
        onsets.push_back(voice.events[position.eventIndex].onset);
        voiceNumbers.push_back(voice.number);
    }

    CHECK(
        onsets == std::vector<Tick>{
                      0, ticksPerQuarterNote, 2 * ticksPerQuarterNote, 2 * ticksPerQuarterNote});

    // The voice that enters first is yielded first even though its number is
    // higher, and the tie at beat three goes to the lower number.
    CHECK(voiceNumbers == std::vector<int>{5, 1, 1, 5});
}

TEST_CASE("traversal crosses measures in order and carries context", "[score][traversal]")
{
    const Part part = makeTwoMeasurePart();

    std::vector<Tick> absolute;
    std::vector<std::size_t> measureIndices;
    std::vector<std::string> barNumbers;

    forEachEvent(
        part,
        [&](const EventVisit& visited)
        {
            absolute.push_back(visited.absoluteOnset);
            measureIndices.push_back(visited.measureIndex);
            barNumbers.push_back(visited.measure.printedNumber);
        });

    const Tick barLength = part.measures[0].nominalDuration;

    CHECK(
        absolute ==
        std::vector<Tick>{
            0, ticksPerQuarterNote, 2 * ticksPerQuarterNote, 2 * ticksPerQuarterNote, barLength});
    CHECK(measureIndices == std::vector<std::size_t>{0, 0, 0, 0, 1});
    CHECK(barNumbers.back() == "2");

    // Every event of measure n precedes every event of measure n+1, which a
    // check on ascending absolute onsets alone would not catch if two measures
    // overlapped.
    CHECK(std::is_sorted(absolute.begin(), absolute.end()));
}

TEST_CASE("a traversal visitor can stop early", "[score][traversal]")
{
    const Part part = makeTwoMeasurePart();

    std::vector<Tick> absolute;

    forEachEvent(
        part,
        [&](const EventVisit& visited)
        {
            absolute.push_back(visited.absoluteOnset);

            return absolute.size() < 2;
        });

    CHECK(absolute == std::vector<Tick>{0, ticksPerQuarterNote});
}

TEST_CASE("traversal yields the voice an event belongs to", "[score][traversal]")
{
    const Part part = makeTwoMeasurePart();

    std::vector<int> voiceNumbers;

    forEachEvent(
        part, [&](const EventVisit& visited) { voiceNumbers.push_back(visited.voice.number); });

    CHECK(voiceNumbers == std::vector<int>{5, 1, 1, 5, 1});
}

TEST_CASE("an empty part visits nothing", "[score][traversal]")
{
    const Part part;

    int visits = 0;

    forEachEvent(part, [&](const EventVisit&) { ++visits; });

    CHECK(visits == 0);
}

TEST_CASE("a chord keeps the order the source wrote it in", "[score][traversal][model]")
{
    // Written top-down, which some exporters do. Nothing sorts it, and the
    // contract in ScoreEvent.h says so: notes.front() is not the lowest.
    ScoreEvent chord;
    chord.kind = EventKind::chord;
    chord.onset = 0;
    chord.duration = ticksPerQuarterNote;
    chord.notes = notesOf(
        {pitchFromSpelling(Step::g, 0, 4), pitchFromSpelling(Step::e, 0, 4),
         pitchFromSpelling(Step::c, 0, 4)});

    REQUIRE(chord.notes.size() == 3);

    CHECK(chord.notes.front().sounding.midiNoteNumber > chord.notes.back().sounding.midiNoteNumber);
}
