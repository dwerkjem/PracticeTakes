#include "PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double minimumDetectableFrequencyHz = 16.352;
constexpr double maximumDetectableFrequencyHz = 2500.0;
constexpr float minimumInputRms = 0.008f;
constexpr double minimumCorrelationScore = 0.72;
} // namespace

PitchDetector::Result
PitchDetector::detect(std::span<const float, windowSize> samples, double sampleRate)
{
    Result result;

    cumulativeEnergy[0] = 0.0;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const auto sample = static_cast<double>(samples[index]);
        cumulativeEnergy[index + 1] = cumulativeEnergy[index] + (sample * sample);
    }

    const auto totalEnergy = cumulativeEnergy.back();
    result.inputLevel =
        static_cast<float>(std::sqrt(totalEnergy / static_cast<double>(windowSize)));
    if (result.inputLevel < minimumInputRms || sampleRate <= 0.0)
    {
        return result;
    }

    std::fill(fftInput.begin(), fftInput.end(), std::complex<float>{});
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        fftInput[index] = {samples[index], 0.0f};
    }

    fft.perform(fftInput.data(), fftOutput.data(), false);
    for (std::size_t index = 0; index < fftOutput.size(); ++index)
    {
        fftInput[index] = {std::norm(fftOutput[index]), 0.0f};
    }
    fft.perform(fftInput.data(), fftOutput.data(), true);

    // JUCE's engines normalize inverse transforms, but derive the scale from
    // lag zero so the detector remains correct if an engine uses another
    // convention.
    const auto inverseZeroLag = static_cast<double>(fftOutput[0].real());
    if (std::abs(inverseZeroLag) <= std::numeric_limits<double>::epsilon())
    {
        return result;
    }
    const auto inverseScale = totalEnergy / inverseZeroLag;

    const auto minimumLag =
        std::max(2, static_cast<int>(sampleRate / maximumDetectableFrequencyHz));
    const auto maximumLag =
        std::min(windowSize / 2, static_cast<int>(sampleRate / minimumDetectableFrequencyHz));
    if (minimumLag + 1 >= maximumLag)
    {
        return result;
    }

    for (int lag = minimumLag; lag <= maximumLag; ++lag)
    {
        const auto firstEnergy = cumulativeEnergy[static_cast<std::size_t>(windowSize - lag)];
        const auto secondEnergy = totalEnergy - cumulativeEnergy[static_cast<std::size_t>(lag)];
        const auto denominator = std::sqrt(firstEnergy * secondEnergy);
        const auto numerator =
            static_cast<double>(fftOutput[static_cast<std::size_t>(lag)].real()) * inverseScale;
        correlations[static_cast<std::size_t>(lag)] =
            denominator > 0.0 ? std::clamp(numerator / denominator, -1.0, 1.0) : 0.0;
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
        return result;
    }

    const auto left = correlations[static_cast<std::size_t>(periodLag - 1)];
    const auto centre = correlations[static_cast<std::size_t>(periodLag)];
    const auto right = correlations[static_cast<std::size_t>(periodLag + 1)];
    const auto curvature = left - (2.0 * centre) + right;
    const auto offset = std::abs(curvature) > std::numeric_limits<double>::epsilon()
                            ? 0.5 * (left - right) / curvature
                            : 0.0;
    const auto refinedLag = static_cast<double>(periodLag) + std::clamp(offset, -0.5, 0.5);

    result.frequency = sampleRate / refinedLag;
    return result;
}
