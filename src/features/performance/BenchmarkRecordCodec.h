#pragma once

#include "BenchmarkTypes.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>

namespace performance
{
enum class RecordDecodeStatus
{
    loaded,
    invalid,
    newerSchema
};

struct RecordDecodeResult
{
    RecordDecodeStatus status = RecordDecodeStatus::invalid;
    std::optional<BenchmarkRunRecord> record;
    std::string error;
};

class BenchmarkRecordCodec
{
  public:
    [[nodiscard]] static juce::var encode(const BenchmarkRunRecord& record);
    [[nodiscard]] static RecordDecodeResult decode(const juce::var& value);
};
} // namespace performance