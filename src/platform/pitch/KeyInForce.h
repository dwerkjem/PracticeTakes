#pragma once

#include "../score/Score.h"
#include "CurrentTarget.h"

#include <cstddef>
#include <optional>

// The key signature every tool spells and draws in (no JUCE dependency).
//
// It is the key of the measure the music is at, not the key the score opens
// in: an A natural in a bar of G major is in G major, and a tool showing it
// shows one sharp, even if the piece began in C. Which measure that is: the
// target's, when the target is a note of the score; otherwise the measure the
// practice clock is in. With no score open, the reader's own key (Settings ->
// Practice), which `withoutScore` carries.
namespace pitch
{
// The key in force in measure `measureIndex` of `part`: the last key change at
// or before it, C major when the part names none.
[[nodiscard]] inline int keyFifthsAtMeasure(const score::Part& part, std::size_t measureIndex)
{
    int fifths = 0;
    const std::size_t last = std::min(measureIndex + 1, part.measures.size());
    for (std::size_t index = 0; index < last; ++index)
    {
        if (const auto& key = part.measures[index].attributes.key; key.has_value())
        {
            fifths = key->fifths;
        }
    }
    return fifths;
}

// The measure of `part` sounding at `tick`: the last that starts at or before
// it, the first before the music starts.
[[nodiscard]] inline std::size_t measureAtTick(const score::Part& part, double tick)
{
    std::size_t found = 0;
    for (std::size_t index = 0; index < part.measures.size(); ++index)
    {
        if (static_cast<double>(part.measures[index].start) > tick)
        {
            break;
        }
        found = index;
    }
    return found;
}

[[nodiscard]] inline int
keyInForce(const score::Score* score, const Target* target, double clockTick, int withoutScore)
{
    if (score == nullptr || score->parts.empty())
    {
        return withoutScore;
    }
    if (target != nullptr && target->place.has_value() &&
        target->place->partIndex < score->parts.size())
    {
        return keyFifthsAtMeasure(
            score->parts[target->place->partIndex], target->place->note.measureIndex);
    }
    const score::Part& first = score->parts.front();
    return keyFifthsAtMeasure(first, measureAtTick(first, clockTick));
}
} // namespace pitch
