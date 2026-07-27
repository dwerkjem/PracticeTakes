#include "HarmonicAnalysisActions.h"

#include <cmath>
#include <numbers>

namespace performance
{
HarmonicAnalysisActions::HarmonicAnalysisActions(double sampleRateHz) : sampleRateHz(sampleRateHz)
{
}

void HarmonicAnalysisActions::prepareAnalysis()
{
    pitchedFrames = 0;
    generateFixture();
}

void HarmonicAnalysisActions::analyzeFixture(std::size_t frameCount)
{
    for (std::size_t frame = 0; frame < frameCount; ++frame)
    {
        const auto result = analyzer.analyze(fixture, sampleRateHz);
        if (result.isPitched)
        {
            ++pitchedFrames;
        }
    }
}

std::size_t HarmonicAnalysisActions::pitchedFrameCount() const noexcept
{
    return pitchedFrames;
}

void HarmonicAnalysisActions::restoreAnalysis() noexcept
{
    pitchedFrames = 0;
}

void HarmonicAnalysisActions::generateFixture()
{
    constexpr double fundamentalHz = 220.0;
    for (std::size_t sample = 0; sample < fixture.size(); ++sample)
    {
        const auto phase =
            2.0 * std::numbers::pi * fundamentalHz * static_cast<double>(sample) / sampleRateHz;
        fixture[sample] = static_cast<float>(0.7 * std::sin(phase) + 0.2 * std::sin(phase * 2.0));
    }
}
} // namespace performance