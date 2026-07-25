#include "HarmonicAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numeric>

HarmonicAnalyzer::Result
HarmonicAnalyzer::analyze(std::span<const float, windowSize> samples, double sampleRate)
{
    Result result;
    const auto pitch = pitchDetector.detect(samples, sampleRate);
    result.fundamentalHz = pitch.frequency;
    result.inputLevel = pitch.inputLevel;
    if (pitch.frequency <= 0.0 || sampleRate <= 0.0)
        return result;

    std::fill(fftData.begin(), fftData.end(), 0.0f);
    std::copy(samples.begin(), samples.end(), fftData.begin());
    window.multiplyWithWindowingTable(fftData.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform(fftData.data());

    const auto binFrequency = sampleRate / static_cast<double>(fftSize);
    const auto nyquistBin = fftSize / 2;
    double weightedFrequency = 0.0;
    double magnitudeSum = 0.0;
    double totalMagnitude = 0.0;
    for (int bin = 1; bin < nyquistBin; ++bin)
    {
        const auto magnitude = static_cast<double>(fftData[static_cast<std::size_t>(bin)]);
        totalMagnitude += magnitude;
        magnitudeSum += magnitude;
        weightedFrequency += magnitude * static_cast<double>(bin) * binFrequency;
    }
    result.spectralCentroidHz = magnitudeSum > 0.0 ? weightedFrequency / magnitudeSum : 0.0;

    std::array<double, harmonicCount> amplitudes{};
    double harmonicMagnitude = 0.0;
    double deviationSum = 0.0;
    int measuredHarmonics = 0;
    for (int harmonic = 1; harmonic <= harmonicCount; ++harmonic)
    {
        const auto expectedFrequency = pitch.frequency * static_cast<double>(harmonic);
        if (expectedFrequency >= sampleRate * 0.5)
            break;

        const auto expectedBin = expectedFrequency / binFrequency;
        const auto centreBin = static_cast<int>(std::round(expectedBin));
        const auto firstBin = std::max(1, centreBin - 2);
        const auto lastBin = std::min(nyquistBin - 1, centreBin + 2);
        int peakBin = firstBin;
        for (int bin = firstBin + 1; bin <= lastBin; ++bin)
        {
            if (fftData[static_cast<std::size_t>(bin)] > fftData[static_cast<std::size_t>(peakBin)])
            {
                peakBin = bin;
            }
        }

        const auto amplitude = static_cast<double>(fftData[static_cast<std::size_t>(peakBin)]);
        amplitudes[static_cast<std::size_t>(harmonic - 1)] = amplitude;
        harmonicMagnitude += amplitude;
        deviationSum +=
            std::abs(static_cast<double>(peakBin) - expectedBin) / std::max(1.0, expectedBin);
        ++measuredHarmonics;
    }

    const auto strongest = *std::max_element(amplitudes.begin(), amplitudes.end());
    if (strongest > 0.0)
    {
        for (std::size_t index = 0; index < amplitudes.size(); ++index)
        {
            result.relativeAmplitudes[index] = static_cast<float>(amplitudes[index] / strongest);
        }
    }

    const auto harmonicShare =
        static_cast<float>(harmonicMagnitude / std::max(1.0, totalMagnitude));
    result.inharmonicity =
        measuredHarmonics > 0
            ? static_cast<float>(deviationSum / static_cast<double>(measuredHarmonics))
            : 1.0f;
    result.confidence = std::clamp(
        (harmonicShare * 2.5f) * (1.0f - std::min(1.0f, result.inharmonicity * 8.0f)), 0.0f, 1.0f);
    result.isPitched = result.confidence >= 0.35f;
    return result;
}
