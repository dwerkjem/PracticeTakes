#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/evaluation/Intonation.h"
#include "platform/pitch/ReferencePitch.h"

#include <cmath>
#include <functional>
#include <vector>

using namespace evaluation;

namespace
{
constexpr double rate = 44100.0;
constexpr std::int64_t hop = 512;

// A contour from `from` to `to`, each frame's cents from A4 given by `centsAt`
// (seconds into the note), sure unless `confidenceAt` says otherwise.
[[nodiscard]] PitchContour contourOf(
    std::int64_t from,
    std::int64_t to,
    const std::function<double(double)>& centsAt,
    const std::function<double(double)>& confidenceAt = [](double) { return 0.95; },
    double referenceHz = 440.0)
{
    PitchContour contour;
    contour.hop = hop;
    for (std::int64_t frame = from; frame < to; frame += hop)
    {
        const double seconds = static_cast<double>(frame - from) / rate;
        ContourFrame point;
        point.frame = frame;
        point.hertz = pitch::midiToFrequency(69.0 + (centsAt(seconds) / 100.0), referenceHz);
        point.confidence = confidenceAt(seconds);
        point.rms = 0.1;
        contour.frames.push_back(point);
    }
    return contour;
}

constexpr std::int64_t second = 44100;
} // namespace

TEST_CASE("octaves fold into half an octave either way", "[evaluation][intonation]")
{
    CHECK(foldOctaves(10.0) == std::pair<double, int>{10.0, 0});
    CHECK(foldOctaves(-1190.0).first == Catch::Approx(10.0));
    CHECK(foldOctaves(-1190.0).second == -1);
    CHECK(foldOctaves(1230.0).first == Catch::Approx(30.0));
    CHECK(foldOctaves(1230.0).second == 1);
    CHECK(foldOctaves(-600.0).first == Catch::Approx(-600.0));
}

TEST_CASE("a steady note in tune scores high", "[evaluation][intonation]")
{
    const PitchContour contour = contourOf(0, second, [](double) { return 3.0; });
    const Intonation result = judgeIntonation(contour, 0, second, 69, 440.0, rate);
    CHECK(result.verdict == IntonationVerdict::inTune);
    CHECK(result.cents == Catch::Approx(3.0).margin(0.01));
    CHECK(result.score == Catch::Approx(1.0));
    CHECK(result.zone == pitch::IntonationZone::nearPerfect);
    CHECK(result.octave == 0);
    CHECK(result.sureFrames > 50);
}

TEST_CASE("consistently sharp and flat notes are told apart", "[evaluation][intonation]")
{
    const Intonation sharp = judgeIntonation(
        contourOf(0, second, [](double) { return 35.0; }), 0, second, 69, 440.0, rate);
    const Intonation flat = judgeIntonation(
        contourOf(0, second, [](double) { return -35.0; }), 0, second, 69, 440.0, rate);
    CHECK(sharp.verdict == IntonationVerdict::sharp);
    CHECK(sharp.cents == Catch::Approx(35.0).margin(0.01));
    CHECK(flat.verdict == IntonationVerdict::flat);
    CHECK(flat.cents == Catch::Approx(-35.0).margin(0.01));
    CHECK(sharp.score == Catch::Approx(0.0));
}

TEST_CASE("the tolerance decides sharp from in tune", "[evaluation][intonation]")
{
    const PitchContour contour = contourOf(0, second, [](double) { return 15.0; });
    IntonationSettings strict;
    strict.toleranceCents = 10.0;
    CHECK(
        judgeIntonation(contour, 0, second, 69, 440.0, rate).verdict == IntonationVerdict::inTune);
    CHECK(
        judgeIntonation(contour, 0, second, 69, 440.0, rate, strict).verdict ==
        IntonationVerdict::sharp);

    // Its zone moves with the tolerance too: blue at 25, amber at 10.
    CHECK(
        judgeIntonation(contour, 0, second, 69, 440.0, rate).zone ==
        pitch::IntonationZone::acceptable);
    CHECK(
        judgeIntonation(contour, 0, second, 69, 440.0, rate, strict).zone ==
        pitch::IntonationZone::nearButOff);
}

TEST_CASE("a scoop into the note does not decide it", "[evaluation][intonation]")
{
    // A semitone up from below over the first 100 ms, then held in tune, then
    // falling away in the last 60 ms.
    const auto scoop = [](double seconds)
    {
        if (seconds < 0.1)
        {
            return -100.0 + (1000.0 * seconds);
        }
        if (seconds > 0.44)
        {
            return -300.0;
        }
        return 4.0;
    };
    const std::int64_t end = second / 2;
    const Intonation result = judgeIntonation(contourOf(0, end, scoop), 0, end, 69, 440.0, rate);
    CHECK(result.verdict == IntonationVerdict::inTune);
    CHECK(result.cents == Catch::Approx(4.0).margin(0.01));

    // The scoop is kept, as attack.
    REQUIRE_FALSE(result.frames.empty());
    CHECK(result.frames.front().region == NoteRegion::attack);
    CHECK(result.frames.front().cents < -50.0);
    CHECK(result.frames.back().region == NoteRegion::release);
}

