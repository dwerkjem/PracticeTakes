#include <catch2/catch_test_macros.hpp>

#include "platform/score/PartTimeline.h"

#include <string>
#include <vector>

using namespace score;

namespace
{
constexpr Tick quarter = ticksPerQuarterNote;

// One event in quarters: a rest without pitches.
struct Written
{
    double beats = 1.0;
    std::vector<int> midi;
    std::string lyric;
    SyllabicPosition syllable = SyllabicPosition::single;
    bool tiedFrom = false;
    bool grace = false;
    bool cue = false;
};

// One 4/4 measure per inner list.
[[nodiscard]] Score scoreOf(const std::vector<std::vector<Written>>& measures)
{
    Score result;
    result.tempoMap = TempoMap::build({{0, 120.0}});
    Part part;
    for (std::size_t index = 0; index < measures.size(); ++index)
    {
        Measure measure;
        measure.index = index;
        measure.start = static_cast<Tick>(index) * 4 * quarter;
        measure.nominalDuration = 4 * quarter;
        Voice voice;
        Tick onset = 0;
        for (const Written& written : measures[index])
        {
            ScoreEvent event;
            event.onset = onset;
            event.duration = written.grace ? 0 : static_cast<Tick>(written.beats * quarter);
            event.isGrace = written.grace;
            event.isCue = written.cue;
            event.kind = written.midi.empty()       ? EventKind::rest
                         : written.midi.size() == 1 ? EventKind::note
                                                    : EventKind::chord;
            for (const int midi : written.midi)
            {
                Pitch pitch;
                pitch.midiNoteNumber = midi;
                Note note{pitch, pitch, std::nullopt, std::nullopt};
                if (written.tiedFrom)
                {
                    note.tiedFrom = NoteRef{0, 0, 0, 0};
                }
                event.notes.push_back(note);
            }
            if (!written.lyric.empty())
            {
                event.lyrics.push_back(LyricSyllable{1, written.syllable, written.lyric});
            }
            voice.events.push_back(event);
            onset += event.duration;
        }
        measure.voices.push_back(voice);
        part.measures.push_back(measure);
    }
    result.parts.push_back(part);
    return result;
}
} // namespace

TEST_CASE("notes and rests become items in order", "[score][timeline]")
{
    const PartTimeline timeline =
        timelineOf(scoreOf({{{1, {67}}, {1, {}}, {1, {}}, {1, {69}}}}), 0);
    REQUIRE(timeline.size() == 3);
    CHECK(timeline[0].isNote());
    CHECK(timeline[0].start == 0);
    CHECK(timeline[0].end == quarter);

    // Two rests in a row are one.
    CHECK_FALSE(timeline[1].isNote());
    CHECK(timeline[1].start == quarter);
    CHECK(timeline[1].end == 3 * quarter);

    CHECK(timeline[2].pitches.front().midiNoteNumber == 69);
}

TEST_CASE("a tied note is one item, lasting to the end of its tie", "[score][timeline]")
{
    const PartTimeline timeline = timelineOf(
        scoreOf(
            {{{3, {67}}, {1, {67}}}, {{2, {67}, "", SyllabicPosition::single, true}, {2, {69}}}}),
        0);
    REQUIRE(timeline.size() == 3);
    CHECK(timeline[1].start == 3 * quarter);
    CHECK(timeline[1].end == 6 * quarter);
    CHECK(timeline[1].tiedOver);
    CHECK(timeline[2].start == 6 * quarter);
}

TEST_CASE("a note sung again is marked, even after a rest", "[score][timeline]")
{
    const PartTimeline timeline =
        timelineOf(scoreOf({{{1, {67}}, {1, {67}}, {1, {}}, {1, {67}}}, {{4, {69}}}}), 0);
    REQUIRE(timeline.size() == 5);
    CHECK_FALSE(timeline[0].repeated);
    CHECK(timeline[1].repeated);
    CHECK(timeline[3].repeated);
    CHECK_FALSE(timeline[4].repeated);
}

TEST_CASE("a phrase starts at the first note and after every rest", "[score][timeline]")
{
    const PartTimeline timeline =
        timelineOf(scoreOf({{{1, {67}}, {1, {69}}, {1, {}}, {1, {71}}}}), 0);
    REQUIRE(timeline.size() == 4);
    CHECK(timeline[0].phraseStart);
    CHECK_FALSE(timeline[1].phraseStart);
    CHECK(timeline[3].phraseStart);
}

TEST_CASE("an unwritten gap in the voice is a rest", "[score][timeline]")
{
    // The second measure's voice starts on beat 3.
    Score source = scoreOf({{{4, {67}}}, {{2, {69}}}});
    source.parts[0].measures[1].voices[0].events[0].onset = 2 * quarter;
    const PartTimeline timeline = timelineOf(source, 0);
    REQUIRE(timeline.size() == 3);
    CHECK_FALSE(timeline[1].isNote());
    CHECK(timeline[1].start == 4 * quarter);
    CHECK(timeline[1].end == 6 * quarter);
    CHECK(timeline[2].phraseStart);
}

TEST_CASE("a lyric carries a hyphen when the word goes on", "[score][timeline]")
{
    const PartTimeline timeline = timelineOf(
        scoreOf(
            {{{1, {67}, "blau", SyllabicPosition::begin},
              {1, {67}, "en", SyllabicPosition::end},
              {2, {69}, "Au", SyllabicPosition::begin}}}),
        0);
    REQUIRE(timeline.size() == 3);
    CHECK(timeline[0].lyric == "blau-");
    CHECK(timeline[1].lyric == "en");
    CHECK(timeline[2].lyric == "Au-");
}

TEST_CASE("a chord is one item, lowest first; grace and cue notes are not", "[score][timeline]")
{
    const PartTimeline timeline = timelineOf(
        scoreOf(
            {{{0, {74}, "", SyllabicPosition::single, false, true},
              {2, {67, 60, 64}},
              {2, {72}, "", SyllabicPosition::single, false, false, true}}}),
        0);
    REQUIRE_FALSE(timeline.empty());
    CHECK(timeline[0].pitches.size() == 3);
    CHECK(timeline[0].pitches.front().midiNoteNumber == 60);
    // The cue note's half is silence for this part.
    for (const TimelineItem& item : timeline)
    {
        if (item.isNote())
        {
            CHECK(item.pitches.front().midiNoteNumber != 72);
            CHECK(item.pitches.front().midiNoteNumber != 74);
        }
    }
}

TEST_CASE("the item at a tick, and the one after it", "[score][timeline]")
{
    const PartTimeline timeline = timelineOf(scoreOf({{{1, {67}}, {1, {}}, {2, {69}}}}), 0);
    REQUIRE(timeline.size() == 3);
    CHECK_FALSE(itemAt(timeline, -10.0).has_value());
    CHECK(itemAt(timeline, 0.0) == 0U);
    CHECK(itemAt(timeline, quarter - 0.5) == 0U);
    CHECK(itemAt(timeline, quarter) == 1U);
    CHECK(itemAt(timeline, 3.5 * quarter) == 2U);
    CHECK_FALSE(itemAt(timeline, 4.0 * quarter).has_value());

    CHECK(itemAfter(timeline, -10.0) == 0U);
    CHECK(itemAfter(timeline, 0.0) == 1U);
    CHECK_FALSE(itemAfter(timeline, 2.0 * quarter).has_value());
}

TEST_CASE("a part the score lacks has no timeline", "[score][timeline]")
{
    CHECK(timelineOf(scoreOf({{{4, {67}}}}), 3).empty());
}
