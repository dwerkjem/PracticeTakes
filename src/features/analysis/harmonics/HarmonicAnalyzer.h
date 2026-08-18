#pragma once

#include "../../../platform/audio/PitchDetector.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <span>

class HarmonicAnalyzer final
{
  public:
    static constexpr int harmonicCount = 8;
    static constexpr int windowSize = PitchDetector::windowSize;

    struct Result
    {
        double fundamentalHz = 0.0;
        double spectralCentroidHz = 0.0;
        float confidence = 0.0f;
        float inharmonicity = 0.0f;
        float inputLevel = 0.0f;
        bool isPitched = false;
        std::array<float, harmonicCount> relativeAmplitudes{};
    };

    [[nodiscard]] Result analyze(
        std::span<const float, windowSize> samples,
        double sampleRate,
        PitchDetector::Result pitch);

  private:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;

    juce::dsp::FFT fft{fftOrder};
    juce::dsp::WindowingFunction<float> window{fftSize, juce::dsp::WindowingFunction<float>::hann};
    std::array<float, fftSize * 2> fftData{};
};
