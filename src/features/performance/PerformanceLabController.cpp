#include "PerformanceLabController.h"

#include <utility>

namespace performance
{
PerformanceLabController::PerformanceLabController(
    Validator validatorIn,
    RunExecutor executorIn,
    RecordLoader loaderIn,
    RecordExporter exporterIn)
    : validator(std::move(validatorIn)), executor(std::move(executorIn)),
      loader(std::move(loaderIn)), exporter(std::move(exporterIn))
{
    currentState.configuration.strategyId = "baseline";
    currentState.validation = validator ? validator(currentState.configuration)
                                        : ValidationResult::failure("Validation is unavailable");
}

const PerformanceLabState& PerformanceLabController::state() const noexcept
{
    return currentState;
}

void PerformanceLabController::setConfiguration(RunConfiguration configuration)
{
    if (currentState.running)
        return;

    currentState.configuration = std::move(configuration);
    currentState.validation = validator ? validator(currentState.configuration)
                                        : ValidationResult::failure("Validation is unavailable");
    currentState.error.reset();
}

bool PerformanceLabController::startRun()
{
    currentState.validation = validator ? validator(currentState.configuration)
                                        : ValidationResult::failure("Validation is unavailable");
    currentState.error.reset();
    if (currentState.running || !currentState.validation.valid || !executor)
        return false;

    currentState.running = true;
    currentState.cancellationRequested = false;
    currentState.progress = {};
    currentState.view = PerformanceLabView::liveRun;

    auto record = executor(
        currentState.configuration,
        [this](const RunnerProgress& progress) { currentState.progress = progress; },
        [this] { return currentState.cancellationRequested; });

    currentState.running = false;
    currentState.history.push_back(std::move(record));
    currentState.selectedResult = currentState.history.size() - 1;
    currentState.view = PerformanceLabView::results;
    return true;
}

void PerformanceLabController::requestCancellation() noexcept
{
    if (currentState.running)
        currentState.cancellationRequested = true;
}

void PerformanceLabController::showConfiguration() noexcept
{
    if (!currentState.running)
        currentState.view = PerformanceLabView::configuration;
}

void PerformanceLabController::showHistory() noexcept
{
    if (!currentState.running)
        currentState.view = PerformanceLabView::history;
}

bool PerformanceLabController::selectResult(std::size_t index)
{
    if (index >= currentState.history.size())
    {
        setError("The selected benchmark result is unavailable");
        return false;
    }

    currentState.selectedResult = index;
    currentState.view = PerformanceLabView::results;
    currentState.error.reset();
    return true;
}

bool PerformanceLabController::selectComparison(
    std::size_t baselineIndex,
    std::size_t candidateIndex)
{
    if (baselineIndex >= currentState.history.size() ||
        candidateIndex >= currentState.history.size())
    {
        setError("Both comparison results must be available");
        return false;
    }

    currentState.baselineSelection = baselineIndex;
    currentState.candidateSelection = candidateIndex;
    currentState.comparison = compareBenchmarkRuns(
        currentState.history[baselineIndex], currentState.history[candidateIndex]);
    currentState.view = PerformanceLabView::comparison;
    currentState.error.reset();
    return true;
}

bool PerformanceLabController::reopen(std::string_view runId)
{
    if (!loader)
    {
        setError("Saved-result reopening is unavailable");
        return false;
    }

    auto record = loader(runId);
    if (!record)
    {
        setError("The saved benchmark result could not be opened");
        return false;
    }

    currentState.history.push_back(std::move(*record));
    currentState.selectedResult = currentState.history.size() - 1;
    currentState.view = PerformanceLabView::results;
    currentState.error.reset();
    return true;
}

bool PerformanceLabController::exportSelected(std::string_view destination)
{
    if (!currentState.selectedResult || *currentState.selectedResult >= currentState.history.size())
    {
        setError("Select a benchmark result before exporting");
        return false;
    }
    if (!exporter || !exporter(currentState.history[*currentState.selectedResult], destination))
    {
        setError("The benchmark report could not be exported");
        return false;
    }

    currentState.error.reset();
    return true;
}

void PerformanceLabController::setSafetyWarnings(std::vector<BenchmarkWarning> warnings)
{
    currentState.safetyWarnings = std::move(warnings);
}

void PerformanceLabController::setError(std::string message)
{
    currentState.error = std::move(message);
}
} // namespace performance
