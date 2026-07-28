#pragma once

#include "BenchmarkTypes.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <optional>
#include <string>

namespace performance
{
inline constexpr auto unknownProvenanceValue = "unknown";

struct PlatformMetadata
{
    std::string operatingSystem;
    std::string cpu;
    std::optional<std::uint64_t> memoryBytes;
    AudioConfiguration audio;
};

class PlatformMetadataSource
{
  public:
    virtual ~PlatformMetadataSource() = default;
    [[nodiscard]] virtual PlatformMetadata detect() const = 0;
};

class JucePlatformMetadataSource final : public PlatformMetadataSource
{
  public:
    explicit JucePlatformMetadataSource(juce::AudioDeviceManager* audioDeviceManager = nullptr);
    [[nodiscard]] PlatformMetadata detect() const override;

  private:
    juce::AudioDeviceManager* audioDeviceManager;
};

class PlatformProvenanceProvider final
{
  public:
    explicit PlatformProvenanceProvider(const PlatformMetadataSource& source);
    [[nodiscard]] RunProvenance collect() const;

  private:
    const PlatformMetadataSource& source;
};
} // namespace performance