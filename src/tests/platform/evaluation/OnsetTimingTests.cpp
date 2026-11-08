#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/evaluation/OnsetTiming.h"

#include "support/ScoreFixtures.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace evaluation;
using Catch::Approx;
using score::Tick;
using testing::score::notesOf;

namespace
{
constexpr double rate = 44100.0;
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

// `bars` bars of four quarters on one pitch -- repeated notes, the hard case
// for matching by anything but time -- at the given tempi.
[[nodiscard]] std::shared_ptr<const score::Score>
quarters(int bars, std::vector<score::TempoEntry> tempi = {{0, 120.0}})
{
    auto result = std::make_shared<score::Score>();
    result->tempoMap = score::TempoMap::build(std::move(tempi));
    score::Part part;
    for (int index = 0; index < bars; ++index)
    {
        score::Measure measure;
        measure.index = static_cast<std::size_t>(index);
        measure.start = static_cast<Tick>(index) * 4 * quarter;
        measure.nominalDuration = 4 * quarter;
        if (index == 0)
        {
            measure.attributes.time = score::TimeSignature{4, 4};
        }
        score::Voice voice;
        for (int beat = 0; beat < 4; ++beat)
        {
            voice.events.push_back(noteAt(static_cast<Tick>(beat) * quarter, quarter, 67));
        }
        measure.voices.push_back(voice);
        part.measures.push_back(measure);
    }
    result->parts.push_back(part);
    return result;
}

[[nodiscard]] std::shared_ptr<transport::ClockTable> tableOf(const score::Score& source)
{
    auto table = std::make_shared<transport::ClockTable>();
    transport::buildTable(*table, source, 100, rate);
    return table;
}

[[nodiscard]] std::int64_t ms(double milliseconds)
{
    return static_cast<std::int64_t>(std::llround(milliseconds * rate / 1000.0));
}

// Onsets at each attack's frame, shifted by `offsets` (ms), skipping NaNs.
[[nodiscard]] std::vector<PerformedOnset> onsetsAt(
    const std::vector<ExpectedAttack>& attacks,
    const transport::ClockTable& table,
    const std::vector<double>& offsets)
{
    std::vector<PerformedOnset> onsets;
    for (std::size_t index = 0; index < attacks.size() && index < offsets.size(); ++index)
    {
        if (std::isnan(offsets[index]))
        {
            continue;
        }
        const std::int64_t frame =
            transport::frameOfTick(table, static_cast<double>(attacks[index].tick));
        onsets.push_back(PerformedOnset{frame + ms(offsets[index]), 1.0, {}});
    }
    return onsets;
}
} // namespace

TEST_CASE("notes played on the beat are on time", "[evaluation][timing]")
{
    const auto source = quarters(2);
    const auto table = tableOf(*source);
    const auto attacks = expectedAttacksOf(*source, 0);
    const auto report = judgeTiming(
        attacks, onsetsAt(attacks, *table, std::vector<double>(8, 5.0)), *table, rate, 0);

    REQUIRE(report.results.size() == 8);
    CHECK(report.summary.onTime == 8);
    CHECK(report.summary.meanErrorMs == Approx(5.0).margin(0.1));
    CHECK(report.summary.errorSpreadMs == Approx(0.0).margin(0.1));
    CHECK(report.extra.empty());
}

TEST_CASE(
    "a tenth of a second late at 120 bpm is late by a fifth of a beat",
    "[evaluation][timing]")
{
    const auto source = quarters(1);
    const auto table = tableOf(*source);
    const auto attacks = expectedAttacksOf(*source, 0);
    const auto report = judgeTiming(
        attacks, onsetsAt(attacks, *table, {100.0, -100.0, 20.0, 60.0}), *table, rate, 0);

    REQUIRE(report.results.size() == 4);
    CHECK(report.results[0].verdict == TimingVerdict::late);
    CHECK(report.results[0].errorMs == Approx(100.0).margin(0.1));
    CHECK(report.results[0].errorBeats == Approx(0.2).margin(0.001));
    CHECK(report.results[1].verdict == TimingVerdict::early);
    CHECK(report.results[2].verdict == TimingVerdict::onTime);

    // 60 ms is beyond 120 bpm's tolerance of 40 ms.
    CHECK(report.results[3].verdict == TimingVerdict::late);
}

TEST_CASE("a missed repeated note is absent, and the others keep their own", "[evaluation][timing]")
{
    const auto source = quarters(1);
    const auto table = tableOf(*source);
    const auto attacks = expectedAttacksOf(*source, 0);
    const auto report =
        judgeTiming(attacks, onsetsAt(attacks, *table, {0.0, NAN, 10.0, -10.0}), *table, rate, 0);

    CHECK(report.results[0].verdict == TimingVerdict::onTime);
    CHECK(report.results[1].verdict == TimingVerdict::absent);
    CHECK_FALSE(report.results[1].onset.has_value());
    CHECK(report.results[2].verdict == TimingVerdict::onTime);
    CHECK(report.results[2].errorMs == Approx(10.0).margin(0.1));
    CHECK(report.results[3].verdict == TimingVerdict::onTime);
    CHECK(report.summary.absent == 1);
}

TEST_CASE("an extra sound between notes is left over, not matched", "[evaluation][timing]")
{
    const auto source = quarters(1);
    const auto table = tableOf(*source);
    const auto attacks = expectedAttacksOf(*source, 0);
    auto onsets = onsetsAt(attacks, *table, {0.0, 0.0, 0.0, 0.0});

    // Halfway between the first two beats.
    onsets.insert(onsets.begin() + 1, PerformedOnset{ms(250.0), 1.0, {}});
    const auto report = judgeTiming(attacks, onsets, *table, rate, 0);

    CHECK(report.summary.onTime == 4);
    REQUIRE(report.extra.size() == 1);
    CHECK(report.extra.front().frame == ms(250.0));
}

