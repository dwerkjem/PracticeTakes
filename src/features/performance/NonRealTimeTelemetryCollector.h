#pragma once

#include "BenchmarkInterfaces.h"

#include <chrono>
#include <cstdint>
#include <vector>

namespace performance
{
struct ProcessMetricsSnapshot
{
    double processCpuPercent = 0.0;
    std::uint64_t processMemoryBytes = 0;
    double ambientCpuPercent = 0.0;
};

class ProcessMetricsProvider
{
  public:
    virtual ~ProcessMetricsProvider() = default;
    [[nodiscard]] virtual ProcessMetricsSnapshot sample() = 0;
};

class NonRealTimeTelemetryCollector final : public TelemetryCollector
{
  public:
    NonRealTimeTelemetryCollector(ProcessMetricsProvider& provider, BenchmarkClock& clock);

    void beginTrial(std::uint32_t trialIndex) override;
    [[nodiscard]] TrialMeasurement endTrial() override;
    void abortTrial() noexcept override;

    void recordGuiLatency(std::chrono::nanoseconds latency);
    void addCompletedWork(std::uint64_t units = 1) noexcept;

  private:
    ProcessMetricsProvider& provider;
    BenchmarkClock& clock;
    std::chrono::steady_clock::time_point startedAt{};
    std::uint32_t activeTrialIndex = 0;
    std::uint64_t completedWork = 0;
    bool active = false;
    std::vector<std::chrono::nanoseconds> guiLatencies;
};
} // namespace performance