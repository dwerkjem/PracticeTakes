#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

#include "platform/audio/PitchDetector.h"
#include "platform/audio/SyntheticTone.h"

namespace
{
constexpr double sampleRate = 48000.0;

std::vector<float>
render(SyntheticTone& tone, double frequency, std::size_t count, float amplitude = 0.25f)
{
    std::vector<float> samples(count, 0.0f);
    tone.render(samples.data(), samples.size(), frequency, sampleRate, amplitude);

    return samples;
}

// What the tuner would read from a stretch of this signal, using the
// application's own detector rather than a test-only one.
double detected(const std::vector<float>& samples, std::size_t offset)
{
    PitchDetector detector;
    constexpr auto window = static_cast<std::size_t>(PitchDetector::windowSize);

    REQUIRE(offset + window <= samples.size());

    const std::span<const float, window> frame{samples.data() + offset, window};

    return detector.detect(frame, sampleRate).frequency;
}

double peak(const std::vector<float>& samples)
{
    auto highest = 0.0;

    for (const auto sample : samples)
    {
        highest = std::max(highest, static_cast<double>(std::abs(sample)));
    }

    return highest;
}
} // namespace

TEST_CASE("a rendered note is the pitch it was asked for", "[audio][tone]")
{
    SyntheticTone tone;
    const auto samples = render(tone, 440.0, 96000);

    // Within a quarter tone: the vibrato moves it either side of 440 on
    // purpose, and a detector reading 440 back is the whole point of feeding
    // the tuner this rather than a microphone.
    const auto reading = detected(samples, 0);

    REQUIRE(reading > 425.0);
    REQUIRE(reading < 455.0);
}

TEST_CASE("the pitch moves rather than sitting still", "[audio][tone]")
{
    // A flat tone draws a straight line on the tuner's graph, which is a state
    // no real input ever produces -- and a layout that only breaks when the
    // reading moves would never be seen.
    SyntheticTone tone;
    const auto samples = render(tone, 440.0, 96000);

    const auto early = detected(samples, 0);
    const auto later = detected(samples, 48000);

    REQUIRE(early > 0.0);
    REQUIRE(later > 0.0);
    REQUIRE(std::abs(early - later) > 0.5);
}

TEST_CASE("the note carries overtones", "[audio][tone]")
{
    // One partial gives the harmonic analyser a single bar to draw. Comparing
    // energy at the fundamental's period against half of it is enough to show
    // the second partial is really there.
    SyntheticTone tone;
    const auto samples = render(tone, 440.0, 48000);

    const auto period = sampleRate / 440.0;
    const auto correlationAt = [&samples](double lag)
    {
        const auto shift = static_cast<std::size_t>(std::lround(lag));
        auto total = 0.0;

        for (std::size_t index = 0; index + shift < samples.size(); ++index)
        {
            total += static_cast<double>(samples[index]) * samples[index + shift];
        }

        return total;
    };

    // A pure sine correlates with itself at half its period as strongly as it
    // anti-correlates; partials break that symmetry.
    REQUIRE(std::abs(correlationAt(period / 2.0)) < std::abs(correlationAt(period)));
}

TEST_CASE("the level stays inside the amplitude it was given", "[audio][tone]")
{
    // Everything downstream assumes samples in [-1, 1], and a clipping
    // indicator that lights up on synthetic input would be a false finding.
    SyntheticTone tone;

    for (const auto amplitude : {0.05f, 0.25f, 0.5f})
    {
        const auto samples = render(tone, 440.0, 24000, amplitude);

        INFO("amplitude " << amplitude);
        CHECK(peak(samples) <= static_cast<double>(amplitude) * 1.02);
        CHECK(peak(samples) > static_cast<double>(amplitude) * 0.5);
    }
}

TEST_CASE("the level swells enough for a meter to show it", "[audio][tone]")
{
    // A loudness or peak meter reading a steady level is as uninformative as a
    // tuner reading a steady pitch. Windows of a tenth of a second, which is
    // roughly what a meter integrates over.
    SyntheticTone tone;
    const auto samples = render(tone, 440.0, 192000, 0.5f);
    const auto window = static_cast<std::size_t>(sampleRate / 10.0);

    auto quietest = 1.0;
    auto loudest = 0.0;

    for (std::size_t start = 0; start + window < samples.size(); start += window)
    {
        const std::vector<float> slice(
            samples.begin() + static_cast<long>(start),
            samples.begin() + static_cast<long>(start + window));
        const auto level = peak(slice);

        quietest = std::min(quietest, level);
        loudest = std::max(loudest, level);
    }

    // At least six decibels between the quietest and loudest moment.
    REQUIRE(loudest > quietest * 2.0);
}

TEST_CASE("silence is rendered for no frequency", "[audio][tone]")
{
    SyntheticTone tone;
    std::vector<float> samples(1000, 1.0f);
    tone.render(samples.data(), samples.size(), 0.0, sampleRate, 0.25f);

    // Untouched rather than zeroed: the caller uses the device's own input when
    // no tone is set, and this buffer is simply not used.
    REQUIRE(samples.front() == 1.0f);
}

TEST_CASE("rendering is refused rather than crashing on nonsense", "[audio][tone]")
{
    SyntheticTone tone;
    std::vector<float> samples(64, 0.0f);

    tone.render(nullptr, 64, 440.0, sampleRate, 0.25f);
    tone.render(samples.data(), 0, 440.0, sampleRate, 0.25f);
    tone.render(samples.data(), samples.size(), 440.0, 0.0, 0.25f);

    REQUIRE(samples.front() == 0.0f);
}

TEST_CASE("a reset note starts where the last one did", "[audio][tone]")
{
    // Two captures of the same surface should differ because the application
    // differs, not because the tone happened to be at a different point in its
    // vibrato when the screenshot was taken.
    SyntheticTone tone;
    const auto first = render(tone, 440.0, 4096);

    tone.reset();
    const auto second = render(tone, 440.0, 4096);

    REQUIRE(first == second);
}

TEST_CASE("noise is present but small", "[audio][tone]")
{
    SyntheticTone one;
    SyntheticTone other;
    SyntheticTone::Shape quiet;
    quiet.noise = 0.0f;
    other.setShape(quiet);

    const auto noisy = render(one, 440.0, 8192);
    const auto clean = render(other, 440.0, 8192);

    auto difference = 0.0;

    for (std::size_t index = 0; index < noisy.size(); ++index)
    {
        difference =
            std::max(difference, std::abs(static_cast<double>(noisy[index]) - clean[index]));
    }

    REQUIRE(difference > 0.0);
    REQUIRE(difference < 0.05);
}
