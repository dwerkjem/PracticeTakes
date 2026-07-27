#include "SustainedAudioAnalysisScenario.h"

namespace performance
{
SustainedAudioAnalysisScenario::SustainedAudioAnalysisScenario(
    SustainedAudioAnalysisActions& actions,
    std::size_t frameCount)
    : actions(actions), frameCount(frameCount)
{
}

std::string_view SustainedAudioAnalysisScenario::id() const noexcept
{
    return "sustained-audio-analysis";
}

std::string_view SustainedAudioAnalysisScenario::displayName() const noexcept
{
    return "Sustained audio analysis";
}

std::string_view SustainedAudioAnalysisScenario::workloadId() const noexcept
{
    return "generated-harmonic-fixture-v1";
}

ValidationResult
SustainedAudioAnalysisScenario::validate(const RunConfiguration& configuration) const
{
    ValidationResult result;
    if (configuration.scenarioId != id())
        result.errors.emplace_back("Scenario identifier does not match sustained audio analysis");
    if (configuration.workloadId != workloadId())
        result.errors.emplace_back("Workload identifier does not match the deterministic fixture");
    if (configuration.audio.sampleRateHz <= 0.0)
        result.errors.emplace_back("Audio sample rate must be greater than zero");
    if (configuration.audio.bufferSizeFrames == 0)
        result.errors.emplace_back("Audio buffer size must be greater than zero");
    if (frameCount == 0)
        result.errors.emplace_back("Analysis frame count must be greater than zero");
    result.valid = result.errors.empty();
    return result;
}

void SustainedAudioAnalysisScenario::prepare()
{
    actions.prepareAnalysis();
}

void SustainedAudioAnalysisScenario::runTrial()
{
    actions.analyzeFixture(frameCount);
}

ValidationResult SustainedAudioAnalysisScenario::verifyCorrectness() const
{
    if (actions.pitchedFrameCount() == 0)
        return ValidationResult::failure("Deterministic audio fixture produced no pitched frames");
    return ValidationResult::success();
}

void SustainedAudioAnalysisScenario::restore() noexcept
{
    actions.restoreAnalysis();
}
} // namespace performance