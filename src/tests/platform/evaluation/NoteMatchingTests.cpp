#include <catch2/catch_test_macros.hpp>

#include "platform/evaluation/NoteMatching.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace evaluation;

namespace
{
// A moment at `tick` with written pitches, matched (or not) to an onset with
// played pitches.
[[nodiscard]] TimingResult moment(
    score::Tick tick,
    std::vector<int> written,
    std::optional<std::vector<int>> played,
    std::size_t measure = 0)
{
    TimingResult result;
    result.attack.tick = tick;
    for (std::size_t index = 0; index < written.size(); ++index)
    {
        result.attack.notes.push_back(ExpectedNote{
            score::NoteRef{measure, 0, static_cast<std::size_t>(tick), index}, written[index]});
    }
    if (played.has_value())
    {
        result.onset = PerformedOnset{static_cast<std::int64_t>(tick) * 10, 1.0, *played};
        result.verdict = TimingVerdict::onTime;
    }
    return result;
}

[[nodiscard]] const NoteResult& written(const NoteReport& report, int midi)
{
    const auto found = std::find_if(
        report.results.begin(), report.results.end(),
        [midi](const NoteResult& result) { return result.writtenMidi == midi; });
    REQUIRE(found != report.results.end());
    return *found;
}
} // namespace

TEST_CASE("notes played at their pitch are correct", "[evaluation][notes]")
{
    TimingReport timing;
    timing.results = {moment(0, {60, 64, 67}, std::vector<int>{67, 60, 64})};
    const NoteReport report = matchNotes(timing, 0);
    CHECK(report.summary.correct == 3);
    CHECK(report.extra.empty());
    CHECK(report.results.front().timing == TimingVerdict::onTime);
}

TEST_CASE("a wrong note in a chord keeps what was played", "[evaluation][notes]")
{
    TimingReport timing;
    timing.results = {moment(0, {60, 64, 67}, std::vector<int>{60, 64, 69})};
    const NoteReport report = matchNotes(timing, 0);
    CHECK(report.summary.correct == 2);
    CHECK(report.summary.wrong == 1);
    const NoteResult& g = written(report, 67);
    CHECK(g.outcome == NoteOutcome::wrong);
    CHECK(g.playedMidi == 69);
    CHECK_FALSE(g.ambiguous);
}

TEST_CASE("a moment nobody played is missed, each note once", "[evaluation][notes]")
{
    TimingReport timing;
    timing.results = {
        moment(0, {67}, std::vector<int>{67}), moment(1, {67}, std::nullopt),
        moment(2, {67}, std::vector<int>{67}), moment(3, {67}, std::vector<int>{67})};
    const NoteReport report = matchNotes(timing, 0);
    CHECK(report.summary.correct == 3);
    CHECK(report.summary.missed == 1);
    CHECK(report.results.at(1).outcome == NoteOutcome::missed);
}

TEST_CASE("a stray key is extra, and attached to nothing", "[evaluation][notes]")
{
    TimingReport timing;
    timing.results = {moment(0, {60}, std::vector<int>{60}), moment(2, {62}, std::vector<int>{62})};
    timing.extra = {PerformedOnset{10, 1.0, {71}}};
    const NoteReport report = matchNotes(timing, 0);
    CHECK(report.summary.correct == 2);
    REQUIRE(report.extra.size() == 1);
    CHECK(report.extra.front().midi == 71);
    CHECK(report.extra.front().frame == 10);
}

TEST_CASE(
    "more keys than notes leaves the rest extra; fewer leaves notes missed",
    "[evaluation][notes]")
{
    TimingReport more;
    more.results = {moment(0, {60}, std::vector<int>{60, 72})};
    const NoteReport extra = matchNotes(more, 0);
    CHECK(extra.summary.correct == 1);
    CHECK(extra.summary.extra == 1);

    TimingReport fewer;
    fewer.results = {moment(0, {60, 64, 67}, std::vector<int>{60})};
    const NoteReport missing = matchNotes(fewer, 0);
    CHECK(missing.summary.correct == 1);
    CHECK(missing.summary.missed == 2);
}

TEST_CASE("a pairing with an equal alternative is ambiguous", "[evaluation][notes]")
{
    // D is as near C as it is E: which one was meant is a guess.
    TimingReport timing;
    timing.results = {moment(0, {60, 64}, std::vector<int>{62})};
    const NoteReport report = matchNotes(timing, 0);
    CHECK(report.summary.wrong == 1);
    CHECK(report.summary.missed == 1);
    const NoteResult& c = written(report, 60);
    CHECK(c.outcome == NoteOutcome::wrong);
    CHECK(c.ambiguous);

    // With E played as well, D can only stand for C.
    TimingReport clear;
    clear.results = {moment(0, {60, 64}, std::vector<int>{62, 64})};
    const NoteReport settled = matchNotes(clear, 0);
    CHECK(written(settled, 60).outcome == NoteOutcome::wrong);
    CHECK_FALSE(written(settled, 60).ambiguous);
}

