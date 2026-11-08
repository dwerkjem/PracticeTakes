#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../audio/RealtimeSafety.h"
#include "../score/MusicalTime.h"
#include "../score/Score.h"
#include "../score/TempoMap.h"

// Every timing rule of the practice clock, with no JUCE dependency, so each is
// tested without a device or a display (openspec/changes/archive/2026-11-08-practice-clock).
//
// Two halves, split by thread:
//
//  - **Building** (message thread). `meterChangesOf` reads a Score's measures
//    and `buildTable` flattens the tempo map and the meter into a `ClockTable`:
//    at most `maximumSegments` POD segments plus the loop and the end of the
//    piece, all converted to frames at one tempo scale and sample rate. These
//    may allocate; nothing on the audio thread calls them.
//
//  - **Reading** (audio thread). Everything that takes a `const ClockTable&` is
//    a binary search plus a multiply, allocates nothing, takes no lock, and is
//    annotated non-blocking, because the renderer calls it from the callback.
//
// The clock's position is an integer frame count. Nothing here accumulates a
// floating-point position: a frame is always computed from a segment's exact
// start frame plus one multiply, so the thousandth beat or the ten-thousandth
// loop lands where the first one did.
namespace transport
{
using Frame = std::int64_t;
using score::Tick;

// A score with more tempo or meter changes than this keeps the first ones and
// says so. No real score comes close; the bound is what keeps the table, and so
// the audio thread's work, a fixed size.
inline constexpr std::size_t maximumSegments = 512;

// The tempo scale, in whole percent of the score's tempo. The maintainer chose
// 5%-200% in 5% steps (design.md, Decisions on the open questions).
inline constexpr int minimumScalePercent = 5;
inline constexpr int maximumScalePercent = 200;
inline constexpr int scaleStepPercent = 5;
inline constexpr int defaultScalePercent = 100;

// A count-in is a whole number of bars, from none to four.
inline constexpr int maximumCountInBars = 4;
inline constexpr int defaultCountInBars = 1;

// The nearest allowed scale: clamped to the range and rounded to a step.
[[nodiscard]] constexpr int clampScalePercent(int percent) noexcept
{
    const int clamped = std::clamp(percent, minimumScalePercent, maximumScalePercent);
    const int steps = (clamped + scaleStepPercent / 2) / scaleStepPercent;
    return std::clamp(steps * scaleStepPercent, minimumScalePercent, maximumScalePercent);
}

[[nodiscard]] constexpr int clampCountInBars(int bars) noexcept
{
    return std::clamp(bars, 0, maximumCountInBars);
}

// Where the meter's bar grid starts afresh: a run of measures with the same
// bar length and beat length, from `tick` onwards.
struct MeterChange
{
    Tick tick = 0;

    // Zero-based index of the measure that starts at `tick`.
    std::int64_t measureIndex = 0;

    // A bar's length. Usually the time signature's, but a bar the importer
    // widened to fit its notes is longer, and the clock follows the bar.
    Tick ticksPerBar = score::ticksPerWholeNote;

    // One click's length: a note of the time signature's beat type, so 7/8
    // clicks seven eighths and 6/8 six. Grouping compound meters is a
    // subdivision question, which this change leaves out (design.md, 4).
    Tick ticksPerBeat = score::ticksPerQuarterNote;

    // The time signature's numerator, for display ("beat 3 of 7").
    int beatsPerBar = 4;
};

// One run of the timeline with a constant tempo and a regular bar grid.
//
// The renderer reads these, so they are plain values: no pointer, nothing to
// free, safe to copy into a preallocated array.
struct ClockSegment
{
    // The first tick this segment covers; it runs to the next segment's.
    Tick startTick = 0;

    // The frame of `startTick` at the table's scale and rate, computed from the
    // tempo map's exact seconds rather than by adding up segments.
    Frame startFrame = 0;

    // Frames per tick at this segment's tempo and the table's scale.
    double framesPerTick = 1.0;

