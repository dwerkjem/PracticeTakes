#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "../pitch/IntonationZone.h"
#include "../take/Take.h"
#include "../transport/ClockMath.h"
#include "ExpectedAttacks.h"
#include "OnsetDetector.h"
#include "PerformedOnset.h"

// Aligning performed onsets to expected attacks, and judging each attack early,
// on time or late (no JUCE dependency).
// openspec/changes/archive/2026-11-08-onset-timing/design.md, decisions 4-6.
namespace evaluation
{
// The three limits a player sets for timing, in milliseconds either way, as a
// singer sets three for pitch: right on the beat (green), on time (blue, and
// what decides early or late), and past what a note is off the beat (red);
// between the last two it is a little early or late (amber). The zones are
// pitch::IntonationZone's four, so timing is coloured as pitch is.
struct TimingLimits
{
    double rightOnMs = 15.0;
    double onTimeMs = 40.0;
    double offBeatMs = 100.0;

    [[nodiscard]] bool operator==(const TimingLimits&) const = default;
};

// What Settings offers, and the gap kept between the bands.
inline constexpr double minimumRightOnMs = 5.0;
inline constexpr double maximumRightOnMs = 50.0;
inline constexpr double minimumOnTimeMs = 10.0;
inline constexpr double maximumOnTimeMs = 150.0;
inline constexpr double minimumOffBeatMs = 20.0;
inline constexpr double maximumOffBeatMs = 300.0;
inline constexpr double timingBandGapMs = 5.0;

// The limits in order: on time kept first, inside its range, since early and
// late depend on it; right on and off the beat give way on either side. A value
// that is not a number takes its default.
[[nodiscard]] inline TimingLimits orderedTimingLimits(TimingLimits limits) noexcept
{
    const TimingLimits defaults;
    const auto orDefault = [](double value, double fallback)
    { return std::isfinite(value) ? value : fallback; };
    TimingLimits result;
    result.onTimeMs =
        std::clamp(orDefault(limits.onTimeMs, defaults.onTimeMs), minimumOnTimeMs, maximumOnTimeMs);
    result.rightOnMs = std::clamp(
        orDefault(limits.rightOnMs, defaults.rightOnMs), minimumRightOnMs,
        std::max(minimumRightOnMs, std::min(maximumRightOnMs, result.onTimeMs - timingBandGapMs)));
    result.offBeatMs = std::clamp(
        orDefault(limits.offBeatMs, defaults.offBeatMs),
        std::max(minimumOffBeatMs, result.onTimeMs + timingBandGapMs), maximumOffBeatMs);
    return result;
}

// Which of the four zones a note `errorMs` off the beat is in, either way.
[[nodiscard]] inline pitch::IntonationZone
timingZoneOf(double errorMs, TimingLimits limits) noexcept
{
    return pitch::zoneOf(errorMs, limits.onTimeMs, limits.rightOnMs, limits.offBeatMs);
}

// Every threshold the judgement uses, so none is a decision hidden in code.
struct TimingSettings
{
    // The player's limits: on time within `onTimeMs`, the rest for colour.
    TimingLimits limits;

    // How far from an attack an onset may be and still be its onset: half the
    // gap to the neighbouring attacks, at least the tolerance, at most this.
    double maximumWindowMs = 250.0;

    // An onset weaker than this makes its attack's judgement uncertain.
    double uncertainStrength = 0.2;

    // The alignment's costs: leaving an attack without an onset, and leaving
    // an onset without an attack. Lower for the second, so a stray sound is
    // left over rather than pulling a real note's match away.
    double missingCost = 1.0;
    double extraCost = 0.6;

    // How a microphone take's onsets are found, its quietest sound included.
    OnsetSettings onsets;
};

enum class TimingVerdict
{
    early,
    onTime,
    late,
    absent
};

struct TimingResult
{
    ExpectedAttack attack;

    // The attack's frame on the take's timeline.
    std::int64_t expectedFrame = 0;

