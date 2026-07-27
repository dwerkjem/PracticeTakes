#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/analysis/harmonics/HarmonicAnalyzer.h"

#include <array>
#include <cmath>
#include <numbers>
#include <random>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr double fundamental = 200.0;

[[nodiscard]] std::array<float, HarmonicAnalyzer::windowSize>
harmonicSignal(const std::array<float, 4>& amplitudes)
{
    std::array<float, HarmonicAnalyzer::windowSize> samples{};
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        for (std::size_t harmonic = 0; harmonic < amplitudes.size(); ++harmonic)
        {
            const auto angle =
                2.0 * std::numbers::pi * fundamental * static_cast<double>(harmonic + 1) *
                static_cast<double>(index) / sampleRate;
            samples[index] += amplitudes[harmonic] * static_cast<float>(std::sin(angle));
        }
    }
    return samples;
}
} // namespace

TEST_CASE("harmonics are measured relative to the strongest partial", "[harmonics]")
{
    HarmonicAnalyzer analyzer;
    const auto result = analyzer.analyze(harmonicSignal({0.8f, 0.4f, 0.2f, 0.1f}), sampleRate);

    REQUIRE(result.isPitched);
    CHECK(result.fundamentalHz == Catch::Approx(fundamental).margin(0.5));
    CHECK(result.relativeAmplitudes[0] == Catch::Approx(1.0f).margin(0.05f));
    CHECK(result.relativeAmplitudes[1] == Catch::Approx(0.5f).margin(0.08f));
    CHECK(result.relativeAmplitudes[2] == Catch::Approx(0.25f).margin(0.08f));
}

TEST_CASE("weak fundamentals retain a useful harmonic profile", "[harmonics]")
{
    HarmonicAnalyzer analyzer;
    const auto result = analyzer.analyze(harmonicSignal({0.05f, 0.8f, 0.4f, 0.2f}), sampleRate);

    REQUIRE(result.isPitched);
    CHECK(result.fundamentalHz == Catch::Approx(fundamental).margin(0.5));
    CHECK(result.relativeAmplitudes[1] == Catch::Approx(1.0f).margin(0.05f));
    CHECK(result.relativeAmplitudes[0] < 0.15f);
}

TEST_CASE("spectral centroid tracks brighter harmonic balances", "[harmonics]")
{
    HarmonicAnalyzer analyzer;
    const auto dark = analyzer.analyze(harmonicSignal({0.8f, 0.3f, 0.1f, 0.03f}), sampleRate);
    const auto bright = analyzer.analyze(harmonicSignal({0.1f, 0.2f, 0.5f, 0.8f}), sampleRate);

    CHECK(bright.spectralCentroidHz > dark.spectralCentroidHz);
}

TEST_CASE("noise is marked as uncertain", "[harmonics]")
{
    std::array<float, HarmonicAnalyzer::windowSize> noise{};
    std::mt19937 generator{42};
    std::uniform_real_distribution<float> distribution{-0.4f, 0.4f};
    for (auto& sample : noise)
        sample = distribution(generator);

    HarmonicAnalyzer analyzer;
    const auto result = analyzer.analyze(noise, sampleRate);

    CHECK_FALSE(result.isPitched);
    CHECK(result.confidence < 0.35f);
}
