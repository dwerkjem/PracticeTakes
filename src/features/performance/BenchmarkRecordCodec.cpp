#include "BenchmarkRecordCodec.h"

namespace performance
{
namespace
{
[[nodiscard]] juce::String toJuceString(const std::string& value)
{
    return juce::String::fromUTF8(value.data(), static_cast<int>(value.size()));
}

[[nodiscard]] juce::var encodeAudio(const AudioConfiguration& audio)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("deviceName", toJuceString(audio.deviceName));
    object->setProperty("backend", toJuceString(audio.backend));
    object->setProperty("sampleRateHz", audio.sampleRateHz);
    object->setProperty("bufferSizeFrames", static_cast<int>(audio.bufferSizeFrames));
    return object.release();
}

[[nodiscard]] AudioConfiguration decodeAudio(const juce::var& value)
{
    return {
        value["deviceName"].toString().toStdString(), value["backend"].toString().toStdString(),
        static_cast<double>(value["sampleRateHz"]),
        static_cast<std::uint32_t>(static_cast<int>(value["bufferSizeFrames"]))};
}

[[nodiscard]] juce::var encodeStringMap(const std::unordered_map<std::string, std::string>& values)
{
    auto object = std::make_unique<juce::DynamicObject>();
    for (const auto& [key, value] : values)
        object->setProperty(juce::Identifier(toJuceString(key)), toJuceString(value));
    return object.release();
}

[[nodiscard]] std::unordered_map<std::string, std::string> decodeStringMap(const juce::var& value)
{
    std::unordered_map<std::string, std::string> result;
    if (const auto* object = value.getDynamicObject())
        for (const auto& property : object->getProperties())
            result.emplace(
                property.name.toString().toStdString(), property.value.toString().toStdString());
    return result;
}

[[nodiscard]] juce::var encodeConfiguration(const RunConfiguration& configuration)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("scenarioId", toJuceString(configuration.scenarioId));
    object->setProperty("workloadId", toJuceString(configuration.workloadId));
    object->setProperty("strategyId", toJuceString(configuration.strategyId));
    object->setProperty("strategyParameters", encodeStringMap(configuration.strategyParameters));
    object->setProperty("warmUpCount", static_cast<int>(configuration.warmUpCount));
    object->setProperty("measuredTrialCount", static_cast<int>(configuration.measuredTrialCount));
    object->setProperty("audio", encodeAudio(configuration.audio));
    return object.release();
}

[[nodiscard]] RunConfiguration decodeConfiguration(const juce::var& value)
{
    return {
        value["scenarioId"].toString().toStdString(),
        value["workloadId"].toString().toStdString(),
        value["strategyId"].toString().toStdString(),
        decodeStringMap(value["strategyParameters"]),
        static_cast<std::uint32_t>(static_cast<int>(value["warmUpCount"])),
        static_cast<std::uint32_t>(static_cast<int>(value["measuredTrialCount"])),
        decodeAudio(value["audio"])};
}

[[nodiscard]] juce::var encodeProvenance(const RunProvenance& provenance)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("applicationVersion", toJuceString(provenance.applicationVersion));
    object->setProperty("commit", toJuceString(provenance.commit));
    object->setProperty("buildType", toJuceString(provenance.buildType));
    object->setProperty("operatingSystem", toJuceString(provenance.operatingSystem));
    object->setProperty("cpu", toJuceString(provenance.cpu));
    object->setProperty(
        "memoryBytes", provenance.memoryBytes.has_value()
                           ? juce::var(static_cast<juce::int64>(*provenance.memoryBytes))
                           : juce::var());
    object->setProperty("audio", encodeAudio(provenance.audio));
    object->setProperty("instrumentationVersion", toJuceString(provenance.instrumentationVersion));
    const auto overheadStatus = [&provenance]
    {
        switch (provenance.instrumentationOverheadStatus)
        {
        case InstrumentationOverheadStatus::documented:
            return "documented";
        case InstrumentationOverheadStatus::measured:
            return "measured";
        case InstrumentationOverheadStatus::unknown:
            return "unknown";
        }
        return "unknown";
    }();
    object->setProperty("instrumentationOverheadStatus", overheadStatus);
    return object.release();
}

