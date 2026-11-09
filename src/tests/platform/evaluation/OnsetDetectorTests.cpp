#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/evaluation/OnsetDetector.h"

#include <cmath>
#include <cstdlib>
#include <random>
#include <vector>

using namespace evaluation;

namespace
{
constexpr double rate = 44100.0;
constexpr double twoPi = 6.283185307179586;

// A plucked note: a sine at `frequency` from `start`, decaying.
void addPluck(std::vector<float>& audio, std::size_t start, double frequency, double level = 0.5)
{
    for (std::size_t index = start; index < audio.size(); ++index)
    {
        const double seconds = static_cast<double>(index - start) / rate;
        audio[index] += static_cast<float>(
            level * std::exp(-seconds * 6.0) * std::sin(twoPi * frequency * seconds));
    }
}

[[nodiscard]] bool
foundNear(const std::vector<PerformedOnset>& onsets, std::int64_t frame, double tolerance)
{
    const auto allowed = static_cast<std::int64_t>(tolerance * rate);
    for (const PerformedOnset& onset : onsets)
    {
        if (std::llabs(onset.frame - frame) <= allowed)
        {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("plucked notes are found within 10 ms", "[evaluation][onsets]")
{
    std::vector<float> audio(static_cast<std::size_t>(rate * 3.0), 0.0f);
    const std::vector<std::size_t> starts{11025, 33075, 55125, 77175, 99225};
    const std::vector<double> pitches{220.0, 330.0, 440.0, 330.0, 523.25};
    for (std::size_t index = 0; index < starts.size(); ++index)
    {
        addPluck(audio, starts.at(index), pitches.at(index));
    }

    const auto onsets = detectOnsets(audio.data(), audio.size(), rate, 0);
    CHECK(onsets.size() == starts.size());
    for (const std::size_t start : starts)
    {
        CHECK(foundNear(onsets, static_cast<std::int64_t>(start), 0.010));
    }
}

TEST_CASE("repeated notes of one pitch are each found", "[evaluation][onsets]")
{
    std::vector<float> audio(static_cast<std::size_t>(rate * 2.0), 0.0f);
    for (int index = 0; index < 4; ++index)
    {
        addPluck(audio, static_cast<std::size_t>(5000 + (index * 15000)), 392.0);
    }
    const auto onsets = detectOnsets(audio.data(), audio.size(), rate, 0);
    REQUIRE(onsets.size() == 4);
    for (int index = 0; index < 4; ++index)
    {
        CHECK(
            std::llabs(
                onsets.at(static_cast<std::size_t>(index)).frame - (5000 + (index * 15000))) <=
            441);
    }
}

TEST_CASE("onsets are placed on the take's timeline", "[evaluation][onsets]")
{
    std::vector<float> audio(static_cast<std::size_t>(rate), 0.0f);
    addPluck(audio, 22050, 440.0);
    const auto onsets = detectOnsets(audio.data(), audio.size(), rate, -44100);
    REQUIRE(onsets.size() == 1);
    CHECK(std::llabs(onsets.front().frame - (-22050)) <= 441);
}

TEST_CASE("silence and steady noise have no onsets", "[evaluation][onsets]")
{
    const std::vector<float> silence(static_cast<std::size_t>(rate), 0.0f);
    CHECK(detectOnsets(silence.data(), silence.size(), rate, 0).empty());

    std::mt19937 generator(7);
    std::normal_distribution<float> noise(0.0f, 0.05f);
    std::vector<float> hiss(static_cast<std::size_t>(rate * 2.0));
    for (float& sample : hiss)
    {
        sample = noise(generator);
    }
    // The noise's own start is an onset; nothing after it is.
    const auto onsets = detectOnsets(hiss.data(), hiss.size(), rate, 0);
    CHECK(onsets.size() <= 1);
}

TEST_CASE("a quiet attack after a loud one is found, and weaker", "[evaluation][onsets]")
{
    std::vector<float> audio(static_cast<std::size_t>(rate * 2.0), 0.0f);
    addPluck(audio, 10000, 440.0, 0.8);
    addPluck(audio, 50000, 660.0, 0.15);
    const auto onsets = detectOnsets(audio.data(), audio.size(), rate, 0);
    REQUIRE(onsets.size() == 2);
    CHECK(onsets.at(0).strength == 1.0);
    CHECK(onsets.at(1).strength < 1.0);
    CHECK(onsets.at(1).strength > 0.0);
}

TEST_CASE("nothing to analyse is no onsets, not an error", "[evaluation][onsets]")
{
    CHECK(detectOnsets(nullptr, 0, rate, 0).empty());
    const std::vector<float> one(1, 0.5f);
    CHECK(detectOnsets(one.data(), one.size(), 0.0, 0).empty());
}

// A three-minute take is the longest a review judges on the message thread when
// the take ends (take-review design.md decision 1).
TEST_CASE("onset detection over a three-minute take", "[.benchmark][evaluation][onsets]")
{
    std::vector<float> audio(static_cast<std::size_t>(180.0 * rate), 0.0F);
    for (std::size_t start = 0; start + 22050 < audio.size(); start += 22050)
    {
        for (std::size_t index = start; index < start + 22050; ++index)
        {
            const double seconds = static_cast<double>(index - start) / rate;
            audio[index] = static_cast<float>(
                0.5 * std::exp(-seconds * 6.0) * std::sin(twoPi * 330.0 * seconds));
        }
    }
    BENCHMARK("detect onsets, 180 s")
    {
        return detectOnsets(audio.data(), audio.size(), rate, 0).size();
    };
}

namespace
{
// A quiet room: steady hiss at about -56 dBFS, and every 0.4 s from `from` a
// 30 ms bump at about -48 dBFS -- a breath, a chair, the fridge.
[[nodiscard]] std::vector<float> room(double seconds, double from = 0.2)
{
    std::mt19937 generator(11);
    std::normal_distribution<float> hiss(0.0f, 0.0015f);
    std::normal_distribution<float> bump(0.0f, 0.004f);
    std::vector<float> audio(static_cast<std::size_t>(rate * seconds));
    for (float& sample : audio)
    {
        sample = hiss(generator);
    }
    for (double at = from; at + 0.03 < seconds; at += 0.4)
    {
        const auto first = static_cast<std::size_t>(at * rate);
        for (std::size_t index = first; index < first + static_cast<std::size_t>(0.03 * rate);
             ++index)
        {
            audio[index] += bump(generator);
        }
    }
    return audio;
}

// A note that dies away in about half a second, so the room between notes is
// the room again.
void addShortNote(std::vector<float>& audio, std::size_t start, double frequency, double level)
{
    for (std::size_t index = start; index < audio.size(); ++index)
    {
        const double seconds = static_cast<double>(index - start) / rate;
        audio[index] += static_cast<float>(
            level * std::exp(-seconds * 12.0) * std::sin(twoPi * frequency * seconds));
    }
}
} // namespace

TEST_CASE(
    "the room is not an attack: nothing is found below the minimum level",
    "[evaluation][onsets]")
{
    const auto quiet = room(3.0);

    // The bug: in a take of nothing but the room, the relative thresholds found
    // the room's own bumps, and they came back as extra notes.
    CHECK(detectOnsets(quiet.data(), quiet.size(), rate, 0).empty());

    // The floor is the singer's to move: lowered past the room, its bumps are
    // attacks again.
    OnsetSettings sensitive;
    sensitive.minimumLevelDb = -80.0;
    CHECK(detectOnsets(quiet.data(), quiet.size(), rate, 0, sensitive).size() >= 3);
}

TEST_CASE(
    "notes over a noisy room are found, and the room is not, between or under them",
    "[evaluation][onsets]")
{
    // Notes at 0.5, 1.5 and 2.5 s, and the room's bumps every 0.4 s from 1 s:
    // some between the notes, and some -- 1.8 and 2.6 s -- under a note's
    // tail, where the note is loud enough that only the floor under each
    // frequency keeps the bump from reading as a new attack.
    auto audio = room(3.2, 1.0);
    for (const double at : {0.5, 1.5, 2.5})
    {
        addShortNote(audio, static_cast<std::size_t>(at * rate), 330.0, 0.3);
    }

    const auto onsets = detectOnsets(audio.data(), audio.size(), rate, 0);
    CHECK(onsets.size() == 3);
    for (const double at : {0.5, 1.5, 2.5})
    {
        CHECK(foundNear(onsets, static_cast<std::int64_t>(at * rate), 0.01));
    }
}

TEST_CASE("a level is the RMS in dB below full scale", "[evaluation][onsets]")
{
    const std::vector<float> half(1000, 0.5f);
    CHECK(std::abs(levelDb(half.data(), 0, half.size()) - (20.0 * std::log10(0.5))) < 1e-6);

    const std::vector<float> silence(1000, 0.0f);
    CHECK(std::isinf(levelDb(silence.data(), 0, silence.size())));
}
