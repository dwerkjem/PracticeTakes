#include "TempoMap.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace score
{
namespace
{
constexpr double secondsPerMinute = 60.0;

[[nodiscard]] bool isUsableTempo(double beatsPerMinute) noexcept
{
    return std::isfinite(beatsPerMinute) && beatsPerMinute > 0.0;
}
} // namespace

double ticksToSecondsAt(Tick ticks, double beatsPerMinute) noexcept
{
    if (!isUsableTempo(beatsPerMinute))
    {
        return 0.0;
    }

    const double quarterNotes =
        static_cast<double>(ticks) / static_cast<double>(ticksPerQuarterNote);

    return quarterNotes * secondsPerMinute / beatsPerMinute;
}

TempoMap::TempoMap() : entries_{TempoEntry{0, defaultBeatsPerMinute}}
{
    recomputeCumulativeSeconds();
}

TempoMap::TempoMap(std::vector<TempoEntry> entries) : entries_(std::move(entries))
{
    recomputeCumulativeSeconds();
}

TempoMap TempoMap::build(std::vector<TempoEntry> entries)
{
    // Drop entries that cannot mean anything before doing any ordering work.
    entries.erase(
        std::remove_if(
            entries.begin(), entries.end(),
            [](const TempoEntry& entry) { return !isUsableTempo(entry.beatsPerMinute); }),
        entries.end());

    // Stable sort so that when two entries share a tick, the one written later
    // in the file is still the later of the two afterwards -- which is what
    // makes "last one wins" below mean "the last declaration in the file".
    std::stable_sort(
        entries.begin(), entries.end(),
        [](const TempoEntry& lhs, const TempoEntry& rhs) { return lhs.tick < rhs.tick; });

    std::vector<TempoEntry> deduplicated;
    deduplicated.reserve(entries.size());

    for (const TempoEntry& entry : entries)
    {
        if (!deduplicated.empty() && deduplicated.back().tick == entry.tick)
        {
            deduplicated.back() = entry;
            continue;
        }

        deduplicated.push_back(entry);
    }

    // Invariant 8: the map is non-empty and covers the start of the score, so
    // no consumer ever has to ask "what if there is no tempo here".
    if (deduplicated.empty() || deduplicated.front().tick > 0)
    {
        deduplicated.insert(deduplicated.begin(), TempoEntry{0, defaultBeatsPerMinute});
    }

    return TempoMap{std::move(deduplicated)};
}

void TempoMap::recomputeCumulativeSeconds()
{
    secondsAtEntry_.assign(entries_.size(), 0.0);

    for (std::size_t index = 1; index < entries_.size(); ++index)
    {
        const Tick span = entries_[index].tick - entries_[index - 1].tick;

        secondsAtEntry_[index] =
            secondsAtEntry_[index - 1] + ticksToSecondsAt(span, entries_[index - 1].beatsPerMinute);
    }
}

double TempoMap::beatsPerMinuteAt(Tick tick) const noexcept
{
    // Last entry whose tick is <= the query. entries_ is never empty and always
    // starts at or before tick 0, so this always finds one.
    const auto upper = std::upper_bound(
        entries_.begin(), entries_.end(), tick,
        [](Tick value, const TempoEntry& entry) { return value < entry.tick; });

    if (upper == entries_.begin())
    {
        return entries_.front().beatsPerMinute;
    }

    return std::prev(upper)->beatsPerMinute;
}

double TempoMap::tickToSeconds(Tick tick) const noexcept
{
    const auto upper = std::upper_bound(
        entries_.begin(), entries_.end(), tick,
        [](Tick value, const TempoEntry& entry) { return value < entry.tick; });

    if (upper == entries_.begin())
    {
        // Before the first entry: extrapolate backwards at the first tempo.
        // Only reachable for a negative tick, which is malformed input.
        return ticksToSecondsAt(tick - entries_.front().tick, entries_.front().beatsPerMinute);
    }

    const auto entry = std::prev(upper);
    const auto index = static_cast<std::size_t>(std::distance(entries_.begin(), entry));

    return secondsAtEntry_[index] + ticksToSecondsAt(tick - entry->tick, entry->beatsPerMinute);
}

Tick TempoMap::secondsToTick(double seconds) const noexcept
{
    if (!std::isfinite(seconds) || seconds <= 0.0)
    {
        return 0;
    }

    // Last entry that begins at or before `seconds`.
    const auto upper = std::upper_bound(secondsAtEntry_.begin(), secondsAtEntry_.end(), seconds);

    const auto index =
        upper == secondsAtEntry_.begin()
            ? std::size_t{0}
            : static_cast<std::size_t>(std::distance(secondsAtEntry_.begin(), std::prev(upper)));

    const TempoEntry& entry = entries_[index];
    const double remainingSeconds = seconds - secondsAtEntry_[index];
    const double quarterNotes = remainingSeconds * entry.beatsPerMinute / secondsPerMinute;
    const double ticks = quarterNotes * static_cast<double>(ticksPerQuarterNote);

    return entry.tick + static_cast<Tick>(std::llround(ticks));
}
} // namespace score