TEST_CASE("timing stays aligned through a tempo change", "[evaluation][timing]")
{
    // 120 bpm for a bar, then 60.
    const auto source = quarters(3, {{0, 120.0}, {4 * quarter, 60.0}});
    const auto table = tableOf(*source);
    const auto attacks = expectedAttacksOf(*source, 0);
    const auto report = judgeTiming(
        attacks, onsetsAt(attacks, *table, std::vector<double>(12, 20.0)), *table, rate, 0);

    CHECK(report.summary.onTime == 12);

    // At 60 bpm the same 20 ms is a smaller part of the beat, and the
    // tolerance is the 80 ms cap rather than 40.
    CHECK(report.results[0].errorBeats == Approx(0.04).margin(0.001));
    CHECK(report.results[8].errorBeats == Approx(0.02).margin(0.001));
    const auto late = judgeTiming(
        attacks, onsetsAt(attacks, *table, std::vector<double>(12, 60.0)), *table, rate, 0);
    CHECK(late.results[0].verdict == TimingVerdict::late);
    CHECK(late.results[8].verdict == TimingVerdict::onTime);
}

TEST_CASE("a weak onset makes its judgement uncertain", "[evaluation][timing]")
{
    const auto source = quarters(1);
    const auto table = tableOf(*source);
    const auto attacks = expectedAttacksOf(*source, 0);
    auto onsets = onsetsAt(attacks, *table, {0.0, 0.0, 0.0, 0.0});
    onsets[2].strength = 0.1;
    const auto report = judgeTiming(attacks, onsets, *table, rate, 0);
    CHECK_FALSE(report.results[1].uncertain);
    CHECK(report.results[2].uncertain);
}

TEST_CASE("latency is taken off every onset before judging", "[evaluation][timing]")
{
    const auto source = quarters(1);
    const auto table = tableOf(*source);
    const auto attacks = expectedAttacksOf(*source, 0);

    // Everything arrived 120 ms late, all of it latency.
    const auto report = judgeTiming(
        attacks, onsetsAt(attacks, *table, std::vector<double>(4, 120.0)), *table, rate, ms(120.0));
    CHECK(report.summary.onTime == 4);
    CHECK(report.summary.meanErrorMs == Approx(0.0).margin(0.1));
}

TEST_CASE("nothing played is every attack absent", "[evaluation][timing]")
{
    const auto source = quarters(1);
    const auto table = tableOf(*source);
    const auto report = judgeTiming(expectedAttacksOf(*source, 0), {}, *table, rate, 0);
    CHECK(report.summary.absent == 4);
    CHECK(report.summary.meanErrorMs == 0.0);
}

TEST_CASE("a keyboard take is judged from its note-ons", "[evaluation][timing]")
{
    take::Take recorded;
    recorded.score = quarters(1);
    recorded.table = tableOf(*recorded.score);
    recorded.sampleRate = rate;
    recorded.usedKeyboard = true;
    recorded.outputLatencyFrames = static_cast<int>(ms(10.0));
    const std::vector<double> offsets{10.0, 110.0, 10.0, -90.0};
    for (std::size_t beat = 0; beat < offsets.size(); ++beat)
    {
        take::KeyboardEvent key;
        key.frame = ms(500.0 * static_cast<double>(beat) + offsets[beat]);
        key.event.kind = midi::MidiEventKind::noteOn;
        key.event.noteNumber = 67;
        key.event.velocity = 80;
        recorded.keyboard.push_back(key);
    }

    const auto report = judgeTake(recorded, 0);
    REQUIRE(report.results.size() == 4);
    CHECK(report.results[0].verdict == TimingVerdict::onTime);
    CHECK(report.results[1].verdict == TimingVerdict::late);
    CHECK(report.results[1].errorMs == Approx(100.0).margin(0.1));
    CHECK(report.results[3].verdict == TimingVerdict::early);
}

TEST_CASE("a microphone take is judged from the onsets in its audio", "[evaluation][timing]")
{
    take::Take recorded;
    recorded.score = quarters(2);
    recorded.table = tableOf(*recorded.score);
    recorded.sampleRate = rate;
    recorded.usedMicrophone = true;

    // A bar of count-in, then a plucked note on each beat but the fifth.
    recorded.audioStart = -ms(2000.0);
    recorded.audio.assign(static_cast<std::size_t>(ms(6000.0)), 0.0f);
    constexpr double twoPi = 6.283185307179586;
    for (int beat = 0; beat < 8; ++beat)
    {
        if (beat == 4)
        {
            continue;
        }
        const auto start = static_cast<std::size_t>(ms(2000.0 + (500.0 * beat)));
        for (std::size_t index = start; index < recorded.audio.size(); ++index)
        {
            const double seconds = static_cast<double>(index - start) / rate;
            recorded.audio[index] += static_cast<float>(
                0.5 * std::exp(-seconds * 12.0) * std::sin(twoPi * 392.0 * seconds));
        }
    }

    const auto report = judgeTake(recorded, 0);
    REQUIRE(report.results.size() == 8);
    CHECK(report.summary.onTime == 7);
    CHECK(report.results[4].verdict == TimingVerdict::absent);
    CHECK(std::abs(report.summary.meanErrorMs) < 5.0);
}
