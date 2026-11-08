#pragma once

#include <algorithm>
#include <vector>

#include "../take/Take.h"
#include "PerformedOnset.h"

// A keyboard take's onsets: its note-ons, gathered into chords (no JUCE
// dependency). openspec/changes/archive/2026-11-08-onset-timing/design.md, decision 2.
namespace evaluation
{
// Note-ons within this long of the first of a group are one chord.
inline constexpr double chordSpreadSeconds = 0.030;

[[nodiscard]] inline std::vector<PerformedOnset>
keyboardOnsetsOf(const std::vector<take::KeyboardEvent>& events, double sampleRate)
{
    std::vector<take::KeyboardEvent> noteOns;
    for (const take::KeyboardEvent& event : events)
    {
        if (event.event.kind == midi::MidiEventKind::noteOn && event.event.velocity > 0)
        {
            noteOns.push_back(event);
        }
    }
    std::stable_sort(
        noteOns.begin(), noteOns.end(),
        [](const take::KeyboardEvent& left, const take::KeyboardEvent& right)
        { return left.frame < right.frame; });

    const auto spread = static_cast<std::int64_t>(chordSpreadSeconds * sampleRate);
    std::vector<PerformedOnset> onsets;
    for (const take::KeyboardEvent& noteOn : noteOns)
    {
        const double strength = std::clamp(noteOn.event.velocity / 127.0, 0.0, 1.0);
        if (!onsets.empty() && noteOn.frame - onsets.back().frame <= spread)
        {
            onsets.back().pitches.push_back(noteOn.event.noteNumber);
            onsets.back().strength = std::max(onsets.back().strength, strength);
            continue;
        }
        onsets.push_back(PerformedOnset{noteOn.frame, strength, {noteOn.event.noteNumber}});
    }
    return onsets;
}
} // namespace evaluation
