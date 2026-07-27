#pragma once

#include "../analysis/harmonics/HarmonicAnalyzer.h"
#include "SustainedAudioAnalysisScenario.h"

#include <array>
#include <cstddef>

namespace performance
{
class HarmonicAnalysisActions final : public SustainedAudioAnalysisActions
{
  public:
    explicit HarmonicAnalysisActions(double sampleRateHz = 48000.0);

    void prepareAnalysis() override;
    void analyzeFixture(std::size_t frameCount) override;
    [[nodiscard]] std::size_t pitchedFrameCount() const noexcept override;
    void restoreAnalysis() noexcept override;

  private:
    void generateFixture();

    HarmonicAnalyzer analyzer;
    std::array<float, HarmonicAnalyzer::windowSize> fixture{};
    double sampleRateHz;
    std::size_t pitchedFrames = 0;
};
} // namespace performance