[[nodiscard]] RunProvenance decodeProvenance(const juce::var& value, std::uint32_t schemaVersion)
{
    const auto& memory = value["memoryBytes"];
    auto overheadStatus = InstrumentationOverheadStatus::unknown;
    if (schemaVersion >= 2)
    {
        const auto encodedStatus = value["instrumentationOverheadStatus"].toString();
        if (encodedStatus == "documented")
            overheadStatus = InstrumentationOverheadStatus::documented;
        else if (encodedStatus == "measured")
            overheadStatus = InstrumentationOverheadStatus::measured;
    }
    return {
        value["applicationVersion"].toString().toStdString(),
        value["commit"].toString().toStdString(),
        value["buildType"].toString().toStdString(),
        value["operatingSystem"].toString().toStdString(),
        value["cpu"].toString().toStdString(),
        memory.isVoid() ? std::nullopt
                        : std::optional<std::uint64_t>(static_cast<juce::int64>(memory)),
        decodeAudio(value["audio"]),
        value["instrumentationVersion"].toString().toStdString(),
        overheadStatus};
}

template <typename Item, typename Encoder>
[[nodiscard]] juce::var encodeArray(const std::vector<Item>& items, Encoder encodeItem)
{
    juce::Array<juce::var> values;
    for (const auto& item : items)
        values.add(encodeItem(item));
    return values;
}

[[nodiscard]] juce::var encodeTrial(const TrialMeasurement& trial)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("trialIndex", static_cast<int>(trial.trialIndex));
    object->setProperty(
        "startedAtNanoseconds",
        static_cast<juce::int64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(trial.startedAt.time_since_epoch())
                .count()));
    object->setProperty("durationNanoseconds", static_cast<juce::int64>(trial.duration.count()));
    object->setProperty(
        "samples", encodeArray(
                       trial.samples,
                       [](const MetricSample& sample)
                       {
                           auto value = std::make_unique<juce::DynamicObject>();
                           value->setProperty("metricId", toJuceString(sample.metricId));
                           value->setProperty("value", sample.value);
                           value->setProperty("unit", toJuceString(sample.unit));
                           return juce::var(value.release());
                       }));
    object->setProperty("deadlineMisses", static_cast<juce::int64>(trial.deadlineMisses));
    object->setProperty("dropouts", static_cast<juce::int64>(trial.dropouts));
    object->setProperty("underruns", static_cast<juce::int64>(trial.underruns));
    return object.release();
}

[[nodiscard]] TrialMeasurement decodeTrial(const juce::var& value)
{
    TrialMeasurement trial;
    trial.trialIndex = static_cast<std::uint32_t>(static_cast<int>(value["trialIndex"]));
    trial.startedAt = std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::nanoseconds(static_cast<juce::int64>(value["startedAtNanoseconds"]))));
    trial.duration =
        std::chrono::nanoseconds(static_cast<juce::int64>(value["durationNanoseconds"]));
    if (const auto* samples = value["samples"].getArray())
        for (const auto& sample : *samples)
            trial.samples.push_back(
                {sample["metricId"].toString().toStdString(), static_cast<double>(sample["value"]),
                 sample["unit"].toString().toStdString()});
    trial.deadlineMisses =
        static_cast<std::uint64_t>(static_cast<juce::int64>(value["deadlineMisses"]));
    trial.dropouts = static_cast<std::uint64_t>(static_cast<juce::int64>(value["dropouts"]));
    trial.underruns = static_cast<std::uint64_t>(static_cast<juce::int64>(value["underruns"]));
    return trial;
}

