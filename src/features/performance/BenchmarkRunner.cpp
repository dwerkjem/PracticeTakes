#include "BenchmarkRunner.h"

#include <utility>

namespace performance
{
BenchmarkRunner::BenchmarkRunner(
    Stabilizer stabilizer,
    ProgressObserver observer,
    Aggregator aggregator,
    CancellationRequested cancellationRequested)
    : stabilizer(std::move(stabilizer)), observer(std::move(observer)),
      aggregator(std::move(aggregator)), cancellationRequested(std::move(cancellationRequested))
{
}

BenchmarkRunnerResult BenchmarkRunner::run(
    const RunConfiguration& configuration,
    BenchmarkScenario& scenario,
    OptimizationStrategy& strategy,
    TelemetryCollector& telemetry)
{
    report(RunnerPhase::validating, 0, configuration.measuredTrialCount);
    auto validation = scenario.validate(configuration);
    const auto parameterValidation = strategy.validateParameters(configuration.strategyParameters);
    validation.errors.insert(
        validation.errors.end(), parameterValidation.errors.begin(),
        parameterValidation.errors.end());
    validation.valid = validation.valid && parameterValidation.valid;
    if (!strategy.isCompatible(scenario))
    {
        validation.valid = false;
        validation.errors.emplace_back("Strategy is not compatible with the selected scenario");
    }
    if (!validation.valid)
    {
        return {.validation = std::move(validation)};
    }

    BenchmarkRunnerResult result;
    result.validation = ValidationResult::success();
    result.status = RunStatus::running;
    bool strategyActive = false;
    bool scenarioPrepared = false;
    const auto cleanUp = [&]
    {
        report(
            RunnerPhase::cleaningUp, static_cast<std::uint32_t>(result.trials.size()),
            configuration.measuredTrialCount);
        if (scenarioPrepared)
        {
            scenario.restore();
        }
        if (strategyActive)
        {
            strategy.deactivate();
        }
    };

    try
    {
        strategy.activate(configuration.strategyParameters);
        strategyActive = true;
        scenario.prepare();
        scenarioPrepared = true;

        report(RunnerPhase::stabilizing, 0, configuration.measuredTrialCount);
        if (stabilizer)
        {
            stabilizer();
        }

        report(RunnerPhase::warmingUp, 0, configuration.warmUpCount);
        for (std::uint32_t trial = 0; trial < configuration.warmUpCount; ++trial)
        {
            scenario.runTrial();
            report(RunnerPhase::warmingUp, trial + 1, configuration.warmUpCount);
            if (cancellationRequested && cancellationRequested())
            {
                result.status = RunStatus::cancelled;
                result.statusDetail = "Cancelled after a warm-up trial";
                cleanUp();
                return result;
            }
        }

        report(RunnerPhase::measuring, 0, configuration.measuredTrialCount);
        for (std::uint32_t trial = 0; trial < configuration.measuredTrialCount; ++trial)
        {
            telemetry.beginTrial(trial);
            try
            {
                scenario.runTrial();
            }
            catch (...)
            {
                telemetry.abortTrial();
                throw;
            }
            result.trials.push_back(telemetry.endTrial());
            report(RunnerPhase::measuring, trial + 1, configuration.measuredTrialCount);
            if (cancellationRequested && cancellationRequested())
            {
                result.status = RunStatus::cancelled;
                result.statusDetail = "Cancelled after a measured trial";
                cleanUp();
                return result;
            }
        }

        result.validation = scenario.verifyCorrectness();
        report(
            RunnerPhase::aggregating, configuration.measuredTrialCount,
            configuration.measuredTrialCount);
        if (aggregator)
        {
            aggregator(result.trials);
        }
        result.status = result.validation.valid ? RunStatus::completed : RunStatus::failed;
        if (!result.validation.valid)
        {
            result.statusDetail = "Scenario correctness verification failed";
        }
    }
    catch (const std::exception& error)
    {
        result.status = RunStatus::failed;
        result.statusDetail = error.what();
    }
    catch (...)
    {
        result.status = RunStatus::failed;
        result.statusDetail = "Benchmark failed with an unknown error";
    }

    cleanUp();
    report(
        RunnerPhase::completed, static_cast<std::uint32_t>(result.trials.size()),
        configuration.measuredTrialCount);
    return result;
}

RunnerProgress BenchmarkRunner::progress() const noexcept
{
    return currentProgress;
}

void BenchmarkRunner::report(
    RunnerPhase phase,
    std::uint32_t completedTrials,
    std::uint32_t totalTrials)
{
    currentProgress = {phase, completedTrials, totalTrials};
    if (observer)
    {
        observer(currentProgress);
    }
}
} // namespace performance