#include <catch2/catch_test_macros.hpp>

#include "platform/transport/PlaybackEvents.h"
#include "platform/transport/PlaybackMix.h"

#include <memory>
#include <vector>

using namespace transport;

namespace
{
constexpr score::Tick quarter = score::ticksPerQuarterNote;
constexpr Frame beat = 22050; // 120 bpm at 44.1 kHz

[[nodiscard]] score::ScoreEvent
noteAt(score::Tick onset, score::Tick duration, int midi, int staff = 1)
{
    score::ScoreEvent event;
    event.kind = score::EventKind::note;
    event.onset = onset;
    event.duration = duration;
    event.staff = staff;
    score::Pitch pitch;
    pitch.midiNoteNumber = midi;
    event.notes.push_back(score::Note{pitch, pitch, std::nullopt, std::nullopt});
    return event;
}

// Two bars of 4/4 at 120 bpm: a voice, and a piano on two staves.
struct Fixture
{
    score::Score source;
    ClockTable table;

    Fixture()
    {
        source.tempoMap = score::TempoMap::build({{0, 120.0}});

        score::Part voice;
        voice.midiProgram = 52;
        score::Part piano;
        piano.staffCount = 2;
        for (std::size_t bar = 0; bar < 2; ++bar)
        {
            score::Measure measure;
            measure.index = bar;
            measure.start = static_cast<score::Tick>(bar) * 4 * quarter;
            measure.nominalDuration = 4 * quarter;
            if (bar == 0)
            {
                measure.attributes.time = score::TimeSignature{4, 4};
            }
            voice.measures.push_back(measure);
            piano.measures.push_back(measure);
        }

        // Voice: a half note tied over the bar to a quarter, then a rest.
        score::Voice sung;
        sung.events = {noteAt(2 * quarter, 2 * quarter, 72)};
        sung.events.front().notes.front().tiedTo = score::NoteRef{1, 0, 0, 0};
        voice.measures[0].voices.push_back(sung);
        score::Voice continued;
        continued.events = {noteAt(0, quarter, 72)};
        continued.events.front().notes.front().tiedFrom = score::NoteRef{0, 0, 0, 0};
        voice.measures[1].voices.push_back(continued);

        // Piano: a right-hand quarter on beat 1 with a grace note before it,
        // and a left-hand whole note; the same key twice in a row.
        score::Voice right;
        score::ScoreEvent grace = noteAt(0, 0, 74);
        grace.isGrace = true;
        right.events = {grace, noteAt(0, quarter, 76), noteAt(quarter, quarter, 76)};
        score::Voice left;
        left.events = {noteAt(0, 4 * quarter, 48, 2)};
        piano.measures[0].voices = {right, left};

        source.parts = {voice, piano};
        buildTable(table, source, 100, 44100.0);
    }
};

[[nodiscard]] std::vector<PlaybackEvent> eventsOn(const PlaybackScore& built, std::size_t track)
{
    std::vector<PlaybackEvent> found;
    for (const PlaybackEvent& event : built.events)
    {
        if (event.track == track)
        {
            found.push_back(event);
        }
    }
    return found;
}
} // namespace

TEST_CASE("every staff of every part is a track with its program", "[transport][playback]")
{
    const Fixture fixture;
    const std::vector<PlaybackTrack> tracks = playbackTracksOf(fixture.source);
    REQUIRE(tracks.size() == 3);
    CHECK(tracks[0].part == 0);
    CHECK(tracks[0].program == 52);
    CHECK(tracks[1].part == 1);
    CHECK(tracks[1].staff == 1);
    CHECK(tracks[1].program == defaultMidiProgram);
    CHECK(tracks[2].staff == 2);
}

TEST_CASE("a tied note sounds once, to the end of its tie", "[transport][playback]")
{
    const Fixture fixture;
    PlaybackScore built;
    playbackEventsOf(fixture.source, fixture.table, built);

    const std::vector<PlaybackEvent> voice = eventsOn(built, 0);
    REQUIRE(voice.size() == 2);
    CHECK(voice[0].on);
    CHECK(voice[0].key == 72);
    CHECK(voice[0].frame == 2 * beat);
    CHECK_FALSE(voice[1].on);
    CHECK(voice[1].frame == 5 * beat);
}

TEST_CASE("each hand is its own track, and grace notes are not played", "[transport][playback]")
{
    const Fixture fixture;
    PlaybackScore built;
    playbackEventsOf(fixture.source, fixture.table, built);

    const std::vector<PlaybackEvent> right = eventsOn(built, 1);
    REQUIRE(right.size() == 4);
    for (const PlaybackEvent& event : right)
    {
        CHECK(event.key == 76);
    }
    const std::vector<PlaybackEvent> left = eventsOn(built, 2);
    REQUIRE(left.size() == 2);
    CHECK(left[0].key == 48);
    CHECK(left[1].frame == 4 * beat);
}

TEST_CASE(
    "events are in time order, offs first, so a repeat strikes again",
    "[transport][playback]")
{
    const Fixture fixture;
    PlaybackScore built;
    playbackEventsOf(fixture.source, fixture.table, built);

    for (std::size_t index = 1; index < built.events.size(); ++index)
    {
        CHECK(built.events[index - 1].frame <= built.events[index].frame);
    }

    // The right hand's E5 ends on beat 2 as the next one starts.
    const std::vector<PlaybackEvent> right = eventsOn(built, 1);
    CHECK(right[1].frame == beat);
    CHECK_FALSE(right[1].on);
    CHECK(right[2].frame == beat);
    CHECK(right[2].on);
}

TEST_CASE("the notes follow the tempo scale", "[transport][playback]")
{
    Fixture fixture;
    buildTable(fixture.table, fixture.source, 50, 44100.0);
    PlaybackScore built;
    playbackEventsOf(fixture.source, fixture.table, built);
    CHECK(eventsOn(built, 0).front().frame == 4 * beat);
}

TEST_CASE("the mix folds into one mask of the tracks that sound", "[transport][playback]")
{
    PlaybackMix mix;
    mix.setTrackCount(3);
    CHECK(mix.audibleTracks() == 0);

    mix.enabled = true;
    CHECK(mix.audibleTracks() == 0b111);

    // Practising the right hand: mute it.
    mix.setMuted(1, true);
    CHECK(mix.audibleTracks() == 0b101);

    // Solo the left hand: only it, whatever else is unmuted.
    mix.setSoloed(2, true);
    CHECK(mix.audibleTracks() == 0b100);

    // A soloed track that is also muted stays silent.
    mix.setMuted(2, true);
    CHECK(mix.audibleTracks() == 0);

    // A new score clears both.
    mix.setTrackCount(2);
    CHECK(mix.audibleTracks() == 0b11);
    CHECK_FALSE(mix.isMuted(1));
}
