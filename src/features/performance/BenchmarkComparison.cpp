#include "BenchmarkComparison.h"

#include <algorithm>
#include <set>

namespace performance
{
BenchmarkComparisonResult
compareBenchmarkRuns(const BenchmarkRunRecord& baseline, const BenchmarkRunRecord& candidate)
{
    const auto validation = validateComparability(baseline, candidate);
    BenchmarkComparisonResult result{validation.comparable, validation.mismatches, {}};
    if (!validation.comparable)
        return result;

    std::set<std::string> metricIds;
    for (const auto& summary : baseline.summaries)
        metricIds.insert(summary.metricId);
    for (const auto& summary : candidate.summaries)
        metricIds.insert(summary.metricId);

    for (const auto& metricId : metricIds)
    {
        const auto findSummary = [&metricId](const auto& summaries)
        {
            return std::find_if(
                summaries.begin(), summaries.end(),
                [&](const auto& summary) { return summary.metricId == metricId; });
        };
        const auto baselineSummary = findSummary(baseline.summaries);
        const auto candidateSummary = findSummary(candidate.summaries);
        MetricComparison metric{metricId};
        if (baselineSummary != baseline.summaries.end())
        {
            metric.unit = baselineSummary->unit;
            metric.baselineValue = baselineSummary->median;
        }
        if (candidateSummary != candidate.summaries.end())
        {
            if (metric.unit.empty())
                metric.unit = candidateSummary->unit;
            metric.candidateValue = candidateSummary->median;
        }
        if (metric.baselineValue.has_value() && metric.candidateValue.has_value())
        {
            metric.absoluteChange = *metric.candidateValue - *metric.baselineValue;
            if (*metric.baselineValue != 0.0)
                metric.relativePercentChange =
                    *metric.absoluteChange / *metric.baselineValue * 100.0;
        }
        result.metrics.push_back(std::move(metric));
    }
    return result;
}
} // namespace performance