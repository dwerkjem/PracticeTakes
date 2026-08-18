#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/analysis/harmonics/HarmonicAnalyzer.h"
#include "platform/audio/PitchDetector.h"

#include <array>
#include <cmath>
#include <numbers>
#include <random>
#include <span>

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

// HarmonicAnalyzer::analyze() takes an already-detected pitch rather than
// detecting it itself (that now happens once, shared, in SharedPitchAnalysis);
// tests stand in for that shared step with their own detector instance.
[[nodiscard]] PitchDetector::Result
detectPitch(std::span<const float, HarmonicAnalyzer::windowSize> samples, double rate)
{
    PitchDetector detector;
    return detector.detect(samples, rate);
}
} // namespace

TEST_CASE("harmonics are measured relative to the strongest partial", "[harmonics]")
{
    HarmonicAnalyzer analyzer;
    const auto samples = harmonicSignal({0.8f, 0.4f, 0.2f, 0.1f});
    const auto result = analyzer.analyze(samples, sampleRate, detectPitch(samples, sampleRate));

    REQUIRE(result.isPitched);
    CHECK(result.fundamentalHz == Catch::Approx(fundamental).margin(0.5));
    CHECK(result.relativeAmplitudes[0] == Catch::Approx(1.0f).margin(0.05f));
    CHECK(result.relativeAmplitudes[1] == Catch::Approx(0.5f).margin(0.08f));
    CHECK(result.relativeAmplitudes[2] == Catch::Approx(0.25f).margin(0.08f));
}

TEST_CASE("weak fundamentals retain a useful harmonic profile", "[harmonics]")
{
    HarmonicAnalyzer analyzer;
    const auto samples = harmonicSignal({0.05f, 0.8f, 0.4f, 0.2f});
    const auto result = analyzer.analyze(samples, sampleRate, detectPitch(samples, sampleRate));

    REQUIRE(result.isPitched);
    CHECK(result.fundamentalHz == Catch::Approx(fundamental).margin(0.5));
    CHECK(result.relativeAmplitudes[1] == Catch::Approx(1.0f).margin(0.05f));
    CHECK(result.relativeAmplitudes[0] < 0.15f);
}

TEST_CASE("spectral centroid tracks brighter harmonic balances", "[harmonics]")
{
    HarmonicAnalyzer analyzer;
    const auto darkSamples = harmonicSignal({0.8f, 0.3f, 0.1f, 0.03f});
    const auto brightSamples = harmonicSignal({0.1f, 0.2f, 0.5f, 0.8f});
    const auto dark =
        analyzer.analyze(darkSamples, sampleRate, detectPitch(darkSamples, sampleRate));
    const auto bright =
        analyzer.analyze(brightSamples, sampleRate, detectPitch(brightSamples, sampleRate));

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
    const auto result = analyzer.analyze(noise, sampleRate, detectPitch(noise, sampleRate));

    CHECK_FALSE(result.isPitched);
    CHECK(result.confidence < 0.35f);
}

TEST_CASE("harmonic analyzer per-frame throughput", "[.benchmark][harmonics][performance]")
{
    // Representative pitched signal at 48 kHz — the primary operating sample rate.
    constexpr double sr48k = 48000.0;
    const auto signal48k = harmonicSignal({0.8f, 0.4f, 0.2f, 0.1f});

    // Representative pitched signal at 44.1 kHz — common consumer device rate.
    constexpr double sr441k = 44100.0;
    std::array<float, HarmonicAnalyzer::windowSize> signal441k{};
    for (std::size_t i = 0; i < signal441k.size(); ++i)
    {
        const auto angle = 2.0 * std::numbers::pi * fundamental * static_cast<double>(i) / sr441k;
        signal441k[i] = 0.8f * static_cast<float>(std::sin(angle));
    }

    HarmonicAnalyzer analyzer48k;
    HarmonicAnalyzer analyzer441k;

    // Pitch detection is no longer part of what analyze() does -- it is
    // computed once, upstream, by SharedPitchAnalysis -- so it is computed
    // once here too rather than inside the benchmarked closure, to keep this
    // benchmark measuring the branch it names.
    const auto pitch48k = detectPitch(signal48k, sr48k);
    const auto pitch441k = detectPitch(signal441k, sr441k);

    BENCHMARK("analyze @ 48 kHz")
    {
        return analyzer48k.analyze(signal48k, sr48k, pitch48k);
    };

    BENCHMARK("analyze @ 44.1 kHz")
    {
        return analyzer441k.analyze(signal441k, sr441k, pitch441k);
    };
}