    // The onset matched to it, if any.
    std::optional<PerformedOnset> onset;

    TimingVerdict verdict = TimingVerdict::absent;

    // Positive is late. Corrected for latency. Zero when absent.
    double errorMs = 0.0;
    double errorBeats = 0.0;

    // Its zone under the player's limits, for colour; nothing when absent.
    std::optional<pitch::IntonationZone> zone;

    // The onset was too weak to trust the judgement.
    bool uncertain = false;
};

struct TimingSummary
{
    std::size_t early = 0;
    std::size_t onTime = 0;
    std::size_t late = 0;
    std::size_t absent = 0;
    std::size_t extra = 0;

    // Over matched attacks, in milliseconds.
    double meanErrorMs = 0.0;
    double errorSpreadMs = 0.0;
};

struct TimingReport
{
    std::vector<TimingResult> results;
    std::vector<PerformedOnset> extra;
    TimingSummary summary;
};

// A match of attack `attack` to onset `onset`, both indices.
struct OnsetMatch
{
    std::size_t attack = 0;
    std::size_t onset = 0;
};

// Align onsets to attacks in order, each onset to at most one attack, only
// within each attack's window (`windows`, in frames). Minimises the costs in
// `settings`; both lists must be in time order.
[[nodiscard]] std::vector<OnsetMatch> alignOnsets(
    const std::vector<std::int64_t>& attackFrames,
    const std::vector<std::int64_t>& windows,
    const std::vector<PerformedOnset>& onsets,
    std::int64_t latencyFrames,
    const TimingSettings& settings);

// Judge `attacks` against `onsets` on a take's timeline, through `table` (the
// take's own) at `sampleRate`. `latencyFrames` is subtracted from every
// onset's frame first.
[[nodiscard]] TimingReport judgeTiming(
    const std::vector<ExpectedAttack>& attacks,
    const std::vector<PerformedOnset>& onsets,
    const transport::ClockTable& table,
    double sampleRate,
    std::int64_t latencyFrames,
    const TimingSettings& settings = {});

// Whether a take is judged from its keyboard rather than its microphone: it
// used the keyboard and pressed something.
[[nodiscard]] inline bool judgedFromKeyboard(const take::Take& take) noexcept
{
    return take.usedKeyboard && !take.keyboard.empty();
}

// How late a take's evidence arrives, in frames: the performer hears the click
// late by the output latency, and the microphone hears them late by the input
// latency on top (design.md decision 5).
[[nodiscard]] inline std::int64_t latencyFramesOf(const take::Take& take) noexcept
{
    return judgedFromKeyboard(take)
               ? static_cast<std::int64_t>(take.outputLatencyFrames)
               : static_cast<std::int64_t>(take.inputLatencyFrames) + take.outputLatencyFrames;
}

// The attacks a take covered: from the tick it started at to the frame it
// stopped at, exclusive. A take stopped in bar 8 is not missing bar 9. A take
// with no last frame (0: built by hand rather than recorded) is judged to the
// end of the score.
[[nodiscard]] std::vector<ExpectedAttack>
attacksInTake(const take::Take& take, std::vector<ExpectedAttack> attacks);

// What a take is judged from: its keyboard's onsets when it has keyboard
// events, otherwise its microphone's. The microphone's take the longest to find
// (about a second for three minutes, optimised), and are the same for every
// part, so a caller judging several parts finds them once.
[[nodiscard]] std::vector<PerformedOnset>
onsetsOf(const take::Take& take, const OnsetSettings& settings = {});

// A take's timing against one part of its score, from `onsets` (`onsetsOf`
// the take). Latency is `latencyFramesOf`.
[[nodiscard]] TimingReport judgeTake(
    const take::Take& take,
    std::size_t part,
    const std::vector<PerformedOnset>& onsets,
    const TimingSettings& settings = {});

// The same, finding the onsets.
[[nodiscard]] TimingReport
judgeTake(const take::Take& take, std::size_t part, const TimingSettings& settings = {});
} // namespace evaluation