    // A bar line at or before `startTick`, from which bars repeat every
    // `ticksPerBar`. A tempo change in the middle of a bar starts a segment
    // whose origin is that bar's start, so the beats keep counting.
    Tick barOriginTick = 0;
    std::int64_t barOriginIndex = 0;
    Tick ticksPerBar = score::ticksPerWholeNote;
    Tick ticksPerBeat = score::ticksPerQuarterNote;
    int beatsPerBar = 4;
};

// Everything the audio thread needs to play one score at one tempo scale.
struct ClockTable
{
    std::array<ClockSegment, maximumSegments> segments{};
    std::size_t segmentCount = 0;

    // True when the score had more changes than `maximumSegments`.
    bool truncated = false;

    double sampleRate = 44100.0;
    int scalePercent = defaultScalePercent;

    // The end of the piece, as a tick and a frame. Reaching it stops the clock.
    Tick endTick = 0;
    Frame endFrame = 0;

    // The loop, in ticks as set and in whole frames converted once.
    bool loopEnabled = false;
    Tick loopStartTick = 0;
    Tick loopEndTick = 0;
    Frame loopStartFrame = 0;
    Frame loopEndFrame = 0;

    [[nodiscard]] bool empty() const noexcept
    {
        return segmentCount == 0;
    }
};

// Round half away from zero, without the libm call: the audio thread uses it.
[[nodiscard]] inline Frame roundToFrame(double value) noexcept PRACTICE_TAKES_NONBLOCKING
{
    return static_cast<Frame>(value < 0.0 ? -std::floor(-value + 0.5) : std::floor(value + 0.5));
}

// Floor division for a possibly negative numerator and a positive divisor.
[[nodiscard]] constexpr std::int64_t
floorDivide(std::int64_t numerator, std::int64_t divisor) noexcept
{
    const std::int64_t quotient = numerator / divisor;
    return (numerator % divisor != 0 && numerator < 0) ? quotient - 1 : quotient;
}

// ---------------------------------------------------------------------------
// Building, on the message thread.
// ---------------------------------------------------------------------------

// The meter as the clock follows it: one entry where the bar grid changes,
// taken from the first part's measures (invariant 1 makes every part agree on
// where measures start and how long they are). A score with no measures gets
// one 4/4 run from tick 0, so an empty score still has a beat.
[[nodiscard]] inline std::vector<MeterChange> meterChangesOf(const score::Score& source)
{
    std::vector<MeterChange> changes;
    score::TimeSignature time;

    if (!source.parts.empty())
    {
        for (const score::Measure& measure : source.parts.front().measures)
        {
            if (measure.attributes.time.has_value() && measure.attributes.time->beats > 0 &&
                measure.attributes.time->beatType > 0)
            {
                time = *measure.attributes.time;
            }

            MeterChange change;
            change.tick = measure.start;
            change.measureIndex = static_cast<std::int64_t>(measure.index);
            change.ticksPerBeat = std::max<Tick>(1, score::ticksPerWholeNote / time.beatType);
            change.ticksPerBar =
                measure.nominalDuration > 0 ? measure.nominalDuration : score::nominalTicks(time);
            change.beatsPerBar = time.beats;

            if (!changes.empty() && changes.back().ticksPerBar == change.ticksPerBar &&
                changes.back().ticksPerBeat == change.ticksPerBeat &&
                changes.back().beatsPerBar == change.beatsPerBar)
            {
                continue;
            }

            changes.push_back(change);
        }
    }

    if (changes.empty())
    {
        changes.push_back(MeterChange{});
    }

    return changes;
}

// The frame of `tick` straight from the tempo map, at a scale and rate. What a
// segment's start frame and the loop's bounds are computed from.
[[nodiscard]] inline Frame
frameOfTickExact(const score::TempoMap& tempoMap, Tick tick, int scalePercent, double sampleRate)
{
    const double scale = static_cast<double>(clampScalePercent(scalePercent)) / 100.0;
    return roundToFrame(tempoMap.tickToSeconds(tick) / scale * sampleRate);
}

// Flatten a tempo map and a meter into `table` at one scale and rate.
//
// A segment starts at every tempo entry and every meter change. Loop bounds and
// the end of the piece are converted here, once; the renderer never converts
// them again. `loopEndTick <= loopStartTick` means no loop.
inline void buildTable(
    ClockTable& table,
    const score::TempoMap& tempoMap,
    const std::vector<MeterChange>& meter,
    Tick endTick,
    int scalePercent,
    double sampleRate,
    Tick loopStartTick = 0,
    Tick loopEndTick = 0)
{
    const int scale = clampScalePercent(scalePercent);
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;

    table.segmentCount = 0;
    table.truncated = false;
    table.sampleRate = rate;
    table.scalePercent = scale;

    // Every boundary, in order, with the meter and tempo in force at each.
    std::vector<Tick> boundaries;
    boundaries.reserve(tempoMap.entries().size() + meter.size());
    for (const auto& entry : tempoMap.entries())
    {
        boundaries.push_back(std::max<Tick>(0, entry.tick));
    }
    for (const auto& change : meter)
    {
        boundaries.push_back(std::max<Tick>(0, change.tick));
    }
    boundaries.push_back(0);
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    const MeterChange fallbackMeter{};

    for (const Tick boundary : boundaries)
    {
        if (table.segmentCount == maximumSegments)
        {
            table.truncated = true;
            break;
        }

        // The meter run in force at this boundary: the last one at or before.
        const MeterChange* inForce = meter.empty() ? &fallbackMeter : &meter.front();
        for (const auto& change : meter)
        {
            if (change.tick <= boundary)
            {
                inForce = &change;
            }
        }

        ClockSegment segment;
        segment.startTick = boundary;
        segment.startFrame = frameOfTickExact(tempoMap, boundary, scale, rate);
        segment.framesPerTick = score::ticksToSecondsAt(1, tempoMap.beatsPerMinuteAt(boundary)) /
                                (static_cast<double>(scale) / 100.0) * rate;
        segment.ticksPerBar = std::max<Tick>(1, inForce->ticksPerBar);
        segment.ticksPerBeat = std::max<Tick>(1, inForce->ticksPerBeat);
        segment.beatsPerBar = std::max(1, inForce->beatsPerBar);

        // The bar line at or before the boundary, counted on from the run's
        // first measure, so a mid-bar tempo change keeps the beat count.
        const Tick barsIntoRun =
            floorDivide(std::max<Tick>(0, boundary - inForce->tick), segment.ticksPerBar);
        segment.barOriginTick = inForce->tick + barsIntoRun * segment.ticksPerBar;
        segment.barOriginIndex = inForce->measureIndex + barsIntoRun;

        table.segments[table.segmentCount++] = segment;
    }

    table.endTick = std::max<Tick>(0, endTick);
    table.endFrame = frameOfTickExact(tempoMap, table.endTick, scale, rate);

    table.loopEnabled = loopEndTick > loopStartTick;
    table.loopStartTick = table.loopEnabled ? std::max<Tick>(0, loopStartTick) : 0;
    table.loopEndTick = table.loopEnabled ? loopEndTick : 0;
    table.loopStartFrame =
        table.loopEnabled ? frameOfTickExact(tempoMap, table.loopStartTick, scale, rate) : 0;
    table.loopEndFrame =
        table.loopEnabled ? frameOfTickExact(tempoMap, table.loopEndTick, scale, rate) : 0;

    // A loop shorter than a frame cannot wrap; treat it as none.
    if (table.loopEnabled && table.loopEndFrame <= table.loopStartFrame)
    {
        table.loopEnabled = false;
    }
}

// The usual case: a whole score.
inline void buildTable(
    ClockTable& table,
    const score::Score& source,
    int scalePercent,
    double sampleRate,
    Tick loopStartTick = 0,
    Tick loopEndTick = 0)
{
    buildTable(
        table, source.tempoMap, meterChangesOf(source), score::totalLength(source), scalePercent,
        sampleRate, loopStartTick, loopEndTick);
}

// ---------------------------------------------------------------------------
// Reading, on the audio thread (and anywhere else).
// ---------------------------------------------------------------------------

// Index of the segment holding `tick`. A tick before the first segment is in it.
[[nodiscard]] inline std::size_t
segmentForTick(const ClockTable& table, double tick) noexcept PRACTICE_TAKES_NONBLOCKING
{
    std::size_t low = 0;
    std::size_t high = table.segmentCount;
    // Last segment whose startTick <= tick.
    while (high - low > 1)
    {
        const std::size_t middle = low + (high - low) / 2;
        if (static_cast<double>(table.segments[middle].startTick) <= tick)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

// Index of the segment holding `frame`. A frame before the first is in it.
[[nodiscard]] inline std::size_t
segmentForFrame(const ClockTable& table, Frame frame) noexcept PRACTICE_TAKES_NONBLOCKING
{
    std::size_t low = 0;
    std::size_t high = table.segmentCount;
    while (high - low > 1)
    {
        const std::size_t middle = low + (high - low) / 2;
        if (table.segments[middle].startFrame <= frame)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

// The frame of a tick, possibly fractional, at the table's scale.
[[nodiscard]] inline Frame
frameOfTick(const ClockTable& table, double tick) noexcept PRACTICE_TAKES_NONBLOCKING
{
    if (table.empty())
    {
        return 0;
    }
    const ClockSegment& segment = table.segments[segmentForTick(table, tick)];
    return segment.startFrame +
           roundToFrame((tick - static_cast<double>(segment.startTick)) * segment.framesPerTick);
}

// The tick at a frame, fractional, at the table's scale. The inverse of
// `frameOfTick` to within a frame.
[[nodiscard]] inline double
tickAtFrame(const ClockTable& table, Frame frame) noexcept PRACTICE_TAKES_NONBLOCKING
{
    if (table.empty())
    {
        return 0.0;
    }
    const ClockSegment& segment = table.segments[segmentForFrame(table, frame)];
    return static_cast<double>(segment.startTick) +
           static_cast<double>(frame - segment.startFrame) / segment.framesPerTick;
}

// Where a position is in the music: bar, beat, and whether that beat is the
// accented one.
struct BeatPosition
{
    // Zero-based measure index; bar "1" to a person is index 0.
    std::int64_t bar = 0;

    // Zero-based beat within the bar.
    int beat = 0;

    bool accented = true;
};

namespace detail
{
// Beats in one bar of a segment: the bar's length in beats, rounded up, so a
// widened bar's last part-beat still clicks.
[[nodiscard]] inline Tick
beatsInBar(const ClockSegment& segment) noexcept PRACTICE_TAKES_NONBLOCKING
{
    return (segment.ticksPerBar + segment.ticksPerBeat - 1) / segment.ticksPerBeat;
}

// A beat in a segment's grid, counted from its bar origin.
struct GridBeat
{
    std::int64_t barsFromOrigin = 0;
    Tick beatInBar = 0;
};

[[nodiscard]] inline Tick
tickOfBeat(const ClockSegment& segment, const GridBeat& beat) noexcept PRACTICE_TAKES_NONBLOCKING
{
    return segment.barOriginTick + beat.barsFromOrigin * segment.ticksPerBar +
           beat.beatInBar * segment.ticksPerBeat;
}

// The beat at or before `tick` in a segment's grid.
[[nodiscard]] inline GridBeat
beatAtOrBefore(const ClockSegment& segment, Tick tick) noexcept PRACTICE_TAKES_NONBLOCKING
{
    GridBeat beat;
    beat.barsFromOrigin = floorDivide(tick - segment.barOriginTick, segment.ticksPerBar);
    const Tick intoBar = tick - segment.barOriginTick - beat.barsFromOrigin * segment.ticksPerBar;
    beat.beatInBar = std::min(intoBar / segment.ticksPerBeat, beatsInBar(segment) - 1);
    return beat;
}

[[nodiscard]] inline GridBeat
nextBeat(const ClockSegment& segment, GridBeat beat) noexcept PRACTICE_TAKES_NONBLOCKING
{
    if (++beat.beatInBar >= beatsInBar(segment))
    {
        beat.beatInBar = 0;
        ++beat.barsFromOrigin;
    }
    return beat;
}
} // namespace detail

// Bar and beat at a frame: the beat whose start is at or before it.
[[nodiscard]] inline BeatPosition
beatAtFrame(const ClockTable& table, Frame frame) noexcept PRACTICE_TAKES_NONBLOCKING
{
    if (table.empty())
    {
        return {};
    }
    const double tick = tickAtFrame(table, frame);
    const ClockSegment& segment = table.segments[segmentForTick(table, tick)];
    const auto beat = detail::beatAtOrBefore(segment, static_cast<Tick>(std::floor(tick)));
    return {
        segment.barOriginIndex + beat.barsFromOrigin, static_cast<int>(beat.beatInBar),
        beat.beatInBar == 0};
}

// One click the metronome should start.
struct Click
{
    Frame frame = 0;
    bool accented = false;
    BeatPosition position;
};

// The first click at or after `fromFrame`, in the score's timeline. Bounded: it
// visits at most every segment once and a handful of beats in each.
[[nodiscard]] inline Click
nextClickAtOrAfter(const ClockTable& table, Frame fromFrame) noexcept PRACTICE_TAKES_NONBLOCKING
{
    Click click;
    if (table.empty())
    {
        click.frame = fromFrame;
        return click;
    }

    std::size_t index = segmentForFrame(table, fromFrame);
    const double startTick = std::max(
        static_cast<double>(table.segments[index].startTick), tickAtFrame(table, fromFrame));
    auto beat =
        detail::beatAtOrBefore(table.segments[index], static_cast<Tick>(std::floor(startTick)));

    // Each pass advances one beat or one segment. A beat lands in one segment
    // and a segment holds at least one boundary, so this bound is never reached
    // for a table that was built by buildTable; it is here so that no input can
    // make the audio thread loop without end.
    const std::size_t limit = table.segmentCount * 4 + 16;
    for (std::size_t pass = 0; pass < limit; ++pass)
    {
        const ClockSegment& segment = table.segments[index];
        const Tick beatTick = detail::tickOfBeat(segment, beat);

        if (beatTick < segment.startTick)
        {
            beat = detail::nextBeat(segment, beat);
            continue;
        }

        if (index + 1 < table.segmentCount && beatTick >= table.segments[index + 1].startTick)
        {
            ++index;
            beat = detail::beatAtOrBefore(table.segments[index], table.segments[index].startTick);
            continue;
        }

        const Frame frame =
            segment.startFrame +
            roundToFrame(static_cast<double>(beatTick - segment.startTick) * segment.framesPerTick);
        if (frame >= fromFrame)
        {
            click.frame = frame;
            click.accented = beat.beatInBar == 0;
            click.position = {
                segment.barOriginIndex + beat.barsFromOrigin, static_cast<int>(beat.beatInBar),
                beat.beatInBar == 0};
            return click;
        }

        beat = detail::nextBeat(segment, beat);
    }

    // Unreachable for a well-formed table: report "no click soon".
    click.frame = fromFrame + static_cast<Frame>(table.sampleRate * 60.0);
    return click;
}

// Whether a click starts inside [frame, frame + count), and which.
[[nodiscard]] inline bool
clickStartsIn(const ClockTable& table, Frame frame, Frame count, Click& click) noexcept
    PRACTICE_TAKES_NONBLOCKING
{
    if (table.empty() || count <= 0)
    {
        return false;
    }
    click = nextClickAtOrAfter(table, frame);
    return click.frame < frame + count;
}

// The frame at which a position in one table continues in another: the same
// score position, under the new table's scale. What keeps a tempo-scale change
// from jumping (design.md, "A tempo-scale change is a republish").
[[nodiscard]] inline Frame
reanchor(const ClockTable& from, const ClockTable& to, Frame frame) noexcept
    PRACTICE_TAKES_NONBLOCKING
{
    if (from.empty() || to.empty())
    {
        return frame;
    }
    return frameOfTick(to, tickAtFrame(from, frame));
}

// One step of the loop: a position at or past the loop's end goes back by
// exactly the loop's length in frames, as many times as it takes. Returns how
// many times it wrapped.
[[nodiscard]] inline std::int64_t
wrapIntoLoop(const ClockTable& table, Frame& position) noexcept PRACTICE_TAKES_NONBLOCKING
{
    if (!table.loopEnabled || position < table.loopEndFrame)
    {
        return 0;
    }
    const Frame length = table.loopEndFrame - table.loopStartFrame;
    const std::int64_t wraps = (position - table.loopStartFrame) / length;
    position -= wraps * length;
    return wraps;
}

// A count-in: `bars` bars of the meter and tempo in force at a start point,
// ending exactly on that point's frame.
struct CountIn
{
    // Where the take begins, and where the count-in ends.
    Frame targetFrame = 0;

    // Where the count-in starts: `beats` beats before the target.
    Frame startFrame = 0;

    double framesPerBeat = 0.0;
    int beatsPerBar = 4;
    int beats = 0;
};

[[nodiscard]] inline CountIn
countInBefore(const ClockTable& table, Tick startTick, int bars) noexcept PRACTICE_TAKES_NONBLOCKING
{
    CountIn countIn;
    if (table.empty())
    {
        return countIn;
    }
    const ClockSegment& segment =
        table.segments[segmentForTick(table, static_cast<double>(startTick))];
    countIn.targetFrame = frameOfTick(table, static_cast<double>(startTick));
    countIn.framesPerBeat = static_cast<double>(segment.ticksPerBeat) * segment.framesPerTick;
    countIn.beatsPerBar = segment.beatsPerBar;
    countIn.beats = clampCountInBars(bars) * segment.beatsPerBar;
    countIn.startFrame = countIn.targetFrame -
                         roundToFrame(static_cast<double>(countIn.beats) * countIn.framesPerBeat);
    return countIn;
}

// The frame of count-in beat `beat` (zero-based, beat 0 first). Computed from
// the target, so the last beat is exactly one beat before it.
[[nodiscard]] inline Frame
countInBeatFrame(const CountIn& countIn, int beat) noexcept PRACTICE_TAKES_NONBLOCKING
{
    return countIn.targetFrame -
           roundToFrame(static_cast<double>(countIn.beats - beat) * countIn.framesPerBeat);
}

// The first count-in click at or after `fromFrame`; false when none is left.
[[nodiscard]] inline bool
nextCountInClick(const CountIn& countIn, Frame fromFrame, Click& click) noexcept
    PRACTICE_TAKES_NONBLOCKING
{
    if (countIn.beats <= 0 || countIn.framesPerBeat <= 0.0)
    {
        return false;
    }
    const double beatsBeforeTarget =
        static_cast<double>(countIn.targetFrame - fromFrame) / countIn.framesPerBeat;
    // The earliest beat whose frame is >= fromFrame; step back one in case
    // rounding put it a frame earlier.
    int beat = countIn.beats - static_cast<int>(std::floor(beatsBeforeTarget));
    beat = std::clamp(beat - 1, 0, countIn.beats);
    for (; beat < countIn.beats; ++beat)
    {
        const Frame frame = countInBeatFrame(countIn, beat);
        if (frame >= fromFrame)
        {
            click.frame = frame;
            const int beatInBar = beat % std::max(1, countIn.beatsPerBar);
            click.accented = beatInBar == 0;
            click.position = {
                -1 - (countIn.beats - beat - 1) / std::max(1, countIn.beatsPerBar), beatInBar,
                beatInBar == 0};
            return true;
        }
    }
    return false;
}
} // namespace transport