TEST_CASE("a microphone onset's notes are played but not judged for pitch", "[evaluation][notes]")
{
    TimingReport timing;
    timing.results = {moment(0, {60, 64}, std::vector<int>{})};
    timing.extra = {PerformedOnset{99, 1.0, {}}};
    const NoteReport report = matchNotes(timing, 0);
    CHECK(report.summary.pitchNotJudged == 2);
    CHECK(report.summary.wrong == 0);
    REQUIRE(report.extra.size() == 1);
    CHECK_FALSE(report.extra.front().midi.has_value());
}

TEST_CASE("a keyboard take is matched end to end", "[evaluation][notes]")
{
    // Four quarters at 120 bpm: G G A G, played G G B G, the last missed.
    auto source = std::make_shared<score::Score>();
    source->tempoMap = score::TempoMap::build({{0, 120.0}});
    score::Part part;
    score::Measure measure;
    measure.nominalDuration = 4 * score::ticksPerQuarterNote;
    measure.attributes.time = score::TimeSignature{4, 4};
    score::Voice voice;
    const std::vector<int> pitches{67, 67, 69, 67};
    for (std::size_t beat = 0; beat < pitches.size(); ++beat)
    {
        score::ScoreEvent event;
        event.kind = score::EventKind::note;
        event.onset = static_cast<score::Tick>(beat) * score::ticksPerQuarterNote;
        event.duration = score::ticksPerQuarterNote;
        score::Pitch pitch;
        pitch.midiNoteNumber = pitches[beat];
        event.notes.push_back(score::Note{pitch, pitch, std::nullopt, std::nullopt});
        voice.events.push_back(event);
    }
    measure.voices.push_back(voice);
    part.measures.push_back(measure);
    source->parts.push_back(part);

    take::Take recorded;
    recorded.score = source;
    auto table = std::make_shared<transport::ClockTable>();
    transport::buildTable(*table, *source, 100, 44100.0);
    recorded.table = table;
    recorded.usedKeyboard = true;
    const std::vector<int> playedPitches{67, 67, 71};
    for (std::size_t beat = 0; beat < playedPitches.size(); ++beat)
    {
        take::KeyboardEvent key;
        key.frame = static_cast<std::int64_t>(beat) * 22050;
        key.event.kind = midi::MidiEventKind::noteOn;
        key.event.noteNumber = playedPitches[beat];
        key.event.velocity = 70;
        recorded.keyboard.push_back(key);
    }

    const NoteReport report = judgeNotes(recorded, 0);
    REQUIRE(report.results.size() == 4);
    CHECK(report.results[0].outcome == NoteOutcome::correct);
    CHECK(report.results[1].outcome == NoteOutcome::correct);
    CHECK(report.results[2].outcome == NoteOutcome::wrong);
    CHECK(report.results[2].playedMidi == 71);
    CHECK(report.results[3].outcome == NoteOutcome::missed);
    CHECK(report.summary.extra == 0);
}

TEST_CASE("only what a take covered is judged", "[evaluation][notes]")
{
    // Four bars of quarters at 120 bpm; the take started in bar 2 and stopped
    // at the start of bar 4.
    auto source = std::make_shared<score::Score>();
    source->tempoMap = score::TempoMap::build({{0, 120.0}});
    score::Part part;
    for (int bar = 0; bar < 4; ++bar)
    {
        score::Measure measure;
        measure.index = static_cast<std::size_t>(bar);
        measure.start = static_cast<score::Tick>(bar) * 4 * score::ticksPerQuarterNote;
        measure.nominalDuration = 4 * score::ticksPerQuarterNote;
        if (bar == 0)
        {
            measure.attributes.time = score::TimeSignature{4, 4};
        }
        score::Voice voice;
        for (int beat = 0; beat < 4; ++beat)
        {
            score::ScoreEvent event;
            event.kind = score::EventKind::note;
            event.onset = static_cast<score::Tick>(beat) * score::ticksPerQuarterNote;
            event.duration = score::ticksPerQuarterNote;
            score::Pitch pitch;
            pitch.midiNoteNumber = 60;
            event.notes.push_back(score::Note{pitch, pitch, std::nullopt, std::nullopt});
            voice.events.push_back(event);
        }
        measure.voices.push_back(voice);
        part.measures.push_back(measure);
    }
    source->parts.push_back(part);

    take::Take recorded;
    recorded.score = source;
    auto table = std::make_shared<transport::ClockTable>();
    transport::buildTable(*table, *source, 100, 44100.0);
    recorded.table = table;
    recorded.startTick = 4 * score::ticksPerQuarterNote;
    recorded.lastFrame = 3 * 4 * 22050;
    recorded.usedKeyboard = true;
    take::KeyboardEvent key;
    key.frame = 4 * 22050;
    key.event.kind = midi::MidiEventKind::noteOn;
    key.event.noteNumber = 60;
    key.event.velocity = 70;
    recorded.keyboard.push_back(key);

    // Bars 2 and 3: eight notes, one played.
    const NoteReport report = judgeNotes(recorded, 0);
    CHECK(report.results.size() == 8);
    CHECK(report.summary.correct == 1);
    CHECK(report.summary.missed == 7);
    CHECK(report.results.front().tick == 4 * score::ticksPerQuarterNote);
}
