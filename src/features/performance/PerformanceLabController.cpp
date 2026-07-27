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

PerformanceLabState PerformanceLabController::state() const
{
    const std::scoped_lock lock(stateMutex);
    return currentState;
}

void PerformanceLabController::setConfiguration(RunConfiguration configuration)
{
    const std::scoped_lock lock(stateMutex);
    if (currentState.running)
        return;

    currentState.configuration = std::move(configuration);
    currentState.validation = validator ? validator(currentState.configuration)
                                        : ValidationResult::failure("Validation is unavailable");
    currentState.error.reset();
}

bool PerformanceLabController::startRun()
{
    RunConfiguration configuration;
    {
        const std::scoped_lock lock(stateMutex);
        currentState.validation =
            validator ? validator(currentState.configuration)
                      : ValidationResult::failure("Validation is unavailable");
        currentState.error.reset();
        if (currentState.running || !currentState.validation.valid || !executor)
            return false;

        currentState.running = true;
        currentState.cancellationRequested = false;
        currentState.progress = {};
        currentState.view = PerformanceLabView::liveRun;
        configuration = currentState.configuration;
    }
    cancellationRequested.store(false, std::memory_order_release);

    auto record = executor(
        configuration,
        [this](const RunnerProgress& progress)
        {
            const std::scoped_lock lock(stateMutex);
            currentState.progress = progress;
        },
        [this] { return cancellationRequested.load(std::memory_order_acquire); });

    const std::scoped_lock lock(stateMutex);
    currentState.running = false;
    currentState.history.push_back(std::move(record));
    currentState.selectedResult = currentState.history.size() - 1;
    currentState.view = PerformanceLabView::results;
    return true;
}

void PerformanceLabController::requestCancellation() noexcept
{
    const std::scoped_lock lock(stateMutex);
    if (currentState.running)
    {
        currentState.cancellationRequested = true;
        cancellationRequested.store(true, std::memory_order_release);
    }
}

void PerformanceLabController::showConfiguration() noexcept
{
    const std::scoped_lock lock(stateMutex);
    if (!currentState.running)
        currentState.view = PerformanceLabView::configuration;
}

void PerformanceLabController::showHistory() noexcept
{
    const std::scoped_lock lock(stateMutex);
    if (!currentState.running)
        currentState.view = PerformanceLabView::history;
}

bool PerformanceLabController::selectResult(std::size_t index)
{
    const std::scoped_lock lock(stateMutex);
    if (index >= currentState.history.size())
    {
        setErrorLocked("The selected benchmark result is unavailable");
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
    const std::scoped_lock lock(stateMutex);
    if (baselineIndex >= currentState.history.size() ||
        candidateIndex >= currentState.history.size())
    {
        setErrorLocked("Both comparison results must be available");
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

    const std::scoped_lock lock(stateMutex);
    currentState.history.push_back(std::move(*record));
    currentState.selectedResult = currentState.history.size() - 1;
    currentState.view = PerformanceLabView::results;
    currentState.error.reset();
    return true;
}

bool PerformanceLabController::exportSelected(std::string_view destination)
{
    BenchmarkRunRecord record;
    {
        const std::scoped_lock lock(stateMutex);
        if (!currentState.selectedResult ||
            *currentState.selectedResult >= currentState.history.size())
        {
            setErrorLocked("Select a benchmark result before exporting");
            return false;
        }
        record = currentState.history[*currentState.selectedResult];
    }
    if (!exporter || !exporter(record, destination))
    {
        const std::scoped_lock lock(stateMutex);
        setErrorLocked("The benchmark report could not be exported");
        return false;
    }

    const std::scoped_lock lock(stateMutex);
    currentState.error.reset();
    return true;
}

void PerformanceLabController::setSafetyWarnings(std::vector<BenchmarkWarning> warnings)
{
    const std::scoped_lock lock(stateMutex);
    currentState.safetyWarnings = std::move(warnings);
}

void PerformanceLabController::setError(std::string message)
{
    const std::scoped_lock lock(stateMutex);
    setErrorLocked(std::move(message));
}

void PerformanceLabController::setErrorLocked(std::string message)
{
    currentState.error = std::move(message);
}
} // namespace performance
