#pragma once

#include "BenchmarkRecordCodec.h"

#include <juce_core/juce_core.h>

namespace performance
{
enum class RecordStoreStatus
{
    succeeded,
    alreadyExists,
    notFound,
    invalid,
    newerSchema,
    ioError
};

struct StoredRecordResult
{
    RecordStoreStatus status = RecordStoreStatus::ioError;
    std::optional<BenchmarkRunRecord> record;
    std::string error;
};

class BenchmarkRecordStore
{
  public:
    explicit BenchmarkRecordStore(juce::File directory);

    [[nodiscard]] RecordStoreStatus save(const BenchmarkRunRecord& record) const;
    [[nodiscard]] StoredRecordResult load(std::string_view runId) const;
    [[nodiscard]] std::vector<std::string> listRunIds() const;

  private:
    [[nodiscard]] juce::File fileFor(std::string_view runId) const;

    juce::File directory;
};

[[nodiscard]] RecordStoreStatus
exportBenchmarkRecord(const BenchmarkRunRecord& record, const juce::File& destination);
} // namespace performance