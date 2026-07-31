#include "BenchmarkRecordStore.h"

#include <algorithm>

namespace performance
{
namespace
{
[[nodiscard]] juce::String jsonFor(const BenchmarkRunRecord& record)
{
    return juce::JSON::toString(BenchmarkRecordCodec::encode(record), true);
}

[[nodiscard]] bool isValidRunId(std::string_view runId)
{
    return !runId.empty() && runId.front() != '~' &&
           runId.find_first_of("/\\:") == std::string_view::npos &&
           runId.find("..") == std::string_view::npos;
}

[[nodiscard]] bool writeFile(const juce::File& file, const juce::String& text)
{
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr || !stream->openedOk() || !stream->writeText(text, false, false, "\n"))
    {
        return false;
    }
    stream->flush();
    return stream->getStatus().wasOk();
}
} // namespace

BenchmarkRecordStore::BenchmarkRecordStore(juce::File directoryToUse)
    : directory(std::move(directoryToUse))
{
}

RecordStoreStatus BenchmarkRecordStore::save(const BenchmarkRunRecord& record) const
{
    if (!isValidRunId(record.runId))
    {
        return RecordStoreStatus::invalid;
    }
    if (!directory.createDirectory().wasOk())
    {
        return RecordStoreStatus::ioError;
    }
    const auto file = fileFor(record.runId);
    if (file.exists())
    {
        return RecordStoreStatus::alreadyExists;
    }
    return writeFile(file, jsonFor(record)) ? RecordStoreStatus::succeeded
                                            : RecordStoreStatus::ioError;
}

StoredRecordResult BenchmarkRecordStore::load(std::string_view runId) const
{
    if (!isValidRunId(runId))
    {
        return {
            .status = RecordStoreStatus::invalid,
            .error = "Benchmark record identifier is not a valid record name"};
    }
    const auto file = fileFor(runId);
    if (!file.existsAsFile())
    {
        return {.status = RecordStoreStatus::notFound, .error = "Benchmark record was not found"};
    }
    const auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (parsed.isVoid())
    {
        return {.status = RecordStoreStatus::invalid, .error = "Benchmark record JSON is corrupt"};
    }
    auto decoded = BenchmarkRecordCodec::decode(parsed);
    if (decoded.status == RecordDecodeStatus::newerSchema)
    {
        return {.status = RecordStoreStatus::newerSchema, .error = std::move(decoded.error)};
    }
    if (decoded.status != RecordDecodeStatus::loaded || !decoded.record.has_value())
    {
        return {.status = RecordStoreStatus::invalid, .error = std::move(decoded.error)};
    }
    return {.status = RecordStoreStatus::succeeded, .record = std::move(decoded.record)};
}

std::vector<std::string> BenchmarkRecordStore::listRunIds() const
{
    std::vector<std::string> result;
    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*.json"))
    {
        result.push_back(file.getFileNameWithoutExtension().toStdString());
    }
    std::sort(result.begin(), result.end());
    return result;
}

juce::File BenchmarkRecordStore::fileFor(std::string_view runId) const
{
    return directory.getChildFile(
        juce::String::fromUTF8(runId.data(), static_cast<int>(runId.size())) + ".json");
}

RecordStoreStatus
exportBenchmarkRecord(const BenchmarkRunRecord& record, const juce::File& destination)
{
    if (!destination.getParentDirectory().createDirectory().wasOk())
    {
        return RecordStoreStatus::ioError;
    }
    juce::TemporaryFile temporary(destination);
    if (!writeFile(temporary.getFile(), jsonFor(record)))
    {
        return RecordStoreStatus::ioError;
    }
    return temporary.overwriteTargetFileWithTemporary()
               ? RecordStoreStatus::succeeded
               : RecordStoreStatus::ioError;
}
} // namespace performance