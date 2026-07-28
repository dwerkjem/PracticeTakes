#pragma once

#include "features/performance/BenchmarkInterfaces.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class FakeBenchmarkClock final : public performance::BenchmarkClock
{
  public:
    [[nodiscard]] std::chrono::steady_clock::time_point now() const noexcept override
    {
        return currentTime;
    }

    void advance(std::chrono::steady_clock::duration duration) noexcept
    {
        currentTime += duration;
    }

  private:
    std::chrono::steady_clock::time_point currentTime{};
};

class FakeTelemetryCollector final : public performance::TelemetryCollector
{
  public:
    void beginTrial(std::uint32_t trialIndex) override
    {
        activeTrialIndex = trialIndex;
        ++beginCount;
    }

    [[nodiscard]] performance::TrialMeasurement endTrial() override
    {
        ++endCount;
        auto result = nextMeasurement;
        result.trialIndex = activeTrialIndex;
        return result;
    }
    void abortTrial() noexcept override
    {
        ++abortCount;
    }

    performance::TrialMeasurement nextMeasurement;
    std::uint32_t activeTrialIndex = 0;
    std::uint32_t beginCount = 0;
    std::uint32_t endCount = 0;
    std::uint32_t abortCount = 0;
};

class FakeBenchmarkScenario final : public performance::BenchmarkScenario
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override
    {
        return scenarioId;
    }
    [[nodiscard]] std::string_view displayName() const noexcept override
    {
        return scenarioName;
    }
    [[nodiscard]] std::string_view workloadId() const noexcept override
    {
        return workload;
    }
    [[nodiscard]] performance::ValidationResult
    validate(const performance::RunConfiguration&) const override
    {
        return validationResult;
    }
    void prepare() override
    {
        ++prepareCount;
    }
    void runTrial() override
    {
        ++runCount;
        if (throwOnRunNumber.has_value() && runCount == *throwOnRunNumber)
            throw std::runtime_error("Fake scenario trial failed");
    }
    [[nodiscard]] performance::ValidationResult verifyCorrectness() const override
    {
        return correctnessResult;
    }
    void restore() noexcept override
    {
        ++restoreCount;
    }

    std::string scenarioId = "fake-scenario";
    std::string scenarioName = "Fake scenario";
    std::string workload = "fake-workload";
    performance::ValidationResult validationResult = performance::ValidationResult::success();
    performance::ValidationResult correctnessResult = performance::ValidationResult::success();
    std::uint32_t prepareCount = 0;
    std::uint32_t runCount = 0;
    std::uint32_t restoreCount = 0;
    std::optional<std::uint32_t> throwOnRunNumber;
};

class FakeOptimizationStrategy final : public performance::OptimizationStrategy
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override
    {
        return strategyId;
    }
    [[nodiscard]] std::string_view displayName() const noexcept override
    {
        return strategyName;
    }
    [[nodiscard]] std::string_view implementationVersion() const noexcept override
    {
        return version;
    }
    [[nodiscard]] std::vector<performance::StrategyParameter> parameterSchema() const override
    {
        return schema;
    }
    [[nodiscard]] performance::ValidationResult
    validateParameters(const std::unordered_map<std::string, std::string>&) const override
    {
        return parameterValidationResult;
    }
    [[nodiscard]] bool isCompatible(const performance::BenchmarkScenario&) const noexcept override
    {
        return compatible;
    }
    void activate(const std::unordered_map<std::string, std::string>& parameters) override
    {
        activeParameters = parameters;
        ++activateCount;
    }
    void deactivate() noexcept override
    {
        ++deactivateCount;
    }

    std::string strategyId = "fake-strategy";
    std::string strategyName = "Fake strategy";
    std::string version = "1";
    std::vector<performance::StrategyParameter> schema;
    performance::ValidationResult parameterValidationResult =
        performance::ValidationResult::success();
    bool compatible = true;
    std::unordered_map<std::string, std::string> activeParameters;
    std::uint32_t activateCount = 0;
    std::uint32_t deactivateCount = 0;
};