[[nodiscard]] juce::var encodeSummary(const MetricSummary& summary)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("metricId", toJuceString(summary.metricId));
    object->setProperty("unit", toJuceString(summary.unit));
    object->setProperty("sampleCount", static_cast<juce::int64>(summary.sampleCount));
    object->setProperty("median", summary.median);
    object->setProperty("tailPercentile", summary.tailPercentile);
    object->setProperty("minimum", summary.minimum);
    object->setProperty("maximum", summary.maximum);
    object->setProperty("variability", summary.variability);
    return object.release();
}

[[nodiscard]] MetricSummary decodeSummary(const juce::var& value)
{
    return {
        value["metricId"].toString().toStdString(),
        value["unit"].toString().toStdString(),
        static_cast<std::size_t>(static_cast<juce::int64>(value["sampleCount"])),
        static_cast<double>(value["median"]),
        static_cast<double>(value["tailPercentile"]),
        static_cast<double>(value["minimum"]),
        static_cast<double>(value["maximum"]),
        static_cast<double>(value["variability"])};
}
} // namespace

juce::var BenchmarkRecordCodec::encode(const BenchmarkRunRecord& record)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("schemaVersion", static_cast<int>(record.schemaVersion));
    object->setProperty("runId", toJuceString(record.runId));
    object->setProperty("status", static_cast<int>(record.status));
    object->setProperty("configuration", encodeConfiguration(record.configuration));
    object->setProperty("provenance", encodeProvenance(record.provenance));
    object->setProperty("trials", encodeArray(record.trials, encodeTrial));
    object->setProperty("summaries", encodeArray(record.summaries, encodeSummary));
    object->setProperty(
        "warnings", encodeArray(
                        record.warnings,
                        [](const BenchmarkWarning& warning)
                        {
                            auto value = std::make_unique<juce::DynamicObject>();
                            value->setProperty("code", static_cast<int>(warning.code));
                            value->setProperty("message", toJuceString(warning.message));
                            return juce::var(value.release());
                        }));
    object->setProperty(
        "statusDetail", record.statusDetail.has_value()
                            ? juce::var(toJuceString(*record.statusDetail))
                            : juce::var());
    return object.release();
}

RecordDecodeResult BenchmarkRecordCodec::decode(const juce::var& value)
{
    if (value.getDynamicObject() == nullptr)
        return {.error = "Benchmark record must be an object"};

    const auto schemaVersion = static_cast<int>(value["schemaVersion"]);
    if (schemaVersion > static_cast<int>(benchmarkRecordSchemaVersion))
        return {
            .status = RecordDecodeStatus::newerSchema,
            .error = "Benchmark record uses a newer schema"};
    if (schemaVersion <= 0)
        return {.error = "Benchmark record schema is invalid"};

    BenchmarkRunRecord record;
    record.schemaVersion = static_cast<std::uint32_t>(schemaVersion);
    record.runId = value["runId"].toString().toStdString();
    record.status = static_cast<RunStatus>(static_cast<int>(value["status"]));
    record.configuration = decodeConfiguration(value["configuration"]);
    record.provenance = decodeProvenance(value["provenance"], record.schemaVersion);
    if (const auto* trials = value["trials"].getArray())
        for (const auto& trial : *trials)
            record.trials.push_back(decodeTrial(trial));
    if (const auto* summaries = value["summaries"].getArray())
        for (const auto& summary : *summaries)
            record.summaries.push_back(decodeSummary(summary));
    if (const auto* warnings = value["warnings"].getArray())
        for (const auto& warning : *warnings)
            record.warnings.push_back(
                {static_cast<WarningCode>(static_cast<int>(warning["code"])),
                 warning["message"].toString().toStdString()});
    if (!value["statusDetail"].isVoid())
        record.statusDetail = value["statusDetail"].toString().toStdString();
    return {.status = RecordDecodeStatus::loaded, .record = std::move(record)};
}
} // namespace performance