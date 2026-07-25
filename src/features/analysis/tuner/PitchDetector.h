#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <complex>
#include <span>

// Computes the same normalized autocorrelation used by the tuner in O(N log N)
// time. Scratch storage is owned by the detector so each analysis frame is
// allocation-free.
class PitchDetector final
{
  public:
    static constexpr int windowSize = 4096;

    struct Result
    {
        double frequency = 0.0;
        float inputLevel = 0.0f;
    };

    [[nodiscard]] Result detect(std::span<const float, windowSize> samples, double sampleRate);

  private:
    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << fftOrder;

    juce::dsp::FFT fft{fftOrder};
    std::array<std::complex<float>, fftSize> fftInput{};
    std::array<std::complex<float>, fftSize> fftOutput{};
    std::array<double, windowSize + 1> cumulativeEnergy{};
    std::array<double, (windowSize / 2) + 1> correlations{};
};
