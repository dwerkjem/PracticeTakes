#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

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
// Every threshold the judgement uses, so none is a decision hidden in code.
struct TimingSettings
{
    // On time: within this fraction of a beat, clamped between the two.
    double toleranceBeatFraction = 0.08;
    double minimumToleranceMs = 30.0;
    double maximumToleranceMs = 80.0;

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
