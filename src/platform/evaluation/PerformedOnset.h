#pragma once

#include <cstdint>
#include <vector>

// An onset found in a take: when something was played, how surely, and -- from
// a keyboard -- which keys (no JUCE dependency).
namespace evaluation
{
struct PerformedOnset
{
    // On the take's clock timeline, uncorrected for latency.
    std::int64_t frame = 0;

    // 0-1. From the keyboard, the loudest velocity; from the microphone, how far
    // the attack stood above its surroundings.
    double strength = 1.0;

    // The keys of a keyboard onset; empty for the microphone, whose pitch is
    // #42's to find.
    std::vector<int> pitches;
};
} // namespace evaluation
