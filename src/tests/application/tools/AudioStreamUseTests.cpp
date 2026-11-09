#include <catch2/catch_test_macros.hpp>

#include "application/tools/AudioStreamUse.h"
#include "application/tools/BuiltInToolCatalog.h"

namespace
{
[[nodiscard]] AudioStreamUse::Use fullWith(std::size_t spectrograms, std::size_t analyzers)
{
    // Sixteen streams, one held by shared pitch detection, the rest by tools.
    return {
        16,
        16 - 1 - spectrograms - analyzers,
        {{toolCountKey("spectrogram"), spectrograms},
         {toolCountKey("harmonic-analyzer"), analyzers}}};
}
} // namespace

TEST_CASE(
    "stream users are counted, largest first, with what is left over",
    "[tools][audio-streams]")
{
    const auto use = fullWith(6, 9);
    CHECK(use.used() == 16);
    CHECK(use.other() == 1);
    CHECK(
        AudioStreamUse::describeUsers(use) ==
        "9 Harmonic Analyzers, 6 Spectrograms and shared pitch detection");
}

TEST_CASE(
    "a tool with nothing open is left out, and one copy is singular",
    "[tools][audio-streams]")
{
    const AudioStreamUse::Use use{
        16, 14, {{toolCountKey("spectrogram"), 1}, {toolCountKey("harmonic-analyzer"), 0}}};
    CHECK(AudioStreamUse::describeUsers(use) == "1 Spectrogram and shared pitch detection");
}

TEST_CASE(
    "slots no tool accounts for beyond one are counted as other listeners",
    "[tools][audio-streams]")
{
    const AudioStreamUse::Use use{16, 10, {{toolCountKey("spectrogram"), 3}}};
    CHECK(use.other() == 3);
    CHECK(AudioStreamUse::describeUsers(use) == "3 Spectrograms and 3 other listeners");
}

TEST_CASE(
    "the alert says how many streams there are, who has them and what to do",
    "[tools][audio-streams]")
{
    CHECK(
        AudioStreamUse::explainFull(fullWith(6, 9)) ==
        "All 16 audio streams are in use, by 9 Harmonic Analyzers, 6 Spectrograms and shared "
        "pitch detection. Close one of them to free a stream. Tuners don't use one.");
}

TEST_CASE("an inconsistent count never underflows", "[tools][audio-streams]")
{
    // More free than capacity, or tools claiming more than is used, cannot
    // happen from the service, but the arithmetic must not wrap if it did.
    const AudioStreamUse::Use use{4, 9, {{toolCountKey("spectrogram"), 3}}};
    CHECK(use.used() == 0);
    CHECK(use.other() == 0);
}
