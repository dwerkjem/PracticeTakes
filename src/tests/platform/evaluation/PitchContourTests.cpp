#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/evaluation/PitchContour.h"

#include <cmath>
#include <random>
#include <vector>

using namespace evaluation;

namespace
{
constexpr double rate = 44100.0;
constexpr double twoPi = 6.283185307179586;

// A tone at `frequency`, with each harmonic's level given in turn.
[[nodiscard]] std::vector<float>
tone(double frequency, double seconds, const std::vector<double>& harmonics)
{
    std::vector<float> audio(static_cast<std::size_t>(seconds * rate), 0.0f);
    for (std::size_t index = 0; index < audio.size(); ++index)
    {
        const double time = static_cast<double>(index) / rate;
        double sample = 0.0;
        for (std::size_t harmonic = 0; harmonic < harmonics.size(); ++harmonic)
        {
            sample += harmonics[harmonic] *
                      std::sin(twoPi * frequency * static_cast<double>(harmonic + 1) * time);
        }
        audio[index] = static_cast<float>(sample);
    }
    return audio;
}

[[nodiscard]] double cents(double hertz, double expected)
{
    return 1200.0 * std::log2(hertz / expected);
}

// The voiced frames' frequencies all within `tolerance` cents of `expected`.
void checkAllNear(const PitchContour& contour, double expected, double tolerance)
{
    REQUIRE_FALSE(contour.frames.empty());
    for (const ContourFrame& frame : contour.frames)
    {
        REQUIRE(frame.voiced());
        CHECK(std::abs(cents(frame.hertz, expected)) < tolerance);
        CHECK(frame.confidence > 0.9);
    }
}
} // namespace

TEST_CASE("the window is the power of two nearest 46 ms", "[evaluation][contour]")
{
    CHECK(contourWindowSize(44100.0, {}) == 2048);
    CHECK(contourWindowSize(48000.0, {}) == 2048);
    CHECK(contourWindowSize(96000.0, {}) == 4096);
}

TEST_CASE("a sine's pitch is found within 2 cents across a voice's range", "[evaluation][contour]")
{
    for (const double frequency : {82.41, 110.0, 196.0, 261.63, 440.0, 659.26, 1046.5})
    {
        const std::vector<float> audio = tone(frequency, 0.5, {0.5});
        checkAllNear(pitchContourOf(audio.data(), audio.size(), rate, 0), frequency, 2.0);
    }
}

TEST_CASE("frames are a hop apart, centred on their windows", "[evaluation][contour]")
{
    const std::vector<float> audio = tone(220.0, 0.5, {0.5});
    const PitchContour contour = pitchContourOf(audio.data(), audio.size(), rate, 1000);
    REQUIRE(contour.frames.size() > 2);
    CHECK(contour.hop == 512);
    CHECK(contour.frames.front().frame == 1000 + 1024);
    CHECK(contour.frames[1].frame - contour.frames[0].frame == 512);

    const auto [from, to] = contour.between(1000 + 1024 + 512, 1000 + 1024 + (3 * 512));
    CHECK(from == 1);
    CHECK(to == 3);
}

TEST_CASE(
    "a loud second harmonic over a weak fundamental is not an octave up",
    "[evaluation][contour]")
{
    // A voice's second or third harmonic is often louder than its fundamental.
    const std::vector<float> audio = tone(196.0, 0.5, {0.1, 0.5, 0.3, 0.15});
    checkAllNear(pitchContourOf(audio.data(), audio.size(), rate, 0), 196.0, 3.0);
}

TEST_CASE("a rich tone is not an octave down", "[evaluation][contour]")
{
    const std::vector<float> audio = tone(330.0, 0.5, {0.5, 0.25, 0.2, 0.1, 0.08});
    checkAllNear(pitchContourOf(audio.data(), audio.size(), rate, 0), 330.0, 3.0);
}

TEST_CASE("silence is unvoiced and noise is unsure", "[evaluation][contour]")
{
    const std::vector<float> silence(static_cast<std::size_t>(rate * 0.3), 0.0f);
    for (const ContourFrame& frame : pitchContourOf(silence.data(), silence.size(), rate, 0).frames)
    {
        CHECK_FALSE(frame.voiced());
    }

    std::mt19937 generator(7);
    std::normal_distribution<float> distribution(0.0f, 0.2f);
    std::vector<float> noise(static_cast<std::size_t>(rate * 0.5));
    for (float& sample : noise)
    {
        sample = distribution(generator);
    }
    const PitchContour contour = pitchContourOf(noise.data(), noise.size(), rate, 0);
    REQUIRE_FALSE(contour.frames.empty());
    for (const ContourFrame& frame : contour.frames)
    {
        CHECK(frame.confidence < 0.8);
    }
}

TEST_CASE("audio shorter than a window has no frames", "[evaluation][contour]")
{
    const std::vector<float> audio = tone(220.0, 0.01, {0.5});
    CHECK(pitchContourOf(audio.data(), audio.size(), rate, 0).frames.empty());
}

TEST_CASE("the contour of three minutes of singing", "[.benchmark][evaluation][contour]")
{
    const std::vector<float> audio = tone(220.0, 180.0, {0.3, 0.3, 0.2});
    BENCHMARK("pitchContourOf, 3 minutes")
    {
        return pitchContourOf(audio.data(), audio.size(), rate, 0).frames.size();
    };
}
