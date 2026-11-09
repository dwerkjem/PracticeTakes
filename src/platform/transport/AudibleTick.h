#pragma once

#include <algorithm>
#include <cstdint>

#include "ClockMath.h"
#include "ClockRenderer.h"

// Where in the score the listener is now: the clock's frame, extrapolated to
// this instant and moved back by the output latency, as a tick (no JUCE
// dependency). openspec/changes/archive/2026-11-09-now-and-next/design.md, decision 2.
namespace transport
{
// The tick heard at `nowMilliseconds` on the high-resolution clock.
// - Playing: `timing` extrapolated to now, less `outputLatencyFrames`, through
//   `table`. So it moves smoothly between the clock's own updates, and changes
//   note when the note is heard rather than when it is rendered.
// - Otherwise, or without a table: `clockTick`, where the clock rests.
[[nodiscard]] inline double audibleTick(
    double clockTick,
    bool playing,
    const ClockTiming& timing,
    const ClockTable* table,
    double nowMilliseconds,
    int outputLatencyFrames) noexcept
{
    if (!playing || table == nullptr || table->empty() || timing.sampleRate <= 0.0)
    {
        return clockTick;
    }
    const Frame frame = timing.frameAt(nowMilliseconds) - std::max(0, outputLatencyFrames);
    return tickAtFrame(*table, frame);
}

// How many beats of the meter from `start` to `end`, at `start`'s meter.
[[nodiscard]] inline double beatsBetween(const ClockTable& table, double start, double end) noexcept
{
    if (table.empty() || end <= start)
    {
        return 0.0;
    }
    const ClockSegment& segment = table.segments[segmentForTick(table, start)];
    return (end - start) / static_cast<double>(std::max<Tick>(1, segment.ticksPerBeat));
}

// Seconds of playing, at the table's scale, from `from` to `to` (zero if past).
[[nodiscard]] inline double secondsBetween(const ClockTable& table, double from, double to) noexcept
{
    if (table.empty() || to <= from || table.sampleRate <= 0.0)
    {
        return 0.0;
    }
    return static_cast<double>(frameOfTick(table, to) - frameOfTick(table, from)) /
           table.sampleRate;
}
} // namespace transport