TEST_CASE("a note too unsure is uncertain, not wrong", "[evaluation][intonation]")
{
    const PitchContour contour =
        contourOf(0, second, [](double) { return 200.0; }, [](double) { return 0.5; });
    const Intonation result = judgeIntonation(contour, 0, second, 69, 440.0, rate);
    CHECK(result.verdict == IntonationVerdict::uncertain);
    CHECK_FALSE(result.sungMidi.has_value());
    CHECK(result.sureFrames == 0);
}

TEST_CASE("unsure frames are left out of the median", "[evaluation][intonation]")
{
    // Every fourth frame is a wild, unsure reading.
    int count = 0;
    const PitchContour contour = contourOf(
        0, second, [&count](double) { return (count++ % 4 == 0) ? 700.0 : -30.0; },
        [](double) { return 0.95; });
    PitchContour mixed = contour;
    for (std::size_t index = 0; index < mixed.frames.size(); index += 4)
    {
        mixed.frames[index].confidence = 0.3;
    }
    const Intonation result = judgeIntonation(mixed, 0, second, 69, 440.0, rate);
    CHECK(result.verdict == IntonationVerdict::flat);
    CHECK(result.cents == Catch::Approx(-30.0).margin(0.01));
}

TEST_CASE("a note nearer another is wrong, with what was sung", "[evaluation][intonation]")
{
    const Intonation result = judgeIntonation(
        contourOf(0, second, [](double) { return 190.0; }), 0, second, 69, 440.0, rate);
    CHECK(result.verdict == IntonationVerdict::wrong);
    REQUIRE(result.sungMidi.has_value());
    CHECK(*result.sungMidi == 71);
}

TEST_CASE("an octave low in tune is in tune, and says so", "[evaluation][intonation]")
{
    const Intonation result = judgeIntonation(
        contourOf(0, second, [](double) { return -1205.0; }), 0, second, 69, 440.0, rate);
    CHECK(result.verdict == IntonationVerdict::inTune);
    CHECK(result.cents == Catch::Approx(-5.0).margin(0.01));
    CHECK(result.octave == -1);
}

TEST_CASE("a tracker's octave slips do not move the verdict", "[evaluation][intonation]")
{
    int count = 0;
    const Intonation result = judgeIntonation(
        contourOf(0, second, [&count](double) { return (count++ % 3 == 0) ? 1210.0 : 10.0; }), 0,
        second, 69, 440.0, rate);
    CHECK(result.verdict == IntonationVerdict::inTune);
    CHECK(result.cents == Catch::Approx(10.0).margin(0.01));
    CHECK(result.octave == 0);
}

TEST_CASE(
    "the reference the take was sung at is the one judged against",
    "[evaluation][intonation]")
{
    // A4 sung at A442 reads 7.85 cents sharp at A440.
    const PitchContour contour =
        contourOf(0, second, [](double) { return 0.0; }, [](double) { return 0.95; }, 442.0);
    const Intonation at442 = judgeIntonation(contour, 0, second, 69, 442.0, rate);
    const Intonation at440 = judgeIntonation(contour, 0, second, 69, 440.0, rate);
    CHECK(at442.cents == Catch::Approx(0.0).margin(0.01));
    CHECK(at440.cents == Catch::Approx(7.85).margin(0.05));
}

TEST_CASE("a short note is judged on its middle half", "[evaluation][intonation]")
{
    // 120 ms: a 40 ms attack and a 20 ms release leave less than half.
    const std::int64_t end = (second * 12) / 100;
    const Intonation result =
        judgeIntonation(contourOf(0, end, [](double) { return -40.0; }), 0, end, 69, 440.0, rate);
    CHECK(result.sustainFrames > 0);
    CHECK(result.sustainFrames <= 6);
}

TEST_CASE("an extra note's pitch is read just after it", "[evaluation][intonation]")
{
    const PitchContour contour = contourOf(0, second / 2, [](double) { return 210.0; });
    const auto found = pitchAfterOnset(contour, 0, 440.0, rate);
    REQUIRE(found.has_value());
    CHECK(*found == 71);

    const PitchContour unsure =
        contourOf(0, second / 2, [](double) { return 0.0; }, [](double) { return 0.4; });
    CHECK_FALSE(pitchAfterOnset(unsure, 0, 440.0, rate).has_value());
}
