#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "../take/Take.h"
#include "../transport/ClockMath.h"
#include "ExpectedAttacks.h"
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

// A take's timing against one part of its score: from the keyboard when the
// take has keyboard events, otherwise from the microphone's audio. Latency is
// the take's stated latencies (design.md decision 5).
[[nodiscard]] TimingReport
judgeTake(const take::Take& take, std::size_t part, const TimingSettings& settings = {});
} // namespace evaluation
