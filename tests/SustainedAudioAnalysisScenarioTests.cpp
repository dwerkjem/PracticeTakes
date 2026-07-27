#include "features/performance/HarmonicAnalysisActions.h"
#include "features/performance/SustainedAudioAnalysisScenario.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
class FakeAnalysisActions final : public performance::SustainedAudioAnalysisActions
{
  public:
    void prepareAnalysis() override
    {
        prepared = true;
    }
    void analyzeFixture(std::size_t frameCount) override
    {
        analyzedFrames += frameCount;
        pitchedFrames += frameCount;
    }
    [[nodiscard]] std::size_t pitchedFrameCount() const noexcept override
    {
        return pitchedFrames;
    }
    void restoreAnalysis() noexcept override
    {
        restored = true;
    }

    bool prepared = false;
    bool restored = false;
    std::size_t analyzedFrames = 0;
    std::size_t pitchedFrames = 0;
};

performance::RunConfiguration validConfiguration()
{
    return {
        .scenarioId = "sustained-audio-analysis",
        .workloadId = "generated-harmonic-fixture-v1",
        .audio = {.sampleRateHz = 48000.0, .bufferSizeFrames = 256}};
}
} // namespace

TEST_CASE("sustained audio analysis uses semantic actions for a deterministic workload")
{
    FakeAnalysisActions actions;
    performance::SustainedAudioAnalysisScenario scenario(actions, 12);

    REQUIRE(scenario.validate(validConfiguration()).valid);
    scenario.prepare();
    scenario.runTrial();
    REQUIRE(scenario.verifyCorrectness().valid);
    scenario.restore();

    REQUIRE(actions.prepared);
    REQUIRE(actions.analyzedFrames == 12);
    REQUIRE(actions.restored);
}

TEST_CASE("sustained audio analysis runs the generated fixture through production analysis")
{
    performance::HarmonicAnalysisActions actions;
    performance::SustainedAudioAnalysisScenario scenario(actions, 3);

    scenario.prepare();
    scenario.runTrial();

    REQUIRE(actions.pitchedFrameCount() == 3);
    REQUIRE(scenario.verifyCorrectness().valid);
    scenario.restore();
    REQUIRE(actions.pitchedFrameCount() == 0);
}