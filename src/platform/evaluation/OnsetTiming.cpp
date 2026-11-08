#include "OnsetTiming.h"

#include "KeyboardOnsets.h"
#include "OnsetDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace evaluation
{
namespace
{
// Frames per beat at a tick: the table's frames per tick there, times the
// beat's length in ticks.
[[nodiscard]] double framesPerBeatAt(const transport::ClockTable& table, score::Tick tick)
{
    if (table.empty())
    {
        return 1.0;
    }
    const transport::ClockSegment& segment =
        table.segments.at(transport::segmentForTick(table, static_cast<double>(tick)));
    return static_cast<double>(segment.ticksPerBeat) * segment.framesPerTick;
}
} // namespace

std::vector<OnsetMatch> alignOnsets(
    const std::vector<std::int64_t>& attackFrames,
    const std::vector<std::int64_t>& windows,
    const std::vector<PerformedOnset>& onsets,
    std::int64_t latencyFrames,
    const TimingSettings& settings)
{
    const std::size_t attackCount = attackFrames.size();
    const std::size_t onsetCount = onsets.size();
    std::vector<OnsetMatch> matches;
    if (attackCount == 0 || onsetCount == 0)
    {
        return matches;
    }

    // The least cost of aligning the first i attacks with the first j onsets,
    // a row at a time: two rows of costs, and one byte per state for the way
    // back. A take is at most a few thousand of each, so the bytes are a few
    // megabytes at most, and this runs once, after the take, on the message
    // thread.
    constexpr double infinite = std::numeric_limits<double>::infinity();
    const std::size_t width = onsetCount + 1;
    std::vector<unsigned char> step((attackCount + 1) * width, 0);
    const auto at = [width](std::size_t attack, std::size_t onset)
    { return (attack * width) + onset; };
    std::vector<double> row(width, infinite);
    std::vector<double> next(width, infinite);

    row[0] = 0.0;
    for (std::size_t attack = 0; attack <= attackCount; ++attack)
    {
        // Within the row: an onset left without an attack.
        for (std::size_t onset = 0; onset < onsetCount; ++onset)
        {
            if (row[onset] + settings.extraCost < row[onset + 1])
            {
                row[onset + 1] = row[onset] + settings.extraCost;
                step[at(attack, onset + 1)] = 2;
            }
        }
        if (attack == attackCount)
        {
            break;
        }

        std::fill(next.begin(), next.end(), infinite);
        for (std::size_t onset = 0; onset <= onsetCount; ++onset)
        {
            const double here = row[onset];
            if (here == infinite)
            {
                continue;
            }
            // This attack has no onset.
            if (here + settings.missingCost < next[onset])
            {
                next[onset] = here + settings.missingCost;
                step[at(attack + 1, onset)] = 1;
            }
            // This onset is this attack's, if it is inside the window.
            if (onset < onsetCount)
            {
                const auto error =
                    static_cast<double>(onsets[onset].frame - latencyFrames - attackFrames[attack]);
                const auto window = static_cast<double>(std::max<std::int64_t>(1, windows[attack]));
                const double matched = here + (std::abs(error) / window);
                if (std::abs(error) <= window && matched < next[onset + 1])
                {
                    next[onset + 1] = matched;
                    step[at(attack + 1, onset + 1)] = 3;
                }
            }
        }
        std::swap(row, next);
    }

    // Back from the end, collecting the matches.
    std::size_t attack = attackCount;
    std::size_t onset = onsetCount;
    while (attack > 0 || onset > 0)
    {
        switch (step[at(attack, onset)])
        {
        case 1:
            --attack;
            break;
        case 2:
            --onset;
            break;
        case 3:
            --attack;
            --onset;
            matches.push_back(OnsetMatch{attack, onset});
            break;
        default:
            attack = 0;
            onset = 0;
            break;
        }
    }
    std::reverse(matches.begin(), matches.end());
    return matches;
}

TimingReport judgeTiming(
    const std::vector<ExpectedAttack>& attacks,
    const std::vector<PerformedOnset>& onsets,
    const transport::ClockTable& table,
    double sampleRate,
    std::int64_t latencyFrames,
    const TimingSettings& settings)
{
    TimingReport report;
    if (sampleRate <= 0.0)
    {
        return report;
    }
    const double framesPerMs = sampleRate / 1000.0;

    std::vector<std::int64_t> frames;
    std::vector<std::int64_t> tolerances;
    frames.reserve(attacks.size());
    for (const ExpectedAttack& attack : attacks)
    {
        frames.push_back(transport::frameOfTick(table, static_cast<double>(attack.tick)));
        const double beatMs = framesPerBeatAt(table, attack.tick) / framesPerMs;
        const double toleranceMs = std::clamp(
            settings.toleranceBeatFraction * beatMs, settings.minimumToleranceMs,
            settings.maximumToleranceMs);
        tolerances.push_back(static_cast<std::int64_t>(std::llround(toleranceMs * framesPerMs)));
    }

    // Each attack's window: half the gap to its neighbours, between its
    // tolerance and the maximum.
    const auto maximumWindow =
        static_cast<std::int64_t>(std::llround(settings.maximumWindowMs * framesPerMs));
    std::vector<std::int64_t> windows(frames.size());
    for (std::size_t index = 0; index < frames.size(); ++index)
    {
        std::int64_t gap = maximumWindow * 2;
        if (index > 0)
        {
            gap = std::min(gap, frames[index] - frames[index - 1]);
        }
        if (index + 1 < frames.size())
        {
            gap = std::min(gap, frames[index + 1] - frames[index]);
        }
        windows[index] =
            std::clamp(gap / 2, tolerances[index], std::max(tolerances[index], maximumWindow));
    }

    const std::vector<OnsetMatch> matches =
        alignOnsets(frames, windows, onsets, latencyFrames, settings);

    report.results.reserve(attacks.size());
    for (std::size_t index = 0; index < attacks.size(); ++index)
    {
        TimingResult result;
        result.attack = attacks[index];
        result.expectedFrame = frames[index];
        report.results.push_back(std::move(result));
    }

    std::vector<bool> used(onsets.size(), false);
    double sum = 0.0;
    double sumOfSquares = 0.0;
    std::size_t matched = 0;
    for (const OnsetMatch& match : matches)
    {
        TimingResult& result = report.results[match.attack];
        const PerformedOnset& onset = onsets[match.onset];
        used[match.onset] = true;
        result.onset = onset;
        const auto error = static_cast<double>(onset.frame - latencyFrames - result.expectedFrame);
        result.errorMs = error / framesPerMs;
        result.errorBeats = error / framesPerBeatAt(table, result.attack.tick);
        const auto tolerance = static_cast<double>(tolerances[match.attack]);
        result.verdict = error < -tolerance  ? TimingVerdict::early
                         : error > tolerance ? TimingVerdict::late
                                             : TimingVerdict::onTime;
        result.uncertain = onset.strength < settings.uncertainStrength;
        sum += result.errorMs;
        sumOfSquares += result.errorMs * result.errorMs;
        ++matched;
    }

    for (std::size_t index = 0; index < onsets.size(); ++index)
    {
        if (!used[index])
        {
            report.extra.push_back(onsets[index]);
        }
    }

    TimingSummary& summary = report.summary;
    for (const TimingResult& result : report.results)
    {
        switch (result.verdict)
        {
        case TimingVerdict::early:
            ++summary.early;
            break;
        case TimingVerdict::onTime:
            ++summary.onTime;
            break;
        case TimingVerdict::late:
            ++summary.late;
            break;
        case TimingVerdict::absent:
            ++summary.absent;
            break;
        }
    }
    summary.extra = report.extra.size();
    if (matched > 0)
    {
        const auto count = static_cast<double>(matched);
        summary.meanErrorMs = sum / count;
        summary.errorSpreadMs = std::sqrt(
            std::max(0.0, (sumOfSquares / count) - (summary.meanErrorMs * summary.meanErrorMs)));
    }
    return report;
}

std::vector<PerformedOnset> onsetsOf(const take::Take& take)
{
    if (judgedFromKeyboard(take))
    {
        return keyboardOnsetsOf(take.keyboard, take.sampleRate);
    }
    return detectOnsets(take.audio.data(), take.audio.size(), take.sampleRate, take.audioStart);
}

TimingReport judgeTake(
    const take::Take& take,
    std::size_t part,
    const std::vector<PerformedOnset>& onsets,
    const TimingSettings& settings)
{
    if (take.score == nullptr || take.table == nullptr)
    {
        return {};
    }
    return judgeTiming(
        expectedAttacksOf(*take.score, part), onsets, *take.table, take.sampleRate,
        latencyFramesOf(take), settings);
}

TimingReport judgeTake(const take::Take& take, std::size_t part, const TimingSettings& settings)
{
    if (take.score == nullptr || take.table == nullptr)
    {
        return {};
    }
    return judgeTake(take, part, onsetsOf(take), settings);
}
} // namespace evaluation
