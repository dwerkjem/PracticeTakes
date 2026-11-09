#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "platform/audio/LevelMeter.h"
#include "platform/evaluation/OnsetDetector.h"

#include <cmath>
#include <numbers>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;

// 375 Hz is exactly eight periods of a 1024-sample window at 48 kHz, so every
// window of it measures the same.
[[nodiscard]] std::vector<float> sine(double amplitude, double seconds, double hz = 375.0)
{
    std::vector<float> samples(static_cast<std::size_t>(seconds * sampleRate));
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        samples[index] = static_cast<float>(
            amplitude *
            std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(index) / sampleRate));
    }
    return samples;
}

[[nodiscard]] std::vector<float> silence(double seconds)
{
    return std::vector<float>(static_cast<std::size_t>(seconds * sampleRate), 0.0f);
}

[[nodiscard]] LevelMeter meterAtRate()
{
    LevelMeter meter;
    meter.setSampleRate(sampleRate);
    return meter;
}

void push(LevelMeter& meter, const std::vector<float>& samples)
{
    meter.push(samples.data(), samples.size());
}
} // namespace

TEST_CASE("the level of a steady tone is its RMS in dB below full scale", "[audio][level-meter]")
{
    auto meter = meterAtRate();
    push(meter, sine(0.5, 0.5));

    // 0.5 / sqrt(2) is -9.03 dB.
    CHECK(meter.reading().levelDb == Catch::Approx(-9.03).margin(0.1));
    CHECK(meter.reading().peakDb == Catch::Approx(-9.03).margin(0.1));
}

TEST_CASE("the meter reads a take's window as the onset detector does", "[audio][level-meter]")
{
    // The line on the meter is the take's threshold, so the two must agree on
    // what a sound measures: the RMS of the detector's own window.
    const evaluation::OnsetSettings settings;
    LevelMeter meter(settings.windowSize);
    meter.setSampleRate(sampleRate);

    const auto tone = sine(0.02, 0.25);
    push(meter, tone);

    const double detector =
        evaluation::levelDb(tone.data(), tone.size() - settings.windowSize, settings.windowSize);
    CHECK(meter.windowSize() == settings.windowSize);
    CHECK(meter.reading().levelDb == Catch::Approx(detector).margin(0.05));
}

TEST_CASE(
    "a tone under the threshold does not reach it, and one over it does",
    "[audio][level-meter]")
{
    const double threshold = evaluation::OnsetSettings::defaultMinimumLevelDb;

    auto quiet = meterAtRate();
    push(quiet, sine(0.004, 0.3)); // about -51 dB
    CHECK_FALSE(quiet.reading().reaches(threshold));

    auto loud = meterAtRate();
    push(loud, sine(0.02, 0.3)); // about -37 dB
    CHECK(loud.reading().reaches(threshold));
}

TEST_CASE("silence reads as silence, never as minus infinity", "[audio][level-meter]")
{
    auto meter = meterAtRate();
    CHECK(meter.reading().levelDb == LevelMeter::silenceDb);

    push(meter, silence(0.2));
    CHECK(meter.reading().levelDb == LevelMeter::silenceDb);
    CHECK(meter.reading().peakDb == LevelMeter::silenceDb);
    CHECK(std::isfinite(meter.reading().levelDb));
}

TEST_CASE("the level rises at once and falls at its release rate", "[audio][level-meter]")
{
    auto meter = meterAtRate();
    push(meter, sine(0.5, 0.2));
    const double loud = meter.reading().levelDb;

    // A quarter of a second of silence: the level falls 30 dB a second, so
    // about 7.5 dB (less the part of the window the tone still filled).
    push(meter, silence(0.25));
    CHECK(meter.reading().levelDb == Catch::Approx(loud - 7.5).margin(0.6));

    // And is back at once when the sound returns.
    push(meter, sine(0.5, 0.05));
    CHECK(meter.reading().levelDb == Catch::Approx(loud).margin(0.1));
}

TEST_CASE("the peak holds, then falls, and never below the level", "[audio][level-meter]")
{
    auto meter = meterAtRate();
    push(meter, sine(0.5, 0.2));
    const double loud = meter.reading().peakDb;

    // Within the hold: the level has fallen, the peak has not.
    push(meter, silence(1.0));
    CHECK(meter.reading().levelDb < loud - 20.0);
    CHECK(meter.reading().peakDb == Catch::Approx(loud));

    // Past the hold, it falls at its own, slower rate: a second at 15 dB a
    // second after half a second's more hold.
    push(meter, silence(1.5));
    CHECK(meter.reading().peakDb == Catch::Approx(loud - 15.0).margin(0.5));
    CHECK(meter.reading().peakDb >= meter.reading().levelDb);

    // A louder sound takes the peak up with it.
    push(meter, sine(0.9, 0.1));
    CHECK(meter.reading().peakDb == Catch::Approx(meter.reading().levelDb));
}

TEST_CASE("a new sample rate or a reset starts from silence", "[audio][level-meter]")
{
    auto meter = meterAtRate();
    push(meter, sine(0.5, 0.2));
    meter.setSampleRate(44100.0);
    CHECK(meter.reading().levelDb == LevelMeter::silenceDb);

    push(meter, sine(0.5, 0.2));
    meter.reset();
    CHECK(meter.reading().levelDb == LevelMeter::silenceDb);
    CHECK(meter.reading().peakDb == LevelMeter::silenceDb);
}

TEST_CASE("a sound shorter than the window reads quieter than it is", "[audio][level-meter]")
{
    // As it does to the detector: a click is not a note.
    auto meter = meterAtRate();
    std::vector<float> click(1024, 0.0f);
    for (std::size_t index = 0; index < 64; ++index)
    {
        click[index] = 0.5f;
    }
    push(meter, silence(0.1));
    push(meter, click);

    // 64 samples of 0.5 in 1024: a sixteenth of the energy, -12 dB under -6 dB.
    CHECK(meter.reading().peakDb == Catch::Approx(-6.02 - 12.04).margin(0.2));
}
