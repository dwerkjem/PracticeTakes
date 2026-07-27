#include "TrialAggregator.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>

namespace performance
{
namespace
{
struct MetricValues
{
    std::string unit;
    std::vector<double> values;
};

double median(const std::vector<double>& sortedValues)
{
    const auto middle = sortedValues.size() / 2;
    if (sortedValues.size() % 2 == 0)
        return (sortedValues[middle - 1] + sortedValues[middle]) / 2.0;
    return sortedValues[middle];
}

double populationStandardDeviation(const std::vector<double>& values)
{
    const auto sum = std::accumulate(values.begin(), values.end(), 0.0);
    const auto mean = sum / static_cast<double>(values.size());
    const auto squaredDifferenceSum = std::accumulate(
        values.begin(), values.end(), 0.0,
        [mean](double total, double value) { return total + std::pow(value - mean, 2.0); });
    return std::sqrt(squaredDifferenceSum / static_cast<double>(values.size()));
}
} // namespace

TrialAggregation TrialAggregator::aggregate(const std::vector<TrialMeasurement>& trials)
{
    std::map<std::string, MetricValues> metrics;
    TrialAggregation result;

    for (const auto& trial : trials)
    {
        result.eventTotals.deadlineMisses += trial.deadlineMisses;
        result.eventTotals.dropouts += trial.dropouts;
        result.eventTotals.underruns += trial.underruns;

        for (const auto& sample : trial.samples)
        {
            if (!std::isfinite(sample.value))
                throw std::invalid_argument("Metric samples must be finite");

            auto& metric = metrics[sample.metricId];
            if (!metric.unit.empty() && metric.unit != sample.unit)
                throw std::invalid_argument("Metric units must be consistent");
            metric.unit = sample.unit;
            metric.values.push_back(sample.value);
        }
    }

    for (auto& [metricId, metric] : metrics)
    {
        std::ranges::sort(metric.values);
        const auto tailIndex =
            static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(metric.values.size()))) -
            1;
        result.summaries.push_back(
            {std::move(metricId), std::move(metric.unit), metric.values.size(),
             median(metric.values), metric.values[tailIndex], metric.values.front(),
             metric.values.back(), populationStandardDeviation(metric.values)});
    }
    return result;
}
} // namespace performance