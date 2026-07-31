#include "features/performance/NonRealTimeTelemetryCollector.h"
#include "support/BenchmarkFakes.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace
{
class FakeProcessMetricsProvider final : public performance::ProcessMetricsProvider
{
  public:
    [[nodiscard]] performance::ProcessMetricsSnapshot sample() override
    {
        return snapshot;
    }
    performance::ProcessMetricsSnapshot snapshot{12.5, 4096, 7.0};
};

double metricValue(const performance::TrialMeasurement& trial, std::string_view id)
{
    const auto metric = std::ranges::find(trial.samples, id, &performance::MetricSample::metricId);
    REQUIRE(metric != trial.samples.end());
    return metric->value;
}
} // namespace

TEST_CASE("non-real-time telemetry collects process GUI ambient and throughput metrics")
{
    FakeProcessMetricsProvider provider;
    FakeBenchmarkClock clock;
    performance::NonRealTimeTelemetryCollector collector(provider, clock);

    collector.beginTrial(4);
    collector.recordGuiLatency(std::chrono::milliseconds(8));
    collector.addCompletedWork(20);
    clock.advance(std::chrono::seconds(2));
    const auto trial = collector.endTrial();

    REQUIRE(trial.trialIndex == 4);
    REQUIRE(trial.duration == std::chrono::seconds(2));
    REQUIRE(metricValue(trial, "process-cpu") == 12.5);
    REQUIRE(metricValue(trial, "process-memory") == 4096.0);
    REQUIRE(metricValue(trial, "ambient-cpu") == 7.0);
    REQUIRE(metricValue(trial, "gui-latency") == 8'000'000.0);
    REQUIRE(metricValue(trial, "scenario-throughput") == 10.0);
}

TEST_CASE("aborting non-real-time telemetry discards trial-local markers")
{
    FakeProcessMetricsProvider provider;
    FakeBenchmarkClock clock;
    performance::NonRealTimeTelemetryCollector collector(provider, clock);

    collector.beginTrial(0);
    collector.recordGuiLatency(std::chrono::milliseconds(9));
    collector.addCompletedWork(5);
    collector.abortTrial();
    collector.beginTrial(1);
    clock.advance(std::chrono::seconds(1));
    const auto trial = collector.endTrial();

    REQUIRE(trial.samples.size() == 4);
    REQUIRE(metricValue(trial, "scenario-throughput") == 0.0);
}