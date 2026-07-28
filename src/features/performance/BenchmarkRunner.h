#pragma once

#include "BenchmarkInterfaces.h"

#include <functional>

namespace performance
{
enum class RunnerPhase
{
    idle,
    validating,
    stabilizing,
    warmingUp,
    measuring,
    aggregating,
    cleaningUp,
    completed
};

struct RunnerProgress
{
    RunnerPhase phase = RunnerPhase::idle;
    std::uint32_t completedTrials = 0;
    std::uint32_t totalTrials = 0;
};

struct BenchmarkRunnerResult
{
    ValidationResult validation;
    std::vector<TrialMeasurement> trials;
    RunStatus status = RunStatus::pending;
    std::optional<std::string> statusDetail;
};

class BenchmarkRunner
{
  public:
    using Stabilizer = std::function<void()>;
    using ProgressObserver = std::function<void(const RunnerProgress&)>;
    using Aggregator = std::function<void(std::vector<TrialMeasurement>&)>;
    using CancellationRequested = std::function<bool()>;

    explicit BenchmarkRunner(
        Stabilizer stabilizer = {},
        ProgressObserver observer = {},
        Aggregator aggregator = {},
        CancellationRequested cancellationRequested = {});

    [[nodiscard]] BenchmarkRunnerResult
    run(const RunConfiguration& configuration,
        BenchmarkScenario& scenario,
        OptimizationStrategy& strategy,
        TelemetryCollector& telemetry);

    [[nodiscard]] RunnerProgress progress() const noexcept;

  private:
    void report(RunnerPhase phase, std::uint32_t completedTrials, std::uint32_t totalTrials);

    Stabilizer stabilizer;
    ProgressObserver observer;
    Aggregator aggregator;
    CancellationRequested cancellationRequested;
    RunnerProgress currentProgress;
};
} // namespace performance