#pragma once

#include "BenchmarkComparison.h"
#include "BenchmarkRunner.h"

#include <functional>
#include <optional>

namespace performance
{
enum class PerformanceLabView
{
    configuration,
    liveRun,
    history,
    results,
    comparison
};

struct PerformanceLabState
{
    PerformanceLabView view = PerformanceLabView::configuration;
    RunConfiguration configuration;
    ValidationResult validation;
    RunnerProgress progress;
    bool running = false;
    bool cancellationRequested = false;
    std::vector<BenchmarkWarning> safetyWarnings;
    std::vector<BenchmarkRunRecord> history;
    std::optional<std::size_t> selectedResult;
    std::optional<std::size_t> baselineSelection;
    std::optional<std::size_t> candidateSelection;
    std::optional<BenchmarkComparisonResult> comparison;
    std::optional<std::string> error;
};

class PerformanceLabController
{
  public:
    using Validator = std::function<ValidationResult(const RunConfiguration&)>;
    using RunExecutor = std::function<BenchmarkRunRecord(
        const RunConfiguration&,
        const std::function<void(const RunnerProgress&)>&,
        const std::function<bool()>&)>;
    using RecordLoader = std::function<std::optional<BenchmarkRunRecord>(std::string_view)>;
    using RecordExporter = std::function<bool(const BenchmarkRunRecord&, std::string_view)>;

    PerformanceLabController(
        Validator validator,
        RunExecutor executor,
        RecordLoader loader = {},
        RecordExporter exporter = {});

    [[nodiscard]] const PerformanceLabState& state() const noexcept;
    void setConfiguration(RunConfiguration configuration);
    [[nodiscard]] bool startRun();
    void requestCancellation() noexcept;
    void showConfiguration() noexcept;
    void showHistory() noexcept;
    [[nodiscard]] bool selectResult(std::size_t index);
    [[nodiscard]] bool selectComparison(std::size_t baselineIndex, std::size_t candidateIndex);
    [[nodiscard]] bool reopen(std::string_view runId);
    [[nodiscard]] bool exportSelected(std::string_view destination);
    void setSafetyWarnings(std::vector<BenchmarkWarning> warnings);

  private:
    void setError(std::string message);

    Validator validator;
    RunExecutor executor;
    RecordLoader loader;
    RecordExporter exporter;
    PerformanceLabState currentState;
};
} // namespace performance
