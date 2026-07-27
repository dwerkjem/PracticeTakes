#pragma once

#include "BenchmarkTypes.h"

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace performance
{
enum class ParameterType
{
    boolean,
    integer,
    number,
    text,
    choice
};

struct StrategyParameter
{
    std::string id;
    std::string displayName;
    ParameterType type = ParameterType::text;
    std::string defaultValue;
    std::vector<std::string> choices;
};

struct ValidationResult
{
    bool valid = true;
    std::vector<std::string> errors;

    [[nodiscard]] static ValidationResult success()
    {
        return {};
    }

    [[nodiscard]] static ValidationResult failure(std::string error)
    {
        return {.valid = false, .errors = {std::move(error)}};
    }
};

class BenchmarkClock
{
  public:
    virtual ~BenchmarkClock() = default;

    [[nodiscard]] virtual std::chrono::steady_clock::time_point now() const noexcept = 0;
};

class TelemetryCollector
{
  public:
    virtual ~TelemetryCollector() = default;

    virtual void beginTrial(std::uint32_t trialIndex) = 0;
    [[nodiscard]] virtual TrialMeasurement endTrial() = 0;
};

class BenchmarkScenario
{
  public:
    virtual ~BenchmarkScenario() = default;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual std::string_view displayName() const noexcept = 0;
    [[nodiscard]] virtual std::string_view workloadId() const noexcept = 0;
    [[nodiscard]] virtual ValidationResult
    validate(const RunConfiguration& configuration) const = 0;
    virtual void prepare() = 0;
    virtual void runTrial() = 0;
    [[nodiscard]] virtual ValidationResult verifyCorrectness() const = 0;
    virtual void restore() noexcept = 0;
};

class OptimizationStrategy
{
  public:
    virtual ~OptimizationStrategy() = default;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual std::string_view displayName() const noexcept = 0;
    [[nodiscard]] virtual std::string_view implementationVersion() const noexcept = 0;
    [[nodiscard]] virtual std::vector<StrategyParameter> parameterSchema() const = 0;
    [[nodiscard]] virtual ValidationResult
    validateParameters(const std::unordered_map<std::string, std::string>& parameters) const = 0;
    [[nodiscard]] virtual bool isCompatible(const BenchmarkScenario& scenario) const noexcept = 0;
    virtual void activate(const std::unordered_map<std::string, std::string>& parameters) = 0;
    virtual void deactivate() noexcept = 0;
};
} // namespace performance