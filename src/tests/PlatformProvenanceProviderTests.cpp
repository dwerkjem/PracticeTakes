#include "features/performance/PlatformProvenanceProvider.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
class FakePlatformMetadataSource final : public performance::PlatformMetadataSource
{
  public:
    [[nodiscard]] performance::PlatformMetadata detect() const override
    {
        return metadata;
    }
    performance::PlatformMetadata metadata;
};
} // namespace

TEST_CASE("platform provenance combines build platform and audio metadata")
{
    FakePlatformMetadataSource source;
    source.metadata = {
        "Test OS",
        "Test CPU",
        8ULL * 1024 * 1024 * 1024,
        {"Test Interface", "Test Backend", 48000.0, 256}};
    performance::PlatformProvenanceProvider provider(source);

    const auto provenance = provider.collect();

    REQUIRE_FALSE(provenance.applicationVersion.empty());
    REQUIRE_FALSE(provenance.commit.empty());
    REQUIRE_FALSE(provenance.buildType.empty());
    REQUIRE(provenance.operatingSystem == "Test OS");
    REQUIRE(provenance.cpu == "Test CPU");
    REQUIRE(provenance.memoryBytes == 8ULL * 1024 * 1024 * 1024);
    REQUIRE(provenance.audio.deviceName == "Test Interface");
    REQUIRE(provenance.audio.backend == "Test Backend");
    REQUIRE(provenance.audio.sampleRateHz == 48000.0);
    REQUIRE(provenance.audio.bufferSizeFrames == 256);
    REQUIRE_FALSE(provenance.instrumentationVersion.empty());
}

TEST_CASE("platform provenance explicitly marks unavailable values")
{
    FakePlatformMetadataSource source;
    performance::PlatformProvenanceProvider provider(source);

    const auto provenance = provider.collect();

    REQUIRE(provenance.operatingSystem == performance::unknownProvenanceValue);
    REQUIRE(provenance.cpu == performance::unknownProvenanceValue);
    REQUIRE_FALSE(provenance.memoryBytes.has_value());
    REQUIRE(provenance.audio.deviceName == performance::unknownProvenanceValue);
    REQUIRE(provenance.audio.backend == performance::unknownProvenanceValue);
    REQUIRE(provenance.audio.sampleRateHz == 0.0);
    REQUIRE(provenance.audio.bufferSizeFrames == 0);
}

TEST_CASE("JUCE platform provenance detects the host without an audio device")
{
    performance::JucePlatformMetadataSource source;
    const auto metadata = source.detect();

    REQUIRE_FALSE(metadata.operatingSystem.empty());
    REQUIRE_FALSE(metadata.cpu.empty());
    REQUIRE(metadata.audio.deviceName == performance::unknownProvenanceValue);
    REQUIRE(metadata.audio.backend == performance::unknownProvenanceValue);
}