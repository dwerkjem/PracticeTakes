#include "NonRealTimeTelemetryCollector.h"

#include <stdexcept>

namespace performance
{
NonRealTimeTelemetryCollector::NonRealTimeTelemetryCollector(
    ProcessMetricsProvider& provider,
    BenchmarkClock& clock)
    : provider(provider), clock(clock)
{
}

void NonRealTimeTelemetryCollector::beginTrial(std::uint32_t trialIndex)
{
    if (active)
    {
        throw std::logic_error("A telemetry trial is already active");
    }
    activeTrialIndex = trialIndex;
    startedAt = clock.now();
    completedWork = 0;
    guiLatencies.clear();
    active = true;
}

TrialMeasurement NonRealTimeTelemetryCollector::endTrial()
{
    if (!active)
    {
        throw std::logic_error("No telemetry trial is active");
    }

    const auto duration = clock.now() - startedAt;
    const auto durationNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    const auto snapshot = provider.sample();
    TrialMeasurement result;
    result.trialIndex = activeTrialIndex;
    result.duration = durationNanoseconds;
    result.samples = {
        {"process-cpu", snapshot.processCpuPercent, "percent"},
        {"process-memory", static_cast<double>(snapshot.processMemoryBytes), "bytes"},
        {"ambient-cpu", snapshot.ambientCpuPercent, "percent"}};
    for (const auto latency : guiLatencies)
    {
        result.samples.push_back(
            {"gui-latency", static_cast<double>(latency.count()), "nanoseconds"});
    }

    const auto seconds = std::chrono::duration<double>(duration).count();
    if (seconds > 0.0)
    {
        result.samples.push_back(
            {"scenario-throughput", static_cast<double>(completedWork) / seconds, "units/second"});
    }

    active = false;
    return result;
}

void NonRealTimeTelemetryCollector::abortTrial() noexcept
{
    active = false;
    completedWork = 0;
    guiLatencies.clear();
}

void NonRealTimeTelemetryCollector::recordGuiLatency(std::chrono::nanoseconds latency)
{
    if (active)
    {
        guiLatencies.push_back(latency);
    }
}

void NonRealTimeTelemetryCollector::addCompletedWork(std::uint64_t units) noexcept
{
    if (active)
    {
        completedWork += units;
    }
}
} // namespace performance