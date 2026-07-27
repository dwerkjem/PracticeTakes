#include "BenchmarkComparability.h"

#include <sstream>

namespace performance
{
namespace
{
template <typename Value> std::string display(const Value& value)
{
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

std::string display(const std::optional<std::uint64_t>& value)
{
    return value.has_value() ? std::to_string(*value) : "unknown";
}
} // namespace

ComparabilityResult
validateComparability(const BenchmarkRunRecord& baseline, const BenchmarkRunRecord& candidate)
{
    ComparabilityResult result;
    const auto compare = [&result](std::string field, const auto& left, const auto& right)
    {
        if (left != right)
            result.mismatches.push_back({std::move(field), display(left), display(right)});
    };

    compare("scenarioId", baseline.configuration.scenarioId, candidate.configuration.scenarioId);
    compare("workloadId", baseline.configuration.workloadId, candidate.configuration.workloadId);
    compare("applicationCommit", baseline.provenance.commit, candidate.provenance.commit);
    compare("buildType", baseline.provenance.buildType, candidate.provenance.buildType);
    compare(
        "operatingSystem", baseline.provenance.operatingSystem,
        candidate.provenance.operatingSystem);
    compare("cpu", baseline.provenance.cpu, candidate.provenance.cpu);
    compare("memoryBytes", baseline.provenance.memoryBytes, candidate.provenance.memoryBytes);
    compare(
        "audioDevice", baseline.provenance.audio.deviceName, candidate.provenance.audio.deviceName);
    compare("audioBackend", baseline.provenance.audio.backend, candidate.provenance.audio.backend);
    compare(
        "sampleRateHz", baseline.provenance.audio.sampleRateHz,
        candidate.provenance.audio.sampleRateHz);
    compare(
        "bufferSizeFrames", baseline.provenance.audio.bufferSizeFrames,
        candidate.provenance.audio.bufferSizeFrames);
    compare(
        "instrumentationVersion", baseline.provenance.instrumentationVersion,
        candidate.provenance.instrumentationVersion);
    result.comparable = result.mismatches.empty();
    return result;
}
} // namespace performance