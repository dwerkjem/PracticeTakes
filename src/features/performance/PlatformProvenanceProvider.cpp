#include "PlatformProvenanceProvider.h"

#ifndef PRACTICE_TAKES_BUILD_TYPE
#define PRACTICE_TAKES_BUILD_TYPE "unknown"
#endif

#ifndef PRACTICE_TAKES_COMMIT
#define PRACTICE_TAKES_COMMIT "unknown"
#endif

#ifndef PRACTICE_TAKES_INSTRUMENTATION_VERSION
#define PRACTICE_TAKES_INSTRUMENTATION_VERSION "unknown"
#endif

#ifndef PRACTICE_TAKES_VERSION
#define PRACTICE_TAKES_VERSION "unknown"
#endif

namespace performance
{
namespace
{
std::string knownOrUnknown(const juce::String& value)
{
    return value.isNotEmpty() ? value.toStdString() : unknownProvenanceValue;
}

std::string knownOrUnknown(std::string value)
{
    return value.empty() ? unknownProvenanceValue : std::move(value);
}
} // namespace

JucePlatformMetadataSource::JucePlatformMetadataSource(juce::AudioDeviceManager* audioDeviceManager)
    : audioDeviceManager(audioDeviceManager)
{
}

PlatformMetadata JucePlatformMetadataSource::detect() const
{
    PlatformMetadata metadata;
    metadata.operatingSystem = knownOrUnknown(juce::SystemStats::getOperatingSystemName());
    metadata.cpu = knownOrUnknown(juce::SystemStats::getCpuModel());
    const auto memoryMegabytes = juce::SystemStats::getMemorySizeInMegabytes();
    if (memoryMegabytes > 0)
        metadata.memoryBytes = static_cast<std::uint64_t>(memoryMegabytes) * 1024 * 1024;

    if (audioDeviceManager == nullptr)
    {
        metadata.audio.deviceName = unknownProvenanceValue;
        metadata.audio.backend = unknownProvenanceValue;
        return metadata;
    }

    if (auto* device = audioDeviceManager->getCurrentAudioDevice())
    {
        metadata.audio.deviceName = knownOrUnknown(device->getName());
        metadata.audio.backend = knownOrUnknown(device->getTypeName());
        metadata.audio.sampleRateHz = device->getCurrentSampleRate();
        metadata.audio.bufferSizeFrames =
            static_cast<std::uint32_t>(juce::jmax(0, device->getCurrentBufferSizeSamples()));
    }
    else
    {
        metadata.audio.deviceName = unknownProvenanceValue;
        metadata.audio.backend = unknownProvenanceValue;
    }
    return metadata;
}

PlatformProvenanceProvider::PlatformProvenanceProvider(const PlatformMetadataSource& source)
    : source(source)
{
}

RunProvenance PlatformProvenanceProvider::collect() const
{
    auto metadata = source.detect();
    metadata.operatingSystem = knownOrUnknown(std::move(metadata.operatingSystem));
    metadata.cpu = knownOrUnknown(std::move(metadata.cpu));
    metadata.audio.deviceName = knownOrUnknown(std::move(metadata.audio.deviceName));
    metadata.audio.backend = knownOrUnknown(std::move(metadata.audio.backend));
    return {PRACTICE_TAKES_VERSION,    PRACTICE_TAKES_COMMIT,
            PRACTICE_TAKES_BUILD_TYPE, std::move(metadata.operatingSystem),
            std::move(metadata.cpu),   metadata.memoryBytes,
            std::move(metadata.audio), PRACTICE_TAKES_INSTRUMENTATION_VERSION};
}
} // namespace performance