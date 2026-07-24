#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "tuner/PitchDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace
{
constexpr double minimumDetectableFrequencyHz = 16.352;
constexpr double maximumDetectableFrequencyHz = 2500.0;
constexpr float minimumInputRms = 0.008f;
constexpr double minimumCorrelationScore = 0.72;

[[nodiscard]] std::array<float, PitchDetector::windowSize>
sineWave(double frequency, double sampleRate, double phase = 0.0, float amplitude = 0.5f)
{
    std::array<float, PitchDetector::windowSize> samples{};
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const auto angle =
            (2.0 * std::numbers::pi * frequency * static_cast<double>(index) / sampleRate) + phase;
        samples[index] = amplitude * static_cast<float>(std::sin(angle));
    }
    return samples;
}

[[nodiscard]] double
scalarReference(const std::array<float, PitchDetector::windowSize>& samples, double sampleRate)
{
    double squareSum = 0.0;
    for (const auto sample : samples)
    {
        squareSum += static_cast<double>(sample) * static_cast<double>(sample);
    }
    const auto inputLevel =
        static_cast<float>(std::sqrt(squareSum / static_cast<double>(samples.size())));
    if (inputLevel < minimumInputRms || sampleRate <= 0.0)
    {
        return 0.0;
    }

    const auto minimumLag =
        std::max(2, static_cast<int>(sampleRate / maximumDetectableFrequencyHz));
    const auto maximumLag = std::min(
        PitchDetector::windowSize / 2, static_cast<int>(sampleRate / minimumDetectableFrequencyHz));
    std::array<double, (PitchDetector::windowSize / 2) + 1> correlations{};

    for (int lag = minimumLag; lag <= maximumLag; ++lag)
    {
        double numerator = 0.0;
        double firstEnergy = 0.0;
        double secondEnergy = 0.0;
        for (int index = 0; index < PitchDetector::windowSize - lag; ++index)
        {
            const auto first = static_cast<double>(samples[static_cast<std::size_t>(index)]);
            const auto second = static_cast<double>(samples[static_cast<std::size_t>(index + lag)]);
            numerator += first * second;
            firstEnergy += first * first;
            secondEnergy += second * second;
        }

        const auto denominator = std::sqrt(firstEnergy * secondEnergy);
        correlations[static_cast<std::size_t>(lag)] =
            denominator > 0.0 ? numerator / denominator : 0.0;
    }

    int periodLag = 0;
    for (int lag = minimumLag + 1; lag < maximumLag; ++lag)
    {
        const auto correlation = correlations[static_cast<std::size_t>(lag)];
        if (correlation >= minimumCorrelationScore &&
            correlation > correlations[static_cast<std::size_t>(lag - 1)] &&
            correlation >= correlations[static_cast<std::size_t>(lag + 1)])
        {
            periodLag = lag;
            break;
        }
    }
    if (periodLag == 0)
    {
        return 0.0;
    }

    const auto left = correlations[static_cast<std::size_t>(periodLag - 1)];
    const auto centre = correlations[static_cast<std::size_t>(periodLag)];
    const auto right = correlations[static_cast<std::size_t>(periodLag + 1)];
    const auto curvature = left - (2.0 * centre) + right;
    const auto offset = std::abs(curvature) > std::numeric_limits<double>::epsilon()
                            ? 0.5 * (left - right) / curvature
                            : 0.0;
    return sampleRate / (static_cast<double>(periodLag) + std::clamp(offset, -0.5, 0.5));
}
} // namespace

TEST_CASE("FFT pitch detection matches scalar normalized autocorrelation", "[tuner][pitch]")
{
    constexpr double sampleRate = 44100.0;
    PitchDetector detector;

    for (const auto frequency : {27.5, 55.0, 110.0, 440.0, 1000.0, 2200.0})
    {
        CAPTURE(frequency);
        const auto samples = sineWave(frequency, sampleRate, 0.37);
        const auto expected = scalarReference(samples, sampleRate);
        const auto actual = detector.detect(samples, sampleRate);

        REQUIRE(expected > 0.0);
        CHECK(actual.frequency == Catch::Approx(expected).margin(0.1));
        CHECK(actual.frequency == Catch::Approx(frequency).margin(0.15));
        CHECK(actual.inputLevel == Catch::Approx(0.3535f).margin(0.002f));
    }
}

TEST_CASE("FFT pitch detection preserves the silence gate", "[tuner][pitch]")
{
    PitchDetector detector;
    std::array<float, PitchDetector::windowSize> silence{};
    const auto quietTone = sineWave(440.0, 44100.0, 0.0, 0.005f);

    CHECK(detector.detect(silence, 44100.0).frequency == 0.0);
    CHECK(detector.detect(quietTone, 44100.0).frequency == 0.0);
    CHECK(detector.detect(silence, 0.0).frequency == 0.0);
}

TEST_CASE("FFT pitch detection supports common device sample rates", "[tuner][pitch]")
{
    PitchDetector detector;
    for (const auto sampleRate : {44100.0, 48000.0, 96000.0})
    {
        CAPTURE(sampleRate);
        const auto samples = sineWave(220.0, sampleRate, 1.1);
        CHECK(detector.detect(samples, sampleRate).frequency == Catch::Approx(220.0).margin(0.2));
    }
}

TEST_CASE("FFT pitch detection preserves harmonic instrument signals", "[tuner][pitch]")
{
    constexpr double fundamental = 196.0;
    constexpr double sampleRate = 48000.0;
    std::array<float, PitchDetector::windowSize> samples{};

    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const auto phase =
            2.0 * std::numbers::pi * fundamental * static_cast<double>(index) / sampleRate;
        samples[index] = (0.35f * static_cast<float>(std::sin(phase))) +
                         (0.12f * static_cast<float>(std::sin((2.0 * phase) + 0.2))) +
                         (0.06f * static_cast<float>(std::sin((3.0 * phase) - 0.4)));
    }

    PitchDetector detector;
    const auto expected = scalarReference(samples, sampleRate);
    const auto actual = detector.detect(samples, sampleRate);

    REQUIRE(expected > 0.0);
    CHECK(actual.frequency == Catch::Approx(expected).margin(0.1));
    CHECK(actual.frequency == Catch::Approx(fundamental).margin(0.2));
}

TEST_CASE("pitch detector performance comparison", "[.benchmark][tuner][pitch]")
{
    constexpr double sampleRate = 44100.0;
    const auto samples = sineWave(440.0, sampleRate, 0.37);
    PitchDetector detector;

    BENCHMARK("FFT normalized autocorrelation")
    {
        return detector.detect(samples, sampleRate);
    };

    BENCHMARK("scalar normalized autocorrelation")
    {
        return scalarReference(samples, sampleRate);
    };
}
