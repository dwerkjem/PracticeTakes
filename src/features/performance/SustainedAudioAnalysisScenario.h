#pragma once

#include "BenchmarkInterfaces.h"

#include <cstddef>
#include <string_view>

namespace performance
{
class SustainedAudioAnalysisActions
{
  public:
    virtual ~SustainedAudioAnalysisActions() = default;

    virtual void prepareAnalysis() = 0;
    virtual void analyzeFixture(std::size_t frameCount) = 0;
    [[nodiscard]] virtual std::size_t pitchedFrameCount() const noexcept = 0;
    virtual void restoreAnalysis() noexcept = 0;
};

class SustainedAudioAnalysisScenario final : public BenchmarkScenario
{
  public:
    static constexpr std::size_t defaultFrameCount = 64;

    explicit SustainedAudioAnalysisScenario(
        SustainedAudioAnalysisActions& actions,
        std::size_t frameCount = defaultFrameCount);

    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] std::string_view displayName() const noexcept override;
    [[nodiscard]] std::string_view workloadId() const noexcept override;
    [[nodiscard]] ValidationResult validate(const RunConfiguration& configuration) const override;
    void prepare() override;
    void runTrial() override;
    [[nodiscard]] ValidationResult verifyCorrectness() const override;
    void restore() noexcept override;

  private:
    SustainedAudioAnalysisActions& actions;
    std::size_t frameCount;
};
} // namespace performance